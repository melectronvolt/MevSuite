#include "core/pyexport.h"

#include "core/format.h"
#include "core/formules.h"
#include "core/pyformule.h"
#include "core/workspace.h"
#include "plot/resolve.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace pyexport {

namespace {

/// Style de trait de matplotlib.
QString style_trait(Dash d)
{
    switch (d) {
    case Dash::Solid: return u"-"_s;
    case Dash::Dashed: return u"--"_s;
    case Dash::Dotted: return u":"_s;
    case Dash::DashDot: return u"-."_s;
    }
    return u"-"_s;
}

QString tableau(const std::vector<double> &values)
{
    QString out = u"np.array(["_s;
    int lineLen = 0;
    for (size_t k = 0; k < values.size(); ++k) {
        const QString it = nombre_python(values[k]);
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

std::pair<QString, bool> texte_formate(const CurveSpec &c)
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

QString guillemets(const QString &s)
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

QString etiquette_axe(const AxisSpec &spec, const QString &name, const QString &unit)
{
    return etiquette_avec_unite(spec.label.value_or(name), spec.unit.value_or(unit));
}

QString commentaire_unite(const QString &unit)
{
    return unit.isEmpty() ? QString() : u"  # "_s + unit.simplified();
}

QString grandeurs_calculees(const Document &doc, FormulesPython &python)
{
    const auto plan = ordonne_formules(doc);
    if (plan.calculables.empty() && plan.bloquees.empty())
        return {};
    QString texte = u"\n# Grandeurs calculées\n"_s;
    const bool avecIncertitude = std::any_of(plan.calculables.begin(), plan.calculables.end(),
        [&](size_t i) { return !plan.mesuresParIncertitude[i].empty(); });
    if (avecIncertitude)
        texte += u"def _mevreg_diffuse(valeur, *mesures):\n"
                  "    \"\"\"Une incertitude constante s'applique à chaque ligne mesurée.\"\"\"\n"
                  "    lignes = max(np.size(mesure) for mesure in mesures)\n"
                  "    if np.size(valeur) == 1 and lignes != 1:\n"
                  "        return np.full(lignes, np.ravel(valeur)[0])\n"
                  "    return valeur\n\n"_s;

    for (size_t i : plan.bloquees) {
        const auto &q = doc.quantities[i];
        texte += python.nom(q.name) + u" = np.array([])  # "_s + q.error.simplified() + u'\n';
    }
    // Une mesure peut dépendre de sa propre colonne d'incertitude. Comme dans
    // le tableur, on reprend le calcul si une telle mesure change de longueur.
    QStringList tailles;
    for (size_t i : plan.mesuresDifferees) {
        const QString nom = python.nom(doc.quantities[i].name);
        texte += nom + u" = np.array([np.nan])\n"_s;
        tailles << u"np.size(%1)"_s.arg(nom);
    }
    const QString listeTailles = u"("_s + tailles.join(u", "_s) + u",)"_s;
    const QString retrait = tailles.isEmpty() ? QString() : u"    "_s;
    if (!tailles.isEmpty())
        texte += u"_mevreg_tours = []\nwhile True:\n    _mevreg_tailles = %1\n"
                  "    if _mevreg_tailles in _mevreg_tours:\n"
                  "        raise ValueError(\"Vérifiez les longueurs des colonnes d'incertitude.\")\n"
                  "    _mevreg_tours.append(_mevreg_tailles)\n"_s.arg(listeTailles);
    for (size_t i : plan.calculables) {
        const auto &q = doc.quantities[i];
        const auto &analyse = plan.analyses[i];
        texte += retrait + python.nom(q.name) + u" = "_s;
        if (!analyse.ok() || (!q.error.isEmpty() && q.values.empty())) {
            const QString erreur = analyse.ok() ? q.error : analyse.error.message;
            texte += u"np.array([])  # "_s + erreur.simplified() + u'\n';
            continue;
        }
        QString expression = python.formule(*analyse.expr);
        QStringList mesures;
        for (size_t mesure : plan.mesuresParIncertitude[i])
            mesures << python.nom(doc.quantities[mesure].name);
        if (!mesures.isEmpty())
            expression = u"_mevreg_diffuse(%1, %2)"_s.arg(expression, mesures.join(u", "_s));
        texte += expression + commentaire_unite(q.unit) + u'\n';
    }
    if (!tailles.isEmpty())
        texte += u"    if _mevreg_tailles == %1:\n        break\n"_s.arg(listeTailles);
    return texte;
}

} // namespace

QString nombre_python(double v)
{
    if (std::isnan(v))
        return u"np.nan"_s;
    if (std::isinf(v))
        return v > 0 ? u"np.inf"_s : u"-np.inf"_s;
    return repr_python_reel(v);
}

QString exporte_script(const Seance &ws)
{
    const Document &doc = ws.doc;
    QString s;
    const QString title = ws.meta.title.isEmpty() ? u"Données exportées de MeVReg"_s : ws.meta.title;
    s += u"# "_s + title.simplified() + u'\n';
    s += u"# Script généré par MeVReg : numpy et matplotlib uniquement (compatible Capytale).\n"_s;
    s += u"import numpy as np\nimport matplotlib.pyplot as plt\n\n"_s;
    // relancé (Capytale), le script redessinerait par-dessus la figure précédente
    s += u"plt.clf()  # figure vide : relancé, le script ne redessine pas par-dessus\n\n"_s;
    FormulesPython python(&doc);
    const qsizetype debutDonnees = s.size();
    if (!doc.parameters.empty()) {
        s += u"# Paramètres\n"_s;
        for (const auto &p : doc.parameters)
            s += python.nom(p.name) + u" = "_s + nombre_python(p.value) + commentaire_unite(p.unit) + u'\n';
        s += u'\n';
    }
    s += u"# Mesures\n"_s;
    for (const auto &q : doc.quantities) {
        if (!q.est_calculee())
            s += python.nom(q.name) + u" = "_s + tableau(q.values) + commentaire_unite(q.unit) + u'\n';
    }
    s += grandeurs_calculees(doc, python);

    bool avecIncertitudes = false;
    for (const auto &g : ws.graphs)
        for (const auto &p : g.plots)
            for (const auto &c : p.curves)
                avecIncertitudes |= c.visible && c.errors != ErrorBars::None;
    if (avecIncertitudes)
        s += u"\ndef _mevreg_incertitude(u):\n"
             u"    return np.where(np.asarray(u) >= 0, u, np.nan)\n"_s;

    for (const auto &g : ws.graphs) {
        const int n = g.traces_visibles();
        bool any = false;
        for (int k = 0; k < n; ++k)
            any = any || !g.plots[k].curves.empty();
        if (!any)
            continue;
        s += u"\n# "_s + g.name.simplified() + u'\n';
        s += u"plt.figure("_s + guillemets(g.name) + u")\nplt.clf()\n"_s;
        for (int k = 0; k < n; ++k) {
            const Plot &p = g.plots[k];
            if (n > 1)
                s += u"plt.subplot(%1, %2, %3)\n"_s.arg(g.rows).arg(g.cols).arg(k + 1);
            s += u"ax = plt.gca()\n"_s;
            const bool right = p.utilise_axe_droit();
            if (right)
                s += u"ax2 = ax.twinx()\n"_s;
            bool hasLabel = false;
            for (const auto &c : p.curves) {
                const Grandeur *xq = doc.obtient(c.x);
                const Grandeur *yq = doc.obtient(c.y);
                if (!c.visible || !xq || !yq)
                    continue;
                const QString target = c.side == YSide::Right ? u"ax2"_s : u"ax"_s;
                if (const auto ve = volume_equivalence(c, doc))
                    s += u"%1.axvline(%2, linestyle=\"--\", label=\"Maximum de pente\")\n"_s.arg(target, nombre_python(*ve));
                const auto [fmt, hollow] = texte_formate(c);
                const QString label = c.label.value_or(yq->name);
                hasLabel = hasLabel || c.inLegend;
                QString extra = u", color="_s + guillemets(c.color.hex());
                if (c.inLegend)
                    extra += u", label="_s + guillemets(label);
                if (hollow)
                    extra += u", mfc=\"none\""_s;
                if (c.line == LineKind::Cubic || c.line == LineKind::Monotone)
                    s += u"# courbe lissée dans MeVReg, tracée ici par segments\n"_s;
                const size_t nPoints = std::min(xq->values.size(), yq->values.size());
                const auto coord = [&](const Grandeur &q) {
                    const QString nom = python.nom(q.name);
                    if (q.est_calculee())
                        return u"np.broadcast_to(%1, (%2,))[:%3]"_s.arg(nom).arg(q.values.size()).arg(nPoints);
                    if (q.values.size() == nPoints)
                        return nom;
                    return u"np.atleast_1d(%1)[:%2]"_s.arg(nom).arg(nPoints);
                };
                const QString xn = coord(*xq), yn = coord(*yq);
                if (c.errors == ErrorBars::None) {
                    s += u"%1.plot(%2, %3, %4%5)\n"_s.arg(target, xn, yn, guillemets(fmt), extra);
                } else {
                    // incertitude d'un axe : la même des deux côtés, ou choisie côté par côté
                    // (barres dissymétriques : [vers le bas, vers le haut])
                    const auto errorOf = [&](const Grandeur *q, std::optional<QId> low, std::optional<QId> high) -> QString {
                        const auto ajuste = [&](QId id) {
                            const auto taille = doc.valeurs(id).size();
                            const QString nom = python.nom(doc.nom(id));
                            if (taille == 1 || taille == nPoints)
                                return nom;
                            if (taille > nPoints)
                                return u"np.atleast_1d(%1)[:%2]"_s.arg(nom).arg(nPoints);
                            return u"np.pad(np.atleast_1d(%1), (0, %2), constant_values=np.nan)"_s.arg(nom).arg(nPoints - taille);
                        };
                        const auto nameOf = [&](std::optional<QId> side) -> QString {
                            if (side && doc.obtient(*side))
                                return ajuste(*side);
                            if (q->uncertainty && doc.obtient(*q->uncertainty))
                                return ajuste(*q->uncertainty);
                            return QString();
                        };
                        // Une valeur négative est invalide, comme dans le graphe du document.
                        const bool sided = (low && doc.obtient(*low)) || (high && doc.obtient(*high));
                        if (!sided)
                            return q->uncertainty && doc.obtient(*q->uncertainty) ? u"_mevreg_incertitude("_s + ajuste(*q->uncertainty) + u")"_s
                                                                             : QString();
                        // Chaque côté doit avoir autant de points que la courbe, même pour une valeur isolée.
                        const QString shape = u"np.shape("_s + coord(*q) + u")"_s;
                        const auto side = [&](const QString &n) {
                            return n.isEmpty() ? u"np.zeros("_s + shape + u")"_s : u"_mevreg_incertitude(np.broadcast_to("_s + n + u", "_s + shape + u"))"_s;
                        };
                        return u"["_s + side(nameOf(low)) + u", "_s + side(nameOf(high)) + u"]"_s;
                    };
                    QString err;
                    if (const QString e = errorOf(yq, c.errYLow, c.errYHigh); !e.isEmpty())
                        err += u", yerr="_s + e;
                    if (c.errors != ErrorBars::Vertical)
                        if (const QString e = errorOf(xq, c.errXLow, c.errXHigh); !e.isEmpty())
                            err += u", xerr="_s + e;
                    s += u"%1.errorbar(%2, %3%4, fmt=%5, capsize=3%6)\n"_s.arg(target, xn, yn, err, guillemets(fmt), extra);
                }
            }
            for (const auto &f : p.fits) {
                const Grandeur *xq = doc.obtient(f.x);
                if (!f.visible || !f.result || !xq)
                    continue;
                auto model = fit::Model::fabrique(f.kind, xq->name, doc.valeurs_parametres());
                if (!model || size_t(model->parametres().size()) != f.result->params.size())
                    continue;
                const QString xn = python.nom(xq->name);
                QStringList params;
                for (qsizetype i = 0; i < model->parametres().size(); ++i) {
                    const auto *yq = doc.obtient(f.y);
                    const QString unite = yq ? f.kind.unite_parametre(i, xq->unit, yq->unit) : QString();
                    params << u"%1 = %2"_s.arg(model->parametres()[i], f.result->texte_parametre(i, unite, NumberStyle{false}));
                }
                s += u"# Modèle %1 : %2\n"_s.arg(f.kind.etiquette().toLower(), params.join(u" ; "_s));
                // étendue de la courbe : tout le graphe, les points utilisés, ou de a à b
                QString lo, hi;
                bool keepLimits = false;
                if (f.draw == FitDraw::Custom) {
                    lo = nombre_python(std::min(f.drawRange.first, f.drawRange.second));
                    hi = nombre_python(std::max(f.drawRange.first, f.drawRange.second));
                } else if (f.draw == FitDraw::Graph) {
                    if (!p.x.autoScale) {
                        lo = nombre_python(p.x.min);
                        hi = nombre_python(p.x.max);
                    } else {
                        s += u"xl = ax.get_xlim()\n"_s;
                        lo = u"xl[0]"_s;
                        hi = u"xl[1]"_s;
                        keepLimits = true;
                    }
                } else if (f.range) {
                    lo = nombre_python(std::min(f.range->first, f.range->second));
                    hi = nombre_python(std::max(f.range->first, f.range->second));
                } else {
                    lo = u"np.nanmin("_s + xn + u')';
                    hi = u"np.nanmax("_s + xn + u')';
                }
                const QString xm = doc.nom_unique(u"xm"_s);
                const auto formule = expr::analyse(model->formule(f.result->params, xm));
                if (!formule.ok())
                    continue;
                s += u"%1 = np.linspace(%2, %3, 300)\n"_s.arg(xm, lo, hi);
                const QString target = f.side == YSide::Right && right ? u"ax2"_s : u"ax"_s;
                const QString label = f.label.value_or(u"Modèle : "_s + f.kind.etiquette().toLower());
                QString ordonnees = python.formule(*formule.expr);
                if (f.kind.type == fit::ModelKind::User)
                    ordonnees = u"np.broadcast_to(%1, %2.shape)"_s.arg(ordonnees, xm);
                s += u"%1.plot(%2, %3, \"-\", color=%4, label=%5)\n"_s.arg(target, xm, ordonnees, guillemets(f.color.hex()), guillemets(label));
                if (keepLimits)
                    s += u"ax.set_xlim(xl)\n"_s;
                hasLabel = true;
            }
            // méthode des tangentes : les deux tangentes, la médiane et le point d'équivalence
            for (const auto &tg : p.tangents) {
                const auto r = calcule_tangentes(tg, doc);
                if (!r)
                    continue;
                const titration::Tangents &t = r->second;
                const QString target = tg.side == YSide::Right && right ? u"ax2"_s : u"ax"_s;
                const QString col = guillemets(tg.color.hex());
                s += u"# Intersection de la médiane avec la courbe en (%1 ; %2)\n"_s.arg(nombre_python(t.equivalence.first), nombre_python(t.equivalence.second));
                s += u"%1.axline((%2, %3), slope=%4, color=%5, linestyle=\"--\")\n"_s.arg(target, nombre_python(t.p1.first), nombre_python(t.droite1(t.p1.first)), nombre_python(t.slope), col);
                s += u"%1.axline((%2, %3), slope=%4, color=%5, linestyle=\"--\")\n"_s.arg(target, nombre_python(t.p2.first), nombre_python(t.droite2(t.p2.first)), nombre_python(t.slope), col);
                s += u"%1.axline((0, %2), slope=%3, color=%4, linestyle=\":\")\n"_s.arg(target, nombre_python(t.bMedian), nombre_python(t.slope), col);
                s += u"%1.plot(%2, %3, \"o\", color=%4)\n"_s.arg(target, nombre_python(t.equivalence.first), nombre_python(t.equivalence.second), col);
            }
            // droites et textes
            for (const auto &a : p.annotations) {
                const QString target = a.side == YSide::Right && right ? u"ax2"_s : u"ax"_s;
                const QString col = guillemets(a.color.hex());
                const QString ls = guillemets(style_trait(a.dash));
                switch (a.kind) {
                case Annotation::VLine:
                    s += u"%1.axvline(%2, color=%3, linestyle=%4)\n"_s.arg(target, nombre_python(a.x), col, ls);
                    if (!a.label.isEmpty())
                        s += u"%1.text(%2, 1, %3, color=%4, transform=%1.get_xaxis_transform(), ha=\"left\", va=\"top\")\n"_s.arg(
                            target, nombre_python(a.x), guillemets(u" "_s + a.label), col);
                    break;
                case Annotation::HLine:
                    s += u"%1.axhline(%2, color=%3, linestyle=%4)\n"_s.arg(target, nombre_python(a.y), col, ls);
                    if (!a.label.isEmpty())
                        s += u"%1.text(1, %2, %3, color=%4, transform=%1.get_yaxis_transform(), ha=\"right\", va=\"bottom\")\n"_s.arg(
                            target, nombre_python(a.y), guillemets(a.label), col);
                    break;
                case Annotation::Line:
                    s += u"%1.axline((%2, %3), (%4, %5), color=%6, linestyle=%7)\n"_s.arg(target, nombre_python(a.x), nombre_python(a.y), nombre_python(a.x2), nombre_python(a.y2), col, ls);
                    if (!a.label.isEmpty())
                        s += u"%1.text(%2, %3, %4, color=%5)\n"_s.arg(target, nombre_python(a.x2), nombre_python(a.y2), guillemets(a.label), col);
                    break;
                case Annotation::Text:
                    // taille en pixels dans MeVReg, en points pour matplotlib
                    s += u"%1.text(%2, %3, %4, color=%5, fontsize=%6)\n"_s.arg(target, nombre_python(a.x), nombre_python(a.y), guillemets(a.text), col,
                                                                               QString::number(a.size * 0.75, 'g', 3));
                    break;
                }
            }
            // axes
            const PlotData data = resout(p, doc, NumberStyle{false});
            s += u"ax.set_xlabel("_s + guillemets(etiquette_axe(p.x, data.xName, data.xUnit)) + u")\n"_s;
            s += u"ax.set_ylabel("_s + guillemets(etiquette_axe(p.y, data.yName, data.yUnit)) + u")\n"_s;
            if (right)
                s += u"ax2.set_ylabel("_s + guillemets(etiquette_axe(p.y2, data.y2Name, data.y2Unit)) + u")\n"_s;
            if (p.title.visible && !p.title.text.isEmpty())
                s += u"ax.set_title("_s + guillemets(p.title.text) + u")\n"_s;
            auto graduations = [&](const AxisSpec &axis, const QString &method) {
                if (!axis.ticks) return;
                QStringList values, labels;
                for (const auto &[v, label] : *axis.ticks) {
                    values << nombre_python(v);
                    labels << guillemets(label);
                }
                s += method + u"(["_s + values.join(u", "_s) + u"], ["_s + labels.join(u", "_s) + u"])\n"_s;
            };
            graduations(p.x, u"ax.set_xticks"_s);
            graduations(p.y, u"ax.set_yticks"_s);
            if (right) graduations(p.y2, u"ax2.set_yticks"_s);
            if (p.x.log)
                s += u"ax.set_xscale(\"log\")\n"_s;
            if (p.y.log)
                s += u"ax.set_yscale(\"log\")\n"_s;
            if (!p.x.autoScale)
                s += u"ax.set_xlim(%1, %2)\n"_s.arg(nombre_python(p.x.min), nombre_python(p.x.max));
            if (!p.y.autoScale)
                s += u"ax.set_ylim(%1, %2)\n"_s.arg(nombre_python(p.y.min), nombre_python(p.y.max));
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
            s += u"\n# --- "_s + sc.name.simplified() + u" ---\n"_s;
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
    s.insert(debutDonnees, python.fonctions_auxiliaires());
    return s;
}

} // namespace pyexport
