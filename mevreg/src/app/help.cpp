#include "app/help.h"
#include "app/savefile.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace help {

std::function<bool(const QUrl &)> openUrl = [](const QUrl &url) { return QDesktopServices::openUrl(url); };

QStringList fichiers()
{
    return {NOTICE, PYTHON, u"help.css"_s, u"tableur.png"_s, u"residus.png"_s, u"import-csv.png"_s, u"resistance.csv"_s};
}

QString ecrit(const QString &page, QString *erreur)
{
    // Chaque compte du lycée doit pouvoir ouvrir son aide, même si un autre
    // utilisateur a déjà utilisé cette version sur le même poste.
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString dossier = QDir(cache).filePath(u"aide-" MEVREG_VERSION ""_s);
    QString probleme;
    if (cache.isEmpty() || !QDir().mkpath(dossier))
        probleme = u"Impossible de créer le dossier d’aide « %1 »."_s.arg(QDir::toNativeSeparators(dossier));
    for (const QString &nom : fichiers()) {
        if (!probleme.isEmpty())
            break;
        QFile ressource(u":/help/"_s + nom);
        if (!ressource.open(QIODevice::ReadOnly)) {
            probleme = ressource.errorString();
            break;
        }
        QByteArray contenu = ressource.readAll();
        if (nom.endsWith(u".html"_s))
            contenu.replace("{{version}}", MEVREG_VERSION);
        QSaveFile sortie(QDir(dossier).filePath(nom));
        if (!sortie.open(QIODevice::WriteOnly) || !ecrit_tout(sortie, contenu) || !sortie.commit())
            probleme = sortie.errorString();
    }
    if (!probleme.isEmpty()) {
        if (erreur)
            *erreur = probleme;
        return {};
    }
    return QDir(dossier).filePath(page);
}

} // namespace help
