#include "icons.h"

#include "style.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <functional>
#include <numbers>

namespace icons {

namespace {

using Drawing = std::function<void(QPainter &, const QRectF &, QIcon::Mode)>;

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
        m_draw(*painter, QRectF(rect), mode);
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

using DessinSimple = std::function<void(QPainter &, const QRectF &)>;

QIcon dessine_icone(DessinSimple draw)
{
    return QIcon(new PaintedEngine([draw = std::move(draw)](QPainter &p, const QRectF &r, QIcon::Mode) { draw(p, r); }));
}

/// Carré centré dans `r`, réduit de `margin` (fraction du côté).
QRectF carre(const QRectF &r, double margin)
{
    const double side = std::min(r.width(), r.height()) * (1.0 - 2.0 * margin);
    return QRectF(r.center() - QPointF(side, side) / 2.0, QSizeF(side, side));
}

void soleil(QPainter &p, const QRectF &r)
{
    const QRectF s = carre(r, 0.04);
    const QPointF c = s.center();
    const double unite = s.width() / 2.0;
    const QColor ray(240, 160, 0);
    p.setPen(QPen(ray, std::max(1.0, unite * 0.16), Qt::SolidLine, Qt::RoundCap));
    for (int k = 0; k < 8; ++k) {
        const double a = k * std::numbers::pi / 4.0;
        const QPointF dir(std::cos(a), std::sin(a));
        p.drawLine(c + dir * unite * 0.66, c + dir * unite * 0.94);
    }
    p.setPen(QPen(QColor(200, 120, 0), std::max(1.0, unite * 0.08)));
    p.setBrush(QColor(255, 196, 30));
    p.drawEllipse(c, unite * 0.44, unite * 0.44);
}

void lune(QPainter &p, const QRectF &r)
{
    const QRectF s = carre(r, 0.08);
    const double unite = s.width() / 2.0;
    QPainterPath disc;
    disc.addEllipse(s.center(), unite * 0.86, unite * 0.86);
    QPainterPath bite;
    bite.addEllipse(s.center() + QPointF(unite * 0.5, -unite * 0.34), unite * 0.72, unite * 0.72);
    p.setPen(QPen(QColor(150, 160, 190), std::max(1.0, unite * 0.08)));
    p.setBrush(QColor(236, 236, 214));
    p.drawPath(disc.subtracted(bite));
}

/// Rectangle 3 × 2 centré dans `r`, contour arrondi léger.
QRectF cadre_drapeau(const QRectF &r)
{
    const double w = std::min(r.width(), r.height() * 1.5) * 0.96;
    return QRectF(r.center() - QPointF(w / 2.0, w / 3.0), QSizeF(w, w * 2.0 / 3.0));
}

void france(QPainter &p, const QRectF &r)
{
    const QRectF f = cadre_drapeau(r);
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

void royaume_uni(QPainter &p, const QRectF &r)
{
    const QRectF f = cadre_drapeau(r);
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
void pastille(QPainter &p, const QRectF &r, Status kind)
{
    const QRectF s = carre(r, 0.06);
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

QIcon commande(Commande commande)
{
    return QIcon(new PaintedEngine([commande](QPainter &p, const QRectF &r, QIcon::Mode mode) {
        const QRectF s = carre(r, 0.04);
        p.translate(s.topLeft());
        p.scale(s.width() / 24, s.height() / 24);
        const QPalette palette = QGuiApplication::palette();
        const QColor trait = palette.color(mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::WindowText);
        const QColor accent = mode == QIcon::Selected ? trait :
            (est_sombre(palette) ? QColor(131, 202, 245) : QColor(40, 104, 153));
        const QPen bleu(accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(QPen(trait, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        switch (commande) {
        case Commande::Ouvrir:
            p.scale(1.5, 1.5);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(190, 122, 36));
            p.drawPolygon(QPolygonF{{0, 3}, {5, 3}, {7, 5}, {14, 5}, {14, 13}, {0, 13}});
            p.setBrush(QColor(255, 191, 77));
            p.drawPolygon(QPolygonF{{0, 13}, {2.5, 6}, {16, 6}, {13.5, 13}});
            break;
        case Commande::Enregistrer:
        case Commande::EnregistrerSous:
            p.save();
            if (commande == Commande::EnregistrerSous)
                p.scale(0.82, 0.82);
            p.scale(1.5, 1.5);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(65, 116, 179));
            p.drawPolygon(QPolygonF{{0, 0}, {12.5, 0}, {16, 3.5}, {16, 16}, {0, 16}});
            p.fillRect(QRectF(3, 0, 8.5, 6), QColor(208, 219, 229));
            p.fillRect(QRectF(8, 1, 2, 4), QColor(54, 73, 93));
            p.fillRect(QRectF(3, 9, 10, 6), QColor(249, 249, 245));
            p.fillRect(QRectF(5, 11, 6, 1), QColor(128, 155, 182));
            p.restore();
            if (commande == Commande::EnregistrerSous) {
                p.setPen(QPen(QColor(92, 63, 31), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(QColor(255, 197, 83));
                p.drawPolygon(QPolygonF{{11, 22}, {12, 17}, {20, 9}, {23, 12}, {15, 20}});
            }
            break;
        case Commande::ImageAnnotee:
            p.drawRoundedRect(QRectF(2, 3, 20, 18), 2, 2);
            p.drawPolyline(QPolygonF{{3, 18}, {8, 12}, {12, 16}, {15, 13}, {20, 19}});
            p.setPen(bleu);
            p.drawLine(13, 11, 19, 7);
            p.drawEllipse(QPointF(13, 11), 1.4, 1.4);
            p.drawEllipse(QPointF(19, 7), 1.4, 1.4);
            break;
        case Commande::Quitter:
            p.drawPolyline(QPolygonF{{11, 3}, {3, 3}, {3, 21}, {11, 21}});
            p.setPen(bleu);
            p.drawLine(9, 12, 21, 12);
            p.drawPolyline(QPolygonF{{16, 7}, {21, 12}, {16, 17}});
            break;
        case Commande::Notice:
            p.drawPolygon(QPolygonF{{2, 4}, {8, 4}, {12, 6}, {16, 4}, {22, 4},
                                    {22, 18}, {16, 18}, {12, 20}, {8, 18}, {2, 18}});
            p.setPen(bleu);
            p.drawLine(12, 6, 12, 19);
            break;
        case Commande::Etalonner:
            p.translate(12, 12);
            p.rotate(-35);
            p.drawRoundedRect(QRectF(-11, -4.5, 22, 9), 1, 1);
            p.setPen(bleu);
            for (int x : {-7, -3, 1, 5})
                p.drawLine(QPointF(x, -4), QPointF(x, -0.5));
            break;
        case Commande::Mesurer:
            p.drawLine(5, 18, 19, 6);
            p.setPen(bleu);
            p.setBrush(accent);
            p.drawEllipse(QPointF(4, 19), 2.3, 2.3);
            p.drawEllipse(QPointF(20, 5), 2.3, 2.3);
            break;
        case Commande::Profil:
            p.drawPolyline(QPolygonF{{3, 3}, {3, 21}, {22, 21}});
            p.setPen(bleu);
            p.drawPolyline(QPolygonF{{5, 16}, {9, 15}, {12, 6}, {15, 17}, {18, 11}, {21, 12}});
            break;
        }
    }));
}

QIcon theme(bool dark)
{
    return dessine_icone(dark ? lune : soleil);
}

QIcon drapeau(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return dessine_icone(france);
    case Lang::En: return dessine_icone(royaume_uni);
    }
    return {};
}

QIcon loupe(bool agrandir, const QColor &trait)
{
    return dessine_icone([agrandir, trait](QPainter &p, const QRectF &r) {
        const QRectF s = carre(r, 0.04);
        p.translate(s.topLeft());
        p.scale(s.width() / 20.0, s.height() / 20.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(trait, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(13.5, 13.5), QPointF(18.5, 18.5));
        p.setPen(QPen(trait, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(QPointF(8.5, 8.5), 7.0, 7.0);
        p.drawLine(QPointF(5.5, 8.5), QPointF(11.5, 8.5));
        if (agrandir)
            p.drawLine(QPointF(8.5, 5.5), QPointF(8.5, 11.5));
    });
}

QIcon fleche(QPointF sens, const QColor &trait)
{
    return dessine_icone([sens, trait](QPainter &p, const QRectF &r) {
        const QRectF s = carre(r, 0.04);
        p.translate(s.center());
        p.scale(s.width() / 20.0, s.height() / 20.0);
        p.setPen(QPen(trait, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF pointe = sens * 8;
        const QPointF cote(-sens.y(), sens.x());
        p.drawLine(-pointe, pointe);
        p.drawPolyline(QPolygonF{sens * 2 + cote * 6, pointe, sens * 2 - cote * 6});
    });
}

QIcon etat(Status kind)
{
    return dessine_icone([kind](QPainter &p, const QRectF &r) { pastille(p, r, kind); });
}

QIcon question()
{
    return dessine_icone([](QPainter &p, const QRectF &r) {
        const QRectF s = carre(r, 0.06);
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
