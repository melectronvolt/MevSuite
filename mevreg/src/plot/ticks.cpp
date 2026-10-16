#include "plot/ticks.h"

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;

namespace ticks {

namespace {

/// Vrai si `s` s'écrit avec un seul chiffre significatif (0,2 ; 0,05…).
bool isRound(double s)
{
    const double mag = std::pow(10.0, std::floor(std::log10(s)));
    const double n = s / mag;
    return std::abs(n - std::round(n)) < 1e-6;
}

/// Écrit les valeurs des graduations, avec un facteur commun pour les très grands ou très petits nombres.
std::pair<QStringList, int> formatTicks(const std::vector<double> &values, double step, NumFormat format, NumberStyle style)
{
    QStringList labels;
    switch (format.kind) {
    case NumFormat::Fixed:
        for (double v : values)
            labels << formatFixed(v, format.decimals, style);
        return {labels, 0};
    case NumFormat::Scientific:
        for (double v : values)
            labels << formatSci(v, format.decimals, style);
        return {labels, 0};
    case NumFormat::Auto:
        break;
    }
    double maxabs = 0.0;
    for (double v : values)
        maxabs = std::max(maxabs, std::abs(v));
    const int mag = maxabs > 0.0 ? static_cast<int>(std::floor(std::log10(maxabs))) : 0;
    const int factorExp = (mag >= 5 || mag <= -3) ? static_cast<int>(std::floor(mag / 3.0)) * 3 : 0;
    const double scale = std::pow(10.0, factorExp);
    const double s = step / scale;
    int decimals = s >= 1.0 ? 0 : static_cast<int>(-std::floor(std::log10(s))) + (isRound(s) ? 0 : 1);
    decimals = std::min(decimals, 12);
    for (double v : values)
        labels << formatFixed(v / scale, decimals, style);
    return {labels, factorExp};
}

} // namespace

double niceStep(double range, double target)
{
    if (!(range > 0.0) || !std::isfinite(range))
        return 1.0;
    const double raw = range / std::max(target, 1.0);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double nice = norm < 1.5 ? 1.0 : norm < 3.0 ? 2.0 : norm < 7.0 ? 5.0 : 10.0;
    return nice * mag;
}

int autoMinor(double step)
{
    const double mag = std::pow(10.0, std::floor(std::log10(step)));
    const long long norm = std::llround(step / mag);
    return norm == 2 ? 4 : 5;
}

Ticks linear(double min, double max, double target, std::optional<double> step, std::optional<int> minorDiv, NumFormat format, NumberStyle style)
{
    const double lo = std::min(min, max), hi = std::max(min, max);
    double st = (step && *step > 0.0 && std::isfinite(*step)) ? *step : niceStep(hi - lo, target);
    // garde-fou : jamais plus de 200 graduations
    if ((hi - lo) / st > 200.0)
        st = niceStep(hi - lo, target);
    Ticks t;
    const double first = std::ceil(lo / st - 1e-9);
    const double last = std::floor(hi / st + 1e-9);
    for (double k = first; k <= last && t.major.size() < 500; k += 1.0) {
        const double v = k * st;
        // évite les « -0 » et les 1e-17
        t.major.push_back(std::abs(v / st) < 1e-9 ? 0.0 : v);
    }
    const int div = std::max(minorDiv.value_or(autoMinor(st)), 1);
    if (div > 1) {
        const double ms = st / div;
        const long long mfirst = static_cast<long long>(std::ceil(lo / ms - 1e-9));
        const long long mlast = static_cast<long long>(std::floor(hi / ms + 1e-9));
        if (mlast - mfirst < 2000) {
            for (long long j = mfirst; j <= mlast; ++j) {
                if (j % div != 0)
                    t.minor.push_back(static_cast<double>(j) * ms);
            }
        }
    }
    auto [labels, fe] = formatTicks(t.major, st, format, style);
    // vrai signe moins typographique
    for (QString &l : labels)
        l = typographicMinus(l);
    t.labels = labels;
    t.factorExp = fe;
    return t;
}

Ticks logarithmic(double min, double max, NumberStyle style)
{
    double lo = std::log10(std::max(min, std::numeric_limits<double>::min()));
    double hi = std::log10(std::max(max, std::numeric_limits<double>::min()));
    if (lo > hi)
        std::swap(lo, hi);
    const int d0 = static_cast<int>(std::floor(lo));
    const int d1 = static_cast<int>(std::ceil(hi));
    const int decades = std::max(d1 - d0, 1);
    const int stride = std::max(static_cast<int>(std::ceil(decades / 8.0)), 1);
    Ticks t;
    for (int d = d0; d <= d1; ++d) {
        const double base = std::pow(10.0, d);
        if (((d % stride) + stride) % stride == 0 && d >= lo - 1e-9 && d <= hi + 1e-9)
            t.major.push_back(base);
        if (stride == 1) {
            for (int m = 2; m < 10; ++m) {
                const double v = m * base;
                const double lv = std::log10(v);
                if (lv >= lo - 1e-9 && lv <= hi + 1e-9)
                    t.minor.push_back(v);
            }
        }
    }
    for (double v : t.major) {
        const int e = static_cast<int>(std::lround(std::log10(v)));
        if (e >= -2 && e <= 3)
            t.labels << formatFixed(v, std::max(-e, 0), style);
        else
            t.labels << u"10"_s + superscript(QString::number(e));
    }
    // moins de deux décades : on étiquette aussi 2 et 5
    if (t.major.size() < 2) {
        std::vector<std::pair<double, QString>> pairs;
        for (size_t i = 0; i < t.major.size(); ++i)
            pairs.emplace_back(t.major[i], t.labels[i]);
        for (double v : t.minor) {
            const double m = std::round(v / std::pow(10.0, std::floor(std::log10(v))));
            if (m == 2.0 || m == 5.0) {
                const int e = static_cast<int>(std::floor(std::log10(v)));
                pairs.emplace_back(v, formatFixed(v, std::max(-e, 0), style));
            }
        }
        std::sort(pairs.begin(), pairs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        t.major.clear();
        t.labels.clear();
        for (const auto &[v, l] : pairs) {
            t.major.push_back(v);
            t.labels << l;
        }
        std::erase_if(t.minor, [&](double v) {
            return std::any_of(t.major.begin(), t.major.end(), [&](double m) { return std::abs(m - v) < 1e-12 * std::abs(v); });
        });
    }
    return t;
}

} // namespace ticks
