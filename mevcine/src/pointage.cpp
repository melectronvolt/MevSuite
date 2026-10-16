#include "pointage.h"

#include "format.h"

#include <QStringList>

#include <algorithm>
#include <format>

using namespace Qt::StringLiterals;

void Pointage::dimensionne(int n_suivis, double delta_t_, int n_images)
{
    m_nb_obj = std::max(n_suivis, 1);
    delta_t = delta_t_;
    instants.reset();
    m_data.assign(std::max(n_images, 0), std::vector<Position>(m_nb_obj));
    m_defaits.clear();
}

void Pointage::garde_premier_objet()
{
    m_nb_obj = 1;
    for (auto &ligne : m_data)
        ligne.resize(1);
    for (auto &[index, positions] : m_defaits)
        positions.resize(1);
    // un pointage défait qui ne concernait que les autres objets n'a plus rien à rétablir
    std::erase_if(m_defaits, [](const auto &d) { return !d.second[0]; });
}

double Pointage::date(int index) const
{
    if (instants && index >= 0 && index < static_cast<int>(instants->size()))
        return (*instants)[index] * delta_t;
    return index * delta_t;
}

double Pointage::date_relative(int index) const
{
    const auto prem = premiere_image();
    const double t0 = prem ? date(*prem - 1) : 0.0;
    return date(index) - t0;
}

QString Pointage::unite() const
{
    return echelle.est_definie() ? u"m"_s : u"px"_s;
}

int Pointage::nb_images_pointees() const
{
    return static_cast<int>(std::count_if(m_data.begin(), m_data.end(), [](const auto &l) { return l[0].has_value(); }));
}

std::vector<Pointage::LigneComplete> Pointage::lignes_completes() const
{
    std::vector<LigneComplete> lignes;
    for (int i = 0; i < len(); ++i) {
        std::vector<Vecteur> positions;
        for (const Position &p : m_data[i]) {
            if (!p)
                break;
            positions.push_back(point_en_metre(*p));
        }
        if (static_cast<int>(positions.size()) == m_nb_obj)
            lignes.push_back({i, date_relative(i), std::move(positions)});
    }
    return lignes;
}

void Pointage::pointe(int obj, Vecteur position, int index)
{
    if (index >= 0 && index < len() && obj >= 1 && obj <= m_nb_obj)
        m_data[index][obj - 1] = position;
}

Position Pointage::position(int obj, int index) const
{
    if (index < 0 || index >= len() || obj < 1 || obj > m_nb_obj)
        return std::nullopt;
    return m_data[index][obj - 1];
}

bool Pointage::a_des_pointages() const
{
    return std::any_of(m_data.begin(), m_data.end(), [](const auto &l) { return l[0].has_value(); });
}

std::optional<int> Pointage::premiere_image() const
{
    for (int i = 0; i < len(); ++i) {
        if (m_data[i][0])
            return i + 1;
    }
    return std::nullopt;
}

std::optional<int> Pointage::derniere_image() const
{
    for (int i = len() - 1; i >= 0; --i) {
        if (m_data[i][0])
            return i + 1;
    }
    return std::nullopt;
}

void Pointage::defaire()
{
    if (const auto der = derniere_image()) {
        const int i = *der - 1;
        m_defaits.emplace_back(i, std::exchange(m_data[i], std::vector<Position>(m_nb_obj)));
    }
}

void Pointage::refaire()
{
    if (m_defaits.empty())
        return;
    auto [i, pointage] = std::move(m_defaits.back());
    m_defaits.pop_back();
    if (i < len())
        m_data[i] = std::move(pointage);
}

QString Pointage::csv_string(const QString &sep, bool en_metre, int debut, Vecteur org) const
{
    const double mul = en_metre ? echelle.m_par_px() : 1.0;
    QStringList lignes;
    QStringList en_tete{u"t"_s};
    for (int o = 1; o <= m_nb_obj; ++o) {
        en_tete << u"x%1"_s.arg(o);
        en_tete << u"y%1"_s.arg(o);
    }
    lignes << en_tete.join(sep);
    // la date zéro correspond à la première image pointée
    debut = std::max(debut, 1) - 1;
    for (int i = debut; i < len(); ++i) {
        QStringList ligne{QString::fromStdString(std::format("{:.3f}", date(i) - date(debut)))};
        for (const Position &p : m_data[i]) {
            if (!p)
                continue;
            ligne << fmt_g(sens_x * (p->x - org.x) * mul, 6);
            ligne << fmt_g(-sens_y * (p->y - org.y) * mul, 6);
        }
        lignes << ligne.join(sep);
    }
    lignes << QString();
    return lignes.join(u'\n');
}

Vecteur Pointage::point_en_metre(Vecteur p) const
{
    const double m = echelle.m_par_px();
    return {sens_x * (p.x - origine.x) * m, sens_y * (origine.y - p.y) * m};
}

Vecteur Pointage::vitesse_en_metre(Vecteur v) const
{
    const double m = echelle.m_par_px();
    return {sens_x * v.x * m, -sens_y * v.y * m};
}

Position Pointage::vitesse_px(int obj, int index) const
{
    if (index == 0)
        return std::nullopt;
    const Position p2 = position(obj, index);
    const Position p1 = position(obj, index - 1);
    if (!p1 || !p2)
        return std::nullopt;
    const double dt = date(index) - date(index - 1);
    if (!(dt > 0.0))
        return std::nullopt;
    return (*p2 - *p1) * (1.0 / dt);
}

std::vector<std::vector<std::pair<Vecteur, Vecteur>>> Pointage::vecteurs_vitesse(double echelle_vitesse) const
{
    std::vector<std::vector<std::pair<Vecteur, Vecteur>>> res;
    for (int obj = 1; obj <= m_nb_obj; ++obj) {
        // (date, position) là où l'objet est pointé
        std::vector<std::pair<double, Vecteur>> traj;
        for (int i = 0; i < len(); ++i) {
            if (const Position p = position(obj, i))
                traj.emplace_back(date(i), *p);
        }
        std::vector<std::pair<Vecteur, Vecteur>> vecteurs;
        for (size_t k = 0; k + 2 < traj.size(); ++k) {
            const auto &[t1, precedent] = traj[k];
            const Vecteur point = traj[k + 1].second;
            const auto &[t3, suivant] = traj[k + 2];
            if (!(t3 > t1))
                continue;
            Vecteur vitesse = (point_en_metre(suivant) - point_en_metre(precedent)) * (1.0 / (t3 - t1));
            // l'axe Y de l'écran est vers le bas
            vitesse = {sens_x * vitesse.x, -sens_y * vitesse.y};
            vecteurs.emplace_back(point, point + vitesse * echelle_vitesse);
        }
        res.push_back(std::move(vecteurs));
    }
    return res;
}

void Pointage::restaure_pointages(const std::vector<std::vector<double>> &lignes, int premiere_image_pointee)
{
    const double px_par_m = echelle.px_par_m();
    for (size_t i = 0; i < lignes.size(); ++i) {
        const int index = static_cast<int>(i) + std::max(premiere_image_pointee, 1) - 1;
        const auto &ligne = lignes[i];
        for (int obj = 1; obj <= m_nb_obj; ++obj) {
            const size_t j = 2 * obj - 1;
            if (j + 1 >= ligne.size())
                continue;
            const double x = ligne[j], y = ligne[j + 1];
            pointe(obj, {origine.x + sens_x * x * px_par_m, origine.y - sens_y * y * px_par_m}, index);
        }
    }
}

Pointage Pointage::dans_referentiel(int obj_ref, Vecteur centre) const
{
    Pointage p = *this;
    for (auto &ligne : p.m_data) {
        const Position reference = (obj_ref >= 1 && obj_ref <= m_nb_obj) ? ligne[obj_ref - 1] : std::nullopt;
        for (Position &q : ligne) {
            if (q && reference)
                q = *q - *reference + centre;
            else
                q.reset();
        }
    }
    p.origine = centre;
    return p;
}
