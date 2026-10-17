#include "core/pyexport.h"

#include "core/format.h"
#include "core/workspace.h"
#include "plot/resolve.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace pyexport {

namespace {

const QStringList KEYWORDS = {
    u"False"_s, u"None"_s, u"True"_s, u"and"_s, u"as"_s, u"assert"_s, u"async"_s, u"await"_s, u"break"_s, u"class"_s,
    u"continue"_s, u"def"_s, u"del"_s, u"elif"_s, u"else"_s, u"except"_s, u"finally"_s, u"for"_s, u"from"_s, u"global"_s,
    u"if"_s, u"import"_s, u"in"_s, u"is"_s, u"lambda"_s, u"nonlocal"_s, u"not"_s, u"or"_s, u"pass"_s, u"raise"_s,
    u"return"_s, u"try"_s, u"while"_s, u"with"_s, u"yield"_s, u"np"_s, u"plt"_s, u"integ"_s, u"deriv"_s,
};

/// Document en cours d'export : noms Python uniques (`pyName`), et dérivées qui sautent les cases
/// vides, comme dans MeVReg, quand le document en a (`toNumpy`).
struct Context {
    const Document *doc = nullptr;
    bool skipMissing = false;
};

Context &context()
{
    static Context c;
    return c;
}

/// Contexte posé le temps d'un export.
struct Scope {
    Scope(const Document *doc, bool skipMissing) { context() = {doc, skipMissing}; }
    ~Scope() { context() = {}; }
};

/// Style de trait de matplotlib.
QString lineStyle(Dash d)
{
    switch (d) {
    case Dash::Solid: return u"-"_s;
    case Dash::Dashed: return u"--"_s;
    case Dash::Dotted: return u":"_s;
    case Dash::DashDot: return u"-."_s;
    }
    return u"-"_s;
}

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
    // chaîne Python entre guillemets : antislash, guillemet et caractères de contrôle (un texte de
    // graphe sur deux lignes) échappés
    QString e;
    e.reserve(s.size() + 2);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u'\\': e += u"\\\\"_s; break;
        case u'"': e += u"\\\""_s; break;
        case u'\n': e += u"\\n"_s; break;
        case u'\r': e += u"\\r"_s; break;
        case u'\t': e += u"\\t"_s; break;
        default:
            if (c.unicode() < 0x20 || c.unicode() == 0x7f)
                e += u"\\x%1"_s.arg(int(c.unicode()), 2, 16, QChar(u'0'));
            else
                e += c;
        }
    }
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
    if (!KEYWORDS.contains(name))
        return name;
    // mot réservé : suffixé de « _ », autant de fois qu'il faut pour ne pas prendre le nom d'une
    // autre grandeur ou d'un paramètre du document (« class » et « class_ »)
    QString n = name + u'_';
    while (const Document *doc = context().doc) {
        if (!doc->byName(n) && !doc->paramByName(n))
            break;
        n += u'_';
    }
    return n;
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
        if (f == u"deriv") return (context().skipMissing ? u"deriv("_s : u"np.gradient("_s) + j + u')';
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
    // cases vides dans le document : dérivées et primitives les sautent, comme dans MeVReg
    const bool missing = std::any_of(doc.quantities.begin(), doc.quantities.end(), [](const Quantity &q) {
        return std::any_of(q.values.begin(), q.values.end(), [](double v) { return !std::isfinite(v); });
    });
    const Scope scope(&doc, missing);
    static const QRegularExpression integCall(u"\\binteg\\s*\\("_s), derivCall(u"\\bderiv\\s*\\("_s);
    bool usesInteg = false, usesDeriv = false;
    for (const auto &q : doc.quantities) {
        if (q.formula) {
            usesInteg = usesInteg || q.formula->contains(integCall);
            usesDeriv = usesDeriv || q.formula->contains(derivCall);
        }
    }
    if (missing && (usesInteg || usesDeriv)) {
        s += u"def _valides(y, x):\n"
             u"    \"\"\"Mêmes longueurs, et les couples sans case vide.\"\"\"\n"
             u"    y, x = np.asarray(y, dtype=float), np.asarray(x, dtype=float)\n"
             u"    n = max(len(y), len(x))\n"
             u"    y = np.concatenate((y, np.full(n - len(y), np.nan)))\n"
             u"    x = np.concatenate((x, np.full(n - len(x), np.nan)))\n"
             u"    return y, x, np.isfinite(y) & np.isfinite(x)\n\n"_s;
        if (usesDeriv)
            s += u"def deriv(y, x):\n"
                 u"    \"\"\"Dérivée dy/dx ; les cases vides sont sautées, comme dans MeVReg.\"\"\"\n"
                 u"    y, x, ok = _valides(y, x)\n"
                 u"    out = np.full(len(y), np.nan)\n"
                 u"    if ok.sum() >= 2:\n"
                 u"        out[ok] = np.gradient(y[ok], x[ok])\n"
                 u"    return out\n\n"_s;
        if (usesInteg)
            s += u"def integ(y, x):\n"
                 u"    \"\"\"Primitive par la méthode des trapèzes, nulle au premier point ; les cases vides sont\n"
                 u"    sautées, comme dans MeVReg.\"\"\"\n"
                 u"    y, x, ok = _valides(y, x)\n"
                 u"    out = np.full(len(y), np.nan)\n"
                 u"    if ok.any():\n"
                 u"        yv, xv = y[ok], x[ok]\n"
                 u"        out[ok] = np.concatenate(([0.0], np.cumsum((yv[1:] + yv[:-1]) / 2 * np.diff(xv))))\n"
                 u"    return out\n\n"_s;
    } else if (usesInteg) {
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
                    // incertitude d'un axe : la même des deux côtés, ou choisie côté par côté
                    // (barres dissymétriques : [vers le bas, vers le haut])
                    const auto errorOf = [&](const Quantity *q, std::optional<QId> low, std::optional<QId> high) -> QString {
                        const auto nameOf = [&](std::optional<QId> side) -> QString {
                            if (side && doc.get(*side))
                                return pyName(doc.name(*side));
                            if (q->uncertainty && doc.get(*q->uncertainty))
                                return pyName(doc.name(*q->uncertainty));
                            return QString();
                        };
                        const bool sided = (low && doc.get(*low)) || (high && doc.get(*high));
                        if (!sided)
                            return q->uncertainty && doc.get(*q->uncertainty) ? pyName(doc.name(*q->uncertainty)) : QString();
                        const QString zero = u"np.zeros(len("_s + pyName(q->name) + u"))"_s;
                        const QString lo = nameOf(low), hi = nameOf(high);
                        return u"["_s + (lo.isEmpty() ? zero : lo) + u", "_s + (hi.isEmpty() ? zero : hi) + u"]"_s;
                    };
                    QString err;
                    if (const QString e = errorOf(yq, c.errYLow, c.errYHigh); !e.isEmpty())
                        err += u", yerr="_s + e;
                    if (c.errors != ErrorBars::Vertical)
                        if (const QString e = errorOf(xq, c.errXLow, c.errXHigh); !e.isEmpty())
                            err += u", xerr="_s + e;
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
                // étendue de la courbe : tout le graphe, les points utilisés, ou de a à b
                QString lo, hi;
                bool keepLimits = false;
                if (f.draw == FitDraw::Custom) {
                    lo = pyNum(std::min(f.drawRange.first, f.drawRange.second));
                    hi = pyNum(std::max(f.drawRange.first, f.drawRange.second));
                } else if (f.draw == FitDraw::Graph) {
                    if (!p.x.autoScale) {
                        lo = pyNum(p.x.min);
                        hi = pyNum(p.x.max);
                    } else {
                        s += u"xl = ax.get_xlim()\n"_s;
                        lo = u"xl[0]"_s;
                        hi = u"xl[1]"_s;
                        keepLimits = true;
                    }
                } else if (f.range) {
                    lo = pyNum(std::min(f.range->first, f.range->second));
                    hi = pyNum(std::max(f.range->first, f.range->second));
                } else {
                    lo = u"np.nanmin("_s + xn + u')';
                    hi = u"np.nanmax("_s + xn + u')';
                }
                s += u"xm = np.linspace(%1, %2, 300)\n"_s.arg(lo, hi);
                const QString target = f.side == YSide::Right && right ? u"ax2"_s : u"ax"_s;
                const QString label = f.label.value_or(u"Modèle : "_s + f.kind.label().toLower());
                s += u"%1.plot(xm, %2, \"-\", color=%3, label=%4)\n"_s.arg(target, formulaToNumpy(model->formula(f.result->params, u"xm"_s)),
                                                                         quote(f.color.hex()), quote(label));
                if (keepLimits)
                    s += u"ax.set_xlim(xl)\n"_s;
                hasLabel = true;
            }
            // méthode des tangentes : les deux tangentes, la médiane et le point d'équivalence
            for (const auto &tg : p.tangents) {
                const auto r = computeTangents(tg, doc);
                if (!r)
                    continue;
                const titration::Tangents &t = r->second;
                const QString target = tg.side == YSide::Right && right ? u"ax2"_s : u"ax"_s;
                const QString col = quote(tg.color.hex());
                s += u"# Méthode des tangentes : équivalence en (%1 ; %2)\n"_s.arg(pyNum(t.equivalence.first), pyNum(t.equivalence.second));
                s += u"%1.axline((%2, %3), slope=%4, color=%5, linestyle=\"--\")\n"_s.arg(target, pyNum(t.p1.first), pyNum(t.line1(t.p1.first)), pyNum(t.slope), col);
                s += u"%1.axline((%2, %3), slope=%4, color=%5, linestyle=\"--\")\n"_s.arg(target, pyNum(t.p2.first), pyNum(t.line2(t.p2.first)), pyNum(t.slope), col);
                s += u"%1.axline((0, %2), slope=%3, color=%4, linestyle=\":\")\n"_s.arg(target, pyNum(t.bMedian), pyNum(t.slope), col);
                s += u"%1.plot(%2, %3, \"o\", color=%4)\n"_s.arg(target, pyNum(t.equivalence.first), pyNum(t.equivalence.second), col);
            }
            // droites et textes
            for (const auto &a : p.annotations) {
                const QString target = a.side == YSide::Right && right ? u"ax2"_s : u"ax"_s;
                const QString col = quote(a.color.hex());
                const QString ls = quote(lineStyle(a.dash));
                switch (a.kind) {
                case Annotation::VLine:
                    s += u"%1.axvline(%2, color=%3, linestyle=%4)\n"_s.arg(target, pyNum(a.x), col, ls);
                    if (!a.label.isEmpty())
                        s += u"%1.text(%2, 1, %3, color=%4, transform=%1.get_xaxis_transform(), ha=\"left\", va=\"top\")\n"_s.arg(
                            target, pyNum(a.x), quote(u" "_s + a.label), col);
                    break;
                case Annotation::HLine:
                    s += u"%1.axhline(%2, color=%3, linestyle=%4)\n"_s.arg(target, pyNum(a.y), col, ls);
                    if (!a.label.isEmpty())
                        s += u"%1.text(1, %2, %3, color=%4, transform=%1.get_yaxis_transform(), ha=\"right\", va=\"bottom\")\n"_s.arg(
                            target, pyNum(a.y), quote(a.label), col);
                    break;
                case Annotation::Line:
                    s += u"%1.axline((%2, %3), (%4, %5), color=%6, linestyle=%7)\n"_s.arg(target, pyNum(a.x), pyNum(a.y), pyNum(a.x2), pyNum(a.y2), col, ls);
                    if (!a.label.isEmpty())
                        s += u"%1.text(%2, %3, %4, color=%5)\n"_s.arg(target, pyNum(a.x2), pyNum(a.y2), quote(a.label), col);
                    break;
                case Annotation::Text:
                    // taille en pixels dans MeVReg, en points pour matplotlib
                    s += u"%1.text(%2, %3, %4, color=%5, fontsize=%6)\n"_s.arg(target, pyNum(a.x), pyNum(a.y), quote(a.text), col,
                                                                               QString::number(a.size * 0.75, 'g', 3));
                    break;
                }
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
            static const QRegularExpression ownModule(u"\\b(import|from)\\s+(mevreg|fenetre)\\b"_s);
            if (const auto m = ownModule.match(code); m.hasMatch()) {
                // les modules mevreg et fenetre n'existent pas dans Capytale : script mis en commentaire
                s += u"# Ce script utilise le module %1, propre à MeVReg : il est mis en commentaire.\n"_s.arg(m.captured(2));
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
