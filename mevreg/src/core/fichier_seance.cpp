#include "core/workspace.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

using namespace Qt::StringLiterals;

namespace phy {

QByteArray enregistre(const Seance &seance, const QJsonValue &disposition)
{
    QJsonObject fichier{{u"format"_s, QString::fromLatin1(FORMAT_NAME)},
                     {u"version"_s, FORMAT_VERSION},
                     {u"app_version"_s, u"mevreg-" MEVREG_VERSION ""_s},
                     {u"workspace"_s, seance.en_json()}};
    fichier.insert(u"layout"_s, disposition.isUndefined() ? QJsonValue(QJsonValue::Null) : disposition);
    return QJsonDocument(fichier).toJson(QJsonDocument::Indented);
}

/// Un champ facultatif absent reste accepté dans les premières séances. Un champ
/// présent mais illisible doit être signalé avant de remplacer le document ouvert.
static QString verifie_collection(const QJsonObject &parent, const QString &cle, QSet<qint64> &identifiants)
{
    const QJsonValue collection = parent.value(cle);
    if (collection.isUndefined())
        return {};
    if (!collection.isArray())
        return u"La liste « %1 » est illisible."_s.arg(cle);
    for (const QJsonValue element : collection.toArray()) {
        const auto objet = element.toObject();
        const qint64 id = objet.value(u"id"_s).toInteger(-1);
        if (id <= 0 || identifiants.contains(id))
            return u"La liste « %1 » contient un identifiant absent ou en double."_s.arg(cle);
        identifiants.insert(id);
        if (cle == u"parameters") {
            // Une chaîne comme « 4,7 » ne doit jamais devenir un paramètre nul.
            for (const auto &champ : {u"value"_s, u"min"_s, u"max"_s, u"step"_s}) {
                const auto valeur = objet.value(champ);
                if (!valeur.isUndefined() && !valeur.isDouble())
                    return u"Le champ « %1 » du paramètre « %2 » est illisible."_s.arg(champ, objet.value(u"name"_s).toString());
            }
        }
        if (cle != u"quantities")
            continue;
        const auto valeurs = objet.value(u"values"_s);
        if (!valeurs.isUndefined() && !valeurs.isArray())
            return u"La colonne « %1 » ne contient pas une liste de mesures."_s.arg(objet.value(u"name"_s).toString());
        for (const QJsonValue valeur : valeurs.toArray()) {
            if (!valeur.isDouble() && !valeur.isNull())
                return u"La colonne « %1 » contient une mesure illisible."_s.arg(objet.value(u"name"_s).toString());
        }
    }
    return {};
}

std::optional<SeanceLue> charge(const QByteArray &octets, QString *erreur)
{
    auto refuse = [&](const QString &raison) -> std::optional<SeanceLue> {
        if (erreur)
            *erreur = raison;
        return std::nullopt;
    };
    QJsonParseError lecture;
    const auto json = QJsonDocument::fromJson(octets, &lecture);
    if (lecture.error != QJsonParseError::NoError)
        return refuse(u"Fichier illisible à l’octet %1. %2"_s.arg(lecture.offset).arg(lecture.errorString()));
    const auto fichier = json.object();
    if (fichier.value(u"format"_s).toString() != QString::fromLatin1(FORMAT_NAME))
        return refuse(u"Ce fichier n’est pas un espace de travail MeVReg."_s);
    const int version = fichier.value(u"version"_s).toInt(-1);
    if (version > FORMAT_VERSION)
        return refuse(u"Ce fichier a été créé par une version plus récente du logiciel (format %1)."_s.arg(version));
    if (version < 0)
        return refuse(u"Version de format inconnue."_s);
    const auto seance = fichier.value(u"workspace"_s).toObject();
    if (!seance.value(u"doc"_s).isObject())
        return refuse(u"Le document de mesures est absent ou illisible."_s);
    const auto document = seance.value(u"doc"_s).toObject();
    QSet<qint64> grandeursEtParametres, fenetres;
    for (const auto &cle : {u"quantities"_s, u"parameters"_s}) {
        if (const QString probleme = verifie_collection(document, cle, grandeursEtParametres); !probleme.isEmpty())
            return refuse(probleme);
    }
    for (const auto &cle : {u"tables"_s, u"graphs"_s, u"scripts"_s}) {
        if (const QString probleme = verifie_collection(seance, cle, fenetres); !probleme.isEmpty())
            return refuse(probleme);
    }
    SeanceLue resultat{Seance::depuis_json(seance), fichier.value(u"layout"_s)};
    resultat.seance.doc.touche();
    return resultat;
}

} // namespace phy
