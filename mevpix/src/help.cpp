#include "help.h"

#include "savefile.h"
#include "view.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace help {

namespace {

/// Page à ouvrir pour une langue de l'interface.
QString page(Lang lang)
{
    return code_langue(lang) + u".html"_s;
}

/// Rétablit le contenu livré avec le logiciel. Les deux pages partagent les images de TP.
QString ecrit_fichiers(const QString &dossier, Lang langue)
{
    if (!QDir().mkpath(dossier))
        return textes(langue).folderFailed(QDir::toNativeSeparators(dossier));
    const QByteArray formats = formats_image().join(u", "_s).toHtmlEscaped().toUtf8();
    for (const QString &nom : fichiers()) {
        QFile ressource(u":/help/"_s + nom);
        if (!ressource.open(QIODevice::ReadOnly))
            return ressource.errorString();
        QByteArray contenu = ressource.readAll();
        if (ressource.error() != QFileDevice::NoError)
            return ressource.errorString();
        if (nom.endsWith(u".html"_s)) {
            contenu.replace("{{version}}", MEVPIX_VERSION);
            contenu.replace("{{formats}}", formats);
            // Dans le dépôt, les photos sont dans exemples/. La copie hors ligne les réunit
            // avec les captures et les deux pages HTML.
            contenu.replace("href=\"../../exemples/", "href=\"");
        }
        const QString chemin = QDir(dossier).filePath(nom);
        QFile copie(chemin);
        if (QFileInfo(copie).isFile() && copie.size() == contenu.size() && copie.open(QIODevice::ReadOnly)
            && copie.readAll() == contenu && copie.error() == QFileDevice::NoError)
            continue;
        copie.close(); // sous Windows, libérer le fichier avant de le remplacer
        QSaveFile destination(chemin);
        if (!destination.open(QIODevice::WriteOnly) || !ecrit_tout(destination, contenu) || !destination.commit())
            return destination.errorString();
    }
    return {};
}

} // namespace

QStringList fichiers()
{
    QStringList noms{u"fr.html"_s, u"en.html"_s, u"help.css"_s, u"navigation.js"_s};
    noms.append(QDir(u":/help"_s).entryList({u"*.png"_s}, QDir::Files));
    return noms;
}

QString ecrit(Lang lang, QString *error)
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!cache.isEmpty()) {
        const QString dir = QDir(cache).filePath(DIR_NAME);
        if (ecrit_fichiers(dir, lang).isEmpty())
            return QDir(dir).filePath(page(lang));
    }
    static const QTemporaryDir secours(QDir::temp().filePath(u"mevpix-help-XXXXXX"_s));
    QString raison = secours.errorString();
    if (secours.isValid()) {
        raison = ecrit_fichiers(secours.path(), lang);
        if (raison.isEmpty())
            return secours.filePath(page(lang));
    }
    if (error)
        *error = raison;
    return {};
}

} // namespace help
