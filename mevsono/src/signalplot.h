// Graphique du signal : tracé du son, position de lecture, et les curseurs de l'activité choisie :
// t₁ et t₂ de mesure d'une période, ou l'extrait analysé (A et B, A toujours avant B). Glisser près
// d'un curseur le déplace ; ailleurs, glisser trace une nouvelle paire ; un simple clic amène le
// curseur le plus proche.

#pragma once

#include "plot.h"
#include "session.h"

#include <optional>

class SignalPlot : public PlotWidget {
    Q_OBJECT

public:
    SignalPlot(Session &session, QWidget *parent = nullptr);

    /// Position de lecture (secondes), pendant la lecture ou la pause.
    void setPlayhead(std::optional<double> t);

signals:
    /// Curseur A ou B déplacé.
    void extractChanged();
    /// Curseur t₁ ou t₂ déplacé.
    void periodChanged();

protected:
    bool hasData() const override;
    void paintEmpty(QPainter &painter) override;
    std::pair<double, double> defaultX() const override;
    std::pair<double, double> rangeY() const override;
    QString xAxisLabel() const override;
    QString yAxisLabel() const override;
    QString hoverText(QPointF data) const override;
    void paintData(QPainter &painter, const QRectF &r) override;
    void hoverMoved(std::optional<QPointF> pos) override;
    void leftPressed(QPointF pos) override;
    void leftMoved(QPointF pos) override;
    void leftReleased(QPointF pos) override;

private:
    /// Instant sous l'abscisse `px`, borné à la durée du son.
    double timeAt(double px) const;
    /// Paire de curseurs de l'activité choisie (vide : pas encore de curseurs de mesure).
    std::optional<Cursors> pair() const;
    void setPair(const Cursors &c);
    /// Curseur de l'activité choisie à moins de `GRAB_RADIUS` pixels de l'abscisse `px`.
    std::optional<int> nearCursor(double px) const;
    /// Aimante les curseurs `moved` de mesure aux passages par zéro montants (si l'option est active).
    void snap(std::initializer_list<int> moved);

    Session &m_session;
    std::optional<double> m_playhead;

    /// Glisser en cours : déplacement d'un curseur, ou nouvelle paire depuis l'instant `anchor`.
    struct Drag {
        std::optional<int> cursor;
        double anchor = 0.0;
    };
    std::optional<QPointF> m_pressedAt;
    std::optional<Drag> m_drag;
};
