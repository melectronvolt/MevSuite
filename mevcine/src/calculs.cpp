#include "calculs.h"

#include "format.h"
#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <format>

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

std::vector<Grandeur> grandeurs_disponibles(int nombre_objets)
{
    using enum Grandeur::Type;
    const std::pair<Grandeur::Type, QString> composantes[] = {
        {X, u"x"_s}, {Y, u"y"_s}, {Vitesse, u"v"_s}, {Vx, u"vx"_s}, {Vy, u"vy"_s},
        {Acceleration, u"a"_s}, {Ax, u"ax"_s}, {Ay, u"ay"_s}
    };
    std::vector<Grandeur> grandeurs{{Temps, 0, u"t"_s}};
    for (int objet = 1; objet <= nombre_objets; ++objet)
        for (const auto &[type, symbole] : composantes)
            grandeurs.push_back({type, objet, nom_grandeur(symbole, objet, nombre_objets)});
    return grandeurs;
}

std::optional<double> Grandeur::valeur(const Pointage &pointage, int index_image) const
{
    using enum Type;
    if (type == Temps) {
        if (pointage.positions_par_image().contains(index_image))
            return pointage.date_relative(index_image);
        return {};
    }
    if (type == X || type == Y) {
        const Position position = pointage.point_de_l_objet(objet, index_image);
        if (!position)
            return {};
        const Vecteur coordonnees = pointage.position_dans_repere(*position);
        return type == X ? coordonnees.x : coordonnees.y;
    }
    const auto mouvement = pointage.cinematique_px(objet, index_image);
    if (!mouvement)
        return {};
    const bool est_vitesse = type == Vitesse || type == Vx || type == Vy;
    const Vecteur vecteur = pointage.vecteur_dans_repere(est_vitesse ? mouvement->vitesse : mouvement->acceleration);
    if (type == Vx || type == Ax)
        return vecteur.x;
    if (type == Vy || type == Ay)
        return vecteur.y;
    return vecteur.module();
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

Tableau Tableau::construit(const Pointage &p, bool numeros_seuls)
{
    Tableau t;
    if (numeros_seuls) {
        t.en_tetes << trad("n°");
    } else {
        const QString u = p.unite();
        t.en_tetes << u"t (s)"_s;
        for (int obj = 1; obj <= p.nombre_objets(); ++obj) {
            t.en_tetes << u"%1 (%2)"_s.arg(nom_grandeur(u"x"_s, obj, p.nombre_objets()), u);
            t.en_tetes << u"%1 (%2)"_s.arg(nom_grandeur(u"y"_s, obj, p.nombre_objets()), u);
        }
    }
    t.images = p.images_mesurees();
    t.lignes.reserve(t.images.size());
    const double t0 = t.images.empty() ? 0.0 : p.date(t.images.front());
    for (const int i : t.images) {
        if (numeros_seuls) {
            t.lignes.push_back({QString::number(i + 1)});
            continue;
        }
        QStringList ligne{texte_date(p.date(i) - t0, p.delta_t)};
        for (const Position &q : p.ligne(i)) {
            if (q) {
                const Vecteur m = p.position_dans_repere(*q);
                ligne << nombre_local(m.x, 4) << nombre_local(m.y, 4);
            } else {
                ligne << QString() << QString();
            }
        }
        t.lignes.push_back(ligne);
    }
    return t;
}
