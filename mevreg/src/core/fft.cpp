#include "core/fft.h"

#include "core/numeric.h"

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace fft {

namespace {

bool isPowerOfTwo(size_t n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

/// exp(−2iπ k/n) (ou +), exact aux multiples de π/2.
Complex twiddle(size_t k, size_t n, bool inverse)
{
    const size_t kk = k % n;
    if ((4 * kk) % n == 0) {
        static const Complex quarter[4] = {{1, 0}, {0, -1}, {-1, 0}, {0, 1}};
        Complex w = quarter[(4 * kk) / n];
        return inverse ? std::conj(w) : w;
    }
    const double a = -2.0 * std::numbers::pi * static_cast<double>(kk) / static_cast<double>(n);
    return {std::cos(a), inverse ? -std::sin(a) : std::sin(a)};
}

void radix2(std::vector<Complex> &a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len / 2;
        std::vector<Complex> w(half);
        for (size_t k = 0; k < half; ++k)
            w[k] = twiddle(k, len, inverse);
        for (size_t i = 0; i < n; i += len) {
            for (size_t k = 0; k < half; ++k) {
                const Complex u = a[i + k];
                const Complex v = a[i + k + half] * w[k];
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
        }
    }
}

void bluestein(std::vector<Complex> &a, bool inverse)
{
    const size_t n = a.size();
    size_t m = 1;
    while (m < 2 * n - 1)
        m <<= 1;
    // chirp : exp(∓iπ k²/n), k² calculé modulo 2n pour garder la précision
    std::vector<Complex> chirp(n);
    for (size_t k = 0; k < n; ++k) {
        const unsigned long long k2 = (static_cast<unsigned long long>(k) * k) % (2ull * n);
        const double ang = std::numbers::pi * static_cast<double>(k2) / static_cast<double>(n);
        chirp[k] = {std::cos(ang), inverse ? std::sin(ang) : -std::sin(ang)};
    }
    std::vector<Complex> x(m, 0.0), y(m, 0.0);
    for (size_t k = 0; k < n; ++k)
        x[k] = a[k] * chirp[k];
    y[0] = std::conj(chirp[0]);
    for (size_t k = 1; k < n; ++k) {
        y[k] = std::conj(chirp[k]);
        y[m - k] = std::conj(chirp[k]);
    }
    radix2(x, false);
    radix2(y, false);
    for (size_t i = 0; i < m; ++i)
        x[i] *= y[i];
    radix2(x, true);
    for (size_t k = 0; k < n; ++k)
        a[k] = x[k] / static_cast<double>(m) * chirp[k];
}

std::vector<double> coefficients(Window w, size_t n)
{
    std::vector<double> c(n, 1.0);
    if (w == Window::Hann && n >= 2) {
        for (size_t i = 0; i < n; ++i)
            c[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1));
    }
    return c;
}

} // namespace

void transform(std::vector<Complex> &data, bool inverse)
{
    const size_t n = data.size();
    if (n <= 1)
        return;
    if (isPowerOfTwo(n)) {
        radix2(data, inverse);
    } else if (n <= 32) {
        // petites tailles : somme directe, plus précise que Bluestein
        std::vector<Complex> out(n, 0.0);
        for (size_t k = 0; k < n; ++k) {
            Complex s = 0.0;
            for (size_t j = 0; j < n; ++j)
                s += data[j] * twiddle(j * k, n, inverse);
            out[k] = s;
        }
        data = std::move(out);
    } else {
        bluestein(data, inverse);
    }
}

QString windowLabel(Window w)
{
    return w == Window::Hann ? u"Hann"_s : u"Rectangulaire"_s;
}

Spectrum spectrumUniform(const std::vector<double> &y, double dt, Window window, int zeroPadding)
{
    const size_t n = y.size();
    const auto w = coefficients(window, n);
    double wsum = 0.0;
    for (double v : w)
        wsum += v;
    const size_t m = n * static_cast<size_t>(std::max(zeroPadding, 1));
    std::vector<Complex> buf(m, 0.0);
    for (size_t i = 0; i < n; ++i)
        buf[i] = y[i] * w[i];
    transform(buf, false);
    const size_t half = m / 2 + 1;
    const double df = 1.0 / (static_cast<double>(m) * dt);
    Spectrum s;
    s.dt = dt;
    s.n = n;
    for (size_t k = 0; k < half && k < m; ++k) {
        s.freq.push_back(static_cast<double>(k) * df);
        // composante continue et fréquence de Nyquist ne sont pas doublées
        const double factor = (k == 0 || (m % 2 == 0 && k == m / 2)) ? 1.0 : 2.0;
        s.amplitude.push_back(factor * std::abs(buf[k]) / wsum);
    }
    return s;
}

std::optional<Spectrum> amplitudeSpectrum(const std::vector<double> &t, const std::vector<double> &y, const SpectrumOptions &opts, QString *error)
{
    const auto [ts, ys] = numeric::sortedPairs(t, y);
    auto fail = [&](const QString &m) {
        if (error)
            *error = m;
        return std::nullopt;
    };
    if (ts.size() < 4)
        return fail(u"il faut au moins 4 points pour calculer un spectre"_s);
    const auto info = numeric::stepInfo(ts);
    if (!info)
        return fail(u"abscisses toutes identiques"_s);
    const auto [dt, dev] = *info;
    if (dev > opts.stepTolerance) {
        return fail(u"le pas d'échantillonnage n'est pas constant (écart jusqu'à %1 %) : le spectre suppose un pas constant"_s
                        .arg(QString::number(dev * 100.0, 'f', 1)));
    }
    return spectrumUniform(ys, dt, opts.window, opts.zeroPadding);
}

std::optional<double> dominantFrequency(const std::vector<double> &t, const std::vector<double> &y)
{
    const auto [ts, ys] = numeric::sortedPairs(t, y);
    if (ts.size() < 4)
        return std::nullopt;
    const size_t n = std::max<size_t>(ts.size(), 64);
    const auto [grid, vals] = numeric::resampleUniform(ts, ys, n);
    const double dt = grid[1] - grid[0];
    if (dt <= 0.0)
        return std::nullopt;
    double mean = 0.0;
    for (double v : vals)
        mean += v;
    mean /= static_cast<double>(vals.size());
    std::vector<double> centered(vals.size());
    for (size_t i = 0; i < vals.size(); ++i)
        centered[i] = vals[i] - mean;
    const Spectrum s = spectrumUniform(centered, dt, Window::Hann, 8);
    size_t k = 1;
    for (size_t i = 1; i < s.amplitude.size(); ++i) {
        if (s.amplitude[i] > s.amplitude[k])
            k = i;
    }
    if (k >= s.amplitude.size() || s.amplitude[k] <= 0.0)
        return std::nullopt;
    const double df = s.freq[1];
    if (k + 1 < s.amplitude.size()) {
        const double a = s.amplitude[k - 1], b = s.amplitude[k], c = s.amplitude[k + 1];
        const double denom = a - 2.0 * b + c;
        if (denom != 0.0) {
            const double p = 0.5 * (a - c) / denom;
            return (static_cast<double>(k) + std::clamp(p, -0.5, 0.5)) * df;
        }
    }
    return static_cast<double>(k) * df;
}

} // namespace fft
