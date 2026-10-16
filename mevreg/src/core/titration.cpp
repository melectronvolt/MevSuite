#include "core/titration.h"

#include "core/numeric.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace titration {

std::optional<Curve> Curve::make(const std::vector<double> &v, const std::vector<double> &ph, QString *error)
{
    const auto [xs, ys] = numeric::sortedPairs(v, ph);
    // abscisses répétées : on garde la première mesure
    std::vector<double> x, y;
    for (size_t i = 0; i < xs.size(); ++i) {
        if (x.empty() || xs[i] > x.back()) {
            x.push_back(xs[i]);
            y.push_back(ys[i]);
        }
    }
    if (x.size() < 5) {
        if (error)
            *error = u"il faut au moins 5 points de mesure pour exploiter la courbe"_s;
        return std::nullopt;
    }
    auto sp = spline::Hermite::make(x, y, spline::Kind::Monotone);
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
        const double d = std::abs(c.m_spline.deriv(t));
        if (d > best) {
            best = d;
            c.m_jump = t;
        }
    }
    return c;
}

double Curve::defaultContact() const
{
    const double smax = std::abs(slope(m_jump));
    const int n = 2000;
    double x = m_jump;
    for (int i = 1; i <= n; ++i) {
        const double t = m_jump - (m_jump - m_xMin) * i / n;
        if (std::abs(slope(t)) <= 0.1 * smax)
            return t;
        x = t;
    }
    return x;
}

std::optional<Tangents> Curve::tangents(double x1, QString *error) const
{
    auto fail = [&](const QString &m) -> std::optional<Tangents> {
        if (error)
            *error = m;
        return std::nullopt;
    };
    x1 = std::clamp(x1, m_xMin, m_xMax);
    const double s = slope(x1);
    const double target = std::abs(s);
    const bool before = x1 < m_jump;
    // recherche, de l'autre côté du saut, du point de même pente
    const double a = before ? m_jump : m_xMin;
    const double b = before ? m_xMax : m_jump;
    const int n = 2000;
    std::optional<std::pair<double, double>> found;
    double prev = m_jump;
    for (int i = 1; i <= n; ++i) {
        const double t = before ? a + (b - a) * i / n : b - (b - a) * i / n;
        if (std::abs(slope(t)) <= target) {
            found = std::make_pair(prev, t);
            break;
        }
        prev = t;
    }
    if (!found)
        return fail(u"pas de point de même pente de l'autre côté du saut : déplacez le point de contact"_s);
    double lo = found->first, hi = found->second;
    for (int k = 0; k < 60; ++k) {
        const double mid = 0.5 * (lo + hi);
        if (std::abs(slope(mid)) > target)
            lo = mid;
        else
            hi = mid;
    }
    const double x2 = 0.5 * (lo + hi);
    const double xa = before ? x1 : x2;
    const double xb = before ? x2 : x1;
    const double ya = value(xa), yb = value(xb);
    Tangents t;
    t.slope = s;
    t.p1 = {xa, ya};
    t.p2 = {xb, yb};
    t.b1 = ya - s * xa;
    t.b2 = yb - s * xb;
    t.bMedian = 0.5 * (t.b1 + t.b2);
    // intersection médiane / courbe entre les deux points de contact
    auto g = [&](double x) { return value(x) - (s * x + t.bMedian); };
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
    t.equivalence = {xe, value(xe)};
    return t;
}

std::optional<std::pair<double, double>> derivativeMaximum(const std::vector<double> &v, const std::vector<double> &ph, QString *error)
{
    const auto [xs, ys] = numeric::sortedPairs(v, ph);
    if (xs.size() < 3) {
        if (error)
            *error = u"il faut au moins 3 points de mesure"_s;
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
    if (k == 0 || k == d.size() - 1 || !std::isfinite(d[k - 1]) || !std::isfinite(d[k + 1]))
        return std::make_pair(xs[k], d[k]);
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
