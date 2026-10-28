#include "erreurs.h"

#include "app/a_propos.h"
#include "fichier.h"
#include "i18n.h"
#include "prefs.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define ERREURS_PILE 1
#endif
#endif

using namespace Qt::StringLiterals;

namespace erreurs {

namespace {

QtMessageHandler g_gestionnaire_precedent = nullptr;
std::atomic_flag g_deja_signale = ATOMIC_FLAG_INIT;
QString g_relance; // programme relancé pour montrer la boîte (vide : aucun)

/// fin de l'en-tête d'une erreur du journal, après la date
QString suite_entete()
{
    return u" MeVCine %1 (C++, Qt)\n"_s.arg(a_propos::VERSION);
}

/// ajoute l'erreur au journal ; renvoie le chemin du journal, vide si l'écriture a échoué
QString ecrit_journal(const QString &message)
{
    const QString journal = prefs::fichier_journal_erreurs();
    QDir().mkpath(prefs::dossier_conf());
    QFile f(journal);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return {};
    const QString date = prefs::horodatage(QDateTime::currentSecsSinceEpoch());
    if (!ecrit_tout(f, (u"[%1]"_s.arg(date) + suite_entete() + message + u"\n\n"_s).toUtf8()) || !f.flush())
        return {};
    return QDir::toNativeSeparators(journal);
}

/// la boîte de dialogue de l'erreur (et sa copie sur la sortie d'erreur)
void montre(const QString &message, const QString &journal)
{
    QString texte = journal.isEmpty()
        ? trad("MeVCine a rencontré une erreur interne et doit s'arrêter.\n\n{}\n\nLe détail n'a pas pu être "
               "enregistré.",
               message)
        : trad("MeVCine a rencontré une erreur interne et doit s'arrêter.\n\n{}\n\nLe détail est dans le "
               "fichier :\n{}",
               message, journal);
    // la sauvegarde automatique peut dater un peu (ou d'une séance précédente) : on ne promet pas
    // qu'elle contient tout
    if (QFileInfo(prefs::fichier_secours()).isFile())
        texte += u"\n\n"_s
            + trad("Une sauvegarde automatique est disponible : relancez MeVCine, puis Fichier › Récupérer le "
                   "travail précédent… Les toutes dernières modifications peuvent y manquer.");
    std::fprintf(stderr, "%s\n", qUtf8Printable(texte));
    // boîte de message depuis le fil de l'interface ; ailleurs (fil de calcul), c'est une copie du
    // programme qui la montre
    if (qobject_cast<QApplication *>(QCoreApplication::instance())
        && QThread::currentThread() == QCoreApplication::instance()->thread())
        QMessageBox::critical(nullptr, trad("Erreur interne de MeVCine"), texte);
    else if (!g_relance.isEmpty())
        QProcess::startDetached(g_relance, {u"--crash"_s, journal, message});
}

void sur_terminate()
{
    QString message = u"?"_s;
    if (const std::exception_ptr erreur = std::current_exception()) {
        try {
            std::rethrow_exception(erreur);
        } catch (const std::exception &e) {
            message = QString::fromLocal8Bit(e.what());
        } catch (...) {
            message = u"exception"_s;
        }
    }
    signale(message);
}

void sur_message(QtMsgType type, const QMessageLogContext &contexte, const QString &message)
{
    if (type != QtFatalMsg) {
        if (g_gestionnaire_precedent)
            g_gestionnaire_precedent(type, contexte, message);
        return;
    }
    QString detail = message;
    if (contexte.file)
        detail += u" (%1:%2)"_s.arg(QString::fromUtf8(contexte.file)).arg(contexte.line);
    signale(detail);
}

// --- plantage ----------------------------------------------------------------------------------
// Pendant un plantage, la mémoire du programme peut être abîmée : le gestionnaire n'alloue rien et
// n'utilise que des appels du système. Chemin du journal, en-tête et programme à relancer sont
// préparés à l'installation.

QByteArray g_suite_entete;
char g_cause[96]; // « Signal 11 (SIGSEGV) », écrit par le gestionnaire

char *ecrit_texte(char *sortie, const char *texte)
{
    while (*texte)
        *sortie++ = *texte++;
    return sortie;
}

/// nombre en base 10 ou 16, complété par des zéros jusqu'à `largeur` chiffres
char *ecrit_nombre(char *sortie, std::uint64_t valeur, unsigned base, int largeur = 1)
{
    char chiffres[24];
    int n = 0;
    do {
        chiffres[n++] = "0123456789ABCDEF"[valeur % base];
        valeur /= base;
    } while (valeur || n < largeur);
    while (n)
        *sortie++ = chiffres[--n];
    return sortie;
}

/// « [aaaa-mm-jj hh:mm:ss UTC] MeVCine … », comme `ecrit_journal`
char *ecrit_entete(char *sortie)
{
    using namespace std::chrono;
    const auto maintenant = floor<seconds>(system_clock::now());
    const auto jour = floor<days>(maintenant);
    const year_month_day date{jour};
    const hh_mm_ss heure{maintenant - jour};
    sortie = ecrit_nombre(ecrit_texte(sortie, "["), static_cast<std::uint64_t>(static_cast<int>(date.year())), 10, 4);
    sortie = ecrit_nombre(ecrit_texte(sortie, "-"), static_cast<unsigned>(date.month()), 10, 2);
    sortie = ecrit_nombre(ecrit_texte(sortie, "-"), static_cast<unsigned>(date.day()), 10, 2);
    sortie = ecrit_nombre(ecrit_texte(sortie, " "), static_cast<std::uint64_t>(heure.hours().count()), 10, 2);
    sortie = ecrit_nombre(ecrit_texte(sortie, ":"), static_cast<std::uint64_t>(heure.minutes().count()), 10, 2);
    sortie = ecrit_nombre(ecrit_texte(sortie, ":"), static_cast<std::uint64_t>(heure.seconds().count()), 10, 2);
    return ecrit_texte(sortie, " UTC]");
}

#ifdef Q_OS_WIN

std::wstring g_journal;
std::wstring g_programme; // vide : pas de relance
wchar_t g_commande[4 * MAX_PATH + 200];

const char *nom_exception(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    }
    return nullptr;
}

/// faux si l'écriture a échoué (disque plein…)
bool ecrit(HANDLE f, const char *donnees, std::size_t taille)
{
    DWORD ecrits = 0;
    while (taille > 0 && WriteFile(f, donnees, static_cast<DWORD>(taille), &ecrits, nullptr) && ecrits > 0) {
        donnees += ecrits;
        taille -= ecrits;
    }
    return taille == 0;
}

/// une ligne de la pile d'appels : « module(+0xdécalage) »
void ecrit_appel(HANDLE f, void *adresse)
{
    char ligne[4 * MAX_PATH + 40];
    char *fin = ligne;
    HMODULE module = nullptr;
    wchar_t chemin[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(adresse), &module)
        && GetModuleFileNameW(module, chemin, MAX_PATH) > 0) {
        const wchar_t *nom = chemin;
        for (const wchar_t *p = chemin; *p; ++p)
            if (*p == L'\\' || *p == L'/')
                nom = p + 1;
        const int n = WideCharToMultiByte(CP_UTF8, 0, nom, -1, fin, 4 * MAX_PATH, nullptr, nullptr);
        fin += n > 0 ? n - 1 : 0;
        fin = ecrit_texte(ecrit_nombre(ecrit_texte(fin, "(+0x"), reinterpret_cast<std::uintptr_t>(adresse) - reinterpret_cast<std::uintptr_t>(module), 16), ")");
    } else {
        fin = ecrit_nombre(ecrit_texte(fin, "0x"), reinterpret_cast<std::uintptr_t>(adresse), 16);
    }
    *fin++ = '\n';
    ecrit(f, ligne, static_cast<std::size_t>(fin - ligne));
}

/// ajoute `texte` à la commande ; nul si elle déborde
wchar_t *ajoute(wchar_t *sortie, const wchar_t *texte)
{
    for (; sortie && *texte; ++texte) {
        if (sortie == std::end(g_commande) - 1)
            return nullptr;
        *sortie++ = *texte;
    }
    return sortie;
}

LONG WINAPI sur_exception(EXCEPTION_POINTERS *info)
{
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    // exception C++ que rien n'a rattrapée (codes de GCC) : elle repart, et le programme appelle
    // std::terminate (sur_terminate, qui note son message), comme avec le gestionnaire de démarrage
    // de MinGW que celui-ci remplace
    if ((code & 0x20FFFFFF) == 0x20474343 && !(info->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE))
        return EXCEPTION_CONTINUE_EXECUTION;
    // un plantage pendant la boîte d'une erreur fatale : rien de plus
    if (!g_deja_signale.test_and_set()) {
        char *cause = ecrit_nombre(ecrit_texte(g_cause, "Exception 0x"), code, 16);
        if (const char *nom = nom_exception(code))
            cause = ecrit_texte(ecrit_texte(ecrit_texte(cause, " ("), nom), ")");
        *cause = '\0';
        bool note = false;
        const HANDLE f = g_journal.empty() ? INVALID_HANDLE_VALUE
                                           : CreateFileW(g_journal.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                                         FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            char entete[64];
            const bool ecrite = ecrit(f, entete, static_cast<std::size_t>(ecrit_entete(entete) - entete))
                && ecrit(f, g_suite_entete.constData(), static_cast<std::size_t>(g_suite_entete.size()))
                && ecrit(f, g_cause, std::strlen(g_cause)) && ecrit(f, "\n", 1);
            void *appels[62];
            const USHORT n = CaptureStackBackTrace(0, 62, appels, nullptr);
            for (USHORT i = 0; i < n; ++i)
                ecrit_appel(f, appels[i]);
            ecrit(f, "\n", 1);
            note = CloseHandle(f) != 0 && ecrite;
        }
        if (!g_programme.empty()) {
            // "programme" --crash "journal" "cause"
            wchar_t cause_large[std::size(g_cause)];
            std::size_t i = 0;
            for (; g_cause[i]; ++i)
                cause_large[i] = static_cast<wchar_t>(g_cause[i]);
            cause_large[i] = L'\0';
            wchar_t *fin = ajoute(g_commande, L"\"");
            fin = ajoute(fin, g_programme.c_str());
            fin = ajoute(fin, L"\" --crash \"");
            fin = ajoute(fin, note ? g_journal.c_str() : L"");
            fin = ajoute(fin, L"\" \"");
            fin = ajoute(fin, cause_large);
            fin = ajoute(fin, L"\"");
            if (fin) {
                *fin = L'\0';
                STARTUPINFOW demarrage{};
                demarrage.cb = sizeof demarrage;
                PROCESS_INFORMATION processus{};
                if (CreateProcessW(nullptr, g_commande, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &demarrage, &processus)) {
                    CloseHandle(processus.hThread);
                    CloseHandle(processus.hProcess);
                }
            }
        }
    }
    // le programme s'arrête, sans la fenêtre d'erreur du système en plus de la nôtre
    return EXCEPTION_EXECUTE_HANDLER;
}

void installe_plantage(const QString &journal, const QString &programme)
{
    g_journal = QDir::toNativeSeparators(journal).toStdWString();
    g_programme = QDir::toNativeSeparators(programme).toStdWString();
    // débordement de la pile : de quoi écrire le journal et relancer le programme
    ULONG reserve = 64 * 1024;
    SetThreadStackGuarantee(&reserve);
    SetUnhandledExceptionFilter(sur_exception);
}

#else

QByteArray g_journal;
QByteArray g_programme; // vide : pas de relance
alignas(16) char g_pile_signal[1 << 16]; // un débordement de la pile plante aussi

/// faux si l'écriture a échoué (disque plein…)
bool ecrit(int fd, const char *donnees, std::size_t taille)
{
    while (taille > 0) {
        const ssize_t n = ::write(fd, donnees, taille);
        if (n <= 0)
            return false;
        donnees += n;
        taille -= static_cast<std::size_t>(n);
    }
    return true;
}

const char *nom_signal(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
    }
    return "?";
}

/// copie du programme, sans les fonctions d'avant `fork` (verrous de l'allocation de mémoire,
/// peut-être pris par le fil qui plante)
pid_t copie_processus()
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    return ::_Fork();
#else
    return ::fork();
#endif
}

void sur_signal(int sig)
{
    // un plantage pendant la boîte d'une erreur fatale, ou son `abort` : rien de plus
    if (!g_deja_signale.test_and_set()) {
        char *cause = ecrit_nombre(ecrit_texte(g_cause, "Signal "), static_cast<unsigned>(sig), 10);
        *ecrit_texte(ecrit_texte(ecrit_texte(cause, " ("), nom_signal(sig)), ")") = '\0';
        bool note = false;
        const int fd = g_journal.isEmpty() ? -1 : ::open(g_journal.constData(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0) {
            char entete[64];
            const bool ecrite = ecrit(fd, entete, static_cast<std::size_t>(ecrit_entete(entete) - entete))
                && ecrit(fd, g_suite_entete.constData(), static_cast<std::size_t>(g_suite_entete.size()))
                && ecrit(fd, g_cause, std::strlen(g_cause)) && ecrit(fd, "\n", 1);
#ifdef ERREURS_PILE
            void *appels[64];
            backtrace_symbols_fd(appels, backtrace(appels, 64), fd);
#endif
            ecrit(fd, "\n", 1);
            note = ::close(fd) == 0 && ecrite;
        }
        if (!g_programme.isEmpty() && copie_processus() == 0) {
            sigset_t aucun;
            sigemptyset(&aucun);
            sigprocmask(SIG_SETMASK, &aucun, nullptr);
            char option[] = "--crash", sans_journal[] = "";
            char *const argv[] = {const_cast<char *>(g_programme.constData()), option,
                                  note ? const_cast<char *>(g_journal.constData()) : sans_journal, g_cause, nullptr};
            ::execv(argv[0], argv);
            ::_exit(127);
        }
    }
    // fin habituelle : fichier core, rapport du système
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void installe_plantage(const QString &journal, const QString &programme)
{
    g_journal = QFile::encodeName(journal);
    g_programme = QFile::encodeName(programme);
#ifdef ERREURS_PILE
    // premier appel ici : il charge une bibliothèque, ce qui alloue de la mémoire
    void *appel = nullptr;
    backtrace(&appel, 1);
#endif
    stack_t pile{};
    pile.ss_sp = g_pile_signal;
    pile.ss_size = sizeof g_pile_signal;
    ::sigaltstack(&pile, nullptr);
    struct sigaction action {};
    action.sa_handler = sur_signal;
    action.sa_flags = SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        ::sigaction(sig, &action, nullptr);
}

#endif

/// programme relancé pour montrer la boîte. AppImage : l'image elle-même (son dossier monté
/// disparaît avec le programme) ; APPIMAGE passe aussi aux programmes qu'une AppImage lance : il ne
/// compte que pour un programme de son dossier (APPDIR)
QString programme_relance()
{
    const QString programme = QCoreApplication::applicationFilePath();
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (appimage.isEmpty())
        return programme;
    const QString appdir = qEnvironmentVariable("APPDIR");
    if (appdir.isEmpty())
        return appimage;
    const QString dossier = QFileInfo(appdir).canonicalFilePath();
    return !dossier.isEmpty() && programme.startsWith(dossier + u'/') ? appimage : programme;
}

} // namespace

void installe(bool relance)
{
    std::set_terminate(sur_terminate);
    g_gestionnaire_precedent = qInstallMessageHandler(sur_message);
    g_suite_entete = suite_entete().toUtf8();
    QDir().mkpath(prefs::dossier_conf());
    g_relance = relance ? programme_relance() : QString();
    installe_plantage(prefs::fichier_journal_erreurs(), g_relance);
}

void signale(const QString &message)
{
    // une seule boîte de dialogue, même si plusieurs fils d'exécution s'arrêtent en même temps
    if (!g_deja_signale.test_and_set())
        montre(message, ecrit_journal(message));
    std::abort();
}

bool montre_plantage(const QStringList &args)
{
    const qsizetype i = args.indexOf(u"--crash"_s);
    if (i < 0)
        return false;
    const QString journal = args.value(i + 1);
    montre(args.value(i + 2, u"?"_s), journal.isEmpty() ? QString() : QDir::toNativeSeparators(journal));
    return true;
}

} // namespace erreurs
