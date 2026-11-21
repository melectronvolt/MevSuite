#include "secours.h"

#include "prefs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace {
const QString ANCIEN_NOM = u"mevcine-secours.mevcine"_s;
const QString PREFIXE = u"mevcine-secours-"_s;

std::unique_ptr<QLockFile> prend_verrou(const QString &chemin)
{
    auto verrou = std::make_unique<QLockFile>(chemin + u".lock"_s);
    // Une séance peut durer plusieurs heures. Le processus peut disparaître après un plantage ;
    // l'âge du fichier ne suffit pas à déclarer son verrou abandonné.
    verrou->setStaleLockTime(0);
    if (verrou->tryLock(0))
        return verrou;
    return {};
}
}

void SecoursSeance::nouvelle(const QString &video)
{
    m_verrou.reset();
    m_dernier_texte.clear();
    // Le nom du film aide à retrouver le travail. L'identifiant final distingue deux séances
    // sur le même film commencées dans la même seconde.
    static const QRegularExpression interdits(u"[^\\p{L}\\p{N}._-]+"_s);
    const QString nom = QFileInfo(video).completeBaseName().left(40).replace(interdits, u"-"_s);
    m_chemin = QDir(prefs::dossier_conf()).absoluteFilePath(
        PREFIXE + nom + u'-' + QUuid::createUuid().toString(QUuid::WithoutBraces) + u".mevcine"_s);
}

std::optional<SecoursSeance> SecoursSeance::reserve_pour_reprise(const QString &fichier)
{
    // Les anciennes versions écrivent le fichier historique sans verrou. On le conserve en
    // lecture seule et la séance récupérée continue dans sa propre copie.
    const QFileInfo info(fichier);
    if (!est_secours(fichier) || info.fileName() == ANCIEN_NOM || !info.isFile())
        return {};
    auto verrou = prend_verrou(fichier);
    if (!verrou)
        return {};
    SecoursSeance secours;
    secours.m_chemin = info.absoluteFilePath();
    secours.m_verrou = std::move(verrou);
    return secours;
}

QString SecoursSeance::ecrit(const QString &texte)
{
    if (texte == m_dernier_texte && QFileInfo(m_chemin).isFile())
        return {};
    const QDir dossier = QFileInfo(m_chemin).absoluteDir();
    if (!dossier.mkpath(u"."_s))
        return QDir::toNativeSeparators(dossier.path());
    if (!m_verrou)
        m_verrou = prend_verrou(m_chemin);
    if (!m_verrou)
        return QDir::toNativeSeparators(m_chemin);
    const QString erreur = prefs::ecrit_projet(m_chemin, texte);
    if (erreur.isEmpty())
        m_dernier_texte = texte;
    return erreur;
}

void SecoursSeance::retire_copie_enregistree()
{
    if (m_verrou)
        QFile::remove(m_chemin);
    m_verrou.reset();
    m_dernier_texte.clear();
}

QStringList SecoursSeance::filtres()
{
    return {ANCIEN_NOM, PREFIXE + u"*.mevcine"_s};
}

bool SecoursSeance::est_secours(const QString &fichier)
{
    const QFileInfo info(fichier);
    if (info.absoluteDir() != QDir(prefs::dossier_conf()))
        return false;
    if (info.fileName() == ANCIEN_NOM)
        return true;
    const QString base = info.completeBaseName();
    return info.suffix() == u"mevcine" && base.startsWith(PREFIXE) && !QUuid::fromString(base.right(36)).isNull();
}

QStringList SecoursSeance::disponibles()
{
    QStringList fichiers;
    const QDir dossier(prefs::dossier_conf());
    for (const QFileInfo &info : dossier.entryInfoList(filtres(), QDir::Files, QDir::Time)) {
        if (est_secours(info.absoluteFilePath()))
            fichiers << info.absoluteFilePath();
    }
    return fichiers;
}
