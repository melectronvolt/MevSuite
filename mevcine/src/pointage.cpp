#include "pointage.h"

#include "format.h"

#include <QStringList>

#include <algorithm>
#include <format>

using namespace Qt::StringLiterals;

void Pointage::alloue_mesures(int n_suivis, double delta_t_, int n_images)
{
    m_nombre_objets = std::max(n_suivis, 1);
    delta_t = delta_t_;
    instants.reset();
    cadence_irreguliere = false;
    m_nombre_images = std::max(n_images, 0);
    m_positions.clear();
    m_ligne_vide.assign(m_nombre_objets, std::nullopt);
    m_provenances.clear();
    m_images_reference.assign(m_nombre_objets, {});
    m_defaits.clear();
    m_actions.clear();
}

double Pointage::date(int index) const
{
    if (instants && index >= 0 && index < static_cast<int>(instants->size()))
        return (*instants)[index] * delta_t;
    return index * delta_t;
}

double Pointage::date_relative(int index) const
{
    const auto prem = image_initiale();
    const double t0 = prem ? date(*prem - 1) : 0.0;
    return date(index) - t0;
}

QString Pointage::unite() const
{
    return echelle.est_definie() ? u"m"_s : u"px"_s;
}

std::vector<int> Pointage::images_mesurees() const
{
    std::vector<int> images;
    images.reserve(m_positions.size());
    for (const auto &[index, positions] : m_positions)
        images.push_back(index);

    // Une image où tous les mobiles sont absents figure uniquement dans les provenances.
    // Les deux listes sont déjà triées par image. Plusieurs absences ne forment qu’une ligne.
    const auto nombre_avec_position = images.size();
    int derniere_absence = -1;
    for (const auto &[cellule, saisie] : m_provenances) {
        const int index = cellule.first;
        if (absence_signalee(saisie) && index != derniere_absence && !m_positions.contains(index)) {
            images.push_back(index);
            derniere_absence = index;
        }
    }
    std::inplace_merge(images.begin(), images.begin() + nombre_avec_position, images.end());
    return images;
}

int Pointage::nb_images_pointees() const
{
    return static_cast<int>(m_positions.size());
}

std::vector<Pointage::LigneExport> Pointage::lignes_export() const
{
    std::vector<LigneExport> lignes;
    const int debut = image_initiale().value_or(1) - 1;
    const int fin = image_finale().value_or(0);
    lignes.reserve(fin - debut);
    for (int i = debut; i < fin; ++i) {
        LigneExport ligne{i, date(i) - date(debut), this->ligne(i)};
        for (Position &p : ligne.positions)
            if (p)
                p = position_dans_repere(*p);
        lignes.push_back(std::move(ligne));
    }
    return lignes;
}

Pointage::Saisie Pointage::saisie(int obj, int index) const
{
    const auto it = m_provenances.find({index, obj});
    return it == m_provenances.end() ? Saisie::Manuelle : it->second;
}

void Pointage::marque_saisie(int obj, int index, Saisie provenance)
{
    if (index < 0 || index >= nombre_images() || obj < 1 || obj > m_nombre_objets)
        return;
    const bool present = point_de_l_objet(obj, index).has_value();
    if (provenance == Saisie::Manuelle)
        m_provenances.erase({index, obj});
    else if (absence_signalee(provenance) == !present)
        m_provenances[{index, obj}] = provenance;
    else
        return;

    if (!m_images_reference[obj - 1])
        return;
    auto &images = *m_images_reference[obj - 1];
    const auto it = std::lower_bound(images.begin(), images.end(), index);
    const bool connue = it != images.end() && *it == index;
    const bool reference = present && est_reference(provenance);
    if (reference && !connue)
        images.insert(it, index);
    else if (!reference && connue)
        images.erase(it);
}

std::vector<Pointage::ExempleVisee> Pointage::exemples_visee(int obj, int avant) const
{
    if (obj < 1 || obj > static_cast<int>(m_images_reference.size()))
        return {};
    auto &index_objet = m_images_reference[obj - 1];
    if (!index_objet) {
        auto &images = index_objet.emplace();
        for (const auto &[i, positions] : m_positions)
            if (positions[obj - 1] && est_reference(saisie(obj, i)))
                images.push_back(i);
    }
    const auto &images = *index_objet;
    const auto n = std::lower_bound(images.begin(), images.end(), avant) - images.begin();
    const auto nombre = std::min<std::ptrdiff_t>(5, n);
    std::vector<ExempleVisee> exemples;
    for (int j = 0; j < nombre; ++j) {
        const int index = images[nombre == 1 ? 0 : j * (n - 1) / (nombre - 1)];
        exemples.emplace_back(index, *ligne(index)[obj - 1]);
    }
    return exemples;
}

void Pointage::memorise_point(int obj, Vecteur position, int index, Saisie provenance, bool nouvelle_action)
{
    enregistre_position(obj, position, index, provenance, nouvelle_action);
}

void Pointage::memorise_absence(int obj, int index)
{
    enregistre_position(obj, std::nullopt, index, Saisie::Perdue, false);
}

void Pointage::confirme_suivi(int obj, int index)
{
    const Saisie source = saisie(obj, index);
    if (source == Saisie::Perdue || source == Saisie::AVerifier)
        enregistre_position(obj, point_de_l_objet(obj, index), index,
                            source == Saisie::Perdue ? Saisie::AbsenceConfirmee : Saisie::Automatique, true);
}

void Pointage::enregistre_position(int obj, Position position, int index, Saisie provenance, bool nouvelle_action)
{
    if (index < 0 || index >= nombre_images() || obj < 1 || obj > m_nombre_objets)
        return;
    const bool suite = !nouvelle_action && !m_actions.empty() && m_defaits.empty() && !m_actions.back().suppression &&
                       m_actions.back().cellules.back().index_image == index &&
                       m_actions.back().cellules.back().numero_objet < obj;
    if (!suite)
        m_actions.emplace_back();
    m_defaits.clear();
    m_actions.back().cellules.emplace_back(index, obj, remplace_position(obj, index, position), saisie(obj, index));
    marque_saisie(obj, index, provenance);
}

Position Pointage::point_de_l_objet(int obj, int index) const
{
    if (index < 0 || index >= nombre_images() || obj < 1 || obj > m_nombre_objets)
        return std::nullopt;
    return ligne(index)[obj - 1];
}

const std::vector<Position> &Pointage::ligne(int index) const
{
    const auto image = m_positions.find(index);
    return image == m_positions.end() ? m_ligne_vide : image->second;
}

Position Pointage::remplace_position(int objet, int index, Position position)
{
    if (position) {
        auto image = m_positions.try_emplace(index, m_ligne_vide).first;
        return std::exchange(image->second[objet - 1], position);
    }
    const auto image = m_positions.find(index);
    if (image == m_positions.end())
        return {};
    auto &positions = image->second;
    const Position ancienne = std::exchange(positions[objet - 1], std::nullopt);
    if (std::ranges::none_of(positions, &Position::has_value))
        m_positions.erase(image);
    return ancienne;
}

bool Pointage::a_des_pointages() const
{
    return !m_positions.empty();
}

std::optional<int> Pointage::image_initiale() const
{
    const int position = m_positions.empty() ? nombre_images() : m_positions.begin()->first;
    const int saisie = m_provenances.empty() ? nombre_images() : m_provenances.begin()->first.first;
    const int premiere = std::min(position, saisie);
    return premiere < nombre_images() ? std::optional(premiere + 1) : std::nullopt;
}

std::optional<int> Pointage::image_finale() const
{
    const int position = m_positions.empty() ? -1 : m_positions.rbegin()->first;
    const int saisie = m_provenances.empty() ? -1 : m_provenances.rbegin()->first.first;
    const int derniere = std::max(position, saisie);
    return derniere >= 0 ? std::optional(derniere + 1) : std::nullopt;
}

void Pointage::supprime_points(const std::vector<Cellule> &cellules)
{
    Modification avant;
    avant.suppression = true;
    for (const auto &[i, obj] : cellules) {
        if (point_de_l_objet(obj, i) || absence_signalee(saisie(obj, i))) {
            avant.cellules.emplace_back(i, obj, remplace_position(obj, i, std::nullopt), saisie(obj, i));
            marque_saisie(obj, i, Saisie::Manuelle);
        }
    }
    if (!avant.cellules.empty()) {
        m_defaits.clear();
        m_actions.push_back(std::move(avant));
    }
}

void Pointage::echange(Modification &action)
{
    for (auto &[i, obj, position, provenance] : action.cellules) {
        position = remplace_position(obj, i, position);
        marque_saisie(obj, i, std::exchange(provenance, saisie(obj, i)));
    }
}

std::optional<int> Pointage::annule_mesure(std::optional<ProchainClic> en_cours)
{
    if (m_actions.empty())
        return {};
    auto action = std::move(m_actions.back());
    m_actions.pop_back();
    const int image = action.cellules.front().index_image + 1;
    echange(action);
    action.en_cours = en_cours;
    m_defaits.push_back(std::move(action));
    return image;
}

std::optional<Pointage::ProchainClic> Pointage::retablit_mesure()
{
    if (m_defaits.empty())
        return {};
    auto action = std::move(m_defaits.back());
    m_defaits.pop_back();
    echange(action);
    m_actions.push_back(std::move(action));
    return m_actions.back().en_cours;
}

QString Pointage::mesures_en_csv(const QString &sep, bool en_metre, int debut, Vecteur org, int chiffres) const
{
    const double mul = en_metre ? echelle.metres_par_pixel() : 1.0;
    QStringList lignes;
    QStringList en_tete{u"t"_s};
    for (int o = 1; o <= m_nombre_objets; ++o) {
        en_tete << u"x%1"_s.arg(o);
        en_tete << u"y%1"_s.arg(o);
    }
    lignes << en_tete.join(sep);
    // la date zéro correspond à la première image pointée
    debut = std::max(debut, 1) - 1;
    // on s'arrête à la dernière image qui porte au moins un pointage : les images suivantes n'ont
    // rien à enregistrer, et un projet de quelques mesures ne grossit pas avec la vidéo
    const int fin = image_finale().value_or(0) - 1;
    for (int i = debut; i <= fin; ++i) {
        QStringList ligne{QString::fromStdString(std::format("{:.3f}", date(i) - date(debut)))};
        // On n'écrit que jusqu'au dernier objet pointé de la ligne : les objets manquants en fin de
        // ligne peuvent être omis (comme avant), mais un objet manquant au milieu reste représenté
        // par deux champs « nan », pour que les colonnes suivantes ne glissent pas.
        const auto &positions = this->ligne(i);
        qsizetype dernier = -1;
        for (qsizetype o = 0; o < static_cast<qsizetype>(positions.size()); ++o) {
            if (positions[o])
                dernier = o;
        }
        for (qsizetype o = 0; o <= dernier; ++o) {
            const Position &p = positions[o];
            if (!p) {
                ligne << u"nan"_s << u"nan"_s;
                continue;
            }
            ligne << texte_significatif(sens_x * (p->x - org.x) * mul, chiffres);
            ligne << texte_significatif(-sens_y * (p->y - org.y) * mul, chiffres);
        }
        lignes << ligne.join(sep);
    }
    lignes << QString();
    return lignes.join(u'\n');
}

Vecteur Pointage::position_dans_repere(Vecteur p) const
{
    const double m = echelle.metres_par_pixel();
    return {sens_x * (p.x - origine.x) * m, sens_y * (origine.y - p.y) * m};
}

Vecteur Pointage::vecteur_dans_repere(Vecteur v) const
{
    const double m = echelle.metres_par_pixel();
    return {sens_x * v.x * m, -sens_y * v.y * m};
}

std::optional<Pointage::Cinematique> Pointage::cinematique_px(int obj, int i) const
{
    if (i <= 0 || i >= nombre_images() - 1)
        return {};
    const auto avant = point_de_l_objet(obj, i - 1);
    const auto centre = point_de_l_objet(obj, i);
    const auto apres = point_de_l_objet(obj, i + 1);
    if (!avant || !centre || !apres)
        return std::nullopt;
    const double duree_avant = date(i) - date(i - 1);
    const double duree_apres = date(i + 1) - date(i);
    if (!(duree_avant > 0 && duree_apres > 0))
        return std::nullopt;
    const Vecteur vitesse_avant = (*centre - *avant) * (1.0 / duree_avant);
    const Vecteur vitesse_apres = (*apres - *centre) * (1.0 / duree_apres);
    const double duree_totale = duree_avant + duree_apres;
    // À cadence constante, les deux vitesses moyennes ont le même poids. Avec des
    // intervalles inégaux, ces poids ramènent la vitesse à la date du point central.
    const Vecteur vitesse = vitesse_avant * (duree_apres / duree_totale) + vitesse_apres * (duree_avant / duree_totale);
    const Vecteur acceleration = (vitesse_apres - vitesse_avant) * (2.0 / duree_totale);
    return Cinematique{vitesse, acceleration};
}

Position Pointage::vitesse_px(int obj, int index) const
{
    if (const auto c = cinematique_px(obj, index))
        return c->vitesse;
    return std::nullopt;
}

Pointage::FlechesVitesse Pointage::fleches_vitesse(double echelle_vitesse) const
{
    FlechesVitesse res(m_nombre_objets);
    const double facteur = echelle.metres_par_pixel() * echelle_vitesse;
    for (int obj = 1; obj <= m_nombre_objets; ++obj) {
        for (const auto &[i, positions] : m_positions) {
            if (!positions[obj - 1])
                continue;
            const Position v = vitesse_px(obj, i);
            if (v && *v != Vecteur{}) {
                const Vecteur centre = *positions[obj - 1];
                res[obj - 1].emplace_back(centre, centre + *v * facteur);
            }
        }
    }
    return res;
}

void Pointage::recharge_mesures(const std::vector<std::vector<double>> &lignes, int premiere_image_pointee,
                                std::optional<ProchainClic> en_cours)
{
    const double pixels_par_metre = echelle.pixels_par_metre();
    const int debut = std::max(premiere_image_pointee, 1) - 1;
    if (debut >= nombre_images())
        return;
    const int nombre = static_cast<int>(std::min(lignes.size(), static_cast<size_t>(nombre_images() - debut)));
    const int fin = debut + nombre;
    const auto relit_objets = [&](int index, int premier_objet, int dernier_objet) {
        const auto &ligne = lignes[index - debut];
        for (int obj = premier_objet; obj <= dernier_objet; ++obj) {
            const size_t j = 2 * obj - 1;
            if (j + 1 >= ligne.size())
                break;
            const double x = ligne[j], y = ligne[j + 1];
            // « nan » (ou un champ vide) signale un objet non pointé : on ne crée pas de position
            if (!std::isfinite(x) || !std::isfinite(y))
                continue;
            memorise_point(obj, {origine.x + sens_x * x * pixels_par_metre, origine.y - sens_y * y * pixels_par_metre}, index);
        }
    };
    // Un élève peut revenir en arrière. Le fichier classe les lignes par numéro d'image,
    // alors que « Annuler » doit commencer par la série qui était sous sa souris.
    if (en_cours && (en_cours->image <= debut || en_cours->image > fin ||
                     en_cours->objet < 2 || en_cours->objet > m_nombre_objets))
        en_cours.reset();
    for (int index = debut; index < fin; ++index) {
        const bool serie_interrompue = en_cours && index + 1 == en_cours->image;
        relit_objets(index, serie_interrompue ? en_cours->objet : 1, m_nombre_objets);
    }
    if (en_cours)
        relit_objets(en_cours->image - 1, 1, en_cours->objet - 1);
}

Pointage Pointage::dans_referentiel(int obj_ref, Vecteur centre) const
{
    Pointage mobile;
    mobile.alloue_mesures(m_nombre_objets, delta_t, nombre_images());
    mobile.instants = instants;
    mobile.cadence_irreguliere = cadence_irreguliere;
    mobile.echelle = echelle;
    mobile.sens_x = sens_x;
    mobile.sens_y = sens_y;
    mobile.origine = centre;
    if (obj_ref < 1 || obj_ref > m_nombre_objets)
        return mobile;

    for (const auto &[index, positions] : m_positions) {
        const Position &reference = positions[obj_ref - 1];
        if (!reference)
            continue;
        auto &transformees = mobile.m_positions.emplace_hint(mobile.m_positions.end(), index, positions)->second;
        for (Position &position : transformees)
            if (position)
                position = *position - *reference + centre;
    }
    for (const auto &[cellule, source] : m_provenances) {
        const auto [index, objet] = cellule;
        if (mobile.point_de_l_objet(objet, index))
            mobile.m_provenances.emplace_hint(mobile.m_provenances.end(), cellule, source);
    }
    return mobile;
}
