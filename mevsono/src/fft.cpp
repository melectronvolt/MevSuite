#include "fft.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace fft {

size_t nextPowerOfTwo(size_t n)
{
    size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

Plan::Plan(size_t n)
    : m_n(n)
    , m_reversed(n)
    , m_twiddles(n / 2)
{
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        m_reversed[i] = j;
    }
    // Chaque facteur calculé directement (pas de récurrence) : pas d'accumulation d'erreurs.
    for (size_t k = 0; k < n / 2; ++k) {
        const double a = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
        m_twiddles[k] = {std::cos(a), std::sin(a)};
    }
}

void Plan::forward(std::vector<Complex> &data) const
{
    run(data, false);
}

void Plan::inverse(std::vector<Complex> &data) const
{
    run(data, true);
}

void Plan::run(std::vector<Complex> &data, bool inverse) const
{
    const size_t n = m_n;
    if (data.size() != n || n < 2)
        return;
    for (size_t i = 1; i < n; ++i) {
        if (i < m_reversed[i])
            std::swap(data[i], data[m_reversed[i]]);
    }
    // Produits complexes écrits à la main : l'opérateur de std::complex vérifie les infinis et les
    // NaN, ce qui ralentit beaucoup la transformée.
    const double sign = inverse ? -1.0 : 1.0;
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len / 2;
        const size_t stride = n / len;
        for (size_t i = 0; i < n; i += len) {
            for (size_t k = 0; k < half; ++k) {
                const Complex &w = m_twiddles[k * stride];
                const double wr = w.real(), wi = sign * w.imag();
                Complex &a = data[i + k];
                Complex &b = data[i + k + half];
                const double vr = b.real() * wr - b.imag() * wi;
                const double vi = b.real() * wi + b.imag() * wr;
                const double ur = a.real(), ui = a.imag();
                a = {ur + vr, ui + vi};
                b = {ur - vr, ui - vi};
            }
        }
    }
}

} // namespace fft
