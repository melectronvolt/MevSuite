#include "core/plotspec.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QString Color::hex() const
{
    return u"#%1%2%3"_s.arg(c[0], 2, 16, QChar(u'0')).arg(c[1], 2, 16, QChar(u'0')).arg(c[2], 2, 16, QChar(u'0'));
}

Color Color::eclaircit(double t) const
{
    auto m = [t](std::uint8_t v) { return static_cast<std::uint8_t>(std::lround(v + (255.0 - v) * t)); };
    return rgb(m(c[0]), m(c[1]), m(c[2]), c[3]);
}

QString nom_marqueur(Marker m)
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

bool marqueur_ferme(Marker m)
{
    return m == Marker::Disc || m == Marker::Circle || m == Marker::Square || m == Marker::Triangle || m == Marker::Diamond;
}

QString nom_trait(LineKind l)
{
    switch (l) {
    case LineKind::None: return u"Aucune"_s;
    case LineKind::Straight: return u"Segments"_s;
    case LineKind::Cubic: return u"Lissée (spline)"_s;
    case LineKind::Monotone: return u"Lissée sans dépassement"_s;
    }
    return {};
}

QString nom_tirets(Dash d)
{
    switch (d) {
    case Dash::Solid: return u"Continu"_s;
    case Dash::Dashed: return u"Tirets"_s;
    case Dash::Dotted: return u"Pointillés"_s;
    case Dash::DashDot: return u"Tiret-point"_s;
    }
    return {};
}

std::vector<double> motif_tirets(Dash d)
{
    switch (d) {
    case Dash::Solid: return {};
    case Dash::Dashed: return {5.0, 3.0};
    case Dash::Dotted: return {1.0, 2.0};
    case Dash::DashDot: return {6.0, 2.5, 1.0, 2.5};
    }
    return {};
}

QString nom_incertitudes(ErrorBars e)
{
    switch (e) {
    case ErrorBars::None: return u"Aucune"_s;
    case ErrorBars::Vertical: return u"Barres verticales"_s;
    case ErrorBars::Both: return u"Barres verticales et horizontales"_s;
    case ErrorBars::Rectangles: return u"Rectangles"_s;
    }
    return {};
}

QString nom_position_legende(LegendPos p)
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

FitSpec FitSpec::fabrique(const fit::ModelKind &kind, QId x, QId y, Color color)
{
    FitSpec f;
    f.kind = kind;
    f.x = x;
    f.y = y;
    f.color = color;
    f.fixed = kind.fixes_par_defaut();
    return f;
}

QString FitSpec::cle_reglages() const
{
    QString s = kind.nom_type() + u'|' + QString::number(kind.degree) + u'|' + kind.expression + u'|' + QString::number(x) + u'|'
                + QString::number(y) + u'|';
    if (range)
        s += QString::number(range->first, 'g', 17) + u',' + QString::number(range->second, 'g', 17);
    for (const auto &[n, v] : fixed)
        s += u"|f"_s + n + u'=' + QString::number(v, 'g', 17);
    for (const auto &[n, v] : initial)
        s += u"|i"_s + n + u'=' + QString::number(v, 'g', 17);
    return s;
}

bool Plot::utilise_axe_droit() const
{
    return std::any_of(curves.begin(), curves.end(), [](const CurveSpec &c) { return c.side == YSide::Right && c.visible; });
}

Color Plot::couleur_suivante() const
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

CurveSpec &Plot::ajoute_courbe(QId x, QId y)
{
    CurveSpec c;
    c.x = x;
    c.y = y;
    c.color = couleur_suivante();
    curves.push_back(c);
    return curves.back();
}

bool Plot::utilise(QId id) const
{
    return std::any_of(curves.begin(), curves.end(),
                       [&](const auto &c) { return c.x == id || c.y == id || c.dosage == id || c.errXLow == id || c.errXHigh == id || c.errYLow == id || c.errYHigh == id; })
           || std::any_of(fits.begin(), fits.end(), [&](const auto &f) { return f.x == id || f.y == id; })
           || std::any_of(tangents.begin(), tangents.end(), [&](const auto &t) { return t.x == id || t.y == id; });
}

void Plot::retire_grandeur(QId id)
{
    std::erase_if(curves, [&](const auto &c) { return c.x == id || c.y == id; });
    for (auto &c : curves)
        if (c.dosage == id)
            c.dosage.reset();
    // incertitude d'un côté : la courbe reprend celle rattachée à sa grandeur
    for (auto &c : curves)
        for (std::optional<QId> *e : {&c.errXLow, &c.errXHigh, &c.errYLow, &c.errYHigh})
            if (*e == id)
                e->reset();
    std::erase_if(fits, [&](const auto &f) { return f.x == id || f.y == id; });
    std::erase_if(tangents, [&](const auto &t) { return t.x == id || t.y == id; });
}

void Plot::retire_courbe(size_t index)
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

GraphView GraphView::fabrique(ViewId id, const QString &name)
{
    GraphView g;
    g.id = id;
    g.name = name;
    g.plots.push_back(Plot{});
    return g;
}

void GraphView::definit_grille(int r, int c)
{
    rows = std::clamp(r, 1, 4);
    cols = std::clamp(c, 1, 4);
    const size_t n = static_cast<size_t>(rows * cols);
    if (plots.size() < n)
        plots.resize(n);
}

int GraphView::traces_visibles() const
{
    return std::min(static_cast<int>(plots.size()), rows * cols);
}

// --- JSON ------------------------------------------------------------------------------------

AxisTicks graduations_depuis_json(const QJsonValue &v)
{
    if (!v.isArray()) return std::nullopt;
    std::vector<std::pair<double, QString>> ticks;
    for (const auto &entry : v.toArray()) {
        const auto pair = entry.toArray();
        if (pair.size() == 2 && pair[0].isDouble() && std::isfinite(pair[0].toDouble()) && pair[1].isString())
            ticks.emplace_back(pair[0].toDouble(), pair[1].toString());
    }
    return ticks;
}

namespace {

template <typename E, size_t N>
QString nom_enum(E v, const std::array<const char16_t *, N> &names)
{
    return QString::fromUtf16(names[static_cast<size_t>(v)]);
}

template <typename E, size_t N>
E enum_depuis(const QJsonValue &j, const std::array<const char16_t *, N> &names, E def)
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

QJsonValue texte_optionnel(const std::optional<QString> &s)
{
    return s ? QJsonValue(*s) : QJsonValue(QJsonValue::Null);
}

std::optional<QString> texte_optionnel_de(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    return std::nullopt;
}

QJsonValue reel_optionnel(const std::optional<double> &d)
{
    return d ? QJsonValue(*d) : QJsonValue(QJsonValue::Null);
}

std::optional<double> reel_optionnel_de(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    return std::nullopt;
}

double valeur_num(const QJsonObject &o, const QString &key, double def)
{
    const QJsonValue v = o.value(key);
    return v.isDouble() ? v.toDouble() : def;
}

bool drapeau(const QJsonObject &o, const QString &key, bool def)
{
    const QJsonValue v = o.value(key);
    return v.isBool() ? v.toBool() : def;
}

QJsonArray paires_en_json(const std::vector<std::pair<QString, double>> &v)
{
    QJsonArray a;
    for (const auto &[n, x] : v)
        a.append(QJsonArray{n, x});
    return a;
}

std::vector<std::pair<QString, double>> paires_depuis_json(const QJsonValue &v)
{
    std::vector<std::pair<QString, double>> out;
    for (const QJsonValue p : v.toArray()) {
        const QJsonArray a = p.toArray();
        if (a.size() == 2)
            out.emplace_back(a[0].toString(), a[1].toDouble());
    }
    return out;
}

QJsonObject titre_en_json(const TitleSpec &t)
{
    return {{u"text"_s, t.text}, {u"visible"_s, t.visible}, {u"size"_s, t.size}, {u"align"_s, nom_enum(t.align, ALIGN_NAMES)}, {u"bold"_s, t.bold}};
}

TitleSpec titre_depuis_json(const QJsonObject &o)
{
    TitleSpec t;
    t.text = o.value(u"text"_s).toString();
    t.visible = drapeau(o, u"visible"_s, t.visible);
    t.size = valeur_num(o, u"size"_s, t.size);
    t.align = enum_depuis(o.value(u"align"_s), ALIGN_NAMES, t.align);
    t.bold = drapeau(o, u"bold"_s, t.bold);
    return t;
}

QJsonValue format_en_json(const NumFormat &f)
{
    switch (f.kind) {
    case NumFormat::Auto: return u"Auto"_s;
    case NumFormat::Fixed: return QJsonObject{{u"Fixed"_s, QJsonObject{{u"decimals"_s, f.decimals}}}};
    case NumFormat::Scientific: return QJsonObject{{u"Scientific"_s, QJsonObject{{u"decimals"_s, f.decimals}}}};
    }
    return u"Auto"_s;
}

NumFormat format_depuis_json(const QJsonValue &v)
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

QJsonObject axe_en_json(const AxisSpec &a)
{
    QJsonObject o{
        {u"label"_s, texte_optionnel(a.label)},
        {u"unit"_s, texte_optionnel(a.unit)},
        {u"auto"_s, a.autoScale},
        {u"min"_s, a.min},
        {u"max"_s, a.max},
        {u"log"_s, a.log},
        {u"include_zero"_s, a.includeZero},
        {u"major_step"_s, reel_optionnel(a.majorStep)},
        {u"minor_divisions"_s, a.minorDivisions ? QJsonValue(*a.minorDivisions) : QJsonValue(QJsonValue::Null)},
        {u"grid_major"_s, a.gridMajor},
        {u"grid_minor"_s, a.gridMinor},
        {u"format"_s, format_en_json(a.format)},
        {u"label_size"_s, a.labelSize},
        {u"tick_size"_s, a.tickSize},
    };
    if (a.ticks) {
        QJsonArray points;
        for (const auto &[pos, label] : *a.ticks)
            points.append(QJsonArray{pos, label});
        o.insert(u"ticks"_s, points);
    }
    return o;
}

AxisSpec axe_depuis_json(const QJsonObject &o, AxisSpec a)
{
    a.label = texte_optionnel_de(o.value(u"label"_s));
    a.unit = texte_optionnel_de(o.value(u"unit"_s));
    a.autoScale = drapeau(o, u"auto"_s, a.autoScale);
    a.min = valeur_num(o, u"min"_s, a.min);
    a.max = valeur_num(o, u"max"_s, a.max);
    a.log = drapeau(o, u"log"_s, a.log);
    a.includeZero = drapeau(o, u"include_zero"_s, a.includeZero);
    a.ticks = graduations_depuis_json(o.value(u"ticks"_s));
    a.majorStep = reel_optionnel_de(o.value(u"major_step"_s));
    if (o.value(u"minor_divisions"_s).isDouble())
        a.minorDivisions = o.value(u"minor_divisions"_s).toInt();
    else
        a.minorDivisions.reset();
    a.gridMajor = drapeau(o, u"grid_major"_s, a.gridMajor);
    a.gridMinor = drapeau(o, u"grid_minor"_s, a.gridMinor);
    a.format = format_depuis_json(o.value(u"format"_s));
    a.labelSize = valeur_num(o, u"label_size"_s, a.labelSize);
    a.tickSize = valeur_num(o, u"tick_size"_s, a.tickSize);
    return a;
}

QJsonValue id_optionnel(std::optional<QId> id)
{
    return id ? QJsonValue(static_cast<qint64>(*id)) : QJsonValue(QJsonValue::Null);
}

std::optional<QId> id_optionnel_de(const QJsonValue &v)
{
    if (!v.isDouble() || v.toInteger() <= 0)
        return std::nullopt;
    return static_cast<QId>(v.toInteger());
}

QJsonObject courbe_en_json(const CurveSpec &c)
{
    return {
        {u"x"_s, static_cast<qint64>(c.x)},
        {u"y"_s, static_cast<qint64>(c.y)},
        {u"side"_s, nom_enum(c.side, SIDE_NAMES)},
        {u"color"_s, couleur_en_json(c.color)},
        {u"marker"_s, nom_enum(c.marker, MARKER_NAMES)},
        {u"marker_size"_s, c.markerSize},
        {u"line"_s, nom_enum(c.line, LINE_NAMES)},
        {u"line_width"_s, c.lineWidth},
        {u"dash"_s, nom_enum(c.dash, DASH_NAMES)},
        {u"errors"_s, nom_enum(c.errors, ERROR_NAMES)},
        {u"error_width"_s, c.errorWidth},
        {u"cap_size"_s, reel_optionnel(c.capSize)},
        {u"label"_s, texte_optionnel(c.label)},
        {u"in_legend"_s, c.inLegend},
        {u"visible"_s, c.visible},
        {u"bar_width"_s, reel_optionnel(c.barWidth)},
        {u"dosage"_s, id_optionnel(c.dosage)},
        {u"error_x_low"_s, id_optionnel(c.errXLow)},
        {u"error_x_high"_s, id_optionnel(c.errXHigh)},
        {u"error_y_low"_s, id_optionnel(c.errYLow)},
        {u"error_y_high"_s, id_optionnel(c.errYHigh)},
    };
}

CurveSpec courbe_depuis_json(const QJsonObject &o)
{
    CurveSpec c;
    c.x = static_cast<QId>(o.value(u"x"_s).toInteger());
    c.y = static_cast<QId>(o.value(u"y"_s).toInteger());
    c.side = enum_depuis(o.value(u"side"_s), SIDE_NAMES, c.side);
    c.color = couleur_depuis_json(o.value(u"color"_s), c.color);
    c.marker = enum_depuis(o.value(u"marker"_s), MARKER_NAMES, c.marker);
    c.markerSize = valeur_num(o, u"marker_size"_s, c.markerSize);
    c.line = enum_depuis(o.value(u"line"_s), LINE_NAMES, c.line);
    c.lineWidth = valeur_num(o, u"line_width"_s, c.lineWidth);
    c.dash = enum_depuis(o.value(u"dash"_s), DASH_NAMES, c.dash);
    c.errors = enum_depuis(o.value(u"errors"_s), ERROR_NAMES, c.errors);
    c.errorWidth = valeur_num(o, u"error_width"_s, c.errorWidth);
    c.capSize = reel_optionnel_de(o.value(u"cap_size"_s));
    c.label = texte_optionnel_de(o.value(u"label"_s));
    c.inLegend = drapeau(o, u"in_legend"_s, c.inLegend);
    c.visible = drapeau(o, u"visible"_s, c.visible);
    c.dosage = id_optionnel_de(o.value(u"dosage"_s));
    c.barWidth = reel_optionnel_de(o.value(u"bar_width"_s));
    c.errXLow = id_optionnel_de(o.value(u"error_x_low"_s));
    c.errXHigh = id_optionnel_de(o.value(u"error_x_high"_s));
    c.errYLow = id_optionnel_de(o.value(u"error_y_low"_s));
    c.errYHigh = id_optionnel_de(o.value(u"error_y_high"_s));
    return c;
}

QJsonObject annotation_en_json(const Annotation &a)
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
            {u"color"_s, couleur_en_json(a.color)},
            {u"side"_s, nom_enum(a.side, SIDE_NAMES)},
            {u"label"_s, a.label},
            {u"dash"_s, nom_enum(a.dash, DASH_NAMES)},
            {u"locked"_s, a.locked}};
}

Annotation annotation_depuis_json(const QJsonObject &o)
{
    Annotation a;
    const QJsonObject k = o.value(u"kind"_s).toObject();
    const QString t = k.value(u"type"_s).toString();
    a.kind = t == u"HLine" ? Annotation::HLine : t == u"Text" ? Annotation::Text : t == u"Line" ? Annotation::Line : Annotation::VLine;
    a.x = valeur_num(k, u"x"_s, 0.0);
    a.y = valeur_num(k, u"y"_s, 0.0);
    a.x2 = valeur_num(k, u"x2"_s, a.x + 1.0);
    a.y2 = valeur_num(k, u"y2"_s, a.y + 1.0);
    a.size = std::clamp(valeur_num(k, u"size"_s, 12.0), 4.0, 96.0);
    a.text = k.value(u"text"_s).toString();
    a.locked = o.value(u"locked"_s).toBool(false);
    a.color = couleur_depuis_json(o.value(u"color"_s), a.color);
    a.side = enum_depuis(o.value(u"side"_s), SIDE_NAMES, a.side);
    a.label = o.value(u"label"_s).toString();
    a.dash = enum_depuis(o.value(u"dash"_s), DASH_NAMES, Dash::Dashed);
    return a;
}

QJsonObject tangente_en_json(const TangentSpec &t)
{
    return {{u"x"_s, static_cast<qint64>(t.x)}, {u"y"_s, static_cast<qint64>(t.y)}, {u"contact"_s, reel_optionnel(t.contact)},
            {u"color"_s, couleur_en_json(t.color)}, {u"side"_s, nom_enum(t.side, SIDE_NAMES)}, {u"locked"_s, t.locked}};
}

TangentSpec tangente_depuis_json(const QJsonObject &o)
{
    TangentSpec t;
    t.x = static_cast<QId>(o.value(u"x"_s).toInteger());
    t.y = static_cast<QId>(o.value(u"y"_s).toInteger());
    t.contact = reel_optionnel_de(o.value(u"contact"_s));
    t.color = couleur_depuis_json(o.value(u"color"_s), t.color);
    t.side = enum_depuis(o.value(u"side"_s), SIDE_NAMES, t.side);
    t.locked = o.value(u"locked"_s).toBool(false);
    return t;
}

QJsonObject modele_en_json(const FitSpec &f)
{
    QJsonValue range(QJsonValue::Null);
    if (f.range)
        range = QJsonArray{f.range->first, f.range->second};
    return {{u"kind"_s, type_modele_en_json(f.kind)},
            {u"x"_s, static_cast<qint64>(f.x)},
            {u"y"_s, static_cast<qint64>(f.y)},
            {u"range"_s, range},
            {u"fixed"_s, paires_en_json(f.fixed)},
            {u"initial"_s, paires_en_json(f.initial)},
            {u"color"_s, couleur_en_json(f.color)},
            {u"visible"_s, f.visible},
            {u"side"_s, nom_enum(f.side, SIDE_NAMES)},
            {u"draw"_s, f.draw == FitDraw::Points ? u"points"_s : f.draw == FitDraw::Custom ? u"custom"_s : u"graph"_s},
            {u"draw_range"_s, QJsonArray{f.drawRange.first, f.drawRange.second}},
            {u"label"_s, texte_optionnel(f.label)}};
}

FitSpec modele_depuis_json(const QJsonObject &o)
{
    FitSpec f;
    f.kind = type_modele_depuis_json(o.value(u"kind"_s).toObject());
    f.x = static_cast<QId>(o.value(u"x"_s).toInteger());
    f.y = static_cast<QId>(o.value(u"y"_s).toInteger());
    const QJsonArray r = o.value(u"range"_s).toArray();
    if (r.size() == 2)
        f.range = std::make_pair(r[0].toDouble(), r[1].toDouble());
    f.fixed = paires_depuis_json(o.value(u"fixed"_s));
    f.initial = paires_depuis_json(o.value(u"initial"_s));
    f.color = couleur_depuis_json(o.value(u"color"_s), f.color);
    f.visible = drapeau(o, u"visible"_s, true);
    f.side = enum_depuis(o.value(u"side"_s), SIDE_NAMES, f.side);
    const QString draw = o.value(u"draw"_s).toString();
    f.draw = draw == u"points" ? FitDraw::Points : draw == u"custom" ? FitDraw::Custom : FitDraw::Graph;
    if (const QJsonArray d = o.value(u"draw_range"_s).toArray(); d.size() == 2)
        f.drawRange = {d[0].toDouble(), d[1].toDouble()};
    f.label = texte_optionnel_de(o.value(u"label"_s));
    return f;
}

} // namespace

QJsonValue couleur_en_json(const Color &c)
{
    return QJsonArray{c.c[0], c.c[1], c.c[2], c.c[3]};
}

Color couleur_depuis_json(const QJsonValue &v, Color def)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 4)
        return def;
    return Color::rgb(static_cast<std::uint8_t>(a[0].toInt()), static_cast<std::uint8_t>(a[1].toInt()), static_cast<std::uint8_t>(a[2].toInt()),
                      static_cast<std::uint8_t>(a[3].toInt()));
}

QJsonObject type_modele_en_json(const fit::ModelKind &k)
{
    QJsonObject o{{u"type"_s, k.nom_type()}};
    if (k.type == fit::ModelKind::Polynomial)
        o.insert(u"degree"_s, k.degree);
    if (k.type == fit::ModelKind::User)
        o.insert(u"expression"_s, k.expression);
    return o;
}

fit::ModelKind type_modele_depuis_json(const QJsonObject &o)
{
    fit::ModelKind k;
    k.type = fit::ModelKind::type_depuis_nom(o.value(u"type"_s).toString()).value_or(fit::ModelKind::Affine);
    k.degree = o.value(u"degree"_s).toInt(3);
    k.expression = o.value(u"expression"_s).toString();
    return k;
}

QJsonObject trace_en_json(const Plot &p)
{
    QJsonArray curves, annotations, tangents, fits;
    for (const auto &c : p.curves)
        curves.append(courbe_en_json(c));
    for (const auto &a : p.annotations)
        annotations.append(annotation_en_json(a));
    for (const auto &t : p.tangents)
        tangents.append(tangente_en_json(t));
    for (const auto &f : p.fits)
        fits.append(modele_en_json(f));
    return {{u"title"_s, titre_en_json(p.title)},
            {u"x"_s, axe_en_json(p.x)},
            {u"y"_s, axe_en_json(p.y)},
            {u"y2"_s, axe_en_json(p.y2)},
            {u"curves"_s, curves},
            {u"legend"_s, QJsonObject{{u"visible"_s, p.legend.visible}, {u"pos"_s, nom_enum(p.legend.pos, LEGEND_NAMES)}, {u"size"_s, p.legend.size}}},
            {u"annotations"_s, annotations},
            {u"tangents"_s, tangents},
            {u"fits"_s, fits}};
}

Plot trace_depuis_json(const QJsonObject &o)
{
    Plot p;
    p.title = titre_depuis_json(o.value(u"title"_s).toObject());
    p.x = axe_depuis_json(o.value(u"x"_s).toObject(), p.x);
    p.y = axe_depuis_json(o.value(u"y"_s).toObject(), p.y);
    p.y2 = axe_depuis_json(o.value(u"y2"_s).toObject(), p.y2);
    for (const QJsonValue v : o.value(u"curves"_s).toArray())
        p.curves.push_back(courbe_depuis_json(v.toObject()));
    const QJsonObject l = o.value(u"legend"_s).toObject();
    p.legend.visible = drapeau(l, u"visible"_s, true);
    p.legend.pos = enum_depuis(l.value(u"pos"_s), LEGEND_NAMES, LegendPos::Auto);
    p.legend.size = valeur_num(l, u"size"_s, 11.5);
    for (const QJsonValue v : o.value(u"annotations"_s).toArray())
        p.annotations.push_back(annotation_depuis_json(v.toObject()));
    for (const QJsonValue v : o.value(u"tangents"_s).toArray())
        p.tangents.push_back(tangente_depuis_json(v.toObject()));
    for (const QJsonValue v : o.value(u"fits"_s).toArray())
        p.fits.push_back(modele_depuis_json(v.toObject()));
    return p;
}

QJsonObject graphe_en_json(const GraphView &g)
{
    QJsonArray plots;
    for (const auto &p : g.plots)
        plots.append(trace_en_json(p));
    QJsonObject o{{u"id"_s, static_cast<qint64>(g.id)}, {u"name"_s, g.name}, {u"rows"_s, g.rows}, {u"cols"_s, g.cols}, {u"plots"_s, plots},
                  {u"link_x"_s, g.linkX}};
    if (g.origin)
        o.insert(u"origin"_s, QJsonObject{{u"script"_s, static_cast<qint64>(g.origin->script)}, {u"figure"_s, g.origin->figure}});
    return o;
}

GraphView graphe_depuis_json(const QJsonObject &o)
{
    GraphView g;
    g.id = static_cast<ViewId>(o.value(u"id"_s).toInteger());
    g.name = o.value(u"name"_s).toString();
    g.rows = std::clamp(o.value(u"rows"_s).toInt(1), 1, 4);
    g.cols = std::clamp(o.value(u"cols"_s).toInt(1), 1, 4);
    for (const QJsonValue v : o.value(u"plots"_s).toArray())
        g.plots.push_back(trace_depuis_json(v.toObject()));
    if (g.plots.empty())
        g.plots.push_back(Plot{});
    g.linkX = drapeau(o, u"link_x"_s, false);
    if (o.value(u"origin"_s).isObject()) {
        const QJsonObject og = o.value(u"origin"_s).toObject();
        g.origin = FigureOrigin{static_cast<std::uint64_t>(og.value(u"script"_s).toInteger()), og.value(u"figure"_s).toString()};
    }
    return g;
}
