// Zone image : affichage, navigation (molette, clic droit), tracé et modification des segments.

#pragma once

#include "i18n.h"
#include "session.h"
#include "view.h"

#include <QWidget>

#include <optional>

class ImageCanvas : public QWidget {
    Q_OBJECT

public:
    ImageCanvas(Session &session, QWidget *parent = nullptr);

    void setTr(const Tr *tr);
    /// Agrandissement de l'interface : épaisseur des traits, poignées et étiquettes.
    void setUiScale(double scale);
    void requestFit();
    /// Abandonne le tracé ou le déplacement en cours (Échap, changement d'outil).
    void cancelDrag();
    /// Zoom et position de l'image (conversions écran ↔ image des essais de l'interface).
    const ImageView &view() const { return m_view; }

signals:
    /// Nouveau segment tracé, assez long pour être gardé.
    void segmentDrawn(Segment seg);
    /// Extrémité de segment ou curseur A/B déplacé.
    void edited();
    /// Le zoom de l'image a changé (molette, ajustement à la fenêtre) : pixels écran par pixel image.
    void zoomChanged(double zoom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Drag {
        enum Kind {
            /// Tracé d'un nouveau segment depuis `start` (coordonnées image).
            New,
            /// Déplacement d'une extrémité (`endB` : extrémité b, sinon a).
            Handle,
            /// Déplacement du curseur A (0) ou B (1) le long de la ligne de coupe.
            Cursor,
        } kind;
        QPointF start;
        Target target{Target::Calib};
        bool endB = false;
        int cursor = 0;
    };

    /// Poignée (extrémité de segment ou curseur A/B) la plus proche de `pos`, si elle est assez
    /// proche. À égalité, l'extrémité l'emporte : les curseurs restent déplaçables sur le graphique.
    std::optional<Drag> grabAt(QPointF pos) const;
    void dragTo(QPointF pos, bool shift);
    void updateCursorShape(QPointF pos);
    void paintOverlays(QPainter &painter) const;
    void layoutView();

    Session &m_session;
    const Tr *m_tr = nullptr;
    ImageView m_view;
    std::optional<Drag> m_drag;
    /// Segment en cours de tracé.
    std::optional<Segment> m_preview;
    bool m_panning = false;
    QPointF m_lastPointer;
    double m_scale = 1.0;
    /// Dernier zoom signalé par `zoomChanged`.
    double m_reportedZoom = 0.0;
};
