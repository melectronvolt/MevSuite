#include "plot/renderer.h"

#include "core/resources.h"
#include "core/spline.h"
#include "plot/ticks.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHash>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

// --- axes ------------------------------------------------------------------------------------

double AxisMap::norm(double v) const
{
    if (log)
        return (std::log10(v) - std::log10(min)) / (std::log10(max) - std::log10(min));
    return (v - min) / (max - min);
}

double AxisMap::fromPx(double px) const
{
    const double t = (px - a) / (b - a);
    if (log)
        return std::pow(10.0, std::log10(min) + t * (std::log10(max) - std::log10(min)));
    return min + t * (max - min);
}

// --- thème -----------------------------------------------------------------------------------

PlotTheme PlotTheme::light()
{
    PlotTheme t;
    t.dark = false;
    t.background = QColor(255, 255, 255);
    t.frame = QColor(38, 38, 38);
    t.text = QColor(25, 25, 25);
    t.grid = QColor(218, 221, 226);
    t.gridMinor = QColor(238, 240, 243);
    t.legendFill = QColor(255, 255, 255, 235);
    t.legendBorder = QColor(196, 199, 204);
    t.labelFill = QColor(255, 255, 255, 215);
    return t;
}

PlotTheme PlotTheme::darkTheme()
{
    PlotTheme t;
    t.dark = true;
    // fond, textes et légende aux couleurs du thème sombre de la suite (Base, WindowText, Mid)
    t.background = QColor(22, 23, 25);
    t.frame = QColor(178, 180, 186);
    t.text = QColor(232, 232, 232);
    t.grid = QColor(56, 58, 63);
    t.gridMinor = QColor(36, 37, 40);
    t.legendFill = QColor(30, 31, 34, 235);
    t.legendBorder = QColor(70, 72, 78);
    t.labelFill = QColor(22, 23, 25, 215);
    return t;
}

QColor PlotTheme::adapt(const Color &c) const
{
    QColor q = c.q();
    if (!dark)
        return q;
    // sur fond sombre, une couleur sombre (noir, gris foncé, bleu nuit…) s'éclaircit en gardant sa teinte
    float h, s, l, a;
    q.getHslF(&h, &s, &l, &a);
    if (l < 0.45f)
        q.setHslF(h, s, 0.92f - l, a);
    return q;
}

// --- polices ---------------------------------------------------------------------------------

namespace {

QString plotFamily(bool mono)
{
    static QString regular, monoFamily;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        ensureCoreResources();
        for (const char *f : {":/fonts/DejaVuSans.ttf", ":/fonts/DejaVuSans-Bold.ttf"}) {
            const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(f));
            if (id >= 0 && regular.isEmpty() && !QFontDatabase::applicationFontFamilies(id).isEmpty())
                regular = QFontDatabase::applicationFontFamilies(id).first();
        }
        const int id = QFontDatabase::addApplicationFont(u":/fonts/DejaVuSansMono.ttf"_s);
        if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
            monoFamily = QFontDatabase::applicationFontFamilies(id).first();
        if (regular.isEmpty())
            regular = u"DejaVu Sans"_s;
        if (monoFamily.isEmpty())
            monoFamily = u"DejaVu Sans Mono"_s;
    }
    return mono ? monoFamily : regular;
}

QFont makeFont(const QString &family, double px, bool bold, const QPaintDevice *device)
{
    QFont f(family);
    const int dpi = device ? device->logicalDpiY() : 96;
    // taille en points pour que le texte mesure `px` unités de l'appareil, quelle que soit sa résolution
    f.setPointSizeF(std::max(px, 0.5) * 72.0 / std::max(dpi, 1));
    f.setBold(bold);
    // métriques identiques à l'écran et à l'export : pas d'ajustement à la grille de pixels
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setKerning(true);
    return f;
}

} // namespace

QFont plotFont(double px, bool bold, const QPaintDevice *device)
{
    return makeFont(plotFamily(false), px, bold, device);
}

QFont monoFont(double px, const QPaintDevice *device)
{
    return makeFont(plotFamily(true), px, false, device);
}

// --- marqueurs ---------------------------------------------------------------------------------

namespace {

double markerStroke(double size)
{
    return std::clamp(size / 5.5, 1.0, 2.2);
}

/// Étendue d'un marqueur autour de son centre (trait compris) : haut, bas, gauche, droite.
struct Extent {
    double up = 0, down = 0, left = 0, right = 0;
};

Extent markerExtent(Marker m, double size)
{
    const double h = size / 2.0;
    const double s = markerStroke(size) / 2.0;
    switch (m) {
    case Marker::Disc: return {h * 0.85, h * 0.85, h * 0.85, h * 0.85};
    case Marker::Circle: return {h * 0.85 + s, h * 0.85 + s, h * 0.85 + s, h * 0.85 + s};
    case Marker::Square: return {h * 0.75 + s, h * 0.75 + s, h * 0.75 + s, h * 0.75 + s};
    case Marker::Triangle: return {h * 0.95 + s, h * 0.475 + s, h * 0.82 + s, h * 0.82 + s};
    case Marker::Diamond: return {h + s, h + s, h * 0.75 + s, h * 0.75 + s};
    default: return {};
    }
}

} // namespace

void drawMarker(QPainter &p, Marker marker, QPointF c, double size, const QColor &color, const QColor &fill)
{
    if (marker == Marker::None)
        return;
    const double h = size / 2.0;
    QPen pen(color, markerStroke(size));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::MiterJoin);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    switch (marker) {
    case Marker::None:
        break;
    case Marker::Point: {
        const double r = std::max(size * 0.22, 1.6);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(c, r, r);
        break;
    }
    case Marker::Disc: {
        const double r = h * 0.85;
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(c, r, r);
        break;
    }
    case Marker::Circle:
        p.setPen(pen);
        p.setBrush(fill);
        p.drawEllipse(c, h * 0.85, h * 0.85);
        break;
    case Marker::Cross: {
        const double d = h * 0.8;
        p.setPen(pen);
        p.drawLine(QPointF(c.x() - d, c.y() - d), QPointF(c.x() + d, c.y() + d));
        p.drawLine(QPointF(c.x() - d, c.y() + d), QPointF(c.x() + d, c.y() - d));
        break;
    }
    case Marker::Plus:
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.drawLine(QPointF(c.x() - h, c.y()), QPointF(c.x() + h, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - h), QPointF(c.x(), c.y() + h));
        break;
    case Marker::Square: {
        const double d = h * 0.75;
        p.setPen(pen);
        p.setBrush(fill);
        p.drawRect(QRectF(c.x() - d, c.y() - d, 2 * d, 2 * d));
        break;
    }
    case Marker::Triangle: {
        const double d = h * 0.95;
        p.setPen(pen);
        p.setBrush(fill);
        p.drawPolygon(QPolygonF{QPointF(c.x(), c.y() - d), QPointF(c.x() + d * 0.866, c.y() + d * 0.5), QPointF(c.x() - d * 0.866, c.y() + d * 0.5)});
        break;
    }
    case Marker::Diamond:
        p.setPen(pen);
        p.setBrush(fill);
        p.drawPolygon(QPolygonF{QPointF(c.x(), c.y() - h), QPointF(c.x() + h * 0.75, c.y()), QPointF(c.x(), c.y() + h), QPointF(c.x() - h * 0.75, c.y())});
        break;
    }
    p.restore();
}

// --- mise en page et dessin ----------------------------------------------------------------------

namespace {

constexpr double TICK_LEN = 5.0;
constexpr double MINOR_LEN = 3.0;
constexpr double PAD = 8.0;

enum class VAlign { Top, Center, Baseline };

/// Mesure du texte, avec la police des graphes pour l'appareil de dessin.
class Metrics {
public:
    explicit Metrics(const QPaintDevice *dev) : m_dev(dev) {}

    QFont font(double size, bool bold) const { return plotFont(size, bold, m_dev); }
    const QFontMetricsF &fm(double size, bool bold) const
    {
        const quint64 key = (static_cast<quint64>(std::lround(size * 100.0)) << 1) | (bold ? 1u : 0u);
        auto it = m_cache.find(key);
        if (it == m_cache.end())
            it = m_cache.insert(key, QFontMetricsF(font(size, bold), const_cast<QPaintDevice *>(m_dev)));
        return *it;
    }
    double width(const QString &s, double size, bool bold = false) const { return fm(size, bold).horizontalAdvance(s); }
    double ascent(double size) const { return fm(size, false).ascent(); }
    double descent(double size) const { return fm(size, false).descent(); }
    double capHeight(double size) const { return fm(size, false).capHeight(); }

private:
    const QPaintDevice *m_dev;
    mutable QHash<quint64, QFontMetricsF> m_cache;
};

using Range = std::optional<std::pair<double, double>>;

double uAt(const std::vector<double> &u, size_t i)
{
    if (u.empty())
        return NAN;
    if (u.size() == 1)
        return u[0];
    return i < u.size() ? u[i] : NAN;
}

/// Incertitudes du point i vers les valeurs décroissantes et croissantes, sur x ou sur y : celles
/// choisies côté par côté, sinon l'incertitude de la grandeur (NaN : pas de barre de ce côté).
std::pair<double, double> sidesAt(const Series &s, bool x, size_t i)
{
    const std::vector<double> &sym = x ? s.ux : s.uy;
    const std::vector<double> &lo = x ? s.uxLow : s.uyLow;
    const std::vector<double> &hi = x ? s.uxHigh : s.uyHigh;
    return {std::abs(uAt(lo.empty() ? sym : lo, i)), std::abs(uAt(hi.empty() ? sym : hi, i))};
}

double finiteOr0(double v)
{
    return std::isfinite(v) ? v : 0.0;
}

/// Bornes d'un axe : bornes fixées, ou bornes des données avec une marge.
std::pair<double, double> axisRange(const AxisSpec &spec, Range data)
{
    if (!spec.autoScale && spec.min < spec.max && std::isfinite(spec.min) && std::isfinite(spec.max) && (!spec.log || spec.min > 0.0))
        return {spec.min, spec.max};
    if (!data)
        return spec.log ? std::make_pair(1.0, 10.0) : std::make_pair(0.0, 1.0);
    auto [lo, hi] = *data;
    if (spec.includeZero && !spec.log) {
        lo = std::min(lo, 0.0);
        hi = std::max(hi, 0.0);
    }
    if (spec.log) {
        double l = std::log10(lo), h = std::log10(hi);
        if (h - l < 1e-9) {
            l -= 0.5;
            h += 0.5;
        }
        const double pad = (h - l) * 0.04;
        return {std::pow(10.0, l - pad), std::pow(10.0, h + pad)};
    }
    if (hi - lo <= std::numeric_limits<double>::epsilon() * std::max(std::abs(hi), 1.0) * 4.0) {
        const double d = lo == 0.0 ? 1.0 : std::abs(lo) * 0.1;
        return {lo - d, hi + d};
    }
    const double pad = (hi - lo) * 0.04;
    // une borne nulle reste nulle si les données y commencent
    const double lo2 = (lo == 0.0 && spec.includeZero) ? 0.0 : lo - pad;
    const double hi2 = (hi == 0.0 && spec.includeZero) ? 0.0 : hi + pad;
    return {lo2, hi2};
}

void extend(Range &r, double v, bool log)
{
    if (!std::isfinite(v) || (log && v <= 0.0))
        return;
    if (!r)
        r = std::make_pair(v, v);
    else
        r = std::make_pair(std::min(r->first, v), std::max(r->second, v));
}

/// Étendue des données sur l'axe x, ou sur l'axe y d'un côté donné. Pour l'axe y, seuls
/// comptent les points dont l'abscisse est dans `xWindow`.
Range dataExtent(const PlotData &data, bool axisX, YSide side, bool log, Range xWindow)
{
    Range r;
    for (const auto &s : data.series) {
        if (!s.autoscale || (!axisX && s.side != side))
            continue;
        for (size_t i = 0; i < std::min(s.xs.size(), s.ys.size()); ++i) {
            const double x = s.xs[i], y = s.ys[i];
            if (!std::isfinite(x) || !std::isfinite(y))
                continue;
            if (!axisX && xWindow && (x < xWindow->first || x > xWindow->second))
                continue;
            if (axisX) {
                const double half = s.bars ? *s.bars / 2.0 : 0.0;
                const bool withX = s.errors == ErrorBars::Both || s.errors == ErrorBars::Rectangles;
                const auto [lo, hi] = withX ? sidesAt(s, true, i) : std::pair<double, double>(NAN, NAN);
                // le point lui-même compte aussi : en échelle log, x − u peut ne pas être représentable
                extend(r, x, log);
                extend(r, x - std::max(finiteOr0(lo), half), log);
                extend(r, x + std::max(finiteOr0(hi), half), log);
            } else {
                const auto [lo, hi] = s.errors != ErrorBars::None ? sidesAt(s, false, i) : std::pair<double, double>(NAN, NAN);
                extend(r, y, log);
                extend(r, y - finiteOr0(lo), log);
                extend(r, y + finiteOr0(hi), log);
                if (s.bars)
                    extend(r, 0.0, log);
            }
        }
    }
    return r;
}

ticks::Ticks computeTicks(const AxisSpec &spec, double min, double max, double lengthPx, bool horizontal, NumberStyle style)
{
    if (spec.log)
        return ticks::logarithmic(min, max, style);
    const double target = std::clamp(lengthPx / (horizontal ? 90.0 : 55.0), 2.0, 12.0);
    return ticks::linear(min, max, target, spec.majorStep, spec.minorDivisions, spec.format, style);
}

/// Libellé d'axe : réglage de l'utilisateur, sinon nom et unité automatiques.
QString axisTitle(const AxisSpec &spec, const QString &autoName, const QString &autoUnit, int factorExp)
{
    const QString name = spec.label.value_or(autoName);
    const QString unit = prettyUnit(spec.unit.value_or(autoUnit).trimmed());
    const QString factor = factorExp != 0 ? u"×10"_s + superscript(QString::number(factorExp)) : QString();
    if (unit.isEmpty() && factor.isEmpty())
        return name;
    if (factor.isEmpty())
        return u"%1 (%2)"_s.arg(name, unit);
    if (unit.isEmpty())
        return u"%1 (%2)"_s.arg(name, factor);
    return u"%1 (%2 %3)"_s.arg(name, factor, unit);
}

std::vector<const Series *> legendEntries(const PlotData &data)
{
    std::vector<const Series *> out;
    for (const auto &s : data.series) {
        if (s.legend && !s.legend->isEmpty())
            out.push_back(&s);
    }
    return out;
}

constexpr double LEGEND_SAMPLE = 30.0;

std::pair<double, double> legendBoxSize(const std::vector<const Series *> &entries, double size, const Metrics &m)
{
    double tw = 0.0;
    for (const Series *s : entries)
        tw = std::max(tw, m.width(*s->legend, size));
    const double row = size * 1.6;
    return {tw + LEGEND_SAMPLE + 22.0, entries.size() * row + 8.0};
}

/// Mesures préalables d'un graphe, avant alignement.
struct Prepared {
    std::pair<double, double> xRange, yRange;
    std::optional<std::pair<double, double>> y2Range;
    ticks::Ticks xTicks, yTicks;
    std::optional<ticks::Ticks> y2Ticks;
    QString xTitle, yTitle, y2Title;
    double margins[4] = {0, 0, 0, 0}; // gauche, haut, droite, bas
    std::optional<std::pair<double, double>> legendSize;
};

Prepared prepare(const Plot &plot, const PlotData &data, QRectF cell, Range xOverride, const Metrics &m, NumberStyle style)
{
    Prepared p;
    p.xRange = xOverride ? *xOverride : axisRange(plot.x, dataExtent(data, true, YSide::Left, plot.x.log, std::nullopt));
    // abscisses zoomées : l'échelle automatique des ordonnées suit la partie visible
    const Range window = (!plot.x.autoScale || xOverride) ? Range(p.xRange) : Range();
    auto yExtent = [&](YSide side, bool log) {
        Range r = dataExtent(data, false, side, log, window);
        return r ? r : dataExtent(data, false, side, log, std::nullopt);
    };
    p.yRange = axisRange(plot.y, yExtent(YSide::Left, plot.y.log));
    const bool right = std::any_of(data.series.begin(), data.series.end(), [](const Series &s) { return s.side == YSide::Right; });
    if (right)
        p.y2Range = axisRange(plot.y2, yExtent(YSide::Right, plot.y2.log));
    // graduations estimées avec la taille de la cellule (affinées ensuite)
    p.xTicks = computeTicks(plot.x, p.xRange.first, p.xRange.second, cell.width() * 0.8, true, style);
    p.yTicks = computeTicks(plot.y, p.yRange.first, p.yRange.second, cell.height() * 0.75, false, style);
    if (p.y2Range)
        p.y2Ticks = computeTicks(plot.y2, p.y2Range->first, p.y2Range->second, cell.height() * 0.75, false, style);
    p.xTitle = axisTitle(plot.x, data.xName, data.xUnit, p.xTicks.factorExp);
    p.yTitle = axisTitle(plot.y, data.yName, data.yUnit, p.yTicks.factorExp);
    if (p.y2Ticks)
        p.y2Title = axisTitle(plot.y2, data.y2Name, data.y2Unit, p.y2Ticks->factorExp);

    auto tickW = [&](const ticks::Ticks &t, double size) {
        double w = 0.0;
        for (const QString &l : t.labels)
            w = std::max(w, m.width(l, size));
        return w;
    };
    auto labelH = [&](const QString &s, double size) { return s.isEmpty() ? 0.0 : m.ascent(size) + m.descent(size) + 6.0; };
    const double left = PAD + labelH(p.yTitle, plot.y.labelSize) + tickW(p.yTicks, plot.y.tickSize) + TICK_LEN + 5.0;
    double rightM;
    if (p.y2Ticks) {
        rightM = PAD + labelH(p.y2Title, plot.y2.labelSize) + tickW(*p.y2Ticks, plot.y2.tickSize) + TICK_LEN + 5.0;
    } else {
        const double last = p.xTicks.labels.isEmpty() ? 0.0 : m.width(p.xTicks.labels.last(), plot.x.tickSize);
        rightM = std::max(last / 2.0 + 4.0, 16.0);
    }
    const double titleH = (plot.title.visible && !plot.title.text.isEmpty()) ? m.ascent(plot.title.size) + m.descent(plot.title.size) + 8.0 : 0.0;
    const double top = PAD + titleH + m.capHeight(plot.y.tickSize) * 0.6 + 4.0;
    const double bottom = TICK_LEN + 4.0 + m.ascent(plot.x.tickSize) + m.descent(plot.x.tickSize) + labelH(p.xTitle, plot.x.labelSize) + PAD;
    const auto entries = legendEntries(data);
    if (plot.legend.visible && !entries.empty())
        p.legendSize = legendBoxSize(entries, plot.legend.size, m);
    if (p.legendSize && plot.legend.pos == LegendPos::Outside)
        rightM += p.legendSize->first + 10.0;
    p.margins[0] = left;
    p.margins[1] = top;
    p.margins[2] = rightM;
    p.margins[3] = bottom;
    return p;
}

/// Découpe de Liang-Barsky ; renvoie les extrémités découpées et si le début est intact.
bool clipSegment(QPointF a, QPointF b, const QRectF &r, QPointF &pa, QPointF &pb, bool &aIn)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    double t0 = 0.0, t1 = 1.0;
    const double ps[4] = {-dx, dx, -dy, dy};
    const double qs[4] = {a.x() - r.left(), r.right() - a.x(), a.y() - r.top(), r.bottom() - a.y()};
    for (int k = 0; k < 4; ++k) {
        if (ps[k] == 0.0) {
            if (qs[k] < 0.0)
                return false;
        } else {
            const double t = qs[k] / ps[k];
            if (ps[k] < 0.0) {
                if (t > t1)
                    return false;
                t0 = std::max(t0, t);
            } else {
                if (t < t0)
                    return false;
                t1 = std::min(t1, t);
            }
        }
    }
    pa = QPointF(a.x() + t0 * dx, a.y() + t0 * dy);
    pb = QPointF(a.x() + t1 * dx, a.y() + t1 * dy);
    aIn = t0 == 0.0;
    return true;
}

/// Convertit une ligne brisée en pixels et la découpe au rectangle `r` (évite les
/// coordonnées démesurées après un fort zoom).
std::vector<QPolygonF> clipPolyline(const std::vector<std::pair<double, double>> &pts, const AxisMap &xm, const AxisMap &ym, const QRectF &r)
{
    std::vector<QPolygonF> out;
    if (pts.size() < 2)
        return out;
    QPolygonF cur;
    auto flush = [&] {
        if (cur.size() >= 2)
            out.push_back(cur);
        cur.clear();
    };
    std::optional<QPointF> prev;
    for (const auto &[x, y] : pts) {
        std::optional<QPointF> q;
        if (xm.valid(x) && ym.valid(y))
            q = QPointF(xm.toPx(x), ym.toPx(y));
        if (prev && q) {
            QPointF pa, pb;
            bool aIn = false;
            if (clipSegment(*prev, *q, r, pa, pb, aIn)) {
                if (!aIn || cur.isEmpty()) {
                    flush();
                    cur << pa;
                }
                cur << pb;
                // le segment sort du rectangle : le morceau se termine
                if (std::abs(pb.x() - q->x()) > 1e-6 || std::abs(pb.y() - q->y()) > 1e-6)
                    flush();
            } else {
                flush();
            }
        } else if (!q) {
            flush();
        }
        prev = q;
    }
    flush();
    return out;
}

/// Contexte de dessin d'une fenêtre graphique.
class Canvas {
public:
    Canvas(QPainter &p, const RenderOptions &o) : m_p(p), m_o(o), m_m(p.device()) {}

    const Metrics &metrics() const { return m_m; }

    /// Position alignée sur la grille de pixels pour un trait de largeur `w` (écran, PNG).
    double snap(double v, double w) const
    {
        if (!m_o.pixelSnap)
            return v;
        const double r = m_o.devicePixelRatio;
        const double wd = std::max(1.0, std::round(w * r));
        const double off = (static_cast<long long>(wd) % 2) ? 0.5 : 0.0;
        return (std::round(v * r - off) + off) / r;
    }
    /// Longueur arrondie à un nombre entier de pixels de l'appareil (embouts centrés sur la barre,
    /// à toutes les échelles).
    double snapLength(double v) const
    {
        if (!m_o.pixelSnap)
            return v;
        const double r = m_o.devicePixelRatio;
        return std::round(v * r) / r;
    }
    /// Largeur de trait arrondie à un nombre entier de pixels de l'appareil.
    double snapWidth(double w) const
    {
        if (!m_o.pixelSnap)
            return w;
        const double r = m_o.devicePixelRatio;
        return std::max(1.0, std::round(w * r)) / r;
    }

    QPen crispPen(const QColor &c, double w) const
    {
        QPen pen(c, snapWidth(w));
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::MiterJoin);
        return pen;
    }

    void hline(double x0, double x1, double y, const QPen &pen)
    {
        const double ys = snap(y, pen.widthF());
        m_p.setPen(pen);
        m_p.drawLine(QPointF(x0, ys), QPointF(x1, ys));
    }

    void vline(double x, double y0, double y1, const QPen &pen)
    {
        const double xs = snap(x, pen.widthF());
        m_p.setPen(pen);
        m_p.drawLine(QPointF(xs, y0), QPointF(xs, y1));
    }

    /// Texte aligné ; renvoie son rectangle englobant (avant rotation, puis tourné si besoin).
    QRectF text(double x, double y, const QString &s, double size, bool bold, const QColor &color, HAlign h, VAlign v, bool vertical = false)
    {
        if (s.isEmpty())
            return QRectF(x, y, 0, 0);
        const double w = m_m.width(s, size, bold);
        const double du = h == HAlign::Left ? 0.0 : h == HAlign::Center ? -w / 2.0 : -w;
        const double dv = v == VAlign::Baseline ? 0.0 : v == VAlign::Center ? m_m.capHeight(size) / 2.0 : m_m.capHeight(size);
        m_p.setFont(m_m.font(size, bold));
        m_p.setPen(color);
        const double asc = m_m.ascent(size), desc = m_m.descent(size);
        if (!vertical) {
            const QPointF pos(x + du, y + dv);
            m_p.drawText(pos, s);
            return QRectF(pos.x(), pos.y() - asc, w, asc + desc);
        }
        // rotation de −90° : lu de bas en haut
        m_p.save();
        m_p.translate(x, y);
        m_p.rotate(-90.0);
        m_p.drawText(QPointF(du, dv), s);
        m_p.restore();
        return QRectF(x + dv - asc, y - du - w, asc + desc, w);
    }

    QPainter &p() { return m_p; }
    const RenderOptions &opts() const { return m_o; }

private:
    QPainter &m_p;
    const RenderOptions &m_o;
    Metrics m_m;
};

QRectF unite(const QRectF &a, const QRectF &b)
{
    return a.united(b);
}

/// Coin de la zone de tracé où la légende recouvre le moins de points.
LegendPos bestLegendCorner(const PlotData &data, const AxisMap &xm, const AxisMap &ym, const std::optional<AxisMap> &y2m, QRectF area, double w,
                           double h, double inset)
{
    const std::array<std::pair<LegendPos, QPointF>, 4> corners = {{
        {LegendPos::TopRight, QPointF(area.right() - inset - w, area.top() + inset)},
        {LegendPos::TopLeft, QPointF(area.left() + inset, area.top() + inset)},
        {LegendPos::BottomRight, QPointF(area.right() - inset - w, area.bottom() - inset - h)},
        {LegendPos::BottomLeft, QPointF(area.left() + inset, area.bottom() - inset - h)},
    }};
    std::array<size_t, 4> counts{};
    for (const auto &s : data.series) {
        const AxisMap &m = (s.side == YSide::Right && y2m) ? *y2m : ym;
        const size_t n = std::min(s.xs.size(), s.ys.size());
        // les courbes très denses sont sous-échantillonnées
        const size_t stride = std::max<size_t>(n / 400, 1);
        for (size_t i = 0; i < n; i += stride) {
            if (!xm.valid(s.xs[i]) || !m.valid(s.ys[i]))
                continue;
            const QPointF q(xm.toPx(s.xs[i]), m.toPx(s.ys[i]));
            for (size_t k = 0; k < 4; ++k) {
                if (QRectF(corners[k].second, QSizeF(w, h)).adjusted(-4, -4, 4, 4).contains(q))
                    counts[k] += stride;
            }
        }
    }
    size_t best = 0;
    for (size_t k = 1; k < 4; ++k) {
        if (counts[k] < counts[best])
            best = k;
    }
    return corners[best].first;
}

QPen curvePen(const QColor &c, double width, Dash dash)
{
    QPen pen(c, width);
    pen.setJoinStyle(Qt::RoundJoin);
    const auto pattern = dashPattern(dash);
    if (pattern.empty()) {
        pen.setCapStyle(Qt::RoundCap);
    } else {
        pen.setCapStyle(Qt::FlatCap);
        // motif en multiples de l'épaisseur ; les traits très fins gardent des tirets lisibles
        const double k = std::max(width, 0.8) / width;
        QList<qreal> d;
        for (double v : pattern)
            d << v * k;
        pen.setDashPattern(d);
    }
    return pen;
}

/// Dessine les barres (ou rectangles) d'incertitude d'une série.
void drawErrorBars(Canvas &cv, const Series &s, const AxisMap &xm, const AxisMap &ym, QRectF area, const QColor &color)
{
    if (s.errors == ErrorBars::None || s.bars)
        return;
    QPainter &p = cv.p();
    const size_t n = std::min(s.xs.size(), s.ys.size());
    const double w = std::max(s.errorWidth, 0.3);
    const QPen pen = cv.crispPen(color, w);
    QPen capPen = pen;
    capPen.setCapStyle(Qt::SquareCap);
    const double cap = cv.snapLength(s.capSize ? std::max(*s.capSize, 0.0) : std::clamp(s.markerSize * 0.5, 2.5, 6.0));
    // marqueur fermé : la barre s'arrête à son bord au lieu de le traverser
    const Extent gap = markerIsClosed(s.marker) ? markerExtent(s.marker, s.markerSize) : Extent{};
    if (s.errors == ErrorBars::Rectangles) {
        QColor fill = color;
        fill.setAlphaF(0.13);
        QColor edge = color;
        edge.setAlphaF(0.85);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        for (size_t i = 0; i < n; ++i) {
            const double x = s.xs[i], y = s.ys[i];
            if (!xm.valid(x) || !ym.valid(y))
                continue;
            const auto [uxLo, uxHi] = sidesAt(s, true, i);
            const auto [uyLo, uyHi] = sidesAt(s, false, i);
            const double x0 = x - finiteOr0(uxLo), x1 = x + finiteOr0(uxHi);
            const double y0 = y - finiteOr0(uyLo), y1 = y + finiteOr0(uyHi);
            // bornes non représentables (échelle logarithmique) : bord de la zone
            const double px0 = xm.valid(x0) ? xm.toPx(x0) : area.left(), px1 = xm.valid(x1) ? xm.toPx(x1) : area.right();
            const double py0 = ym.valid(y0) ? ym.toPx(y0) : area.bottom(), py1 = ym.valid(y1) ? ym.toPx(y1) : area.top();
            QRectF r = QRectF(QPointF(px0, py1), QPointF(px1, py0)).normalized();
            r = r.intersected(area.adjusted(-1, -1, 1, 1));
            if (r.isEmpty() && r.width() <= 0 && r.height() <= 0)
                continue;
            const QRectF rs(QPointF(cv.snap(r.left(), w), cv.snap(r.top(), w)), QPointF(cv.snap(r.right(), w), cv.snap(r.bottom(), w)));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRect(rs);
            p.setBrush(Qt::NoBrush);
            p.setPen(cv.crispPen(edge, w));
            p.drawRect(rs);
        }
        p.restore();
        return;
    }
    // Une barre : de v − uLo à v + uHi sur l'axe `m`, à la position `across` sur l'autre axe.
    // `lo` et `hi` bornent la zone (pixels croissants) ; `gapLo`, `gapHi` : bord du marqueur
    // côté pixels décroissants et croissants. Une extrémité hors de la zone, ou non
    // représentable en échelle logarithmique, est coupée et reste sans embout ; un côté sans
    // incertitude (0) n'a ni barre ni embout.
    auto bar = [&](QPainter &p, double across, double center, const AxisMap &m, double v, double uLo, double uHi, double lo, double hi,
                   double gapLo, double gapHi, bool vertical) {
        double a = m.valid(v + uHi) ? m.toPx(v + uHi) : (vertical ? lo : hi);
        double b = m.valid(v - uLo) ? m.toPx(v - uLo) : (vertical ? hi : lo);
        bool capA = m.valid(v + uHi) && uHi > 0.0, capB = m.valid(v - uLo) && uLo > 0.0;
        if (a > b) {
            std::swap(a, b);
            std::swap(capA, capB);
        }
        capA = capA && a >= lo - 0.5;
        capB = capB && b <= hi + 0.5;
        a = cv.snap(std::max(a, lo), w);
        b = cv.snap(std::min(b, hi), w);
        if (a >= b)
            return;
        auto line = [&](double p0, double q0, double p1, double q1) {
            if (vertical)
                p.drawLine(QPointF(q0, p0), QPointF(q1, p1));
            else
                p.drawLine(QPointF(p0, q0), QPointF(p1, q1));
        };
        const double g0 = center - gapLo, g1 = center + gapHi;
        if (a < g0 - 0.25)
            line(a, across, std::min(g0, b), across);
        if (b > g1 + 0.25)
            line(std::max(g1, a), across, b, across);
        if (cap > 0.0) {
            // embouts à bouts carrés : 2·cap + 1 pixels, centrés sur la barre
            p.setPen(capPen);
            if (capA && a < g0 - 0.5)
                line(a, across - cap, a, across + cap);
            if (capB && b > g1 + 0.5)
                line(b, across - cap, b, across + cap);
            p.setPen(pen);
        }
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(pen);
    for (size_t i = 0; i < n; ++i) {
        const double x = s.xs[i], y = s.ys[i];
        if (!xm.valid(x) || !ym.valid(y))
            continue;
        const double cx = cv.snap(xm.toPx(x), w), cy = cv.snap(ym.toPx(y), w);
        const auto [uyLo, uyHi] = sidesAt(s, false, i);
        const auto [uxLo, uxHi] = s.errors == ErrorBars::Vertical ? std::pair<double, double>(NAN, NAN) : sidesAt(s, true, i);
        // barre verticale (le long de y) puis horizontale (le long de x)
        if (finiteOr0(uyLo) + finiteOr0(uyHi) > 0.0 && cx >= area.left() - 0.5 && cx <= area.right() + 0.5)
            bar(p, cx, cy, ym, y, finiteOr0(uyLo), finiteOr0(uyHi), area.top(), area.bottom(), gap.up, gap.down, true);
        if (finiteOr0(uxLo) + finiteOr0(uxHi) > 0.0 && cy >= area.top() - 0.5 && cy <= area.bottom() + 0.5)
            bar(p, cy, cx, xm, x, finiteOr0(uxLo), finiteOr0(uxHi), area.left(), area.right(), gap.left, gap.right, false);
    }
    p.restore();
}

void drawHistogram(Canvas &cv, const Series &s, const AxisMap &xm, const AxisMap &ym, QRectF area, const QColor &color)
{
    QPainter &p = cv.p();
    const double w = *s.bars;
    QColor fill = color;
    fill = QColor::fromRgbF(fill.redF() + (1 - fill.redF()) * 0.45, fill.greenF() + (1 - fill.greenF()) * 0.45, fill.blueF() + (1 - fill.blueF()) * 0.45);
    if (cv.opts().theme.dark)
        fill = QColor(color.red(), color.green(), color.blue(), 110);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    for (size_t i = 0; i < std::min(s.xs.size(), s.ys.size()); ++i) {
        if (!xm.valid(s.xs[i]) || !ym.valid(s.ys[i]))
            continue;
        const double x0 = xm.toPx(s.xs[i] - w / 2.0), x1 = xm.toPx(s.xs[i] + w / 2.0);
        const double base = ym.valid(0.0) ? ym.toPx(0.0) : area.bottom();
        const double y1 = ym.toPx(s.ys[i]);
        QRectF r = QRectF(QPointF(x0, std::min(base, y1)), QPointF(x1, std::max(base, y1))).intersected(area.adjusted(-1, -1, 1, 1));
        if (r.width() <= 0)
            continue;
        r = QRectF(QPointF(cv.snap(r.left(), 1), cv.snap(r.top(), 1)), QPointF(cv.snap(r.right(), 1), cv.snap(r.bottom(), 1)));
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRect(r);
        p.setBrush(Qt::NoBrush);
        p.setPen(cv.crispPen(color, 1.0));
        p.drawRect(r);
    }
    p.restore();
}

void drawLine(Canvas &cv, const Series &s, const AxisMap &xm, const AxisMap &ym, QRectF area, const QColor &color, double widthFactor)
{
    if (s.line == LineKind::None || s.lineWidth <= 0.0 || s.bars)
        return;
    if (s.function) {
        // fonction (modèle) : calculée sur les abscisses affichées, zoom compris, limitées à son
        // domaine ; points régulièrement espacés à l'écran (juste aussi en échelle logarithmique)
        double lo = xm.fromPx(area.left()), hi = xm.fromPx(area.right());
        if (lo > hi)
            std::swap(lo, hi);
        if (s.domain) {
            lo = std::max(lo, s.domain->first);
            hi = std::min(hi, s.domain->second);
        }
        if (!(hi > lo) || !xm.valid(lo) || !xm.valid(hi))
            return;
        const int count = std::max(64, static_cast<int>(area.width() / 2.0));
        const double p0 = xm.toPx(lo), p1 = xm.toPx(hi);
        std::vector<double> xs(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            xs[static_cast<size_t>(i)] = i == 0 ? lo : i == count - 1 ? hi : xm.fromPx(p0 + (p1 - p0) * i / (count - 1));
        Series sampled = s;
        sampled.function = nullptr;
        sampled.ys = s.function(xs);
        sampled.xs = std::move(xs);
        drawLine(cv, sampled, xm, ym, area, color, widthFactor);
        return;
    }
    QPainter &p = cv.p();
    const size_t n = std::min(s.xs.size(), s.ys.size());
    auto ok = [&](size_t i) { return xm.valid(s.xs[i]) && ym.valid(s.ys[i]); };
    std::vector<std::vector<std::pair<double, double>>> pieces;
    if (s.line == LineKind::Straight) {
        // les cases vides coupent la courbe
        pieces.emplace_back();
        for (size_t i = 0; i < n; ++i) {
            if (ok(i))
                pieces.back().emplace_back(s.xs[i], s.ys[i]);
            else if (!pieces.back().empty())
                pieces.emplace_back();
        }
    } else {
        std::vector<double> xs, ys;
        for (size_t i = 0; i < n; ++i) {
            if (ok(i)) {
                xs.push_back(s.xs[i]);
                ys.push_back(s.ys[i]);
            }
        }
        const int per = xs.size() > 3000 ? 2 : xs.size() > 500 ? 4 : 12;
        pieces.push_back(spline::smoothCurve(xs, ys, s.line == LineKind::Cubic ? spline::Kind::Cubic : spline::Kind::Monotone, per));
    }
    const double width = s.lineWidth * widthFactor;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(curvePen(color, width, s.dash));
    p.setBrush(Qt::NoBrush);
    const QRectF r = area.adjusted(-width - 2, -width - 2, width + 2, width + 2);
    for (const auto &piece : pieces) {
        for (const QPolygonF &poly : clipPolyline(piece, xm, ym, r))
            p.drawPolyline(poly);
    }
    p.restore();
}

void drawMarkers(Canvas &cv, const Series &s, const AxisMap &xm, const AxisMap &ym, QRectF area, const QColor &color, const QColor &fill)
{
    if (s.marker == Marker::None || s.bars)
        return;
    const QRectF bounds = area.adjusted(-s.markerSize - 2, -s.markerSize - 2, s.markerSize + 2, s.markerSize + 2);
    const double w = s.errors != ErrorBars::None ? std::max(s.errorWidth, 0.3) : 1.0;
    for (size_t i = 0; i < std::min(s.xs.size(), s.ys.size()); ++i) {
        if (!xm.valid(s.xs[i]) || !ym.valid(s.ys[i]))
            continue;
        // centre aligné comme les barres d'incertitude, pour qu'elles passent exactement par lui
        const QPointF c(cv.snap(xm.toPx(s.xs[i]), w), cv.snap(ym.toPx(s.ys[i]), w));
        if (bounds.contains(c))
            drawMarker(cv.p(), s.marker, c, s.markerSize, color, fill);
    }
}

void drawLegendSample(Canvas &cv, const Series &s, QPointF origin, double cy, const QColor &color, const QColor &fill)
{
    QPainter &p = cv.p();
    const double x0 = origin.x();
    const double mid = x0 + LEGEND_SAMPLE / 2.0;
    if (s.bars) {
        QColor f = color;
        f.setAlpha(cv.opts().theme.dark ? 110 : 255);
        if (!cv.opts().theme.dark)
            f = QColor::fromRgbF(f.redF() + (1 - f.redF()) * 0.45, f.greenF() + (1 - f.greenF()) * 0.45, f.blueF() + (1 - f.blueF()) * 0.45);
        const QRectF r(cv.snap(x0 + 5, 1), cv.snap(cy - 5, 1), 20, 10);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(cv.crispPen(color, 1.0));
        p.setBrush(f);
        p.drawRect(r);
        p.restore();
        return;
    }
    // barre d'incertitude miniature derrière le marqueur
    if (s.errors != ErrorBars::None) {
        const double w = std::max(s.errorWidth, 0.3);
        const double sx = cv.snap(mid, w), sy = cv.snap(cy, w);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        if (s.errors == ErrorBars::Rectangles) {
            QColor f = color;
            f.setAlphaF(0.13);
            QColor e = color;
            e.setAlphaF(0.85);
            const QRectF r(QPointF(cv.snap(mid - 8, w), cv.snap(cy - 5.5, w)), QPointF(cv.snap(mid + 8, w), cv.snap(cy + 5.5, w)));
            p.setPen(Qt::NoPen);
            p.setBrush(f);
            p.drawRect(r);
            p.setBrush(Qt::NoBrush);
            p.setPen(cv.crispPen(e, w));
            p.drawRect(r);
        } else {
            const double cap = s.capSize ? std::max(*s.capSize, 0.0) : std::clamp(s.markerSize * 0.5, 2.5, 6.0);
            const double half = 6.5;
            p.setPen(cv.crispPen(color, w));
            p.drawLine(QPointF(sx, cv.snap(cy - half, w)), QPointF(sx, cv.snap(cy + half, w)));
            if (cap > 0) {
                const double c = cv.snapLength(std::min(cap, 4.0));
                p.drawLine(QPointF(sx - c, cv.snap(cy - half, w)), QPointF(sx + c, cv.snap(cy - half, w)));
                p.drawLine(QPointF(sx - c, cv.snap(cy + half, w)), QPointF(sx + c, cv.snap(cy + half, w)));
            }
            if (s.errors == ErrorBars::Both) {
                const double hw = 9.0;
                p.drawLine(QPointF(cv.snap(mid - hw, w), sy), QPointF(cv.snap(mid + hw, w), sy));
                if (cap > 0) {
                    const double c = cv.snapLength(std::min(cap, 4.0));
                    p.drawLine(QPointF(cv.snap(mid - hw, w), sy - c), QPointF(cv.snap(mid - hw, w), sy + c));
                    p.drawLine(QPointF(cv.snap(mid + hw, w), sy - c), QPointF(cv.snap(mid + hw, w), sy + c));
                }
            }
        }
        p.restore();
    }
    if (s.line != LineKind::None) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(curvePen(color, s.lineWidth, s.dash));
        p.drawLine(QPointF(x0, cy), QPointF(x0 + LEGEND_SAMPLE, cy));
        p.restore();
    }
    if (s.marker != Marker::None) {
        const double w = s.errors != ErrorBars::None ? std::max(s.errorWidth, 0.3) : 1.0;
        drawMarker(p, s.marker, QPointF(cv.snap(mid, w), cv.snap(cy, w)), std::min(s.markerSize, 11.0), color, fill);
    }
}

void drawLegend(Canvas &cv, const std::vector<const Series *> &entries, QRectF r, double size)
{
    QPainter &p = cv.p();
    const PlotTheme &th = cv.opts().theme;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen border(th.legendBorder, cv.snapWidth(1.0));
    p.setPen(border);
    p.setBrush(th.legendFill);
    const QRectF rs(QPointF(cv.snap(r.left(), 1), cv.snap(r.top(), 1)), QPointF(cv.snap(r.right(), 1), cv.snap(r.bottom(), 1)));
    p.drawRoundedRect(rs, 3.0, 3.0);
    p.restore();
    const double row = size * 1.6;
    for (size_t k = 0; k < entries.size(); ++k) {
        const Series &s = *entries[k];
        const double cy = r.top() + 4.0 + row * (k + 0.5);
        const QColor color = th.adapt(s.color);
        drawLegendSample(cv, s, QPointF(r.left() + 8.0, 0), cy, color, th.background);
        cv.text(r.left() + 8.0 + LEGEND_SAMPLE + 7.0, cy, *s.legend, size, false, th.text, HAlign::Left, VAlign::Center);
    }
}

QRectF drawYAxis(Canvas &cv, const AxisSpec &spec, const AxisMap &m, const ticks::Ticks &t, const QString &title, QRectF area, QRectF cell, bool left)
{
    const PlotTheme &th = cv.opts().theme;
    const Metrics &mt = cv.metrics();
    const double x = left ? area.left() : area.right();
    const double dir = left ? -1.0 : 1.0;
    const QPen pen = cv.crispPen(th.frame, 1.0);
    const double xs = cv.snap(x, 1.0);
    QRectF rect = QRectF(QPointF(std::min(x, x + dir * TICK_LEN), area.top()), QPointF(std::max(x, x + dir * TICK_LEN), area.bottom()));
    for (double v : t.minor) {
        if (m.valid(v)) {
            const double py = m.toPx(v);
            if (py >= area.top() - 0.5 && py <= area.bottom() + 0.5)
                cv.hline(xs, xs + dir * MINOR_LEN, py, pen);
        }
    }
    const double lx = x + dir * (TICK_LEN + 4.0);
    for (size_t k = 0; k < t.major.size(); ++k) {
        const double v = t.major[k];
        if (!m.valid(v))
            continue;
        const double py = m.toPx(v);
        if (py < area.top() - 0.5 || py > area.bottom() + 0.5)
            continue;
        cv.hline(xs, xs + dir * TICK_LEN, py, pen);
        rect = unite(rect, cv.text(lx, py, t.labels.value(static_cast<qsizetype>(k)), spec.tickSize, false, th.text, left ? HAlign::Right : HAlign::Left, VAlign::Center));
    }
    if (!title.isEmpty()) {
        const double cy = (area.top() + area.bottom()) / 2.0;
        // texte vertical, lu de bas en haut, collé au bord de la cellule
        const double tx = left ? cell.left() + PAD + mt.ascent(spec.labelSize) : cell.right() - PAD - mt.descent(spec.labelSize);
        rect = unite(rect, cv.text(tx, cy, title, spec.labelSize, false, th.text, HAlign::Center, VAlign::Baseline, true));
    }
    return rect;
}

PlotLayout drawPlot(Canvas &cv, int index, const Plot &plot, const PlotData &data, const Prepared &pr, QRectF cell, QRectF area, int plotIndexForHighlight)
{
    QPainter &p = cv.p();
    const PlotTheme &th = cv.opts().theme;
    const Metrics &mt = cv.metrics();
    PlotLayout L;
    L.index = index;
    L.cell = cell;
    L.area = area;
    L.x = AxisMap{pr.xRange.first, pr.xRange.second, plot.x.log, area.left(), area.right()};
    L.y = AxisMap{pr.yRange.first, pr.yRange.second, plot.y.log, area.bottom(), area.top()};
    if (pr.y2Range)
        L.y2 = AxisMap{pr.y2Range->first, pr.y2Range->second, plot.y2.log, area.bottom(), area.top()};
    const AxisMap &xm = L.x;
    auto ymap = [&](YSide side) -> const AxisMap & { return (side == YSide::Right && L.y2) ? *L.y2 : L.y; };

    // quadrillage
    auto grid = [&](const ticks::Ticks &t, const AxisSpec &spec, const AxisMap &m, bool vertical) {
        auto draw = [&](const std::vector<double> &vals, const QColor &c) {
            const QPen pen = cv.crispPen(c, 0.8);
            for (double v : vals) {
                if (!m.valid(v))
                    continue;
                const double px = m.toPx(v);
                if (vertical) {
                    if (px > area.left() + 0.5 && px < area.right() - 0.5)
                        cv.vline(px, area.top(), area.bottom(), pen);
                } else if (px > area.top() + 0.5 && px < area.bottom() - 0.5) {
                    cv.hline(area.left(), area.right(), px, pen);
                }
            }
        };
        if (spec.gridMinor)
            draw(t.minor, th.gridMinor);
        if (spec.gridMajor)
            draw(t.major, th.grid);
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    grid(pr.xTicks, plot.x, xm, true);
    grid(pr.yTicks, plot.y, L.y, false);
    if (pr.y2Ticks && L.y2)
        grid(*pr.y2Ticks, plot.y2, *L.y2, false);
    p.restore();

    // données : histogrammes, puis incertitudes, puis traits, puis marqueurs, pour qu'aucune
    // courbe ne cache les points d'une autre
    p.save();
    p.setClipRect(area.adjusted(-0.5, -0.5, 0.5, 0.5));
    for (const auto &s : data.series) {
        if (s.bars)
            drawHistogram(cv, s, xm, ymap(s.side), area, th.adapt(s.color));
    }
    for (const auto &s : data.series)
        drawErrorBars(cv, s, xm, ymap(s.side), area, th.adapt(s.color));
    auto highlighted = [&](const Series &s) {
        const auto &h = cv.opts().highlight;
        return h && h->first == plotIndexForHighlight && h->second == s.curve && s.curve >= 0;
    };
    for (const auto &s : data.series)
        drawLine(cv, s, xm, ymap(s.side), area, th.adapt(s.color), highlighted(s) ? 1.8 : 1.0);
    // droites de référence et tangentes
    for (const auto &o : data.overlays) {
        const AxisMap &m = ymap(o.side);
        const QColor col = th.adapt(o.color);
        const QPen pen = curvePen(col, o.width, o.dash);
        switch (o.kind) {
        case Overlay::VLine:
            if (xm.valid(o.a)) {
                QPen pn = pen;
                pn.setWidthF(cv.snapWidth(o.width));
                const double x = cv.snap(xm.toPx(o.a), pn.widthF());
                p.setPen(pn);
                p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            }
            break;
        case Overlay::HLine:
            if (m.valid(o.a)) {
                QPen pn = pen;
                pn.setWidthF(cv.snapWidth(o.width));
                const double y = cv.snap(m.toPx(o.a), pn.widthF());
                p.setPen(pn);
                p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            }
            break;
        case Overlay::Line: {
            // échantillonnage le long de l'axe x (correct aussi en échelle log)
            std::vector<std::pair<double, double>> pts;
            for (int k = 0; k <= 64; ++k) {
                const double x = xm.fromPx(area.left() + area.width() * k / 64.0);
                pts.emplace_back(x, o.a * x + o.b);
            }
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(pen);
            for (const QPolygonF &poly : clipPolyline(pts, xm, m, area.adjusted(-2, -2, 2, 2)))
                p.drawPolyline(poly);
            p.restore();
            break;
        }
        }
    }
    for (const auto &s : data.series)
        drawMarkers(cv, s, xm, ymap(s.side), area, th.adapt(s.color), th.background);
    p.restore();

    // cadre, graduations et nombres
    const QPen frame = cv.crispPen(th.frame, 1.0);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(frame);
    p.setBrush(Qt::NoBrush);
    const QRectF fr(QPointF(cv.snap(area.left(), 1), cv.snap(area.top(), 1)), QPointF(cv.snap(area.right(), 1), cv.snap(area.bottom(), 1)));
    p.drawRect(fr);
    // axe x
    QRectF xAxis(QPointF(area.left(), area.bottom()), QPointF(area.right(), area.bottom() + TICK_LEN));
    const double yb = fr.bottom();
    for (double v : pr.xTicks.minor) {
        if (xm.valid(v)) {
            const double px = xm.toPx(v);
            if (px >= area.left() - 0.5 && px <= area.right() + 0.5)
                cv.vline(px, yb, yb + MINOR_LEN, frame);
        }
    }
    const double labelY = area.bottom() + TICK_LEN + 4.0;
    for (size_t k = 0; k < pr.xTicks.major.size(); ++k) {
        const double v = pr.xTicks.major[k];
        if (!xm.valid(v))
            continue;
        const double px = xm.toPx(v);
        if (px < area.left() - 0.5 || px > area.right() + 0.5)
            continue;
        cv.vline(px, yb, yb + TICK_LEN, frame);
        xAxis = unite(xAxis, cv.text(px, labelY, pr.xTicks.labels.value(static_cast<qsizetype>(k)), plot.x.tickSize, false, th.text, HAlign::Center, VAlign::Top));
    }
    if (!pr.xTitle.isEmpty()) {
        const double ty = cell.bottom() - PAD - mt.descent(plot.x.labelSize);
        xAxis = unite(xAxis, cv.text((area.left() + area.right()) / 2.0, ty, pr.xTitle, plot.x.labelSize, false, th.text, HAlign::Center, VAlign::Baseline));
    }
    L.xAxis = xAxis;
    L.yAxis = drawYAxis(cv, plot.y, L.y, pr.yTicks, pr.yTitle, area, cell, true);
    if (L.y2 && pr.y2Ticks)
        L.y2Axis = drawYAxis(cv, plot.y2, *L.y2, *pr.y2Ticks, pr.y2Title, area, cell, false);
    p.restore();

    // titre
    if (plot.title.visible && !plot.title.text.isEmpty()) {
        const double size = plot.title.size;
        const double ty = cell.top() + PAD + mt.ascent(size);
        double tx = plot.title.align == HAlign::Left ? area.left() : plot.title.align == HAlign::Right ? area.right() : (area.left() + area.right()) / 2.0;
        // graphe étroit (réglages ouverts, interface agrandie) : titre raccourci « … » et gardé dans
        // la cellule plutôt que coupé à ses bords
        QString title = plot.title.text;
        const double room = std::max(cell.width() - 2.0 * PAD, 1.0);
        if (mt.width(title, size, plot.title.bold) > room)
            title = mt.fm(size, plot.title.bold).elidedText(title, Qt::ElideRight, room);
        if (plot.title.align == HAlign::Center) {
            const double half = mt.width(title, size, plot.title.bold) / 2.0;
            tx = std::clamp(tx, cell.left() + PAD + half, std::max(cell.left() + PAD + half, cell.right() - PAD - half));
        }
        L.title = cv.text(tx, ty, title, size, plot.title.bold, th.text, plot.title.align, VAlign::Baseline);
    }

    // textes attachés aux données
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto &l : data.labels) {
        const AxisMap &m = ymap(l.side);
        // ordonnée non renseignée : texte en haut de la zone (droite verticale)
        const bool top = std::isnan(l.y);
        if (!xm.valid(l.x) || (!top && !m.valid(l.y)))
            continue;
        const double px = xm.toPx(l.x);
        const double py = top ? area.top() + l.size + 2.0 : m.toPx(l.y);
        if (!area.adjusted(-1, -1, 1, 1).contains(QPointF(px, py)))
            continue;
        const QColor col = th.adapt(l.color);
        if (l.dot) {
            p.setPen(QPen(th.background, 1.4));
            p.setBrush(col);
            p.drawEllipse(QPointF(px, py), 3.6, 3.6);
            p.setBrush(Qt::NoBrush);
        }
        if (!l.text.isEmpty()) {
            const double w = mt.width(l.text, l.size);
            // le texte reste dans la zone de tracé
            double tx = px + l.dx;
            if (tx + w > area.right() - 2.0)
                tx = px - std::abs(l.dx) - w;
            const double ty = std::clamp(py + l.dy, area.top() + l.size, area.bottom() - 4.0);
            const QRectF r(tx - 3.0, ty - mt.ascent(l.size), w + 6.0, mt.ascent(l.size) + mt.descent(l.size));
            p.setPen(Qt::NoPen);
            p.setBrush(th.labelFill);
            p.drawRoundedRect(r, 2.0, 2.0);
            p.setBrush(Qt::NoBrush);
            cv.text(tx, ty, l.text, l.size, false, col, HAlign::Left, VAlign::Baseline);
        }
    }
    p.restore();

    // légende
    if (pr.legendSize) {
        const auto [w, h] = *pr.legendSize;
        const double inset = 8.0;
        LegendPos pos = plot.legend.pos;
        if (pos == LegendPos::Auto)
            pos = bestLegendCorner(data, xm, L.y, L.y2, area, w, h, inset);
        QPointF o;
        switch (pos) {
        case LegendPos::TopLeft: o = QPointF(area.left() + inset, area.top() + inset); break;
        case LegendPos::BottomRight: o = QPointF(area.right() - inset - w, area.bottom() - inset - h); break;
        case LegendPos::BottomLeft: o = QPointF(area.left() + inset, area.bottom() - inset - h); break;
        case LegendPos::Outside: o = QPointF((L.y2Axis ? L.y2Axis->right() : area.right()) + 10.0, area.top()); break;
        default: o = QPointF(area.right() - inset - w, area.top() + inset); break;
        }
        const QRectF r(o, QSizeF(w, h));
        drawLegend(cv, legendEntries(data), r, plot.legend.size);
        L.legend = r;
    }
    return L;
}

} // namespace

std::vector<PlotLayout> renderGraph(QPainter &p, const GraphView &view, const std::vector<PlotData> &data, QSizeF size, const RenderOptions &opts)
{
    Canvas cv(p, opts);
    const double width = size.width(), height = size.height();
    p.save();
    // police des graphes dès le départ : rien ne dépend de celle de l'interface (agrandissement),
    // pas même les attributs par défaut d'un SVG
    p.setFont(cv.metrics().font(12.0, false));
    p.fillRect(QRectF(QPointF(0, 0), size), opts.theme.background);
    const int n = std::min(view.visiblePlots(), static_cast<int>(data.size()));
    const int rows = std::max(view.rows, 1), cols = std::max(view.cols, 1);
    const double gap = 6.0;
    const double cw = std::max((width - gap * (cols - 1)) / cols, 10.0);
    const double ch = std::max((height - gap * (rows - 1)) / rows, 10.0);
    auto cell = [&](int i) {
        const int r = i / cols, c = i % cols;
        return QRectF(c * (cw + gap), r * (ch + gap), cw, ch);
    };
    // axes des abscisses liés : bornes communes
    Range xOverride;
    if (view.linkX && n > 1) {
        Range r;
        for (int i = 0; i < n; ++i) {
            if (auto e = dataExtent(data[i], true, YSide::Left, view.plots[i].x.log, std::nullopt)) {
                extend(r, e->first, view.plots[i].x.log);
                extend(r, e->second, view.plots[i].x.log);
            }
        }
        // le premier graphe impose ses bornes s'il a été zoomé
        xOverride = view.plots[0].x.autoScale ? axisRange(view.plots[0].x, r) : axisRange(view.plots[0].x, std::nullopt);
    }
    std::vector<Prepared> prepared;
    for (int i = 0; i < n; ++i)
        prepared.push_back(prepare(view.plots[i], data[i], cell(i), xOverride, cv.metrics(), opts.style));
    // alignement : marges communes par colonne (gauche/droite) et par ligne (haut/bas)
    std::vector<double> colL(cols, 0.0), colR(cols, 0.0), rowT(rows, 0.0), rowB(rows, 0.0);
    for (int i = 0; i < n; ++i) {
        const int r = i / cols, c = i % cols;
        colL[c] = std::max(colL[c], prepared[i].margins[0]);
        rowT[r] = std::max(rowT[r], prepared[i].margins[1]);
        colR[c] = std::max(colR[c], prepared[i].margins[2]);
        rowB[r] = std::max(rowB[r], prepared[i].margins[3]);
    }
    std::vector<PlotLayout> layouts;
    for (int i = 0; i < n; ++i) {
        const int r = i / cols, c = i % cols;
        const QRectF cl = cell(i);
        const double x0 = cl.left() + colL[c], y0 = cl.top() + rowT[r];
        const double x1 = std::max(cl.right() - colR[c], x0 + 10.0), y1 = std::max(cl.bottom() - rowB[r], y0 + 10.0);
        const QRectF area(QPointF(x0, y0), QPointF(x1, y1));
        // graduations définitives avec la vraie taille de la zone de tracé
        const Plot &plot = view.plots[i];
        Prepared &pr = prepared[i];
        pr.xTicks = computeTicks(plot.x, pr.xRange.first, pr.xRange.second, area.width(), true, opts.style);
        pr.yTicks = computeTicks(plot.y, pr.yRange.first, pr.yRange.second, area.height(), false, opts.style);
        if (pr.y2Range)
            pr.y2Ticks = computeTicks(plot.y2, pr.y2Range->first, pr.y2Range->second, area.height(), false, opts.style);
        pr.xTitle = axisTitle(plot.x, data[i].xName, data[i].xUnit, pr.xTicks.factorExp);
        pr.yTitle = axisTitle(plot.y, data[i].yName, data[i].yUnit, pr.yTicks.factorExp);
        if (pr.y2Ticks)
            pr.y2Title = axisTitle(plot.y2, data[i].y2Name, data[i].y2Unit, pr.y2Ticks->factorExp);
        layouts.push_back(drawPlot(cv, i, plot, data[i], pr, cl, area, i));
    }
    p.restore();
    return layouts;
}

PlotLayout scaledLayout(const PlotLayout &l, double s)
{
    const auto rect = [s](const QRectF &r) { return QRectF(r.topLeft() * s, r.size() * s); };
    const auto axis = [s](AxisMap m) {
        m.a *= s;
        m.b *= s;
        return m;
    };
    PlotLayout out = l;
    out.cell = rect(l.cell);
    out.area = rect(l.area);
    out.x = axis(l.x);
    out.y = axis(l.y);
    if (l.y2)
        out.y2 = axis(*l.y2);
    if (l.title)
        out.title = rect(*l.title);
    if (l.legend)
        out.legend = rect(*l.legend);
    out.xAxis = rect(l.xAxis);
    out.yAxis = rect(l.yAxis);
    if (l.y2Axis)
        out.y2Axis = rect(*l.y2Axis);
    return out;
}

std::vector<PlotLayout> renderGraphScaled(QPainter &p, const GraphView &view, const std::vector<PlotData> &data, QSizeF size, RenderOptions opts,
                                          double scale)
{
    if (!(scale > 0.0) || scale == 1.0)
        return renderGraph(p, view, data, size, opts);
    // dessin à la taille divisée, agrandi par le peintre : un pixel logique du graphe vaut `scale`
    // pixels du widget, et l'alignement des traits se fait sur les pixels de l'appareil
    p.save();
    p.scale(scale, scale);
    opts.devicePixelRatio *= scale;
    std::vector<PlotLayout> layouts = renderGraph(p, view, data, size / scale, opts);
    p.restore();
    for (PlotLayout &l : layouts)
        l = scaledLayout(l, scale);
    return layouts;
}
