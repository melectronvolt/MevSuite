#include "seance.h"

#include <QCryptographicHash>
#include <QFile>
#include <QStringList>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace seance {

QString signature_video(const QString &chemin)
{
    QFile fichier(chemin);
    if (!fichier.open(QIODevice::ReadOnly))
        return {};
    constexpr int bloc = 64 * 1024;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(fichier.size()));
    hash.addData(fichier.read(bloc));
    if (fichier.size() > bloc && fichier.seek(fichier.size() - bloc))
        hash.addData(fichier.read(bloc));
    return QString::fromLatin1(hash.result().toHex());
}

QString texte(prefs::Config reglages, const Pointage &pointage, std::optional<Pointage::ProchainClic> en_cours)
{
    // virgule décimale et commentaire en français, quelle que soit la langue de l'interface
    const QString donnees = pointage.mesures_en_csv(u"\t"_s, true, pointage.image_initiale().value_or(1), pointage.origine,
                                                   std::numeric_limits<double>::max_digits10)
                                .replace(u'.', u',');
    const QString message = u"temps en seconde, positions en "_s + (pointage.echelle.est_definie() ? u"mètre"_s : u"pixel"_s);
    // Zéro distingue une série terminée d'un ancien projet qui ne connaissait pas cette clé.
    reglages.definit(u"objet_suivant"_s, QString::number(en_cours ? en_cours->objet : 0));
    if (en_cours)
        reglages.definit(u"image_en_cours"_s, QString::number(en_cours->image));
    QStringList assistes, assistes_corriges, automatiques, alertes, perdus, absences;
    for (const auto &[cellule, saisie] : pointage.provenances()) {
        const auto [i, obj] = cellule;
        const QString paire = u"%1:%2"_s.arg(i).arg(obj);
        if (Pointage::absence_signalee(saisie)) {
            (saisie == Pointage::Saisie::Perdue ? perdus : absences) << paire;
            continue;
        }
        if (Pointage::est_assistee(saisie)) {
            assistes << paire;
            if (saisie == Pointage::Saisie::AssisteeCorrigee)
                assistes_corriges << paire;
            continue;
        }
        automatiques << paire;
        if (saisie == Pointage::Saisie::AVerifier)
            alertes << paire;
    }
    reglages.definit(u"absences_confirmees"_s, absences.join(u' '));
    reglages.definit(u"perdus"_s, perdus.join(u' '));
    reglages.definit(u"assistes"_s, assistes.join(u' '));
    reglages.definit(u"assistes_corriges"_s, assistes_corriges.join(u' '));
    reglages.definit(u"automatiques"_s, automatiques.join(u' '));
    reglages.definit(u"a_verifier"_s, alertes.join(u' '));
    return prefs::texte_projet(reglages, message, donnees);
}

namespace {

void relit_provenances(const prefs::Config &reglages, Pointage &pointage)
{
    // Les listes vont du général au particulier. Un point « assisté corrigé » figure aussi
    // parmi les points « assistés », afin que les anciennes versions puissent le relire.
    const auto relit_liste = [&](const QString &cle, Pointage::Saisie saisie) {
        const auto paires = reglages.valeur_texte(cle).value_or(QString()).split(u' ', Qt::SkipEmptyParts);
        for (const QString &paire : paires) {
            const auto numeros = paire.split(u':');
            if (numeros.size() != 2)
                continue;
            bool image_ok, objet_ok;
            const int index_image = numeros[0].toInt(&image_ok);
            const int numero_objet = numeros[1].toInt(&objet_ok);
            if (image_ok && objet_ok)
                pointage.marque_saisie(numero_objet, index_image, saisie);
        }
    };
    relit_liste(u"assistes"_s, Pointage::Saisie::Assistee);
    relit_liste(u"assistes_corriges"_s, Pointage::Saisie::AssisteeCorrigee);
    relit_liste(u"automatiques"_s, Pointage::Saisie::Automatique);
    relit_liste(u"a_verifier"_s, Pointage::Saisie::AVerifier);
    relit_liste(u"perdus"_s, Pointage::Saisie::Perdue);
    relit_liste(u"absences_confirmees"_s, Pointage::Saisie::AbsenceConfirmee);
}

std::optional<Pointage::ProchainClic> lit_prochain_clic(const prefs::Config &reglages, const Pointage &pointage)
{
    const auto objet = reglages.valeur_entiere(u"objet_suivant"_s);
    const auto image = reglages.valeur_entiere(u"image_en_cours"_s);
    if (!objet || *objet < 2 || *objet > pointage.nombre_objets() ||
        !image || *image < 1 || *image > pointage.nombre_images())
        return {};
    return Pointage::ProchainClic{static_cast<int>(*image), static_cast<int>(*objet)};
}

std::optional<Pointage::ProchainClic> reprise_ancien_projet(const Pointage &pointage)
{
    // Un ancien secours ne mémorisait pas le curseur. On reconnaît une dernière ligne dont
    // seuls les premiers objets ont été pointés à la main. Une ligne du suivi automatique
    // ou une ligne trouée au milieu ne permet pas de déduire où en était l'élève.
    const auto derniere = pointage.image_finale();
    if (!derniere)
        return {};
    const int index = *derniere - 1;
    int suivant = 1;
    for (int objet = 1; objet <= pointage.nombre_objets(); ++objet) {
        const auto saisie = pointage.saisie(objet, index);
        if (saisie != Pointage::Saisie::Manuelle && !Pointage::est_assistee(saisie))
            return {};
        if (pointage.point_de_l_objet(objet, index)) {
            if (objet != suivant)
                return {};
            ++suivant;
        }
    }
    if (suivant > 1 && suivant <= pointage.nombre_objets())
        return Pointage::ProchainClic{*derniere, suivant};
    return {};
}

} // namespace

std::optional<Pointage::ProchainClic> restaure_mesures(const prefs::Projet &projet, Pointage &pointage)
{
    const auto &reglages = projet.config;
    const qint64 premiere_image = reglages.valeur_entiere(u"index_depart"_s).value_or(1);
    const int depart = static_cast<int>(std::clamp<qint64>(premiere_image, 1, std::numeric_limits<int>::max()));
    const auto reprise = lit_prochain_clic(reglages, pointage);
    pointage.recharge_mesures(projet.donnees, depart, reprise);
    relit_provenances(reglages, pointage);

    if (!reglages.valeur_texte(u"objet_suivant"_s))
        return reprise_ancien_projet(pointage);
    if (reprise) {
        for (int objet = 1; objet < reprise->objet; ++objet) {
            if (!pointage.point_de_l_objet(objet, reprise->image - 1))
                return {};
        }
    }
    return reprise;
}

} // namespace seance
