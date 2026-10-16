#include "python/npformat.h"

#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using arr::Arr;
using arr::Kind;

namespace npformat {

namespace {

constexpr int LINE_WIDTH = 75;
constexpr size_t THRESHOLD = 1000;
constexpr size_t EDGE = 3;
constexpr int PRECISION = 8;

QString trimZeros(QString s)
{
    while (s.endsWith(u'0'))
        s.chop(1);
    return s;
}

/// Écriture décimale la plus courte qui relit la même valeur, limitée à `precision` décimales
/// (comme `dragon4_positional(unique=True)`).
QString positional(double x, int precision)
{
    const QString shortest = QString::number(std::abs(x), 'f', QLocale::FloatingPointShortest);
    const qsizetype dot = shortest.indexOf(u'.');
    const qsizetype fracLen = dot < 0 ? 0 : shortest.size() - dot - 1;
    QString s = fracLen <= precision ? shortest : trimZeros(QString::number(std::abs(x), 'f', precision));
    if (!s.contains(u'.'))
        s += u'.';
    if (std::signbit(x))
        s.prepend(u'-');
    return s;
}

/// Écriture scientifique la plus courte (mantisse, exposant), au plus `precision` décimales.
std::pair<QString, int> scientificParts(double x, int precision)
{
    if (x == 0.0)
        return {std::signbit(x) ? u"-0."_s : u"0."_s, 0};
    const QString s = QString::number(std::abs(x), 'e', QLocale::FloatingPointShortest);
    const qsizetype k = s.indexOf(u'e');
    QString m = s.left(k);
    int e = s.mid(k + 1).toInt();
    const qsizetype dot = m.indexOf(u'.');
    const qsizetype frac = dot < 0 ? 0 : m.size() - dot - 1;
    if (frac > precision) {
        const QString t = QString::number(std::abs(x), 'e', precision);
        const qsizetype k2 = t.indexOf(u'e');
        e = t.mid(k2 + 1).toInt();
        m = trimZeros(t.left(k2));
    }
    if (!m.contains(u'.'))
        m += u'.';
    if (x < 0)
        m.prepend(u'-');
    return {m, e};
}

QString expStr(int e, int digits)
{
    return u'e' + QString(e < 0 ? u'-' : u'+') + QString::number(std::abs(e)).rightJustified(digits, u'0');
}

int intPartLen(const QString &s)
{
    const qsizetype d = s.indexOf(u'.');
    return static_cast<int>(d < 0 ? s.size() : d);
}

int fracPartLen(const QString &s)
{
    const qsizetype d = s.indexOf(u'.');
    return static_cast<int>(d < 0 ? 0 : s.size() - d - 1);
}

/// Format commun aux éléments réels d'un tableau.
struct FloatFormat {
    bool exp = false;
    int padLeft = 0, padRight = 0, precision = 0, expDigits = 2;
    bool plus = false;

    FloatFormat(const std::vector<double> &vals, bool plusSign) : plus(plusSign)
    {
        std::vector<double> finite, nonzero;
        for (double v : vals) {
            if (std::isfinite(v)) {
                finite.push_back(v);
                if (v != 0.0)
                    nonzero.push_back(std::abs(v));
            }
        }
        if (!nonzero.empty()) {
            const double mx = *std::max_element(nonzero.begin(), nonzero.end());
            const double mn = *std::min_element(nonzero.begin(), nonzero.end());
            exp = mx >= 1e8 || mn < 0.0001 || mx / mn > 1000.0;
        }
        auto sign = [&](const QString &s) { return (plus && !s.startsWith(u'-')) ? u'+' + s : s; };
        if (!finite.empty()) {
            if (exp) {
                for (double x : finite) {
                    const auto [m, e] = scientificParts(x, PRECISION);
                    precision = std::max(precision, fracPartLen(m));
                    expDigits = std::max(expDigits, static_cast<int>(QString::number(std::abs(e)).size()));
                    padLeft = std::max(padLeft, intPartLen(sign(m)));
                }
                padRight = expDigits + 2 + precision;
            } else {
                for (double x : finite) {
                    const QString s = sign(positional(x, PRECISION));
                    padLeft = std::max(padLeft, intPartLen(s));
                    padRight = std::max(padRight, fracPartLen(s));
                }
            }
        }
        // valeurs non finies : la colonne doit pouvoir les contenir
        if (finite.size() != vals.size()) {
            const bool negInf = std::any_of(vals.begin(), vals.end(), [](double v) { return std::isinf(v) && v < 0; }) || plus;
            const int offset = padRight + 1;
            auto need = [&](int len) { return std::max(len - offset, 0); };
            padLeft = std::max({padLeft, need(3), need(3 + (negInf ? 1 : 0))});
        }
    }

    QString fmt(double x) const
    {
        const int width = padLeft + padRight + 1;
        if (!std::isfinite(x)) {
            QString s = std::isnan(x) ? u"nan"_s : x > 0 ? (plus ? u"+inf"_s : u"inf"_s) : u"-inf"_s;
            return s.rightJustified(width);
        }
        auto withSign = [&](const QString &s) { return (plus && !s.startsWith(u'-')) ? u'+' + s : s; };
        if (exp) {
            const auto [m0, e] = scientificParts(x, PRECISION);
            const QString m = withSign(m0);
            const qsizetype d = m.indexOf(u'.');
            const QString ip = m.left(d);
            const QString fp = m.mid(d + 1).leftJustified(precision, u'0');
            return ip.rightJustified(padLeft) + u'.' + fp + expStr(e, expDigits);
        }
        const QString s = withSign(positional(x, PRECISION));
        const qsizetype d = s.indexOf(u'.');
        const QString ip = d < 0 ? s : s.left(d);
        const QString fp = d < 0 ? QString() : s.mid(d + 1);
        return ip.rightJustified(padLeft) + u'.' + fp.leftJustified(padRight);
    }
};

/// Textes des éléments, tous de même largeur.
QStringList elementStrings(const Arr &a, const std::vector<size_t> &idx)
{
    QStringList out;
    switch (a.kind) {
    case Kind::Bool:
        for (size_t i : idx)
            out << (a.re[i] != 0.0 ? u"True"_s : u"False"_s).rightJustified(5);
        break;
    case Kind::Int: {
        int w = 0;
        for (size_t i : idx) {
            out << QString::number(static_cast<long long>(a.re[i]));
            w = std::max(w, static_cast<int>(out.last().size()));
        }
        for (QString &s : out)
            s = s.rightJustified(w);
        break;
    }
    case Kind::Float: {
        std::vector<double> vals;
        for (size_t i : idx)
            vals.push_back(a.re[i]);
        const FloatFormat f(vals, false);
        for (double x : vals)
            out << f.fmt(x);
        break;
    }
    case Kind::Complex: {
        std::vector<double> re, im;
        for (size_t i : idx) {
            re.push_back(a.re[i]);
            im.push_back(a.im[i]);
        }
        const FloatFormat fr(re, false), fi(im, true);
        for (size_t k = 0; k < re.size(); ++k) {
            // comme numpy : les espaces de remplissage de la partie imaginaire vont après le « j »
            QString imag = fi.fmt(im[k]);
            while (imag.startsWith(u' '))
                imag.remove(0, 1);
            qsizetype pad = 0;
            while (imag.endsWith(u' ')) {
                imag.chop(1);
                ++pad;
            }
            out << fr.fmt(re[k]) + imag + u'j' + QString(pad, u' ');
        }
        break;
    }
    }
    return out;
}

void extendLine(QString &s, QString &line, const QString &word, int width, const QString &prefix)
{
    bool wrap = line.size() + word.size() > width;
    if (line.size() <= prefix.size())
        wrap = false;
    if (wrap) {
        QString t = line;
        while (t.endsWith(u' '))
            t.chop(1);
        s += t + u'\n';
        line = prefix;
    }
    line += word;
}

std::pair<QString, bool> formatBody(const Arr &a, const QString &sep, const QString &prefix, int lineWidth)
{
    const size_t n = a.size();
    const bool summary = n > THRESHOLD;
    std::vector<size_t> idx;
    if (summary) {
        for (size_t i = 0; i < EDGE; ++i)
            idx.push_back(i);
        for (size_t i = n - EDGE; i < n; ++i)
            idx.push_back(i);
    } else {
        for (size_t i = 0; i < n; ++i)
            idx.push_back(i);
    }
    const QStringList words = elementStrings(a, idx);
    QString sepTrim = sep;
    while (sepTrim.endsWith(u' '))
        sepTrim.chop(1);
    const int elemWidth = lineWidth - static_cast<int>(std::max<qsizetype>(sepTrim.size(), 1));
    QString s;
    QString line = prefix;
    const qsizetype lead = summary ? static_cast<qsizetype>(EDGE) : 0;
    for (qsizetype k = 0; k < lead; ++k) {
        extendLine(s, line, words[k], elemWidth, prefix);
        line += sep;
    }
    if (summary) {
        extendLine(s, line, u"..."_s, elemWidth, prefix);
        line += sep;
    }
    for (qsizetype k = lead; k < words.size(); ++k) {
        extendLine(s, line, words[k], elemWidth, prefix);
        if (k + 1 < words.size())
            line += sep;
    }
    s += line;
    return {u'[' + s.mid(prefix.size()) + u']', summary};
}

} // namespace

QString arrayStr(const Arr &a)
{
    if (a.size() == 0)
        return u"[]"_s;
    return formatBody(a, u" "_s, u" "_s, LINE_WIDTH).first;
}

QString arrayRepr(const Arr &a)
{
    if (a.size() == 0)
        return u"array([], dtype="_s + QString::fromLatin1(arr::kindName(a.kind)) + u')';
    const QString prefix(QString(u"array(").size() + 1, u' ');
    const auto [body, summary] = formatBody(a, u", "_s, prefix, LINE_WIDTH - 1);
    const QString shape = summary ? u", shape=(%1,)"_s.arg(a.size()) : QString();
    return u"array("_s + body + shape + u')';
}

} // namespace npformat
