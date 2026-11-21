#include "app/graphwidget.h"

#include "app/dialogs.h"
#include "app/graphsettings.h"
#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/modelingpanel.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "plot/export.h"

#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QDockWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

using namespace Qt::StringLiterals;

namespace graphops {

void echelle_auto(Plot &p)
{
    p.x.autoScale = true;
    p.y.autoScale = true;
    p.y2.autoScale = true;
}

void synchronise_x(GraphView &g, int p)
{
    if (!g.linkX || p < 0 || static_cast<size_t>(p) >= g.plots.size())
        return;
    const AxisSpec x = g.plots[p].x;
    for (size_t i = 0; i < g.plots.size(); ++i) {
        if (static_cast<int>(i) == p)
            continue;
        g.plots[i].x.autoScale = x.autoScale;
        g.plots[i].x.min = x.min;
        g.plots[i].x.max = x.max;
    }
}

namespace {

/// Courbe y(x) ajoutée ; une incertitude déjà rattachée se voit tout de suite.
void ajoute_courbe(Seance &ws, Plot &pl, QId x, QId y, bool right)
{
    CurveSpec &c = pl.ajoute_courbe(x, y);
    if (right)
        c.side = YSide::Right;
    const Grandeur *yq = ws.doc.obtient(y);
    const Grandeur *xq = ws.doc.obtient(x);
    if (yq && yq->uncertainty)
        c.errors = (xq && xq->uncertainty) ? ErrorBars::Both : ErrorBars::Vertical;
    else if (xq && xq->uncertainty)
        c.errors = ErrorBars::Both;
}

} // namespace

void depose_grandeur(MainWindow &win, ViewId graph, int pi, QId q, std::optional<int> zone)
{
    Store &store = win.depot();
    const QString name = store.ws.doc.nom(q);
    std::optional<QId> pending;
    if (auto it = store.pendingAbscissa.find(graph); it != store.pendingAbscissa.end()) {
        pending = it->second;
        store.pendingAbscissa.erase(it);
    }
    GraphView *g = store.ws.graphe_modifiable(graph);
    if (!g || pi < 0 || static_cast<size_t>(pi) >= g->plots.size())
        return;
    Plot &plot = g->plots[pi];
    auto askOrdinate = [&] {
        store.pendingAbscissa[graph] = q;
        store.message(u"Abscisse : %1. Ajoutez maintenant la grandeur en ordonnée."_s.arg(name), icons::Status::Info);
    };
    if (zone == 0) {
        if (plot.curves.empty()) {
            askOrdinate();
        } else {
            store.edite([&](Seance &ws) {
                Plot &pl = ws.graphe_modifiable(graph)->plots[pi];
                for (auto &c : pl.curves)
                    c.x = q;
                pl.x.autoScale = true;
            });
        }
        return;
    }
    std::optional<QId> x = plot.curves.empty() ? pending : std::optional<QId>(plot.curves.front().x);
    if (!x || *x == q) {
        askOrdinate();
        return;
    }
    if (std::any_of(plot.curves.begin(), plot.curves.end(), [&](const CurveSpec &c) { return c.x == *x && c.y == q; })) {
        store.message(u"%1 est déjà tracée sur ce graphe"_s.arg(name), icons::Status::Info);
        return;
    }
    store.edite([&](Seance &ws) {
        Plot &pl = ws.graphe_modifiable(graph)->plots[pi];
        ajoute_courbe(ws, pl, *x, q, zone == 2);
        pl.y.autoScale = true;
        pl.y2.autoScale = true;
    });
}

void depose_grandeurs(MainWindow &win, ViewId graph, int pi, const std::vector<QId> &ids, std::optional<int> zone)
{
    if (ids.size() == 1)
        depose_grandeur(win, graph, pi, ids.front(), zone);
    if (ids.size() < 2)
        return;
    // plusieurs colonnes à la fois : la première en abscisse, les suivantes en ordonnées
    Store &store = win.depot();
    store.pendingAbscissa.erase(graph);
    const GraphView *g = store.ws.graphe(graph);
    if (!g || pi < 0 || static_cast<size_t>(pi) >= g->plots.size())
        return;
    const QId x = ids.front();
    std::vector<QId> ys;
    for (size_t k = 1; k < ids.size(); ++k) {
        const QId y = ids[k];
        const auto &curves = g->plots[pi].curves;
        if (y != x && std::find(ys.begin(), ys.end(), y) == ys.end()
            && std::none_of(curves.begin(), curves.end(), [&](const CurveSpec &c) { return c.x == x && c.y == y; }))
            ys.push_back(y);
    }
    if (ys.empty()) {
        store.message(u"Ces courbes sont déjà tracées sur ce graphe"_s, icons::Status::Info);
        return;
    }
    QStringList names;
    store.edite([&](Seance &ws) {
        Plot &pl = ws.graphe_modifiable(graph)->plots[pi];
        for (QId y : ys) {
            ajoute_courbe(ws, pl, x, y, zone == 2);
            names << ws.doc.nom(y);
        }
        pl.x.autoScale = true;
        pl.y.autoScale = true;
        pl.y2.autoScale = true;
    });
    store.message(u"Tracé en fonction de %1 : %2"_s.arg(store.ws.doc.nom(x), names.join(u", "_s)), icons::Status::Success);
}

} // namespace graphops

namespace {

void zoome_axe(AxisSpec &spec, const AxisMap &m, double anchor, double f)
{
    if (!std::isfinite(anchor))
        return;
    if (m.log) {
        if (anchor <= 0.0)
            return;
        const double a = std::log10(anchor), lo = std::log10(m.min), hi = std::log10(m.max);
        spec.min = std::pow(10.0, a - (a - lo) * f);
        spec.max = std::pow(10.0, a + (hi - a) * f);
    } else {
        spec.min = anchor - (anchor - m.min) * f;
        spec.max = anchor + (m.max - anchor) * f;
    }
    spec.autoScale = false;
}

void deplace_axe(AxisSpec &spec, const AxisMap &m, double d)
{
    const double lo = m.depuis_px(m.a - d), hi = m.depuis_px(m.b - d);
    if (std::isfinite(lo) && std::isfinite(hi) && lo != hi) {
        spec.min = std::min(lo, hi);
        spec.max = std::max(lo, hi);
        spec.autoScale = false;
    }
}

void definit_plage(AxisSpec &spec, double a, double b)
{
    if (std::isfinite(a) && std::isfinite(b) && a != b) {
        spec.min = std::min(a, b);
        spec.max = std::max(a, b);
        spec.autoScale = false;
    }
}

QString nom_unite(const Document &doc, QId q, QString *unit)
{
    const Grandeur *x = doc.obtient(q);
    if (!x) {
        *unit = QString();
        return u"?"_s;
    }
    *unit = x->unit.isEmpty() ? QString() : u" "_s + unite_lisible(x->unit);
    return x->name;
}

QString nom_axe(const QString &name, const QString &fallback)
{
    return name.isEmpty() || name.contains(u',') ? fallback : name;
}

} // namespace

// --- zone de dessin ---------------------------------------------------------------------------

GraphCanvas::GraphCanvas(MainWindow &win, ViewId id, QWidget *parent) : QWidget(parent), m_win(win), m_id(id)
{
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::ClickFocus);
    theme::definit_taille_min(this, QSize(200, 150));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    connect(&win.depot(), &Store::espace_travail_change, this, &GraphCanvas::invalide);
    connect(&win.depot(), &Store::affichage_change, this, &GraphCanvas::invalide);
}

void GraphCanvas::invalide()
{
    m_dirty = true;
    update();
}

void GraphCanvas::choisit_outil(Tool t)
{
    m_tool = t;
    setCursor(curseur_outil());
    update();
}

QCursor GraphCanvas::curseur_outil() const
{
    return m_tool == Tool::MovePoints ? Qt::OpenHandCursor : m_tool == Tool::Pan ? Qt::SizeAllCursor : Qt::CrossCursor;
}

// --- annotations à la souris -------------------------------------------------------------------

std::optional<GraphCanvas::AnnotationHit> GraphCanvas::annotation_en(int plot, QPointF p) const
{
    const GraphView *view = m_win.depot().ws.graphe(m_id);
    if (!view || static_cast<size_t>(plot) >= m_layouts.size() || static_cast<size_t>(plot) >= view->plots.size())
        return std::nullopt;
    const PlotLayout &l = m_layouts[static_cast<size_t>(plot)];
    if (!l.area.adjusted(-4, -4, 4, 4).contains(p))
        return std::nullopt;
    const double z = theme::zoom();
    const double tol = 6.0 * z, grab = 9.0 * z;
    const auto &as = view->plots[static_cast<size_t>(plot)].annotations;
    // les dernières sont dessinées par-dessus : cherchées d'abord
    for (size_t k = as.size(); k-- > 0;) {
        const Annotation &a = as[k];
        const AxisMap &ym = l.echelle_y(a.side);
        switch (a.kind) {
        case Annotation::VLine:
            if (l.x.est_valide(a.x) && std::abs(l.x.vers_px(a.x) - p.x()) <= tol)
                return AnnotationHit{k, 0};
            break;
        case Annotation::HLine:
            if (ym.est_valide(a.y) && std::abs(ym.vers_px(a.y) - p.y()) <= tol)
                return AnnotationHit{k, 0};
            break;
        case Annotation::Text: {
            if (!l.x.est_valide(a.x) || !ym.est_valide(a.y) || a.text.isEmpty())
                break;
            const QFontMetricsF fm(police_trace(a.size * z, false, this));
            const QPointF o(l.x.vers_px(a.x), ym.vers_px(a.y));
            const QRectF r(o.x() - 4 * z, o.y() - fm.ascent() - 3 * z, fm.horizontalAdvance(a.text) + 8 * z, fm.ascent() + fm.descent() + 6 * z);
            if (r.contains(p))
                return AnnotationHit{k, 0};
            break;
        }
        case Annotation::Line: {
            if (!l.x.est_valide(a.x) || !l.x.est_valide(a.x2) || !ym.est_valide(a.y) || !ym.est_valide(a.y2))
                break;
            const QPointF p1(l.x.vers_px(a.x), ym.vers_px(a.y)), p2(l.x.vers_px(a.x2), ym.vers_px(a.y2));
            if (QLineF(p, p1).length() <= grab)
                return AnnotationHit{k, 1};
            if (QLineF(p, p2).length() <= grab)
                return AnnotationHit{k, 2};
            const QPointF d = p2 - p1;
            const double len = std::hypot(d.x(), d.y());
            if (len > 0.0 && std::abs(d.x() * (p.y() - p1.y()) - d.y() * (p.x() - p1.x())) / len <= tol)
                return AnnotationHit{k, 0};
            break;
        }
        }
    }
    return std::nullopt;
}

Annotation GraphCanvas::annotation_deplacee(const PlotLayout &l, const Annotation &a, int part, QPointF from, QPointF p) const
{
    // déplacement en pixels, reporté sur les axes : juste aussi en échelle logarithmique
    const AxisMap &ym = l.echelle_y(a.side);
    const QPointF d = p - from;
    auto shiftX = [&](double v) { return l.x.est_valide(v) ? l.x.depuis_px(l.x.vers_px(v) + d.x()) : v; };
    auto shiftY = [&](double v) { return ym.est_valide(v) ? ym.depuis_px(ym.vers_px(v) + d.y()) : v; };
    Annotation b = a;
    switch (a.kind) {
    case Annotation::VLine: b.x = shiftX(a.x); break;
    case Annotation::HLine: b.y = shiftY(a.y); break;
    case Annotation::Text:
        b.x = shiftX(a.x);
        b.y = shiftY(a.y);
        break;
    case Annotation::Line:
        if (part == 1) {
            b.x = l.x.depuis_px(p.x());
            b.y = ym.depuis_px(p.y());
        } else if (part == 2) {
            b.x2 = l.x.depuis_px(p.x());
            b.y2 = ym.depuis_px(p.y());
        } else {
            b.x = shiftX(a.x);
            b.y = shiftY(a.y);
            b.x2 = shiftX(a.x2);
            b.y2 = shiftY(a.y2);
        }
        break;
    }
    return b;
}

std::optional<std::pair<int, int>> GraphCanvas::plage_modele_en(int plot, QPointF p) const
{
    const ModelingPanel *mp = ModelingPanel::instance();
    const GraphView *view = m_win.depot().ws.graphe(m_id);
    if (!mp || !view || static_cast<size_t>(plot) >= m_layouts.size() || static_cast<size_t>(plot) >= view->plots.size())
        return std::nullopt;
    const auto k = mp->modele_affiche(m_id, plot);
    const auto &fits = view->plots[static_cast<size_t>(plot)].fits;
    if (!k || static_cast<size_t>(*k) >= fits.size())
        return std::nullopt;
    const FitSpec &f = fits[static_cast<size_t>(*k)];
    const PlotLayout &l = m_layouts[static_cast<size_t>(plot)];
    if (p.y() < l.area.top() - 4 || p.y() > l.area.bottom() + 4)
        return std::nullopt;
    // bornes 0, 1 : plage des points ; 2, 3 : étendue de la courbe tracée (« de … à … »)
    std::vector<std::pair<int, double>> bars;
    if (f.range) {
        bars.emplace_back(0, f.range->first);
        bars.emplace_back(1, f.range->second);
    }
    if (f.draw == FitDraw::Custom) {
        bars.emplace_back(2, f.drawRange.first);
        bars.emplace_back(3, f.drawRange.second);
    }
    const double tol = 7.0 * theme::zoom();
    for (const auto &[bound, v] : bars)
        if (l.x.est_valide(v) && std::abs(l.x.vers_px(v) - p.x()) <= tol)
            return std::make_pair(*k, bound);
    return std::nullopt;
}

void GraphCanvas::dessine_plage_modele(QPainter &p)
{
    // plage du modèle affiché par la fenêtre Modélisation : deux barres à glisser
    const ModelingPanel *mp = ModelingPanel::instance();
    const GraphView *view = m_win.depot().ws.graphe(m_id);
    if (!mp || !view)
        return;
    const double z = theme::zoom();
    const bool dark = theme::est_sombre() && m_win.depot().prefs.darkPlots;
    for (size_t i = 0; i < m_layouts.size() && i < view->plots.size(); ++i) {
        const auto k = mp->modele_affiche(m_id, static_cast<int>(i));
        const auto &fits = view->plots[i].fits;
        if (!k || static_cast<size_t>(*k) >= fits.size())
            continue;
        const FitSpec &f = fits[static_cast<size_t>(*k)];
        const PlotLayout &l = m_layouts[i];
        const QColor c = (dark ? PlotTheme::theme_sombre() : PlotTheme::clair()).adapte(f.color);
        // étendue de la courbe tracée (« de … à … ») : barres en pointillés, poignées en losange
        if (f.draw == FitDraw::Custom) {
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            for (double v : {f.drawRange.first, f.drawRange.second}) {
                if (!l.x.est_valide(v))
                    continue;
                const double x = l.x.vers_px(v);
                if (x < l.area.left() - 1 || x > l.area.right() + 1)
                    continue;
                QPen dash(c, 1.6 * z, Qt::DashLine);
                p.setPen(dash);
                p.drawLine(QPointF(x, l.area.top()), QPointF(x, l.area.bottom()));
                p.setPen(QPen(Qt::white, 1.0 * z));
                p.setBrush(c);
                const double h = 6 * z, y = l.area.center().y();
                p.drawPolygon(QPolygonF{QPointF(x, y - h), QPointF(x + h, y), QPointF(x, y + h), QPointF(x - h, y)});
            }
            p.restore();
        }
        if (!f.range)
            continue;
        const double xa = std::clamp(l.x.vers_px(f.range->first), l.area.left(), l.area.right());
        const double xb = std::clamp(l.x.vers_px(f.range->second), l.area.left(), l.area.right());
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        QColor band = c;
        band.setAlphaF(0.08);
        p.fillRect(QRectF(QPointF(std::min(xa, xb), l.area.top()), QPointF(std::max(xa, xb), l.area.bottom())), band);
        for (double x : {xa, xb}) {
            p.setPen(QPen(c, 2.0 * z));
            p.drawLine(QPointF(x, l.area.top()), QPointF(x, l.area.bottom()));
            // poignées en haut et en bas
            p.setBrush(c);
            p.setPen(QPen(Qt::white, 1.0 * z));
            for (double y : {l.area.top() + 7 * z, l.area.bottom() - 7 * z})
                p.drawRoundedRect(QRectF(x - 4 * z, y - 7 * z, 8 * z, 14 * z), 3 * z, 3 * z);
        }
        p.restore();
    }
}

void GraphCanvas::dessine_selection(QPainter &p)
{
    if (!m_selected || static_cast<size_t>(m_selected->first) >= m_layouts.size())
        return;
    const GraphView *view = m_win.depot().ws.graphe(m_id);
    if (!view || static_cast<size_t>(m_selected->first) >= view->plots.size())
        return;
    const auto &as = view->plots[static_cast<size_t>(m_selected->first)].annotations;
    if (m_selected->second >= as.size())
        return;
    const Annotation &a = as[m_selected->second];
    const PlotLayout &l = m_layouts[static_cast<size_t>(m_selected->first)];
    const AxisMap &ym = l.echelle_y(a.side);
    const double z = theme::zoom();
    const QColor accent = theme::accent();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(l.area.adjusted(-6 * z, -6 * z, 6 * z, 6 * z));
    // poignée : rond plein (libre) ou creux (verrouillée)
    auto handle = [&](QPointF c) {
        p.setPen(QPen(accent, 1.6 * z));
        p.setBrush(a.locked ? QBrush(Qt::NoBrush) : QBrush(accent.lighter(130)));
        p.drawEllipse(c, 5.0 * z, 5.0 * z);
    };
    QPen frame(accent, 1.2 * z, Qt::DashLine);
    switch (a.kind) {
    case Annotation::VLine:
        if (l.x.est_valide(a.x))
            handle(QPointF(l.x.vers_px(a.x), l.area.center().y()));
        break;
    case Annotation::HLine:
        if (ym.est_valide(a.y))
            handle(QPointF(l.area.center().x(), ym.vers_px(a.y)));
        break;
    case Annotation::Text:
        if (l.x.est_valide(a.x) && ym.est_valide(a.y)) {
            const QFontMetricsF fm(police_trace(a.size * z, false, this));
            const QPointF o(l.x.vers_px(a.x), ym.vers_px(a.y));
            p.setPen(frame);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(o.x() - 4 * z, o.y() - fm.ascent() - 3 * z, fm.horizontalAdvance(a.text) + 8 * z, fm.ascent() + fm.descent() + 6 * z));
        }
        break;
    case Annotation::Line:
        if (l.x.est_valide(a.x) && l.x.est_valide(a.x2) && ym.est_valide(a.y) && ym.est_valide(a.y2)) {
            handle(QPointF(l.x.vers_px(a.x), ym.vers_px(a.y)));
            handle(QPointF(l.x.vers_px(a.x2), ym.vers_px(a.y2)));
        }
        break;
    }
    p.restore();
}

void GraphCanvas::met_en_surbrillance(std::optional<std::pair<int, int>> h)
{
    if (m_highlight != h) {
        m_highlight = h;
        invalide();
    }
}

void GraphCanvas::resizeEvent(QResizeEvent *)
{
    m_dirty = true;
}

std::optional<int> GraphCanvas::trace_en(QPointF p) const
{
    for (const auto &l : m_layouts) {
        if (l.cell.contains(p))
            return l.index;
    }
    return std::nullopt;
}

void GraphCanvas::rendu()
{
    const Store &store = m_win.depot();
    const GraphView *view = store.ws.graphe(m_id);
    const qreal dpr = devicePixelRatioF();
    m_cache = QPixmap(size() * dpr);
    m_cache.setDevicePixelRatio(dpr);
    RenderOptions o;
    o.style = store.style();
    o.theme = theme::est_sombre() && store.prefs.darkPlots ? PlotTheme::theme_sombre() : PlotTheme::clair();
    m_cache.fill(o.theme.background);
    if (!view) {
        m_layouts.clear();
        m_data.clear();
        m_dirty = false;
        return;
    }
    o.devicePixelRatio = dpr;
    o.highlight = m_highlight;
    m_data = exporter::resout_tout(*view, store.ws.doc, store.style());
    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    // agrandissement de l'interface : textes, traits, marqueurs et marges suivent (pas les exports)
    m_layouts = rend_graphe_agrandi(p, *view, m_data, QSizeF(size()), o, theme::zoom());
    // graphe vide : mode d'emploi
    bool noCurve = true, noData = true;
    for (int i = 0; i < view->traces_visibles(); ++i)
        noCurve = noCurve && view->plots[i].curves.empty();
    for (const auto &d : m_data) {
        for (const auto &s : d.series) {
            for (size_t k = 0; k < std::min(s.xs.size(), s.ys.size()); ++k)
                noData = noData && !(std::isfinite(s.xs[k]) && std::isfinite(s.ys[k]));
        }
    }
    QString hint;
    if (noCurve) {
        hint = store.pendingAbscissa.count(m_id)
                   ? u"Abscisse : %1. Glissez maintenant la grandeur à placer en ordonnée."_s.arg(store.ws.doc.nom(store.pendingAbscissa.at(m_id)))
                   : u"Glissez ici des colonnes d'un tableur : la première donne les abscisses, les suivantes les ordonnées."_s;
    } else if (noData) {
        hint = u"Saisissez des valeurs dans le tableur : le graphe se met à jour à chaque mesure."_s;
    }
    if (!hint.isEmpty()) {
        // dans la zone de tracé, sur un fond qui le détache de la grille ; police réduite s'il le
        // faut pour qu'il y tienne
        const double z = theme::zoom();
        const QRectF area = m_layouts.empty() ? QRectF(rect()) : m_layouts.front().area;
        const QRectF room = area.adjusted(10 * z, 10 * z, -10 * z, -10 * z);
        QFont f = font();
        QRectF box;
        const int flags = Qt::AlignCenter | Qt::TextWordWrap;
        for (int k = 0; k < 4; ++k) {
            const QRectF width(room.left(), room.top(), std::min(room.width(), 420.0 * z), room.height());
            box = QFontMetricsF(f).boundingRect(width, flags, hint);
            if (box.height() <= room.height() && box.width() <= room.width())
                break;
            f.setPointSizeF(f.pointSizeF() * 0.85);
        }
        box.moveCenter(room.center());
        const QRectF back = box.adjusted(-8 * z, -6 * z, 8 * z, 6 * z).intersected(area);
        QColor fill = o.theme.background;
        fill.setAlpha(235);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(back, 4 * z, 4 * z);
        p.setFont(f);
        p.setPen(theme::couleur_attenuee());
        p.setClipRect(area);
        p.drawText(box, flags, hint);
        p.setClipping(false);
    }
    p.end();
    m_dirty = false;
}

std::optional<GraphCanvas::Nearest> GraphCanvas::point_le_plus_proche(int plot, QPointF p) const
{
    if (plot < 0 || static_cast<size_t>(plot) >= m_layouts.size() || static_cast<size_t>(plot) >= m_data.size())
        return std::nullopt;
    const PlotLayout &l = m_layouts[plot];
    std::optional<Nearest> best;
    const auto &series = m_data[plot].series;
    for (size_t k = 0; k < series.size(); ++k) {
        const Series &s = series[k];
        if (s.curve < 0)
            continue;
        const AxisMap &m = l.echelle_y(s.side);
        for (size_t i = 0; i < std::min(s.xs.size(), s.ys.size()); ++i) {
            if (!l.x.est_valide(s.xs[i]) || !m.est_valide(s.ys[i]))
                continue;
            const QPointF q(l.x.vers_px(s.xs[i]), m.vers_px(s.ys[i]));
            const double d = std::hypot(q.x() - p.x(), q.y() - p.y());
            if (!best || d < best->dist)
                best = Nearest{k, i, d, q};
        }
    }
    return best;
}

void GraphCanvas::paintEvent(QPaintEvent *)
{
    if (m_dirty || m_cache.size() != size() * devicePixelRatioF())
        rendu();
    QPainter p(this);
    p.drawPixmap(0, 0, m_cache);
    dessine_plage_modele(p);
    dessine_selection(p);
    dessine_surcouche(p);
}

void GraphCanvas::lecture(QPainter &p, QPointF at, const QStringList &lines)
{
    const QFont f = font();
    p.setFont(f);
    const QFontMetricsF fm(f);
    const double z = theme::zoom();
    double w = 0;
    for (const QString &l : lines)
        w = std::max(w, fm.horizontalAdvance(l));
    w += 14 * z;
    const double h = lines.size() * fm.height() + 8 * z;
    QPointF pos = at + QPointF(14, 14) * z;
    if (pos.x() + w > width())
        pos.setX(at.x() - 14 * z - w);
    if (pos.y() + h > height())
        pos.setY(at.y() - 14 * z - h);
    // bulle aux couleurs des bulles d'aide du thème ; claire sur un graphe resté clair en thème sombre
    const QPalette pal = QApplication::palette();
    const bool lightPlot = theme::est_sombre() && !m_win.depot().prefs.darkPlots;
    const QRectF r(pos, QSizeF(w, h));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(lightPlot ? QColor(170, 174, 180) : pal.color(QPalette::Mid), std::max(1.0, std::round(z))));
    QColor fill = lightPlot ? QColor(255, 255, 250) : pal.color(QPalette::ToolTipBase);
    fill.setAlpha(242);
    p.setBrush(fill);
    p.drawRoundedRect(r, 4 * z, 4 * z);
    p.setPen(lightPlot ? QColor(20, 20, 20) : pal.color(QPalette::ToolTipText));
    double y = r.top() + 4 * z + fm.ascent();
    for (const QString &l : lines) {
        p.drawText(QPointF(r.left() + 7 * z, y), l);
        y += fm.height();
    }
}

void GraphCanvas::dessine_surcouche(QPainter &p)
{
    const Store &store = m_win.depot();
    const GraphView *view = store.ws.graphe(m_id);
    if (!view)
        return;
    const QColor accent = theme::accent();
    const NumberStyle style = store.style();
    // traits de la taille de ceux du graphe : ils suivent l'agrandissement de l'interface
    const double z = theme::zoom();
    const double hair = std::max(1.0, std::round(z));
    // dépôt d'une grandeur
    if (m_dropPlot && static_cast<size_t>(*m_dropPlot) < m_layouts.size()) {
        p.setPen(QPen(accent, 2.5 * z));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(m_layouts[*m_dropPlot].area.adjusted(z, z, -z, -z), 2 * z, 2 * z);
    }
    // glisser en cours
    if (m_drag.kind == Drag::ZoomRect) {
        QColor fill = accent;
        fill.setAlphaF(0.12);
        p.setPen(QPen(accent, hair));
        p.setBrush(fill);
        p.drawRect(QRectF(m_drag.start, m_drag.last).normalized());
    } else if (m_drag.kind == Drag::Range && static_cast<size_t>(m_drag.plot) < m_layouts.size()) {
        const PlotLayout &l = m_layouts[m_drag.plot];
        const double a = l.x.vers_px(m_drag.startX);
        QColor fill = accent;
        fill.setAlphaF(0.15);
        p.setPen(QPen(accent, hair));
        p.setBrush(fill);
        p.drawRect(QRectF(QPointF(std::min(a, m_drag.last.x()), l.area.top()), QPointF(std::max(a, m_drag.last.x()), l.area.bottom())));
    }
    // plage sélectionnée pour la modélisation
    if (auto it = store.selectedRange.find(m_id); it != store.selectedRange.end() && m_drag.kind != Drag::Range) {
        const auto [pi, a, b] = it->second;
        if (static_cast<size_t>(pi) < m_layouts.size()) {
            const PlotLayout &l = m_layouts[pi];
            QColor fill = accent;
            fill.setAlphaF(0.07);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            const double xa = std::clamp(l.x.vers_px(a), l.area.left(), l.area.right()), xb = std::clamp(l.x.vers_px(b), l.area.left(), l.area.right());
            p.drawRect(QRectF(QPointF(std::min(xa, xb), l.area.top()), QPointF(std::max(xa, xb), l.area.bottom())));
        }
    }
    // réticule et lecture des coordonnées
    if (!m_hover)
        return;
    const auto pi = trace_en(*m_hover);
    if (!pi || static_cast<size_t>(*pi) >= m_layouts.size())
        return;
    const PlotLayout &l = m_layouts[*pi];
    if (!l.area.contains(*m_hover))
        return;
    const bool draggingPoint = m_drag.kind == Drag::Point;
    if (!(m_tool == Tool::Reticle || m_tool == Tool::MovePoints || draggingPoint))
        return;
    const bool dark = theme::est_sombre() && store.prefs.darkPlots;
    const QColor lineColor = dark ? QColor(200, 200, 200, 120) : QColor(80, 80, 80, 130);
    QPen dashed(lineColor, hair, Qt::DashLine);
    QStringList lines;
    const Plot &plot = view->plots[*pi];
    const auto near = point_le_plus_proche(*pi, *m_hover);
    p.setRenderHint(QPainter::Antialiasing, false);
    if (near && near->dist < 14.0 * z) {
        const Series &s = m_data[*pi].series[near->series];
        const CurveSpec &c = plot.curves[static_cast<size_t>(s.curve)];
        QString xu, yu;
        const QString xn = nom_unite(store.ws.doc, c.x, &xu);
        const QString yn = nom_unite(store.ws.doc, c.y, &yu);
        p.setPen(dashed);
        p.drawLine(QPointF(near->pos.x(), l.area.top()), QPointF(near->pos.x(), l.area.bottom()));
        p.drawLine(QPointF(l.area.left(), near->pos.y()), QPointF(l.area.right(), near->pos.y()));
        p.setRenderHint(QPainter::Antialiasing);
        QPen ring(PlotTheme().adapte(c.color), 2 * z);
        ring.setColor((dark ? PlotTheme::theme_sombre() : PlotTheme::clair()).adapte(c.color));
        p.setPen(ring);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(near->pos, 6.5 * z, 6.5 * z);
        const size_t i = near->index;
        // avec son incertitude, la valeur est arrondie à la même décimale : U = (5,042 ± 0,070) V ;
        // incertitudes différentes de part et d'autre : U = 5,042 V (+0,080 / −0,060)
        auto at = [&](const std::vector<double> &u) { return u.empty() ? NAN : u.size() == 1 ? u.front() : (i < u.size() ? u[i] : NAN); };
        auto withU = [&](double v, const std::vector<double> &sym, const std::vector<double> &low, const std::vector<double> &high, const QString &unit) {
            const double lo = at(low.empty() ? sym : low), hi = at(high.empty() ? sym : high);
            const bool okLo = std::isfinite(lo) && lo > 0, okHi = std::isfinite(hi) && hi > 0;
            if (okLo && okHi && lo == hi)
                return arrondit_mesure(v, lo, 2, style).en_texte(unit.trimmed());
            if (okLo || okHi)
                return u"%1%2 (+%3 / −%4)"_s.arg(format_significatif(v, 5, style), unit, okHi ? format_significatif(hi, 2, style) : u"0"_s,
                                                  okLo ? format_significatif(lo, 2, style) : u"0"_s);
            return format_significatif(v, 5, style) + unit;
        };
        lines << u"point n° %1"_s.arg(i + 1);
        lines << u"%1 = %2"_s.arg(xn, withU(s.xs[i], s.ux, s.uxLow, s.uxHigh, xu));
        lines << u"%1 = %2"_s.arg(yn, withU(s.ys[i], s.uy, s.uyLow, s.uyHigh, yu));
    } else if (m_tool == Tool::Reticle) {
        p.setPen(dashed);
        p.drawLine(QPointF(m_hover->x(), l.area.top()), QPointF(m_hover->x(), l.area.bottom()));
        p.drawLine(QPointF(l.area.left(), m_hover->y()), QPointF(l.area.right(), m_hover->y()));
        const PlotData &d = m_data[*pi];
        auto unit = [](const QString &u) { return u.isEmpty() ? QString() : u" "_s + unite_lisible(u); };
        lines << u"%1 = %2%3"_s.arg(nom_axe(d.xName, u"x"_s), format_significatif(l.x.depuis_px(m_hover->x()), 4, style), unit(d.xUnit));
        lines << u"%1 = %2%3"_s.arg(nom_axe(d.yName, u"y"_s), format_significatif(l.y.depuis_px(m_hover->y()), 4, style), unit(d.yUnit));
        if (l.y2)
            lines << u"%1 = %2%3"_s.arg(nom_axe(d.y2Name, u"y₂"_s), format_significatif(l.y2->depuis_px(m_hover->y()), 4, style), unit(d.y2Unit));
    }
    if (!lines.isEmpty())
        lecture(p, *m_hover, lines);
}

void GraphCanvas::mousePressEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    const auto pi = trace_en(pos);
    if (!pi)
        return;
    Store &store = m_win.depot();
    store.selectedPlot[m_id] = *pi;
    Q_EMIT trace_selectionne(*pi);
    if (e->button() == Qt::RightButton) {
        m_contextPos = pos;
        return;
    }
    const PlotLayout &l = m_layouts[*pi];
    const GraphView *view = store.ws.graphe(m_id);
    m_drag = Drag{};
    m_drag.plot = *pi;
    m_drag.start = m_drag.last = pos;
    if (e->button() == Qt::MiddleButton) {
        m_drag.kind = Drag::Pan;
        return;
    }
    if (e->button() != Qt::LeftButton || !view)
        return;
    // poignée de la méthode des tangentes (prioritaire)
    const Plot &plot = view->plots[*pi];
    for (size_t ti = 0; ti < plot.tangents.size(); ++ti) {
        if (plot.tangents[ti].locked)
            continue;
        if (auto res = calcule_tangentes(plot.tangents[ti], store.ws.doc)) {
            const AxisMap &m = l.echelle_y(plot.tangents[ti].side);
            for (const auto &pt : {res->second.p1, res->second.p2}) {
                if (std::hypot(l.x.vers_px(pt.first) - pos.x(), m.vers_px(pt.second) - pos.y()) < 10.0 * theme::zoom()) {
                    m_drag.kind = Drag::Tangent;
                    m_drag.tangent = static_cast<int>(ti);
                    return;
                }
            }
        }
    }
    // barre de plage du modèle affiché (fenêtre Modélisation ouverte)
    if (const auto bar = plage_modele_en(*pi, pos); bar && m_tool != Tool::Zoom) {
        m_drag.kind = Drag::FitRange;
        m_drag.index = static_cast<size_t>(bar->first);
        m_drag.part = bar->second;
        setCursor(Qt::SizeHorCursor);
        return;
    }
    // annotation (outils réticule et déplacement de la vue) : sélectionnée, et glissée si elle
    // n'est pas verrouillée
    if (m_tool == Tool::Reticle || m_tool == Tool::Pan) {
        if (const auto hit = annotation_en(*pi, pos)) {
            m_selected = std::make_pair(*pi, hit->index);
            const Annotation &a = plot.annotations[hit->index];
            if (!a.locked) {
                m_drag.kind = Drag::Annotation;
                m_drag.index = hit->index;
                m_drag.part = hit->part;
                m_drag.original = a;
                setCursor(Qt::ClosedHandCursor);
                update();
                return;
            }
            store.message(u"Annotation verrouillée : réglages du graphe, onglet Annotations, pour la libérer (cadenas)"_s);
        } else if (m_selected) {
            m_selected.reset();
            update();
        }
    }
    switch (m_tool) {
    case Tool::Zoom: m_drag.kind = Drag::ZoomRect; break;
    case Tool::Range:
        m_drag.kind = Drag::Range;
        m_drag.startX = l.x.depuis_px(pos.x());
        break;
    case Tool::MovePoints:
        if (auto n = point_le_plus_proche(*pi, pos); n && n->dist < 12.0 * theme::zoom()) {
            m_drag.kind = Drag::Point;
            m_drag.curve = m_data[*pi].series[n->series].curve;
            m_drag.index = n->index;
            setCursor(Qt::ClosedHandCursor);
        }
        break;
    case Tool::Pan:
    case Tool::Reticle: m_drag.kind = Drag::Pan; break;
    }
}

void GraphCanvas::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    m_hover = pos;
    Store &store = m_win.depot();
    if (m_drag.kind == Drag::None || static_cast<size_t>(m_drag.plot) >= m_layouts.size()) {
        // survol d'une annotation qui se déplace : curseur de déplacement
        std::optional<AnnotationHit> hit;
        if ((m_tool == Tool::Reticle || m_tool == Tool::Pan) && !e->buttons())
            if (const auto pi = trace_en(pos))
                hit = annotation_en(*pi, pos);
        const GraphView *view = store.ws.graphe(m_id);
        const auto pi = trace_en(pos);
        if (pi && !e->buttons() && m_tool != Tool::Zoom && plage_modele_en(*pi, pos))
            setCursor(Qt::SizeHorCursor);
        else if (hit && view && pi && !view->plots[static_cast<size_t>(*pi)].annotations[hit->index].locked) {
            const Annotation::Kind k = view->plots[static_cast<size_t>(*pi)].annotations[hit->index].kind;
            setCursor(hit->part != 0 ? Qt::PointingHandCursor
                      : k == Annotation::VLine ? Qt::SizeHorCursor
                      : k == Annotation::HLine ? Qt::SizeVerCursor
                                               : Qt::SizeAllCursor);
        } else if (!e->buttons()) {
            setCursor(curseur_outil());
        }
        update();
        return;
    }
    const PlotLayout l = m_layouts[m_drag.plot];
    const int pi = m_drag.plot;
    switch (m_drag.kind) {
    case Drag::Pan: {
        const QPointF d = pos - m_drag.last;
        m_drag.last = pos;
        store.edite([&](Seance &ws) {
            GraphView *g = ws.graphe_modifiable(m_id);
            Plot &pl = g->plots[pi];
            deplace_axe(pl.x, l.x, d.x());
            deplace_axe(pl.y, l.y, d.y());
            if (l.y2)
                deplace_axe(pl.y2, *l.y2, d.y());
            graphops::synchronise_x(*g, pi);
        });
        break;
    }
    case Drag::Point: {
        const GraphView *view = store.ws.graphe(m_id);
        if (!view || static_cast<size_t>(m_drag.curve) >= view->plots[pi].curves.size())
            break;
        const CurveSpec c = view->plots[pi].curves[m_drag.curve];
        const double x = l.x.depuis_px(pos.x());
        const double y = l.echelle_y(c.side).depuis_px(pos.y());
        store.edite([&](Seance &ws) {
            const Grandeur *xq = ws.doc.obtient(c.x);
            const Grandeur *yq = ws.doc.obtient(c.y);
            if (xq && !xq->est_calculee() && c.x != c.y)
                ws.doc.definit_valeur(c.x, m_drag.index, x);
            if (yq && !yq->est_calculee())
                ws.doc.definit_valeur(c.y, m_drag.index, y);
        });
        break;
    }
    case Drag::FitRange: {
        const double x = l.x.depuis_px(std::clamp(pos.x(), l.area.left(), l.area.right()));
        store.edite([&](Seance &ws) {
            auto &fits = ws.graphe_modifiable(m_id)->plots[pi].fits;
            if (m_drag.index >= fits.size())
                return;
            FitSpec &f = fits[m_drag.index];
            if (m_drag.part >= 2)
                (m_drag.part == 2 ? f.drawRange.first : f.drawRange.second) = x;
            else if (f.range)
                (m_drag.part == 0 ? f.range->first : f.range->second) = x;
        });
        break;
    }
    case Drag::Annotation: {
        const Annotation moved = annotation_deplacee(l, m_drag.original, m_drag.part, m_drag.start, pos);
        store.edite([&](Seance &ws) {
            auto &as = ws.graphe_modifiable(m_id)->plots[pi].annotations;
            if (m_drag.index < as.size())
                as[m_drag.index] = moved;
        });
        break;
    }
    case Drag::Tangent: {
        const double x = l.x.depuis_px(pos.x());
        store.edite([&](Seance &ws) {
            auto &ts = ws.graphe_modifiable(m_id)->plots[pi].tangents;
            if (static_cast<size_t>(m_drag.tangent) < ts.size())
                ts[m_drag.tangent].contact = x;
        });
        break;
    }
    default:
        m_drag.last = pos;
        update();
        break;
    }
}

void GraphCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    Store &store = m_win.depot();
    const Drag d = m_drag;
    m_drag = Drag{};
    if (m_tool == Tool::MovePoints || d.kind == Drag::Annotation || d.kind == Drag::FitRange)
        setCursor(curseur_outil());
    if (d.kind == Drag::FitRange) {
        // bornes remises dans l'ordre
        store.edite([&](Seance &ws) {
            auto &fits = ws.graphe_modifiable(m_id)->plots[d.plot].fits;
            if (d.index >= fits.size())
                return;
            FitSpec &f = fits[d.index];
            if (f.range && f.range->first > f.range->second)
                std::swap(f.range->first, f.range->second);
            if (f.drawRange.first > f.drawRange.second)
                std::swap(f.drawRange.first, f.drawRange.second);
        });
    }
    if (static_cast<size_t>(d.plot) >= m_layouts.size()) {
        update();
        return;
    }
    const PlotLayout l = m_layouts[d.plot];
    if (d.kind == Drag::ZoomRect && QLineF(d.start, pos).length() > 6.0 * theme::zoom()) {
        store.edite([&](Seance &ws) {
            GraphView *g = ws.graphe_modifiable(m_id);
            Plot &pl = g->plots[d.plot];
            definit_plage(pl.x, l.x.depuis_px(d.start.x()), l.x.depuis_px(pos.x()));
            definit_plage(pl.y, l.y.depuis_px(d.start.y()), l.y.depuis_px(pos.y()));
            if (l.y2)
                definit_plage(pl.y2, l.y2->depuis_px(d.start.y()), l.y2->depuis_px(pos.y()));
            graphops::synchronise_x(*g, d.plot);
        });
    } else if (d.kind == Drag::Range) {
        const double end = l.x.depuis_px(pos.x());
        const double a = std::min(d.startX, end), b = std::max(d.startX, end);
        store.selectedRange[m_id] = {d.plot, a, b};
        // appliquée à la modélisation en cours sur ce graphe
        bool applied = false;
        if (ModelingPanel *mp = ModelingPanel::instance(); mp && mp->applique_plage(m_id, d.plot, a, b))
            applied = true;
        store.message(u"Plage : %1 à %2%3"_s.arg(format_significatif(a, 4, store.style()), format_significatif(b, 4, store.style()),
                                                 applied ? u" (appliquée au modèle)"_s : u" (utilisée par la prochaine modélisation)"_s),
                      icons::Status::Info);
    }
    update();
}

void GraphCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPointF p = e->position();
    const auto pi = trace_en(p);
    if (!pi)
        return;
    const PlotLayout &l = m_layouts[*pi];
    auto near = [&](const QRectF &r, double m) {
        m *= theme::zoom();
        return r.adjusted(-m, -m, m, m).contains(p);
    };
    if (annotation_en(*pi, p))
        Q_EMIT reglages_demandes(SettingsSection::Annotations, -1);
    else if (l.title && near(*l.title, 4))
        Q_EMIT reglages_demandes(SettingsSection::Title, -1);
    else if (near(l.xAxis, 2) && !l.area.contains(p))
        Q_EMIT reglages_demandes(SettingsSection::Axes, 0);
    else if (near(l.yAxis, 2) && !l.area.contains(p))
        Q_EMIT reglages_demandes(SettingsSection::Axes, 1);
    else if (l.y2Axis && near(*l.y2Axis, 2) && !l.area.contains(p))
        Q_EMIT reglages_demandes(SettingsSection::Axes, 2);
    else if (l.legend && l.legend->contains(p))
        Q_EMIT reglages_demandes(SettingsSection::Curves, -1);
    else {
        m_win.depot().edite([&](Seance &ws) {
            GraphView *g = ws.graphe_modifiable(m_id);
            graphops::echelle_auto(g->plots[*pi]);
            graphops::synchronise_x(*g, *pi);
        });
    }
}

void GraphCanvas::wheelEvent(QWheelEvent *e)
{
    const QPointF pos = e->position();
    const auto pi = trace_en(pos);
    if (!pi || m_drag.kind != Drag::None)
        return;
    QPoint delta = e->angleDelta();
    const bool alt = e->modifiers() & Qt::AltModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    // avec Alt, certains systèmes envoient la molette sur l'axe horizontal
    double dy = delta.y() != 0 ? delta.y() : delta.x();
    const double f = std::exp(-dy / 120.0 * 50.0 * 0.0025);
    const bool onlyX = shift || (delta.y() == 0 && delta.x() != 0 && !alt);
    const bool onlyY = alt;
    const PlotLayout l = m_layouts[*pi];
    m_win.depot().edite([&](Seance &ws) {
        GraphView *g = ws.graphe_modifiable(m_id);
        Plot &pl = g->plots[*pi];
        if (!onlyY)
            zoome_axe(pl.x, l.x, l.x.depuis_px(pos.x()), f);
        if (!onlyX) {
            zoome_axe(pl.y, l.y, l.y.depuis_px(pos.y()), f);
            if (l.y2)
                zoome_axe(pl.y2, *l.y2, l.y2->depuis_px(pos.y()), f);
        }
        graphops::synchronise_x(*g, *pi);
    });
    e->accept();
}

void GraphCanvas::keyPressEvent(QKeyEvent *e)
{
    // Suppr : l'annotation sélectionnée (si elle n'est pas verrouillée) ; Échap : plus de sélection
    if (m_selected && (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)) {
        const auto [plot, index] = *m_selected;
        const GraphView *view = m_win.depot().ws.graphe(m_id);
        if (view && static_cast<size_t>(plot) < view->plots.size() && index < view->plots[plot].annotations.size()) {
            if (view->plots[plot].annotations[index].locked) {
                m_win.depot().message(u"Annotation verrouillée : elle n'est pas supprimée"_s, icons::Status::Warning);
            } else {
                m_selected.reset();
                m_win.depot().edite([&](Seance &ws) {
                    auto &as = ws.graphe_modifiable(m_id)->plots[plot].annotations;
                    as.erase(as.begin() + static_cast<std::ptrdiff_t>(index));
                });
            }
        }
        return;
    }
    if (m_selected && e->key() == Qt::Key_Escape) {
        m_selected.reset();
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void GraphCanvas::leaveEvent(QEvent *)
{
    m_hover.reset();
    update();
}

void GraphCanvas::contextMenuEvent(QContextMenuEvent *e)
{
    const QPointF pos = e->pos();
    const auto pi = trace_en(pos);
    if (!pi)
        return;
    MainWindow &win = m_win;
    Store &store = win.depot();
    const ViewId id = m_id;
    const int plot = *pi;
    const PlotLayout l = m_layouts[plot];
    QMenu menu(this);
    menu.addAction(icons::obtient("fit"), u"Ajuster l'échelle"_s, this, [&store, id, plot] {
        store.edite([&](Seance &ws) {
            GraphView *g = ws.graphe_modifiable(id);
            graphops::echelle_auto(g->plots[plot]);
            graphops::synchronise_x(*g, plot);
        });
    });
    menu.addAction(icons::obtient("sliders"), u"Réglages du graphe…"_s, this, [this] { Q_EMIT reglages_demandes(SettingsSection::Curves, -1); });
    // annotation sous le pointeur : verrou, suppression
    if (const auto hit = annotation_en(plot, pos)) {
        const size_t k = hit->index;
        const bool locked = store.ws.graphe(id)->plots[plot].annotations[k].locked;
        menu.addSeparator();
        m_selected = std::make_pair(plot, k);
        update();
        menu.addAction(icons::obtient(locked ? "unlock" : "lock"), locked ? u"Libérer l'annotation"_s : u"Verrouiller l'annotation"_s, this,
                       [&store, id, plot, k, locked] {
                           store.edite([&](Seance &ws) {
                               auto &as = ws.graphe_modifiable(id)->plots[plot].annotations;
                               if (k < as.size())
                                   as[k].locked = !locked;
                           });
                       });
        if (!locked)
            menu.addAction(icons::obtient("trash"), u"Supprimer l'annotation"_s, this, [this, &store, id, plot, k] {
                m_selected.reset();
                store.edite([&](Seance &ws) {
                    auto &as = ws.graphe_modifiable(id)->plots[plot].annotations;
                    if (k < as.size())
                        as.erase(as.begin() + static_cast<std::ptrdiff_t>(k));
                });
            });
        menu.addAction(icons::obtient("pencil"), u"Régler l'annotation…"_s, this, [this] { Q_EMIT reglages_demandes(SettingsSection::Annotations, -1); });
    }
    menu.addSeparator();
    menu.addAction(icons::obtient("model"), u"Modéliser…"_s, this, [&win, id, plot] { win.ouvre_modelisation(id, plot); });
    menu.addAction(icons::obtient("tangent"), u"Méthode des tangentes…"_s, this, [&win, id, plot] { dialogs::tangentes(win, id, {plot}); });
    // méthode des tangentes déjà tracée : la retirer, la verrouiller
    if (const auto &ts = store.ws.graphe(id)->plots[plot].tangents; !ts.empty()) {
        const bool locked = std::all_of(ts.begin(), ts.end(), [](const TangentSpec &t) { return t.locked; });
        menu.addAction(icons::obtient(locked ? "unlock" : "lock"), locked ? u"Libérer les tangentes"_s : u"Verrouiller les tangentes"_s, this,
                       [&store, id, plot, locked] {
                           store.edite([&](Seance &ws) {
                               for (TangentSpec &t : ws.graphe_modifiable(id)->plots[plot].tangents)
                                   t.locked = !locked;
                           });
                       });
        menu.addAction(icons::obtient("trash"), u"Supprimer la méthode des tangentes"_s, this, [&store, id, plot] {
            store.edite([&](Seance &ws) { ws.graphe_modifiable(id)->plots[plot].tangents.clear(); });
            store.message(u"Méthode des tangentes retirée du graphe (Ctrl+Z pour la retrouver)"_s);
        });
    }
    if (l.area.contains(pos)) {
        const double x = l.x.depuis_px(pos.x()), y = l.y.depuis_px(pos.y());
        QMenu *add = menu.addMenu(icons::obtient("plus"), u"Ajouter ici"_s);
        auto addAnnotation = [&store, id, plot](Annotation a) {
            store.edite([&](Seance &ws) { ws.graphe_modifiable(id)->plots[plot].annotations.push_back(a); });
        };
        add->addAction(icons::obtient("vline"), u"Droite verticale x = %1"_s.arg(format_significatif(x, 4, store.style())), this, [addAnnotation, x] {
            Annotation a;
            a.kind = Annotation::VLine;
            a.x = x;
            addAnnotation(a);
        });
        add->addAction(icons::obtient("hline"), u"Droite horizontale y = %1"_s.arg(format_significatif(y, 4, store.style())), this, [addAnnotation, y] {
            Annotation a;
            a.kind = Annotation::HLine;
            a.y = y;
            addAnnotation(a);
        });
        add->addAction(icons::obtient("slope"), u"Droite oblique"_s, this, [addAnnotation, &l, pos] {
            // passant par le point cliqué, inclinée ; ses poignées la règlent ensuite
            const double d = 60.0 * theme::zoom();
            Annotation a;
            a.kind = Annotation::Line;
            a.dash = Dash::Solid;
            a.x = l.x.depuis_px(pos.x() - d);
            a.y = l.y.depuis_px(pos.y() + d * 0.6);
            a.x2 = l.x.depuis_px(pos.x() + d);
            a.y2 = l.y.depuis_px(pos.y() - d * 0.6);
            addAnnotation(a);
        });
        add->addAction(icons::obtient("text"), u"Texte…"_s, this, [this, addAnnotation, x, y] {
            Annotation a;
            a.kind = Annotation::Text;
            a.x = x;
            a.y = y;
            a.text = u"Texte"_s;
            a.color = colors::TEXT;
            a.dash = Dash::Solid;
            addAnnotation(a);
            Q_EMIT reglages_demandes(SettingsSection::Annotations, -1);
        });
    }
    menu.addSeparator();
    menu.addAction(icons::obtient("copy"), u"Copier l'image"_s, this, [&win, id] { win.copie_image_graphe(id); });
    QMenu *exp = menu.addMenu(icons::obtient("export"), u"Exporter"_s);
    exp->addAction(icons::obtient("image"), u"Image PNG (haute résolution)…"_s, this, [&win, id] { win.exporte_graphe(id, exporter::Format::Png); });
    exp->addAction(icons::obtient("vector"), u"Image vectorielle SVG…"_s, this, [&win, id] { win.exporte_graphe(id, exporter::Format::Svg); });
    exp->addAction(icons::obtient("pdf"), u"Document PDF…"_s, this, [&win, id] { win.exporte_graphe(id, exporter::Format::Pdf); });
    menu.addAction(icons::obtient("print"), u"Imprimer…"_s, this, [&win, id] { win.imprime_graphe(id); });
    menu.exec(e->globalPos());
}

int GraphCanvas::zone_depot(int plot, QPointF p) const
{
    if (static_cast<size_t>(plot) >= m_layouts.size())
        return 1;
    const PlotLayout &l = m_layouts[plot];
    const double m = 4 * theme::zoom();
    if (l.xAxis.adjusted(-m, -m, m, m).contains(p) || p.y() > l.area.bottom())
        return 0;
    if ((l.y2Axis && l.y2Axis->contains(p)) || p.x() > l.area.right())
        return 2;
    return 1;
}

void GraphCanvas::dragEnterEvent(QDragEnterEvent *e)
{
    if (dnd::grandeur_de(e->mimeData()))
        e->acceptProposedAction();
}

void GraphCanvas::dragMoveEvent(QDragMoveEvent *e)
{
    if (!dnd::grandeur_de(e->mimeData()))
        return;
    e->acceptProposedAction();
    m_dropPlot = trace_en(e->position());
    update();
}

void GraphCanvas::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropPlot.reset();
    update();
}

void GraphCanvas::dropEvent(QDropEvent *e)
{
    const std::vector<QId> ids = dnd::grandeurs_de(e->mimeData());
    m_dropPlot.reset();
    if (ids.empty())
        return;
    e->acceptProposedAction();
    const QPointF pos = e->position();
    const int pi = trace_en(pos).value_or(0);
    m_win.depot().selectedPlot[m_id] = pi;
    graphops::depose_grandeurs(m_win, m_id, pi, ids, zone_depot(pi, pos));
    invalide();
}

// --- panneau ----------------------------------------------------------------------------------

GraphPanel::GraphPanel(MainWindow &win, ViewId id, QWidget *parent) : QWidget(parent), m_win(win), m_id(id)
{
    theme::definit_largeur_min(this, 340);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    auto *bar = new QWidget(this);
    auto *flow = new FlowLayout(bar, 2);
    m_canvas = new GraphCanvas(win, id, this);
    m_settings = new GraphSettings(win, id, this);

    struct T {
        Tool tool;
        const char *icon;
        const char *tip;
    };
    const T tools[] = {
        {Tool::Reticle, "crosshair", "Réticule : lecture des coordonnées, accrochée aux points (glisser pour déplacer la vue)"},
        {Tool::Zoom, "zoom", "Zoom : tracer un rectangle"},
        {Tool::Pan, "pan", "Déplacer la vue"},
        {Tool::MovePoints, "move-point", "Glisser les points de mesure"},
        {Tool::Range, "range", "Sélectionner une plage d'abscisses (modélisation)"},
    };
    auto *group = new QActionGroup(this);
    for (const T &t : tools) {
        auto *b = bouton_outil(t.icon, QString(), QString::fromUtf8(t.tip), bar, false);
        b->setCheckable(true);
        b->setChecked(t.tool == Tool::Reticle);
        const Tool tool = t.tool;
        connect(b, &QToolButton::clicked, this, [this, tool] {
            m_canvas->choisit_outil(tool);
            for (auto &[tt, bb] : m_tools)
                bb->setChecked(tt == tool);
        });
        m_tools.emplace_back(t.tool, b);
        flow->addWidget(b);
    }
    Q_UNUSED(group);
    auto *fit = bouton_outil("fit", QString(), u"Ajuster l'échelle aux données (double-clic dans le graphe)"_s, bar, false);
    connect(fit, &QToolButton::clicked, this, [this] {
        m_win.depot().edite([&](Seance &ws) {
            if (GraphView *g = ws.graphe_modifiable(m_id)) {
                for (auto &p : g->plots)
                    graphops::echelle_auto(p);
            }
        });
    });
    auto *grid = bouton_outil("grid", QString(), u"Plusieurs graphes dans la fenêtre"_s, bar, false);
    grid->setPopupMode(QToolButton::InstantPopup);
    auto *gridMenu = new QMenu(grid);
    connect(gridMenu, &QMenu::aboutToShow, this, [this, gridMenu] {
        gridMenu->clear();
        const GraphView *g = m_win.depot().ws.graphe(m_id);
        if (!g)
            return;
        gridMenu->addSection(u"Disposition des graphes"_s);
        const struct {
            int r, c;
            const char *icon, *label;
        } layouts[] = {{1, 1, "1x1", "1 graphe"}, {2, 1, "2x1", "2 graphes l'un au-dessus de l'autre"}, {1, 2, "1x2", "2 graphes côte à côte"},
                       {3, 1, "3x1", "3 graphes empilés"}, {2, 2, "2x2", "4 graphes (2 × 2)"}};
        for (const auto &lt : layouts) {
            auto *a = gridMenu->addAction(icons::obtient(lt.icon), QString::fromUtf8(lt.label), this, [this, r = lt.r, c = lt.c] {
                m_win.depot().edite([&](Seance &ws) { ws.graphe_modifiable(m_id)->definit_grille(r, c); });
            });
            a->setCheckable(true);
            a->setChecked(g->rows == lt.r && g->cols == lt.c);
        }
        gridMenu->addSeparator();
        auto *link = gridMenu->addAction(icons::obtient("link"), u"Abscisses liées (même échelle)"_s, this, [this](bool on) {
            m_win.depot().edite([&](Seance &ws) { ws.graphe_modifiable(m_id)->linkX = on; });
        });
        link->setCheckable(true);
        link->setChecked(g->linkX);
    });
    grid->setMenu(gridMenu);
    m_linkButton = bouton_outil("link", QString(), u"Abscisses liées"_s, bar, false);
    m_linkButton->setCheckable(true);
    connect(m_linkButton, &QToolButton::clicked, this, [this](bool on) {
        m_win.depot().edite([&](Seance &ws) { ws.graphe_modifiable(m_id)->linkX = on; });
    });
    auto *model = bouton_outil("model", u"Modéliser"_s, u"Ajuster un modèle à une courbe"_s, bar);
    connect(model, &QToolButton::clicked, this, [this] { m_win.ouvre_modelisation(m_id, trace_choisi()); });
    auto *exp = bouton_outil("export", QString(), u"Exporter, copier, imprimer"_s, bar, false);
    exp->setPopupMode(QToolButton::InstantPopup);
    auto *expMenu = new QMenu(exp);
    expMenu->addAction(icons::obtient("copy"), u"Copier l'image"_s, this, [this] { m_win.copie_image_graphe(m_id); });
    expMenu->addSeparator();
    expMenu->addAction(icons::obtient("image"), u"Image PNG (haute résolution)…"_s, this, [this] { m_win.exporte_graphe(m_id, exporter::Format::Png); });
    expMenu->addAction(icons::obtient("vector"), u"Image vectorielle SVG…"_s, this, [this] { m_win.exporte_graphe(m_id, exporter::Format::Svg); });
    expMenu->addAction(icons::obtient("pdf"), u"Document PDF…"_s, this, [this] { m_win.exporte_graphe(m_id, exporter::Format::Pdf); });
    expMenu->addSeparator();
    expMenu->addAction(icons::obtient("print"), u"Imprimer…"_s, this, [this] { m_win.imprime_graphe(m_id); });
    exp->setMenu(expMenu);
    m_settingsButton = bouton_outil("sliders", u"Réglages"_s, u"Réglages : titre, axes, courbes, incertitudes, légende…"_s, bar);
    m_settingsButton->setCheckable(true);
    connect(m_settingsButton, &QToolButton::toggled, this, [this](bool on) {
        if (on && m_splitter->width() < largeur_cote_a_cote()) {
            // de la place prise aux fenêtres voisines, si elles en ont : le graphe reste visible
            if (auto *dock = qobject_cast<QDockWidget *>(parentWidget()))
                m_win.elargit_fenetre(dock, dock->width() + largeur_cote_a_cote() - m_splitter->width());
        }
        reglages_modele();
    });
    for (QWidget *w : std::initializer_list<QWidget *>{fit, grid, m_linkButton, model, exp, m_settingsButton})
        flow->addWidget(w);
    layout->addWidget(bar);
    m_narrowHint = etiquette_attenuee(u"Le graphe est caché : la fenêtre est trop étroite pour le graphe et ses réglages. Élargissez-la, ou fermez les réglages (✕)."_s, this);
    m_narrowHint->hide();
    layout->addWidget(m_narrowHint);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_canvas);
    m_splitter->addWidget(m_settings);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setChildrenCollapsible(false);
    m_settings->hide();
    layout->addWidget(m_splitter, 1);
    connect(m_settings, &GraphSettings::fermeture_demandee, this, [this] { m_settingsButton->setChecked(false); });
    connect(m_canvas, &GraphCanvas::reglages_demandes, this, [this](SettingsSection s, int axis) { ouvre_reglages(s, axis); });
    connect(m_canvas, &GraphCanvas::trace_selectionne, m_settings, &GraphSettings::choisit_trace);
    connect(m_settings, &GraphSettings::surbrillance_changee, m_canvas, &GraphCanvas::met_en_surbrillance);
    connect(&win.depot(), &Store::espace_travail_change, this, &GraphPanel::rafraichit);
    rafraichit();
}

int GraphPanel::largeur_cote_a_cote() const
{
    const int settings = std::max(m_settings->minimumWidth(), m_settings->minimumSizeHint().width());
    return m_canvas->minimumWidth() + m_splitter->handleWidth() + settings;
}

void GraphPanel::reglages_modele()
{
    const bool open = m_settingsButton->isChecked();
    const bool side = !open || m_splitter->width() >= largeur_cote_a_cote();
    m_settings->setVisible(open);
    m_canvas->setVisible(side);
    m_narrowHint->setVisible(!side);
    if (open && side && !m_sideBySide) {
        // réglages à leur largeur minimale, le graphe prend le reste
        const int settings = std::max(m_settings->minimumWidth(), m_settings->minimumSizeHint().width());
        m_splitter->setSizes({m_splitter->width() - settings - m_splitter->handleWidth(), settings});
    }
    m_sideBySide = open && side;
}

void GraphPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_settingsButton->isChecked())
        reglages_modele();
}

int GraphPanel::trace_choisi() const
{
    const auto &sp = m_win.depot().selectedPlot;
    auto it = sp.find(m_id);
    return it == sp.end() ? 0 : it->second;
}

void GraphPanel::ouvre_reglages(SettingsSection section, int axis)
{
    m_settingsButton->setChecked(true);
    m_settings->montre_section(section, axis);
}

void GraphPanel::rafraichit()
{
    const GraphView *g = m_win.depot().ws.graphe(m_id);
    if (!g)
        return;
    m_linkButton->setVisible(g->rows * g->cols > 1);
    QSignalBlocker b(m_linkButton);
    m_linkButton->setChecked(g->linkX);
    m_linkButton->setIcon(icons::obtient(g->linkX ? "link" : "unlink"));
    m_linkButton->setToolTip(g->linkX ? u"Abscisses liées"_s : u"Abscisses indépendantes"_s);
}
