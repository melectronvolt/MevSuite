#include "core/pyexport.h"

#include "core/format.h"
#include "core/workspace.h"
#include "plot/resolve.h"

#include <QSet>

#include <cmath>

using namespace Qt::StringLiterals;

namespace pyexport {

namespace {

const QStringList KEYWORDS = {
    u"False"_s, u"None"_s, u"True"_s, u"and"_s, u"as"_s, u"assert"_s, u"async"_s, u"await"_s, u"break"_s, u"class"_s,
    u"continue"_s, u"def"_s, u"del"_s, u"elif"_s, u"else"_s, u"except"_s, u"finally"_s, u"for"_s, u"from"_s, u"global"_s,
    u"if"_s, u"import"_s, u"in"_s, u"is"_s, u"lambda"_s, u"nonlocal"_s, u"not"_s, u"or"_s, u"pass"_s, u"raise"_s,
    u"return"_s, u"try"_s, u"while"_s, u"with"_s, u"yield"_s, u"np"_s, u"plt"_s, u"integ"_s,
};

int prec(const expr::Node *e)
{
    if (e->kind == expr::Node::Bin) {
        switch (e->op) {
        case expr::BinOp::Add:
        case expr::BinOp::Sub: return 1;
        case expr::BinOp::Mul:
        case expr::BinOp::Div: return 2;
        case expr::BinOp::Pow: return 4;
        }
    }
    if (e->kind == expr::Node::Neg)
        return 3;
    return 5;
}

QString formulaToNumpy(const QString &src)
{
    const auto p = expr::parse(src);
    if (!p.ok())
        return u"None  # formule invalide : "_s + src;
    return toNumpy(p.expr->root());
}

QString array(const std::vector<double> &values)
{
    QString out = u"np.array(["_s;
    int lineLen = 0;
    for (size_t k = 0; k < values.size(); ++k) {
        const QString it = pyNum(values[k]);
        if (k > 0) {
            out += u", "_s;
            lineLen += 2;
        }
        if (lineLen > 70) {
            out += u"\n    "_s;
            lineLen = 0;
        }
        out += it;
        lineLen += static_cast<int>(it.size());
    }
    return out + u"])"_s;
}

/// Ordre de calcul des grandeurs calculées (dépendances d'abord).
std::vector<size_t> calcOrder(const Document &doc)
{
    std::vector<size_t> remaining;
    QSet<QString> done;
    for (size_t i = 0; i < doc.quantities.size(); ++i) {
        if (doc.quantities[i].isCalculated())
            remaining.push_back(i);
        else
            done.insert(doc.quantities[i].name);
    }
    std::vector<size_t> order;
    while (!remaining.empty()) {
        const size_t before = remaining.size();
        std::vector<size_t> next;
        for (size_t i : remaining) {
            const auto &q = doc.quantities[i];
            const auto p = expr::parse(*q.formula);
            bool ready = true;
            if (p.ok()) {
                for (const QString &v : p.expr->variables()) {
                    if (doc.byName(v) && !done.contains(v))
                        ready = false;
                }
            }
            if (ready) {
                done.insert(q.name);
                order.push_back(i);
            } else {
                next.push_back(i);
            }
        }
        remaining = next;
        if (remaining.size() == before) {
            // dépendance circulaire : on écrit le reste tel quel
            order.insert(order.end(), remaining.begin(), remaining.end());
            break;
        }
    }
    return order;
}

std::pair<QString, bool> fmtString(const CurveSpec &c)
{
    QString marker;
    switch (c.marker) {
    case Marker::None: break;
    case Marker::Point: marker = u"."_s; break;
    case Marker::Disc:
    case Marker::Circle: marker = u"o"_s; break;
    case Marker::Cross: marker = u"x"_s; break;
    case Marker::Plus: marker = u"+"_s; break;
    case Marker::Square: marker = u"s"_s; break;
    case Marker::Triangle: marker = u"^"_s; break;
    case Marker::Diamond: marker = u"D"_s; break;
    }
    QString line;
    if (c.line != LineKind::None) {
        switch (c.dash) {
        case Dash::Solid: line = u"-"_s; break;
        case Dash::Dashed: line = u"--"_s; break;
        case Dash::Dotted: line = u":"_s; break;
        case Dash::DashDot: line = u"-."_s; break;
        }
    }
    const bool hollow = c.marker == Marker::Circle || c.marker == Marker::Square || c.marker == Marker::Triangle || c.marker == Marker::Diamond;
    const QString fmt = marker.isEmpty() && line.isEmpty() ? u"-"_s : marker + line;
    return {fmt, hollow};
}

QString quote(const QString &s)
{
    QString e = s;
    e.replace(u"\\"_s, u"\\\\"_s);
    e.replace(u"\""_s, u"\\\""_s);
    return u'"' + e + u'"';
}

QString axisLabel(const AxisSpec &spec, const QString &name, const QString &unit)
{
    const QString n = spec.label.value_or(name);
    const QString u = spec.unit.value_or(unit).trimmed();
    return u.isEmpty() ? n : n + u" ("_s + prettyUnit(u) + u')';
}

QString unitComment(const QString &unit)
{
    return unit.isEmpty() ? QString() : u"  # "_s + unit;
}

} // namespace

QString pyName(const QString &name)
{
    return KEYWORDS.contains(name) ? name + u'_' : name;
}

QString pyNum(double v)
{
    if (std::isnan(v))
        return u"np.nan"_s;
    if (std::isinf(v))
        return v > 0 ? u"np.inf"_s : u"-np.inf"_s;
    return pythonFloatRepr(v);
}

QString toNumpy(const expr::Node *e)
{
    using expr::BinOp;
    using expr::Node;
    switch (e->kind) {
    case Node::Num:
        // entiers écrits sans décimale : v**2, 2*np.pi
        if (std::floor(e->num) == e->num && std::abs(e->num) < 1e15)
            return QString::number(static_cast<qint64>(e->num));
        return pyNum(e->num);
    case Node::Var:
        if (e->name == u"pi" || e->name == u"π")
            return u"np.pi"_s;
        return pyName(e->name);
    case Node::Neg: {
        const QString s = toNumpy(e->args[0].get());
        return prec(e->args[0].get()) < 4 ? u"-("_s + s + u')' : u'-' + s;
    }
    case Node::Bin: {
        const Node *a = e->args[0].get();
        const Node *b = e->args[1].get();
        // a + (−b) s'écrit a - b, et a − (−b) s'écrit a + b
        if ((e->op == BinOp::Add || e->op == BinOp::Sub) && b->kind == Node::Neg) {
            Node flipped = *e;
            flipped.op = e->op == BinOp::Add ? BinOp::Sub : BinOp::Add;
            flipped.args = {e->args[0], b->args[0]};
            return toNumpy(&flipped);
        }
        const int p = prec(e);
        auto wrap = [&](const Node *x, bool right) {
            const QString s = toNumpy(x);
            const int px = prec(x);
            const bool need = px < p || (right && px == p && (e->op == BinOp::Sub || e->op == BinOp::Div)) || (e->op == BinOp::Pow && !right && px <= p);
            return need ? u'(' + s + u')' : s;
        };
        QString sym;
        switch (e->op) {
        case BinOp::Add: sym = u" + "_s; break;
        case BinOp::Sub: sym = u" - "_s; break;
        case BinOp::Mul: sym = u"*"_s; break;
        case BinOp::Div: sym = u"/"_s; break;
        case BinOp::Pow: sym = u"**"_s; break;
        }
        return wrap(a, false) + sym + wrap(b, true);
    }
    case Node::Call: {
        QStringList args;
        for (const auto &x : e->args)
            args << toNumpy(x.get());
        const QString j = args.join(u", "_s);
        const QString &f = e->name;
        const size_t n = e->args.size();
        if (f == u"asin") return u"np.arcsin("_s + j + u')';
        if (f == u"acos") return u"np.arccos("_s + j + u')';
        if (f == u"atan") return u"np.arctan("_s + j + u')';
        if (f == u"atan2") return u"np.arctan2("_s + j + u')';
        if (f == u"ln") return u"np.log("_s + j + u')';
        if (f == u"log" || f == u"log10") return u"np.log10("_s + j + u')';
        if (f == u"rad") return u"np.radians("_s + j + u')';
        if (f == u"deg") return u"np.degrees("_s + j + u')';
        if (f == u"pow") return u"np.power("_s + j + u')';
        // sinc de numpy : sin(πx)/(πx)
        if (f == u"sinc") return u"np.sinc(("_s + j + u") / np.pi)"_s;
        if (f == u"min") return (n == 1 ? u"np.nanmin("_s : u"np.minimum("_s) + j + u')';
        if (f == u"max") return (n == 1 ? u"np.nanmax("_s : u"np.maximum("_s) + j + u')';
        if (f == u"sum") return u"np.nansum("_s + j + u')';
        if (f == u"mean") return u"np.nanmean("_s + j + u')';
        if (f == u"std") return u"np.nanstd("_s + j + u')';
        if (f == u"len") return u"len("_s + j + u')';
        if (f == u"deriv") return u"np.gradient("_s + j + u')';
        if (f == u"integ") return u"integ("_s + j + u')';
        if (f == u"index") return u"np.arange(len("_s + j + u"))"_s;
        return u"np."_s + f + u'(' + j + u')';
    }
    }
    return {};
}

QString exportScript(const Workspace &ws)
{
    const Document &doc = ws.doc;
    QString s;
    const QString title = ws.meta.title.isEmpty() ? u"Données exportées de MeVReg"_s : ws.meta.title;
    s += u"# "_s + title + u'\n';
    s += u"# Script généré par MeVReg : numpy et matplotlib uniquement (compatible Capytale).\n"_s;
    s += u"import numpy as np\nimport matplotlib.pyplot as plt\n\n"_s;
    // relancé (Capytale), le script redessinerait par-dessus la figure précédente
    s += u"plt.clf()  # figure vide : relancé, le script ne redessine pas par-dessus\n\n"_s;
    bool usesInteg = false;
    for (const auto &q : doc.quantities) {
        if (q.formula && q.formula->contains(u"integ("_s))
            usesInteg = true;
    }
    if (usesInteg) {
        s += u"def integ(y, x):\n    \"\"\"Primitive par la méthode des trapèzes, nulle au premier point.\"\"\"\n"
             u"    return np.concatenate(([0.0], np.cumsum((y[1:] + y[:-1]) / 2 * np.diff(x))))\n\n"_s;
    }
    if (!doc.parameters.empty()) {
        s += u"# Paramètres\n"_s;
        for (const auto &p : doc.parameters)
            s += pyName(p.name) + u" = "_s + pyNum(p.value) + unitComment(p.unit) + u'\n';
        s += u'\n';
    }
    s += u"# Mesures\n"_s;
    for (const auto &q : doc.quantities) {
        if (!q.isCalculated())
            s += pyName(q.name) + u" = "_s + array(q.values) + unitComment(q.unit) + u'\n';
    }
    const auto order = calcOrder(doc);
    if (!order.empty()) {
        s += u"\n# Grandeurs calculées\n"_s;
        for (size_t i : order) {
            const auto &q = doc.quantities[i];
            s += pyName(q.name) + u" = "_s + formulaToNumpy(*q.formula) + unitComment(q.unit) + u'\n';
        }
    }

    for (const auto &g : ws.graphs) {
        const int n = g.visiblePlots();
        bool any = false;
        for (int k = 0; k < n; ++k)
            any = any || !g.plots[k].curves.empty();
        if (!any)
            continue;
        s += u"\n# "_s + g.name + u'\n';
        s += u"plt.figure("_s + quote(g.name) + u")\nplt.clf()\n"_s;
        for (int k = 0; k < n; ++k) {
            const Plot &p = g.plots[k];
            if (n > 1)
                s += u"plt.subplot(%1, %2, %3)\n"_s.arg(g.rows).arg(g.cols).arg(k + 1);
            s += u"ax = plt.gca()\n"_s;
            const bool right = p.usesRightAxis();
            if (right)
                s += u"ax2 = ax.twinx()\n"_s;
            bool hasLabel = false;
            for (const auto &c : p.curves) {
                const Quantity *xq = doc.get(c.x);
                const Quantity *yq = doc.get(c.y);
                if (!c.visible || !xq || !yq)
                    continue;
                const QString target = c.side == YSide::Right ? u"ax2"_s : u"ax"_s;
                const auto [fmt, hollow] = fmtString(c);
                const QString label = c.label.value_or(yq->name);
                hasLabel = hasLabel || c.inLegend;
                QString extra = u", color="_s + quote(c.color.hex());
                if (c.inLegend)
                    extra += u", label="_s + quote(label);
                if (hollow)
                    extra += u", mfc=\"none\""_s;
                if (c.line == LineKind::Cubic || c.line == LineKind::Monotone)
                    s += u"# courbe lissée dans MeVReg, tracée ici par segments\n"_s;
                const QString xn = pyName(xq->name), yn = pyName(yq->name);
                if (c.errors == ErrorBars::None) {
                    s += u"%1.plot(%2, %3, %4%5)\n"_s.arg(target, xn, yn, quote(fmt), extra);
                } else {
                    QString err;
                    if (yq->uncertainty && doc.get(*yq->uncertainty))
                        err += u", yerr="_s + pyName(doc.name(*yq->uncertainty));
                    if (c.errors != ErrorBars::Vertical && xq->uncertainty && doc.get(*xq->uncertainty))
                        err += u", xerr="_s + pyName(doc.name(*xq->uncertainty));
                    s += u"%1.errorbar(%2, %3%4, fmt=%5, capsize=3%6)\n"_s.arg(target, xn, yn, err, quote(fmt), extra);
                }
            }
            for (const auto &f : p.fits) {
                const Quantity *xq = doc.get(f.x);
                if (!f.visible || !f.result || !xq)
                    continue;
                auto model = fit::Model::make(f.kind, u"xm"_s, doc.paramValues());
                if (!model)
                    continue;
                const QString xn = pyName(xq->name);
                QStringList params;
                for (qsizetype i = 0; i < model->params().size(); ++i) {
                    const double v = f.result->params[i];
                    const double u = f.result->uncertainties[i];
                    if (std::isfinite(u))
                        params << u"%1 = %2 ± %3"_s.arg(model->params()[i], QString::number(v, 'f', 6), QString::number(u, 'e', 2));
                    else
                        params << u"%1 = %2"_s.arg(model->params()[i], QString::number(v, 'f', 6));
                }
                s += u"# Modèle %1 : %2\n"_s.arg(f.kind.label().toLower(), params.join(u" ; "_s));
                QString lo, hi;
                if (f.range) {
                    lo = pyNum(std::min(f.range->first, f.range->second));
                    hi = pyNum(std::max(f.range->first, f.range->second));
                } else {
                    lo = u"np.nanmin("_s + xn + u')';
                    hi = u"np.nanmax("_s + xn + u')';
                }
                s += u"xm = np.linspace(%1, %2, 300)\n"_s.arg(lo, hi);
                const QString target = f.side == YSide::Right && right ? u"ax2"_s : u"ax"_s;
                s += u"%1.plot(xm, %2, \"-\", color=%3, label=\"modèle\")\n"_s.arg(target, formulaToNumpy(model->formula(f.result->params, u"xm"_s)),
                                                                                   quote(f.color.hex()));
                hasLabel = true;
            }
            // axes
            const PlotData data = resolve(p, doc, NumberStyle{false});
            s += u"ax.set_xlabel("_s + quote(axisLabel(p.x, data.xName, data.xUnit)) + u")\n"_s;
            s += u"ax.set_ylabel("_s + quote(axisLabel(p.y, data.yName, data.yUnit)) + u")\n"_s;
            if (right)
                s += u"ax2.set_ylabel("_s + quote(axisLabel(p.y2, data.y2Name, data.y2Unit)) + u")\n"_s;
            if (p.title.visible && !p.title.text.isEmpty())
                s += u"ax.set_title("_s + quote(p.title.text) + u")\n"_s;
            if (p.x.log)
                s += u"ax.set_xscale(\"log\")\n"_s;
            if (p.y.log)
                s += u"ax.set_yscale(\"log\")\n"_s;
            if (!p.x.autoScale)
                s += u"ax.set_xlim(%1, %2)\n"_s.arg(pyNum(p.x.min), pyNum(p.x.max));
            if (!p.y.autoScale)
                s += u"ax.set_ylim(%1, %2)\n"_s.arg(pyNum(p.y.min), pyNum(p.y.max));
            if (p.x.gridMajor || p.y.gridMajor)
                s += u"ax.grid(True)\n"_s;
            if (hasLabel && p.legend.visible)
                s += u"ax.legend()\n"_s;
        }
        s += u"plt.tight_layout()\n"_s;
    }
    // scripts de l'espace de travail, à la suite
    if (!ws.scripts.empty()) {
        s += u"\n# Scripts de MeVReg\n"_s;
        s += u"def grandeur(nom, valeurs, unite=\"\"):\n    \"\"\"Dans MeVReg, crée une grandeur du document ; ici, une simple variable.\"\"\"\n"
             u"    globals()[nom] = np.asarray(valeurs)\n"_s;
        for (const auto &sc : ws.scripts) {
            s += u"\n# --- "_s + sc.name + u" ---\n"_s;
            const QString code = sc.code.trimmed().isEmpty() ? QString() : sc.code;
            if (code.contains(u"import fenetre"_s)) {
                // le module fenetre n'existe pas dans Capytale : script mis en commentaire
                s += u"# Ce script utilise le module fenetre, propre à MeVReg : il est mis en commentaire.\n"_s;
                QString body = code;
                while (body.endsWith(u'\n') || body.endsWith(u' '))
                    body.chop(1);
                for (const QString &l : body.split(u'\n'))
                    s += u"# "_s + l + u'\n';
            } else {
                QString body = code;
                while (body.endsWith(u'\n') || body.endsWith(u' '))
                    body.chop(1);
                s += body + u'\n';
            }
        }
    }
    s += u"\nplt.show()\n"_s;
    return s;
}

} // namespace pyexport
