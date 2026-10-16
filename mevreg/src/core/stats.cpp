#include "core/stats.h"

#include <algorithm>
#include <cmath>

namespace stats {

namespace {

std::vector<double> finite(const std::vector<double> &values)
{
    std::vector<double> v;
    for (double x : values) {
        if (std::isfinite(x))
            v.push_back(x);
    }
    return v;
}

} // namespace

double stdDev(const std::vector<double> &v, size_t ddof)
{
    const size_t n = v.size();
    if (n <= ddof)
        return NAN;
    double m = 0.0;
    for (double x : v)
        m += x;
    m /= static_cast<double>(n);
    double ss = 0.0;
    for (double x : v)
        ss += (x - m) * (x - m);
    return std::sqrt(ss / static_cast<double>(n - ddof));
}

std::optional<Summary> summarize(const std::vector<double> &values)
{
    const auto v = finite(values);
    if (v.empty())
        return std::nullopt;
    Summary s;
    s.n = v.size();
    double sum = 0.0;
    for (double x : v)
        sum += x;
    s.mean = sum / static_cast<double>(s.n);
    s.stdSample = stdDev(v, 1);
    s.stdPop = stdDev(v, 0);
    s.min = *std::min_element(v.begin(), v.end());
    s.max = *std::max_element(v.begin(), v.end());
    s.uA = s.stdSample / std::sqrt(static_cast<double>(s.n));
    return s;
}

size_t defaultBins(size_t n)
{
    if (n < 2)
        return 1;
    const auto b = static_cast<size_t>(std::ceil(std::log2(static_cast<double>(n)) + 1.0));
    return std::clamp<size_t>(b, 3, 30);
}

std::optional<Histogram> histogram(const std::vector<double> &values, size_t bins)
{
    const auto v = finite(values);
    if (v.empty())
        return std::nullopt;
    bins = std::max<size_t>(bins, 1);
    double lo = *std::min_element(v.begin(), v.end());
    double hi = *std::max_element(v.begin(), v.end());
    if (lo == hi) {
        lo -= 0.5;
        hi += 0.5;
    }
    const double w = (hi - lo) / static_cast<double>(bins);
    Histogram h;
    for (size_t i = 0; i <= bins; ++i)
        h.edges.push_back(lo + w * static_cast<double>(i));
    h.counts.assign(bins, 0);
    for (double x : v) {
        auto k = static_cast<long long>(std::floor((x - lo) / w));
        k = std::clamp<long long>(k, 0, static_cast<long long>(bins) - 1);
        // correction des erreurs d'arrondi au voisinage des bornes
        if (static_cast<size_t>(k) + 1 < bins && x >= h.edges[k + 1])
            ++k;
        else if (k > 0 && x < h.edges[k])
            --k;
        ++h.counts[k];
    }
    return h;
}

} // namespace stats
