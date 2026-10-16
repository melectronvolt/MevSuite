#include "chemins.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace chemins {

namespace {

bool est_dossier(const QString &d)
{
    return QFileInfo(d).isDir();
}

/// le dossier de l'exécutable (vide sans QCoreApplication)
QString dossier_programme()
{
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
}

} // namespace

const QStringList EXTENSIONS_VIDEO = {
    u"avi"_s, u"mp4"_s,  u"m4v"_s,  u"mov"_s, u"qt"_s,   u"3gp"_s,  u"3g2"_s,  u"mkv"_s,   u"webm"_s, u"ogv"_s, u"ogg"_s,
    u"ogm"_s, u"mpg"_s,  u"mpeg"_s, u"mts"_s, u"m2ts"_s, u"ts"_s,   u"wmv"_s,  u"asf"_s,   u"flv"_s,  u"f4v"_s, u"lrv"_s,
    u"mxf"_s, u"dv"_s,   u"vob"_s,  u"divx"_s, u"y4m"_s, u"mjpg"_s, u"mjpeg"_s, u"rm"_s,   u"rmvb"_s,
};

QString data_dir()
{
    QStringList candidats;
    if (const QString d = qEnvironmentVariable("MEVCINE_DATA"); !d.isEmpty())
        candidats << d;
    if (const QString exe = dossier_programme(); !exe.isEmpty()) {
        const QDir dir(exe);
        candidats << dir.filePath(u"data"_s);
        candidats << dir.filePath(u"../share/mevcine/data"_s);
        // exécutable dans le dossier de compilation (mevcine/build)
        candidats << dir.filePath(u"../data"_s);
    }
    candidats << u"/usr/share/mevcine/data"_s;
    candidats << QString::fromUtf8(MEVCINE_DONNEES_SOURCES);
    for (const QString &d : candidats) {
        if (est_dossier(QDir(d).filePath(u"video"_s)) || est_dossier(QDir(d).filePath(u"help"_s)))
            return QDir::cleanPath(d);
    }
    return QDir::cleanPath(candidats.last());
}

QString help_dir()
{
    return QDir(data_dir()).filePath(u"help"_s);
}

QString dossier_videos_utilisateur()
{
    for (auto lieu : {QStandardPaths::MoviesLocation, QStandardPaths::DownloadLocation}) {
        const QString d = QStandardPaths::writableLocation(lieu);
        if (!d.isEmpty() && est_dossier(d))
            return d;
    }
    const QString d = QDir::homePath();
    return d.isEmpty() ? u"."_s : d;
}

QString document_dir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return !d.isEmpty() && est_dossier(d) ? d : QDir::homePath();
}

bool ouvre_avec_le_bureau(const QString &chemin)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(chemin));
}

QString absolu(const QString &chemin)
{
    const QFileInfo f(chemin);
    const QString c = f.canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(f.absoluteFilePath()) : c;
}

QString outil(const QString &nom)
{
#ifdef Q_OS_WIN
    const QString exe = nom + u".exe"_s;
#else
    const QString exe = nom;
#endif
    if (const QString d = dossier_programme(); !d.isEmpty()) {
        const QDir dir(QDir(d).filePath(u"ffmpeg"_s));
        for (const QString &p : {dir.filePath(exe), dir.filePath(u"bin/"_s + exe)}) {
            if (QFileInfo(p).isFile())
                return p;
        }
    }
    return nom;
}

QStringList filtre_videos()
{
    QStringList motifs;
    for (const QString &e : EXTENSIONS_VIDEO)
        motifs << u"*."_s + e << u"*."_s + e.toUpper();
    return motifs;
}

} // namespace chemins
