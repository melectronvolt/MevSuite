#include "plot/resolve.h"

#include "core/numeric.h"

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

std::pair<std::vector<double>, std::vector<double>> pairsInRange(const Document &doc, QId x, QId y, std::optional<std::pair<double, double>> range)
{
    const auto &xs = doc.values(x);
    const auto &ys = doc.values(y);
    double lo = -INFINITY, hi = INFINITY;
    if (range) {
        lo = std::min(range->first, range->second);
        hi = std::max(range->first, range->second);
    }
    std::vector<double> ox, oy;
    for (size_t i = 0; i < std::min(xs.size(), ys.size()); ++i) {
        if (std::isfinite(xs[i]) && std::isfinite(ys[i]) && xs[i] >= lo && xs[i] <= hi) {
            ox.push_back(xs[i]);
            oy.push_back(ys[i]);
        }
    }
    return {ox, oy};
}

void computeFit(FitSpec &spec, const Document &doc)
{
    spec.fitted = true;
    spec.result.reset();
    spec.resultError.clear();
    spec.paramNames.clear();
    const Quantity *xq = doc.get(spec.x);
    if (!xq) {
        spec.resultError = u"grandeur x introuvable"_s;
        return;
    }
    if (!doc.get(spec.y)) {
        spec.resultError = u"grandeur y introuvable"_s;
        return;
    }
    QString err;
    auto model = fit::Model::make(spec.kind, xq->name, doc.paramValues(), &err);
    if (!model) {
        spec.resultError = err;
        return;
    }
    const auto [xs, ys] = pairsInRange(doc, spec.x, spec.y, spec.range);
    std::vector<std::optional<double>> fixed;
    for (const QString &n : model->params()) {
        std::optional<double> v;
        for (const auto &[k, val] : spec.fixed) {
            if (k == n)
                v = val;
        }
        fixed.push_back(v);
    }
    fit::FitOptions opts;
    opts.fixed = fixed;
    if (!spec.initial.empty() && !model->kind().isLinear()) {
        auto init = fit::initialGuess(*model, xs, ys, fixed);
        for (qsizetype i = 0; i < model->params().size(); ++i) {
            for (const auto &[k, val] : spec.initial) {
                if (k == model->params()[i])
                    init[i] = val;
            }
        }
        opts.initial = init;
    }
    spec.result = fit::fit(*model, xs, ys, opts, &err);
    if (!spec.result)
        spec.resultError = err;
    spec.paramNames = model->params();
}

void updateFits(Plot &plot, const Document &doc)
{
    for (auto &spec : plot.fits) {
        const QString key = spec.settingsKey();
        if (!spec.fitted || spec.fittedRevision != doc.revision() || spec.fittedSettings != key) {
            computeFit(spec, doc);
            spec.fittedRevision = doc.revision();
            spec.fittedSettings = key;
        }
    }
}

std::optional<std::pair<titration::Curve, titration::Tangents>> computeTangents(const TangentSpec &spec, const Document &doc, QString *error)
{
    auto curve = titration::Curve::make(doc.values(spec.x), doc.values(spec.y), error);
    if (!curve)
        return std::nullopt;
    const double contact = spec.contact.value_or(curve->defaultContact());
    auto t = curve->tangents(contact, error);
    if (!t)
        return std::nullopt;
    return std::make_pair(*curve, *t);
}

std::pair<QString, QString> namesAndUnit(const Document &doc, const std::vector<QId> &ids)
{
    QStringList names, units;
    for (QId id : ids) {
        if (const Quantity *q = doc.get(id)) {
            if (!names.contains(q->name))
                names << q->name;
            if (!units.contains(q->unit))
                units << q->unit;
        }
    }
    return {names.join(u", "_s), units.size() == 1 ? units[0] : QString()};
}

PlotData resolve(const Plot &plot, const Document &doc, NumberStyle style)
{
    PlotData data;
    std::vector<QId> xs, left, right;
    for (const auto &c : plot.curves) {
        if (!c.visible || !doc.get(c.x) || !doc.get(c.y))
            continue;
        xs.push_back(c.x);
        (c.side == YSide::Left ? left : right).push_back(c.y);
    }
    std::tie(data.xName, data.xUnit) = namesAndUnit(doc, xs);
    std::tie(data.yName, data.yUnit) = namesAndUnit(doc, left);
    std::tie(data.y2Name, data.y2Unit) = namesAndUnit(doc, right);

    for (size_t k = 0; k < plot.curves.size(); ++k) {
        const CurveSpec &c = plot.curves[k];
        const Quantity *xq = doc.get(c.x);
        const Quantity *yq = doc.get(c.y);
        if (!c.visible || !xq || !yq)
            continue;
        Series s;
        s.xs = xq->values;
        s.ys = yq->values;
        if (xq->uncertainty)
            s.ux = doc.values(*xq->uncertainty);
        if (yq->uncertainty)
            s.uy = doc.values(*yq->uncertainty);
        // incertitudes choisies côté par côté pour cette courbe
        const auto side = [&](std::optional<QId> id, std::vector<double> &out) {
            if (id && doc.get(*id))
                out = doc.values(*id);
        };
        side(c.errXLow, s.uxLow);
        side(c.errXHigh, s.uxHigh);
        side(c.errYLow, s.uyLow);
        side(c.errYHigh, s.uyHigh);
        s.color = c.color;
        s.marker = c.marker;
        s.markerSize = c.markerSize;
        s.line = c.line;
        s.lineWidth = c.lineWidth;
        s.dash = c.dash;
        s.errors = c.errors;
        s.errorWidth = c.errorWidth;
        s.capSize = c.capSize;
        s.side = c.side;
        if (c.inLegend)
            s.legend = c.label.value_or(yq->name);
        s.bars = c.barWidth;
        s.curve = static_cast<int>(k);
        data.series.push_back(std::move(s));
    }

    // courbes des modèles
    for (const auto &f : plot.fits) {
        if (!f.visible || !f.result)
            continue;
        const Quantity *xq = doc.get(f.x);
        if (!xq)
            continue;
        auto model = fit::Model::make(f.kind, xq->name, doc.paramValues());
        if (!model)
            continue;
        const auto [px, py] = pairsInRange(doc, f.x, f.y, f.range);
        const auto r = numeric::finiteRange(px);
        if (!r)
            continue;
        const auto [lo, hi] = *r;
        const int n = 400;
        const bool logX = plot.x.log && lo > 0.0;
        std::vector<double> grid(n);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / (n - 1);
            grid[i] = logX ? std::pow(10.0, std::log10(lo) + t * (std::log10(hi) - std::log10(lo))) : lo + t * (hi - lo);
        }
        Series s;
        s.ys = model->eval(f.result->params, grid);
        s.xs = std::move(grid);
        s.color = f.color;
        s.marker = Marker::None;
        s.line = LineKind::Straight;
        s.lineWidth = 1.8;
        s.side = f.side;
        s.legend = f.label && !f.label->trimmed().isEmpty() ? *f.label : u"Modèle : "_s + f.kind.label().toLower();
        s.autoscale = false;
        // une fonction : tracée sur tout le graphe affiché (ou de a à b), pas seulement sur les points
        if (f.draw != FitDraw::Points) {
            auto shared = std::make_shared<const fit::Model>(std::move(*model));
            const std::vector<double> params = f.result->params;
            s.function = [shared, params](const std::vector<double> &x) { return shared->eval(params, x); };
            if (f.draw == FitDraw::Custom)
                s.domain = std::minmax(f.drawRange.first, f.drawRange.second);
        }
        data.series.push_back(std::move(s));
    }

    // méthode des tangentes
    for (const auto &t : plot.tangents) {
        auto res = computeTangents(t, doc);
        if (!res)
            continue;
        const auto &tg = res->second;
        auto line = [&](double intercept, Dash dash) {
            Overlay o;
            o.kind = Overlay::Line;
            o.a = tg.slope;
            o.b = intercept;
            o.side = t.side;
            o.color = t.color;
            o.width = 1.2;
            o.dash = dash;
            return o;
        };
        data.overlays.push_back(line(tg.b1, Dash::Solid));
        data.overlays.push_back(line(tg.b2, Dash::Solid));
        data.overlays.push_back(line(tg.bMedian, Dash::Dashed));
        const QString xu = doc.get(t.x) ? prettyUnit(doc.get(t.x)->unit) : QString();
        for (const auto &p : {tg.p1, tg.p2}) {
            DataLabel l;
            l.x = p.first;
            l.y = p.second;
            l.side = t.side;
            l.color = t.color;
            l.dot = true;
            data.labels.push_back(l);
        }
        DataLabel e;
        e.x = tg.equivalence.first;
        e.y = tg.equivalence.second;
        e.side = t.side;
        e.text = u"E : %1 = %2 %3 ; %4 = %5"_s.arg(doc.name(t.x), formatSig(e.x, 4, style), xu, doc.name(t.y), formatSig(e.y, 3, style));
        e.color = t.color;
        e.dx = 10;
        e.dy = 14;
        e.dot = true;
        data.labels.push_back(e);
    }

    // annotations
    for (const auto &a : plot.annotations) {
        switch (a.kind) {
        case Annotation::VLine: {
            data.overlays.push_back({Overlay::VLine, a.x, 0.0, a.side, a.color, 1.2, a.dash});
            if (!a.label.isEmpty()) {
                DataLabel l;
                l.x = a.x;
                l.y = NAN;
                l.side = a.side;
                l.text = a.label;
                l.color = a.color;
                l.size = 11.0;
                l.dx = 4;
                data.labels.push_back(l);
            }
            break;
        }
        case Annotation::HLine:
            data.overlays.push_back({Overlay::HLine, a.y, 0.0, a.side, a.color, 1.2, a.dash});
            break;
        case Annotation::Text: {
            DataLabel l;
            l.x = a.x;
            l.y = a.y;
            l.side = a.side;
            l.text = a.text;
            l.color = a.color;
            l.size = a.size;
            data.labels.push_back(l);
            break;
        }
        case Annotation::Line: {
            // droite par deux points ; points confondus : rien ; même abscisse : verticale
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.x2) || !std::isfinite(a.y2) || (a.x == a.x2 && a.y == a.y2))
                break;
            if (a.x == a.x2) {
                data.overlays.push_back({Overlay::VLine, a.x, 0.0, a.side, a.color, 1.2, a.dash});
            } else {
                const double slope = (a.y2 - a.y) / (a.x2 - a.x);
                data.overlays.push_back({Overlay::Line, slope, a.y - slope * a.x, a.side, a.color, 1.2, a.dash});
            }
            if (!a.label.isEmpty()) {
                DataLabel l;
                l.x = a.x2;
                l.y = a.y2;
                l.side = a.side;
                l.text = a.label;
                l.color = a.color;
                l.size = 11.0;
                l.dx = 6;
                l.dy = -6;
                data.labels.push_back(l);
            }
            break;
        }
        }
    }
    return data;
}
