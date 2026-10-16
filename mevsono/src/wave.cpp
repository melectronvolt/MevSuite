#include "wave.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

/// Taille des plus petits blocs précalculés.
constexpr size_t BASE_BLOCK = 64;

using Range = std::pair<float, float>;

constexpr Range EMPTY = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};

Range merge(Range a, Range b)
{
    return {std::min(a.first, b.first), std::max(a.second, b.second)};
}

Range scan(std::span<const float> samples)
{
    Range r = EMPTY;
    for (float x : samples)
        r = {std::min(r.first, x), std::max(r.second, x)};
    return r;
}

/// Indice d'échantillon d'un instant, borné entre 0 et `len` (comme `x as usize` en Rust).
size_t clampIndex(double x, size_t len)
{
    if (!(x > 0.0))
        return 0;
    return x >= double(len) ? len : static_cast<size_t>(x);
}

} // namespace

Envelope::Envelope(std::span<const float> samples)
{
    std::vector<Range> current;
    for (size_t i = 0; i < samples.size(); i += BASE_BLOCK)
        current.push_back(scan(samples.subspan(i, std::min(BASE_BLOCK, samples.size() - i))));
    while (current.size() > 1) {
        std::vector<Range> next;
        next.reserve((current.size() + 1) / 2);
        for (size_t i = 0; i < current.size(); i += 2)
            next.push_back(i + 1 < current.size() ? merge(current[i], current[i + 1]) : merge(EMPTY, current[i]));
        m_levels.push_back(std::move(current));
        current = std::move(next);
    }
    m_levels.push_back(std::move(current));
}

std::pair<float, float> Envelope::minMax(std::span<const float> samples, size_t first, size_t last) const
{
    // le plus grand niveau dont les blocs tiennent au moins deux fois dans l'intervalle
    for (size_t k = m_levels.size(); k-- > 0;) {
        const size_t size = BASE_BLOCK << k;
        if (size * 2 > last - first)
            continue;
        const size_t blockFirst = (first + size - 1) / size;
        const size_t blockLast = last / size;
        Range m = scan(samples.subspan(first, blockFirst * size - first));
        m = merge(m, scan(samples.subspan(blockLast * size, last - blockLast * size)));
        for (size_t b = blockFirst; b < blockLast; ++b)
            m = merge(m, m_levels[k][b]);
        return m;
    }
    return scan(samples.subspan(first, last - first));
}

std::vector<QPointF> Envelope::points(std::span<const float> samples, double rate, double t0, double t1,
                                      size_t columns) const
{
    const size_t len = samples.size();
    const size_t first = clampIndex(std::floor(t0 * rate), len);
    const size_t last = std::min(clampIndex(std::ceil(t1 * rate), len) + 2, len);
    if (last <= first + 1)
        return {};
    const size_t count = last - first;
    columns = std::max<size_t>(columns, 1);
    std::vector<QPointF> out;
    if (count <= 2 * columns) {
        out.reserve(count);
        for (size_t i = first; i < last; ++i)
            out.emplace_back(double(i) / rate, samples[i]);
        return out;
    }
    const double perColumn = double(count) / double(columns);
    out.reserve(2 * columns);
    for (size_t c = 0; c < columns; ++c) {
        const size_t a = first + static_cast<size_t>(double(c) * perColumn);
        const size_t b = std::clamp(first + static_cast<size_t>(double(c + 1) * perColumn), a + 1, last);
        const auto [lo, hi] = minMax(samples, a, b);
        const double t = double(a + b) / 2.0 / rate;
        out.emplace_back(t, lo);
        out.emplace_back(t, hi);
    }
    return out;
}
