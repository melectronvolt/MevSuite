// Le minuteur traite une image à chaque passage pour laisser les commandes disponibles.
// Chaque piste garde le numéro d’objet attribué lors de la sélection des motifs.
#include "app.h"

#include "debogage.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

void App::fin_selection_motif(Vecteur premier_coin, Vecteur coin_oppose)
{
    if (etat != Etat::SelectionMotifs || static_cast<int>(pistes.size()) >= pointage.nombre_objets())
        return;
    const auto image = image_courante();
    if (!image)
        return;
    const int gauche = std::lround(std::min(premier_coin.x, coin_oppose.x));
    const int droite = std::lround(std::max(premier_coin.x, coin_oppose.x));
    const int haut = std::lround(std::min(premier_coin.y, coin_oppose.y));
    const int bas = std::lround(std::max(premier_coin.y, coin_oppose.y));
    const QRect cadre = QRect(gauche, haut, droite - gauche, bas - haut).intersected(image->rect());
    if (cadre.width() < 3 || cadre.height() < 3)
        return;
    cadres_des_motifs.push_back({premier_coin, coin_oppose});
    pistes.emplace_back(image->copy(cadre), cadre.topLeft(), pointage.date(index - 1));
    if (static_cast<int>(pistes.size()) == pointage.nombre_objets()) {
        image_motifs = index;
        dbg_p(3, u"sélection des motifs finie"_s);
        prochaine_image_du_suivi = index;
        objet_courant = 1;
        passe_a_etat(Etat::SuiviAutomatique);
    }
}

void App::etape_detection()
{
    if (etat != Etat::SuiviAutomatique || !prochaine_image_du_suivi)
        return;
    const int numero_image = *prochaine_image_du_suivi;
    change_image(numero_image);
    const auto image = image_courante();
    if (!image) {
        const QString erreur = statut;
        arrete_suivi();
        affiche_statut(erreur, icones::Statut::Erreur);
        return;
    }
    detect::Image trame(*image);
    const double date = pointage.date(numero_image - 1);
    const bool image_de_depart = numero_image == image_motifs;
    struct Mesure {
        detect::Observation observation;
        Vecteur centre;
    };
    std::vector<Mesure> mesures;
    mesures.reserve(pistes.size());
    for (const auto &piste : pistes) {
        auto observation = image_de_depart ? detect::Observation{piste.dernier_coin(), false}
                                           : piste.recherche(trame, date);
        Vecteur centre;
        const QSize motif = piste.taille();
        if (observation.coin) {
            centre = {observation.coin->x() + motif.width() / 2.0,
                      observation.coin->y() + motif.height() / 2.0};
            // Quand deux centres se rapprochent à moins d’un demi-motif, les deux positions sont
            // à vérifier. Elles restent visibles mais ne servent plus à prédire le déplacement.
            for (size_t autre = 0; !image_de_depart && autre < mesures.size(); ++autre) {
                const QSize autre_motif = pistes[autre].taille();
                const int plus_petit_cote = std::min({motif.width(), motif.height(),
                                                      autre_motif.width(), autre_motif.height()});
                auto &autre_mesure = mesures[autre];
                if (autre_mesure.observation.coin && (autre_mesure.centre - centre).module() < plus_petit_cote / 2.0) {
                    observation.a_verifier = true;
                    autre_mesure.observation.a_verifier = true;
                }
            }
        }
        mesures.push_back({observation, centre});
    }
    for (size_t rang = 0; rang < pistes.size(); ++rang) {
        const auto &[observation, centre] = mesures[rang];
        const int objet = static_cast<int>(rang) + 1;
        pistes[rang].enregistre(observation, date);
        if (observation.coin) {
            const auto saisie = observation.a_verifier ? Pointage::Saisie::AVerifier : Pointage::Saisie::Automatique;
            pointage.memorise_point(objet, centre, numero_image - 1, saisie);
        } else {
            pointage.memorise_absence(objet, numero_image - 1);
        }
    }
    objet_courant = 1;
    if (numero_image == nombre_images_video) {
        arrete_suivi();
        arrete_pointage();
    } else {
        prochaine_image_du_suivi = numero_image + 1;
        change_image(*prochaine_image_du_suivi);
    }
}

void App::arrete_suivi()
{
    if (etat != Etat::SuiviAutomatique)
        return;
    m_detection.stop();
    prochaine_image_du_suivi.reset();
    passe_a_etat(Etat::Pointage);
    image_courante(); // STOP peut arriver avant l’affichage de l’image suivante.
}
