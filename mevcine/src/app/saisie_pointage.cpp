// Du clic sur un mobile à la mesure enregistrée. Les numéros d’image de la fenêtre
// commencent à 1. Le modèle Pointage reçoit l’index correspondant, à partir de 0.
#include "app.h"

#include "debogage.h"
#include "plein_ecran.h"

#include <QPainter>

#include <algorithm>

using namespace Qt::StringLiterals;

bool App::image_affichee() const
{
    return video && !image.isNull() && image_cle == std::pair{index, video->rotation()};
}

bool App::pointage_ok() const
{
    if (!image_affichee() || !(etat == Etat::Pointage || etat == Etat::SerieEnCours) || choix_origine)
        return false;
    // pointage arrêté : seule la mesure demandée par le tableau se refait
    if (!simplifie() && pointage_arrete && !refait)
        return false;
    if (simplifie() && !guide.pointage_permis(refait.has_value()))
        return false;
    const auto prem = pointage.image_initiale(), der = pointage.image_finale();
    if (prem && der)
        return index >= *prem - 1 && index - 1 <= *der;
    return true;
}

bool App::visee_assistee_active() const
{
    return simplifie() && guide.visee_assistee && guide.etape == Etape::Pointage && !refait &&
           (etat == Etat::Pointage || etat == Etat::SerieEnCours) && !choix_origine;
}

void App::actualise_visee()
{
    const int i = index - 1;
    const auto precedent = pointage.point_de_l_objet(objet_courant, i - 1);
    if (!visee_assistee_active() || !image_affichee() || !precedent ||
        pointage.saisie(objet_courant, i - 1) == Pointage::Saisie::AVerifier ||
        pointage.point_de_l_objet(objet_courant, i) || Pointage::absence_signalee(pointage.saisie(objet_courant, i)) ||
        index < pointage.image_finale().value_or(index)) {
        m_visee.reset();
        return;
    }
    auto exemples = pointage.exemples_visee(objet_courant, i);
    if (m_visee && m_visee->image == image.cacheKey() && m_visee->numero == index &&
        m_visee->objet == objet_courant && m_visee->precedent == *precedent && m_visee->exemples == exemples)
        return;
    m_visee = Visee{image.cacheKey(), index, objet_courant, *precedent, std::move(exemples), {}, {}};
    auto &cache = m_references_visee[objet_courant];
    std::erase_if(cache, [&](const ReferenceVisee &r) {
        return std::ranges::find(m_visee->exemples, r.mesure) == m_visee->exemples.end();
    });
    std::vector<detect::ReferenceDetail> references;
    for (const auto &mesure : m_visee->exemples) {
        const auto it = std::ranges::find(cache, mesure, &ReferenceVisee::mesure);
        if (it != cache.end()) {
            references.push_back(it->detail);
        } else if (const auto source = video->image(mesure.first + 1)) {
            const auto detail = detect::prepare_detail(*source, {mesure.second.x, mesure.second.y});
            cache.push_back({mesure, detail});
            references.push_back(detail);
        }
    }
    if (const auto p = detect::retrouve_detail(references, image, {precedent->x, precedent->y})) {
        m_visee->proposition = Vecteur(p->x(), p->y());
        m_visee->cible = m_visee->proposition;
    }
}

std::optional<Vecteur> App::visee_proposee() const
{
    if (m_visee && visee_assistee_active() && m_visee->numero == index && m_visee->objet == objet_courant &&
        m_visee->image == image.cacheKey())
        return m_visee->cible;
    return {};
}

void App::ajuste_visee(Vecteur pas)
{
    if (const auto cible = visee_proposee()) {
        const Vecteur p = *cible + pas;
        if (p.x >= 0 && p.y >= 0 && p.x < image.width() && p.y < image.height()) {
            m_visee->cible = p;
            rafraichit_loupe_et_image();
        }
    }
}

QString App::consigne_visee() const
{
    if (visee_proposee())
        return trad("Cliquez sur le détail dans la loupe pour enregistrer sa position.");
    if (m_visee)
        return trad("Détail incertain. Pointez-le dans la vidéo.");
    if (pointage.point_de_l_objet(objet_courant, index - 1) || Pointage::absence_signalee(pointage.saisie(objet_courant, index - 1)))
        return trad("Cette mesure figure déjà dans le tableau.");
    return trad("Cliquez sur le détail à suivre dans la vidéo.");
}

ZoneVideo *App::zone_pointage() const
{
    if (plein_ecran())
        return m_plein_ecran->zone();
    return simplifie() ? m_guide->zone() : m_onglet_pointage->zone();
}

void App::montre_repere_attendu()
{
    if (refait && objet_repris) {
        const bool perdu = pointage.saisie(objet_repris, index - 1) == Pointage::Saisie::Perdue;
        affiche_statut(perdu ? trad("Image {} · objet {} perdu. Pointez-le ou confirmez son absence.", index, objet_repris)
                             : trad("Image {} · objet {} à vérifier. Validez le point ou cliquez pour le corriger.", index, objet_repris));
    } else if (refait && pointage.nombre_objets() > 1)
        affiche_statut(trad("Image {} · cliquez sur l'objet {} (Échap pour annuler)", index, objet_courant));
    else if (refait)
        affiche_statut(trad("Cliquez sur l'objet pour refaire la mesure de l'image {} (Échap : annuler)", index));
    else if (!simplifie() && pointage_arrete)
        affiche_statut(trad("Pointage arrêté : « Reprendre le pointage » pour continuer"));
    else if (pointage.nombre_objets() > 1)
        affiche_statut(trad("Cliquez sur l'objet {}", objet_courant));
    else if (simplifie() && guide.visee_assistee && guide.etape == Etape::Pointage)
        affiche_statut(trad("Gardez le même repère sur le mobile."));
    else
        affiche_statut(trad("Cliquez sur l'objet"));
}

void App::clic_pointage(Vecteur p, Pointage::Saisie provenance)
{
    if (!pointage_ok())
        return;
    if (provenance == Pointage::Saisie::Assistee) {
        if (!visee_proposee())
            return;
        if ((p - *m_visee->proposition).module() > 1e-7)
            provenance = Pointage::Saisie::AssisteeCorrigee;
    }
    passe_a_etat(Etat::SerieEnCours);
    if (refait) {
        correction.push_back(p);
        if (!objet_repris && objet_courant < pointage.nombre_objets()) {
            ++objet_courant;
            montre_repere_attendu();
            return;
        }
        const int premier = objet_repris ? objet_repris : 1;
        for (size_t j = 0; j < correction.size(); ++j)
            pointage.memorise_point(premier + static_cast<int>(j), correction[j], index - 1, Pointage::Saisie::Manuelle, j == 0);
        annule_refait();
        affiche_statut(trad("Mesure refaite"), icones::Statut::Reussite);
        return;
    }
    pointage.memorise_point(objet_courant, p, index - 1, provenance);
    passe_objet_suivant();
    prepare_clic_suivant();
}

void App::passe_objet_suivant()
{
    if (objet_courant < pointage.nombre_objets()) {
        ++objet_courant;
        return;
    }
    objet_courant = 1;
    if (index < nombre_images_video)
        ++index;
    // une série terminée rend la main au pointage manuel, sauf pendant le suivi automatique
    if (etat != Etat::SuiviAutomatique)
        passe_a_etat(Etat::Pointage);
}

void App::prepare_clic_suivant()
{
    if (etat != Etat::SuiviAutomatique)
        montre_repere_attendu();
}

bool App::annulation_possible() const
{
    return (etat == Etat::Pointage || etat == Etat::SerieEnCours) && !refait && pointage.annulation_possible();
}

bool App::retablissement_possible() const
{
    return etat == Etat::Pointage && !refait && pointage.retablissement_possible();
}

void App::retire_mesure_avant()
{
    dbg_p(2, u"rentre dans 'retire_mesure_avant'"_s);
    if (!annulation_possible())
        return;
    const auto der = pointage.annule_mesure(clic_en_cours());
    // on revient sur l'image dont on vient d'effacer le pointage
    if (der)
        change_image(*der);
    if (etat == Etat::SerieEnCours)
        passe_a_etat(Etat::Pointage);
    objet_courant = 1;
    prepare_clic_suivant();
}

void App::retablit_point()
{
    dbg_p(2, u"rentre dans 'retablit_point'"_s);
    if (!retablissement_possible())
        return;
    if (const auto clic = pointage.retablit_mesure()) {
        change_image(clic->image);
        objet_courant = clic->objet;
        passe_a_etat(Etat::SerieEnCours);
    } else {
        if (const auto derniere = pointage.image_finale())
            change_image(std::min(*derniere, nombre_images_video - 1) + 1);
        objet_courant = 1;
    }
    prepare_clic_suivant();
}

QImage App::apercu_point(int i, int objet, QSize taille)
{
    const Position position = pointage.point_de_l_objet(objet, i);
    if (!video || !position || taille.isEmpty() || etat == Etat::SuiviAutomatique)
        return {};
    const auto source = video->image(i + 1);
    if (!source)
        return {};
    QImage vue = source->scaled(taille, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&vue);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF centre(position->x * vue.width() / source->width(), position->y * vue.height() / source->height());
    p.setPen(QPen(Qt::black, 4));
    p.drawEllipse(centre, 8, 8);
    p.setPen(QPen(Qt::yellow, 2));
    p.drawEllipse(centre, 8, 8);
    return vue;
}

void App::reprend_mesure(int i, int objet)
{
    if (etat != Etat::Pointage || choix_origine || i < 1 || i > nombre_images_video || objet < 0 || objet > pointage.nombre_objets())
        return;
    if (!refait) {
        refait = index;
        onglet_avant_reprise = onglet;
    }
    correction.clear();
    objet_repris = objet;
    objet_courant = objet ? objet : 1;
    change_image(i);
    onglet = Onglet::Pointage;
    if (simplifie() && guide.etape < Etape::Pointage)
        guide.etape = Etape::Pointage;
    prepare_clic_suivant();
}

void App::annule_refait()
{
    if (!refait)
        return;
    const int retour = *refait;
    refait.reset();
    correction.clear();
    objet_repris = 0;
    passe_a_etat(Etat::Pointage);
    change_image(retour);
    objet_courant = 1;
    if (mode == Mode::Expert)
        onglet = onglet_avant_reprise;
    prepare_clic_suivant();
}

void App::verifie_point_suivant()
{
    if (etat != Etat::Pointage || refait || choix_origine)
        return;
    const auto &points = pointage.provenances();
    const auto debut = derniere_verification ? points.upper_bound(*derniere_verification) : points.begin();
    const auto a_revoir = [](const auto &p) { return p.second == Pointage::Saisie::AVerifier || p.second == Pointage::Saisie::Perdue; };
    auto it = std::find_if(debut, points.end(), a_revoir);
    if (it == points.end())
        it = std::find_if(points.begin(), points.end(), a_revoir);
    if (it != points.end()) {
        derniere_verification = it->first;
        reprend_mesure(it->first.first + 1, it->first.second);
    }
}

void App::lance_pointage()
{
    if (etat == Etat::Pointage && pointage_arrete) {
        // reprise du pointage arrêté : les mesures sont gardées
        pointage_arrete = false;
        montre_repere_attendu();
        return;
    }
    if (pointage.nombre_objets() != nombre_objets_demandes || pointage.nombre_images() != nombre_images_video)
        ajuste_nombre_objets();
    passe_a_etat(suivi_auto && !simplifie() ? Etat::SelectionMotifs : Etat::Pointage);
}

void App::arrete_pointage()
{
    if (etat != Etat::Pointage || refait || choix_origine)
        return;
    quitte_plein_ecran();
    pointage_arrete = true;
    objet_courant = 1;
    if (pointage.a_des_pointages())
        onglet = Onglet::Coordonnees;
    if (simplifie()) {
        // Après un passage depuis le mode expert, le repère ou l'échelle peuvent encore manquer.
        for (Etape e : TOUTES_LES_ETAPES) {
            if (e == Etape::Export || !etape_faite(e)) {
                va_a_l_etape(e);
                return;
            }
        }
    }
    montre_repere_attendu();
}
