#include "core/series.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace series {

namespace {

/// Supprime le bruit des flottants : 0.1 * 3 donne 0.3 et non 0.30000000000000004.
double clean(double x)
{
    if (!std::isfinite(x) || x == 0.0)
        return x;
    return QString::number(x, 'e', 12).toDouble();
}

} // namespace

Result byStep(double start, double end, double step)
{
    if (!(std::isfinite(start) && std::isfinite(end) && std::isfinite(step)))
        return {{}, u"valeurs non numériques"_s};
    if (step == 0.0)
        return {{}, u"le pas ne peut pas être nul"_s};
    if ((end - start) * step < 0.0) {
        return {{}, step > 0 ? u"pas positif : la fin doit dépasser le début"_s
                             : u"pas négatif : la fin doit être inférieure au début"_s};
    }
    // tolérance relative : de 0 à 1 par pas de 0,1 donne bien 11 valeurs
    const double span = (end - start) / step;
    const double n = std::floor(span + 1e-9 * std::max(std::abs(span), 1.0)) + 1.0;
    if (n > static_cast<double>(MAX_VALUES))
        return {{}, u"trop de valeurs (%1, au plus %2)"_s.arg(n, 0, 'f', 0).arg(MAX_VALUES)};
    std::vector<double> out(static_cast<size_t>(n));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = clean(start + static_cast<double>(i) * step);
    return {out, {}};
}

Result byCount(double start, double end, size_t n)
{
    if (!(std::isfinite(start) && std::isfinite(end)))
        return {{}, u"valeurs non numériques"_s};
    if (n == 0)
        return {{}, u"il faut au moins une valeur"_s};
    if (n > MAX_VALUES)
        return {{}, u"trop de valeurs (au plus %1)"_s.arg(MAX_VALUES)};
    if (n == 1)
        return {{clean(start)}, {}};
    const double h = (end - start) / static_cast<double>(n - 1);
    std::vector<double> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = i == n - 1 ? clean(end) : clean(start + static_cast<double>(i) * h);
    return {out, {}};
}

std::vector<double> extend(const std::vector<double> &selected, size_t count)
{
    std::vector<std::pair<double, double>> pts;
    for (size_t i = 0; i < selected.size(); ++i) {
        if (std::isfinite(selected[i]))
            pts.emplace_back(static_cast<double>(i), selected[i]);
    }
    const size_t n = selected.size();
    if (pts.empty())
        return std::vector<double>(count, NAN);
    if (pts.size() == 1)
        return std::vector<double>(count, pts[0].second);
    double mx = 0, my = 0;
    for (auto [x, y] : pts) {
        mx += x;
        my += y;
    }
    mx /= static_cast<double>(pts.size());
    my /= static_cast<double>(pts.size());
    double sxx = 0, sxy = 0;
    for (auto [x, y] : pts) {
        sxx += (x - mx) * (x - mx);
        sxy += (x - mx) * (y - my);
    }
    const double slope = sxy / sxx;
    std::vector<double> out(count);
    for (size_t k = 0; k < count; ++k)
        out[k] = clean(my + slope * (static_cast<double>(n + k) - mx));
    return out;
}

} // namespace series
