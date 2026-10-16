// Moteur de tracé : mise en page d'une fenêtre graphique (grille de graphes alignés) et
// dessin avec QPainter. Le même code dessine à l'écran, en PNG, en SVG, en PDF et sur
// l'imprimante : le rendu est identique partout.

#pragma once

#include "plot/resolve.h"

#include <QColor>
#include <QFont>
#include <QRectF>

#include <optional>
#include <vector>

class QPainter;
class QPaintDevice;

/// Correspondance entre valeurs d'un axe et pixels.
struct AxisMap {
    double min = 0, max = 1;
    bool log = false;
    /// Pixel correspondant à `min`, puis à `max`.
    double a = 0, b = 1;

    double norm(double v) const;
    double toPx(double v) const { return a + (b - a) * norm(v); }
    double fromPx(double px) const;
    bool valid(double v) const { return std::isfinite(v) && (!log || v > 0.0); }
};

/// Zones d'un graphe, pour l'interaction (zoom, réticule, double-clic…).
struct PlotLayout {
    int index = 0;
    QRectF cell, area;
    AxisMap x, y;
    std::optional<AxisMap> y2;
    std::optional<QRectF> title, legend;
    QRectF xAxis, yAxis;
    std::optional<QRectF> y2Axis;

    const AxisMap &yMap(YSide side) const { return side == YSide::Right && y2 ? *y2 : y; }
};

/// Couleurs du graphe : claires pour l'impression et les exports, sombres pour l'écran en thème sombre.
struct PlotTheme {
    bool dark = false;
    QColor background, frame, text, grid, gridMinor, legendFill, legendBorder, labelFill;

    static PlotTheme light();
    static PlotTheme darkTheme();
    /// Couleur d'un élément du graphe : les couleurs très sombres s'éclaircissent sur fond sombre.
    QColor adapt(const Color &c) const;
};

struct RenderOptions {
    NumberStyle style;
    PlotTheme theme = PlotTheme::light();
    /// Pixels de l'appareil par pixel logique (écran haute densité, image agrandie).
    double devicePixelRatio = 1.0;
    /// Aligne les traits fins sur la grille de pixels (écran, PNG) ; inutile en vectoriel.
    bool pixelSnap = true;
    /// Courbe mise en évidence (graphe, série) : réglages ouverts, survol de la légende.
    std::optional<std::pair<int, int>> highlight;
};

/// Police des graphes (DejaVu Sans intégrée), taille en pixels logiques pour l'appareil donné.
QFont plotFont(double px, bool bold, const QPaintDevice *device);
/// Police à chasse fixe (code Python dans les documents PDF).
QFont monoFont(double px, const QPaintDevice *device);

/// Dessine un marqueur centré en `c`, de taille `size` pixels ; `fill` remplit les marqueurs creux.
void drawMarker(QPainter &p, Marker marker, QPointF c, double size, const QColor &color, const QColor &fill);

/// Dessine la fenêtre graphique dans le rectangle (0, 0, size) ; renvoie la mise en page de chaque graphe.
std::vector<PlotLayout> renderGraph(QPainter &p, const GraphView &view, const std::vector<PlotData> &data, QSizeF size, const RenderOptions &opts);
/// Comme `renderGraph`, agrandi `scale` fois (agrandissement de l'interface, à l'écran) : textes,
/// traits, marqueurs, barres d'incertitude et marges suivent, toujours alignés sur les pixels de
/// l'appareil ; la mise en page est rendue dans les pixels de `size`.
std::vector<PlotLayout> renderGraphScaled(QPainter &p, const GraphView &view, const std::vector<PlotData> &data, QSizeF size, RenderOptions opts,
                                          double scale);
/// Mise en page d'un dessin agrandi `scale` fois.
PlotLayout scaledLayout(const PlotLayout &layout, double scale);
