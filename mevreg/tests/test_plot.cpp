// Moteur de tracé : mise en page, barres d'incertitude au pixel près, exports.
// Avec MEVREG_SHOTS=dossier, les images de contrôle sont enregistrées dans ce dossier.

#include "core/workspace.h"
#include "plot/export.h"
#include "plot/renderer.h"
#include "plot/report.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

GraphView view(int plots)
{
    GraphView v = GraphView::make(1, u"G"_s);
    v.setGrid(plots, 1);
    return v;
}

Series series(std::vector<double> xs, std::vector<double> ys)
{
    Series s;
    s.xs = std::move(xs);
    s.ys = std::move(ys);
    s.legend = u"y"_s;
    return s;
}

struct Rendered {
    QImage image;
    std::vector<PlotLayout> layouts;
};

/// Rendu à l'échelle 1 : un pixel d'image par pixel logique.
Rendered render(const GraphView &v, const std::vector<PlotData> &data, QSize size, double scale = 1.0)
{
    Rendered r;
    r.image = QImage(size * scale, QImage::Format_ARGB32);
    r.image.fill(Qt::white);
    QPainter p(&r.image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    RenderOptions o;
    o.style = NumberStyle{true};
    o.devicePixelRatio = scale;
    r.layouts = renderGraph(p, v, data, QSizeF(size), o);
    p.end();
    return r;
}

void shot(const QImage &img, const QString &name)
{
    const QString dir = qEnvironmentVariable("MEVREG_SHOTS");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    img.save(QDir(dir).filePath(name + u".png"_s));
}

/// Le pixel est-il de la couleur donnée (à l'anticrénelage près) ?
bool near(QRgb px, QColor c, int tol = 40)
{
    return std::abs(qRed(px) - c.red()) <= tol && std::abs(qGreen(px) - c.green()) <= tol && std::abs(qBlue(px) - c.blue()) <= tol;
}

/// Lignes de la colonne `x` dont le pixel a la couleur `c`, entre y0 et y1.
std::vector<int> coloredRows(const QImage &img, int x, int y0, int y1, QColor c)
{
    std::vector<int> rows;
    for (int y = std::max(y0, 0); y <= std::min(y1, img.height() - 1); ++y) {
        if (near(img.pixel(x, y), c))
            rows.push_back(y);
    }
    return rows;
}

const QColor RED(214, 39, 40);

} // namespace

class TestPlot : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void layoutMapsDataInsideArea()
    {
        PlotData d;
        d.series = {series({0, 1, 2}, {0, 10, 5})};
        d.xName = u"t"_s;
        d.xUnit = u"s"_s;
        d.yName = u"x"_s;
        d.yUnit = u"m"_s;
        const auto r = render(view(1), {d}, QSize(600, 400));
        const PlotLayout &l = r.layouts[0];
        QVERIFY(l.area.left() > 30 && l.area.right() < 600 && l.area.bottom() < 400);
        QVERIFY(l.x.toPx(0) > l.area.left() && l.x.toPx(2) < l.area.right());
        QVERIFY(l.y.toPx(10) > l.area.top() && l.y.toPx(0) < l.area.bottom());
        QVERIFY(std::abs(l.x.fromPx(l.x.toPx(1.3)) - 1.3) < 1e-9);
        shot(r.image, u"mise-en-page"_s);
        // légendes d'axes automatiques, dans le SVG
        QTemporaryDir dir;
        Workspace ws;
        const QString path = dir.filePath(u"g.svg"_s);
        GraphView v = view(1);
        const QId t = *ws.doc.addExperimental(u"t"_s, u"s"_s, {0, 1, 2});
        const QId x = *ws.doc.addExperimental(u"x"_s, u"m"_s, {0, 10, 5});
        v.plots[0].addCurve(t, x);
        QString err;
        QVERIFY(exporter::writeGraph(path, exporter::Format::Svg, v, ws, QSizeF(600, 400), NumberStyle{}, &err));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString svg = QString::fromUtf8(f.readAll());
        QVERIFY(svg.contains(u"t (s)"_s));
        QVERIFY(svg.contains(u"x (m)"_s));
    }

    void errorBarsHaveRightSizeOnRightAxis()
    {
        GraphView v = view(1);
        CurveSpec c;
        c.side = YSide::Right;
        v.plots[0].curves.push_back(c);
        Series left = series({0, 10}, {0, 100});
        left.color = Color::rgb(31, 119, 180);
        left.legend.reset();
        Series right;
        right.xs = {5};
        right.ys = {2};
        right.uy = {0.5};
        right.errors = ErrorBars::Vertical;
        right.side = YSide::Right;
        right.marker = Marker::None;
        right.color = Color::from(RED);
        right.capSize = 0.0;
        PlotData d;
        d.series = {left, right};
        const auto r = render(v, {d}, QSize(500, 400));
        const PlotLayout &l = r.layouts[0];
        QVERIFY(l.y2);
        const int px = static_cast<int>(std::floor(l.x.toPx(5.0)));
        const auto rows = coloredRows(r.image, px, 0, 399, RED);
        QVERIFY(!rows.empty());
        // barre verticale de y2(2,5) à y2(1,5), sur l'axe de droite et non celui de gauche
        QVERIFY(std::abs(rows.front() - l.y2->toPx(2.5)) <= 1.0);
        QVERIFY(std::abs(rows.back() - l.y2->toPx(1.5)) <= 1.0);
        QVERIFY(std::abs(l.y2->toPx(2.0) - l.y.toPx(2.0)) > 10.0);
        // un seul pixel de large, net
        QVERIFY(!near(r.image.pixel(px - 1, rows[rows.size() / 2]), RED, 80));
        QVERIFY(!near(r.image.pixel(px + 1, rows[rows.size() / 2]), RED, 80));
    }

    void errorBarsStopAtMarkerEdgeAndHaveCaps()
    {
        GraphView v = view(1);
        v.plots[0].x.autoScale = false;
        v.plots[0].x.min = 0;
        v.plots[0].x.max = 10;
        v.plots[0].y.autoScale = false;
        v.plots[0].y.min = 0;
        v.plots[0].y.max = 10;
        v.plots[0].x.gridMajor = v.plots[0].y.gridMajor = false;
        Series s;
        s.xs = {5};
        s.ys = {5};
        s.uy = {2};
        s.ux = {2};
        s.errors = ErrorBars::Both;
        s.marker = Marker::Circle;
        s.markerSize = 12;
        s.color = Color::from(RED);
        s.capSize = 4.0;
        PlotData d;
        d.series = {s};
        const auto r = render(v, {d}, QSize(400, 400));
        shot(r.image, u"barre-et-marqueur"_s);
        const PlotLayout &l = r.layouts[0];
        const int cx = static_cast<int>(std::floor(l.x.toPx(5))), cy = static_cast<int>(std::floor(l.y.toPx(5)));
        const int top = static_cast<int>(std::lround(l.y.toPx(7)));
        // la barre arrive à son embout…
        QVERIFY(near(r.image.pixel(cx, top + 2), RED));
        // …l'embout a la largeur demandée (4 px de part et d'autre)…
        QVERIFY(near(r.image.pixel(cx - 4, top), RED) || near(r.image.pixel(cx - 4, top + 1), RED) || near(r.image.pixel(cx - 4, top - 1), RED));
        QVERIFY(!near(r.image.pixel(cx - 7, top), RED, 80));
        // …et elle s'arrête au bord du rond : l'intérieur du marqueur reste vide
        QVERIFY(!near(r.image.pixel(cx, cy - 2), RED, 80));
        QVERIFY(!near(r.image.pixel(cx + 2, cy), RED, 80));
        // barre horizontale symétrique
        const int right = static_cast<int>(std::lround(l.x.toPx(7)));
        QVERIFY(near(r.image.pixel(right - 2, cy), RED));
    }

    void asymmetricErrorBars()
    {
        // une grandeur pour chaque côté : la barre va plus loin vers le haut que vers le bas, le
        // rectangle de même ; l'échelle automatique compte le côté le plus long
        GraphView v = view(1);
        Plot &pl = v.plots[0];
        pl.x.autoScale = false;
        pl.x.min = 0;
        pl.x.max = 10;
        pl.y.autoScale = false;
        pl.y.min = 0;
        pl.y.max = 10;
        pl.x.gridMajor = pl.y.gridMajor = false;
        Series s;
        s.xs = {5};
        s.ys = {5};
        s.uyHigh = {3};
        s.uyLow = {1};
        s.errors = ErrorBars::Vertical;
        s.marker = Marker::None;
        s.color = Color::from(RED);
        s.capSize = 0.0;
        PlotData d;
        d.series = {s};
        auto r = render(v, {d}, QSize(400, 400));
        shot(r.image, u"barre-dissymetrique"_s);
        const PlotLayout l = r.layouts[0];
        const int cx = static_cast<int>(std::floor(l.x.toPx(5)));
        QVERIFY(near(r.image.pixel(cx, static_cast<int>(std::lround(l.y.toPx(7.8)))), RED));
        QVERIFY(near(r.image.pixel(cx, static_cast<int>(std::lround(l.y.toPx(4.2)))), RED));
        QVERIFY(!near(r.image.pixel(cx, static_cast<int>(std::lround(l.y.toPx(3.6)))), RED, 80));
        // échelle automatique : de 4 à 8
        pl.y.autoScale = true;
        r = render(v, {d}, QSize(400, 400));
        const PlotLayout &la = r.layouts[0];
        QVERIFY(la.y.fromPx(la.area.top()) >= 8.0 && la.y.fromPx(la.area.top()) < 8.5);
        QVERIFY(la.y.fromPx(la.area.bottom()) <= 4.0 && la.y.fromPx(la.area.bottom()) > 3.5);
    }

    void modelFunctionSpansTheGraph()
    {
        // la courbe d'un modèle se prolonge jusqu'aux bords du graphe, pas seulement sur les points
        GraphView v = view(1);
        Plot &pl = v.plots[0];
        pl.x.autoScale = pl.y.autoScale = false;
        pl.x.min = -10;
        pl.x.max = 10;
        pl.y.min = -10;
        pl.y.max = 10;
        pl.x.gridMajor = pl.y.gridMajor = false;
        Series s;
        s.xs = {0, 1};
        s.ys = {0, 0};
        s.marker = Marker::None;
        s.line = LineKind::Straight;
        s.lineWidth = 2.0;
        s.color = Color::from(RED);
        s.autoscale = false;
        s.function = [](const std::vector<double> &x) { return std::vector<double>(x.size(), 0.0); };
        PlotData d;
        d.series = {s};
        auto r = render(v, {d}, QSize(400, 300));
        const PlotLayout l = r.layouts[0];
        const int y0 = static_cast<int>(std::floor(l.y.toPx(0)));
        auto red = [&](double x) { return near(r.image.pixel(static_cast<int>(std::lround(l.x.toPx(x))), y0), RED) || near(r.image.pixel(static_cast<int>(std::lround(l.x.toPx(x))), y0 + 1), RED); };
        QVERIFY(red(-9) && red(9));
        // limitée de −2 à 2
        d.series[0].domain = std::pair(-2.0, 2.0);
        r = render(v, {d}, QSize(400, 300));
        QVERIFY(red(-1.5) && !red(-5) && !red(5));
    }

    void scaledErrorBarsStayCentered()
    {
        // agrandissement de l'interface (à l'écran) : barres, embouts et marqueurs agrandis, les
        // embouts toujours centrés sur la barre au pixel près, la barre arrêtée au bord du rond
        GraphView v = view(1);
        Plot &pl = v.plots[0];
        pl.x.autoScale = pl.y.autoScale = false;
        pl.x.min = pl.y.min = 0;
        pl.x.max = pl.y.max = 10;
        pl.x.gridMajor = pl.y.gridMajor = false;
        pl.x.gridMinor = pl.y.gridMinor = false;
        Series s;
        s.xs = {5};
        s.ys = {5};
        s.uy = {2};
        s.ux = {2};
        s.errors = ErrorBars::Both;
        s.marker = Marker::Circle;
        s.markerSize = 12;
        s.color = Color::from(RED);
        PlotData d;
        d.series = {s};
        /// Pixels rouges consécutifs d'une ligne (ou colonne) autour de `at` : premier, dernier.
        const auto run = [](const QImage &img, int fixed, int at, bool row) {
            const auto red = [&](int k) { return near(row ? img.pixel(k, fixed) : img.pixel(fixed, k), RED, 60); };
            int a = at, b = at;
            while (a > 0 && red(a - 1))
                --a;
            while (b < (row ? img.width() : img.height()) - 1 && red(b + 1))
                ++b;
            return std::pair(a, b);
        };
        for (const double scale : {1.0, 1.25, 1.5, 2.0, 3.0}) {
            QImage img(QSize(600, 600), QImage::Format_ARGB32);
            img.fill(Qt::white);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            RenderOptions o;
            o.style = NumberStyle{true};
            const auto layouts = renderGraphScaled(p, v, {d}, QSizeF(600, 600), o, scale);
            p.end();
            shot(img, u"barres-agrandies-%1"_s.arg(std::lround(scale * 100)));
            const PlotLayout &l = layouts.front();
            const QString at = u" à %1 %"_s.arg(scale * 100);
            // barre verticale : sa colonne, entre le rond et l'embout du haut
            const int mid = static_cast<int>(std::lround(l.y.toPx(6.0)));
            const int cx = static_cast<int>(std::floor(l.x.toPx(5.0)));
            int bx = -1;
            for (int x = cx - 3; x <= cx + 3 && bx < 0; ++x)
                if (near(img.pixel(x, mid), RED, 60))
                    bx = x;
            QVERIFY2(bx >= 0, qPrintable(u"barre verticale"_s + at));
            const auto [ba, bb] = run(img, mid, bx, true);
            // embout du haut : la plus longue ligne rouge près de y(7)
            const int top = static_cast<int>(std::lround(l.y.toPx(7.0)));
            int best = -1, ca = 0, cb = 0;
            for (int y = top - 3; y <= top + 3; ++y) {
                if (!near(img.pixel(bx, y), RED, 60))
                    continue;
                const auto [a, b] = run(img, y, bx, true);
                if (b - a > best) {
                    best = b - a;
                    ca = a;
                    cb = b;
                }
            }
            QVERIFY2(best > bb - ba, qPrintable(u"embout"_s + at));
            QVERIFY2(ba - ca == cb - bb, qPrintable(u"embout décentré%1 : %2..%3 pour la barre %4..%5"_s.arg(at).arg(ca).arg(cb).arg(ba).arg(bb)));
            // embout de 6 px de part et d'autre (moitié du marqueur), agrandi
            QVERIFY2(std::abs((ba - ca) - 6.0 * scale) <= 1.5, qPrintable(u"longueur de l'embout"_s + at));
            // la barre s'arrête au bord du rond : l'intérieur du marqueur reste vide
            const int cy = static_cast<int>(std::floor(l.y.toPx(5.0)));
            QVERIFY2(!near(img.pixel(bx, cy - static_cast<int>(2 * scale)), RED, 80), qPrintable(u"barre dans le marqueur"_s + at));
            // barre horizontale : embout de droite centré de même
            const int midX = static_cast<int>(std::lround(l.x.toPx(6.0)));
            int by = -1;
            for (int y = cy - 3; y <= cy + 3 && by < 0; ++y)
                if (near(img.pixel(midX, y), RED, 60))
                    by = y;
            QVERIFY2(by >= 0, qPrintable(u"barre horizontale"_s + at));
            const auto [ha, hb] = run(img, midX, by, false);
            const int right = static_cast<int>(std::lround(l.x.toPx(7.0)));
            best = -1;
            for (int x = right - 3; x <= right + 3; ++x) {
                if (!near(img.pixel(x, by), RED, 60))
                    continue;
                const auto [a, b] = run(img, x, by, false);
                if (b - a > best) {
                    best = b - a;
                    ca = a;
                    cb = b;
                }
            }
            QVERIFY2(ha - ca == cb - hb, qPrintable(u"embout horizontal décentré"_s + at));
        }
    }

    void errorBarsAreClippedWithoutCaps()
    {
        GraphView v = view(1);
        Plot &p = v.plots[0];
        p.y.autoScale = false;
        p.y.min = 0;
        p.y.max = 10;
        p.x.gridMajor = p.y.gridMajor = false;
        Series s;
        s.xs = {1, 2, 3};
        s.ys = {9, 5, 1};
        s.uy = {3};
        s.errors = ErrorBars::Vertical;
        s.marker = Marker::None;
        s.color = Color::from(RED);
        s.capSize = 5.0;
        PlotData d;
        d.series = {s};
        const auto r = render(v, {d}, QSize(400, 300));
        const PlotLayout &l = r.layouts[0];
        const int cx = static_cast<int>(std::floor(l.x.toPx(1)));
        const int areaTop = static_cast<int>(std::ceil(l.area.top()));
        // la barre du premier point dépasse en haut : coupée au cadre, sans embout
        QVERIFY(near(r.image.pixel(cx, areaTop + 3), RED));
        QVERIFY(!near(r.image.pixel(cx - 4, areaTop + 1), RED, 80));
        // celle du deuxième point a ses deux embouts
        const int cx2 = static_cast<int>(std::floor(l.x.toPx(2)));
        const int y8 = static_cast<int>(std::lround(l.y.toPx(8)));
        bool cap = false;
        for (int dy = -1; dy <= 1; ++dy)
            cap = cap || near(r.image.pixel(cx2 - 4, y8 + dy), RED);
        QVERIFY(cap);
        shot(r.image, u"barres-coupees"_s);
    }

    void logScaleBarsReachTheBottom()
    {
        GraphView v = view(1);
        v.plots[0].y.log = true;
        Series s;
        s.xs = {1, 2, 3};
        s.ys = {1, 10, 100};
        s.uy = {2, 5, 50};
        s.errors = ErrorBars::Vertical;
        s.marker = Marker::Disc;
        s.color = Color::from(RED);
        PlotData d;
        d.series = {s};
        const auto r = render(v, {d}, QSize(400, 300));
        const PlotLayout &l = r.layouts[0];
        QVERIFY(l.y.min > 0);
        // y − u ≤ 0 : la barre descend jusqu'au bas du cadre
        const int cx = static_cast<int>(std::floor(l.x.toPx(1)));
        QVERIFY(near(r.image.pixel(cx, static_cast<int>(l.area.bottom()) - 2), RED));
        shot(r.image, u"echelle-log"_s);
    }

    void rectanglesAreTranslucent()
    {
        GraphView v = view(1);
        v.plots[0].x.gridMajor = v.plots[0].y.gridMajor = false;
        Series s;
        s.xs = {1, 2, 3, 4};
        s.ys = {1, 2, 3, 4};
        s.ux = {0.2};
        s.uy = {0.4};
        s.errors = ErrorBars::Rectangles;
        s.marker = Marker::Plus;
        s.color = Color::from(RED);
        PlotData d;
        d.series = {s};
        const auto r = render(v, {d}, QSize(400, 300));
        const PlotLayout &l = r.layouts[0];
        // intérieur du rectangle : teinte claire, ni blanc ni rouge franc
        const QRgb in = r.image.pixel(static_cast<int>(l.x.toPx(2.1)), static_cast<int>(l.y.toPx(2.25)));
        QVERIFY(qRed(in) > 230 && qGreen(in) < 245 && qGreen(in) > 150);
        shot(r.image, u"rectangles"_s);
    }

    void gridAlignmentAndLinkedAxes()
    {
        GraphView v = view(2);
        v.linkX = true;
        PlotData d1, d2;
        d1.series = {series({0, 1}, {0, 1})};
        // libellés plus larges sur le second graphe
        d2.series = {series({0, 4}, {-123456, 1e5})};
        const auto r = render(v, {d1, d2}, QSize(600, 600));
        QCOMPARE(r.layouts[0].area.left(), r.layouts[1].area.left());
        QCOMPARE(r.layouts[0].area.right(), r.layouts[1].area.right());
        QCOMPARE(r.layouts[0].x.min, r.layouts[1].x.min);
        QCOMPARE(r.layouts[0].x.max, r.layouts[1].x.max);
    }

    void emptyAndDegenerateDataDoNotCrash()
    {
        std::vector<PlotData> cases(5);
        cases[1].series = {series({}, {})};
        cases[2].series = {series({1}, {1})};
        cases[3].series = {series({NAN, 1}, {2, NAN})};
        Series cubic = series({1, 1, 1}, {1, 2, 3});
        cubic.line = LineKind::Cubic;
        cases[4].series = {cubic};
        for (const auto &d : cases)
            render(view(1), {d}, QSize(300, 200));
        GraphView v = view(1);
        v.plots[0].y.log = true;
        PlotData d;
        d.series = {series({1, 2, 3}, {-1, 10, 100})};
        const auto r = render(v, {d}, QSize(300, 200));
        QVERIFY(r.layouts[0].y.min > 0);
        // fenêtre minuscule
        render(view(2), {d, d}, QSize(20, 20));
    }

    void exportsAndReport()
    {
        QTemporaryDir dir;
        Workspace ws;
        const QId t = *ws.doc.addExperimental(u"t"_s, u"s"_s, {0, 0.1, 0.2, 0.3});
        const QId z = *ws.doc.addExperimental(u"z"_s, u"m"_s, {1.5, 1.45, 1.3, 1.06});
        ws.doc.createUncertainty(z, u"0.02"_s);
        ws.meta.title = u"Chute libre"_s;
        ws.meta.authors = u"A. Élève"_s;
        const ViewId g = ws.addGraph();
        ws.graphMut(g)->plots[0].addCurve(t, z).errors = ErrorBars::Vertical;
        QString err;
        for (auto [name, fmt] : {std::make_pair(u"g.png"_s, exporter::Format::Png), std::make_pair(u"g.svg"_s, exporter::Format::Svg),
                                 std::make_pair(u"g.pdf"_s, exporter::Format::Pdf)}) {
            const QString path = dir.filePath(name);
            QVERIFY2(exporter::writeGraph(path, fmt, *ws.graph(g), ws, QSizeF(700, 450), NumberStyle{}, &err), qPrintable(err));
            QVERIFY(QFile(path).size() > 1000);
        }
        QFile pdf(dir.filePath(u"g.pdf"_s));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.read(4) == "%PDF");
        const QImage png(dir.filePath(u"g.png"_s));
        QCOMPARE(png.width(), 2100);
        // compte rendu de plusieurs pages, en-têtes de tableau répétés
        report::PageWriter w(u"TP"_s, u"Noms"_s);
        QStringList col;
        for (int i = 0; i < 120; ++i)
            col << QString::number(i);
        w.table({u"t (s)"_s, u"x (m)"_s}, {col, col});
        w.code(u"import numpy as np\nprint(np.pi)\n"_s);
        w.graph(*ws.graph(g), exporter::resolveAll(*ws.graph(g), ws.doc, NumberStyle{}), 300, NumberStyle{});
        QVERIFY(w.pageCount() >= 3);
        QVERIFY2(w.writePdf(dir.filePath(u"cr.pdf"_s), u"TP"_s, &err), qPrintable(err));
        QVERIFY(QFile(dir.filePath(u"cr.pdf"_s)).size() > 2000);
    }

    void showcase()
    {
        // image de contrôle : mesures avec incertitudes, modèle, double axe, légende
        Workspace ws;
        std::vector<double> i, u;
        for (int k = 0; k < 10; ++k) {
            i.push_back(k * 0.01);
            u.push_back(100.0 * k * 0.01 + 0.03 * std::sin(k * 1.7));
        }
        const QId iq = *ws.doc.addExperimental(u"I"_s, u"A"_s, i);
        const QId uq = *ws.doc.addExperimental(u"U"_s, u"V"_s, u);
        ws.doc.createUncertainty(iq, u"0.002"_s);
        ws.doc.createUncertainty(uq, u"0.01*U + 0.02"_s);
        const ViewId g = ws.addGraph();
        Plot &p = ws.graphMut(g)->plots[0];
        p.title.text = u"Caractéristique U = f(I)"_s;
        CurveSpec &c = p.addCurve(iq, uq);
        c.errors = ErrorBars::Both;
        c.marker = Marker::Circle;
        p.x.includeZero = p.y.includeZero = true;
        fit::ModelKind lin;
        lin.type = fit::ModelKind::Linear;
        p.fits.push_back(FitSpec::make(lin, iq, uq, PALETTE[3]));
        updateFits(p, ws.doc);
        const QImage img = exporter::graphImage(*ws.graph(g), ws.doc, QSizeF(720, 460), 2.0, NumberStyle{});
        QVERIFY(!img.isNull());
        shot(img, u"vitrine"_s);
    }
};

QTEST_MAIN(TestPlot)
#include "test_plot.moc"
