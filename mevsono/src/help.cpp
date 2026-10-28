#include "help.h"

#include "savefile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace help {

namespace {

/// Page à ouvrir pour une langue de l'interface.
QString page(Lang lang)
{
    return langCode(lang) + u".html"_s;
}

/// Écrit tous les fichiers ; le marqueur `{{version}}` des pages est remplacé par la version du programme.
QString writeFiles(const QString &dir, Lang lang)
{
    if (!QDir().mkpath(dir))
        return texts(lang).folderFailed(QDir::toNativeSeparators(dir));
    for (const QString &name : files()) {
        QFile resource(u":/help/"_s + name);
        if (!resource.open(QIODevice::ReadOnly))
            return resource.errorString();
        QByteArray content = resource.readAll();
        if (name.endsWith(u".html"_s))
            content.replace("{{version}}", MEVSONO_VERSION);
        QSaveFile out(QDir(dir).filePath(name));
        if (!out.open(QIODevice::WriteOnly) || !writeAll(out, content) || !out.commit())
            return out.errorString();
    }
    return {};
}

} // namespace

QStringList files()
{
    return {u"fr.html"_s,         u"en.html"_s,        u"help.css"_s,       u"accueil-fr.jpg"_s,
            u"fenetre-fr.jpg"_s,  u"mesure-fr.jpg"_s,  u"expert-fr.jpg"_s,  u"reglages-fr.jpg"_s,
            u"accueil-en.jpg"_s,  u"fenetre-en.jpg"_s, u"mesure-en.jpg"_s,  u"expert-en.jpg"_s,
            u"reglages-en.jpg"_s};
}

QString write(Lang lang, const SettingsFiles &settings, QString *error)
{
    // pages refaites à chaque ouverture : dans le dossier temporaire du système, pas dans celui des
    // réglages de la suite (MeVSuite), qui ne contient que des fichiers
    Q_UNUSED(settings);
    const QStringList dirs{QDir(QDir::tempPath()).filePath(DIR_NAME)};

    QString lastError = u"no writable directory"_s;
    for (const QString &dir : dirs) {
        lastError = writeFiles(dir, lang);
        if (lastError.isEmpty())
            return QDir(dir).filePath(page(lang));
    }
    if (error)
        *error = lastError;
    return {};
}

} // namespace help
