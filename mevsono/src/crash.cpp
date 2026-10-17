#include "crash.h"

#include "about.h"
#include "savefile.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSysInfo>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>

using namespace Qt::StringLiterals;

namespace crash {

namespace {

QString g_log;
Lang g_lang = Lang::En;
QtMessageHandler g_previousHandler = nullptr;
std::atomic_flag g_reporting = ATOMIC_FLAG_INIT;

/// Écrit le journal (remplacé à chaque erreur) ; renvoie son chemin si l'écriture a réussi.
QString writeLog(const QString &message)
{
    if (g_log.isEmpty())
        return {};
    QDir().mkpath(QFileInfo(g_log).absolutePath());
    QFile file(g_log);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return {};
    const QString text = u"%1 %2 (%3 %4)\n%5\n"_s.arg(about::NAME, about::VERSION, QSysInfo::productType(),
                                                       QSysInfo::currentCpuArchitecture(), message);
    return writeAll(file, text.toUtf8()) && file.flush() ? QDir::toNativeSeparators(g_log) : QString();
}

void show(const QString &message, const QString &log)
{
    const QString text = texts(g_lang).fatalError(message, log);
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

} // namespace

void install(const QString &logPath, Lang lang)
{
    g_log = logPath;
    g_lang = lang;
    std::set_terminate(onTerminate);
    g_previousHandler = qInstallMessageHandler(onMessage);
}

void report(const QString &message)
{
    // Une seconde erreur pendant l'affichage de la première (boîte de message) ferme directement.
    if (!g_reporting.test_and_set())
        show(message, writeLog(message));
    std::abort();
}

} // namespace crash
