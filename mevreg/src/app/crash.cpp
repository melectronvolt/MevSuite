#include "crash.h"

#include "app/about.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSysInfo>
#include <QThread>

#include <atomic>
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
#define CRASH_BACKTRACE 1
#endif
#endif

using namespace Qt::StringLiterals;

namespace crash {

namespace {

QString g_log;
QtMessageHandler g_previousHandler = nullptr;
std::atomic_flag g_reporting = ATOMIC_FLAG_INIT;

/// Première ligne du journal : logiciel, version, système.
QString header()
{
    return u"%1 %2 (%3 %4)\n"_s.arg(about::NAME, about::VERSION, QSysInfo::productType(), QSysInfo::currentCpuArchitecture());
}

/// Écrit le journal (remplacé à chaque erreur) ; renvoie son chemin si l'écriture a réussi.
QString writeLog(const QString &message)
{
    if (g_log.isEmpty())
        return {};
    QDir().mkpath(QFileInfo(g_log).absolutePath());
    QFile file(g_log);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return {};
    const QByteArray text = (header() + message + u'\n').toUtf8();
    return file.write(text) == text.size() && file.flush() ? QDir::toNativeSeparators(g_log) : QString();
}

void show(const QString &message, const QString &log)
{
    QString text = u"MeVReg a rencontré une erreur imprévue et doit se fermer.\n\n%1\n\n"_s.arg(message);
    text += log.isEmpty() ? u"Les détails n'ont pas pu être enregistrés."_s : u"Les détails ont été enregistrés dans :\n%1"_s.arg(log);
    std::fprintf(stderr, "%s\n", text.toLocal8Bit().constData());
    // Boîte de message seulement depuis le fil de l'interface, et si l'affichage fonctionne.
    if (qobject_cast<QApplication *>(QCoreApplication::instance())
        && QThread::currentThread() == QCoreApplication::instance()->thread())
        QMessageBox::critical(nullptr, about::NAME, text);
}

void onTerminate()
{
    QString message = u"?"_s;
    if (const std::exception_ptr error = std::current_exception()) {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception &e) {
            message = QString::fromLocal8Bit(e.what());
        } catch (...) {
            message = u"exception"_s;
        }
    }
    report(message);
}

void onMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtFatalMsg) {
        if (g_previousHandler)
            g_previousHandler(type, context, message);
        return;
    }
    QString detail = message;
    if (context.file)
        detail += u" (%1:%2)"_s.arg(QString::fromUtf8(context.file)).arg(context.line);
    report(detail);
}

// --- plantage ----------------------------------------------------------------------------------
// Pendant un plantage, la mémoire du programme peut être abîmée : le gestionnaire n'alloue rien et
// n'utilise que des appels du système. Chemin du journal, en-tête et programme à relancer sont
// préparés à l'installation.

QByteArray g_header;
char g_cause[96]; // « Signal 11 (SIGSEGV) », écrit par le gestionnaire

char *putText(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

/// Nombre en base 10 ou 16, sans zéro devant.
char *putNumber(char *out, std::uint64_t value, unsigned base)
{
    char digits[24];
    int n = 0;
    do {
        digits[n++] = "0123456789ABCDEF"[value % base];
        value /= base;
    } while (value);
    while (n)
        *out++ = digits[--n];
    return out;
}

#ifdef Q_OS_WIN

std::wstring g_logPath;
std::wstring g_program; // vide : pas de relance
wchar_t g_command[4 * MAX_PATH + 200];

const char *exceptionName(DWORD code)
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

void put(HANDLE file, const char *data, std::size_t size)
{
    DWORD written = 0;
    while (size > 0 && WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written > 0) {
        data += written;
        size -= written;
    }
}

/// Une ligne de la pile d'appels : « module(+0xdécalage) ».
void putFrame(HANDLE file, void *address)
{
    char line[4 * MAX_PATH + 40];
    char *end = line;
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &module)
        && GetModuleFileNameW(module, path, MAX_PATH) > 0) {
        const wchar_t *name = path;
        for (const wchar_t *p = path; *p; ++p)
            if (*p == L'\\' || *p == L'/')
                name = p + 1;
        const int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, end, 4 * MAX_PATH, nullptr, nullptr);
        end += n > 0 ? n - 1 : 0;
        end = putText(putNumber(putText(end, "(+0x"), reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module), 16), ")");
    } else {
        end = putNumber(putText(end, "0x"), reinterpret_cast<std::uintptr_t>(address), 16);
    }
    *end++ = '\n';
    put(file, line, static_cast<std::size_t>(end - line));
}

/// Ajoute `text` à la commande ; nul si elle déborde.
wchar_t *append(wchar_t *out, const wchar_t *text)
{
    for (; out && *text; ++text) {
        if (out == std::end(g_command) - 1)
            return nullptr;
        *out++ = *text;
    }
    return out;
}

LONG WINAPI onException(EXCEPTION_POINTERS *info)
{
    // un plantage pendant la boîte d'une erreur fatale : rien de plus
    if (!g_reporting.test_and_set()) {
        const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
        char *cause = putNumber(putText(g_cause, "Exception 0x"), code, 16);
        if (const char *name = exceptionName(code))
            cause = putText(putText(putText(cause, " ("), name), ")");
        *cause = '\0';
        bool logged = false;
        const HANDLE file = g_logPath.empty() ? INVALID_HANDLE_VALUE
                                              : CreateFileW(g_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                                            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            put(file, g_header.constData(), static_cast<std::size_t>(g_header.size()));
            put(file, g_cause, std::strlen(g_cause));
            put(file, "\n", 1);
            void *frames[62];
            const USHORT n = CaptureStackBackTrace(0, 62, frames, nullptr);
            for (USHORT i = 0; i < n; ++i)
                putFrame(file, frames[i]);
            logged = CloseHandle(file) != 0;
        }
        if (!g_program.empty()) {
            // "programme" --crash "journal" "cause"
            wchar_t causeWide[std::size(g_cause)];
            std::size_t i = 0;
            for (; g_cause[i]; ++i)
                causeWide[i] = static_cast<wchar_t>(g_cause[i]);
            causeWide[i] = L'\0';
            wchar_t *end = append(g_command, L"\"");
            end = append(end, g_program.c_str());
            end = append(end, L"\" --crash \"");
            end = append(end, logged ? g_logPath.c_str() : L"");
            end = append(end, L"\" \"");
            end = append(end, causeWide);
            end = append(end, L"\"");
            if (end) {
                *end = L'\0';
                STARTUPINFOW startup{};
                startup.cb = sizeof startup;
                PROCESS_INFORMATION process{};
                if (CreateProcessW(nullptr, g_command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
                    CloseHandle(process.hThread);
                    CloseHandle(process.hProcess);
                }
            }
        }
    }
    // le programme s'arrête, sans la fenêtre d'erreur du système en plus de la nôtre
    return EXCEPTION_EXECUTE_HANDLER;
}

void installCrashHandler(const QString &logPath, bool relaunch)
{
    g_logPath = logPath.isEmpty() ? std::wstring() : QDir::toNativeSeparators(logPath).toStdWString();
    g_program = relaunch ? QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString() : std::wstring();
    SetUnhandledExceptionFilter(onException);
}

#else

QByteArray g_logPath;
QByteArray g_program; // vide : pas de relance
alignas(16) char g_signalStack[1 << 16]; // un débordement de la pile plante aussi

void put(int fd, const char *data, std::size_t size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n <= 0)
            return;
        data += n;
        size -= static_cast<std::size_t>(n);
    }
}

const char *signalName(int sig)
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

/// Copie du programme, sans les fonctions d'avant `fork` (verrous de l'allocation de mémoire,
/// peut-être pris par le fil qui plante).
pid_t forkProcess()
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    return ::_Fork();
#else
    return ::fork();
#endif
}

void onSignal(int sig)
{
    // un plantage pendant la boîte d'une erreur fatale, ou son `abort` : rien de plus
    if (!g_reporting.test_and_set()) {
        char *cause = putNumber(putText(g_cause, "Signal "), static_cast<unsigned>(sig), 10);
        *putText(putText(putText(cause, " ("), signalName(sig)), ")") = '\0';
        bool logged = false;
        const int fd = g_logPath.isEmpty() ? -1 : ::open(g_logPath.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd >= 0) {
            put(fd, g_header.constData(), static_cast<std::size_t>(g_header.size()));
            put(fd, g_cause, std::strlen(g_cause));
            put(fd, "\n", 1);
#ifdef CRASH_BACKTRACE
            void *frames[64];
            backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
#endif
            logged = ::close(fd) == 0;
        }
        if (!g_program.isEmpty() && forkProcess() == 0) {
            sigset_t none;
            sigemptyset(&none);
            sigprocmask(SIG_SETMASK, &none, nullptr);
            char flag[] = "--crash", noLog[] = "";
            char *const argv[] = {const_cast<char *>(g_program.constData()), flag,
                                  logged ? const_cast<char *>(g_logPath.constData()) : noLog, g_cause, nullptr};
            ::execv(argv[0], argv);
            ::_exit(127);
        }
    }
    // fin habituelle : fichier core, rapport du système
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void installCrashHandler(const QString &logPath, bool relaunch)
{
    g_logPath = logPath.isEmpty() ? QByteArray() : QFile::encodeName(logPath);
    // AppImage : l'image elle-même (son dossier monté disparaît avec le programme)
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    g_program = !relaunch ? QByteArray() : QFile::encodeName(appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage);
#ifdef CRASH_BACKTRACE
    // premier appel ici : il charge une bibliothèque, ce qui alloue de la mémoire
    void *frame = nullptr;
    backtrace(&frame, 1);
#endif
    stack_t stack{};
    stack.ss_sp = g_signalStack;
    stack.ss_size = sizeof g_signalStack;
    ::sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_handler = onSignal;
    action.sa_flags = SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        ::sigaction(sig, &action, nullptr);
}

#endif

} // namespace

void install(const QString &logPath, bool relaunch)
{
    g_log = logPath;
    std::set_terminate(onTerminate);
    g_previousHandler = qInstallMessageHandler(onMessage);
    g_header = header().toUtf8();
    if (!logPath.isEmpty())
        QDir().mkpath(QFileInfo(logPath).absolutePath());
    installCrashHandler(logPath, relaunch);
}

void report(const QString &message)
{
    // Une seconde erreur pendant l'affichage de la première (boîte de message) ferme directement.
    if (!g_reporting.test_and_set())
        show(message, writeLog(message));
    std::abort();
}

bool showCrash(const QStringList &args)
{
    const qsizetype i = args.indexOf(u"--crash"_s);
    if (i < 0)
        return false;
    const QString log = args.value(i + 1);
    show(args.value(i + 2, u"?"_s), log.isEmpty() ? QString() : QDir::toNativeSeparators(log));
    return true;
}

} // namespace crash
