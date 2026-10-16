#include "icones.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <cmath>
#include <functional>
#include <numbers>

using namespace Qt::StringLiterals;

namespace icones {

namespace {

using Dessin = std::function<void(QPainter &, const QRectF &, QIcon::Mode)>;

/// Icône vectorielle : dessinée à la taille demandée, grisée quand le bouton est désactivé
class MoteurDessine : public QIconEngine {
public:
    explicit MoteurDessine(Dessin dessin)
        : m_dessin(std::move(dessin))
    {
    }

    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (mode == QIcon::Disabled)
            p->setOpacity(0.4);
        m_dessin(*p, QRectF(rect), mode);
        p->restore();
    }

    QPixmap pixmap(const QSize &taille, QIcon::Mode mode, QIcon::State etat) override
    {
        return scaledPixmap(taille, mode, etat, 1.0);
    }

    QPixmap scaledPixmap(const QSize &taille, QIcon::Mode mode, QIcon::State etat, qreal echelle) override
    {
        QPixmap image(taille * echelle);
        image.setDevicePixelRatio(echelle);
        image.fill(Qt::transparent);
        QPainter p(&image);
        paint(&p, QRect(QPoint(0, 0), taille), mode, etat);
        return image;
    }

    QIconEngine *clone() const override { return new MoteurDessine(m_dessin); }

private:
    Dessin m_dessin;
};

QIcon dessinee(Dessin dessin)
{
    return QIcon(new MoteurDessine(std::move(dessin)));
}

/// carré centré dans `r`, réduit de `marge` (fraction du côté)
QRectF carre(const QRectF &r, double marge)
{
    const double cote = std::min(r.width(), r.height()) * (1.0 - 2.0 * marge);
    return QRectF(r.center() - QPointF(cote, cote) / 2.0, QSizeF(cote, cote));
}

void soleil(QPainter &p, const QRectF &r)
{
    const QRectF s = carre(r, 0.04);
    const QPointF c = s.center();
    const double u = s.width() / 2.0;
    p.setPen(QPen(QColor(240, 160, 0), std::max(1.0, u * 0.16), Qt::SolidLine, Qt::RoundCap));
    for (int k = 0; k < 8; ++k) {
        const double a = k * std::numbers::pi / 4.0;
        const QPointF dir(std::cos(a), std::sin(a));
        p.drawLine(c + dir * u * 0.66, c + dir * u * 0.94);
    }
    p.setPen(QPen(QColor(200, 120, 0), std::max(1.0, u * 0.08)));
    p.setBrush(QColor(255, 196, 30));
    p.drawEllipse(c, u * 0.44, u * 0.44);
}

void lune(QPainter &p, const QRectF &r)
{
    const QRectF s = carre(r, 0.08);
    const double u = s.width() / 2.0;
    QPainterPath disque;
    disque.addEllipse(s.center(), u * 0.86, u * 0.86);
    QPainterPath morsure;
    morsure.addEllipse(s.center() + QPointF(u * 0.5, -u * 0.34), u * 0.72, u * 0.72);
    p.setPen(QPen(QColor(150, 160, 190), std::max(1.0, u * 0.08)));
    p.setBrush(QColor(236, 236, 214));
    p.drawPath(disque.subtracted(morsure));
}

/// rectangle 3 × 2 centré dans `r`
QRectF cadre_drapeau(const QRectF &r)
{
    const double w = std::min(r.width(), r.height() * 1.5) * 0.96;
    return QRectF(r.center() - QPointF(w / 2.0, w / 3.0), QSizeF(w, w * 2.0 / 3.0));
}

/// le contour arrondi d'un drapeau, une fois le dessin fait à l'intérieur
void bord_drapeau(QPainter &p, const QPainterPath &contour)
{
    p.setClipping(false);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(contour);
}

void france(QPainter &p, const QRectF &r)
{
    const QRectF f = cadre_drapeau(r);
    QPainterPath contour;
    contour.addRoundedRect(f, f.height() * 0.08, f.height() * 0.08);
    p.setClipPath(contour);
    const double tiers = f.width() / 3.0;
    p.fillRect(QRectF(f.left(), f.top(), tiers, f.height()), QColor(0, 85, 164));
    p.fillRect(QRectF(f.left() + tiers, f.top(), tiers, f.height()), Qt::white);
    p.fillRect(QRectF(f.left() + 2 * tiers, f.top(), tiers + 0.5, f.height()), QColor(239, 65, 53));
    bord_drapeau(p, contour);
}

void royaume_uni(QPainter &p, const QRectF &r)
{
    const QRectF f = cadre_drapeau(r);
    QPainterPath contour;
    contour.addRoundedRect(f, f.height() * 0.08, f.height() * 0.08);
    p.setClipPath(contour);
    p.fillRect(f, QColor(1, 33, 105));
    const double h = f.height();
    const QColor rouge(200, 16, 46);
    // croix de Saint-André (blanche) et de Saint-Patrick (rouge, plus fine)
    for (const auto &[largeur, couleur] : {std::pair{h * 0.2, QColor(Qt::white)}, std::pair{h * 0.08, rouge}}) {
        p.setPen(QPen(couleur, largeur, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(f.topLeft(), f.bottomRight());
        p.drawLine(f.bottomLeft(), f.topRight());
    }
    // croix de Saint-Georges, bordée de blanc
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRect(QRectF(f.center().x() - h * 0.17, f.top(), h * 0.34, h));
    p.drawRect(QRectF(f.left(), f.center().y() - h * 0.17, f.width(), h * 0.34));
    p.setBrush(rouge);
    p.drawRect(QRectF(f.center().x() - h * 0.1, f.top(), h * 0.2, h));
    p.drawRect(QRectF(f.left(), f.center().y() - h * 0.1, f.width(), h * 0.2));
    bord_drapeau(p, contour);
}

/// pastille ronde ou triangulaire, avec un symbole blanc
void pastille(QPainter &p, const QRectF &r, Statut s)
{
    const QRectF c = carre(r, 0.06);
    const double u = c.width();
    const QPen signe(Qt::white, std::max(1.2, u * 0.13), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (s) {
    case Statut::Info:
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 120, 210));
        p.drawEllipse(c);
        p.setPen(signe);
        p.drawLine(QPointF(c.center().x(), c.top() + u * 0.44), QPointF(c.center().x(), c.top() + u * 0.74));
        p.drawPoint(QPointF(c.center().x(), c.top() + u * 0.27));
        break;
    case Statut::Reussite:
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 160, 80));
        p.drawEllipse(c);
        p.setPen(signe);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF{c.topLeft() + QPointF(u * 0.27, u * 0.52), c.topLeft() + QPointF(u * 0.44, u * 0.68),
                                 c.topLeft() + QPointF(u * 0.74, u * 0.34)});
        break;
    case Statut::Attention: {
        QPainterPath triangle;
        triangle.moveTo(c.center().x(), c.top() + u * 0.04);
        triangle.lineTo(c.right(), c.bottom() - u * 0.06);
        triangle.lineTo(c.left(), c.bottom() - u * 0.06);
        triangle.closeSubpath();
        p.setPen(QPen(QColor(225, 140, 0), u * 0.06, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(245, 160, 0));
        p.drawPath(triangle);
        p.setPen(signe);
        p.drawLine(QPointF(c.center().x(), c.top() + u * 0.38), QPointF(c.center().x(), c.top() + u * 0.62));
        p.drawPoint(QPointF(c.center().x(), c.top() + u * 0.78));
        break;
    }
    case Statut::Erreur:
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(210, 45, 45));
        p.drawEllipse(c);
        p.setPen(signe);
        p.drawLine(c.topLeft() + QPointF(u * 0.32, u * 0.32), c.topLeft() + QPointF(u * 0.68, u * 0.68));
        p.drawLine(c.topLeft() + QPointF(u * 0.68, u * 0.32), c.topLeft() + QPointF(u * 0.32, u * 0.68));
        break;
    }
}

/// Un petit repère : l'origine du côté opposé aux flèches, les noms x et y au bout des axes
void petit_repere(QPainter &p, const QRectF &r, int sx, int sy, const QColor &couleur)
{
    const QRectF c = carre(r, 0.04);
    const double u = c.width();
    // l'origine, et le bout de chaque axe
    const QPointF o(sx > 0 ? c.left() + u * 0.2 : c.right() - u * 0.2, sy > 0 ? c.bottom() - u * 0.2 : c.top() + u * 0.2);
    const QPointF bx(sx > 0 ? c.right() - u * 0.08 : c.left() + u * 0.08, o.y());
    const QPointF by(o.x(), sy > 0 ? c.top() + u * 0.08 : c.bottom() - u * 0.08);
    const double pointe = u * 0.16;
    const double epaisseur = std::max(1.0, u * 0.06);
    for (const QPointF &bout : {bx, by}) {
        const QPointF dir = (bout - o) / std::hypot(bout.x() - o.x(), bout.y() - o.y());
        const QPointF ortho(-dir.y(), dir.x());
        p.setPen(QPen(couleur, epaisseur, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(o, bout - dir * pointe * 0.6);
        p.setPen(Qt::NoPen);
        p.setBrush(couleur);
        p.drawPolygon(QPolygonF({bout, bout - dir * pointe + ortho * pointe * 0.45, bout - dir * pointe - ortho * pointe * 0.45}));
    }
    QFont police = QApplication::font();
    police.setPixelSize(std::max(7, static_cast<int>(std::lround(u * 0.3))));
    p.setFont(police);
    p.setPen(couleur);
    const double t = u * 0.3;
    // « x » sous la flèche des abscisses (au-dessus si l'axe est en bas), « y » à côté de celle des
    // ordonnées, du côté intérieur
    p.drawText(QRectF(bx.x() - (sx > 0 ? t : 0), o.y() + (sy > 0 ? -t - u * 0.04 : u * 0.04), t, t), Qt::AlignCenter, u"x"_s);
    p.drawText(QRectF(o.x() + (sx > 0 ? u * 0.04 : -t - u * 0.04), by.y() - (sy > 0 ? 0 : t), t, t), Qt::AlignCenter, u"y"_s);
    p.setPen(Qt::NoPen);
    p.drawEllipse(o, u * 0.05, u * 0.05);
}

} // namespace

QIcon theme(bool sombre)
{
    return dessinee([sombre](QPainter &p, const QRectF &r, QIcon::Mode) { sombre ? lune(p, r) : soleil(p, r); });
}

QIcon drapeau(Langue l)
{
    return dessinee([l](QPainter &p, const QRectF &r, QIcon::Mode) {
        if (l == Langue::Francais)
            france(p, r);
        else
            royaume_uni(p, r);
    });
}

QIcon statut(Statut s)
{
    return dessinee([s](QPainter &p, const QRectF &r, QIcon::Mode) { pastille(p, r, s); });
}

QIcon question()
{
    return dessinee([](QPainter &p, const QRectF &r, QIcon::Mode) {
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

QIcon onglet(Onglet o)
{
    return dessinee([o](QPainter &p, const QRectF &r, QIcon::Mode mode) {
        const QPalette pal = QApplication::palette();
        const QColor trait = mode == QIcon::Selected ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::WindowText);
        const QColor rouge(220, 40, 40);
        const QColor bleu = pal.color(QPalette::Highlight);
        const QRectF s = carre(r, 0.08);
        const double u = s.width();
        const double e = std::max(1.0, u * 0.08);
        const auto point = [&](QPointF c, double rayon, const QColor &couleur) {
            p.setPen(Qt::NoPen);
            p.setBrush(couleur);
            p.drawEllipse(c, rayon, rayon);
        };
        p.setBrush(Qt::NoBrush);
        switch (o) {
        case Onglet::Pointage: {
            // une mire : un cercle, quatre traits qui ne touchent pas le centre, le point visé
            const QPointF c = s.center();
            p.setPen(QPen(trait, e, Qt::SolidLine, Qt::RoundCap));
            p.drawEllipse(c, u * 0.32, u * 0.32);
            for (const QPointF d : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)})
                p.drawLine(c + d * (u * 0.16), c + d * (u * 0.48));
            point(c, u * 0.08, rouge);
            break;
        }
        case Onglet::Coordonnees: {
            // un tableau : un cadre, la ligne des titres teintée, deux colonnes, des lignes
            const QRectF t = s.adjusted(u * 0.04, u * 0.1, -u * 0.04, -u * 0.1);
            QColor teinte = trait;
            teinte.setAlphaF(0.25f);
            p.fillRect(QRectF(t.topLeft(), QSizeF(t.width(), t.height() * 0.25)), teinte);
            p.setPen(QPen(trait, e));
            p.drawRect(t);
            p.drawLine(QPointF(t.center().x(), t.top()), QPointF(t.center().x(), t.bottom()));
            for (double k : {0.25, 0.5, 0.75})
                p.drawLine(QPointF(t.left(), t.top() + t.height() * k), QPointF(t.right(), t.top() + t.height() * k));
            break;
        }
        case Onglet::Trajectoires: {
            // des points le long d'une parabole, comme une chronophotographie
            QPainterPath courbe;
            const auto sur = [&](double x) { return QPointF(s.left() + x * u, s.top() + u * (0.2 + 2.4 * (x - 0.5) * (x - 0.5))); };
            courbe.moveTo(sur(0.08));
            for (int k = 1; k <= 20; ++k)
                courbe.lineTo(sur(0.08 + 0.84 * k / 20.0));
            QPen pointille(trait, std::max(1.0, u * 0.05), Qt::DashLine);
            p.setPen(pointille);
            p.drawPath(courbe);
            for (int k = 0; k < 5; ++k)
                point(sur(0.1 + 0.2 * k), u * 0.1, rouge);
            break;
        }
        case Onglet::Grapheur: {
            // deux axes, et une courbe qui monte
            p.setPen(QPen(trait, e, Qt::SolidLine, Qt::RoundCap));
            const QPointF o0(s.left() + u * 0.12, s.bottom() - u * 0.12);
            p.drawLine(o0, QPointF(o0.x(), s.top() + u * 0.04));
            p.drawLine(o0, QPointF(s.right() - u * 0.04, o0.y()));
            QPainterPath courbe;
            courbe.moveTo(o0 + QPointF(u * 0.06, -u * 0.08));
            courbe.cubicTo(o0 + QPointF(u * 0.4, -u * 0.08), o0 + QPointF(u * 0.5, -u * 0.62),
                           QPointF(s.right() - u * 0.06, s.top() + u * 0.12));
            p.setPen(QPen(bleu, std::max(1.0, u * 0.1), Qt::SolidLine, Qt::RoundCap));
            p.drawPath(courbe);
            break;
        }
        }
    });
}

QIcon repere(int sens_x, int sens_y)
{
    return dessinee([sens_x, sens_y](QPainter &p, const QRectF &r, QIcon::Mode mode) {
        const QPalette pal = QApplication::palette();
        const QColor couleur = mode == QIcon::Selected ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::ButtonText);
        petit_repere(p, r, sens_x, sens_y, couleur);
    });
}

} // namespace icones
