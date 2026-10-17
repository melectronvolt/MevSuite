#include "calculs.h"

#include "format.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

/// les lettres d'un nom de grandeur, sans le numéro d'objet
QString lettres_du_nom(const QString &nom)
{
    qsizetype n = 0;
    while (n < nom.size() && !(nom[n] >= u'0' && nom[n] <= u'9'))
        ++n;
    return nom.left(n);
}

} // namespace

QString nom_grandeur(const QString &lettres, int obj, int nb_obj)
{
    return nb_obj > 1 ? lettres + QString::number(obj) : lettres;
}

std::vector<Serie> grandeurs(const Pointage &p)
{
    const int n = p.len();
    std::vector<Serie> series;
    for (int obj = 1; obj <= p.nb_obj(); ++obj) {
        std::vector<Position> pos(n), vit(n);
        for (int i = 0; i < n; ++i) {
            if (const Position q = p.position(obj, i))
                pos[i] = p.point_en_metre(*q);
            if (const Position v = p.vitesse_px(obj, i))
                vit[i] = p.vitesse_en_metre(*v);
        }
        // vit[i] est la vitesse moyenne entre les images i − 1 et i, au milieu de cet intervalle : deux
        // vitesses successives sont donc séparées de (t[i] − t[i − 2]) / 2, qui n'est égal à t[i] − t[i − 1]
        // que si la cadence est régulière (une image perdue, une vidéo à cadence variable)
        const auto derivee = [&](double Vecteur::*composante) {
            std::vector<std::optional<double>> d(n);
            for (int i = 2; i < n; ++i) {
                if (!vit[i] || !vit[i - 1])
                    continue;
                const double dt = (p.date(i) - p.date(i - 2)) / 2.0;
                if (dt > 0.0)
                    d[i] = ((*vit[i]).*composante - (*vit[i - 1]).*composante) / dt;
            }
            return d;
        };
        const auto ax = derivee(&Vecteur::x);
        const auto ay = derivee(&Vecteur::y);
        std::vector<std::optional<double>> a(n);
        for (int i = 0; i < n; ++i) {
            if (ax[i] && ay[i])
                a[i] = Vecteur(*ax[i], *ay[i]).norme();
        }
        const auto composante = [&](const std::vector<Position> &v, const std::function<double(Vecteur)> &f) {
            std::vector<std::optional<double>> c(n);
            for (int i = 0; i < n; ++i) {
                if (v[i])
                    c[i] = f(*v[i]);
            }
            return c;
        };
        const std::pair<QString, std::vector<std::optional<double>>> colonnes[] = {
            {u"x"_s, composante(pos, [](Vecteur q) { return q.x; })},
            {u"y"_s, composante(pos, [](Vecteur q) { return q.y; })},
            {u"v"_s, composante(vit, [](Vecteur v) { return v.norme(); })},
            {u"vx"_s, composante(vit, [](Vecteur v) { return v.x; })},
            {u"vy"_s, composante(vit, [](Vecteur v) { return v.y; })},
            {u"a"_s, a},
            {u"ax"_s, ax},
            {u"ay"_s, ay},
        };
        for (const auto &[nom, valeurs] : colonnes)
            series.push_back({nom_grandeur(nom, obj, p.nb_obj()), valeurs});
    }
    return series;
}

QString unite(const QString &nom, bool metre)
{
    const QString lettres = lettres_du_nom(nom);
    if (lettres == u"t")
        return u"s"_s;
    if (lettres == u"v" || lettres == u"vx" || lettres == u"vy")
        return metre ? u"m/s"_s : u"px/s"_s;
    if (lettres == u"a" || lettres == u"ax" || lettres == u"ay")
        return metre ? u"m/s²"_s : u"px/s²"_s;
    return metre ? u"m"_s : u"px"_s;
}

QString texte_date(double t, double delta_t)
{
    const int decimales = delta_t > 0.0
        ? std::clamp(static_cast<int>(std::ceil(-std::log10(delta_t))) + 1, 3, 6)
        : 3;
    return QString::fromStdString(std::format("{:.{}f}", t, decimales)).replace(u'.', separateur_decimal());
}

Tableau Tableau::construit(const Pointage &p)
{
    Tableau t;
    const QString u = p.unite();
    t.en_tetes << u"t (s)"_s;
    for (int obj = 1; obj <= p.nb_obj(); ++obj) {
        t.en_tetes << u"%1 (%2)"_s.arg(nom_grandeur(u"x"_s, obj, p.nb_obj()), u);
        t.en_tetes << u"%1 (%2)"_s.arg(nom_grandeur(u"y"_s, obj, p.nb_obj()), u);
    }
    for (int i = 0; i < p.len(); ++i) {
        const auto &l = p.ligne(i);
        if (std::none_of(l.begin(), l.end(), [](const Position &q) { return q.has_value(); }))
            continue;
        t.images.push_back(i);
        QStringList ligne{texte_date(p.date_relative(i), p.delta_t)};
        for (const Position &q : l) {
            if (q) {
                const Vecteur m = p.point_en_metre(*q);
                ligne << nombre_local(m.x, 4) << nombre_local(m.y, 4);
            } else {
                ligne << QString() << QString();
            }
        }
        t.lignes.push_back(ligne);
    }
    return t;
}
