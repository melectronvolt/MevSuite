#include "plot/resolve.h"

#include "core/numeric.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

std::pair<std::vector<double>, std::vector<double>> paires_dans_plage(const Document &doc, QId x, QId y, std::optional<std::pair<double, double>> range)
{
    const auto &xs = doc.valeurs(x);
    const auto &ys = doc.valeurs(y);
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

void calcule_modele(FitSpec &spec, const Document &doc)
{
    spec.fitted = true;
    spec.result.reset();
    spec.resultError.clear();
    spec.paramNames.clear();
    const Grandeur *xq = doc.obtient(spec.x);
    if (!xq) {
        spec.resultError = u"grandeur x introuvable"_s;
        return;
    }
    if (!doc.obtient(spec.y)) {
        spec.resultError = u"grandeur y introuvable"_s;
        return;
    }
    QString err;
    auto model = fit::Model::fabrique(spec.kind, xq->name, doc.valeurs_parametres(), &err);
    if (!model) {
        spec.resultError = err;
        return;
    }
    const auto [xs, ys] = paires_dans_plage(doc, spec.x, spec.y, spec.range);
    std::vector<std::optional<double>> fixed;
    for (const QString &n : model->parametres()) {
        std::optional<double> v;
        for (const auto &[k, val] : spec.fixed) {
            if (k == n)
                v = val;
        }
        fixed.push_back(v);
    }
    fit::FitOptions opts;
    opts.fixed = fixed;
    if (!spec.initial.empty() && !model->genre().est_lineaire()) {
        auto init = fit::estimation_initiale(*model, xs, ys, fixed);
        for (qsizetype i = 0; i < model->parametres().size(); ++i) {
            for (const auto &[k, val] : spec.initial) {
                if (k == model->parametres()[i])
                    init[i] = val;
            }
        }
        opts.initial = init;
    }
    spec.result = fit::ajuste(*model, xs, ys, opts, &err);
    if (!spec.result)
        spec.resultError = err;
    spec.paramNames = model->parametres();
}

/// La révision change aussi quand on corrige une autre série du TP. L'empreinte
/// permet de garder l'ajustement si ses deux colonnes ont encore les mêmes valeurs.
static QByteArray empreinte_modele(const FitSpec &spec, const Document &doc)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray description;
    QDataStream champs(&description, QIODevice::WriteOnly);
    for (QId id : {spec.x, spec.y}) {
        const Grandeur *q = doc.obtient(id);
        champs << (q ? qint64(q->values.size()) : qint64(-1));
        if (q)
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(q->values.data()), qsizetype(q->values.size() * sizeof(double))));
    }
    if (spec.kind.type == fit::ModelKind::User) {
        const QString abscisse = doc.nom(spec.x);
        champs << abscisse;
        QString formule = spec.kind.expression;
        if (auto definition = expr::decoupe_definition(formule))
            formule = definition->second;
        for (const Parametre &p : doc.parameters)
            if (p.name != abscisse && expr::utilise_identifiant(formule, p.name))
                champs << p.name << p.value;
    }
    hash.addData(description);
    return hash.result();
}

size_t maj_modeles(Plot &plot, const Document &doc)
{
    size_t recalcules = 0;
    for (auto &spec : plot.fits) {
        const QString key = spec.cle_reglages();
        if (spec.fitted && spec.fittedRevision == doc.revision() && spec.fittedSettings == key)
            continue;
        const QByteArray donnees = empreinte_modele(spec, doc);
        if (!spec.fitted || spec.fittedSettings != key || spec.empreinteDonnees != donnees) {
            calcule_modele(spec, doc);
            spec.fittedSettings = key;
            spec.empreinteDonnees = donnees;
            ++recalcules;
        }
        spec.fittedRevision = doc.revision();
    }
    return recalcules;
}

std::optional<std::pair<titration::Curve, titration::Tangents>> calcule_tangentes(const TangentSpec &spec, const Document &doc, QString *error)
{
    auto curve = titration::Curve::fabrique(doc.valeurs(spec.x), doc.valeurs(spec.y), error);
    if (!curve)
        return std::nullopt;
    const double contact = spec.contact.value_or(curve->contact_par_defaut());
    auto t = curve->tangentes(contact, error);
    if (!t)
        return std::nullopt;
    return std::make_pair(*curve, *t);
}

std::pair<QString, QString> noms_et_unite(const Document &doc, const std::vector<QId> &ids)
{
    QStringList names, units;
    for (QId id : ids) {
        if (const Grandeur *q = doc.obtient(id)) {
            if (!names.contains(q->name))
                names << q->name;
            if (!units.contains(q->unit))
                units << q->unit;
        }
    }
    return {names.join(u", "_s), units.size() == 1 ? units[0] : QString()};
}

std::optional<double> volume_equivalence(const CurveSpec &c, const Document &doc)
{
    if (!c.dosage)
        return {};
    const Grandeur *ph = doc.obtient(*c.dosage), *v = doc.obtient(c.x), *derivee = doc.obtient(c.y);
    if (!v || !ph || !derivee || derivee->formula != u"deriv(%1, %2)"_s.arg(ph->name, v->name))
        return {};
    const auto maximum = titration::maximum_derivee(v->values, ph->values, nullptr);
    return maximum ? std::optional(maximum->first) : std::nullopt;
}

PlotData resout(const Plot &plot, const Document &doc, NumberStyle style)
{
    PlotData data;
    std::vector<QId> xs, left, right;
    for (const auto &c : plot.curves) {
        if (!c.visible || !doc.obtient(c.x) || !doc.obtient(c.y))
            continue;
        xs.push_back(c.x);
        (c.side == YSide::Left ? left : right).push_back(c.y);
    }
    std::tie(data.xName, data.xUnit) = noms_et_unite(doc, xs);
    std::tie(data.yName, data.yUnit) = noms_et_unite(doc, left);
    std::tie(data.y2Name, data.y2Unit) = noms_et_unite(doc, right);

    for (size_t k = 0; k < plot.curves.size(); ++k) {
        const CurveSpec &c = plot.curves[k];
        const Grandeur *xq = doc.obtient(c.x);
        const Grandeur *yq = doc.obtient(c.y);
        if (!c.visible || !xq || !yq)
            continue;
        Series s;
        s.xs = xq->values;
        s.ys = yq->values;
        if (xq->uncertainty)
            s.ux = doc.valeurs(*xq->uncertainty);
        if (yq->uncertainty)
            s.uy = doc.valeurs(*yq->uncertainty);
        // incertitudes choisies côté par côté pour cette courbe
        const auto side = [&](std::optional<QId> id, std::vector<double> &out) {
            if (id && doc.obtient(*id))
                out = doc.valeurs(*id);
        };
        side(c.errXLow, s.uxLow);
        side(c.errXHigh, s.uxHigh);
        side(c.errYLow, s.uyLow);
        side(c.errYHigh, s.uyHigh);
        for (auto *cotes : {&s.ux, &s.uy, &s.uxLow, &s.uxHigh, &s.uyLow, &s.uyHigh})
            for (double &u : *cotes)
                if (u < 0.0)
                    u = NAN;
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
        if (const auto ve = volume_equivalence(c, doc)) {
            data.overlays.push_back({Overlay::VLine, *ve, 0.0, c.side, c.color, 1.2, Dash::Dashed});
            DataLabel label;
            label.x = *ve;
            label.text = u"Maximum de pente · %1 %2"_s.arg(format_significatif(*ve, 4, style), unite_lisible(xq->unit)).trimmed();
            label.color = c.color;
            label.dx = 4;
            data.labels.push_back(label);
        }
        s.curve = static_cast<int>(k);
        data.series.push_back(std::move(s));
    }

    // courbes des modèles
    for (const auto &f : plot.fits) {
        if (!f.visible || !f.result)
            continue;
        const Grandeur *xq = doc.obtient(f.x);
        if (!xq)
            continue;
        auto model = fit::Model::fabrique(f.kind, xq->name, doc.valeurs_parametres());
        if (!model)
            continue;
        const auto [px, py] = paires_dans_plage(doc, f.x, f.y, f.range);
        const auto r = numeric::plage_finie(px);
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
        s.ys = model->evalue(f.result->params, grid);
        s.xs = std::move(grid);
        s.color = f.color;
        s.marker = Marker::None;
        s.line = LineKind::Straight;
        s.lineWidth = 1.8;
        s.side = f.side;
        s.legend = f.label && !f.label->trimmed().isEmpty() ? *f.label : u"Modèle : "_s + f.kind.etiquette().toLower();
        s.autoscale = false;
        // une fonction : tracée sur tout le graphe affiché (ou de a à b), pas seulement sur les points
        if (f.draw != FitDraw::Points) {
            auto shared = std::make_shared<const fit::Model>(std::move(*model));
            const std::vector<double> params = f.result->params;
            s.function = [shared, params](const std::vector<double> &x) { return shared->evalue(params, x); };
            if (f.draw == FitDraw::Custom)
                s.domain = std::minmax(f.drawRange.first, f.drawRange.second);
        }
        data.series.push_back(std::move(s));
    }

    // méthode des tangentes
    for (const auto &t : plot.tangents) {
        auto res = calcule_tangentes(t, doc);
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
        const QString xu = doc.obtient(t.x) ? unite_lisible(doc.obtient(t.x)->unit) : QString();
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
        const QString yu = doc.obtient(t.y) ? unite_lisible(doc.obtient(t.y)->unit) : QString();
        e.text = u"Intersection · %1 = %2, %3 = %4"_s.arg(doc.nom(t.x), (format_significatif(e.x, 4, style) + u' ' + xu).trimmed(),
                                                           doc.nom(t.y), (format_significatif(e.y, 3, style) + u' ' + yu).trimmed());
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
