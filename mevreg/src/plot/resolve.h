// Passage de la description d'un graphe aux valeurs à tracer, en lisant le document.

#pragma once

#include "core/document.h"
#include "core/format.h"
#include "core/plotspec.h"
#include "core/titration.h"

#include <functional>
#include <optional>
#include <utility>
#include <vector>

/// Série à tracer : valeurs et style.
struct Series {
    std::vector<double> xs, ys;
    /// Incertitudes ; vide : aucune, une seule valeur : la même pour tous les points.
    std::vector<double> ux, uy;
    /// Incertitudes d'un seul côté (vers les valeurs décroissantes, croissantes), à la place de
    /// `ux`, `uy` de ce côté quand elles sont données.
    std::vector<double> uxLow, uxHigh, uyLow, uyHigh;
    Color color = PALETTE[0];
    Marker marker = Marker::Cross;
    double markerSize = 7.0;
    LineKind line = LineKind::None;
    double lineWidth = 1.5;
    Dash dash = Dash::Solid;
    ErrorBars errors = ErrorBars::None;
    double errorWidth = 1.1;
    std::optional<double> capSize;
    YSide side = YSide::Left;
    std::optional<QString> legend;
    /// Histogramme : largeur des barres (unités de x), tracées depuis y = 0.
    std::optional<double> bars;
    /// Compte dans l'échelle automatique (pas les courbes de modèle).
    bool autoscale = true;
    /// Courbe du graphe représentée (indice dans `Plot::curves`), -1 pour un modèle.
    int curve = -1;
    /// Courbe d'une fonction (modèle) : recalculée au tracé sur les abscisses affichées, limitées à
    /// `domain` s'il est donné ; `xs`, `ys` en gardent un échantillon sur la plage des points.
    std::function<std::vector<double>(const std::vector<double> &)> function;
    std::optional<std::pair<double, double>> domain;
};

/// Droite tracée sur toute la zone du graphe.
struct Overlay {
    enum Kind { VLine, HLine, Line } kind = VLine;
    /// Abscisse (VLine), ordonnée (HLine) ou pente (Line).
    double a = 0.0;
    /// Ordonnée à l'origine (Line).
    double b = 0.0;
    YSide side = YSide::Left;
    Color color = colors::GRAY;
    double width = 1.2;
    Dash dash = Dash::Dashed;
};

/// Texte placé en coordonnées de données.
struct DataLabel {
    double x = 0, y = NAN;
    YSide side = YSide::Left;
    QString text;
    Color color = colors::TEXT;
    double size = 11.5;
    /// Décalage en pixels par rapport au point.
    double dx = 0, dy = 0;
    /// Dessine un point au lieu indiqué.
    bool dot = false;
};

struct PlotData {
    std::vector<Series> series;
    std::vector<Overlay> overlays;
    std::vector<DataLabel> labels;
    /// Légendes d'axes automatiques (nom et unité des grandeurs tracées).
    QString xName, xUnit, yName, yUnit, y2Name, y2Unit;
};

/// Valeurs (x, y) d'un couple de grandeurs, limitées à une plage d'abscisses.
std::pair<std::vector<double>, std::vector<double>> pairsInRange(const Document &doc, QId x, QId y, std::optional<std::pair<double, double>> range);
/// Ajuste un modèle selon sa description (résultat rangé dans la description).
void computeFit(FitSpec &spec, const Document &doc);
/// Refait les ajustements dont les données ou les réglages ont changé.
void updateFits(Plot &plot, const Document &doc);
/// Méthode des tangentes pour une description donnée.
std::optional<std::pair<titration::Curve, titration::Tangents>> computeTangents(const TangentSpec &spec, const Document &doc, QString *error = nullptr);
/// Noms (séparés par des virgules) et unité commune des grandeurs.
std::pair<QString, QString> namesAndUnit(const Document &doc, const std::vector<QId> &ids);
/// Valeurs à tracer pour un graphe.
PlotData resolve(const Plot &plot, const Document &doc, NumberStyle style);
