#include "projet.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace {

QJsonArray coordonnees(const Segment &segment)
{
    return {segment.a.x(), segment.a.y(), segment.b.x(), segment.b.y()};
}

std::optional<Segment> lit_segment(const QJsonValue &valeur, QSize taille)
{
    // Le format garde les deux extrémités dans l'ordre [xA, yA, xB, yB].
    const auto points = valeur.toArray();
    if (points.size() != 4)
        return {};
    for (int i = 0; i < points.size(); ++i) {
        const double coordonnee = points[i].toDouble(-1);
        const int limite = i % 2 == 0 ? taille.width() : taille.height();
        if (!std::isfinite(coordonnee) || coordonnee < 0 || coordonnee > limite)
            return {};
    }
    return Segment{{points[0].toDouble(), points[1].toDouble()}, {points[2].toDouble(), points[3].toDouble()}};
}

} // namespace

QByteArray donnees_seance(const ContenuSeance &seance)
{
    QJsonArray mesures;
    for (const Segment &segment : seance.mesures)
        mesures.append(coordonnees(segment));
    QJsonObject donnees{
        {"version", 1}, {"image", seance.image->name}, {"outil", int(seance.outil)}, {"mesures", mesures},
        {"saisie", seance.saisieLongueur}, {"unite", int(seance.uniteSaisie)}, {"canal", int(seance.canal)},
        {"bande", seance.largeurBande}, {"A", seance.fractionsCurseurs[0]}, {"B", seance.fractionsCurseurs[1]}
    };
    if (seance.image->bitsSource > 8)
        donnees["bitsSource"] = seance.image->bitsSource;
    if (seance.coupe)
        donnees["coupe"] = coordonnees(*seance.coupe);
    if (seance.segmentEtalon)
        donnees["etalon"] = coordonnees(*seance.segmentEtalon);
    if (seance.etalonnage) {
        const Etalonnage &echelle = *seance.etalonnage;
        donnees["echelle"] = QJsonArray{echelle.referencePx, echelle.longueurReelle, int(echelle.unite)};
    }
    return QJsonDocument(donnees).toJson(QJsonDocument::Compact);
}

bool restaure_seance(Seance &seance, const QJsonObject &donnees)
{
    if (!seance.image || donnees["version"].toInt() != 1 || !donnees["mesures"].isArray())
        return false;
    if (donnees.contains("saisie") && !donnees["saisie"].isString())
        return false;
    const int outil = donnees["outil"].toInt(-1);
    const int unite = donnees["unite"].toInt(-1);
    const int canal = donnees["canal"].toInt(-1);
    if (outil < 0 || outil > int(Outil::Profil) || unite < 0 || unite > int(Unite::Metre)
        || canal < 0 || canal > int(Canal::Bleu))
        return false;
    const int bande = donnees["bande"].toInt();
    const double a = donnees["A"].toDouble(-1);
    const double b = donnees["B"].toDouble(-1);
    if (bande < 1 || bande > 50 || !(a >= 0 && a <= 1) || !(b >= 0 && b <= 1))
        return false;

    // Tout lire dans une séance provisoire. Une erreur, même sur la dernière mesure,
    // doit laisser le travail déjà ouvert intact.
    Seance lue;
    lue.image = seance.image;
    const QSize taille = lue.image->rgb.size();
    lue.outil = Outil(outil);
    lue.canal = Canal(canal);
    lue.uniteSaisie = Unite(unite);
    lue.saisieLongueur = donnees["saisie"].toString();
    lue.largeurBande = bande;
    lue.fractionsCurseurs = {a, b};
    lue.segmentEtalon = lit_segment(donnees["etalon"], taille);
    lue.coupe = lit_segment(donnees["coupe"], taille);
    if ((donnees.contains("etalon") && !lue.segmentEtalon) || (donnees.contains("coupe") && !lue.coupe))
        return false;
    const QJsonArray mesures = donnees["mesures"].toArray();
    for (const auto &valeur : mesures) {
        const auto segment = lit_segment(valeur, taille);
        if (!segment)
            return false;
        lue.mesures.push_back(*segment);
    }
    if (donnees.contains("echelle")) {
        const auto echelle = donnees["echelle"].toArray();
        if (echelle.size() != 3)
            return false;
        const int uniteValidee = echelle[2].toInt(-1);
        if (uniteValidee < 0 || uniteValidee > int(Unite::Metre))
            return false;
        lue.etalonnage = Etalonnage::fabrique(echelle[0].toDouble(), echelle[1].toDouble(), Unite(uniteValidee),
                                               lue.image->diagonale_px());
        if (!lue.etalonnage)
            return false;
    }
    seance = std::move(lue);
    return true;
}
