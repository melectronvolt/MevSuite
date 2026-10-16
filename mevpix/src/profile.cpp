#include "profile.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Valeur à partir de laquelle un pixel est considéré comme saturé (écrêté).
constexpr double SATURATION = 253.5;

/// Composantes d'un pixel `Format_RGB888`.
struct Rgb {
    double r, g, b;
};

double channelValue(Channel channel, Rgb px)
{
    switch (channel) {
    case Channel::Luminance: return 0.299 * px.r + 0.587 * px.g + 0.114 * px.b;
    case Channel::Red: return px.r;
    case Channel::Green: return px.g;
    case Channel::Blue: return px.b;
    }
    return 0.0;
}

/// Pixel écrêté dans ce canal. Pour la luminance, il suffit qu'une composante le soit : un laser
/// rouge sature le rouge (255) bien avant que la luminance n'approche 255.
bool isSaturated(Channel channel, Rgb px)
{
    if (channel == Channel::Luminance)
        return px.r >= SATURATION || px.g >= SATURATION || px.b >= SATURATION;
    return channelValue(channel, px) >= SATURATION;
}

Rgb pixel(const QImage &img, int i, int j)
{
    const uchar *p = img.constScanLine(j) + 3 * i;
    return {double(p[0]), double(p[1]), double(p[2])};
}

/// Le pixel (i, j) couvre [i, i+1[ × [j, j+1[ : son centre est en (i + 0,5 ; j + 0,5).
Rgb nearest(const QImage &img, double x, double y)
{
    const int i = std::min(static_cast<int>(std::max(std::floor(x), 0.0)), img.width() - 1);
    const int j = std::min(static_cast<int>(std::max(std::floor(y), 0.0)), img.height() - 1);
    return pixel(img, i, j);
}

double bilinear(const QImage &img, Channel channel, double x, double y)
{
    const int w = img.width(), h = img.height();
    x = std::clamp(x - 0.5, 0.0, double(w - 1));
    y = std::clamp(y - 0.5, 0.0, double(h - 1));
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0, fy = y - y0;
    const auto v = [&](int i, int j) { return channelValue(channel, pixel(img, i, j)); };
    return v(x0, y0) * (1.0 - fx) * (1.0 - fy) + v(x1, y0) * fx * (1.0 - fy) + v(x0, y1) * (1.0 - fx) * fy
         + v(x1, y1) * fx * fy;
}

/// Arrondi à 6 chiffres significatifs, écrit sans zéros superflus.
QString csvNum(double v, QChar dec)
{
    if (v == 0.0 || !std::isfinite(v))
        return u"0"_s;
    const double factor = std::pow(10.0, 5 - static_cast<int>(std::floor(std::log10(std::abs(v)))));
    return withDecimal(shortest(std::round(v * factor) / factor), dec);
}

} // namespace

double Profile::lenPx() const
{
    return stepPx * double(values.empty() ? 0 : values.size() - 1);
}

double Profile::valueAt(double t) const
{
    if (values.empty())
        return 0.0;
    const size_t last = values.size() - 1;
    const auto k = static_cast<size_t>(std::round(std::clamp(t, 0.0, 1.0) * double(last)));
    return values[std::min(k, last)];
}

Profile sampleProfile(const QImage &img, const Segment &seg, int bandWidth, Channel channel)
{
    Q_ASSERT(img.format() == QImage::Format_RGB888 && !img.isNull());
    const double len = seg.lenPx();
    const int n = std::max(static_cast<int>(std::ceil(len)) + 1, 2);
    const QPointF normal = seg.normal();
    const int band = std::max(bandWidth, 1);
    std::vector<double> offsets(band);
    for (int j = 0; j < band; ++j)
        offsets[j] = j - (band - 1) / 2.0;

    Profile profile;
    profile.stepPx = len / (n - 1);
    profile.values.reserve(n);
    for (int k = 0; k < n; ++k) {
        const QPointF p = seg.pointAt(double(k) / (n - 1));
        bool saturated = false;
        double sum = 0.0;
        for (double o : offsets) {
            const QPointF q = p + normal * o;
            saturated = saturated || isSaturated(channel, nearest(img, q.x(), q.y()));
            sum += bilinear(img, channel, q.x(), q.y());
        }
        if (saturated)
            ++profile.saturated;
        profile.values.push_back(sum / band);
    }
    return profile;
}

QString toCsv(const Profile &profile, const std::optional<Calibration> &calib, QChar dec)
{
    const QString unit = calib ? unitSymbol(calib->unit) : u"px"_s;
    const double perPx = calib ? calib->unitsPerPx() : 1.0;
    const QChar sep = dec == u',' ? u';' : u',';
    QString out = u"x (%1)%2I\n"_s.arg(unit, sep);
    for (size_t k = 0; k < profile.values.size(); ++k) {
        const double x = double(k) * profile.stepPx * perPx;
        out += csvNum(x, dec) + sep + fmtDec(profile.values[k], 2, dec) + u'\n';
    }
    return out;
}

QString withCsvExtension(const QString &path)
{
    // « .csv » ajouté au nom tapé sans extension ; si ce fichier existe déjà, le programme demande
    // avant de le remplacer (le dialogue a confirmé le nom tapé, pas celui-ci)
    return QFileInfo(path).suffix().isEmpty() ? path + u".csv"_s : path;
}
