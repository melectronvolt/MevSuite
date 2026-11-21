#include "help.h"

#include "savefile.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace help {

namespace {

/// Page à ouvrir pour une langue de l'interface.
QString page(Lang lang)
{
    return code_langue(lang) + u".html"_s;
}

/// Les pages du dépôt pointent vers ../../exemples/. Dans le cache, les WAV sont
/// placés à côté des pages pour que tous les liens fonctionnent hors connexion.
QString ecrit_fichiers(const QString &dossier, Lang langue)
{
    if (!QDir().mkpath(dossier))
        return textes(langue).folderFailed(QDir::toNativeSeparators(dossier));
    for (const QString &nom : fichiers()) {
        QFile ressource(u":/help/"_s + nom);
        if (!ressource.open(QIODevice::ReadOnly))
            return ressource.errorString();
        QByteArray contenu = ressource.readAll();
        if (ressource.error() != QFileDevice::NoError)
            return ressource.errorString();
        if (nom.endsWith(u".html"_s)) {
            contenu.replace("{{version}}", MEVSONO_VERSION);
            contenu.replace("href=\"../../exemples/", "href=\"");
        }

        const QString chemin = QDir(dossier).filePath(nom);
        QFile copie(chemin);
        if (copie.open(QIODevice::ReadOnly) && copie.size() == contenu.size() && copie.readAll() == contenu)
            continue;
        copie.close();

        QSaveFile destination(chemin);
        if (!destination.open(QIODevice::WriteOnly) || !ecrit_tout(destination, contenu) || !destination.commit())
            return destination.errorString();
    }
    return {};
}

} // namespace

QStringList fichiers()
{
    return QDir(u":/help"_s).entryList(QDir::Files);
}

QString dossier()
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cache.isEmpty() ? QString() : QDir(cache).filePath(u"notice"_s);
}

QString ecrit(Lang lang, QString *error)
{
    const QString dir = dossier();
    const QString erreur = dir.isEmpty() ? textes(lang).folderFailed(dir) : ecrit_fichiers(dir, lang);
    if (error)
        *error = erreur;
    return erreur.isEmpty() ? QDir(dir).filePath(page(lang)) : QString();
}

} // namespace help
