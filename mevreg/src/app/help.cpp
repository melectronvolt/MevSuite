#include "app/help.h"
#include "app/savefile.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace help {

std::function<bool(const QUrl &)> openUrl = [](const QUrl &url) { return QDesktopServices::openUrl(url); };

QStringList files()
{
    return {NOTICE,           PYTHON,           u"help.css"_s,     u"accueil.jpg"_s, u"fenetre.jpg"_s,
            u"tableur.jpg"_s, u"import-csv.jpg"_s, u"graphe.jpg"_s, u"modele.jpg"_s,  u"python.jpg"_s};
}

QString write(const QString &page, QString *error)
{
    // pages refaites à chaque ouverture : dans le dossier temporaire du système, pas dans celui des
    // réglages de la suite (MeVSuite), qui ne contient que des fichiers
    const QString dir = QDir(QDir::tempPath()).filePath(DIR_NAME);
    QString problem;
    if (!QDir().mkpath(dir))
        problem = u"impossible de créer le dossier « %1 »"_s.arg(QDir::toNativeSeparators(dir));
    for (const QString &name : files()) {
        if (!problem.isEmpty())
            break;
        QFile resource(u":/help/"_s + name);
        if (!resource.open(QIODevice::ReadOnly)) {
            problem = resource.errorString();
            break;
        }
        QByteArray content = resource.readAll();
        if (name.endsWith(u".html"_s))
            content.replace("{{version}}", MEVREG_VERSION);
        QSaveFile out(QDir(dir).filePath(name));
        if (!out.open(QIODevice::WriteOnly) || !writeAll(out, content) || !out.commit())
            problem = out.errorString();
    }
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return {};
    }
    return QDir(dir).filePath(page);
}

} // namespace help
