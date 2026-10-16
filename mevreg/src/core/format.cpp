#include "core/format.h"

#include <QLocale>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QString applyComma(QString s, NumberStyle style)
{
    if (style.decimalComma)
        s.replace(u'.', u',');
    return s;
}

QString trimZeros(const QString &s)
{
    if (!s.contains(u'.'))
        return s;
    qsizetype end = s.size();
    while (end > 0 && s[end - 1] == u'0')
        --end;
    if (end > 0 && s[end - 1] == u'.')
        --end;
    return s.left(end);
}

/// Écriture `{:.*e}` : mantisse à `decimals` décimales et exposant sans zéros de tête.
std::pair<QString, int> sciParts(double v, int decimals)
{
    const QString s = QString::number(v, 'e', decimals);
    const qsizetype k = s.indexOf(u'e');
    if (k < 0)
        return {s, 0};
    return {s.left(k), s.mid(k + 1).toInt()};
}

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

std::pair<QString, int> readExponent(const QString &chars, int start)
{
    QString s;
    int i = start;
    if (i < chars.size() && (chars[i] == u'-' || chars[i] == u'−' || chars[i] == u'+')) {
        s.append(chars[i] == u'+' ? u'+' : u'-');
        ++i;
    }
    const int digitsStart = i;
    while (i < chars.size() && isDigit(chars[i])) {
        s.append(chars[i]);
        ++i;
    }
    if (i == digitsStart)
        return {QString(), 0};
    return {s, i - start};
}

} // namespace

std::optional<double> parseNumber(const QString &s)
{
    QString cleaned;
    cleaned.reserve(s.size());
    for (QChar c : s.trimmed()) {
        if (c.isSpace() || c == QChar(0xa0) || c == QChar(0x202f))
            continue;
        if (c == u',')
            c = u'.';
        else if (c == u'−')
            c = u'-';
        else if (c == u'E')
            c = u'e';
        cleaned.append(c);
    }
    if (cleaned.isEmpty())
        return std::nullopt;
    // pas d'écritures exotiques acceptées par certaines bibliothèques (0x10, 1_000…)
    for (QChar c : cleaned) {
        if (!(isDigit(c) || c == u'.' || c == u'-' || c == u'+' || c == u'e'))
            return std::nullopt;
    }
    bool ok = false;
    const double v = cleaned.toDouble(&ok);
    if (!ok || !std::isfinite(v))
        return std::nullopt;
    return v;
}

QString formatSig(double v, int sig, NumberStyle style)
{
    if (std::isnan(v))
        return QString();
    if (std::isinf(v))
        return v > 0 ? u"∞"_s : u"−∞"_s;
    if (v == 0.0)
        return u"0"_s;
    sig = std::clamp(sig, 1, 17);
    const int exp = static_cast<int>(std::floor(std::log10(std::abs(v))));
    QString s;
    if (exp >= -4 && exp < 6) {
        const int decimals = std::max(sig - 1 - exp, 0);
        s = trimZeros(QString::number(v, 'f', decimals));
    } else {
        const auto [mant, e] = sciParts(v, sig - 1);
        s = trimZeros(mant) + u'e' + QString::number(e);
    }
    return applyComma(s, style);
}

QString formatFixed(double v, int decimals, NumberStyle style)
{
    if (!std::isfinite(v))
        return formatSig(v, 3, style);
    QString s = QString::number(v, 'f', std::max(decimals, 0));
    if (s.startsWith(u'-')) {
        bool zero = true;
        for (qsizetype i = 1; i < s.size(); ++i) {
            if (s[i] != u'0' && s[i] != u'.')
                zero = false;
        }
        if (zero)
            s.remove(0, 1);
    }
    return applyComma(s, style);
}

QString formatSci(double v, int decimals, NumberStyle style)
{
    if (!std::isfinite(v))
        return formatSig(v, 3, style);
    if (v == 0.0)
        return u"0"_s;
    const auto [mant, e] = sciParts(v, std::max(decimals, 0));
    if (e == 0)
        return applyComma(mant, style);
    return applyComma(mant, style) + u"×10"_s + superscript(QString::number(e));
}

QString shortest(double v)
{
    if (std::isnan(v))
        return u"nan"_s;
    if (std::isinf(v))
        return v > 0 ? u"inf"_s : u"-inf"_s;
    return QString::number(v, 'f', QLocale::FloatingPointShortest);
}

QString shortestStyled(double v, NumberStyle style)
{
    return applyComma(shortest(v), style);
}

QString pythonFloatRepr(double v)
{
    if (std::isnan(v))
        return u"nan"_s;
    if (std::isinf(v))
        return v > 0 ? u"inf"_s : u"-inf"_s;
    const QString e = QString::number(v, 'e', QLocale::FloatingPointShortest);
    const qsizetype k = e.indexOf(u'e');
    const int exp = k >= 0 ? e.mid(k + 1).toInt() : 0;
    if (v != 0.0 && (exp < -4 || exp >= 16)) {
        // mantisse la plus courte, exposant signé sur au moins deux chiffres
        const QString mant = e.left(k);
        return mant + u'e' + (exp < 0 ? u'-' : u'+') + QString::number(std::abs(exp)).rightJustified(2, u'0');
    }
    QString s = QString::number(v, 'f', QLocale::FloatingPointShortest);
    if (!s.contains(u'.'))
        s += u".0"_s;
    return s;
}

QString superscript(const QString &s)
{
    static const char16_t digits[] = u"⁰¹²³⁴⁵⁶⁷⁸⁹";
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (isDigit(c))
            out.append(QChar(digits[c.unicode() - u'0']));
        else if (c == u'-' || c == u'−')
            out.append(u'⁻');
        else if (c == u'+')
            out.append(u'⁺');
        else
            out.append(c);
    }
    return out;
}

QString prettyUnit(const QString &unit)
{
    const QString &chars = unit;
    QString out;
    int i = 0;
    const int n = static_cast<int>(chars.size());
    while (i < n) {
        const QChar c = chars[i];
        const bool prevAlpha = i > 0 && (chars[i - 1].isLetter() || chars[i - 1] == u')');
        // exposant explicite : ^-2, ^3
        if (c == u'^') {
            const auto [exp, len] = readExponent(chars, i + 1);
            if (len > 0) {
                out += superscript(exp);
                i += 1 + len;
                continue;
            }
        }
        // exposant implicite après une lettre : s-1, m2
        if (prevAlpha && (isDigit(c) || ((c == u'-' || c == u'−') && i + 1 < n && isDigit(chars[i + 1])))) {
            const auto [exp, len] = readExponent(chars, i);
            if (len > 0) {
                out += superscript(exp);
                i += len;
                continue;
            }
        }
        // point ou étoile de multiplication entre deux symboles
        if ((c == u'.' || c == u'*') && i > 0 && i + 1 < n) {
            if (!(isDigit(chars[i - 1]) && isDigit(chars[i + 1]))) {
                out.append(u'·');
                ++i;
                continue;
            }
        }
        out.append(c);
        ++i;
    }
    return out;
}

QString labelWithUnit(const QString &name, const QString &unit)
{
    const QString u = unit.trimmed();
    return u.isEmpty() ? name : name + u" ("_s + prettyUnit(u) + u')';
}

QString typographicMinus(const QString &s)
{
    if (s.startsWith(u'-'))
        return u'−' + s.mid(1);
    return s;
}

QString RoundedMeasure::toText(const QString &unit) const
{
    const QString u = prettyUnit(unit.trimmed());
    const QString core = value + u" ± "_s + uncertainty;
    QString body;
    if (exponent != 0)
        body = u'(' + core + u")×10"_s + superscript(QString::number(exponent));
    else if (u.isEmpty())
        body = core;
    else
        body = u'(' + core + u')';
    return u.isEmpty() ? body : body + u' ' + u;
}

RoundedMeasure roundMeasure(double value, double u, int sigU, NumberStyle style)
{
    sigU = std::max(sigU, 1);
    if (!std::isfinite(value) || !std::isfinite(u) || u <= 0.0)
        return {formatSig(value, 4, style), u == 0.0 ? u"0"_s : u"?"_s, 0};
    // facteur commun pour les valeurs très grandes ou très petites
    const double refMag = value != 0.0 ? std::max(std::abs(value), u) : u;
    const int mag = static_cast<int>(std::floor(std::log10(refMag)));
    const int exponent = (mag >= -3 && mag <= 4) ? 0 : static_cast<int>(std::floor(mag / 3.0)) * 3;
    const double scale = std::pow(10.0, exponent);
    const double v = value / scale;
    const double us = u / scale;
    int eU = static_cast<int>(std::floor(std::log10(us)));
    // l'arrondi de l'incertitude peut ajouter un chiffre (0,0996 → 0,10)
    int last = eU - (sigU - 1);
    const double roundedU = std::round(us / std::pow(10.0, last)) * std::pow(10.0, last);
    if (roundedU >= std::pow(10.0, eU + 1)) {
        eU += 1;
        last = eU - (sigU - 1);
    }
    const int decimals = std::max(-last, 0);
    const double q = std::pow(10.0, last);
    const double rv = std::round(v / q) * q;
    const double ru = std::round(us / q) * q;
    return {formatFixed(rv, decimals, style), formatFixed(ru, decimals, style), exponent};
}
