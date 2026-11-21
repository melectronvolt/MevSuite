#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTest>

#include <optional>

using namespace Qt::StringLiterals;

namespace outils {

/// Fabrique une vidéo d'essai avec ffmpeg (une seule fois) dans le dossier de compilation ; rien si
/// ce ffmpeg ne le permet pas (encodeur ou filtre absent) : le test est alors sauté.
inline std::optional<QString> video_test(const QString &nom, const QStringList &args)
{
    const QDir dossier(QString::fromUtf8(MEVCINE_VIDEOS_TESTS));
    const QString chemin = dossier.filePath(nom);
    if (QFile::exists(chemin))
        return chemin;
    QDir().mkpath(dossier.path());
    const QString provisoire = dossier.filePath(u"en-cours-%1-%2"_s.arg(QCoreApplication::applicationPid()).arg(nom));
    QProcess p;
    p.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    p.start(u"ffmpeg"_s, QStringList{u"-nostdin"_s, u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-y"_s} + args
                             + QStringList{provisoire});
    const bool ok = p.waitForStarted(-1) && p.waitForFinished(-1) && p.exitStatus() == QProcess::NormalExit
        && p.exitCode() == 0;
    if (ok && QFile::rename(provisoire, chemin))
        return chemin;
    QFile::remove(provisoire);
    if (!ok || !QFile::exists(chemin)) {
        qWarning("vidéo d'essai %s impossible à fabriquer : test sauté", qUtf8Printable(nom));
        return std::nullopt;
    }
    return chemin;
}

/// Mire mobile : le centre du carré est (72, 115) à la troisième image.
inline QString mire(int images)
{
    return video_test(u"mire-%1.avi"_s.arg(images),
                      {u"-f"_s, u"lavfi"_s, u"-i"_s,
                       u"color=gray:s=400x300:r=25[bg];color=white:s=32x32:r=25[fg];"
                       u"[bg][fg]overlay=x=40+8*(n-1):y=99"_s,
                       u"-frames:v"_s, QString::number(images), u"-c:v"_s, u"mpeg4"_s}).value_or(QString());
}

/// Exécute un texte Python suivi d'un contrôle ; sauté si python3 ou numpy manquent. Renvoie le
/// message d'erreur, vide si tout va bien.
inline QString execute_python(const QString &texte, const QString &controle)
{
    QProcess p;
    p.start(u"python3"_s, {u"-I"_s, u"-c"_s, texte + u"\n"_s + controle});
    if (!p.waitForStarted(-1)) {
        qWarning("python3 absent : test sauté");
        return {};
    }
    p.waitForFinished(-1);
    const QString erreurs = QString::fromUtf8(p.readAllStandardError());
    if (erreurs.contains(u"No module named 'numpy'"_s)) {
        qWarning("numpy absent : test sauté");
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return erreurs.isEmpty() ? u"échec de python3"_s : erreurs;
    return {};
}

} // namespace outils
