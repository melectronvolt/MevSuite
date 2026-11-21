#include "core/titration.h"

#include "core/numeric.h"

#include <cmath>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace titration {

std::optional<Curve> Curve::fabrique(const std::vector<double> &v, const std::vector<double> &ph, QString *error)
{
    const auto [x, y] = numeric::paires_triees(v, ph);
    if (!maximum_derivee(x, y, error))
        return std::nullopt;
    if (x.size() < 5) {
        if (error)
            *error = u"Il faut au moins cinq points pour construire les tangentes."_s;
        return std::nullopt;
    }
    auto sp = spline::Hermite::fabrique(x, y, spline::Kind::Monotone);
    if (!sp) {
        if (error)
            *error = u"courbe inexploitable"_s;
        return std::nullopt;
    }
    Curve c;
    c.m_spline = *sp;
    c.m_xMin = x.front();
    c.m_xMax = x.back();
    // saut : maximum de |pente| sur une grille fine
    const int n = 4000;
    double best = -1.0;
    c.m_jump = c.m_xMin;
    for (int i = 0; i <= n; ++i) {
        const double t = c.m_xMin + (c.m_xMax - c.m_xMin) * i / n;
        const double d = std::abs(c.m_spline.derive(t));
        if (d > best) {
            best = d;
            c.m_jump = t;
        }
    }
    return c;
}

double Curve::contact_par_defaut() const
{
    const double smax = std::abs(pente(m_jump));
    const int n = 2000;
    double x = m_jump;
    for (int i = 1; i <= n; ++i) {
        const double t = m_jump - (m_jump - m_xMin) * i / n;
        if (std::abs(pente(t)) <= 0.1 * smax)
            return t;
        x = t;
    }
    return x;
}

std::optional<Tangents> Curve::tangentes(double x1, QString *error) const
{
    auto fail = [&](const QString &m) -> std::optional<Tangents> {
        if (error)
            *error = m;
        return std::nullopt;
    };
    x1 = std::clamp(x1, m_xMin, m_xMax);
    const double s = pente(x1);
    const bool before = x1 < m_jump;
    if (x1 == m_jump)
        return fail(u"Déplacez le point de contact de part ou d’autre du maximum de pente."_s);
    // De l’autre côté du saut, la pente doit avoir le même signe et la même valeur.
    const double fin = before ? m_xMax : m_xMin;
    const int n = 2000;
    double lo = m_jump, hi = m_jump;
    double ecartLo = pente(lo) - s;
    for (int i = 1; i <= n; ++i) {
        hi = m_jump + (fin - m_jump) * i / n;
        const double ecart = pente(hi) - s;
        if (ecart == 0.0 || std::signbit(ecart) != std::signbit(ecartLo)) break;
        lo = hi;
        ecartLo = ecart;
    }
    if (lo == hi)
        return fail(u"Aucun second point de même pente. Déplacez le point de contact."_s);
    for (int k = 0; k < 60; ++k) {
        const double mid = 0.5 * (lo + hi);
        const double ecart = pente(mid) - s;
        if (ecart == 0.0 || std::signbit(ecart) != std::signbit(ecartLo))
            hi = mid;
        else
            lo = mid;
    }
    const double x2 = 0.5 * (lo + hi);
    const double xa = before ? x1 : x2;
    const double xb = before ? x2 : x1;
    const double ya = valeur(xa), yb = valeur(xb);
    Tangents t;
    t.slope = s;
    t.p1 = {xa, ya};
    t.p2 = {xb, yb};
    t.b1 = ya - s * xa;
    t.b2 = yb - s * xb;
    t.bMedian = 0.5 * (t.b1 + t.b2);
    // intersection médiane / courbe entre les deux points de contact
    auto g = [&](double x) { return valeur(x) - (s * x + t.bMedian); };
    lo = xa;
    hi = xb;
    const double glo = g(lo), ghi = g(hi);
    if (glo * ghi > 0.0)
        return fail(u"la médiane ne coupe pas la courbe entre les points de contact"_s);
    const bool increasing = glo < ghi;
    for (int k = 0; k < 80; ++k) {
        const double mid = 0.5 * (lo + hi);
        if ((g(mid) < 0.0) == increasing)
            lo = mid;
        else
            hi = mid;
    }
    const double xe = 0.5 * (lo + hi);
    t.equivalence = {xe, valeur(xe)};
    return t;
}

std::optional<std::pair<double, double>> maximum_derivee(const std::vector<double> &v, const std::vector<double> &ph, QString *error)
{
    const auto [xs, ys] = numeric::paires_triees(v, ph);
    if (xs.size() < 3) {
        if (error)
            *error = u"il faut au moins 3 points de mesure"_s;
        return std::nullopt;
    }
    if (std::adjacent_find(xs.begin(), xs.end()) != xs.end()) {
        if (error)
            *error = u"Deux mesures ont la même abscisse. Regroupez-les avant de calculer les pentes."_s;
        return std::nullopt;
    }
    // La grandeur deriv(pH, V) utilise les lignes du tableur dans leur ordre de saisie.
    const auto indices = numeric::indices_valides(ph, v);
    bool monte = true, descend = true;
    for (size_t i = 1; i < indices.size(); ++i) {
        monte &= v[indices[i]] > v[indices[i - 1]];
        descend &= v[indices[i]] < v[indices[i - 1]];
    }
    if (!monte && !descend) {
        if (error) *error = u"Triez le tableur par abscisse avant de calculer la dérivée."_s;
        return std::nullopt;
    }
    const auto d = numeric::gradient(ys, xs);
    std::optional<size_t> best;
    for (size_t i = 0; i < d.size(); ++i) {
        if (std::isfinite(d[i]) && (!best || std::abs(d[i]) >= std::abs(d[*best])))
            best = i;
    }
    if (!best) {
        if (error)
            *error = u"dérivée incalculable"_s;
        return std::nullopt;
    }
    const size_t k = *best;
    const double pic = std::abs(d[k]);
    const double bord = std::max(std::abs(d.front()), std::abs(d.back()));
    if (!(pic > bord + 1e-10 * pic) || k == 0 || k + 1 == d.size()
        || !std::isfinite(d[k - 1]) || !std::isfinite(d[k + 1])) {
        if (error)
            *error = u"Il faut un maximum de pente en valeur absolue à l’intérieur de la plage, avec des mesures de part et d’autre."_s;
        return std::nullopt;
    }
    const double x0 = xs[k - 1], x1 = xs[k], x2 = xs[k + 1];
    const double y0 = std::abs(d[k - 1]), y1 = std::abs(d[k]), y2 = std::abs(d[k + 1]);
    const double denom = (x0 - x1) * (x0 - x2) * (x1 - x2);
    if (denom == 0.0)
        return std::make_pair(x1, d[k]);
    const double a = (x2 * (y1 - y0) + x1 * (y0 - y2) + x0 * (y2 - y1)) / denom;
    const double b = (x2 * x2 * (y0 - y1) + x1 * x1 * (y2 - y0) + x0 * x0 * (y1 - y2)) / denom;
    if (a >= 0.0)
        return std::make_pair(x1, d[k]);
    return std::make_pair(std::clamp(-b / (2.0 * a), x0, x2), d[k]);
}

} // namespace titration
