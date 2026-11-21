// Exports des graphes : PNG haute résolution, SVG et PDF vectoriels, image pour le presse-papiers.

#pragma once

#include "plot/renderer.h"

#include <QImage>
#include <QIODevice>
#include <functional>
#include <QString>

class Seance;
struct Meta;

namespace exporter {

/// Remplace le fichier seulement après la finalisation et l'écriture complète du dessin.
bool ecrit_fichier(const QString &path, const std::function<bool(QIODevice &)> &dessine, QString *error);

enum class Format { Png, Svg, Pdf };

/// Format d'après l'extension du fichier (PNG par défaut).
Format format_de(const QString &path);
/// Taille d'export par défaut d'une fenêtre graphique.
QSizeF taille_par_defaut(int rows, int cols);
/// Valeurs à tracer de chaque graphe de la fenêtre.
std::vector<PlotData> resout_tout(const GraphView &view, const Document &doc, NumberStyle style);
/// Image de la fenêtre graphique à `scale` fois sa taille (fond blanc).
QImage image_graphe(const GraphView &view, const Document &doc, QSizeF size, double scale, NumberStyle style);
/// Image de graphes dont les valeurs sont déjà calculées (histogramme…).
QImage image_traces(const GraphView &view, const std::vector<PlotData> &data, QSizeF size, double scale, NumberStyle style);
/// En-tête du TP pour les exports PDF : titre, puis « noms — classe — date » (vide si non renseigné).
std::pair<QString, QString> lignes_en_tete(const Meta &meta);
/// Enregistre le graphe ; message dans `error` en cas d'échec.
bool ecrit_graphe(const QString &path, Format format, const GraphView &view, const Seance &ws, QSizeF size, NumberStyle style, QString *error);
/// Comme `ecrit_graphe`, avec des valeurs déjà calculées ; `meta` donne l'en-tête des PDF.
bool ecrit_traces(const QString &path, Format format, const GraphView &view, const std::vector<PlotData> &data, const Meta &meta, QSizeF size,
                NumberStyle style, QString *error);

} // namespace exporter
