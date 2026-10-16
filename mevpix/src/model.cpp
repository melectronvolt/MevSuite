#include "model.h"

#include <algorithm>
#include <charconv>
#include <cmath>

using namespace Qt::StringLiterals;

double length(QPointF v)
{
    return std::hypot(v.x(), v.y());
}

double Segment::lenPx() const
{
    return length(b - a);
}

QPointF Segment::pointAt(double t) const
{
    return a + (b - a) * t;
}

double Segment::project(QPointF p) const
{
    const QPointF d = b - a;
    const double lenSq = QPointF::dotProduct(d, d);
    return lenSq > 0.0 ? std::clamp(QPointF::dotProduct(p - a, d) / lenSq, 0.0, 1.0) : 0.0;
}

QPointF Segment::normal() const
{
    const QPointF d = b - a;
    const double len = length(d);
    return len > 0.0 ? QPointF(-d.y(), d.x()) / len : QPointF();
}

QPointF snapAxis(QPointF a, QPointF b)
{
    const QPointF d = b - a;
    return std::abs(d.x()) >= std::abs(d.y()) ? QPointF(b.x(), a.y()) : QPointF(a.x(), b.y());
}

QString unitSymbol(Unit unit)
{
    switch (unit) {
    case Unit::Micrometre: return u"µm"_s;
    case Unit::Millimetre: return u"mm"_s;
    case Unit::Centimetre: return u"cm"_s;
    case Unit::Metre: return u"m"_s;
    }
    return {};
}

std::optional<Calibration> Calibration::make(double refPx, double realLen, Unit unit)
{
    if (refPx >= 1.0 && std::isfinite(realLen) && realLen > 0.0)
        return Calibration{realLen, refPx, unit};
    return std::nullopt;
}

bool Calibration::matches(const Calibration &other) const
{
    return std::abs(refPx - other.refPx) < 0.5 && realLen == other.realLen && unit == other.unit;
}

namespace {

/// Écriture en virgule fixe, arrondie correctement comme `format!("{v:.decimals$}")` en Rust.
QString fixed(double v, int decimals)
{
    std::array<char, 1024> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, decimals);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

} // namespace

QString shortest(double v)
{
    std::array<char, 1024> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

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
    return withDecimal(fixed(v, decimals), dec);
}

QString withDecimal(QString s, QChar dec)
{
    if (dec != u'.')
        s.replace(u'.', dec);
    return s;
}

QString fmtLen(double px, const std::optional<Calibration> &calib, QChar dec)
{
    if (calib)
        return fmtNum(calib->toReal(px), 3, dec) + u' ' + unitSymbol(calib->unit);
    return fmtNum(px, 3, dec) + u" px"_s;
}

QString measuresTable(const std::vector<double> &lengthsPx, const std::optional<Calibration> &calib, QChar dec,
                      const std::array<QString, 2> &header)
{
    QString out = header[0] + u'\t' + header[1] + u'\n';
    for (size_t i = 0; i < lengthsPx.size(); ++i) {
        const double px = lengthsPx[i];
        const QString value = calib ? fmtNum(calib->toReal(px), 3, dec) : fmtNum(px, 3, dec);
        out += u"M%1\t%2\n"_s.arg(i + 1).arg(value);
    }
    return out;
}

std::optional<double> parseNum(const QString &s)
{
    QByteArray text = s.trimmed().replace(u',', u'.').toUtf8();
    // Comme en Rust, un signe « + » est accepté devant le nombre.
    if (text.startsWith('+') && text.size() > 1 && text[1] != '-')
        text.remove(0, 1);
    double v = 0.0;
    const char *end = text.constData() + text.size();
    const auto [ptr, ec] = std::from_chars(text.constData(), end, v);
    if (text.isEmpty() || ec != std::errc() || ptr != end || !std::isfinite(v))
        return std::nullopt;
    return v;
}
