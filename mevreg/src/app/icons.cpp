#include "app/icons.h"

#include "app/theme.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

using namespace Qt::StringLiterals;

namespace icons {

namespace {

using Draw = std::function<void(QPainter &, const QColor &)>;

QPainterPath trait(QPointF a, QPointF b)
{
    QPainterPath p;
    p.moveTo(a);
    p.lineTo(b);
    return p;
}

QPainterPath polygone(std::initializer_list<QPointF> pts, bool closed = false)
{
    QPainterPath p;
    bool first = true;
    for (const QPointF &q : pts) {
        if (first)
            p.moveTo(q);
        else
            p.lineTo(q);
        first = false;
    }
    if (closed)
        p.closeSubpath();
    return p;
}

void contour(QPainter &p, const QPainterPath &path, const QColor &c, double w = 1.7)
{
    QPen pen(c, w);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void remplit(QPainter &p, const QPainterPath &path, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(path);
}

void texte(QPainter &p, const QRectF &r, const QString &s, const QColor &c, double size, bool bold = true, bool italic = false)
{
    QFont f(u"DejaVu Sans"_s);
    f.setPixelSize(static_cast<int>(size));
    f.setBold(bold);
    f.setItalic(italic);
    p.setFont(f);
    p.setPen(c);
    p.drawText(r, Qt::AlignCenter, s);
}

QPainterPath cercle(QPointF c, double r)
{
    QPainterPath p;
    p.addEllipse(c, r, r);
    return p;
}

QPainterPath rectangle_arrondi(double x, double y, double w, double h, double r = 2.0)
{
    QPainterPath p;
    p.addRoundedRect(QRectF(x, y, w, h), r, r);
    return p;
}

QPainterPath pointe_fleche(QPointF tip, QPointF from, double len = 4.0)
{
    const double dx = tip.x() - from.x(), dy = tip.y() - from.y();
    const double l = std::hypot(dx, dy);
    const double ux = dx / l, uy = dy / l;
    return polygone({QPointF(tip.x() - ux * len - uy * len * 0.8, tip.y() - uy * len + ux * len * 0.8), tip,
                 QPointF(tip.x() - ux * len + uy * len * 0.8, tip.y() - uy * len - ux * len * 0.8)});
}

Draw panneaux(int rows, int cols)
{
    return [rows, cols](QPainter &p, const QColor &c) {
        contour(p, rectangle_arrondi(3, 4, 18, 16), c);
        for (int i = 1; i < rows; ++i) {
            const qreal y = 4 + 16.0 * i / rows;
            contour(p, trait({3, y}, {21, y}), c);
        }
        for (int j = 1; j < cols; ++j) {
            const qreal x = 3 + 18.0 * j / cols;
            contour(p, trait({x, 4}, {x, 20}), c);
        }
    };
}

const QHash<QString, Draw> &glyphes()
{
    static const QHash<QString, Draw> g = {
        {u"plus"_s, [](QPainter &p, const QColor &c) { contour(p, trait({12, 5}, {12, 19}), c); contour(p, trait({5, 12}, {19, 12}), c); }},
        {u"minus"_s, [](QPainter &p, const QColor &c) { contour(p, trait({5, 12}, {19, 12}), c); }},
        {u"function"_s, [](QPainter &p, const QColor &c) { texte(p, QRectF(0, 0, 24, 24), u"ƒ"_s, c, 19, false, true); }},
        {u"table"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(3.5, 4.5, 17, 15), c);
             contour(p, trait({3.5, 9.5}, {20.5, 9.5}), c);
             contour(p, trait({3.5, 14.5}, {20.5, 14.5}), c, 1.2);
             contour(p, trait({10, 4.5}, {10, 19.5}), c);
         }},
        {u"chart"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{4, 4}, {4, 20}, {20, 20}}), c);
             contour(p, polygone({{7, 15}, {11, 10}, {14, 13}, {19.5, 6}}), c);
         }},
        {u"scatter"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{4, 4}, {4, 20}, {20, 20}}), c);
             for (QPointF q : {QPointF(8, 15), QPointF(11, 11), QPointF(15, 12), QPointF(18, 7)})
                 remplit(p, cercle(q, 1.6), c);
         }},
        {u"python"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{14, 3.5}, {6, 3.5}, {6, 20.5}, {18, 20.5}, {18, 7.5}, {14, 3.5}}, true), c, 1.5);
             texte(p, QRectF(5, 9, 14, 11), u"py"_s, c, 8.5);
         }},
        {u"list"_s,
         [](QPainter &p, const QColor &c) {
             for (double y : {7.0, 12.0, 17.0}) {
                 remplit(p, cercle({5, y}, 1.3), c);
                 contour(p, trait({9, y}, {20, y}), c);
             }
         }},
        {u"crosshair"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, cercle({12, 12}, 6.5), c);
             contour(p, trait({12, 2.5}, {12, 8}), c);
             contour(p, trait({12, 16}, {12, 21.5}), c);
             contour(p, trait({2.5, 12}, {8, 12}), c);
             contour(p, trait({16, 12}, {21.5, 12}), c);
         }},
        {u"zoom"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, cercle({10.5, 10.5}, 6), c);
             contour(p, trait({15, 15}, {20, 20}), c, 2.2);
             contour(p, trait({10.5, 7.8}, {10.5, 13.2}), c, 1.4);
             contour(p, trait({7.8, 10.5}, {13.2, 10.5}), c, 1.4);
         }},
        {u"pan"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, trait({12, 3}, {12, 21}), c);
             contour(p, trait({3, 12}, {21, 12}), c);
             contour(p, pointe_fleche({12, 3}, {12, 8}, 3.5), c);
             contour(p, pointe_fleche({12, 21}, {12, 16}, 3.5), c);
             contour(p, pointe_fleche({3, 12}, {8, 12}, 3.5), c);
             contour(p, pointe_fleche({21, 12}, {16, 12}, 3.5), c);
         }},
        {u"move-point"_s,
         [](QPainter &p, const QColor &c) {
             remplit(p, cercle({8, 8}, 3.2), c);
             contour(p, trait({11, 11}, {19.5, 19.5}), c);
             contour(p, pointe_fleche({19.5, 19.5}, {14, 14}, 4), c);
         }},
        {u"range"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, trait({4, 5}, {4, 19}), c);
             contour(p, trait({20, 5}, {20, 19}), c);
             contour(p, trait({6.5, 12}, {17.5, 12}), c);
             contour(p, pointe_fleche({6.5, 12}, {10, 12}, 3.2), c);
             contour(p, pointe_fleche({17.5, 12}, {14, 12}, 3.2), c);
         }},
        {u"fit"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{4, 9}, {4, 4}, {9, 4}}), c);
             contour(p, polygone({{15, 4}, {20, 4}, {20, 9}}), c);
             contour(p, polygone({{20, 15}, {20, 20}, {15, 20}}), c);
             contour(p, polygone({{9, 20}, {4, 20}, {4, 15}}), c);
         }},
        {u"grid"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(4, 4, 7, 7, 1.5), c);
             contour(p, rectangle_arrondi(13, 4, 7, 7, 1.5), c);
             contour(p, rectangle_arrondi(4, 13, 7, 7, 1.5), c);
             contour(p, rectangle_arrondi(13, 13, 7, 7, 1.5), c);
         }},
        {u"link"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(3, 9, 10, 6, 3), c);
             contour(p, rectangle_arrondi(11, 9, 10, 6, 3), c);
         }},
        {u"unlink"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(2.5, 9, 8.5, 6, 3), c);
             contour(p, rectangle_arrondi(13, 9, 8.5, 6, 3), c);
             contour(p, trait({12, 4}, {12, 7}), c, 1.4);
             contour(p, trait({12, 17}, {12, 20}), c, 1.4);
         }},
        {u"export"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{10, 5}, {5, 5}, {5, 19}, {19, 19}, {19, 14}}), c);
             contour(p, trait({11, 13}, {19.5, 4.5}), c);
             contour(p, polygone({{14, 4.5}, {19.5, 4.5}, {19.5, 10}}), c);
         }},
        {u"sliders"_s,
         [](QPainter &p, const QColor &c) {
             const double ys[] = {6, 12, 18}, xs[] = {15, 8, 13};
             for (int k = 0; k < 3; ++k) {
                 contour(p, trait({4, ys[k]}, {20, ys[k]}), c, 1.4);
                 remplit(p, cercle({xs[k], ys[k]}, 2.4), c);
             }
         }},
        {u"model"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath curve;
             curve.moveTo(4, 19);
             curve.cubicTo(9, 18, 12, 6, 20, 5);
             contour(p, curve, c);
             for (QPointF q : {QPointF(6, 16.5), QPointF(10.5, 14), QPointF(13, 8.5), QPointF(17.5, 7)})
                 remplit(p, cercle(q, 1.5), c);
         }},
        {u"trash"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, trait({4, 7}, {20, 7}), c);
             contour(p, polygone({{9, 7}, {9, 4.5}, {15, 4.5}, {15, 7}}), c);
             contour(p, polygone({{6, 7}, {7, 20}, {17, 20}, {18, 7}}), c);
             contour(p, trait({10, 10.5}, {10, 16.5}), c, 1.3);
             contour(p, trait({14, 10.5}, {14, 16.5}), c, 1.3);
         }},
        {u"play"_s, [](QPainter &p, const QColor &c) { remplit(p, polygone({{7, 4.5}, {19, 12}, {7, 19.5}}, true), c); }},
        {u"stop"_s, [](QPainter &p, const QColor &c) { remplit(p, rectangle_arrondi(6, 6, 12, 12, 1.5), c); }},
        {u"save"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{4, 4}, {16.5, 4}, {20, 7.5}, {20, 20}, {4, 20}}, true), c);
             contour(p, polygone({{8, 4}, {8, 9}, {15, 9}, {15, 4}}), c, 1.4);
             contour(p, rectangle_arrondi(7.5, 13, 9, 7, 1), c, 1.4);
         }},
        {u"open"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{3.5, 18.5}, {3.5, 5}, {9, 5}, {11, 7.5}, {18.5, 7.5}, {18.5, 10}}), c);
             contour(p, polygone({{3.5, 18.5}, {6.5, 10.5}, {21, 10.5}, {18, 18.5}}, true), c);
         }},
        {u"new"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{14, 3.5}, {6, 3.5}, {6, 20.5}, {18, 20.5}, {18, 7.5}, {14, 3.5}}, true), c);
             contour(p, trait({12, 10}, {12, 17}), c, 1.4);
             contour(p, trait({8.5, 13.5}, {15.5, 13.5}), c, 1.4);
         }},
        {u"undo"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath a;
             a.moveTo(5, 9);
             a.lineTo(14, 9);
             a.cubicTo(22, 9, 22, 19, 14, 19);
             a.lineTo(9, 19);
             contour(p, a, c);
             contour(p, polygone({{9, 5}, {5, 9}, {9, 13}}), c);
         }},
        {u"redo"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath a;
             a.moveTo(19, 9);
             a.lineTo(10, 9);
             a.cubicTo(2, 9, 2, 19, 10, 19);
             a.lineTo(15, 19);
             contour(p, a, c);
             contour(p, polygone({{15, 5}, {19, 9}, {15, 13}}), c);
         }},
        {u"stats"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, trait({3.5, 20}, {20.5, 20}), c);
             remplit(p, rectangle_arrondi(5, 12, 3.5, 8, 0.8), c);
             remplit(p, rectangle_arrondi(10.25, 6, 3.5, 14, 0.8), c);
             remplit(p, rectangle_arrondi(15.5, 9.5, 3.5, 10.5, 0.8), c);
         }},
        {u"copy"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(8.5, 8.5, 11, 12), c);
             contour(p, polygone({{15.5, 8.5}, {15.5, 4}, {4.5, 4}, {4.5, 16}, {8.5, 16}}), c);
         }},
        {u"paste"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(5, 5, 14, 16), c);
             contour(p, rectangle_arrondi(9, 3, 6, 4, 1), c, 1.4);
         }},
        {u"print"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{7, 9}, {7, 3.5}, {17, 3.5}, {17, 9}}), c);
             contour(p, polygone({{7, 17}, {3.5, 17}, {3.5, 9}, {20.5, 9}, {20.5, 17}, {17, 17}}), c);
             contour(p, rectangle_arrondi(7, 13.5, 10, 7, 0.8), c);
         }},
        {u"help"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, cercle({12, 12}, 8.5), c);
             texte(p, QRectF(4, 3.5, 16, 17), u"?"_s, c, 13);
         }},
        {u"book"_s,
         [](QPainter &p, const QColor &c) {
             // livre ouvert : deux pages qui se rejoignent à la reliure
             contour(p, polygone({{12, 7.5}, {9, 5.5}, {3.5, 5.5}, {3.5, 17.5}, {9, 17.5}, {12, 19.5}}), c);
             contour(p, polygone({{12, 7.5}, {15, 5.5}, {20.5, 5.5}, {20.5, 17.5}, {15, 17.5}, {12, 19.5}}), c);
             contour(p, trait({12, 7.5}, {12, 19.5}), c);
         }},
        {u"info"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, cercle({12, 12}, 8.5), c);
             contour(p, trait({12, 11}, {12, 16.5}), c, 2);
             remplit(p, cercle({12, 7.7}, 1.2), c);
         }},
        {u"warning"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{12, 3.5}, {21, 19.5}, {3, 19.5}}, true), c);
             contour(p, trait({12, 9}, {12, 14}), c, 2);
             remplit(p, cercle({12, 16.8}, 1.1), c);
         }},
        {u"check"_s, [](QPainter &p, const QColor &c) { contour(p, polygone({{4.5, 12.5}, {9.5, 17.5}, {19.5, 6.5}}), c, 2); }},
        {u"restore"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath arc;
             arc.arcMoveTo(QRectF(4.5, 4.5, 15, 15), 154);
             arc.arcTo(QRectF(4.5, 4.5, 15, 15), 154, -302);
             contour(p, arc, c);
             contour(p, polygone({{3, 4}, {4, 9}, {9, 8}}), c);
             contour(p, polygone({{12, 7.5}, {12, 12}, {15.5, 14}}), c);
         }},
        {u"save-as"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{9, 20}, {4, 20}, {4, 4}, {16, 4}, {19, 7}, {19, 9}}), c);
             contour(p, polygone({{7, 4}, {7, 9}, {14, 9}, {14, 4}}), c);
             contour(p, polygone({{11, 20}, {12, 16}, {18.5, 9.5}, {21.5, 12.5}, {15, 19}, {11, 20}}, true), c);
         }},
        {u"steps"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{3, 18}, {8, 18}, {8, 13}, {13, 13}, {13, 8}, {20, 8}}), c, 2);
         }},
        {u"derivative2"_s, [](QPainter &p, const QColor &c) { texte(p, QRectF(0, 2, 24, 19), u"f″"_s, c, 14); }},
        {u"vline"_s, [](QPainter &p, const QColor &c) { contour(p, trait({12, 3}, {12, 21}), c, 2); }},
        {u"hline"_s, [](QPainter &p, const QColor &c) { contour(p, trait({3, 12}, {21, 12}), c, 2); }},
        {u"text"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, trait({5, 5}, {19, 5}), c, 2);
             contour(p, trait({12, 5}, {12, 19}), c, 2);
             contour(p, trait({8.5, 19}, {15.5, 19}), c, 2);
         }},
        {u"image"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(3, 4, 18, 16), c);
             remplit(p, cercle({8, 9}, 1.6), c);
             contour(p, polygone({{4, 17}, {9, 13}, {12, 15}, {16, 10}, {20.5, 15}}), c);
         }},
        {u"vector"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath curve({4, 18});
             curve.cubicTo({4, 5}, {20, 19}, {20, 6});
             contour(p, curve, c);
             contour(p, rectangle_arrondi(2.5, 17, 3, 3, 0), c);
             contour(p, rectangle_arrondi(18.5, 4, 3, 3, 0), c);
         }},
        {u"pdf"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{14, 3.5}, {6, 3.5}, {6, 20.5}, {18, 20.5}, {18, 7.5}, {14, 3.5}}, true), c, 1.5);
             texte(p, QRectF(3, 10, 18, 9), u"PDF"_s, c, 6.5);
         }},
        {u"csv"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{14, 3.5}, {6, 3.5}, {6, 20.5}, {18, 20.5}, {18, 7.5}, {14, 3.5}}, true), c, 1.5);
             texte(p, QRectF(3, 10, 18, 9), u"CSV"_s, c, 6.5);
         }},
        // chiffres en grand, traits épais : lisibles à la taille d'une ligne de texte
        {u"numbers"_s, [](QPainter &p, const QColor &c) { texte(p, QRectF(-2, 1, 28, 22), u"123"_s, c, 12.5); }},
        {u"row-insert"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(2.5, 13, 19, 8), c, 2.0);
             contour(p, trait({12, 2}, {12, 10}), c, 2.4);
             contour(p, trait({8, 6}, {16, 6}), c, 2.4);
         }},
        {u"row-delete"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(2.5, 13, 19, 8), c, 2.0);
             contour(p, trait({8, 6}, {16, 6}), c, 2.4);
         }},
        {u"dots"_s,
         [](QPainter &p, const QColor &c) {
             for (double x : {6.0, 12.0, 18.0})
                 remplit(p, cercle({x, 12}, 1.7), c);
         }},
        {u"gear"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath g;
             for (int k = 0; k < 8; ++k) {
                 const double a0 = k * M_PI / 4 - 0.22, a1 = k * M_PI / 4 + 0.22;
                 const double a2 = k * M_PI / 4 + M_PI / 8;
                 if (k == 0)
                     g.moveTo(12 + 8.5 * std::cos(a0), 12 + 8.5 * std::sin(a0));
                 g.lineTo(12 + 8.5 * std::cos(a0), 12 + 8.5 * std::sin(a0));
                 g.lineTo(12 + 8.5 * std::cos(a1), 12 + 8.5 * std::sin(a1));
                 g.lineTo(12 + 6.2 * std::cos(a2 - 0.1), 12 + 6.2 * std::sin(a2 - 0.1));
                 g.lineTo(12 + 6.2 * std::cos(a2 + 0.1 + M_PI / 8 - 0.22), 12 + 6.2 * std::sin(a2 + 0.1 + M_PI / 8 - 0.22));
             }
             g.closeSubpath();
             contour(p, g, c, 1.4);
             contour(p, cercle({12, 12}, 2.8), c, 1.4);
         }},
        {u"wave"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath w;
             for (int k = 0; k <= 40; ++k) {
                 const double x = 3 + 18.0 * k / 40;
                 const double y = 12 - 6 * std::sin((x - 3) / 18 * 4 * M_PI);
                 if (k == 0)
                     w.moveTo(x, y);
                 else
                     w.lineTo(x, y);
             }
             contour(p, w, c);
         }},
        {u"flask"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{9.5, 3.5}, {9.5, 10}, {4.5, 19}, {5.5, 20.5}, {18.5, 20.5}, {19.5, 19}, {14.5, 10}, {14.5, 3.5}}), c);
             contour(p, trait({8, 3.5}, {16, 3.5}), c);
             contour(p, trait({7, 15}, {17, 15}), c, 1.2);
         }},
        {u"pencil"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{15.5, 4.5}, {19.5, 8.5}, {8.5, 19.5}, {4, 20}, {4.5, 15.5}}, true), c);
             contour(p, trait({13, 7}, {17, 11}), c, 1.3);
         }},
        {u"eye-off"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath e;
             e.moveTo(3, 12);
             e.cubicTo(7, 5.5, 17, 5.5, 21, 12);
             e.cubicTo(17, 18.5, 7, 18.5, 3, 12);
             contour(p, e, c);
             contour(p, cercle({12, 12}, 2.8), c, 1.4);
             contour(p, trait({4, 20}, {20, 4}), c);
         }},
        {u"plusminus"_s, [](QPainter &p, const QColor &c) { texte(p, QRectF(0, 0, 24, 24), u"±"_s, c, 18, false); }},
        {u"snowflake"_s,
         [](QPainter &p, const QColor &c) {
             for (int k = 0; k < 3; ++k) {
                 const double a = k * M_PI / 3 + M_PI / 2;
                 contour(p, trait({12 - 8.5 * std::cos(a), 12 - 8.5 * std::sin(a)}, {12 + 8.5 * std::cos(a), 12 + 8.5 * std::sin(a)}), c, 1.5);
             }
         }},
        {u"paint"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{19.5, 4.5}, {11, 13}}), c, 2);
             QPainterPath b;
             b.moveTo(10.5, 13);
             b.cubicTo(7, 12.5, 6, 15, 6, 17);
             b.cubicTo(6, 19, 4.5, 19.5, 3.5, 19.5);
             b.cubicTo(7, 21.5, 12, 20, 12, 16);
             b.closeSubpath();
             contour(p, b, c, 1.4);
         }},
        {u"terminal"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(3, 4.5, 18, 15), c);
             contour(p, polygone({{7, 9.5}, {10, 12}, {7, 14.5}}), c, 1.5);
             contour(p, trait({12, 15}, {16.5, 15}), c, 1.5);
         }},
        {u"integral"_s, [](QPainter &p, const QColor &c) { texte(p, QRectF(0, 0, 24, 24), u"∫"_s, c, 19, false); }},
        {u"derivative"_s, [](QPainter &p, const QColor &c) { texte(p, QRectF(0, 0, 24, 24), u"d/dx"_s, c, 8.5); }},
        {u"tangent"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath s;
             s.moveTo(3, 19);
             s.cubicTo(12, 19, 12, 5, 21, 5);
             contour(p, s, c);
             contour(p, trait({4, 14}, {14, 4}), c, 1.2);
             contour(p, trait({10, 20}, {20, 10}), c, 1.2);
         }},
        {u"close"_s, [](QPainter &p, const QColor &c) { contour(p, trait({6, 6}, {18, 18}), c); contour(p, trait({18, 6}, {6, 18}), c); }},
        // cadenas fermé (verrouillé), ouvert (libre)
        {u"lock"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(5, 10.5, 14, 10), c);
             QPainterPath arc;
             arc.moveTo(8, 10.5);
             arc.lineTo(8, 7.5);
             arc.arcTo(QRectF(8, 3.5, 8, 8), 180, -180);
             arc.lineTo(16, 10.5);
             contour(p, arc, c);
             remplit(p, cercle({12, 15.5}, 1.4), c);
         }},
        {u"unlock"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(5, 10.5, 14, 10), c);
             QPainterPath arc;
             arc.moveTo(8, 10.5);
             arc.lineTo(8, 6.5);
             arc.arcTo(QRectF(8, 2.5, 8, 8), 180, -180);
             arc.lineTo(16, 7.5);
             contour(p, arc, c);
             remplit(p, cercle({12, 15.5}, 1.4), c);
         }},
        // droite oblique avec ses deux poignées
        {u"slope"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, trait({3.5, 20.5}, {20.5, 3.5}), c);
             contour(p, cercle({8, 16}, 2.2), c, 1.4);
             contour(p, cercle({16, 8}, 2.2), c, 1.4);
         }},
        // fenêtre ouverte en haut à droite, flèche qui en sort (détacher) ou qui y entre (rattacher)
        {u"detach"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{10, 4.5}, {4.5, 4.5}, {4.5, 19.5}, {19.5, 19.5}, {19.5, 14}}), c);
             contour(p, trait({11, 13}, {19.5, 4.5}), c);
             contour(p, polygone({{13.5, 4.5}, {19.5, 4.5}, {19.5, 10.5}}), c);
         }},
        {u"attach"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{10, 4.5}, {4.5, 4.5}, {4.5, 19.5}, {19.5, 19.5}, {19.5, 14}}), c);
             contour(p, trait({19.5, 4.5}, {11, 13}), c);
             contour(p, polygone({{11, 7}, {11, 13}, {17, 13}}), c);
         }},
        {u"swap"_s, [](QPainter &p, const QColor &c) {
             contour(p, polygone({{3, 8}, {20, 8}, {16, 4}}), c);
             contour(p, polygone({{21, 16}, {4, 16}, {8, 20}}), c);
         }},
        {u"sort"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, trait({7, 4}, {7, 20}), c);
             contour(p, polygone({{3.5, 16.5}, {7, 20}, {10.5, 16.5}}), c);
             contour(p, trait({13, 6}, {20, 6}), c, 1.4);
             contour(p, trait({13, 11}, {18, 11}), c, 1.4);
             contour(p, trait({13, 16}, {16, 16}), c, 1.4);
         }},
        {u"eraser"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, polygone({{9, 20}, {4, 15}, {14, 5}, {20, 11}, {11, 20}, {20, 20}}), c);
             contour(p, trait({8.5, 10.5}, {14.5, 16.5}), c, 1.3);
         }},
        {u"eye"_s,
         [](QPainter &p, const QColor &c) {
             QPainterPath e;
             e.moveTo(3, 12);
             e.cubicTo(7, 5.5, 17, 5.5, 21, 12);
             e.cubicTo(17, 18.5, 7, 18.5, 3, 12);
             contour(p, e, c);
             contour(p, cercle({12, 12}, 2.8), c, 1.4);
         }},
        {u"1x1"_s, panneaux(1, 1)}, {u"2x1"_s, panneaux(2, 1)}, {u"1x2"_s, panneaux(1, 2)},
        {u"3x1"_s, panneaux(3, 1)}, {u"2x2"_s, panneaux(2, 2)},
        {u"layout"_s,
         [](QPainter &p, const QColor &c) {
             contour(p, rectangle_arrondi(3.5, 4.5, 17, 15), c);
             contour(p, trait({9, 4.5}, {9, 19.5}), c, 1.4);
             contour(p, trait({15, 4.5}, {15, 19.5}), c, 1.4);
         }},
    };
    return g;
}

/// Moteur d'icône : dessin vectoriel à la taille demandée, couleur du thème.
class GlyphEngine : public QIconEngine {
public:
    explicit GlyphEngine(QString name) : m_name(std::move(name)) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const auto it = glyphes().find(m_name);
        if (it == glyphes().end())
            return;
        QColor c = QApplication::palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::ButtonText);
        if (mode == QIcon::Selected)
            c = QApplication::palette().color(QPalette::HighlightedText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);
        const double s = std::min(rect.width(), rect.height()) / 24.0;
        painter->translate(rect.x() + (rect.width() - 24 * s) / 2, rect.y() + (rect.height() - 24 * s) / 2);
        painter->scale(s, s);
        (*it)(*painter, c);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm(size * scale);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(scale);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override { return new GlyphEngine(m_name); }
    QString key() const override { return u"mevreg-glyph"_s; }

private:
    QString m_name;
};

} // namespace

QIcon obtient(const char *name)
{
    const QString key = QString::fromLatin1(name);
    return glyphes().contains(key) ? QIcon(new GlyphEngine(key)) : QIcon{};
}

// --- Icônes communes aux logiciels de la suite (mêmes dessins que MeVSono) --------------------

namespace {

using Drawing = std::function<void(QPainter &, const QRectF &)>;

/// Icône vectorielle en couleurs : dessinée à la taille demandée, grisée quand le bouton est désactivé.
class PaintedEngine : public QIconEngine {
public:
    explicit PaintedEngine(Drawing draw) : m_draw(std::move(draw)) {}

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

/// Pastille ronde ou triangulaire colorée, avec un symbole blanc.
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
