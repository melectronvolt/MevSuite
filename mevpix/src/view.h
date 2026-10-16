// Affichage de l'image : chargement, zoom/déplacement, conversions écran ↔ image, dessin.

#pragma once

#include "model.h"

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QString>

#include <memory>
#include <vector>

class QPainter;

struct LoadedImage {
    QString name;
    /// Pixels en pleine résolution (`Format_RGB888`), utilisés pour tous les calculs.
    QImage rgb;
    /// Images d'affichage, chacune moitié de la précédente. La première est réduite si l'image dépasse
    /// la taille maximale d'affichage ; les suivantes évitent de réduire une grande photo à chaque dessin.
    std::vector<QImage> levels;

    QSizeF size() const { return QSizeF(rgb.size()); }
    /// Ramène un point dans les limites de l'image.
    QPointF clamp(QPointF p) const;
};

/// Résultat du chargement d'une image : l'image, ou la raison de l'échec.
struct LoadResult {
    std::shared_ptr<const LoadedImage> image;
    /// Format non reconnu (HEIC d'iPhone sans greffon, PDF…) : message traduit pour l'élève.
    bool unsupported = false;
    QString error;
};

/// Décode l'image (orientation EXIF comprise) et prépare l'affichage. Peut tourner dans un fil de fond.
LoadResult loadImage(const QString &path);

/// Zoom et position de l'image dans la zone d'affichage.
class ImageView {
public:
    /// Pixels écran par pixel image.
    double zoom() const { return m_zoom; }
    QPointF toScreen(QPointF p) const { return m_offset + p * m_zoom; }
    QPointF toImage(QPointF s) const { return (s - m_offset) / m_zoom; }

    /// Réajuste l'image à la zone si elle l'a demandé, ou si l'élève n'a pas encore navigué.
    void layout(QSizeF area, QSizeF imgSize);
    void requestFit() { m_fitRequested = true; }
    void zoomAround(QPointF pointer, double factor);
    void pan(QPointF delta);
    void paintImage(QPainter &painter, QSizeF area, const LoadedImage &img) const;

private:
    void fit(QSizeF area, QSizeF imgSize);

    double m_zoom = 1.0;
    /// Position du coin (0, 0) de l'image, relative au coin de la zone d'affichage.
    QPointF m_offset;
    bool m_fitRequested = true;
    /// Vrai dès que l'élève a zoomé ou déplacé l'image : on ne réajuste plus automatiquement.
    bool m_navigated = false;
    QSizeF m_lastSize;
};

/// Segment entouré de noir pour rester visible sur n'importe quel fond. `scale` : agrandissement de l'interface.
void paintSegment(QPainter &painter, QPointF a, QPointF b, const QColor &color, bool handles, double scale);
/// Bande de moyennage autour de la ligne de coupe (en coordonnées écran).
void paintBand(QPainter &painter, QPointF a, QPointF b, QPointF halfWidth, QColor color);
/// Texte sur fond sombre semi-transparent, centré horizontalement au-dessus de `pos`.
void paintLabel(QPainter &painter, QPointF pos, const QString &text, const QColor &color, double scale);
/// Position écran conseillée pour l'étiquette d'un segment : au milieu, décalée sur le côté.
QPointF labelAnchor(const ImageView &view, const Segment &seg, double scale);
