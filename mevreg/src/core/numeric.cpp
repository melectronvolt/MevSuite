#include "core/numeric.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace numeric {

std::vector<size_t> indices_valides(const Vec &y, const Vec &x)
{
    std::vector<size_t> idx;
    const size_t n = std::min(y.size(), x.size());
    for (size_t i = 0; i < n; ++i) {
        if (std::isfinite(y[i]) && std::isfinite(x[i]))
            idx.push_back(i);
    }
    return idx;
}

Vec gradient(const Vec &y, const Vec &x)
{
    const size_t n = std::max(y.size(), x.size());
    Vec out(n, NAN);
    const auto idx = indices_valides(y, x);
    if (idx.size() < 2)
        return out;
    Vec ys, xs;
    for (size_t i : idx) {
        ys.push_back(y[i]);
        xs.push_back(x[i]);
    }
    const Vec g = gradient_numpy(ys, xs);
    for (size_t k = 0; k < idx.size(); ++k)
        out[idx[k]] = std::isfinite(g[k]) ? g[k] : NAN;
    return out;
}

Vec gradient_numpy(const Vec &f, const Vec &x)
{
    const size_t n = f.size();
    if (n < 2 || (x.size() != 1 && x.size() != n))
        return Vec(n, NAN);
    Vec g(n, 0.0);
    // écarts entre abscisses ; constants : formule du pas constant
    Vec dx;
    if (x.size() == 1) {
        dx.assign(n - 1, x[0]);
    } else {
        for (size_t i = 0; i + 1 < x.size(); ++i)
            dx.push_back(x[i + 1] - x[i]);
    }
    const bool uniform = std::all_of(dx.begin(), dx.end(), [&](double d) { return d == dx[0]; });
    if (uniform) {
        const double h = dx[0];
        for (size_t i = 1; i + 1 < n; ++i)
            g[i] = (f[i + 1] - f[i - 1]) / (2.0 * h);
        g[0] = (f[1] - f[0]) / h;
        g[n - 1] = (f[n - 1] - f[n - 2]) / h;
        return g;
    }
    for (size_t i = 1; i + 1 < n; ++i) {
        const double dx1 = dx[i - 1], dx2 = dx[i];
        const double a = -dx2 / (dx1 * (dx1 + dx2));
        const double b = (dx2 - dx1) / (dx1 * dx2);
        const double c = dx1 / (dx2 * (dx1 + dx2));
        g[i] = a * f[i - 1] + b * f[i] + c * f[i + 1];
    }
    g[0] = (f[1] - f[0]) / dx[0];
    g[n - 1] = (f[n - 1] - f[n - 2]) / dx[n - 2];
    return g;
}

Vec trapezes_cumules(const Vec &y, const Vec &x)
{
    const size_t n = std::max(y.size(), x.size());
    Vec out(n, NAN);
    const auto idx = indices_valides(y, x);
    if (idx.empty())
        return out;
    double acc = 0.0;
    out[idx[0]] = 0.0;
    for (size_t k = 1; k < idx.size(); ++k) {
        const size_t a = idx[k - 1], b = idx[k];
        acc += 0.5 * (y[a] + y[b]) * (x[b] - x[a]);
        out[b] = acc;
    }
    return out;
}

double trapezes(const Vec &y, const Vec &x)
{
    const auto idx = indices_valides(y, x);
    double s = 0.0;
    for (size_t k = 1; k < idx.size(); ++k) {
        const size_t a = idx[k - 1], b = idx[k];
        s += 0.5 * (y[a] + y[b]) * (x[b] - x[a]);
    }
    return s;
}

double aire_entre(const Vec &y, const Vec &x, double a, double b)
{
    const double lo = std::min(a, b), hi = std::max(a, b), sign = a <= b ? 1.0 : -1.0;
    const auto [xs, ys] = paires_triees(x, y);
    if (xs.size() < 2)
        return NAN;
    double area = 0.0;
    for (size_t k = 0; k + 1 < xs.size(); ++k) {
        const double x0 = xs[k], x1 = xs[k + 1], y0 = ys[k], y1 = ys[k + 1];
        const double s = std::max(x0, lo), e = std::min(x1, hi);
        if (e <= s || x1 <= x0)
            continue;
        const double ya = y0 + (y1 - y0) * (s - x0) / (x1 - x0);
        const double yb = y0 + (y1 - y0) * (e - x0) / (x1 - x0);
        area += 0.5 * (ya + yb) * (e - s);
    }
    return sign * area;
}

std::pair<Vec, Vec> paires_triees(const Vec &x, const Vec &y)
{
    auto idx = indices_valides(y, x);
    const auto parAbscisse = [&](size_t a, size_t b) { return x[a] < x[b]; };
    if (!std::is_sorted(idx.begin(), idx.end(), parAbscisse))
        std::stable_sort(idx.begin(), idx.end(), parAbscisse);
    Vec xs, ys;
    xs.reserve(idx.size());
    ys.reserve(idx.size());
    for (size_t i : idx) {
        xs.push_back(x[i]);
        ys.push_back(y[i]);
    }
    return {std::move(xs), std::move(ys)};
}

/// Les abscisses encadrent la lecture et sont distinctes.
static double interpole_intervalle(double abscisse, double x0, double x1, double y0, double y1)
{
    const long double fraction = (static_cast<long double>(abscisse) - x0) / (static_cast<long double>(x1) - x0);
    return std::lerp(y0, y1, static_cast<double>(fraction));
}

double interpole(double t, const Vec &xs, const Vec &ys)
{
    const size_t n = xs.size();
    if (n == 0 || ys.size() != n || !std::isfinite(t) || !std::isfinite(xs.front()) || !std::isfinite(xs.back()))
        return NAN;
    if (t <= xs[0])
        return ys[0];
    if (t >= xs[n - 1])
        return ys[n - 1];
    const size_t droite = static_cast<size_t>(std::upper_bound(xs.begin(), xs.end(), t) - xs.begin());
    return interpole_intervalle(t, xs[droite - 1], xs[droite], ys[droite - 1], ys[droite]);
}

std::optional<std::pair<double, double>> infos_pas(const Vec &x)
{
    size_t effectif = 0;
    long double premier = 0, precedent = 0;
    long double pasMin = std::numeric_limits<long double>::infinity(), pasMax = -pasMin;
    for (double instant : x) {
        if (!std::isfinite(instant))
            continue;
        if (effectif == 0) {
            premier = instant;
        } else {
            const long double pas = instant - precedent;
            pasMin = std::min(pasMin, pas);
            pasMax = std::max(pasMax, pas);
        }
        precedent = instant;
        ++effectif;
    }
    if (effectif < 2 || precedent == premier)
        return std::nullopt;
    const long double moyen = (precedent - premier) / (effectif - 1);
    const long double ecartMax = std::max(pasMax - moyen, moyen - pasMin);
    return std::pair{static_cast<double>(moyen), static_cast<double>(ecartMax / std::abs(moyen))};
}

std::pair<Vec, Vec> reechantillonne_uniforme(const Vec &x, const Vec &y, size_t n)
{
    auto [xs, ys] = paires_triees(x, y);
    if (xs.size() < 2 || n < 2)
        return {std::move(xs), std::move(ys)};
    Vec grille(n), valeurs(n);
    size_t droite = 1;
    for (size_t i = 0; i < n; ++i) {
        const double t = std::lerp(xs.front(), xs.back(), static_cast<double>(i) / static_cast<double>(n - 1));
        grille[i] = t;
        if (t <= xs.front())
            valeurs[i] = ys.front();
        else if (t >= xs.back())
            valeurs[i] = ys.back();
        else {
            // Les instants de la grille se suivent. Chaque intervalle de
            // l'acquisition est parcouru une seule fois, même après un suréchantillonnage.
            while (xs[droite] <= t)
                ++droite;
            valeurs[i] = interpole_intervalle(t, xs[droite - 1], xs[droite], ys[droite - 1], ys[droite]);
        }
    }
    return {std::move(grille), std::move(valeurs)};
}

std::optional<std::pair<double, double>> plage_finie(const Vec &v)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double x : v) {
        if (std::isfinite(x)) {
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
    }
    if (lo <= hi)
        return std::make_pair(lo, hi);
    return std::nullopt;
}

} // namespace numeric
