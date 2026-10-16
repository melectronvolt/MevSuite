#include "core/plotspec.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QString Color::hex() const
{
    return u"#%1%2%3"_s.arg(c[0], 2, 16, QChar(u'0')).arg(c[1], 2, 16, QChar(u'0')).arg(c[2], 2, 16, QChar(u'0'));
}

Color Color::lighten(double t) const
{
    auto m = [t](std::uint8_t v) { return static_cast<std::uint8_t>(std::lround(v + (255.0 - v) * t)); };
    return rgb(m(c[0]), m(c[1]), m(c[2]), c[3]);
}

QString markerLabel(Marker m)
{
    switch (m) {
    case Marker::None: return u"Aucun"_s;
    case Marker::Point: return u"Point"_s;
    case Marker::Disc: return u"Rond plein"_s;
    case Marker::Circle: return u"Rond"_s;
    case Marker::Cross: return u"Croix ×"_s;
    case Marker::Plus: return u"Croix +"_s;
    case Marker::Square: return u"Carré"_s;
    case Marker::Triangle: return u"Triangle"_s;
    case Marker::Diamond: return u"Losange"_s;
    }
    return {};
}

bool markerIsClosed(Marker m)
{
    return m == Marker::Disc || m == Marker::Circle || m == Marker::Square || m == Marker::Triangle || m == Marker::Diamond;
}

QString lineLabel(LineKind l)
{
    switch (l) {
    case LineKind::None: return u"Aucune"_s;
    case LineKind::Straight: return u"Segments"_s;
    case LineKind::Cubic: return u"Lissée (spline)"_s;
    case LineKind::Monotone: return u"Lissée sans dépassement"_s;
    }
    return {};
}

QString dashLabel(Dash d)
{
    switch (d) {
    case Dash::Solid: return u"Continu"_s;
    case Dash::Dashed: return u"Tirets"_s;
    case Dash::Dotted: return u"Pointillés"_s;
    case Dash::DashDot: return u"Tiret-point"_s;
    }
    return {};
}

std::vector<double> dashPattern(Dash d)
{
    switch (d) {
    case Dash::Solid: return {};
    case Dash::Dashed: return {5.0, 3.0};
    case Dash::Dotted: return {1.0, 2.0};
    case Dash::DashDot: return {6.0, 2.5, 1.0, 2.5};
    }
    return {};
}

QString errorsLabel(ErrorBars e)
{
    switch (e) {
    case ErrorBars::None: return u"Aucune"_s;
    case ErrorBars::Vertical: return u"Barres verticales"_s;
    case ErrorBars::Both: return u"Barres verticales et horizontales"_s;
    case ErrorBars::Rectangles: return u"Rectangles"_s;
    }
    return {};
}

QString legendPosLabel(LegendPos p)
{
    switch (p) {
    case LegendPos::Auto: return u"Automatique"_s;
    case LegendPos::TopRight: return u"En haut à droite"_s;
    case LegendPos::TopLeft: return u"En haut à gauche"_s;
    case LegendPos::BottomRight: return u"En bas à droite"_s;
    case LegendPos::BottomLeft: return u"En bas à gauche"_s;
    case LegendPos::Outside: return u"À droite, hors du graphe"_s;
    }
    return {};
}

FitSpec FitSpec::make(const fit::ModelKind &kind, QId x, QId y, Color color)
{
    FitSpec f;
    f.kind = kind;
    f.x = x;
    f.y = y;
    f.color = color;
    f.fixed = kind.defaultFixed();
    return f;
}

QString FitSpec::settingsKey() const
{
    QString s = kind.typeName() + u'|' + QString::number(kind.degree) + u'|' + kind.expression + u'|' + QString::number(x) + u'|'
                + QString::number(y) + u'|';
    if (range)
        s += QString::number(range->first, 'g', 17) + u',' + QString::number(range->second, 'g', 17);
    for (const auto &[n, v] : fixed)
        s += u"|f" + n + u'=' + QString::number(v, 'g', 17);
    for (const auto &[n, v] : initial)
        s += u"|i" + n + u'=' + QString::number(v, 'g', 17);
    return s;
}

bool Plot::usesRightAxis() const
{
    return std::any_of(curves.begin(), curves.end(), [](const CurveSpec &c) { return c.side == YSide::Right && c.visible; });
}

Color Plot::nextColor() const
{
    std::vector<Color> used;
    for (const auto &c : curves)
        used.push_back(c.color);
    for (const auto &f : fits)
        used.push_back(f.color);
    for (const Color &c : PALETTE) {
        if (std::find(used.begin(), used.end(), c) == used.end())
            return c;
    }
    return PALETTE[used.size() % PALETTE.size()];
}

CurveSpec &Plot::addCurve(QId x, QId y)
{
    CurveSpec c;
    c.x = x;
    c.y = y;
    c.color = nextColor();
    curves.push_back(c);
    return curves.back();
}

bool Plot::uses(QId id) const
{
    return std::any_of(curves.begin(), curves.end(),
                       [&](const auto &c) { return c.x == id || c.y == id || c.errXLow == id || c.errXHigh == id || c.errYLow == id || c.errYHigh == id; })
           || std::any_of(fits.begin(), fits.end(), [&](const auto &f) { return f.x == id || f.y == id; })
           || std::any_of(tangents.begin(), tangents.end(), [&](const auto &t) { return t.x == id || t.y == id; });
}

void Plot::removeQuantity(QId id)
{
    std::erase_if(curves, [&](const auto &c) { return c.x == id || c.y == id; });
    // incertitude d'un côté : la courbe reprend celle rattachée à sa grandeur
    for (auto &c : curves)
        for (std::optional<QId> *e : {&c.errXLow, &c.errXHigh, &c.errYLow, &c.errYHigh})
            if (*e == id)
                e->reset();
    std::erase_if(fits, [&](const auto &f) { return f.x == id || f.y == id; });
    std::erase_if(tangents, [&](const auto &t) { return t.x == id || t.y == id; });
}

void Plot::removeCurve(size_t index)
{
    if (index >= curves.size())
        return;
    const QId x = curves[index].x, y = curves[index].y;
    curves.erase(curves.begin() + static_cast<std::ptrdiff_t>(index));
    // une autre courbe y(x) les garde
    if (std::any_of(curves.begin(), curves.end(), [&](const CurveSpec &c) { return c.x == x && c.y == y; }))
        return;
    std::erase_if(tangents, [&](const TangentSpec &t) { return t.x == x && t.y == y; });
    std::erase_if(fits, [&](const FitSpec &f) { return f.x == x && f.y == y; });
}

GraphView GraphView::make(ViewId id, const QString &name)
{
    GraphView g;
    g.id = id;
    g.name = name;
    g.plots.push_back(Plot{});
    return g;
}

void GraphView::setGrid(int r, int c)
{
    rows = std::clamp(r, 1, 4);
    cols = std::clamp(c, 1, 4);
    const size_t n = static_cast<size_t>(rows * cols);
    if (plots.size() < n)
        plots.resize(n);
}

int GraphView::visiblePlots() const
{
    return std::min(static_cast<int>(plots.size()), rows * cols);
}

// --- JSON ------------------------------------------------------------------------------------

namespace {

template <typename E, size_t N>
QString enumName(E v, const std::array<const char16_t *, N> &names)
{
    return QString::fromUtf16(names[static_cast<size_t>(v)]);
}

template <typename E, size_t N>
E enumFrom(const QJsonValue &j, const std::array<const char16_t *, N> &names, E def)
{
    const QString s = j.toString();
    for (size_t i = 0; i < N; ++i) {
        if (s == QString::fromUtf16(names[i]))
            return static_cast<E>(i);
    }
    return def;
}

constexpr std::array<const char16_t *, 9> MARKER_NAMES = {u"None", u"Point", u"Disc", u"Circle", u"Cross", u"Plus", u"Square", u"Triangle", u"Diamond"};
constexpr std::array<const char16_t *, 4> LINE_NAMES = {u"None", u"Straight", u"Cubic", u"Monotone"};
constexpr std::array<const char16_t *, 4> DASH_NAMES = {u"Solid", u"Dashed", u"Dotted", u"DashDot"};
constexpr std::array<const char16_t *, 4> ERROR_NAMES = {u"None", u"Vertical", u"Both", u"Rectangles"};
constexpr std::array<const char16_t *, 2> SIDE_NAMES = {u"Left", u"Right"};
constexpr std::array<const char16_t *, 3> ALIGN_NAMES = {u"Left", u"Center", u"Right"};
constexpr std::array<const char16_t *, 6> LEGEND_NAMES = {u"Auto", u"TopRight", u"TopLeft", u"BottomRight", u"BottomLeft", u"Outside"};

QJsonValue optString(const std::optional<QString> &s)
{
    return s ? QJsonValue(*s) : QJsonValue(QJsonValue::Null);
}

std::optional<QString> optStringFrom(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    return std::nullopt;
}

QJsonValue optDouble(const std::optional<double> &d)
{
    return d ? QJsonValue(*d) : QJsonValue(QJsonValue::Null);
}

std::optional<double> optDoubleFrom(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    return std::nullopt;
}

double num(const QJsonObject &o, const QString &key, double def)
{
    const QJsonValue v = o.value(key);
    return v.isDouble() ? v.toDouble() : def;
}

bool flag(const QJsonObject &o, const QString &key, bool def)
{
    const QJsonValue v = o.value(key);
    return v.isBool() ? v.toBool() : def;
}

QJsonArray pairsToJson(const std::vector<std::pair<QString, double>> &v)
{
    QJsonArray a;
    for (const auto &[n, x] : v)
        a.append(QJsonArray{n, x});
    return a;
}

std::vector<std::pair<QString, double>> pairsFromJson(const QJsonValue &v)
{
    std::vector<std::pair<QString, double>> out;
    for (const QJsonValue p : v.toArray()) {
        const QJsonArray a = p.toArray();
        if (a.size() == 2)
            out.emplace_back(a[0].toString(), a[1].toDouble());
    }
    return out;
}

QJsonObject titleToJson(const TitleSpec &t)
{
    return {{u"text"_s, t.text}, {u"visible"_s, t.visible}, {u"size"_s, t.size}, {u"align"_s, enumName(t.align, ALIGN_NAMES)}, {u"bold"_s, t.bold}};
}

TitleSpec titleFromJson(const QJsonObject &o)
{
    TitleSpec t;
    t.text = o.value(u"text"_s).toString();
    t.visible = flag(o, u"visible"_s, t.visible);
    t.size = num(o, u"size"_s, t.size);
    t.align = enumFrom(o.value(u"align"_s), ALIGN_NAMES, t.align);
    t.bold = flag(o, u"bold"_s, t.bold);
    return t;
}

QJsonValue formatToJson(const NumFormat &f)
{
    switch (f.kind) {
    case NumFormat::Auto: return u"Auto"_s;
    case NumFormat::Fixed: return QJsonObject{{u"Fixed"_s, QJsonObject{{u"decimals"_s, f.decimals}}}};
    case NumFormat::Scientific: return QJsonObject{{u"Scientific"_s, QJsonObject{{u"decimals"_s, f.decimals}}}};
    }
    return u"Auto"_s;
}

NumFormat formatFromJson(const QJsonValue &v)
{
    NumFormat f;
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(u"Fixed"_s)) {
            f.kind = NumFormat::Fixed;
            f.decimals = o.value(u"Fixed"_s).toObject().value(u"decimals"_s).toInt(1);
        } else if (o.contains(u"Scientific"_s)) {
            f.kind = NumFormat::Scientific;
            f.decimals = o.value(u"Scientific"_s).toObject().value(u"decimals"_s).toInt(1);
        }
    }
    return f;
}

QJsonObject axisToJson(const AxisSpec &a)
{
    return {
        {u"label"_s, optString(a.label)},
        {u"unit"_s, optString(a.unit)},
        {u"auto"_s, a.autoScale},
        {u"min"_s, a.min},
        {u"max"_s, a.max},
        {u"log"_s, a.log},
        {u"include_zero"_s, a.includeZero},
        {u"major_step"_s, optDouble(a.majorStep)},
        {u"minor_divisions"_s, a.minorDivisions ? QJsonValue(*a.minorDivisions) : QJsonValue(QJsonValue::Null)},
        {u"grid_major"_s, a.gridMajor},
        {u"grid_minor"_s, a.gridMinor},
        {u"format"_s, formatToJson(a.format)},
        {u"label_size"_s, a.labelSize},
        {u"tick_size"_s, a.tickSize},
    };
}

AxisSpec axisFromJson(const QJsonObject &o, AxisSpec a)
{
    a.label = optStringFrom(o.value(u"label"_s));
    a.unit = optStringFrom(o.value(u"unit"_s));
    a.autoScale = flag(o, u"auto"_s, a.autoScale);
    a.min = num(o, u"min"_s, a.min);
    a.max = num(o, u"max"_s, a.max);
    a.log = flag(o, u"log"_s, a.log);
    a.includeZero = flag(o, u"include_zero"_s, a.includeZero);
    a.majorStep = optDoubleFrom(o.value(u"major_step"_s));
    if (o.value(u"minor_divisions"_s).isDouble())
        a.minorDivisions = o.value(u"minor_divisions"_s).toInt();
    else
        a.minorDivisions.reset();
    a.gridMajor = flag(o, u"grid_major"_s, a.gridMajor);
    a.gridMinor = flag(o, u"grid_minor"_s, a.gridMinor);
    a.format = formatFromJson(o.value(u"format"_s));
    a.labelSize = num(o, u"label_size"_s, a.labelSize);
    a.tickSize = num(o, u"tick_size"_s, a.tickSize);
    return a;
}

QJsonValue optId(std::optional<QId> id)
{
    return id ? QJsonValue(static_cast<qint64>(*id)) : QJsonValue(QJsonValue::Null);
}

std::optional<QId> optIdFrom(const QJsonValue &v)
{
    if (!v.isDouble() || v.toInteger() <= 0)
        return std::nullopt;
    return static_cast<QId>(v.toInteger());
}

QJsonObject curveToJson(const CurveSpec &c)
{
    return {
        {u"x"_s, static_cast<qint64>(c.x)},
        {u"y"_s, static_cast<qint64>(c.y)},
        {u"side"_s, enumName(c.side, SIDE_NAMES)},
        {u"color"_s, colorToJson(c.color)},
        {u"marker"_s, enumName(c.marker, MARKER_NAMES)},
        {u"marker_size"_s, c.markerSize},
        {u"line"_s, enumName(c.line, LINE_NAMES)},
        {u"line_width"_s, c.lineWidth},
        {u"dash"_s, enumName(c.dash, DASH_NAMES)},
        {u"errors"_s, enumName(c.errors, ERROR_NAMES)},
        {u"error_width"_s, c.errorWidth},
        {u"cap_size"_s, optDouble(c.capSize)},
        {u"label"_s, optString(c.label)},
        {u"in_legend"_s, c.inLegend},
        {u"visible"_s, c.visible},
        {u"bar_width"_s, optDouble(c.barWidth)},
        {u"error_x_low"_s, optId(c.errXLow)},
        {u"error_x_high"_s, optId(c.errXHigh)},
        {u"error_y_low"_s, optId(c.errYLow)},
        {u"error_y_high"_s, optId(c.errYHigh)},
    };
}

CurveSpec curveFromJson(const QJsonObject &o)
{
    CurveSpec c;
    c.x = static_cast<QId>(o.value(u"x"_s).toInteger());
    c.y = static_cast<QId>(o.value(u"y"_s).toInteger());
    c.side = enumFrom(o.value(u"side"_s), SIDE_NAMES, c.side);
    c.color = colorFromJson(o.value(u"color"_s), c.color);
    c.marker = enumFrom(o.value(u"marker"_s), MARKER_NAMES, c.marker);
    c.markerSize = num(o, u"marker_size"_s, c.markerSize);
    c.line = enumFrom(o.value(u"line"_s), LINE_NAMES, c.line);
    c.lineWidth = num(o, u"line_width"_s, c.lineWidth);
    c.dash = enumFrom(o.value(u"dash"_s), DASH_NAMES, c.dash);
    c.errors = enumFrom(o.value(u"errors"_s), ERROR_NAMES, c.errors);
    c.errorWidth = num(o, u"error_width"_s, c.errorWidth);
    c.capSize = optDoubleFrom(o.value(u"cap_size"_s));
    c.label = optStringFrom(o.value(u"label"_s));
    c.inLegend = flag(o, u"in_legend"_s, c.inLegend);
    c.visible = flag(o, u"visible"_s, c.visible);
    c.barWidth = optDoubleFrom(o.value(u"bar_width"_s));
    c.errXLow = optIdFrom(o.value(u"error_x_low"_s));
    c.errXHigh = optIdFrom(o.value(u"error_x_high"_s));
    c.errYLow = optIdFrom(o.value(u"error_y_low"_s));
    c.errYHigh = optIdFrom(o.value(u"error_y_high"_s));
    return c;
}

QJsonObject annotationToJson(const Annotation &a)
{
    QJsonObject kind;
    switch (a.kind) {
    case Annotation::VLine: kind = {{u"type"_s, u"VLine"_s}, {u"x"_s, a.x}}; break;
    case Annotation::HLine: kind = {{u"type"_s, u"HLine"_s}, {u"y"_s, a.y}}; break;
    case Annotation::Text:
        kind = {{u"type"_s, u"Text"_s}, {u"x"_s, a.x}, {u"y"_s, a.y}, {u"text"_s, a.text}, {u"size"_s, a.size}};
        break;
    case Annotation::Line: kind = {{u"type"_s, u"Line"_s}, {u"x"_s, a.x}, {u"y"_s, a.y}, {u"x2"_s, a.x2}, {u"y2"_s, a.y2}}; break;
    }
    return {{u"kind"_s, kind},
            {u"color"_s, colorToJson(a.color)},
            {u"side"_s, enumName(a.side, SIDE_NAMES)},
            {u"label"_s, a.label},
            {u"dash"_s, enumName(a.dash, DASH_NAMES)},
            {u"locked"_s, a.locked}};
}

Annotation annotationFromJson(const QJsonObject &o)
{
    Annotation a;
    const QJsonObject k = o.value(u"kind"_s).toObject();
    const QString t = k.value(u"type"_s).toString();
    a.kind = t == u"HLine" ? Annotation::HLine : t == u"Text" ? Annotation::Text : t == u"Line" ? Annotation::Line : Annotation::VLine;
    a.x = num(k, u"x"_s, 0.0);
    a.y = num(k, u"y"_s, 0.0);
    a.x2 = num(k, u"x2"_s, a.x + 1.0);
    a.y2 = num(k, u"y2"_s, a.y + 1.0);
    a.size = std::clamp(num(k, u"size"_s, 12.0), 4.0, 96.0);
    a.text = k.value(u"text"_s).toString();
    a.locked = o.value(u"locked"_s).toBool(false);
    a.color = colorFromJson(o.value(u"color"_s), a.color);
    a.side = enumFrom(o.value(u"side"_s), SIDE_NAMES, a.side);
    a.label = o.value(u"label"_s).toString();
    a.dash = enumFrom(o.value(u"dash"_s), DASH_NAMES, Dash::Dashed);
    return a;
}

QJsonObject tangentToJson(const TangentSpec &t)
{
    return {{u"x"_s, static_cast<qint64>(t.x)}, {u"y"_s, static_cast<qint64>(t.y)}, {u"contact"_s, optDouble(t.contact)},
            {u"color"_s, colorToJson(t.color)}, {u"side"_s, enumName(t.side, SIDE_NAMES)}, {u"locked"_s, t.locked}};
}

TangentSpec tangentFromJson(const QJsonObject &o)
{
    TangentSpec t;
    t.x = static_cast<QId>(o.value(u"x"_s).toInteger());
    t.y = static_cast<QId>(o.value(u"y"_s).toInteger());
    t.contact = optDoubleFrom(o.value(u"contact"_s));
    t.color = colorFromJson(o.value(u"color"_s), t.color);
    t.side = enumFrom(o.value(u"side"_s), SIDE_NAMES, t.side);
    t.locked = o.value(u"locked"_s).toBool(false);
    return t;
}

QJsonObject fitToJson(const FitSpec &f)
{
    QJsonValue range(QJsonValue::Null);
    if (f.range)
        range = QJsonArray{f.range->first, f.range->second};
    return {{u"kind"_s, fitKindToJson(f.kind)},
            {u"x"_s, static_cast<qint64>(f.x)},
            {u"y"_s, static_cast<qint64>(f.y)},
            {u"range"_s, range},
            {u"fixed"_s, pairsToJson(f.fixed)},
            {u"initial"_s, pairsToJson(f.initial)},
            {u"color"_s, colorToJson(f.color)},
            {u"visible"_s, f.visible},
            {u"side"_s, enumName(f.side, SIDE_NAMES)},
            {u"draw"_s, f.draw == FitDraw::Points ? u"points"_s : f.draw == FitDraw::Custom ? u"custom"_s : u"graph"_s},
            {u"draw_range"_s, QJsonArray{f.drawRange.first, f.drawRange.second}},
            {u"label"_s, optString(f.label)}};
}

FitSpec fitFromJson(const QJsonObject &o)
{
    FitSpec f;
    f.kind = fitKindFromJson(o.value(u"kind"_s).toObject());
    f.x = static_cast<QId>(o.value(u"x"_s).toInteger());
    f.y = static_cast<QId>(o.value(u"y"_s).toInteger());
    const QJsonArray r = o.value(u"range"_s).toArray();
    if (r.size() == 2)
        f.range = std::make_pair(r[0].toDouble(), r[1].toDouble());
    f.fixed = pairsFromJson(o.value(u"fixed"_s));
    f.initial = pairsFromJson(o.value(u"initial"_s));
    f.color = colorFromJson(o.value(u"color"_s), f.color);
    f.visible = flag(o, u"visible"_s, true);
    f.side = enumFrom(o.value(u"side"_s), SIDE_NAMES, f.side);
    const QString draw = o.value(u"draw"_s).toString();
    f.draw = draw == u"points" ? FitDraw::Points : draw == u"custom" ? FitDraw::Custom : FitDraw::Graph;
    if (const QJsonArray d = o.value(u"draw_range"_s).toArray(); d.size() == 2)
        f.drawRange = {d[0].toDouble(), d[1].toDouble()};
    f.label = optStringFrom(o.value(u"label"_s));
    return f;
}

} // namespace

QJsonValue colorToJson(const Color &c)
{
    return QJsonArray{c.c[0], c.c[1], c.c[2], c.c[3]};
}

Color colorFromJson(const QJsonValue &v, Color def)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 4)
        return def;
    return Color::rgb(static_cast<std::uint8_t>(a[0].toInt()), static_cast<std::uint8_t>(a[1].toInt()), static_cast<std::uint8_t>(a[2].toInt()),
                      static_cast<std::uint8_t>(a[3].toInt()));
}

QJsonObject fitKindToJson(const fit::ModelKind &k)
{
    QJsonObject o{{u"type"_s, k.typeName()}};
    if (k.type == fit::ModelKind::Polynomial)
        o.insert(u"degree"_s, k.degree);
    if (k.type == fit::ModelKind::User)
        o.insert(u"expression"_s, k.expression);
    return o;
}

fit::ModelKind fitKindFromJson(const QJsonObject &o)
{
    fit::ModelKind k;
    k.type = fit::ModelKind::typeFromName(o.value(u"type"_s).toString()).value_or(fit::ModelKind::Affine);
    k.degree = o.value(u"degree"_s).toInt(3);
    k.expression = o.value(u"expression"_s).toString();
    return k;
}

QJsonObject plotToJson(const Plot &p)
{
    QJsonArray curves, annotations, tangents, fits;
    for (const auto &c : p.curves)
        curves.append(curveToJson(c));
    for (const auto &a : p.annotations)
        annotations.append(annotationToJson(a));
    for (const auto &t : p.tangents)
        tangents.append(tangentToJson(t));
    for (const auto &f : p.fits)
        fits.append(fitToJson(f));
    return {{u"title"_s, titleToJson(p.title)},
            {u"x"_s, axisToJson(p.x)},
            {u"y"_s, axisToJson(p.y)},
            {u"y2"_s, axisToJson(p.y2)},
            {u"curves"_s, curves},
            {u"legend"_s, QJsonObject{{u"visible"_s, p.legend.visible}, {u"pos"_s, enumName(p.legend.pos, LEGEND_NAMES)}, {u"size"_s, p.legend.size}}},
            {u"annotations"_s, annotations},
            {u"tangents"_s, tangents},
            {u"fits"_s, fits}};
}

Plot plotFromJson(const QJsonObject &o)
{
    Plot p;
    p.title = titleFromJson(o.value(u"title"_s).toObject());
    p.x = axisFromJson(o.value(u"x"_s).toObject(), p.x);
    p.y = axisFromJson(o.value(u"y"_s).toObject(), p.y);
    p.y2 = axisFromJson(o.value(u"y2"_s).toObject(), p.y2);
    for (const QJsonValue v : o.value(u"curves"_s).toArray())
        p.curves.push_back(curveFromJson(v.toObject()));
    const QJsonObject l = o.value(u"legend"_s).toObject();
    p.legend.visible = flag(l, u"visible"_s, true);
    p.legend.pos = enumFrom(l.value(u"pos"_s), LEGEND_NAMES, LegendPos::Auto);
    p.legend.size = num(l, u"size"_s, 11.5);
    for (const QJsonValue v : o.value(u"annotations"_s).toArray())
        p.annotations.push_back(annotationFromJson(v.toObject()));
    for (const QJsonValue v : o.value(u"tangents"_s).toArray())
        p.tangents.push_back(tangentFromJson(v.toObject()));
    for (const QJsonValue v : o.value(u"fits"_s).toArray())
        p.fits.push_back(fitFromJson(v.toObject()));
    return p;
}

QJsonObject graphToJson(const GraphView &g)
{
    QJsonArray plots;
    for (const auto &p : g.plots)
        plots.append(plotToJson(p));
    QJsonObject o{{u"id"_s, static_cast<qint64>(g.id)}, {u"name"_s, g.name}, {u"rows"_s, g.rows}, {u"cols"_s, g.cols}, {u"plots"_s, plots},
                  {u"link_x"_s, g.linkX}};
    if (g.origin)
        o.insert(u"origin"_s, QJsonObject{{u"script"_s, static_cast<qint64>(g.origin->script)}, {u"figure"_s, g.origin->figure}});
    return o;
}

GraphView graphFromJson(const QJsonObject &o)
{
    GraphView g;
    g.id = static_cast<ViewId>(o.value(u"id"_s).toInteger());
    g.name = o.value(u"name"_s).toString();
    g.rows = std::clamp(o.value(u"rows"_s).toInt(1), 1, 4);
    g.cols = std::clamp(o.value(u"cols"_s).toInt(1), 1, 4);
    for (const QJsonValue v : o.value(u"plots"_s).toArray())
        g.plots.push_back(plotFromJson(v.toObject()));
    if (g.plots.empty())
        g.plots.push_back(Plot{});
    g.linkX = flag(o, u"link_x"_s, false);
    if (o.value(u"origin"_s).isObject()) {
        const QJsonObject og = o.value(u"origin"_s).toObject();
        g.origin = FigureOrigin{static_cast<std::uint64_t>(og.value(u"script"_s).toInteger()), og.value(u"figure"_s).toString()};
    }
    return g;
}
