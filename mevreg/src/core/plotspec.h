// Description des graphes : tout ce que l'utilisateur peut régler. Les grandeurs y sont
// référencées par identifiant, jamais copiées. Le format JSON est celui des fichiers `.phy`.

#pragma once

#include "core/document.h"
#include "core/fit.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

using ViewId = std::uint64_t;

/// Couleur RVBA.
struct Color {
    std::array<std::uint8_t, 4> c{0, 0, 0, 255};

    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) { return Color{{r, g, b, a}}; }
    QColor q() const { return QColor(c[0], c[1], c[2], c[3]); }
    static Color from(const QColor &q) { return rgb(q.red(), q.green(), q.blue(), q.alpha()); }
    QString hex() const;
    /// Couleur plus claire (mélange avec du blanc).
    Color lighten(double t) const;
    Color withAlpha(std::uint8_t a) const { return rgb(c[0], c[1], c[2], a); }
    bool operator==(const Color &) const = default;
};

namespace colors {
inline constexpr Color BLACK = Color::rgb(0, 0, 0);
inline constexpr Color WHITE = Color::rgb(255, 255, 255);
inline constexpr Color TEXT = Color::rgb(25, 25, 25);
inline constexpr Color GRAY = Color::rgb(110, 110, 110);
} // namespace colors

/// Palette par défaut, celle de matplotlib (C0 à C9).
inline constexpr std::array<Color, 10> PALETTE = {
    Color::rgb(0x1f, 0x77, 0xb4), Color::rgb(0xff, 0x7f, 0x0e), Color::rgb(0x2c, 0xa0, 0x2c), Color::rgb(0xd6, 0x27, 0x28),
    Color::rgb(0x94, 0x67, 0xbd), Color::rgb(0x8c, 0x56, 0x4b), Color::rgb(0xe3, 0x77, 0xc2), Color::rgb(0x7f, 0x7f, 0x7f),
    Color::rgb(0xbc, 0xbd, 0x22), Color::rgb(0x17, 0xbe, 0xcf),
};

enum class Marker { None, Point, Disc, Circle, Cross, Plus, Square, Triangle, Diamond };
inline constexpr std::array<Marker, 9> ALL_MARKERS = {Marker::None, Marker::Point, Marker::Disc, Marker::Circle, Marker::Cross,
                                                      Marker::Plus, Marker::Square, Marker::Triangle, Marker::Diamond};
QString markerLabel(Marker m);
/// Marqueur fermé (rond, carré…) : les barres d'incertitude s'arrêtent à son bord.
bool markerIsClosed(Marker m);

enum class LineKind {
    None,
    /// Segments entre les points.
    Straight,
    /// Courbe lissée par spline cubique, passant par tous les points.
    Cubic,
    /// Courbe lissée monotone, sans dépassement entre deux points.
    Monotone,
};
inline constexpr std::array<LineKind, 4> ALL_LINES = {LineKind::None, LineKind::Straight, LineKind::Cubic, LineKind::Monotone};
QString lineLabel(LineKind l);

enum class Dash { Solid, Dashed, Dotted, DashDot };
inline constexpr std::array<Dash, 4> ALL_DASHES = {Dash::Solid, Dash::Dashed, Dash::Dotted, Dash::DashDot};
QString dashLabel(Dash d);
/// Motif (longueurs tracé/espace, en multiples de l'épaisseur du trait) ; vide : trait continu.
std::vector<double> dashPattern(Dash d);

enum class ErrorBars { None, Vertical, Both, Rectangles };
inline constexpr std::array<ErrorBars, 4> ALL_ERRORS = {ErrorBars::None, ErrorBars::Vertical, ErrorBars::Both, ErrorBars::Rectangles};
QString errorsLabel(ErrorBars e);

enum class YSide { Left, Right };
enum class HAlign { Left, Center, Right };

enum class LegendPos {
    /// Coin intérieur où la légende cache le moins de points.
    Auto,
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
    Outside,
};
inline constexpr std::array<LegendPos, 6> ALL_LEGEND_POS = {LegendPos::Auto, LegendPos::TopRight, LegendPos::TopLeft,
                                                            LegendPos::BottomRight, LegendPos::BottomLeft, LegendPos::Outside};
QString legendPosLabel(LegendPos p);

struct NumFormat {
    enum Kind { Auto, Fixed, Scientific } kind = Auto;
    int decimals = 1;
    bool operator==(const NumFormat &o) const { return kind == o.kind && (kind == Auto || decimals == o.decimals); }
};

struct TitleSpec {
    QString text;
    bool visible = true;
    /// taille du texte des titres des axes, comme les titres des graphiques de la suite (taille
    /// normale) ; en gras : la police des graphes (DejaVu Sans) n'a pas de demi-gras
    double size = 13.0;
    HAlign align = HAlign::Center;
    bool bold = true;
    bool operator==(const TitleSpec &) const = default;
};

struct AxisSpec {
    /// Légende de l'axe ; vide : nom de la grandeur tracée.
    std::optional<QString> label;
    /// Unité affichée ; vide : unité de la grandeur tracée.
    std::optional<QString> unit;
    bool autoScale = true;
    double min = 0.0, max = 1.0;
    bool log = false;
    bool includeZero = false;
    /// Pas des graduations principales ; vide : automatique.
    std::optional<double> majorStep;
    /// Nombre d'intervalles entre deux graduations principales ; vide : automatique.
    std::optional<int> minorDivisions;
    bool gridMajor = true;
    bool gridMinor = false;
    NumFormat format;
    double labelSize = 13.0;
    double tickSize = 11.5;
    bool operator==(const AxisSpec &) const = default;
};

struct CurveSpec {
    QId x = 0, y = 0;
    YSide side = YSide::Left;
    Color color = PALETTE[0];
    Marker marker = Marker::Cross;
    double markerSize = 7.0;
    LineKind line = LineKind::None;
    double lineWidth = 1.5;
    Dash dash = Dash::Solid;
    ErrorBars errors = ErrorBars::None;
    /// Épaisseur des barres d'incertitude.
    double errorWidth = 1.1;
    /// Demi-largeur des embouts des barres (pixels) ; vide : selon la taille du marqueur ; 0 : sans embout.
    std::optional<double> capSize;
    /// Incertitudes choisies côté par côté (vers les x décroissants, croissants, les y décroissants,
    /// croissants) : n'importe quelle grandeur, pour des barres ou des rectangles dissymétriques ;
    /// vide : celle rattachée à la grandeur, u(x) ou u(y), la même des deux côtés.
    std::optional<QId> errXLow, errXHigh, errYLow, errYHigh;
    /// Texte de la légende ; vide : nom de la grandeur y.
    std::optional<QString> label;
    bool inLegend = true;
    bool visible = true;
    /// Diagramme en barres : largeur des barres (unités de x).
    std::optional<double> barWidth;
    bool operator==(const CurveSpec &) const = default;
};

struct LegendSpec {
    bool visible = true;
    LegendPos pos = LegendPos::Auto;
    double size = 11.5;
    bool operator==(const LegendSpec &) const = default;
};

/// Élément ajouté au graphe : droite de référence (verticale, horizontale, oblique), texte. Il se
/// déplace à la souris, sauf s'il est verrouillé.
struct Annotation {
    enum Kind { VLine, HLine, Text, Line } kind = VLine;
    /// Droite verticale : x ; horizontale : y ; texte : point d'ancrage (début de la ligne de
    /// base) ; droite oblique : premier point.
    double x = 0.0, y = 0.0;
    /// Droite oblique : second point.
    double x2 = 1.0, y2 = 1.0;
    QString text;
    Color color = colors::GRAY;
    YSide side = YSide::Left;
    QString label;
    Dash dash = Dash::Dashed;
    /// Taille du texte (pixels).
    double size = 12.0;
    /// Verrouillée : la souris ne la déplace pas (réglages seulement).
    bool locked = false;
    bool operator==(const Annotation &) const = default;
};

/// Méthode des tangentes appliquée à une courbe du graphe.
struct TangentSpec {
    QId x = 0, y = 0;
    /// Abscisse du point de contact de la première tangente ; vide : automatique.
    std::optional<double> contact;
    Color color = PALETTE[2];
    YSide side = YSide::Left;
    /// Verrouillée : les points de contact ne se glissent plus à la souris.
    bool locked = false;
    bool operator==(const TangentSpec &) const = default;
};

/// Étendue de la courbe d'un modèle : tout le graphe (elle suit le zoom, avant et après les
/// points), les seuls points utilisés, ou de a à b.
enum class FitDraw { Graph, Points, Custom };

/// Modélisation d'une courbe du graphe.
struct FitSpec {
    fit::ModelKind kind;
    QId x = 0, y = 0;
    /// Plage d'abscisses prise en compte ; vide : tous les points.
    std::optional<std::pair<double, double>> range;
    /// Paramètres fixés : (nom, valeur).
    std::vector<std::pair<QString, double>> fixed;
    /// Valeurs initiales imposées : (nom, valeur).
    std::vector<std::pair<QString, double>> initial;
    Color color = PALETTE[3];
    bool visible = true;
    YSide side = YSide::Left;
    FitDraw draw = FitDraw::Graph;
    std::pair<double, double> drawRange{0.0, 1.0};
    /// Nom dans la légende ; vide : « Modèle : linéaire »…
    std::optional<QString> label;

    // Résultat du dernier ajustement (recalculé, jamais enregistré).
    bool fitted = false;
    std::optional<fit::FitResult> result;
    QString resultError;
    QStringList paramNames;
    std::uint64_t fittedRevision = 0;
    QString fittedSettings;

    static FitSpec make(const fit::ModelKind &kind, QId x, QId y, Color color);
    /// Empreinte des réglages, pour savoir s'il faut refaire l'ajustement.
    QString settingsKey() const;

    bool operator==(const FitSpec &o) const
    {
        return kind == o.kind && x == o.x && y == o.y && range == o.range && fixed == o.fixed && initial == o.initial && color == o.color
               && visible == o.visible && side == o.side && draw == o.draw && drawRange == o.drawRange && label == o.label;
    }
};

struct Plot {
    TitleSpec title;
    AxisSpec x, y, y2 = [] {
        AxisSpec a;
        a.gridMajor = false;
        return a;
    }();
    std::vector<CurveSpec> curves;
    LegendSpec legend;
    std::vector<Annotation> annotations;
    std::vector<TangentSpec> tangents;
    std::vector<FitSpec> fits;

    bool usesRightAxis() const;
    /// Couleur suivante de la palette, non encore utilisée si possible.
    Color nextColor() const;
    /// Ajoute la courbe y(x) avec un style par défaut.
    CurveSpec &addCurve(QId x, QId y);
    const AxisSpec &axis(YSide side) const { return side == YSide::Right ? y2 : y; }
    AxisSpec &axis(YSide side) { return side == YSide::Right ? y2 : y; }
    /// Le graphe utilise-t-il cette grandeur ?
    bool uses(QId id) const;
    /// Retire tout ce qui utilise la grandeur.
    void removeQuantity(QId id);
    /// Retire la courbe, et avec elle la méthode des tangentes et les modèles faits sur elle.
    void removeCurve(size_t index);

    bool operator==(const Plot &) const = default;
};

struct FigureOrigin {
    std::uint64_t script = 0;
    QString figure;
    bool operator==(const FigureOrigin &) const = default;
};

/// Fenêtre graphique : un ou plusieurs graphes disposés en grille.
struct GraphView {
    ViewId id = 0;
    QString name;
    int rows = 1, cols = 1;
    std::vector<Plot> plots;
    /// Axes des abscisses liés : même échelle pour tous les graphes.
    bool linkX = false;
    /// Figure produite par un script Python (redessinée à chaque exécution).
    std::optional<FigureOrigin> origin;

    static GraphView make(ViewId id, const QString &name);
    /// Ajuste la grille et le nombre de graphes.
    void setGrid(int rows, int cols);
    /// Graphes effectivement affichés (ceux qui tiennent dans la grille).
    int visiblePlots() const;

    bool operator==(const GraphView &) const = default;
};

QJsonValue colorToJson(const Color &c);
Color colorFromJson(const QJsonValue &v, Color def);
QJsonObject plotToJson(const Plot &p);
Plot plotFromJson(const QJsonObject &o);
QJsonObject graphToJson(const GraphView &g);
GraphView graphFromJson(const QJsonObject &o);
QJsonObject fitKindToJson(const fit::ModelKind &k);
fit::ModelKind fitKindFromJson(const QJsonObject &o);
