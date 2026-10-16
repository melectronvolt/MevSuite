// Graphique dessiné par le programme (Qt Charts n'est pas utilisé : il est distribué sous GPL,
// incompatible avec la licence MIT de MeVSono) : axes gradués, grille, réticule et valeur sous la
// souris. Molette : zoom horizontal autour de la souris ; Maj + molette, molette horizontale, clic
// droit ou milieu glissé : déplacement ; double-clic : vue complète. L'axe vertical est fixé par le
// graphique (signal ou spectre).

#pragma once

#include "i18n.h"

#include <QWidget>

#include <optional>
#include <utility>
#include <vector>

class QPainter;
class QPolygonF;

class PlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget *parent = nullptr);

    void setTr(const Tr *tr);
    /// Épaisseur des traits et taille des repères, comme l'agrandissement de l'interface.
    void setUiScale(double scale);
    /// Revient à la vue complète (nouveau son, double-clic, menu « Tout voir »).
    void resetView();
    /// Impose les bornes horizontales visibles (unités de l'axe).
    void setViewX(double x0, double x1);

    /// Zone des courbes, à l'intérieur des axes.
    QRectF plotRect() const;
    /// Abscisse à l'écran (pixels du widget) d'une valeur de l'axe horizontal, et inversement.
    double screenX(double x) const;
    double dataX(double px) const;
    double screenY(double y) const;
    double dataY(double py) const;
    double viewX0() const { return m_x0; }
    double viewX1() const { return m_x1; }

protected:
    struct Tick {
        double value;
        QString label;
    };

    /// Vrai si le graphique a quelque chose à tracer ; sinon `paintEmpty` le remplace.
    virtual bool hasData() const = 0;
    /// Graphique vide : une explication au centre.
    virtual void paintEmpty(QPainter &painter) = 0;
    /// Bornes horizontales de la vue complète.
    virtual std::pair<double, double> defaultX() const = 0;
    /// Bornes de l'axe vertical.
    virtual std::pair<double, double> rangeY() const = 0;
    virtual QString xAxisLabel() const = 0;
    virtual QString yAxisLabel() const = 0;
    /// Graduations de l'axe horizontal ; par défaut, des pas ronds (1, 2 ou 5 × 10ⁿ).
    virtual std::vector<Tick> xTicks(double x0, double x1, double pixels) const;
    /// Texte du réticule, pour le point (`x`, `y`) sous la souris.
    virtual QString hoverText(QPointF data) const = 0;
    /// Courbes et repères, dans la zone `r` (le dessin y est limité).
    virtual void paintData(QPainter &painter, const QRectF &r) = 0;
    /// Le pointeur a bougé (ou quitté le graphique : vide).
    virtual void hoverMoved(std::optional<QPointF>) {}

    // Bouton gauche : appui, glisser, relâchement, à la position `pos` (pixels du widget).
    virtual void leftPressed(QPointF) {}
    virtual void leftMoved(QPointF) {}
    virtual void leftReleased(QPointF) {}
    /// Clic droit sans glisser.
    virtual void rightClicked(QPointF) {}

    /// Graduations rondes entre `lo` et `hi`, pour environ `count` graduations.
    std::vector<Tick> niceTicks(double lo, double hi, double count) const;
    /// Ligne brisée de nombreux points, avec le stylo du `painter`, tracée segment par segment : d'un
    /// seul tenant, une longue ligne épaisse et lissée qui zigzague (le signal) prend plus d'une
    /// demi-seconde avec Qt 6.10 et 6.11 (5 ms par segments), et l'interface se fige à chaque image.
    static void drawCurve(QPainter &painter, const QPolygonF &line);
    double uiScale() const { return m_uiScale; }
    const Tr *uiText() const { return m_tr; }
    std::optional<QPointF> hover() const { return m_hover; }
    /// Texte centré dans le graphique (graphique vide, calcul en cours).
    void paintMessage(QPainter &painter, const QString &text, double scale = 1.0);

    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /// Bornes à jour : vue complète demandée, ou première vue.
    void ensureView();
    void panPixels(double pixels);

    const Tr *m_tr = nullptr;
    double m_uiScale = 1.0;
    double m_x0 = 0.0;
    double m_x1 = 1.0;
    bool m_needsReset = true;
    std::optional<QPointF> m_hover;
    /// Glisser du bouton droit ou du milieu : dernière position, et déplacement depuis l'appui.
    std::optional<QPointF> m_panFrom;
    double m_panDistance = 0.0;
    Qt::MouseButton m_panButton = Qt::NoButton;
};
