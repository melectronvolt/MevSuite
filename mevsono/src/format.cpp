#include "format.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Écriture en virgule fixe, arrondie correctement comme `format!("{v:.decimals$}")` en Rust.
QString fixed(double v, int decimals)
{
    std::array<char, 1100> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, decimals);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

QString withDecimal(QString s, QChar dec)
{
    if (dec != u'.')
        s.replace(u'.', dec);
    return s;
}

} // namespace

QString fmtNum(double v, int sig, QChar dec)
{
    if (v == 0.0 || !std::isfinite(v))
        return u"0"_s;
    return fmtDec(v, sigDecimals(v, sig), dec);
}

int sigDecimals(double v, int sig)
{
    if (v == 0.0 || !std::isfinite(v))
        return 0;
    const int magnitude = static_cast<int>(std::floor(std::log10(std::abs(v))));
    const int decimals = std::max(sig - 1 - magnitude, 0);
    // L'arrondi peut faire passer à la puissance de 10 suivante (9,996 → 10,00) : une décimale de moins.
    const double rounded = fixed(std::abs(v), decimals).toDouble();
    if (decimals > 0 && rounded > 0.0 && static_cast<int>(std::floor(std::log10(rounded))) > magnitude)
        return decimals - 1;
    return decimals;
}

QString fmtDec(double v, int decimals, QChar dec)
{
    QString s = fixed(v, decimals);
    // « -0,0 » s'écrit « 0,0 »
    if (s.startsWith(u'-') && std::all_of(s.begin() + 1, s.end(), [](QChar c) { return c == u'0' || c == u'.'; }))
        s.remove(0, 1);
    if (s.startsWith(u'-'))
        s.replace(0, 1, u"−"_s);
    return withDecimal(s, dec);
}

QString csvNum(double v, int decimals, QChar dec)
{
    return withDecimal(fixed(v, decimals), dec);
}

QString shortest(double v)
{
    std::array<char, 1100> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

QString fmtTime(double t, QChar dec)
{
    if (std::abs(t) < 1.0)
        return fmtNum(t * 1000.0, 4, dec) + u" ms"_s;
    return fmtNum(t, 5, dec) + u" s"_s;
}

QString fmtFreq(double f, QChar dec)
{
    const int decimals = std::abs(f) < 100.0 ? 2 : std::abs(f) < 10'000.0 ? 1 : 0;
    return fmtDec(f, decimals, dec) + u" Hz"_s;
}

QString fmtPosition(double t, QChar dec)
{
    return fmtDec(t, 4, dec) + u" s"_s;
}

QString fmtDuration(double t, QChar dec)
{
    if (t < 1.0)
        return fmtNum(t * 1000.0, 3, dec) + u" ms"_s;
    if (t < 60.0)
        return fmtNum(t, 3, dec) + u" s"_s;
    const auto s = static_cast<long long>(std::llround(t));
    return u"%1 min %2 s"_s.arg(s / 60).arg(s % 60, 2, 10, QChar(u'0'));
}

QString fmtFreqShort(double f, QChar dec)
{
    const auto shortNum = [dec](double v) {
        QString s = fmtNum(v, 3, dec);
        if (s.contains(dec)) {
            while (s.endsWith(u'0'))
                s.chop(1);
            while (s.endsWith(dec))
                s.chop(1);
        }
        return s;
    };
    if (f >= 1000.0)
        return shortNum(f / 1000.0) + u" kHz"_s;
    return shortNum(f) + u" Hz"_s;
}

double toDb(double a)
{
    return 20.0 * std::log10(std::max(a, 1e-9));
}
