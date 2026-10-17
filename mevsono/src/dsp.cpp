#include "dsp.h"

#include "fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

using namespace Qt::StringLiterals;

namespace dsp {

namespace {

/// Conversion d'un réel positif en indice, comme `x as usize` en Rust : 0 pour un négatif ou un NaN,
/// borné pour un très grand nombre (ou l'infini).
size_t toIndex(double x)
{
    if (!(x > 0.0))
        return 0;
    constexpr double LIMIT = 9.0e15;
    return static_cast<size_t>(std::min(x, LIMIT));
}

/// Coefficients d'une fenêtre « somme de cosinus » : w(i) = Σ (−1)^k a_k cos(2π k i / n).
std::vector<double> cosineTerms(Window w)
{
    switch (w) {
    case Window::Rectangular: return {1.0};
    case Window::Hann: return {0.5, 0.5};
    case Window::Hamming: return {0.54, 0.46};
    case Window::Blackman: return {0.42, 0.5, 0.08};
    case Window::BlackmanHarris: return {0.35875, 0.48829, 0.14128, 0.01168};
    case Window::FlatTop: return {0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368};
    }
    return {1.0};
}

double mean(std::span<const float> samples)
{
    double sum = 0.0;
    for (float x : samples)
        sum += x;
    return samples.empty() ? 0.0 : sum / double(samples.size());
}

/// Noyau de Dirichlet : Σ e^(−2iπνn) pour n de 0 à `len` − 1 (périodique en ν, de période 1).
std::complex<double> dirichlet(double nu, double len)
{
    nu -= std::round(nu);
    const double s = std::sin(std::numbers::pi * nu);
    if (std::abs(s) < 1e-12)
        return len;
    return std::polar(std::sin(std::numbers::pi * nu * len) / s, -std::numbers::pi * nu * (len - 1.0));
}

/// Amplitude relative d'une sinusoïde décalée de `delta` raies de la raie mesurée, pour la fenêtre
/// `terms` de `len` échantillons et une FFT de `fftLen` points : |W(δ / fftLen)| / W(0), où W est le
/// spectre de la fenêtre (somme de noyaux de Dirichlet décalés).
double windowGain(const std::vector<double> &terms, double delta, size_t len, size_t fftLen)
{
    const double n = double(len), nu = delta / double(fftLen);
    std::complex<double> w;
    for (size_t k = 0; k < terms.size(); ++k) {
        const double c = (k % 2 == 0 ? terms[k] : -terms[k]) / 2.0;
        w += c * (dirichlet(nu - double(k) / n, n) + dirichlet(nu + double(k) / n, n));
    }
    return std::abs(w) / (n * terms[0]);
}

/// Rang maximal des harmoniques pris en compte pour reconnaître la fondamentale.
constexpr double MAX_RANK = 40.0;
/// Plus long extrait pris pour l'autocorrélation.
constexpr size_t MAX_AUTOCORR = size_t(1) << 16;

} // namespace

QString windowCode(Window w)
{
    switch (w) {
    case Window::Rectangular: return u"rectangular"_s;
    case Window::Hann: return u"hann"_s;
    case Window::Hamming: return u"hamming"_s;
    case Window::Blackman: return u"blackman"_s;
    case Window::BlackmanHarris: return u"blackman-harris"_s;
    case Window::FlatTop: return u"flat-top"_s;
    }
    return {};
}

std::optional<Window> windowFromCode(const QString &code)
{
    for (Window w : ALL_WINDOWS) {
        if (windowCode(w) == code)
            return w;
    }
    return std::nullopt;
}

std::vector<double> windowCoefficients(Window w, size_t n)
{
    const std::vector<double> terms = cosineTerms(w);
    std::vector<double> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double x = 2.0 * std::numbers::pi * double(i) / double(n);
        double v = 0.0;
        for (size_t k = 0; k < terms.size(); ++k)
            v += (k % 2 == 0 ? terms[k] : -terms[k]) * std::cos(double(k) * x);
        out[i] = v;
    }
    return out;
}

double mainLobeBins(Window w)
{
    switch (w) {
    case Window::Rectangular: return 1.0;
    case Window::Hann:
    case Window::Hamming: return 2.0;
    case Window::Blackman: return 3.0;
    case Window::BlackmanHarris: return 4.0;
    case Window::FlatTop: return 5.0;
    }
    return 1.0;
}

double Spectrum::maxAmplitude() const
{
    double m = 0.0;
    for (size_t k = 1; k < amplitude.size(); ++k)
        m = std::max(m, amplitude[k]);
    return m;
}

std::optional<Spectrum> spectrum(std::span<const float> samples, double rate, const SpectrumParams &params,
                                 const std::atomic<bool> *cancel)
{
    if (samples.size() < 8 || !(rate > 0.0))
        return std::nullopt;
    size_t segmentLen, fftLen;
    if (params.fftSize == 0) {
        segmentLen = std::min(samples.size(), MAX_AUTO_FFT);
        fftLen = fft::nextPowerOfTwo(segmentLen);
    } else {
        segmentLen = std::min(samples.size(), params.fftSize);
        fftLen = params.fftSize;
    }
    const size_t hop = std::max<size_t>(segmentLen / 2, 1);
    std::vector<size_t> starts;
    if (samples.size() <= segmentLen) {
        starts.push_back(0);
    } else {
        for (size_t s = 0; s <= samples.size() - segmentLen; s += hop)
            starts.push_back(s);
    }
    const std::vector<double> window = windowCoefficients(params.window, segmentLen);
    double windowSum = 0.0;
    for (double w : window)
        windowSum += w;
    const fft::Plan plan(fftLen);
    std::vector<double> power(fftLen / 2 + 1, 0.0);
    std::vector<fft::Complex> buffer(fftLen);
    for (size_t start : starts) {
        if (cancel && *cancel)
            return std::nullopt;
        const std::span<const float> segment = samples.subspan(start, segmentLen);
        const double m = mean(segment);
        for (size_t i = 0; i < fftLen; ++i)
            buffer[i] = i < segmentLen ? fft::Complex((double(segment[i]) - m) * window[i], 0.0) : fft::Complex();
        plan.forward(buffer);
        for (size_t k = 0; k < power.size(); ++k)
            power[k] += std::norm(buffer[k]);
    }
    Spectrum sp;
    sp.rate = rate;
    sp.fftLen = fftLen;
    sp.segmentLen = segmentLen;
    sp.segments = starts.size();
    sp.amplitude.resize(power.size());
    const double count = double(starts.size());
    for (size_t k = 0; k < power.size(); ++k) {
        const double a = std::sqrt(power[k] / count) / windowSum;
        // une sinusoïde se partage entre fréquences positives et négatives, sauf en 0 et à Nyquist
        sp.amplitude[k] = (k == 0 || 2 * k == fftLen) ? a : 2.0 * a;
    }
    return sp;
}

std::vector<Peak> peaks(const Spectrum &sp, Window window, const PeakParams &params)
{
    const std::vector<double> &a = sp.amplitude;
    const double max = sp.maxAmplitude();
    if (max <= 0.0 || a.size() < 3)
        return {};
    const double floor = max * std::pow(10.0, params.thresholdDb / 20.0);
    const double padding = double(sp.fftLen) / double(sp.segmentLen);
    const size_t radius = std::max<size_t>(toIndex(std::round(mainLobeBins(window) * padding)), 1);
    const double df = sp.df();
    const size_t first = std::max<size_t>(toIndex(std::ceil(params.fmin / df)), 1);
    const size_t last = std::min(toIndex(std::floor(params.fmax / df)), a.size() - 2);
    const std::vector<double> terms = cosineTerms(window);
    const auto gain = [&](double delta) { return windowGain(terms, delta, sp.segmentLen, sp.fftLen); };
    std::vector<Peak> found;
    for (size_t k = first; k <= last; ++k) {
        if (a[k] < floor)
            continue;
        const size_t lo = std::max<size_t>(k > radius ? k - radius : 0, 1);
        const size_t hi = std::min(k + radius, a.size() - 1);
        // maximum du voisinage (à égalité, la première raie l'emporte)
        bool isMax = true;
        for (size_t j = lo; j < k && isMax; ++j)
            isMax = a[j] < a[k];
        for (size_t j = k + 1; j <= hi && isMax; ++j)
            isMax = a[j] <= a[k];
        if (!isMax)
            continue;
        // Écart δ du vrai sommet à la raie k, puis amplitude a[k] / G(δ), G étant la réponse de la
        // fenêtre : justes pour une sinusoïde, où qu'elle tombe entre deux raies (une parabole passant
        // par le logarithme des trois raies se trompait sur l'amplitude jusqu'à 27 % avec la fenêtre
        // rectangulaire, 4 % avec Hann, 2 % avec le sommet plat).
        double delta, centre;
        if (window == Window::FlatTop) {
            // sommet plat : les raies voisines changent trop peu avec δ, le barycentre du lobe situe
            // mieux le centre
            double sum = 0.0, weighted = 0.0;
            for (size_t j = lo; j <= hi; ++j) {
                sum += a[j] * a[j];
                weighted += a[j] * a[j] * double(j);
            }
            centre = weighted / sum;
            delta = std::clamp(centre - double(k), -0.5, 0.5);
        } else {
            // δ pour lequel le rapport de la plus grande voisine à la raie k est celui que donne la
            // fenêtre (il croît de G(1) / G(0) à 1 quand δ va de 0 à une demi-raie)
            const int side = a[k + 1] >= a[k - 1] ? 1 : -1;
            const double ratio = a[side > 0 ? k + 1 : k - 1] / a[k];
            double below = 0.0, above = 0.5;
            for (int i = 0; i < 50; ++i) {
                const double u = 0.5 * (below + above);
                (gain(1.0 - u) < ratio * gain(u) ? below : above) = u;
            }
            delta = side * 0.5 * (below + above);
            centre = double(k) + delta;
        }
        found.push_back({centre * df, a[k] / gain(delta)});
    }
    std::stable_sort(found.begin(), found.end(), [](const Peak &x, const Peak &y) { return x.amplitude > y.amplitude; });
    if (found.size() > params.maxPeaks)
        found.resize(params.maxPeaks);
    std::stable_sort(found.begin(), found.end(), [](const Peak &x, const Peak &y) { return x.freq < y.freq; });
    return found;
}

double harmonicTolerance(double f0, double df)
{
    return std::max(0.03 * f0, 1.5 * df);
}

std::optional<double> fundamental(std::span<const Peak> peaks, double df, double fmin)
{
    std::vector<Peak> strong(peaks.begin(), peaks.end());
    std::stable_sort(strong.begin(), strong.end(), [](const Peak &x, const Peak &y) { return x.amplitude > y.amplitude; });
    if (strong.size() > 24)
        strong.resize(24);
    if (strong.empty())
        return std::nullopt;
    const Peak loudest = strong.front();
    std::stable_sort(strong.begin(), strong.end(), [](const Peak &x, const Peak &y) { return x.freq < y.freq; });

    const auto rank = [df](double f, double f0) -> std::optional<double> {
        const double n = std::round(f / f0);
        if (n >= 1.0 && n <= MAX_RANK && std::abs(f - n * f0) <= harmonicTolerance(f0, df))
            return n;
        return std::nullopt;
    };
    // pour chaque rang, le plus grand pic qui lui correspond (les lobes secondaires d'une fenêtre
    // rectangulaire, proches d'un harmonique, ne comptent pas en plus)
    const auto matched = [&](double f0) {
        std::vector<std::pair<double, Peak>> best;
        for (const Peak &p : strong) {
            const auto n = rank(p.freq, f0);
            if (!n)
                continue;
            const auto entry = std::ranges::find_if(best, [&](const auto &e) { return e.first == *n; });
            if (entry == best.end())
                best.push_back({*n, p});
            else if (p.amplitude > entry->second.amplitude)
                entry->second = p;
        }
        return best;
    };
    const auto score = [&](double f0) {
        double sum = 0.0;
        for (const auto &[n, p] : matched(f0))
            sum += p.amplitude;
        return sum;
    };

    std::vector<double> candidates;
    for (const Peak &p : strong)
        candidates.push_back(p.freq);
    for (size_t i = 0; i + 1 < strong.size(); ++i)
        candidates.push_back(strong[i + 1].freq - strong[i].freq);
    std::erase_if(candidates, [&](double f) { return !(f >= fmin && rank(loudest.freq, f)); });
    double best = 0.0;
    for (double f : candidates)
        best = std::max(best, score(f));
    if (best <= 0.0)
        return std::nullopt;
    double f0 = 0.0;
    for (double f : candidates) {
        if (score(f) >= 0.97 * best)
            f0 = std::max(f0, f);
    }

    // moindres carrés pondérés : f0 = Σ a n f / Σ a n²
    double num = 0.0, den = 0.0;
    for (const auto &[n, p] : matched(f0)) {
        num += p.amplitude * n * p.freq;
        den += p.amplitude * n * n;
    }
    if (den > 0.0)
        return num / den;
    return std::nullopt;
}

std::vector<Harmonic> harmonics(std::span<const Peak> peaks, double f0, double df, size_t count)
{
    const double tolerance = harmonicTolerance(f0, df);
    std::vector<Harmonic> out;
    out.reserve(count);
    for (size_t rank = 1; rank <= count; ++rank) {
        const double expected = double(rank) * f0;
        std::optional<Peak> peak;
        for (const Peak &p : peaks) {
            if (std::abs(p.freq - expected) <= tolerance && (!peak || p.amplitude >= peak->amplitude))
                peak = p;
        }
        out.push_back({rank, expected, peak});
    }
    return out;
}

std::optional<double> periodAutocorrelation(std::span<const float> samples, double rate, double fmin, double fmax)
{
    const size_t n = std::min(samples.size(), MAX_AUTOCORR);
    if (n < 16)
        return std::nullopt;
    samples = samples.first(n);
    const double m = mean(samples);
    const size_t len = fft::nextPowerOfTwo(2 * n);
    std::vector<fft::Complex> buffer(len);
    for (size_t i = 0; i < n; ++i)
        buffer[i] = {double(samples[i]) - m, 0.0};
    const fft::Plan plan(len);
    plan.forward(buffer);
    for (fft::Complex &b : buffer)
        b = {std::norm(b), 0.0};
    plan.inverse(buffer);
    const double r0 = buffer[0].real();
    if (!(r0 > 0.0))
        return std::nullopt;
    // autocorrélation normalisée et non biaisée (chaque décalage divisé par son nombre de termes)
    const auto r = [&](size_t lag) { return buffer[lag].real() / r0 * double(n) / double(n - lag); };
    const size_t lagMin = std::max<size_t>(toIndex(std::floor(rate / fmax)), 2);
    const size_t lagMax = std::min(toIndex(std::ceil(rate / fmin)), n / 2);
    if (lagMin + 2 >= lagMax)
        return std::nullopt;
    std::vector<size_t> localMax;
    for (size_t l = lagMin; l < lagMax; ++l) {
        if (r(l) > r(l - 1) && r(l) >= r(l + 1) && r(l) > 0.3)
            localMax.push_back(l);
    }
    double highest = -std::numeric_limits<double>::infinity();
    for (size_t l : localMax)
        highest = std::max(highest, r(l));
    const auto found = std::ranges::find_if(localMax, [&](size_t l) { return r(l) >= 0.9 * highest; });
    if (found == localMax.end())
        return std::nullopt;
    const size_t lag = *found;
    const double a = r(lag - 1), b = r(lag), c = r(lag + 1);
    const double denom = a - 2.0 * b + c;
    const double delta = denom < 0.0 ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
    return (double(lag) + delta) / rate;
}

std::optional<double> risingZeroCrossing(std::span<const float> samples, double rate, double t, double radius)
{
    if (samples.empty())
        return std::nullopt;
    const double lastIndex = double(samples.size() - 1);
    const auto index = [&](double s) { return toIndex(std::clamp(std::round(s * rate), 0.0, lastIndex)); };
    const size_t first = index(t - radius);
    const size_t last = std::min(index(t + radius), samples.size() - 1);
    std::optional<double> best;
    for (size_t i = first; i < last; ++i) {
        if (samples[i] <= 0.0f && samples[i + 1] > 0.0f) {
            const double a = samples[i], b = samples[i + 1];
            const double crossing = (double(i) + a / (a - b)) / rate;
            if (!best || std::abs(crossing - t) < std::abs(*best - t))
                best = crossing;
        }
    }
    return best;
}

std::optional<Analysis> analyse(std::span<const float> samples, size_t first, size_t last, double rate,
                                const SpectrumParams &spectrumParams, const PeakParams &peakParams,
                                const std::atomic<bool> *cancel)
{
    if (first > last || last > samples.size())
        return std::nullopt;
    const std::span<const float> extract = samples.subspan(first, last - first);
    auto sp = spectrum(extract, rate, spectrumParams, cancel);
    if (!sp)
        return std::nullopt;
    PeakParams params = peakParams;
    params.fmax = std::min(peakParams.fmax, rate / 2.0);
    Analysis a;
    a.peaks = peaks(*sp, spectrumParams.window, params);
    a.fundamental = fundamental(a.peaks, sp->df(), peakParams.fmin);
    a.period = periodAutocorrelation(extract, rate, peakParams.fmin, std::min(params.fmax, 5000.0));
    a.spectrum = std::move(*sp);
    return a;
}

} // namespace dsp
