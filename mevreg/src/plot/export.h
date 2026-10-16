// Exports des graphes : PNG haute résolution, SVG et PDF vectoriels, image pour le presse-papiers.

#pragma once

#include "plot/renderer.h"

#include <QImage>
#include <QString>

class Workspace;
struct Meta;

namespace exporter {

enum class Format { Png, Svg, Pdf };

/// Format d'après l'extension du fichier (PNG par défaut).
Format formatOf(const QString &path);
/// Taille d'export par défaut d'une fenêtre graphique.
QSizeF defaultSize(int rows, int cols);
/// Valeurs à tracer de chaque graphe de la fenêtre.
std::vector<PlotData> resolveAll(const GraphView &view, const Document &doc, NumberStyle style);
/// Image de la fenêtre graphique à `scale` fois sa taille (fond blanc).
QImage graphImage(const GraphView &view, const Document &doc, QSizeF size, double scale, NumberStyle style);
/// Image de graphes dont les valeurs sont déjà calculées (histogramme…).
QImage plotsImage(const GraphView &view, const std::vector<PlotData> &data, QSizeF size, double scale, NumberStyle style);
/// En-tête du TP pour les exports PDF : titre, puis « noms — classe — date » (vide si non renseigné).
std::pair<QString, QString> headerLines(const Meta &meta);
/// Enregistre le graphe ; message dans `error` en cas d'échec.
bool writeGraph(const QString &path, Format format, const GraphView &view, const Workspace &ws, QSizeF size, NumberStyle style, QString *error);
/// Comme `writeGraph`, avec des valeurs déjà calculées ; `meta` donne l'en-tête des PDF.
bool writePlots(const QString &path, Format format, const GraphView &view, const std::vector<PlotData> &data, const Meta &meta, QSizeF size,
                NumberStyle style, QString *error);

} // namespace exporter
