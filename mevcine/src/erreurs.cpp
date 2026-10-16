#include "erreurs.h"

#include "app/a_propos.h"
#include "i18n.h"
#include "prefs.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>

using namespace Qt::StringLiterals;

namespace erreurs {

namespace {

QtMessageHandler g_gestionnaire_precedent = nullptr;
std::atomic_flag g_deja_signale = ATOMIC_FLAG_INIT;

/// ajoute l'erreur au journal ; renvoie le chemin du journal
QString ecrit_journal(const QString &message)
{
    const QString journal = prefs::fichier_journal_erreurs();
    QDir().mkpath(prefs::dossier_conf());
    QFile f(journal);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        const QString date = prefs::horodatage(QDateTime::currentSecsSinceEpoch());
        f.write(u"[%1] MeVCine %2 (C++, Qt)\n%3\n\n"_s.arg(date, a_propos::VERSION, message).toUtf8());
    }
    return QDir::toNativeSeparators(journal);
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

} // namespace

void installe()
{
    std::set_terminate(sur_terminate);
    g_gestionnaire_precedent = qInstallMessageHandler(sur_message);
}

void signale(const QString &message)
{
    // une seule boîte de dialogue, même si plusieurs fils d'exécution s'arrêtent en même temps
    if (!g_deja_signale.test_and_set()) {
        const QString journal = ecrit_journal(message);
        QString texte = trad("MeVCine a rencontré une erreur interne et doit s'arrêter.\n\n{}\n\nLe détail est dans le "
                             "fichier :\n{}",
                             message, journal);
        if (QFileInfo(prefs::fichier_secours()).isFile())
            texte += u"\n\n"_s
                + trad("Le travail en cours a été sauvegardé automatiquement : relancez MeVCine, puis "
                       "Fichier › Récupérer le travail précédent…");
        std::fprintf(stderr, "%s\n", qUtf8Printable(texte));
        // boîte de message seulement depuis le fil de l'interface
        if (qobject_cast<QApplication *>(QCoreApplication::instance())
            && QThread::currentThread() == QCoreApplication::instance()->thread())
            QMessageBox::critical(nullptr, trad("Erreur interne de MeVCine"), texte);
    }
    std::abort();
}

} // namespace erreurs
