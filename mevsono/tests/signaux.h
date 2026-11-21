#pragma once

#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace essais {

inline constexpr double RATE = 44100.0;

/// Somme de sinusoïdes (fréquence en hertz, amplitude), échantillonnée à la cadence demandée.
inline std::vector<float> sons_purs(const std::vector<std::pair<double, double>> &components, double seconds,
                                   double rate = RATE)
{
    const auto n = size_t(seconds * rate);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        double v = 0.0;
        for (const auto &[f, a] : components)
            v += a * std::sin(2.0 * std::numbers::pi * f * t);
        out[i] = float(v);
    }
    return out;
}

} // namespace essais
