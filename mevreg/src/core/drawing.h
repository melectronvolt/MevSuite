// Fenêtres de dessin : formes en unités physiques et cartes d'intensité (figures de
// diffraction, interférences), avec des palettes dont « laser », la couleur réelle de la
// longueur d'onde.

#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

class QPainter;

namespace drawing {

struct Item {
    enum Type { Line, Arrow, Circle, Rect, Text, Point, Bands, Radial, Map } type = Line;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    double x = 0, y = 0, r = 0, w = 0, h = 0;
    QString color = QStringLiteral("#000000");
    double width = 1.5;
    QString dash;
    std::optional<QString> fill;
    QString text;
    double size = 12;
    /// Cartes : indice du tableau de données, palette, longueur d'onde, normalisation.
    int data = 0;
    int nx = 0, ny = 0;
    double rmax = 0;
    QString palette = QStringLiteral("gris");
    std::optional<double> wavelength;
    bool normalize = true;
};

struct Desc {
    QString name;
    double xmin = -1, xmax = 1, ymin = -1, ymax = 1;
    QString unit;
    QString background = QStringLiteral("#ffffff");
    std::vector<Item> items;

    /// Description envoyée par le module Python `fenetre` ; message dans `error` si invalide.
    static std::optional<Desc> fromJson(const QJsonObject &o, QString *error);
};

const QStringList &palettes();
/// Couleur perçue d'une lumière monochromatique (λ en mètres), composantes entre 0 et 1.
std::array<double, 3> wavelengthRgb(double lambda);
/// Couleur d'une intensité t ∈ [0, 1] (−1..1 pour « bleu-rouge »).
QColor paletteColor(const QString &name, double t, std::optional<double> wavelength);

/// Évalue une expression en x et y (et r = √(x² + y²)) sur une grille nx × ny, ligne du haut
/// d'abord ; les lignes sont réparties sur les cœurs du processeur.
std::optional<std::vector<double>> evalMap(const QString &src, double xmin, double xmax, double ymin, double ymax, int nx, int ny,
                                           const std::vector<std::pair<QString, double>> &params, QString *error);

/// Dessin prêt à afficher : images des cartes calculées une fois.
struct Drawing {
    Desc desc;
    std::vector<std::vector<double>> data;
    std::vector<QImage> images;

    static Drawing make(Desc desc, std::vector<std::vector<double>> data);
};

/// Correspondance unités physiques → pixels (échelle identique sur les deux axes).
struct View {
    double x0 = 0, y0 = 0, scale = 1, xmin = 0, ymax = 0, w = 0, h = 0;
    QPointF toPx(double x, double y) const { return {x0 + (x - xmin) * scale, y0 + (ymax - y) * scale}; }
    QPointF fromPx(QPointF p) const { return {xmin + (p.x() - x0) / scale, ymax - (p.y() - y0) / scale}; }
    QRectF frame() const { return {x0, y0, w, h}; }
};

/// Dessine la fenêtre à l'échelle, centrée dans `size`, avec une règle graduée.
View paint(QPainter &p, const Drawing &d, QSizeF size);

} // namespace drawing
