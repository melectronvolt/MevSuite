#include "icons.h"

#include "style.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <functional>
#include <numbers>

namespace icons {

namespace {

using Drawing = std::function<void(QPainter &, const QRectF &)>;

/// Icône vectorielle : dessinée à la taille demandée, grisée quand le bouton est désactivé.
class PaintedEngine : public QIconEngine {
public:
    explicit PaintedEngine(Drawing draw)
        : m_draw(std::move(draw))
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (mode == QIcon::Disabled)
            painter->setOpacity(0.4);
        m_draw(*painter, QRectF(rect));
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QIconEngine *clone() const override { return new PaintedEngine(m_draw); }

private:
    Drawing m_draw;
};

QIcon painted(Drawing draw)
{
    return QIcon(new PaintedEngine(std::move(draw)));
}

/// Carré centré dans `r`, réduit de `margin` (fraction du côté).
QRectF square(const QRectF &r, double margin)
{
    const double side = std::min(r.width(), r.height()) * (1.0 - 2.0 * margin);
    return QRectF(r.center() - QPointF(side, side) / 2.0, QSizeF(side, side));
}

void sun(QPainter &p, const QRectF &r)
{
    const QRectF s = square(r, 0.04);
    const QPointF c = s.center();
    const double unit = s.width() / 2.0;
    const QColor ray(240, 160, 0);
    p.setPen(QPen(ray, std::max(1.0, unit * 0.16), Qt::SolidLine, Qt::RoundCap));
    for (int k = 0; k < 8; ++k) {
        const double a = k * std::numbers::pi / 4.0;
        const QPointF dir(std::cos(a), std::sin(a));
        p.drawLine(c + dir * unit * 0.66, c + dir * unit * 0.94);
    }
    p.setPen(QPen(QColor(200, 120, 0), std::max(1.0, unit * 0.08)));
    p.setBrush(QColor(255, 196, 30));
    p.drawEllipse(c, unit * 0.44, unit * 0.44);
}

void moon(QPainter &p, const QRectF &r)
{
    const QRectF s = square(r, 0.08);
    const double unit = s.width() / 2.0;
    QPainterPath disc;
    disc.addEllipse(s.center(), unit * 0.86, unit * 0.86);
    QPainterPath bite;
    bite.addEllipse(s.center() + QPointF(unit * 0.5, -unit * 0.34), unit * 0.72, unit * 0.72);
    p.setPen(QPen(QColor(150, 160, 190), std::max(1.0, unit * 0.08)));
    p.setBrush(QColor(236, 236, 214));
    p.drawPath(disc.subtracted(bite));
}

/// Rectangle 3 × 2 centré dans `r`, contour arrondi léger.
QRectF flagRect(const QRectF &r)
{
    const double w = std::min(r.width(), r.height() * 1.5) * 0.96;
    return QRectF(r.center() - QPointF(w / 2.0, w / 3.0), QSizeF(w, w * 2.0 / 3.0));
}

void france(QPainter &p, const QRectF &r)
{
    const QRectF f = flagRect(r);
    QPainterPath clip;
    clip.addRoundedRect(f, f.height() * 0.08, f.height() * 0.08);
    p.setClipPath(clip);
    const double third = f.width() / 3.0;
    p.fillRect(QRectF(f.left(), f.top(), third, f.height()), QColor(0, 85, 164));
    p.fillRect(QRectF(f.left() + third, f.top(), third, f.height()), Qt::white);
    p.fillRect(QRectF(f.left() + 2 * third, f.top(), third + 0.5, f.height()), QColor(239, 65, 53));
    p.setClipping(false);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(clip);
}

void unitedKingdom(QPainter &p, const QRectF &r)
{
    const QRectF f = flagRect(r);
    QPainterPath clip;
    clip.addRoundedRect(f, f.height() * 0.08, f.height() * 0.08);
    p.setClipPath(clip);
    p.fillRect(f, QColor(1, 33, 105));
    const double h = f.height();
    const QColor red(200, 16, 46);
    // Croix de Saint-André (blanche) et de Saint-Patrick (rouge, plus fine).
    for (const auto &[w, color] : {std::pair{h * 0.2, QColor(Qt::white)}, std::pair{h * 0.08, red}}) {
        p.setPen(QPen(color, w, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(f.topLeft(), f.bottomRight());
        p.drawLine(f.bottomLeft(), f.topRight());
    }
    // Croix de Saint-Georges, bordée de blanc.
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRect(QRectF(f.center().x() - h * 0.17, f.top(), h * 0.34, h));
    p.drawRect(QRectF(f.left(), f.center().y() - h * 0.17, f.width(), h * 0.34));
    p.setBrush(red);
    p.drawRect(QRectF(f.center().x() - h * 0.1, f.top(), h * 0.2, h));
    p.drawRect(QRectF(f.left(), f.center().y() - h * 0.1, f.width(), h * 0.2));
    p.setClipping(false);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(clip);
}

/// Pastille ronde ou triangulaire de couleur `fill`, avec un symbole blanc.
void badge(QPainter &p, const QRectF &r, Status kind)
{
    const QRectF s = square(r, 0.06);
    const double u = s.width();
    QPen mark(Qt::white, std::max(1.2, u * 0.13), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (kind) {
    case Status::Info: {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 120, 210));
        p.drawEllipse(s);
        p.setPen(mark);
        p.drawLine(QPointF(s.center().x(), s.top() + u * 0.44), QPointF(s.center().x(), s.top() + u * 0.74));
        p.drawPoint(QPointF(s.center().x(), s.top() + u * 0.27));
        break;
    }
    case Status::Success:
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 160, 80));
        p.drawEllipse(s);
        p.setPen(mark);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF{s.topLeft() + QPointF(u * 0.27, u * 0.52), s.topLeft() + QPointF(u * 0.44, u * 0.68),
                                 s.topLeft() + QPointF(u * 0.74, u * 0.34)});
        break;
    case Status::Warning: {
        QPainterPath triangle;
        triangle.moveTo(s.center().x(), s.top() + u * 0.04);
        triangle.lineTo(s.right(), s.bottom() - u * 0.06);
        triangle.lineTo(s.left(), s.bottom() - u * 0.06);
        triangle.closeSubpath();
        p.setPen(QPen(QColor(225, 140, 0), u * 0.06, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(245, 160, 0));
        p.drawPath(triangle);
        p.setPen(mark);
        p.drawLine(QPointF(s.center().x(), s.top() + u * 0.38), QPointF(s.center().x(), s.top() + u * 0.62));
        p.drawPoint(QPointF(s.center().x(), s.top() + u * 0.78));
        break;
    }
    case Status::Error:
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(210, 45, 45));
        p.drawEllipse(s);
        p.setPen(mark);
        p.drawLine(s.topLeft() + QPointF(u * 0.32, u * 0.32), s.topLeft() + QPointF(u * 0.68, u * 0.68));
        p.drawLine(s.topLeft() + QPointF(u * 0.68, u * 0.32), s.topLeft() + QPointF(u * 0.32, u * 0.68));
        break;
    }
}

} // namespace

QIcon theme(bool dark)
{
    return painted(dark ? Drawing(moon) : Drawing(sun));
}

QIcon flag(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return painted(france);
    case Lang::En: return painted(unitedKingdom);
    }
    return {};
}

QIcon periodTab()
{
    return painted([](QPainter &p, const QRectF &r) {
        const QPalette pal = QApplication::palette();
        const QRectF s = square(r, 0.06);
        const double u = s.width();
        const double line = std::max(1.0, u * 0.08);
        // une période et demie de sinusoïde ; t₁ et t₂ sur deux passages par zéro montants
        const double x1 = s.left() + u * 0.14;
        const double period = u * 0.62;
        const double middle = s.top() + u * 0.4;
        QPainterPath wave;
        for (int k = 0; k <= 40; ++k) {
            const double x = s.left() + u * k / 40.0;
            const QPointF point(x, middle - u * 0.24 * std::sin(2.0 * std::numbers::pi * (x - x1) / period));
            k == 0 ? wave.moveTo(point) : wave.lineTo(point);
        }
        p.setPen(QPen(pal.color(QPalette::WindowText), line, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(wave);
        p.setPen(QPen(cursorColorT(pal), line * 1.2, Qt::SolidLine, Qt::RoundCap));
        for (double x : {x1, x1 + period})
            p.drawLine(QPointF(x, s.top() + u * 0.06), QPointF(x, s.bottom() - u * 0.04));
        // la durée mesurée, entre les deux curseurs
        const double y = s.bottom() - u * 0.14;
        p.drawLine(QPointF(x1, y), QPointF(x1 + period, y));
    });
}

QIcon frequenciesTab()
{
    return painted([](QPainter &p, const QRectF &r) {
        const QPalette pal = QApplication::palette();
        const QRectF s = square(r, 0.06);
        const double u = s.width();
        const double line = std::max(1.0, u * 0.08);
        const double base = s.bottom() - u * 0.08;
        // raies de plus en plus faibles : la fondamentale et ses harmoniques
        p.setPen(QPen(spectrumColor(pal), line * 1.6, Qt::SolidLine, Qt::RoundCap));
        const double heights[] = {0.78, 0.5, 0.32, 0.18};
        for (int k = 0; k < 4; ++k) {
            const double x = s.left() + u * (0.2 + 0.2 * k);
            p.drawLine(QPointF(x, base), QPointF(x, base - u * heights[k]));
        }
        p.setPen(QPen(pal.color(QPalette::WindowText), line, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(s.left() + u * 0.04, base), QPointF(s.right() - u * 0.04, base));
    });
}

QIcon status(Status kind)
{
    return painted([kind](QPainter &p, const QRectF &r) { badge(p, r, kind); });
}

QIcon question()
{
    return painted([](QPainter &p, const QRectF &r) {
        const QRectF s = square(r, 0.06);
        const double u = s.width();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 120, 210));
        p.drawEllipse(s);
        // crochet du « ? », puis son point
        QPainterPath q;
        q.moveTo(s.left() + u * 0.36, s.top() + u * 0.38);
        q.cubicTo(s.left() + u * 0.36, s.top() + u * 0.17, s.left() + u * 0.66, s.top() + u * 0.15, s.left() + u * 0.66, s.top() + u * 0.36);
        q.cubicTo(s.left() + u * 0.66, s.top() + u * 0.50, s.left() + u * 0.50, s.top() + u * 0.49, s.left() + u * 0.50, s.top() + u * 0.62);
        p.setPen(QPen(Qt::white, std::max(1.2, u * 0.12), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(q);
        p.drawPoint(QPointF(s.left() + u * 0.50, s.top() + u * 0.79));
    });
}

} // namespace icons
