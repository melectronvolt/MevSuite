// Outils communs aux tests : vidéos d'exemple, vidéos d'essai fabriquées par ffmpeg, exécution d'un
// texte Python.

#pragma once

#include "chemins.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTest>

#include <optional>

using namespace Qt::StringLiterals;

namespace outils {

/// une vidéo d'exemple
inline QString video(const QString &nom)
{
    // vidéos des essais, dans les sources (elles ne sont pas livrées avec le logiciel)
    return QDir(QStringLiteral(MEVCINE_SOURCES "/data/video")).filePath(nom);
}

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
    if (!ok || !QFile::rename(provisoire, chemin)) {
        qWarning("vidéo d'essai %s impossible à fabriquer : test sauté", qUtf8Printable(nom));
        QFile::remove(provisoire);
        return std::nullopt;
    }
    return chemin;
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
