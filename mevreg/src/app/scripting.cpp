#include "app/scripting.h"

#include "core/csv.h"
#include "core/fit.h"
#include "plot/resolve.h"

#include <QColor>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace scripting {

namespace {

bool sameValues(const std::vector<double> &a, const std::vector<double> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i] || (std::isnan(a[i]) && std::isnan(b[i]))))
            return false;
    }
    return true;
}

/// Crée ou met à jour une grandeur produite par le script ; message d'erreur dans `error`.
std::optional<QId> upsert(Workspace &ws, ViewId script, const QString &name, const QString &unit, const std::vector<double> &values, QString *error)
{
    if (const Quantity *q = ws.doc.byName(name)) {
        const QId id = q->id;
        if (q->isCalculated()) {
            if (error)
                *error = u"« %1 » est une grandeur calculée par une formule : choisissez un autre nom"_s.arg(name);
            return std::nullopt;
        }
        const bool changed = !sameValues(q->values, values) || (!unit.isEmpty() && q->unit != unit) || q->origin != script;
        if (changed) {
            std::vector<double> v = values;
            while (!v.empty() && std::isnan(v.back()))
                v.pop_back();
            if (Quantity *m = ws.doc.getMut(id)) {
                m->values = v;
                if (!unit.isEmpty())
                    m->unit = unit;
                m->origin = script;
            }
            ws.doc.recompute();
        }
        return id;
    }
    auto id = ws.doc.addExperimental(name, unit, values);
    if (!id) {
        if (error)
            *error = id.error;
        return std::nullopt;
    }
    if (Quantity *m = ws.doc.getMut(*id))
        m->origin = script;
    return *id;
}

/// Grandeur à tracer pour une série : la grandeur du document de même nom si ses valeurs sont
/// identiques, sinon une grandeur du script.
std::optional<QId> seriesQuantity(Workspace &ws, ViewId script, const std::optional<QString> &name, const std::vector<double> &values,
                                  const QString &fallback, QString *error)
{
    if (name) {
        if (const Quantity *q = ws.doc.byName(*name)) {
            if (sameValues(q->values, values) || q->origin == script) {
                const QId id = q->id;
                if (q->origin == script)
                    upsert(ws, script, *name, {}, values, error);
                return id;
            }
            // nom déjà pris par d'autres données : nom dérivé
            const QString alt = *name + u"_py"_s;
            const Quantity *a = ws.doc.byName(alt);
            if (!a || a->origin == script)
                return upsert(ws, script, alt, {}, values, error);
        } else if (ws.doc.checkName(*name).isEmpty()) {
            return upsert(ws, script, *name, {}, values, error);
        }
    }
    const Quantity *q = ws.doc.byName(fallback);
    if (q && q->origin != script)
        return upsert(ws, script, ws.doc.uniqueName(fallback), {}, values, error);
    return upsert(ws, script, fallback, {}, values, error);
}

Color parseColor(const std::optional<QString> &hex, Color def)
{
    if (!hex)
        return def;
    const QColor c(*hex);
    return c.isValid() ? Color::from(c) : def;
}

Marker markerOf(const std::optional<QString> &m)
{
    if (!m || m->isEmpty() || *m == u"None" || *m == u"none")
        return Marker::None;
    const QString &s = *m;
    if (s == u"." || s == u",")
        return Marker::Point;
    if (s == u"x")
        return Marker::Cross;
    if (s == u"+" || s == u"|" || s == u"_" || s == u"1" || s == u"2" || s == u"3" || s == u"4")
        return Marker::Plus;
    if (s == u"s")
        return Marker::Square;
    if (s == u"^" || s == u"v" || s == u"<" || s == u">")
        return Marker::Triangle;
    if (s == u"D" || s == u"d")
        return Marker::Diamond;
    return Marker::Disc;
}

std::pair<LineKind, Dash> dashOf(const std::optional<QString> &ls)
{
    if (!ls || ls->isEmpty())
        return {LineKind::None, Dash::Solid};
    if (*ls == u"--")
        return {LineKind::Straight, Dash::Dashed};
    if (*ls == u":")
        return {LineKind::Straight, Dash::Dotted};
    if (*ls == u"-.")
        return {LineKind::Straight, Dash::DashDot};
    return {LineKind::Straight, Dash::Solid};
}

void axisText(AxisSpec &spec, const std::optional<QString> &text)
{
    if (text) {
        // la légende de matplotlib contient déjà l'unité
        spec.label = *text;
        spec.unit = QString();
    }
}

void axisLimits(AxisSpec &spec, const figure::Limits &lim)
{
    if (lim && lim->first && lim->second && *lim->first != *lim->second) {
        spec.autoScale = false;
        spec.min = std::min(*lim->first, *lim->second);
        spec.max = std::max(*lim->first, *lim->second);
    }
}

std::vector<double> broadcast(const std::vector<double> &v, size_t n)
{
    if (v.size() == 1)
        return std::vector<double>(n, v[0]);
    return v;
}

std::optional<CurveSpec> seriesCurve(Workspace &ws, ViewId script, const python::FigureMsg &fig, const figure::SeriesDesc &s, const QString &tag,
                                     QString *error)
{
    static const std::vector<double> empty;
    const auto &ys0 = s.y >= 0 && static_cast<size_t>(s.y) < fig.data.size() ? fig.data[s.y] : empty;
    const auto &xs0 = s.x >= 0 && static_cast<size_t>(s.x) < fig.data.size() ? fig.data[s.x] : empty;
    const size_t n = std::max(xs0.size(), ys0.size());
    const auto xs = broadcast(xs0, n);
    const auto ys = broadcast(ys0, n);
    const auto xq = seriesQuantity(ws, script, s.xname, xs, tag + u"_x"_s, error);
    if (!xq)
        return std::nullopt;
    const auto yq = seriesQuantity(ws, script, s.yname, ys, tag + u"_y"_s, error);
    if (!yq)
        return std::nullopt;
    auto [line, dash] = dashOf(s.linestyle);
    Marker marker = markerOf(s.marker);
    if (s.kind == u"scatter") {
        line = LineKind::None;
        if (marker == Marker::None)
            marker = Marker::Disc;
    }
    // incertitudes : grandeurs u_… rattachées (créées par le script)
    bool hasY = false, hasX = false;
    for (auto [err, isY] : {std::make_pair(s.yerr, true), std::make_pair(s.xerr, false)}) {
        if (!err || *err < 0 || static_cast<size_t>(*err) >= fig.data.size())
            continue;
        const QId owner = isY ? *yq : *xq;
        const Quantity *oq = ws.doc.get(owner);
        if (!oq)
            continue;
        const QString uname = u"u_"_s + oq->name;
        const std::optional<QId> existing = oq->uncertainty;
        const bool mine = oq->origin == script;
        std::optional<QId> uq;
        if (existing && !mine && ws.doc.get(*existing) && ws.doc.get(*existing)->origin != script) {
            // incertitude déjà rattachée par l'utilisateur : on la garde
            uq = existing;
        } else {
            const Quantity *byName = ws.doc.byName(uname);
            if (byName && byName->origin != script)
                continue;
            uq = upsert(ws, script, uname, {}, broadcast(fig.data[*err], n), error);
            if (!uq)
                return std::nullopt;
        }
        ws.doc.setUncertainty(owner, uq);
        (isY ? hasY : hasX) = true;
    }
    CurveSpec c;
    c.x = *xq;
    c.y = *yq;
    c.side = s.right ? YSide::Right : YSide::Left;
    c.color = parseColor(s.color, PALETTE[0]);
    c.marker = marker;
    c.markerSize = s.markersize ? std::clamp(*s.markersize, 2.0, 20.0) : (marker == Marker::Point ? 5.0 : 7.0);
    c.line = s.kind == u"bar" ? LineKind::None : line;
    c.lineWidth = s.linewidth ? std::clamp(*s.linewidth * 1.1, 0.3, 8.0) : 1.6;
    c.dash = dash;
    c.errors = hasX ? ErrorBars::Both : hasY ? ErrorBars::Vertical : ErrorBars::None;
    c.label = s.label;
    c.inLegend = s.label.has_value();
    if (s.kind == u"bar")
        c.barWidth = s.width.value_or(0.8);
    return c;
}

Plot buildPlot(Workspace &ws, ViewId script, const python::FigureMsg &fig, const figure::AxesDesc &ax, size_t k, QStringList &msgs)
{
    Plot plot;
    if (ax.title)
        plot.title.text = *ax.title;
    axisText(plot.x, ax.xlabel);
    axisText(plot.y, ax.ylabel);
    axisText(plot.y2, ax.y2label);
    axisLimits(plot.x, ax.xlim);
    axisLimits(plot.y, ax.ylim);
    axisLimits(plot.y2, ax.y2lim);
    // matplotlib n'affiche quadrillage et légende que sur demande
    plot.x.gridMajor = ax.grid;
    plot.y.gridMajor = ax.grid;
    plot.legend.visible = ax.legend;
    plot.x.log = ax.xscale == u"log"_s;
    plot.y.log = ax.yscale == u"log"_s;
    plot.y2.log = ax.y2scale == u"log"_s;
    const QString figTag = csv::sanitizeName(fig.desc.name, u"fig"_s).toLower();
    for (size_t j = 0; j < ax.series.size(); ++j) {
        QString err;
        if (auto c = seriesCurve(ws, script, fig, ax.series[j], u"%1_%2%3"_s.arg(figTag).arg(k + 1).arg(j + 1), &err))
            plot.curves.push_back(*c);
        else
            msgs << err;
    }
    for (const auto &l : ax.hlines) {
        Annotation a;
        a.kind = Annotation::HLine;
        a.y = l.value;
        a.color = parseColor(l.color, colors::BLACK);
        a.side = l.right ? YSide::Right : YSide::Left;
        a.label = l.label.value_or(QString());
        a.dash = dashOf(l.linestyle.value_or(u"-"_s)).second;
        plot.annotations.push_back(a);
    }
    for (const auto &l : ax.vlines) {
        Annotation a;
        a.kind = Annotation::VLine;
        a.x = l.value;
        a.color = parseColor(l.color, colors::BLACK);
        a.label = l.label.value_or(QString());
        a.dash = dashOf(l.linestyle.value_or(u"-"_s)).second;
        plot.annotations.push_back(a);
    }
    for (const auto &t : ax.texts) {
        Annotation a;
        a.kind = Annotation::Text;
        a.x = t.x;
        a.y = t.y;
        a.text = t.text;
        a.color = parseColor(t.color, colors::TEXT);
        a.side = t.right ? YSide::Right : YSide::Left;
        a.dash = Dash::Solid;
        plot.annotations.push_back(a);
    }
    return plot;
}

/// Graphe correspondant à une figure du script (créé au besoin) ; renvoie son identifiant et
/// s'il vient d'être créé.
std::pair<ViewId, bool> upsertFigure(Workspace &ws, ViewId script, const python::FigureMsg &fig, QStringList &msgs)
{
    const FigureOrigin origin{script, fig.desc.name};
    ViewId id = 0;
    bool created = false;
    for (const auto &g : ws.graphs) {
        if (g.origin == origin)
            id = g.id;
    }
    if (!id) {
        id = ws.addGraph();
        GraphView *g = ws.graphMut(id);
        g->name = fig.desc.name;
        g->origin = origin;
        created = true;
    }
    const int rows = std::clamp(fig.desc.rows, 1, 4), cols = std::clamp(fig.desc.cols, 1, 4);
    std::vector<Plot> plots(static_cast<size_t>(rows * cols));
    for (size_t k = 0; k < fig.desc.axes.size(); ++k) {
        const auto &ax = fig.desc.axes[k];
        const size_t slot = static_cast<size_t>(std::max(ax.index - 1, 0));
        if (slot < plots.size())
            plots[slot] = buildPlot(ws, script, fig, ax, k, msgs);
    }
    if (fig.desc.suptitle && plots[0].title.text.isEmpty())
        plots[0].title.text = *fig.desc.suptitle;
    GraphView *g = ws.graphMut(id);
    // l'utilisateur a pu zoomer : on garde les bornes réglées à la souris si la figure ne les fixe pas
    for (size_t i = 0; i < plots.size() && i < g->plots.size(); ++i) {
        const std::array<std::pair<AxisSpec *, AxisSpec *>, 3> axes = {
            {{&plots[i].x, &g->plots[i].x}, {&plots[i].y, &g->plots[i].y}, {&plots[i].y2, &g->plots[i].y2}}};
        for (auto [n, o] : axes) {
            if (n->autoScale && !o->autoScale && !created) {
                n->autoScale = false;
                n->min = o->min;
                n->max = o->max;
            }
        }
    }
    g->rows = rows;
    g->cols = cols;
    g->plots = std::move(plots);
    return {id, created};
}

const Color *frenchColor(const QString &name)
{
    static const std::vector<std::pair<QString, Color>> names = {
        {u"bleu"_s, PALETTE[0]}, {u"orange"_s, PALETTE[1]}, {u"vert"_s, PALETTE[2]}, {u"rouge"_s, PALETTE[3]},
        {u"violet"_s, PALETTE[4]}, {u"marron"_s, PALETTE[5]}, {u"rose"_s, PALETTE[6]}, {u"gris"_s, PALETTE[7]},
        {u"olive"_s, PALETTE[8]}, {u"cyan"_s, PALETTE[9]}, {u"noir"_s, colors::BLACK},
    };
    for (const auto &[n, c] : names)
        if (n == name.toLower())
            return &c;
    return nullptr;
}

std::optional<ErrorBars> errorsFrom(const QString &s)
{
    const QString t = s.toLower();
    if (t == u"barres" || t == u"verticales")
        return ErrorBars::Vertical;
    if (t == u"deux" || t == u"croix")
        return ErrorBars::Both;
    if (t == u"rectangles")
        return ErrorBars::Rectangles;
    if (t == u"aucune")
        return ErrorBars::None;
    return std::nullopt;
}

/// Graphe du nom donné ; créé s'il n'existe pas et que `create` est vrai.
GraphView *graphNamed(Workspace &ws, const QString &name, bool create, bool *created = nullptr)
{
    for (auto &g : ws.graphs)
        if (g.name == name)
            return &g;
    if (!create)
        return nullptr;
    const ViewId id = ws.addGraph();
    GraphView *g = ws.graphMut(id);
    g->name = name;
    if (created)
        *created = true;
    return g;
}

/// Courbe y(x) sur le premier graphe de `g`, si elle n'y est pas ; ses options (couleur…) sinon.
void addCurve(Workspace &ws, Plot &plot, QId x, QId y, const QJsonObject &o, QStringList &messages)
{
    CurveSpec *c = nullptr;
    for (auto &cs : plot.curves)
        if (cs.x == x && cs.y == y)
            c = &cs;
    if (!c)
        c = &plot.addCurve(x, y);
    const Quantity *xq = ws.doc.get(x), *yq = ws.doc.get(y);
    if (yq && yq->uncertainty && c->errors == ErrorBars::None)
        c->errors = (xq && xq->uncertainty) ? ErrorBars::Both : ErrorBars::Vertical;
    // incertitudes côté par côté : une grandeur pour chaque côté
    bool sided = false;
    for (const auto &[key, side] : {std::pair(u"x_bas"_s, &c->errXLow), std::pair(u"x_haut"_s, &c->errXHigh),
                                    std::pair(u"y_bas"_s, &c->errYLow), std::pair(u"y_haut"_s, &c->errYHigh)}) {
        const QString n = o.value(key).toString();
        if (n.isEmpty())
            continue;
        if (const Quantity *q = ws.doc.byName(n)) {
            *side = q->id;
            sided = true;
        } else {
            messages << u"mevreg : pas de grandeur « %1 »"_s.arg(n);
        }
    }
    if (sided && c->errors == ErrorBars::None)
        c->errors = (c->errXLow || c->errXHigh) ? ErrorBars::Both : ErrorBars::Vertical;
    if (const QString e = o.value(u"incertitudes"_s).toString(); !e.isEmpty()) {
        if (const auto eb = errorsFrom(e))
            c->errors = *eb;
        else
            messages << u"incertitudes : « %1 » inconnu (barres, deux, rectangles, aucune)"_s.arg(e);
    }
    if (const QString col = o.value(u"couleur"_s).toString(); !col.isEmpty()) {
        if (const Color *fc = frenchColor(col))
            c->color = *fc;
        else if (QColor q(col); q.isValid())
            c->color = Color::from(q);
        else
            messages << u"couleur « %1 » inconnue"_s.arg(col);
    }
    static const std::vector<std::pair<QString, Marker>> markers = {
        {u"aucun"_s, Marker::None}, {u"point"_s, Marker::Point}, {u"disque"_s, Marker::Disc}, {u"rond"_s, Marker::Circle},
        {u"croix"_s, Marker::Cross}, {u"plus"_s, Marker::Plus}, {u"carre"_s, Marker::Square}, {u"carré"_s, Marker::Square},
        {u"triangle"_s, Marker::Triangle}, {u"losange"_s, Marker::Diamond}};
    if (const QString m = o.value(u"marqueur"_s).toString(); !m.isEmpty())
        for (const auto &[n, mk] : markers)
            if (n == m.toLower())
                c->marker = mk;
    if (const QString t = o.value(u"trait"_s).toString().toLower(); !t.isEmpty())
        c->line = t == u"droit" ? LineKind::Straight : t == u"lisse" ? LineKind::Cubic : LineKind::None;
    plot.x.autoScale = plot.y.autoScale = true;
}

/// Grandeur du script (`grandeur()`, `mr.creer`) mise dans le document ; `newIds` reçoit celles
/// qui n'existaient pas.
void applyQuantity(Workspace &ws, ViewId script, const python::NamedValues &q, Applied &res, std::vector<QId> &newIds)
{
    const bool existed = ws.doc.byName(q.name) != nullptr;
    QString err;
    if (auto id = upsert(ws, script, q.name, q.unit, q.values, &err)) {
        if (!existed)
            newIds.push_back(*id);
    } else {
        res.messages << err;
    }
}

/// Ordres du module `mevreg`, dans l'ordre du script : ceux qui touchent le document sont faits
/// ici (les grandeurs données par `grandeur()` ou `mr.creer` à leur place, repère « grandeur »), les
/// autres (fenêtres, fichiers, messages) passent à la fenêtre principale.
void applyCommands(Workspace &ws, ViewId script, const python::ScriptOutput &out, Applied &res, std::vector<QId> &newIds)
{
    const QJsonArray &commands = out.commands;
    auto quantity = [&](const QJsonObject &o, const QString &key) -> std::optional<QId> {
        const QString n = o.value(key).toString();
        if (const Quantity *q = ws.doc.byName(n))
            return q->id;
        res.messages << u"mevreg : pas de grandeur « %1 »"_s.arg(n);
        return std::nullopt;
    };
    for (const QJsonValue &v : commands) {
        const QJsonObject o = v.toObject();
        const QString order = o.value(u"ordre"_s).toString();
        const QString name = o.value(u"nom"_s).toString();
        if (order == u"grandeur") {
            // les valeurs de cet appel ; à défaut, les dernières données sous ce nom
            const qint64 k = o.value(u"indice"_s).toInteger(-1);
            if (k >= 0 && k < static_cast<qint64>(out.given.size()) && out.given[static_cast<size_t>(k)].name == name) {
                applyQuantity(ws, script, out.given[static_cast<size_t>(k)], res, newIds);
            } else {
                for (const auto &q : out.quantities)
                    if (q.name == name)
                        applyQuantity(ws, script, q, res, newIds);
            }
        } else if (order == u"calculer") {
            const QString formula = o.value(u"formule"_s).toString(), unit = o.value(u"unite"_s).toString();
            if (const Quantity *q = ws.doc.byName(name)) {
                const QId id = q->id;
                if (const QString e = ws.doc.setFormula(id, formula); !e.isEmpty())
                    res.messages << u"%1 : %2"_s.arg(name, e);
                if (!unit.isEmpty())
                    ws.doc.setUnit(id, unit);
            } else if (auto r = ws.doc.addCalculated(name, unit, formula); !r) {
                res.messages << u"%1 : %2"_s.arg(name, r.error);
            }
        } else if (order == u"unite") {
            if (const auto id = quantity(o, u"nom"_s))
                ws.doc.setUnit(*id, o.value(u"unite"_s).toString());
        } else if (order == u"renommer") {
            if (const auto id = quantity(o, u"nom"_s))
                if (const QString e = ws.renameQuantity(*id, o.value(u"nouveau"_s).toString()); !e.isEmpty())
                    res.messages << e;
        } else if (order == u"supprimer") {
            if (const auto id = quantity(o, u"nom"_s))
                ws.deleteQuantity(*id);
        } else if (order == u"incertitude") {
            if (const auto id = quantity(o, u"nom"_s)) {
                const QString formula = o.value(u"formule"_s).toString();
                const Quantity *q = ws.doc.get(*id);
                if (q && q->uncertainty) {
                    if (const QString e = ws.doc.setFormula(*q->uncertainty, formula); !e.isEmpty())
                        res.messages << e;
                } else if (auto r = ws.doc.createUncertainty(*id, formula); !r) {
                    res.messages << r.error;
                }
            }
        } else if (order == u"parametre") {
            const double value = o.value(u"valeur"_s).toDouble();
            if (Parameter *p = ws.doc.paramByName(name) ? ws.doc.paramMut(ws.doc.paramByName(name)->id) : nullptr) {
                p->value = value;
                if (o.contains(u"unite"_s) && !o.value(u"unite"_s).toString().isEmpty())
                    p->unit = o.value(u"unite"_s).toString();
            } else if (auto r = ws.doc.addParameter(name, o.value(u"unite"_s).toString(), value); !r) {
                res.messages << r.error;
            }
        } else if (order == u"tableur") {
            std::vector<QId> cols;
            for (const QJsonValue &c : o.value(u"colonnes"_s).toArray())
                if (const Quantity *q = ws.doc.byName(c.toString()))
                    cols.push_back(q->id);
                else
                    res.messages << u"mevreg : pas de grandeur « %1 »"_s.arg(c.toString());
            TableView *t = nullptr;
            for (auto &tv : ws.tables)
                if (tv.name == name)
                    t = &tv;
            if (!t) {
                t = ws.tableMut(ws.addTable({}));
                t->name = name;
            }
            t->columns = cols;
        } else if (order == u"graphe" || order == u"courbe") {
            const QString gname = order == u"graphe" ? name : o.value(u"graphe"_s).toString();
            bool created = false;
            GraphView *g = graphNamed(ws, gname, true, &created);
            if (created)
                res.openGraphs.push_back(g->id);
            if (o.contains(u"x"_s) && o.contains(u"y"_s)) {
                const auto x = quantity(o, u"x"_s);
                QStringList ys;
                if (o.value(u"y"_s).isArray())
                    for (const QJsonValue &y : o.value(u"y"_s).toArray())
                        ys << y.toString();
                else
                    ys << o.value(u"y"_s).toString();
                for (const QString &yn : ys) {
                    const Quantity *yq = ws.doc.byName(yn);
                    if (!yq) {
                        res.messages << u"mevreg : pas de grandeur « %1 »"_s.arg(yn);
                        continue;
                    }
                    if (x)
                        addCurve(ws, ws.graphMut(g->id)->plots[0], *x, yq->id, o, res.messages);
                }
            }
        } else if (order == u"retirer_courbe") {
            if (GraphView *g = graphNamed(ws, o.value(u"graphe"_s).toString(), false))
                if (const auto y = quantity(o, u"y"_s))
                    for (auto &p : g->plots)
                        for (size_t k = p.curves.size(); k-- > 0;)
                            if (p.curves[k].y == *y)
                                p.removeCurve(k);
        } else if (order == u"axes") {
            GraphView *g = graphNamed(ws, o.value(u"graphe"_s).toString(), false);
            if (!g) {
                res.messages << u"mevreg : pas de graphe « %1 »"_s.arg(o.value(u"graphe"_s).toString());
                continue;
            }
            Plot &p = g->plots[0];
            // étendue des valeurs tracées sur un axe (y : les courbes de l'axe de gauche)
            const auto extent = [&](bool xAxis) -> std::optional<std::pair<double, double>> {
                double low = INFINITY, high = -INFINITY;
                for (const auto &c : p.curves) {
                    if (!c.visible || (!xAxis && c.side != YSide::Left))
                        continue;
                    for (double v : ws.doc.values(xAxis ? c.x : c.y))
                        if (std::isfinite(v)) {
                            low = std::min(low, v);
                            high = std::max(high, v);
                        }
                }
                return low <= high ? std::optional(std::pair(low, high)) : std::nullopt;
            };
            // une seule borne donnée : l'autre est prise sur les valeurs tracées, avec une marge
            auto bounds = [&](AxisSpec &a, const QString &lo, const QString &hi, bool xAxis) {
                if (!o.contains(lo) && !o.contains(hi))
                    return;
                double min = o.value(lo).toDouble(), max = o.value(hi).toDouble();
                if (!o.contains(lo) || !o.contains(hi)) {
                    const auto e = extent(xAxis);
                    if (!e) {
                        res.messages << u"mevreg : axes, rien n'est tracé : donnez les deux bornes (%1 et %2)"_s.arg(lo, hi);
                        return;
                    }
                    if (o.contains(lo))
                        max = e->second + 0.05 * (e->second - min);
                    else
                        min = e->first - 0.05 * (max - e->first);
                }
                if (!(min < max)) {
                    res.messages << u"mevreg : axes, %1 doit être plus petit que %2"_s.arg(lo, hi);
                    return;
                }
                a.autoScale = false;
                a.min = min;
                a.max = max;
            };
            bounds(p.x, u"xmin"_s, u"xmax"_s, true);
            bounds(p.y, u"ymin"_s, u"ymax"_s, false);
            if (o.contains(u"titre"_s))
                p.title.text = o.value(u"titre"_s).toString();
            if (o.contains(u"x_titre"_s))
                p.x.label = o.value(u"x_titre"_s).toString();
            if (o.contains(u"y_titre"_s))
                p.y.label = o.value(u"y_titre"_s).toString();
        } else if (order == u"annotation") {
            GraphView *g = graphNamed(ws, o.value(u"graphe"_s).toString(), false);
            if (!g) {
                res.messages << u"mevreg : pas de graphe « %1 »"_s.arg(o.value(u"graphe"_s).toString());
                continue;
            }
            Annotation a;
            const QString t = o.value(u"type"_s).toString();
            a.kind = t == u"horizontale" ? Annotation::HLine : t == u"oblique" ? Annotation::Line : t == u"texte" ? Annotation::Text : Annotation::VLine;
            a.x = o.value(u"x"_s).toDouble();
            a.y = o.value(u"y"_s).toDouble();
            a.x2 = o.value(u"x2"_s).toDouble(a.x + 1);
            a.y2 = o.value(u"y2"_s).toDouble(a.y + 1);
            if (a.kind == Annotation::Text) {
                a.text = o.value(u"texte"_s).toString();
                a.size = o.value(u"taille"_s).toDouble(12.0);
                a.color = colors::TEXT;
                a.dash = Dash::Solid;
            } else {
                a.label = o.value(u"texte"_s).toString();
                if (a.kind == Annotation::Line)
                    a.dash = Dash::Solid;
            }
            a.locked = o.value(u"verrou"_s).toBool();
            g->plots[0].annotations.push_back(a);
        } else if (order == u"effacer_annotations" || order == u"retirer_tangentes") {
            GraphView *g = graphNamed(ws, o.value(u"graphe"_s).toString(), false);
            if (!g) {
                res.messages << u"mevreg : pas de graphe « %1 »"_s.arg(o.value(u"graphe"_s).toString());
                continue;
            }
            if (order == u"effacer_annotations")
                g->plots[0].annotations.clear();
            else
                g->plots[0].tangents.clear();
        } else if (order == u"tangentes") {
            const auto x = quantity(o, u"x"_s), y = quantity(o, u"y"_s);
            GraphView *g = graphNamed(ws, o.value(u"graphe"_s).toString(), false);
            if (!g) {
                res.messages << u"mevreg : pas de graphe « %1 »"_s.arg(o.value(u"graphe"_s).toString());
                continue;
            }
            if (!x || !y)
                continue;
            Plot &p = g->plots[0];
            if (std::none_of(p.curves.begin(), p.curves.end(), [&](const CurveSpec &c) { return c.x == *x && c.y == *y; }))
                addCurve(ws, p, *x, *y, {}, res.messages);
            // une seule méthode par courbe : la nouvelle remplace l'ancienne
            std::erase_if(p.tangents, [&](const TangentSpec &t) { return t.x == *x && t.y == *y; });
            TangentSpec t;
            t.x = *x;
            t.y = *y;
            if (o.contains(u"contact"_s) && !o.value(u"contact"_s).isNull())
                t.contact = o.value(u"contact"_s).toDouble();
            t.locked = o.value(u"verrou"_s).toBool();
            QString error;
            if (!computeTangents(t, ws.doc, &error))
                res.messages << u"mevreg : méthode des tangentes impossible : %1"_s.arg(error);
            p.tangents.push_back(t);
        } else if (order == u"modeliser") {
            const auto x = quantity(o, u"x"_s), y = quantity(o, u"y"_s);
            if (!x || !y)
                continue;
            GraphView *g = o.contains(u"graphe"_s) ? graphNamed(ws, o.value(u"graphe"_s).toString(), true) : nullptr;
            int plot = 0;
            if (!g)
                for (auto &gv : ws.graphs)
                    for (int k = 0; k < static_cast<int>(gv.plots.size()) && !g; ++k)
                        if (std::any_of(gv.plots[k].curves.begin(), gv.plots[k].curves.end(), [&](const CurveSpec &c) { return c.x == *x && c.y == *y; })) {
                            g = &gv;
                            plot = k;
                        }
            if (!g)
                continue;
            Plot &p = g->plots[static_cast<size_t>(plot)];
            if (std::none_of(p.curves.begin(), p.curves.end(), [&](const CurveSpec &c) { return c.x == *x && c.y == *y; }))
                addCurve(ws, p, *x, *y, {}, res.messages);
            fit::ModelKind kind;
            kind.type = fit::ModelKind::typeFromName(o.value(u"type"_s).toString()).value_or(fit::ModelKind::Affine);
            kind.degree = o.value(u"degre"_s).toInt(3);
            kind.expression = o.value(u"expression"_s).toString();
            // un même modèle de la même courbe est remplacé, pas ajouté une fois de plus
            std::erase_if(p.fits, [&](const FitSpec &f) { return f.x == *x && f.y == *y && f.kind == kind; });
            FitSpec f = FitSpec::make(kind, *x, *y, p.nextColor());
            if (const QJsonArray r = o.value(u"plage"_s).toArray(); r.size() == 2)
                f.range = std::minmax(r[0].toDouble(), r[1].toDouble());
            if (const QString n = o.value(u"nom"_s).toString(); !n.isEmpty())
                f.label = n;
            p.fits.push_back(f);
        } else if (order == u"avertir") {
            res.messages << o.value(u"texte"_s).toString();
        } else {
            // fenêtres, fichiers, messages : pour la fenêtre principale
            res.actions.append(o);
        }
    }
}

} // namespace

Applied applyOutput(Workspace &ws, ViewId script, const python::ScriptOutput &out)
{
    Applied res;
    const QString scriptName = ws.script(script) ? ws.script(script)->name : QString();
    std::vector<QId> newIds;
    // variables du script devenues grandeurs à sa fin (listes, tableaux) : d'abord, pour que les
    // ordres puissent s'en servir ; celles données par grandeur() ou mr.creer le sont à leur place
    // parmi les ordres
    QSet<QString> ordered;
    for (const QJsonValue &v : out.commands)
        if (v.toObject().value(u"ordre"_s).toString() == u"grandeur")
            ordered.insert(v.toObject().value(u"nom"_s).toString());
    for (const auto &q : out.quantities)
        if (!ordered.contains(q.name))
            applyQuantity(ws, script, q, res, newIds);
    for (const auto &fig : out.figures) {
        const auto [gid, created] = upsertFigure(ws, script, fig, res.messages);
        if (fig.savefig)
            res.savefig.emplace_back(gid, *fig.savefig);
        else if (created)
            res.openGraphs.push_back(gid);
    }
    applyCommands(ws, script, out, res, newIds);
    // nouvelles grandeurs : dans le tableur des résultats du script
    std::vector<QId> orphans;
    for (QId id : newIds) {
        const bool shown = std::any_of(ws.tables.begin(), ws.tables.end(), [&](const TableView &t) {
            return std::find(t.columns.begin(), t.columns.end(), id) != t.columns.end();
        });
        if (!shown)
            orphans.push_back(id);
    }
    if (!orphans.empty()) {
        const QString title = u"Résultats de "_s + scriptName;
        ViewId tid = 0;
        for (const auto &t : ws.tables) {
            if (t.name == title)
                tid = t.id;
        }
        if (!tid) {
            tid = ws.addTable({});
            ws.tableMut(tid)->name = title;
        }
        QStringList names;
        for (QId q : orphans)
            names << ws.doc.name(q);
        res.messages << u"Nouvelles grandeurs : %1 (tableur « %2 », menu Fenêtres)"_s.arg(names.join(u", "_s), title);
        auto &cols = ws.tableMut(tid)->columns;
        cols.insert(cols.end(), orphans.begin(), orphans.end());
    }
    ws.doc.recompute();
    return res;
}

QString templateCode(const Workspace &ws)
{
    QString x = u"t"_s, y = u"x"_s;
    if (ws.doc.quantities.size() >= 2) {
        x = ws.doc.quantities[0].name;
        y = ws.doc.quantities[1].name;
    }
    return u"import numpy as np\nimport matplotlib.pyplot as plt\n\n"
           "# Les grandeurs du document sont des tableaux numpy : %1, %2…\n"
           "# Une liste de nombres ou un tableau numpy devient une grandeur (sans unité) ;\n"
           "# grandeur(\"nom\", valeurs, \"unité\") en crée une avec son unité. Menu Python → Exemples.\n\n"
           "d%2_d%1 = np.gradient(%2, %1)\n\n"
           "plt.figure(\"Dérivée\")\n"
           "plt.plot(%1, d%2_d%1, 'r+-', label=\"d%2/d%1\")\n"
           "plt.legend()\n"
           "plt.grid()\n"
           "plt.show()\n"_s.arg(x, y);
}

} // namespace scripting
