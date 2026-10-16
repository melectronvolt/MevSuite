#include "app/examples.h"

#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

namespace examples {

namespace {

/// Bruit pseudo-aléatoire reproductible (approximativement gaussien).
struct Noise {
    std::uint64_t s;
    double next()
    {
        double sum = 0.0;
        for (int k = 0; k < 6; ++k) {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            sum += static_cast<double>(s >> 11) / static_cast<double>(std::uint64_t(1) << 53);
        }
        return (sum - 3.0) * M_SQRT2;
    }
};

QId add(Workspace &ws, const QString &name, const QString &unit, std::vector<double> values)
{
    return *ws.doc.addExperimental(name, unit, std::move(values));
}

QId calc(Workspace &ws, const QString &name, const QString &unit, const QString &formula)
{
    return *ws.doc.addCalculated(name, unit, formula);
}

GraphView &graph(Workspace &ws, const QString &name)
{
    const ViewId id = ws.addGraph();
    GraphView &g = *ws.graphMut(id);
    g.name = name;
    return g;
}

template <typename F>
std::vector<double> generate(int n, F f)
{
    std::vector<double> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = f(i);
    return v;
}

std::vector<double> map(const std::vector<double> &x, const std::function<double(double)> &f)
{
    std::vector<double> v;
    v.reserve(x.size());
    for (double t : x)
        v.push_back(f(t));
    return v;
}

const char *DIFFRACTION = R"py(import numpy as np
import matplotlib.pyplot as plt
import fenetre as fe

# a, l (longueur d'onde) et D sont des paramètres réglables par curseur :
# ce script est « lié », il est relancé à chaque changement.
x = np.linspace(-0.05, 0.05, 2001)
u = np.pi * a * x / (l * D)
I = np.ones_like(x)
nz = u != 0
I[nz] = (np.sin(u[nz]) / u[nz])**2

d = fe.Dessin("Figure de diffraction", -0.05, 0.05, -0.015, 0.015, unite="m", fond="noir")
d.bandes(I, palette="laser", longueur_onde=l)
L = 2 * l * D / a
d.fleche(-L/2, -0.011, L/2, -0.011, couleur="blanc")
d.fleche(L/2, -0.011, -L/2, -0.011, couleur="blanc")
d.texte(-0.006, -0.0135, "L = 2λD/a", couleur="blanc", taille=13)
d.montrer()

plt.figure("Intensité sur l'écran")
plt.plot(x, I, color=fe.couleur_longueur_onde(l))
plt.xlabel("x (m)")
plt.ylabel("I / I0")
plt.show()
print(f"L = {L*1000:.1f} mm")
)py";

} // namespace

const QStringList &names()
{
    static const QStringList n = {
        u"Chute libre (z, t)"_s,
        u"Décharge d'un condensateur"_s,
        u"Dosage pH-métrique"_s,
        u"Oscillations amorties"_s,
        u"Mesures répétées (statistiques)"_s,
        u"Loi d'Ohm avec incertitudes"_s,
        u"Son : signal et spectre"_s,
        u"Diffraction par une fente (Python, dessin)"_s,
    };
    return n;
}

Workspace build(int index)
{
    Workspace ws;
    Noise noise{0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(index)};
    switch (index) {
    case 0: {
        ws.meta.title = u"Chute libre d'une bille"_s;
        const auto t = generate(16, [](int i) { return i * 0.04; });
        const auto z = map(t, [&](double x) { return 1.5 - 0.5 * 9.81 * x * x + 0.004 * noise.next(); });
        const QId tq = add(ws, u"t"_s, u"s"_s, t);
        const QId zq = add(ws, u"z"_s, u"m"_s, z);
        const QId uz = *ws.doc.createUncertainty(zq, u"0.01"_s);
        const QId vz = calc(ws, u"v_z"_s, u"m/s"_s, u"deriv(z, t)"_s);
        ws.addTable({tq, zq, uz, vz});
        GraphView &g = graph(ws, u"Chute"_s);
        g.setGrid(2, 1);
        g.linkX = true;
        Plot &p = g.plots[0];
        p.title.text = u"Position de la bille"_s;
        p.addCurve(tq, zq).errors = ErrorBars::Vertical;
        p.fits.push_back(FitSpec::make(fit::ModelKind{fit::ModelKind::Parabola}, tq, zq, PALETTE[3]));
        Plot &q = g.plots[1];
        q.title.text = u"Vitesse verticale"_s;
        CurveSpec &c = q.addCurve(tq, vz);
        c.marker = Marker::Disc;
        c.markerSize = 6.0;
        q.fits.push_back(FitSpec::make(fit::ModelKind{fit::ModelKind::Affine}, tq, vz, PALETTE[3]));
        break;
    }
    case 1: {
        ws.meta.title = u"Décharge d'un condensateur dans une résistance"_s;
        const auto t = generate(200, [](int i) { return i * 25e-6; });
        const auto u = map(t, [&](double x) { return 5.0 * std::exp(-x / 1.0e-3) + 0.02 * noise.next(); });
        const QId tq = add(ws, u"t"_s, u"s"_s, t);
        const QId uq = add(ws, u"u_C"_s, u"V"_s, u);
        ws.doc.addParameter(u"C"_s, u"F"_s, 1e-6);
        const QId i = calc(ws, u"i"_s, u"A"_s, u"C*deriv(u_C, t)"_s);
        ws.addTable({tq, uq, i});
        GraphView &g = graph(ws, u"Décharge"_s);
        Plot &p = g.plots[0];
        p.title.text = u"Tension aux bornes du condensateur"_s;
        p.addCurve(tq, uq).marker = Marker::Point;
        p.fits.push_back(FitSpec::make(fit::ModelKind{fit::ModelKind::Discharge}, tq, uq, PALETTE[3]));
        break;
    }
    case 2: {
        ws.meta.title = u"Dosage de l'acide éthanoïque par la soude"_s;
        // acide faible (pKa 4,76, 0,1 mol/L, 10 mL) par la soude à 0,1 mol/L
        auto phOf = [](double v) {
            const double ca = 0.1, va = 10.0, cb = 0.1, ka = std::pow(10.0, -4.76), kw = 1e-14;
            const double vt = va + v;
            const double cA = ca * va / vt, cNa = cb * v / vt;
            // électroneutralité : h + Na = OH + A ; résolution par dichotomie sur le pH
            auto f = [&](double ph) {
                const double h = std::pow(10.0, -ph);
                return h + cNa - kw / h - cA * ka / (ka + h);
            };
            double lo = 0.0, hi = 14.0;
            for (int k = 0; k < 80; ++k) {
                const double mid = 0.5 * (lo + hi);
                if (f(mid) > 0.0)
                    lo = mid;
                else
                    hi = mid;
            }
            return 0.5 * (lo + hi);
        };
        const auto v = generate(41, [](int i) { return i * 0.5; });
        const auto ph = map(v, [&](double x) { return phOf(x) + 0.02 * noise.next(); });
        const QId vq = add(ws, u"V"_s, u"mL"_s, v);
        const QId phq = add(ws, u"pH"_s, QString(), ph);
        const QId d = calc(ws, u"dpH_dV"_s, u"mL-1"_s, u"deriv(pH, V)"_s);
        ws.addTable({vq, phq, d});
        GraphView &g = graph(ws, u"Dosage"_s);
        Plot &p = g.plots[0];
        p.title.text = u"Courbe de dosage"_s;
        p.addCurve(vq, phq).line = LineKind::Monotone;
        CurveSpec &c = p.addCurve(vq, d);
        c.side = YSide::Right;
        c.marker = Marker::None;
        c.line = LineKind::Straight;
        c.dash = Dash::Dashed;
        TangentSpec t;
        t.x = vq;
        t.y = phq;
        t.color = PALETTE[2];
        p.tangents.push_back(t);
        break;
    }
    case 3: {
        ws.meta.title = u"Pendule élastique amorti"_s;
        const auto t = generate(250, [](int i) { return i * 0.02; });
        const auto x = map(t, [&](double s) { return 0.08 * std::exp(-s / 2.5) * std::cos(2.0 * M_PI * s / 0.8 + 0.3) + 0.0015 * noise.next(); });
        const QId tq = add(ws, u"t"_s, u"s"_s, t);
        const QId xq = add(ws, u"x"_s, u"m"_s, x);
        ws.addTable({tq, xq});
        GraphView &g = graph(ws, u"Oscillations"_s);
        Plot &p = g.plots[0];
        CurveSpec &c = p.addCurve(tq, xq);
        c.marker = Marker::Point;
        c.line = LineKind::Straight;
        c.lineWidth = 0.8;
        p.fits.push_back(FitSpec::make(fit::ModelKind{fit::ModelKind::DampedCosine}, tq, xq, PALETTE[3]));
        break;
    }
    case 4: {
        ws.meta.title = u"Mesures répétées de la période d'un pendule"_s;
        const auto T = generate(24, [&](int) { return std::round((1.418 + 0.012 * noise.next()) * 1000.0) / 1000.0; });
        const auto n = generate(24, [](int i) { return i + 1.0; });
        const QId nq = add(ws, u"n"_s, QString(), n);
        const QId tq = add(ws, u"T"_s, u"s"_s, T);
        ws.addTable({nq, tq});
        GraphView &g = graph(ws, u"Mesures"_s);
        CurveSpec &c = g.plots[0].addCurve(nq, tq);
        c.marker = Marker::Disc;
        c.markerSize = 6.0;
        break;
    }
    case 5: {
        ws.meta.title = u"Caractéristique d'un conducteur ohmique"_s;
        const auto i = generate(10, [](int k) { return k * 0.01; });
        const auto u = map(i, [&](double x) { return 100.0 * x + 0.03 * noise.next(); });
        const QId iq = add(ws, u"I"_s, u"A"_s, i);
        const QId uq = add(ws, u"U"_s, u"V"_s, u);
        const QId ui = *ws.doc.createUncertainty(iq, u"0.002"_s);
        const QId uu = *ws.doc.createUncertainty(uq, u"0.01*U + 0.02"_s);
        ws.addTable({iq, ui, uq, uu});
        GraphView &g = graph(ws, u"U(I)"_s);
        Plot &p = g.plots[0];
        p.title.text = u"Caractéristique U = f(I)"_s;
        CurveSpec &c = p.addCurve(iq, uq);
        c.errors = ErrorBars::Both;
        c.marker = Marker::None;
        p.x.includeZero = true;
        p.y.includeZero = true;
        p.fits.push_back(FitSpec::make(fit::ModelKind{fit::ModelKind::Linear}, iq, uq, PALETTE[3]));
        break;
    }
    case 6: {
        ws.meta.title = u"Analyse spectrale d'un son"_s;
        const double fs = 20000.0;
        const auto t = generate(2000, [&](int i) { return i / fs; });
        const auto s = map(t, [&](double x) {
            const double w = 2.0 * M_PI * 440.0;
            return std::sin(w * x) + 0.5 * std::sin(2.0 * w * x) + 0.25 * std::sin(3.0 * w * x + 0.4) + 0.01 * noise.next();
        });
        const QId tq = add(ws, u"t"_s, u"s"_s, t);
        const QId sq = add(ws, u"u"_s, u"V"_s, s);
        ws.addTable({tq, sq});
        GraphView &g = graph(ws, u"Signal"_s);
        CurveSpec &c = g.plots[0].addCurve(tq, sq);
        c.marker = Marker::None;
        c.line = LineKind::Straight;
        c.lineWidth = 1.0;
        g.plots[0].x.autoScale = false;
        g.plots[0].x.min = 0.0;
        g.plots[0].x.max = 0.01;
        break;
    }
    case 7: {
        ws.meta.title = u"Diffraction par une fente"_s;
        const ParamId a = *ws.doc.addParameter(u"a"_s, u"m"_s, 1e-4);
        if (Parameter *p = ws.doc.paramMut(a)) {
            p->min = 2e-5;
            p->max = 4e-4;
            p->step = 1e-6;
        }
        const ParamId l = *ws.doc.addParameter(u"l"_s, u"m"_s, 633e-9);
        if (Parameter *p = ws.doc.paramMut(l)) {
            p->min = 400e-9;
            p->max = 750e-9;
            p->step = 1e-9;
        }
        ws.doc.addParameter(u"D"_s, u"m"_s, 2.0);
        const std::vector<double> as = {5e-5, 1e-4, 1.5e-4, 2e-4, 3e-4, 4e-4};
        const QId am = add(ws, u"a_mes"_s, u"m"_s, as);
        const QId lq = add(ws, u"L"_s, u"m"_s, map(as, [&](double x) { return 2.0 * 633e-9 * 2.0 / x * (1.0 + 0.02 * noise.next()); }));
        const QId ul = *ws.doc.createUncertainty(lq, u"0.001"_s);
        const QId inv = calc(ws, u"inv_a"_s, u"m-1"_s, u"1/a_mes"_s);
        ws.addTable({am, inv, lq, ul});
        GraphView &g = graph(ws, u"L(1/a)"_s);
        Plot &p = g.plots[0];
        p.title.text = u"Largeur de la tache centrale"_s;
        p.addCurve(inv, lq).errors = ErrorBars::Vertical;
        p.x.includeZero = true;
        p.y.includeZero = true;
        p.fits.push_back(FitSpec::make(fit::ModelKind{fit::ModelKind::Linear}, inv, lq, PALETTE[3]));
        const ViewId sid = ws.addScript(QString::fromUtf8(DIFFRACTION));
        Script *s = ws.scriptMut(sid);
        s->name = u"Diffraction"_s;
        s->linked = true;
        break;
    }
    default:
        break;
    }
    ws.doc.recompute();
    return ws;
}

} // namespace examples
