// Graphique du profil d'intensité : courbe, curseurs A et B, zoom horizontal à la molette.

#pragma once

#include "i18n.h"
#include "session.h"

#include <QWidget>

#include <optional>

class ProfilePlot : public QWidget {
    Q_OBJECT

public:
    ProfilePlot(Session &session, QWidget *parent = nullptr);

    void setTr(const Tr *tr);
    /// Revient à la vue complète de la coupe (nouvelle coupe, double-clic).
    void resetView();
    /// Partie visible de la coupe, en fractions de sa longueur (essais de l'interface).
    void setView(double t0, double t1);

signals:
    /// Curseur A ou B déplacé sur le graphique.
    void cursorsMoved();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /// Zone de la courbe, à l'intérieur des axes.
    QRectF plotRect() const;
    /// Fraction de la coupe affichée au bord gauche ou droit, à l'abscisse `px` (pixels du widget).
    double fractionAt(double px) const;
    double screenX(double fraction) const;
    double screenY(double intensity) const;
    void moveCursor(QPointF pos);

    Session &m_session;
    const Tr *m_tr = nullptr;
    /// Partie visible de la coupe, en fractions de sa longueur (marges de 5 % de chaque côté).
    double m_t0 = -0.05;
    double m_t1 = 1.05;
    /// Curseur gardé pendant tout le glisser.
    std::optional<int> m_draggedCursor;
    std::optional<QPointF> m_hover;
};
