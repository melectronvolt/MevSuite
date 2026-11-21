// Affichage de l'image : chargement, zoom/déplacement, conversions écran ↔ image, dessin.

#pragma once

#include "model.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QPainter;

struct ImageChargee {
    QString name;
    /// Pixels en pleine résolution (`Format_RGB888`), utilisés pour tous les calculs.
    QImage rgb;
    /// Précision par composante livrée par le décodeur avant conversion en RGB888.
    int bitsSource = 8;
    /// Aperçus réduits pour la vue d’ensemble. Aux forts zooms, le dessin utilise directement rgb.
    std::vector<QImage> apercus;

    QSizeF taille() const { return QSizeF(rgb.size()); }
    double diagonale_px() const { return longueur({double(rgb.width()), double(rgb.height())}); }
    /// Ramène un point dans les limites de l'image.
    QPointF borne(QPointF p) const;
};

/// Résultat du chargement d'une image : l'image, ou la raison de l'échec.
struct ResultatChargement {
    std::shared_ptr<const ImageChargee> image;
    /// En cas d'échec de lecture, error contient le détail donné par le système ou par Qt.
    enum class Erreur { Lecture, FormatInconnu, JpegIncomplet, FichierSpecial };
    Erreur typeErreur = Erreur::Lecture;
    QJsonObject seance;
    QByteArray empreinteSeance;  ///< Version lue sur ce fichier. Vide pour une photo seule.
    QString error;
};

/// Décode l'image (orientation EXIF comprise) et prépare l'affichage. Peut tourner dans un fil de fond.
ResultatChargement charge_image(const QString &path);
QStringList formats_image();

/// Zoom et position de l'image dans la zone d'affichage.
class VueImage {
public:
    /// Pixels écran par pixel image.
    double zoom() const { return m_zoom; }
    QPointF vers_ecran(QPointF p) const { return m_origineEcran + p * m_zoom; }
    QPointF vers_image(QPointF s) const { return (s - m_origineEcran) / m_zoom; }

    /// Réajuste l'image à la zone si elle l'a demandé, ou si l'élève n'a pas encore navigué.
    void dispose(QSizeF area, QSizeF imgSize);
    void demande_ajustement() { m_ajustementDemande = true; }
    void zoome_autour(QPointF pointer, double factor);
    void deplace_vue(QPointF delta);
    void dessine_image(QPainter &painter, QSizeF area, const ImageChargee &img) const;

private:
    void ajuste(QSizeF area, QSizeF imgSize);

    double m_zoom = 1.0;
    double m_zoomMini = 0.02;
    /// Position du coin (0, 0) de l'image, relative au coin de la zone d'affichage.
    QPointF m_origineEcran;
    bool m_ajustementDemande = true;
    /// Vrai dès que l'élève a zoomé ou déplacé l'image : on ne réajuste plus automatiquement.
    bool m_vueManuelle = false;
    QSizeF m_derniereTaille;
};

/// Segment entouré de noir pour rester visible sur n'importe quel fond. `scale` : agrandissement de l'interface.
void dessine_segment(QPainter &painter, QPointF a, QPointF b, const QColor &color, bool handles, double scale);
/// Bande de moyennage autour de la ligne de coupe (en coordonnées écran).
void dessine_bande(QPainter &painter, QPointF a, QPointF b, QPointF halfWidth, QColor color);
/// Texte sur fond sombre semi-transparent, centré horizontalement au-dessus de `pos`, dans la police
/// de l'interface ; `name` : nom d'un curseur (demi-gras).
void dessine_etiquette(QPainter &painter, QPointF pos, const QString &text, const QColor &color, double scale, bool name = false);
/// Position écran conseillée pour l'étiquette d'un segment : au milieu, décalée sur le côté.
QPointF ancre_etiquette(const VueImage &view, const Segment &seg, double scale);
