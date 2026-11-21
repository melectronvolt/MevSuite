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

QIcon dessine_icone(Drawing draw)
{
    return QIcon(new PaintedEngine(std::move(draw)));
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

void lune(QPainter &p, const QRectF &r)
{
    const QRectF s = carre(r, 0.08);
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

QIcon theme(bool dark)
{
    return dessine_icone(dark ? Drawing(lune) : Drawing(soleil));
}

QIcon drapeau(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return dessine_icone(france);
    case Lang::En: return dessine_icone(royaume_uni);
    }
    return {};
}

QIcon onglet_periode()
{
    return dessine_icone([](QPainter &p, const QRectF &r) {
        const QPalette pal = QApplication::palette();
        const QRectF s = carre(r, 0.06);
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
        p.setPen(QPen(couleur_curseur_t(pal), line * 1.2, Qt::SolidLine, Qt::RoundCap));
        for (double x : {x1, x1 + period})
            p.drawLine(QPointF(x, s.top() + u * 0.06), QPointF(x, s.bottom() - u * 0.04));
        // la durée mesurée, entre les deux curseurs
        const double y = s.bottom() - u * 0.14;
        p.drawLine(QPointF(x1, y), QPointF(x1 + period, y));
    });
}

QIcon onglet_frequences()
{
    return dessine_icone([](QPainter &p, const QRectF &r) {
        const QPalette pal = QApplication::palette();
        const QRectF s = carre(r, 0.06);
        const double u = s.width();
        const double line = std::max(1.0, u * 0.08);
        const double base = s.bottom() - u * 0.08;
        // raies de plus en plus faibles : la fondamentale et ses harmoniques
        p.setPen(QPen(couleur_spectre(pal), line * 1.6, Qt::SolidLine, Qt::RoundCap));
        const double heights[] = {0.78, 0.5, 0.32, 0.18};
        for (int k = 0; k < 4; ++k) {
            const double x = s.left() + u * (0.2 + 0.2 * k);
            p.drawLine(QPointF(x, base), QPointF(x, base - u * heights[k]));
        }
        p.setPen(QPen(pal.color(QPalette::WindowText), line, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(s.left() + u * 0.04, base), QPointF(s.right() - u * 0.04, base));
    });
}

QIcon dossier_ouvert()
{
    return dessine_icone([](QPainter &p, const QRectF &r) {
        const QRectF s = carre(r, 0.04);
        p.translate(s.topLeft());
        p.scale(s.width() / 16, s.height() / 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(190, 122, 36));
        p.drawPolygon(QPolygonF{{0, 3}, {5, 3}, {7, 5}, {14, 5}, {14, 13}, {0, 13}});
        p.setBrush(QColor(255, 191, 77));
        p.drawPolygon(QPolygonF{{0, 13}, {2.5, 6}, {16, 6}, {13.5, 13}});
    });
}

QIcon enregistrer()
{
    return dessine_icone([](QPainter &p, const QRectF &r) {
        p.setPen(Qt::NoPen);
        p.setBrush(couleur_erreur(QApplication::palette()));
        p.drawEllipse(carre(r, 0.10));
    });
}

QIcon disquette()
{
    return dessine_icone([](QPainter &p, const QRectF &r) {
        const QRectF s = carre(r, 0.07);
        p.translate(s.topLeft());
        p.scale(s.width() / 16, s.height() / 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(65, 116, 179));
        p.drawPolygon(QPolygonF{{0, 0}, {12.5, 0}, {16, 3.5}, {16, 16}, {0, 16}});
        p.setBrush(QColor(208, 219, 229));
        p.drawRect(QRectF(3, 0, 8.5, 6));
        p.setBrush(QColor(54, 73, 93));
        p.drawRect(QRectF(8, 1, 2, 4));
        p.setBrush(QColor(249, 249, 245));
        p.drawRect(QRectF(3, 9, 10, 6));
        p.setBrush(QColor(128, 155, 182));
        p.drawRect(QRectF(5, 11, 6, 1));
    });
}

QIcon commande(Commande commande)
{
    return dessine_icone([commande](QPainter &p, const QRectF &r) {
        const QRectF s = carre(r, 0.02);
        p.translate(s.topLeft());
        p.scale(s.width() / 24, s.height() / 24);
        const QPalette pal = QApplication::palette();
        const QColor accent = est_sombre(pal) ? QColor(131, 202, 245) : QColor(40, 104, 153);
        const QPen contour(pal.color(QPalette::WindowText), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        const QPen bleu(accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(contour);
        p.setBrush(Qt::NoBrush);
        switch (commande) {
        case Commande::Peripheriques: {
            QPainterPath arceau;
            arceau.moveTo(3, 15);
            arceau.lineTo(3, 12);
            arceau.cubicTo(3, 0, 21, 0, 21, 12);
            arceau.lineTo(21, 15);
            p.drawPath(arceau);
            p.setPen(bleu);
            p.setBrush(accent);
            p.drawRoundedRect(QRectF(3, 13, 4, 7), 1.5, 1.5);
            p.drawRoundedRect(QRectF(17, 13, 4, 7), 1.5, 1.5);
            break;
        }
        case Commande::ToutVoir:
            p.drawPolyline(QPolygonF{{3, 9}, {3, 4}, {8, 4}});
            p.drawPolyline(QPolygonF{{16, 4}, {21, 4}, {21, 9}});
            p.drawPolyline(QPolygonF{{21, 15}, {21, 20}, {16, 20}});
            p.drawPolyline(QPolygonF{{8, 20}, {3, 20}, {3, 15}});
            break;
        case Commande::ZoomExtrait:
            p.drawEllipse(QPointF(10, 10), 6.5, 6.5);
            p.drawLine(15, 15, 21, 21);
            p.setPen(bleu);
            p.drawLine(7, 10, 13, 10);
            p.drawLine(10, 7, 10, 13);
            break;
        case Commande::MemoriserSpectre:
        case Commande::RetirerSpectre:
            // Le même marque-page, avec un + ou un −.
            p.drawPolygon(QPolygonF{{5, 3}, {19, 3}, {19, 21}, {12, 17}, {5, 21}});
            p.setPen(bleu);
            p.drawLine(9, 10, 15, 10);
            if (commande == Commande::MemoriserSpectre)
                p.drawLine(12, 7, 12, 13);
            break;
        case Commande::Voie:
            for (int x : {2, 14}) {
                p.drawRoundedRect(QRectF(x, 4, 8, 16), 1.5, 1.5);
                p.drawPoint(x + 4, 8);
                p.drawEllipse(QPointF(x + 4, 15), 1.5, 1.5);
            }
            break;
        case Commande::Reglages:
            for (const QPoint centre : {QPoint(15, 5), QPoint(8, 12), QPoint(14, 19)}) {
                p.setPen(contour);
                p.drawLine(3, centre.y(), 21, centre.y());
                p.setPen(Qt::NoPen);
                p.setBrush(accent);
                p.drawRoundedRect(QRectF(centre.x() - 2, centre.y() - 3, 4, 6), 1, 1);
            }
            break;
        case Commande::ModeSimple:
        case Commande::ModeExpert:
            // Signal seul ou avec le panneau de réglages.
            p.drawRoundedRect(QRectF(2.5, 4, 19, 16), 2, 2);
            if (commande == Commande::ModeExpert)
                p.drawLine(9, 5, 9, 19);
            p.setPen(bleu);
            if (commande == Commande::ModeSimple)
                p.drawPolyline(QPolygonF{{5, 13}, {8, 13}, {11, 8}, {14, 16}, {17, 11}, {19, 11}});
            else {
                p.drawLine(12, 15, 12, 9);
                p.drawLine(16, 15, 16, 12);
            }
            break;
        case Commande::Notice:
            p.drawPolygon(QPolygonF{{2, 4}, {8, 4}, {12, 6}, {16, 4}, {22, 4},
                                    {22, 18}, {16, 18}, {12, 20}, {8, 18}, {2, 18}});
            p.setPen(bleu);
            p.drawLine(12, 6, 12, 19);
            break;
        case Commande::ChoisirPic:
            p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
            p.setPen(bleu);
            p.drawLine(12, 2, 12, 8);
            p.drawLine(12, 16, 12, 22);
            p.drawLine(2, 12, 8, 12);
            p.drawLine(16, 12, 22, 12);
            break;
        case Commande::DetectionAuto:
            p.drawArc(QRectF(4, 4, 16, 16), 55 * 16, -295 * 16);
            p.setPen(bleu);
            p.drawPolyline(QPolygonF{{3, 5}, {3, 11}, {9, 11}});
            break;
        }
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
