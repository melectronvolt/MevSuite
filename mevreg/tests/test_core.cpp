// Tests du cœur : la physique et numpy servent d'oracle.

#include "core/csv.h"
#include "core/document.h"
#include "core/expr.h"
#include "core/fft.h"
#include "core/fit.h"
#include "core/format.h"
#include "core/linalg.h"
#include "core/numeric.h"
#include "core/pyexport.h"
#include "core/series.h"
#include "core/spline.h"
#include "core/stats.h"
#include "core/titration.h"
#include "core/workspace.h"
#include "plot/resolve.h"

#include <QJsonDocument>
#include <QTest>

#include <cmath>
#include <numbers>
#include <random>

using namespace Qt::StringLiterals;
using std::numbers::pi;

namespace {

std::vector<double> linspace(double a, double b, int n)
{
    std::vector<double> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = a + (b - a) * i / (n - 1);
    return v;
}

double scalar(const QString &src)
{
    auto v = expr::evalStr(src, {});
    return v && v->scalar ? v->s : NAN;
}

QString evalError(const QString &src)
{
    expr::Error e;
    expr::evalStr(src, {}, &e);
    return e.message;
}

constexpr NumberStyle DOT{false};
constexpr NumberStyle COMMA{true};

/// Générateur pseudo-aléatoire reproductible et bruit gaussien.
struct Rng {
    std::uint64_t s;
    double next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<double>(s >> 11) / static_cast<double>(1ull << 53);
    }
    double gauss()
    {
        const double u1 = std::max(next(), 1e-300), u2 = next();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * pi * u2);
    }
};

} // namespace

class TestCore : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- nombres et unités ---
    void parseNumbers()
    {
        QCOMPARE(parseNumber(u"1,5"_s), 1.5);
        QCOMPARE(parseNumber(u" -2.5e-3 "_s), -2.5e-3);
        QCOMPARE(parseNumber(u"1 000,5"_s), 1000.5);
        QCOMPARE(parseNumber(u"3E2"_s), 300.0);
        QVERIFY(!parseNumber(u""_s));
        QVERIFY(!parseNumber(u"abc"_s));
        QVERIFY(!parseNumber(u"nan"_s));
        QCOMPARE(parseNumber(u"−4"_s), -4.0);
    }

    void significantDigits()
    {
        QCOMPARE(formatSig(1.23456789, 4, DOT), u"1.235"_s);
        QCOMPARE(formatSig(1.5, 6, COMMA), u"1,5"_s);
        QCOMPARE(formatSig(0.000123, 3, DOT), u"0.000123"_s);
        QCOMPARE(formatSig(1.23e-7, 3, DOT), u"1.23e-7"_s);
        QCOMPARE(formatSig(100.0, 6, DOT), u"100"_s);
        QCOMPARE(formatSig(-0.5, 6, DOT), u"-0.5"_s);
        QCOMPARE(formatSig(NAN, 6, DOT), QString());
        QCOMPARE(formatSci(1.5e-4, 2, COMMA), u"1,50×10⁻⁴"_s);
        QCOMPARE(formatSci(2.0, 1, DOT), u"2.0"_s);
        QCOMPARE(formatFixed(-0.0001, 2, DOT), u"0.00"_s);
        QCOMPARE(pythonFloatRepr(2.0), u"2.0"_s);
        QCOMPARE(pythonFloatRepr(0.1), u"0.1"_s);
        QCOMPARE(pythonFloatRepr(1e-5), u"1e-05"_s);
        QCOMPARE(pythonFloatRepr(1.5e20), u"1.5e+20"_s);
    }

    void units()
    {
        QCOMPARE(prettyUnit(u"m.s-1"_s), u"m·s⁻¹"_s);
        QCOMPARE(prettyUnit(u"m/s^2"_s), u"m/s²"_s);
        QCOMPARE(prettyUnit(u"kg.m^-3"_s), u"kg·m⁻³"_s);
        QCOMPARE(prettyUnit(u"m2"_s), u"m²"_s);
        QCOMPARE(prettyUnit(u"mol.L-1"_s), u"mol·L⁻¹"_s);
        QCOMPARE(prettyUnit(u"V"_s), u"V"_s);
        QCOMPARE(prettyUnit(u"°C"_s), u"°C"_s);
        QCOMPARE(prettyUnit(u"10^3 Pa"_s), u"10³ Pa"_s);
    }

    void roundedMeasure()
    {
        auto r = roundMeasure(9.81234, 0.0234, 2, COMMA);
        QCOMPARE(r.value, u"9,812"_s);
        QCOMPARE(r.uncertainty, u"0,023"_s);
        QCOMPARE(r.toText(u"m/s^2"_s), u"(9,812 ± 0,023) m/s²"_s);
        r = roundMeasure(9.81234, 0.0996, 2, DOT);
        QCOMPARE(r.value, u"9.81"_s);
        QCOMPARE(r.uncertainty, u"0.10"_s);
        r = roundMeasure(1.2346e-5, 2.1e-7, 2, DOT);
        QCOMPARE(r.exponent, -6);
        QCOMPARE(r.value, u"12.35"_s);
        QCOMPARE(r.uncertainty, u"0.21"_s);
        r = roundMeasure(1523.0, 12.0, 1, DOT);
        QCOMPARE(r.value, u"1520"_s);
        QCOMPARE(r.uncertainty, u"10"_s);
    }

    // --- formules ---
    void arithmetic()
    {
        QCOMPARE(scalar(u"1+2*3"_s), 7.0);
        QCOMPARE(scalar(u"(1+2)*3"_s), 9.0);
        QCOMPARE(scalar(u"2^3^2"_s), 512.0);
        QCOMPARE(scalar(u"2**3"_s), 8.0);
        QCOMPARE(scalar(u"-2^2"_s), -4.0);
        QCOMPARE(scalar(u"2^-1"_s), 0.5);
        QCOMPARE(scalar(u"10/4"_s), 2.5);
        QCOMPARE(scalar(u"1e3 + 2.5E-1"_s), 1000.25);
        QCOMPARE(scalar(u"3²"_s), 9.0);
        QCOMPARE(scalar(u"2 × 3 − 1"_s), 5.0);
        QVERIFY(std::abs(scalar(u"sin(pi/2)"_s) - 1.0) < 1e-15);
        QCOMPARE(scalar(u"log(1000)"_s), 3.0);
        QVERIFY(std::abs(scalar(u"ln(e)"_s) - 1.0) < 1e-15);
        QCOMPARE(scalar(u"max(2, 5)"_s), 5.0);
        QVERIFY(std::isnan(scalar(u"(-8)^(1/3)"_s)));
        QCOMPARE(scalar(u"(-2)^3"_s), -8.0);
    }

    void vectors()
    {
        auto env = [](const QString &n) -> std::optional<expr::Value> {
            if (n == u"v")
                return expr::Value::vector({1, 2, 3});
            if (n == u"m")
                return expr::Value::number(2);
            if (n == u"b")
                return expr::Value::vector({10, 20});
            if (n == u"c")
                return expr::Value::vector({10});
            return std::nullopt;
        };
        QCOMPARE(expr::evalStr(u"0.5*m*v^2"_s, env)->v, (std::vector<double>{1, 4, 9}));
        QCOMPARE(expr::evalStr(u"mean(v)"_s, env)->s, 2.0);
        QCOMPARE(expr::evalStr(u"len(v)"_s, env)->s, 3.0);
        const auto r = expr::evalStr(u"v+b"_s, env)->v;
        QCOMPARE(r[0], 11.0);
        QCOMPARE(r[1], 22.0);
        QVERIFY(std::isnan(r[2]));
        QCOMPARE(expr::evalStr(u"v*c"_s, env)->v, (std::vector<double>{10, 20, 30}));
    }

    void formulaErrors()
    {
        QVERIFY(evalError(u"x+1"_s).contains(u"« x »"_s));
        QVERIFY(expr::parse(u"2x"_s).error.message.contains(u"opérateur manquant"_s));
        QVERIFY(expr::parse(u"0,5*m"_s).error.message.contains(u"point"_s));
        QVERIFY(expr::parse(u"(1+2"_s).error.message.contains(u"parenthèse"_s));
        QVERIFY(!expr::parse(u"1+"_s).ok());
        QVERIFY(!expr::parse(u""_s).ok());
        QVERIFY(evalError(u"foo(2)"_s).contains(u"inconnue"_s));
        QVERIFY(evalError(u"sin(1, 2)"_s).contains(u"1 argument"_s));
    }

    void renaming()
    {
        QCOMPARE(expr::renameIdentifier(u"0.5*m*v^2 + v"_s, u"v"_s, u"vitesse"_s), u"0.5*m*vitesse^2 + vitesse"_s);
        QCOMPARE(expr::renameIdentifier(u"deriv(x, t)"_s, u"x"_s, u"pos"_s), u"deriv(pos, t)"_s);
        QCOMPARE(expr::renameIdentifier(u"vv + v"_s, u"v"_s, u"w"_s), u"vv + w"_s);
        QCOMPARE(expr::renameIdentifier(u"exp(t)"_s, u"exp"_s, u"z"_s), u"exp(t)"_s);
        QVERIFY(expr::usesIdentifier(u"a*exp(t)"_s, u"t"_s));
        QVERIFY(!expr::usesIdentifier(u"a*exp(t)"_s, u"exp"_s));
        QVERIFY(!expr::usesIdentifier(u"tt"_s, u"t"_s));
        const auto e = expr::parse(u"A*exp(-t/tau) + B"_s);
        QCOMPARE(e.expr->variables(), (QStringList{u"A"_s, u"t"_s, u"tau"_s, u"B"_s}));
        QVERIFY(expr::isValidName(u"v_x"_s));
        QVERIFY(expr::isValidName(u"Δt"_s));
        QVERIFY(expr::isValidName(u"τ"_s));
        QVERIFY(!expr::isValidName(u"2x"_s));
        QVERIFY(!expr::isValidName(u"u(x)"_s));
        const auto d = expr::splitDefinition(u"Ec = 0.5*m*v^2"_s);
        QCOMPARE(d->first, u"Ec"_s);
        QCOMPARE(d->second, u"0.5*m*v^2"_s);
    }

    // --- calculs numériques ---
    void gradientOfSine()
    {
        const auto t = linspace(0, 2 * pi, 629);
        std::vector<double> y(t.size());
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = std::sin(t[i]);
        const auto g = numeric::gradient(y, t);
        for (size_t i = 1; i + 1 < t.size(); ++i)
            QVERIFY(std::abs(g[i] - std::cos(t[i])) < 1e-4);
        QVERIFY(std::abs(g[0] - 1.0) < 0.01);
        // valeurs obtenues avec numpy.gradient([1, 4, 9, 25], [0, 1, 1.5, 3])
        const auto h = numeric::gradient({1, 4, 9, 25}, {0, 1, 1.5, 3});
        const double expected[] = {3.0, 7.666666666666667, 10.166666666666666, 10.666666666666666};
        for (int i = 0; i < 4; ++i)
            QVERIFY(std::abs(h[i] - expected[i]) < 1e-12);
        // cases vides et abscisses dupliquées
        const auto k = numeric::gradient({1, NAN, 3, 4}, {0, 1, 2, 3});
        QVERIFY(std::isnan(k[1]));
        QVERIFY(std::abs(k[0] - 1.0) < 1e-12);
        const auto z = numeric::gradient({1, 2}, {1, 1});
        QVERIFY(std::isnan(z[0]) && std::isnan(z[1]));
    }

    void integral()
    {
        const auto t = linspace(0, pi, 1001);
        std::vector<double> y(t.size());
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = std::sin(t[i]);
        const auto c = numeric::cumulativeTrapezoid(y, t);
        QCOMPARE(c[0], 0.0);
        QVERIFY(std::abs(c[1000] - 2.0) < 1e-5);
        QVERIFY(std::abs(numeric::trapezoid(y, t) - 2.0) < 1e-5);
        QVERIFY(std::abs(numeric::areaBetween(y, t, 0.0, pi / 2) - 1.0) < 1e-5);
        QVERIFY(std::abs(numeric::areaBetween({1, 1}, {0, 10}, 2, 5) - 3.0) < 1e-12);
        QCOMPARE(numeric::interp(0.5, {0, 1, 2}, {0, 10, 0}), 5.0);
        QCOMPARE(numeric::interp(-1.0, {0, 1, 2}, {0, 10, 0}), 0.0);
        const auto s = numeric::stepInfo({0, 0.1, 0.2, 0.3});
        QVERIFY(std::abs(s->first - 0.1) < 1e-12 && s->second < 1e-9);
    }

    void deriveIntegrateRoundtrip()
    {
        // propriété : dériver la primitive redonne la fonction
        std::mt19937 gen(42);
        std::uniform_real_distribution<double> d(-3, 3);
        for (int k = 0; k < 200; ++k) {
            const double a = d(gen), b = d(gen), c = d(gen);
            const int n = 50 + k;
            std::vector<double> t(n), y(n);
            for (int i = 0; i < n; ++i) {
                t[i] = 2.0 * i / (n - 1);
                y[i] = a + b * t[i] + c * t[i] * t[i];
            }
            const auto back = numeric::gradient(numeric::cumulativeTrapezoid(y, t), t);
            const double h = 2.0 / (n - 1);
            for (int i = 1; i + 1 < n; ++i)
                QVERIFY(std::abs(back[i] - y[i]) < 10 * h * h * (1 + std::abs(a) + std::abs(b) + std::abs(c)) + 1e-9);
        }
    }

    void sequences()
    {
        QCOMPARE(series::byStep(0, 1, 0.1).values, (std::vector<double>{0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0}));
        QCOMPARE(series::byStep(0, 10, 2.5).values, (std::vector<double>{0, 2.5, 5, 7.5, 10}));
        QCOMPARE(series::byStep(0, 1, 0.3).values, (std::vector<double>{0, 0.3, 0.6, 0.9}));
        QCOMPARE(series::byStep(5, 1, -2).values, (std::vector<double>{5, 3, 1}));
        QVERIFY(!series::byStep(0, 1, 0).ok());
        QVERIFY(!series::byStep(0, 1, -0.1).ok());
        QVERIFY(!series::byStep(0, 1e9, 1e-3).ok());
        QCOMPARE(series::byCount(0, 1, 5).values, (std::vector<double>{0, 0.25, 0.5, 0.75, 1}));
        QCOMPARE(series::byCount(0, 0.6, 7).values, (std::vector<double>{0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6}));
        QCOMPARE(series::extend({1, 3}, 3), (std::vector<double>{5, 7, 9}));
        QCOMPARE(series::extend({0, 0.1}, 3), (std::vector<double>{0.2, 0.3, 0.4}));
        QCOMPARE(series::extend({10, 8, 6}, 2), (std::vector<double>{4, 2}));
        QCOMPARE(series::extend({4.2}, 2), (std::vector<double>{4.2, 4.2}));
        QCOMPARE(series::extend({1, NAN, 3}, 2), (std::vector<double>{4, 5}));
    }

    void linearAlgebra()
    {
        linalg::Matrix a{3, 3, {2, 1, -1, -3, -1, 2, -2, 1, 2}};
        const auto x = linalg::solve(a, {8, -11, -3});
        QVERIFY(x);
        QVERIFY(std::abs((*x)[0] - 2) < 1e-12 && std::abs((*x)[1] - 3) < 1e-12 && std::abs((*x)[2] + 1) < 1e-12);
        QVERIFY(!linalg::solve({2, 2, {1, 2, 2, 4}}, {1, 2}));
        const auto inv = linalg::inverseSpd({2, 2, {4, 2, 2, 3}});
        QVERIFY(inv && std::abs((*inv)(0, 0) - 0.375) < 1e-12 && std::abs((*inv)(1, 1) - 0.5) < 1e-12);
        // polynôme mal conditionné (abscisses autour de 1000)
        auto m = linalg::Matrix::zeros(20, 3);
        std::vector<double> b(20);
        for (int i = 0; i < 20; ++i) {
            const double xv = 1000.0 + i;
            m(i, 0) = 1;
            m(i, 1) = xv;
            m(i, 2) = xv * xv;
            b[i] = 3 - 2 * xv + 0.5 * xv * xv;
        }
        const auto p = linalg::leastSquares(m, b);
        QVERIFY(p && std::abs((*p)[2] - 0.5) < 1e-6 && std::abs((*p)[1] + 2.0) < 1e-3);
    }

    void splines()
    {
        const std::vector<double> x{0, 1, 1.5, 3, 4, 6}, y{0, 2, 1, 5, 4, 4.5};
        for (auto kind : {spline::Kind::Cubic, spline::Kind::Monotone}) {
            const auto h = spline::Hermite::make(x, y, kind);
            QVERIFY(h);
            for (size_t i = 0; i < x.size(); ++i)
                QVERIFY(std::abs(h->eval(x[i]) - y[i]) < 1e-12);
        }
        const auto line = spline::Hermite::make({0, 1, 2, 3}, {1, 3, 5, 7}, spline::Kind::Cubic);
        QVERIFY(std::abs(line->eval(1.37) - (1 + 2 * 1.37)) < 1e-12);
        const auto nat = spline::Hermite::make({0, 1, 2}, {0, 1, 0}, spline::Kind::Cubic);
        QVERIFY(std::abs(nat->eval(0.5) - 0.6875) < 1e-12);
        const auto pchip = spline::Hermite::make({0, 1, 2, 4}, {0, 1, 4, 5}, spline::Kind::Monotone);
        QVERIFY(std::abs(pchip->d[1] - 1.5) < 1e-12);
        QVERIFY(std::abs(pchip->d[2] - 9.0 / (5.0 / 3.0 + 8.0)) < 1e-12);
        QCOMPARE(pchip->d[0], 0.0);
        // propriété : la variante monotone ne dépasse jamais les valeurs de deux points voisins
        std::mt19937 gen(7);
        std::uniform_real_distribution<double> step(0.01, 2.0), val(-100, 100);
        for (int k = 0; k < 300; ++k) {
            std::vector<double> xs{0}, ys{val(gen)};
            const int n = 3 + k % 30;
            for (int i = 0; i < n; ++i) {
                xs.push_back(xs.back() + step(gen));
                ys.push_back(val(gen));
            }
            const auto h = spline::Hermite::make(xs, ys, spline::Kind::Monotone);
            for (size_t s = 0; s + 1 < xs.size(); ++s) {
                const double lo = std::min(ys[s], ys[s + 1]), hi = std::max(ys[s], ys[s + 1]);
                for (int j = 0; j <= 20; ++j) {
                    const double v = h->eval(xs[s] + (xs[s + 1] - xs[s]) * j / 20.0);
                    QVERIFY(v >= lo - 1e-9 * (1 + std::abs(lo)) && v <= hi + 1e-9 * (1 + std::abs(hi)));
                }
            }
        }
        // trajectoire fermée : interpolation paramétrique
        std::vector<double> cx, cy;
        for (int i = 0; i <= 12; ++i) {
            cx.push_back(std::cos(i / 12.0 * 6.283));
            cy.push_back(std::sin(i / 12.0 * 6.283));
        }
        const auto s = spline::smoothCurve(cx, cy, spline::Kind::Cubic, 8);
        QCOMPARE(s.size(), size_t(12 * 8 + 1));
        for (const auto &[a, b] : s)
            QVERIFY(std::abs(std::hypot(a, b) - 1.0) < 0.03);
    }

    void spectra()
    {
        const int n = 1000;
        const double dt = 1.0 / 44000.0;
        std::vector<double> t(n), y(n);
        for (int i = 0; i < n; ++i) {
            t[i] = i * dt;
            y[i] = 2.0 * std::sin(2 * pi * 440.0 * t[i]);
        }
        auto peak = [](const fft::Spectrum &s) {
            size_t k = 1;
            for (size_t i = 1; i < s.amplitude.size(); ++i) {
                if (s.amplitude[i] > s.amplitude[k])
                    k = i;
            }
            return std::make_pair(s.freq[k], s.amplitude[k]);
        };
        QString err;
        auto s = fft::amplitudeSpectrum(t, y, {}, &err);
        QVERIFY(s);
        auto [f, a] = peak(*s);
        QVERIFY(std::abs(f - 440.0) < 1e-9);
        QVERIFY(std::abs(a - 2.0) < 1e-9);
        s = fft::amplitudeSpectrum(t, y, {fft::Window::Hann, 4, 0.01}, &err);
        std::tie(f, a) = peak(*s);
        QVERIFY(std::abs(f - 440.0) < 1e-9);
        QVERIFY(std::abs(a - 2.0) < 0.01);
        // carré de 1 V à 100 Hz : harmoniques impaires 4/(πn)
        std::vector<double> t2(10000), sq(10000);
        for (int i = 0; i < 10000; ++i) {
            t2[i] = i * 1e-5;
            sq[i] = std::sin(2 * pi * 100 * (t2[i] + 0.5e-5)) >= 0 ? 1.0 : -1.0;
        }
        s = fft::amplitudeSpectrum(t2, sq, {}, &err);
        auto at = [&](double fr) { return s->amplitude[static_cast<size_t>(std::lround(fr / s->freq[1]))]; };
        for (double h : {1.0, 3.0, 5.0, 7.0})
            QVERIFY(std::abs(at(100 * h) - 4 / (pi * h)) / (4 / (pi * h)) < 0.01);
        for (double h : {2.0, 4.0, 6.0})
            QVERIFY(at(100 * h) < 0.01);
        // composante continue, pas irrégulier refusé
        s = fft::amplitudeSpectrum(linspace(0, 63, 64), std::vector<double>(64, 3.0), {}, &err);
        QVERIFY(std::abs(s->amplitude[0] - 3.0) < 1e-12);
        QVERIFY(!fft::amplitudeSpectrum({0, 1, 2, 3.5, 4}, {0, 1, 0, -1, 0}, {}, &err));
        QVERIFY(err.contains(u"pas"_s));
        // fréquence dominante
        std::vector<double> t3(300), y3(300);
        for (int i = 0; i < 300; ++i) {
            t3[i] = i * 0.01;
            y3[i] = 1.5 + std::sin(2 * pi * 2.3 * t3[i] + 0.4);
        }
        QVERIFY(std::abs(*fft::dominantFrequency(t3, y3) - 2.3) < 0.02);
        // transformée exacte sur 4 points, comme numpy
        std::vector<fft::Complex> v{1, 2, 3, 4};
        fft::transform(v, false);
        QCOMPARE(v[0], fft::Complex(10, 0));
        QCOMPARE(v[1], fft::Complex(-2, 2));
        QCOMPARE(v[2], fft::Complex(-2, 0));
        QCOMPARE(v[3], fft::Complex(-2, -2));
        // Bluestein (taille première) : aller-retour
        std::vector<fft::Complex> w(37);
        for (size_t i = 0; i < w.size(); ++i)
            w[i] = fft::Complex(std::cos(i * 0.3), std::sin(i * 0.7));
        auto ww = w;
        fft::transform(ww, false);
        fft::transform(ww, true);
        for (size_t i = 0; i < w.size(); ++i)
            QVERIFY(std::abs(ww[i] / 37.0 - w[i]) < 1e-12);
    }

    void statistics()
    {
        const auto s = stats::summarize({9.81, 9.75, 9.86, 9.74, 9.79});
        QVERIFY(s);
        QCOMPARE(s->n, size_t(5));
        QVERIFY(std::abs(s->mean - 9.79) < 1e-12);
        QVERIFY(std::abs(s->stdPop - 0.04335896677735741) < 1e-12);
        QVERIFY(std::abs(s->stdSample - 0.04847679857416308) < 1e-12);
        QCOMPARE(s->min, 9.74);
        QCOMPARE(s->max, 9.86);
        const auto r = s->result(COMMA);
        QCOMPARE(r.value, u"9,790"_s);
        QCOMPARE(r.uncertainty, u"0,022"_s);
        QVERIFY(!stats::summarize({}));
        QVERIFY(std::isnan(stats::summarize({3.0})->stdSample));
        const auto h = stats::histogram({1, 2, 2, 3, 4}, 3);
        QCOMPARE(h->counts, (std::vector<size_t>{1, 2, 2}));
        QCOMPARE(h->edges, (std::vector<double>{1, 2, 3, 4}));
        QCOMPARE(stats::defaultBins(100), size_t(8));
    }

    void titrationMethods()
    {
        // pH théorique du dosage de 10 mL d'acide fort à 0,1 mol/L par une base forte à 0,1 mol/L
        auto ph = [](double v) {
            const double d = (0.1 * 10 - 0.1 * v) / (10 + v);
            return -std::log10((d + std::sqrt(d * d + 4e-14)) / 2);
        };
        std::vector<double> v, p;
        for (int i = 0; i <= 200; ++i) {
            v.push_back(i * 0.1);
            p.push_back(ph(i * 0.1));
        }
        QString err;
        const auto c = titration::Curve::make(v, p, &err);
        QVERIFY(c);
        QVERIFY(std::abs(c->jump() - 10.0) < 0.05);
        const auto t = c->tangents(c->defaultContact(), &err);
        QVERIFY(t);
        QVERIFY(std::abs(t->equivalence.first - 10.0) < 0.05);
        QVERIFY(std::abs(t->median(5.0) - 0.5 * (t->line1(5.0) + t->line2(5.0))) < 1e-12);
        const auto t2 = c->tangents(9.0, &err);
        QVERIFY(std::abs(t2->equivalence.first - 10.0) < 0.1);
        const auto t3 = c->tangents(11.0, &err);
        QVERIFY(t3->p1.first < 10.0 && t3->p2.first > 10.0);
        QVERIFY(std::abs(titration::derivativeMaximum(v, p, &err)->first - 10.0) < 0.1);
        // courbe symétrique : V_E exact
        std::vector<double> v2, p2;
        for (int i = 0; i <= 80; ++i) {
            v2.push_back(i * 0.25);
            p2.push_back(7 + 4 * std::tanh((i * 0.25 - 10) / 0.4));
        }
        const auto c2 = titration::Curve::make(v2, p2, &err);
        QVERIFY(std::abs(c2->tangents(c2->defaultContact(), &err)->equivalence.first - 10.0) < 1e-3);
        QVERIFY(std::abs(titration::derivativeMaximum(v2, p2, &err)->first - 10.0) < 1e-6);
        QVERIFY(!titration::Curve::make({1, 2}, {3, 4}, &err));
    }

    // --- modélisation ---
    void allModelsRecoverParameters()
    {
        auto check = [](fit::ModelKind kind, std::vector<double> truth, std::vector<double> xs, double noise, double tol) {
            const auto model = fit::Model::make(kind, u"x"_s, {});
            QVERIFY(model);
            Rng rng{12345};
            auto ys = model->eval(truth, xs);
            for (double &y : ys)
                y += noise * rng.gauss();
            std::vector<std::optional<double>> fixed;
            for (const QString &n : model->params()) {
                std::optional<double> f;
                for (const auto &[k, v] : kind.defaultFixed()) {
                    if (k == n)
                        f = v;
                }
                fixed.push_back(f);
            }
            QString err;
            const auto r = fit::fit(*model, xs, ys, {fixed, std::nullopt}, &err);
            QVERIFY2(r, qPrintable(kind.label() + u" : "_s + err));
            for (size_t i = 0; i < truth.size(); ++i) {
                const double scale = truth[i] == 0.0 ? 0.1 : std::abs(truth[i]);
                const double u = r->uncertainties[i];
                const bool ok = std::abs(r->params[i] - truth[i]) / scale < tol || (std::isfinite(u) && std::abs(r->params[i] - truth[i]) < 4 * u);
                QVERIFY2(ok, qPrintable(u"%1 : %2 = %3, attendu %4"_s.arg(kind.label(), model->params()[i]).arg(r->params[i]).arg(truth[i])));
            }
            if (kind.type != fit::ModelKind::Constant)
                QVERIFY2(r->r2 > 0.9, qPrintable(kind.label()));
        };
        using MK = fit::ModelKind;
        auto k = [](MK::Type t) {
            MK m;
            m.type = t;
            return m;
        };
        const auto x = linspace(0, 10, 80);
        const auto xp = linspace(0.5, 10, 80);
        check(k(MK::Constant), {3.2}, x, 0.05, 0.01);
        check(k(MK::Linear), {2.5}, x, 0.05, 0.01);
        check(k(MK::Affine), {2.5, -1.2}, x, 0.05, 0.03);
        check(k(MK::Polynomial), {1.0, -2.0, 0.5, 0.03}, x, 0.02, 0.05);
        check(k(MK::Parabola), {-4.9, 3.0, 1.5}, x, 0.05, 0.01);
        check(k(MK::Power), {1.7, 1.5}, xp, 0.02, 0.01);
        check(k(MK::ExpX), {2.0, -0.35}, x, 0.01, 0.02);
        check(k(MK::ExpInvX), {3.0, -1.2}, xp, 0.005, 0.02);
        check(k(MK::Charge), {5.0, 0.0, 1.8, 0.2}, x, 0.02, 0.02);
        check(k(MK::Discharge), {5.0, 0.0, 1.8, 0.2}, x, 0.02, 0.02);
        check(k(MK::Ln), {2.0, 1.0}, xp, 0.01, 0.02);
        check(k(MK::Log10), {2.0, 1.0}, xp, 0.01, 0.02);
        check(k(MK::Sine), {1.5, 2.3, 0.7, 0.4}, x, 0.02, 0.02);
        check(k(MK::Cosine), {1.5, 2.3, -0.7, 0.4}, x, 0.02, 0.02);
        check(k(MK::DampedSine), {2.0, 4.0, 1.7, 0.5, 0.1}, x, 0.02, 0.03);
        check(k(MK::DampedCosine), {2.0, 4.0, 1.7, -0.5, 0.1}, x, 0.02, 0.03);
        MK user = k(MK::User);
        user.expression = u"a*exp(-x/tau)+c"_s;
        check(user, {3.0, 2.0, 0.5}, x, 0.01, 0.03);
        // échelles physiques : décharge (τ = 1 ms) et son à 440 Hz
        check(k(MK::Discharge), {5.0, 0.0, 1e-3, 0.0}, linspace(0, 5e-3, 250), 0.02, 0.02);
        check(k(MK::Sine), {0.8, 1.0 / 440.0, 1.0, 0.0}, linspace(0, 0.02, 400), 0.01, 0.02);
    }

    void affineUncertainties()
    {
        fit::ModelKind affine;
        affine.type = fit::ModelKind::Affine;
        const auto m = fit::Model::make(affine, u"x"_s, {});
        QString err;
        auto r = fit::fit(*m, {0, 1, 2, 3}, {1, 3, 5, 7}, {}, &err);
        QVERIFY(std::abs(r->params[0] - 2.0) < 1e-12 && std::abs(r->params[1] - 1.0) < 1e-12);
        QVERIFY(r->uncertainties[0] < 1e-10);
        QVERIFY(std::abs(r->r2 - 1.0) < 1e-12);
        const std::vector<double> x{0, 1, 2, 3}, y{1.1, 2.9, 5.2, 6.8};
        r = fit::fit(*m, x, y, {}, &err);
        QVERIFY(std::abs(r->params[0] - 1.94) < 1e-12);
        QVERIFY(std::abs(r->params[1] - 1.09) < 1e-12);
        double s2 = 0;
        for (int i = 0; i < 4; ++i)
            s2 += std::pow(y[i] - 1.94 * x[i] - 1.09, 2);
        s2 /= 2.0;
        QVERIFY(std::abs(r->uncertainties[0] - std::sqrt(s2 / 5.0)) < 1e-12);
        QVERIFY(std::abs(r->uncertainties[1] - std::sqrt(s2 * 14.0 / 20.0)) < 1e-12);
        // paramètre fixé
        const auto xs = linspace(0, 4, 20);
        std::vector<double> ys;
        for (double v : xs)
            ys.push_back(3 * v + 2);
        r = fit::fit(*m, xs, ys, {{std::nullopt, 0.0}, std::nullopt}, &err);
        QVERIFY(std::isnan(r->uncertainties[1]));
        QCOMPARE(r->params[1], 0.0);
        QVERIFY(r->params[0] > 3.0);
    }

    void fitErrorsAndUserModels()
    {
        using MK = fit::ModelKind;
        QString err;
        MK parabola;
        parabola.type = MK::Parabola;
        QVERIFY(!fit::fit(*fit::Model::make(parabola, u"x"_s, {}), {1, 2}, {1, 2}, {}, &err));
        QVERIFY(err.contains(u"pas assez"_s));
        MK ln;
        ln.type = MK::Ln;
        QVERIFY(!fit::fit(*fit::Model::make(ln, u"x"_s, {}), {-1, 2, 3}, {1, 2, 3}, {}, &err));
        QVERIFY(err.contains(u"logarithme"_s));
        MK user;
        user.type = MK::User;
        user.expression = u"a*t"_s;
        QVERIFY(!fit::Model::make(user, u"x"_s, {}, &err));
        QVERIFY(err.contains(u"variable"_s));
        user.expression = u"2*x"_s;
        QVERIFY(!fit::Model::make(user, u"x"_s, {}, &err));
        QVERIFY(err.contains(u"paramètre"_s));
        // g connu (paramètre du document), seul v0 est ajusté
        user.expression = u"z = v0*t - 0.5*g*t^2"_s;
        const auto m = fit::Model::make(user, u"t"_s, {{u"g"_s, 9.81}});
        QCOMPARE(m->params(), QStringList{u"v0"_s});
        const auto t = linspace(0, 1, 20);
        std::vector<double> z;
        for (double v : t)
            z.push_back(4 * v - 0.5 * 9.81 * v * v);
        QVERIFY(std::abs(fit::fit(*m, t, z, {}, &err)->params[0] - 4.0) < 1e-8);
    }

    void formulaRoundtrip()
    {
        // la formule exportée redonne les mêmes valeurs que le modèle
        for (auto kind : fit::ModelKind::catalog()) {
            if (kind.type == fit::ModelKind::User)
                kind.expression = u"a*sin(t)+b"_s;
            const auto m = fit::Model::make(kind, u"t"_s, {});
            QVERIFY(m);
            std::vector<double> p;
            for (qsizetype i = 0; i < m->params().size(); ++i)
                p.push_back(0.7 + 0.3 * i * (i % 2 == 0 ? 1 : -1));
            const auto xs = linspace(0.5, 3, 7);
            const auto expected = m->eval(p, xs);
            const QString f = m->formula(p, u"t"_s);
            auto env = [&](const QString &n) -> std::optional<expr::Value> {
                if (n == u"t")
                    return expr::Value::vector(xs);
                return std::nullopt;
            };
            const auto got = expr::evalStr(f, env);
            QVERIFY2(got, qPrintable(f));
            for (size_t i = 0; i < xs.size(); ++i)
                QVERIFY2(std::abs(got->v[i] - expected[i]) < 1e-9 * (1 + std::abs(expected[i])), qPrintable(kind.label() + u" : "_s + f));
            QVERIFY(!m->display(p, u"t"_s, u"y"_s, NumberStyle{}).isEmpty());
        }
    }

    // --- document et espace de travail ---
    void calculatedAndDependencies()
    {
        Document d;
        const QId t = *d.addExperimental(u"t"_s, u"s"_s, {0, 1, 2, 3});
        const QId x = *d.addExperimental(u"x"_s, u"m"_s, {0, 1, 4, 9});
        Q_UNUSED(t);
        // ordre de création inverse de l'ordre de calcul
        const QId ec = *d.addCalculated(u"Ec"_s, u"J"_s, u"0.5*m*v^2"_s);
        const QId v = *d.addCalculated(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        QVERIFY(!d.get(ec)->error.isEmpty());
        d.addParameter(u"m"_s, u"kg"_s, 2.0);
        QVERIFY(d.get(ec)->error.isEmpty());
        QCOMPARE(d.values(v), (std::vector<double>{1, 2, 4, 5}));
        QCOMPARE(d.values(ec), (std::vector<double>{1, 4, 16, 25}));
        d.setValue(x, 3, 10.0);
        QCOMPARE(d.values(v)[3], 6.0);
    }

    void circularDependency()
    {
        Document d;
        d.addExperimental(u"x"_s, {}, {1, 2});
        const QId a = *d.addCalculated(u"a"_s, {}, u"b + 1"_s);
        const QId b = *d.addCalculated(u"b"_s, {}, u"a * 2"_s);
        const QId c = *d.addCalculated(u"c"_s, {}, u"x + 1"_s);
        QVERIFY(d.get(a)->error.contains(u"circulaire"_s));
        QVERIFY(d.get(b)->error.contains(u"circulaire"_s));
        QVERIFY(d.get(c)->error.isEmpty());
        const QId s = *d.addCalculated(u"s"_s, {}, u"s + 1"_s);
        QVERIFY(!d.get(s)->error.isEmpty());
    }

    void renameAndValues()
    {
        Document d;
        const QId t = *d.addExperimental(u"t"_s, u"s"_s, {0, 1, 2, 3});
        const QId x = *d.addExperimental(u"x"_s, u"m"_s, {0, 1, 4, 9});
        const QId v = *d.addCalculated(u"v"_s, {}, u"deriv(x, t) + x*0"_s);
        QVERIFY(d.renameQuantity(x, u"pos"_s));
        QCOMPARE(*d.get(v)->formula, u"deriv(pos, t) + pos*0"_s);
        QVERIFY(d.get(v)->error.isEmpty());
        QVERIFY(d.renameQuantity(x, u"t"_s).error.contains(u"déjà"_s));
        QVERIFY(d.renameQuantity(x, u"sin"_s).error.contains(u"réservé"_s));
        QVERIFY(!d.renameQuantity(x, u"2a"_s));
        d.setValue(t, 6, 6.0);
        QCOMPARE(d.values(t).size(), size_t(7));
        QVERIFY(std::isnan(d.values(t)[5]));
        d.setValue(t, 6, std::nullopt);
        QCOMPARE(d.values(t).size(), size_t(4));
        const QId c = *d.addCalculated(u"c"_s, {}, u"2*t"_s);
        QVERIFY(!d.setValue(c, 0, 1.0).isEmpty());
        d.deleteRows({t}, {0, 1});
        QCOMPARE(d.values(t), (std::vector<double>{2, 3}));
        QCOMPARE(d.uniqueName(u"x"_s), u"x"_s);
        QCOMPARE(d.uniqueName(u"t"_s), u"t2"_s);
    }

    void uncertaintyBroadcast()
    {
        Document d;
        d.addExperimental(u"t"_s, u"s"_s, {0, 1, 2, 3});
        const QId x = *d.addExperimental(u"x"_s, u"m"_s, {0, 1, 4, 9});
        const QId u = *d.createUncertainty(x, u"0.05"_s);
        QCOMPARE(d.get(u)->name, u"u_x"_s);
        QCOMPARE(d.values(u), std::vector<double>(4, 0.05));
        QCOMPARE(d.get(x)->uncertainty, std::optional<QId>(u));
        QCOMPARE(d.uncertaintyOwner(u)->id, x);
        d.removeQuantity(u);
        QVERIFY(!d.get(x)->uncertainty);
    }

    void workspaceUsagesAndRename()
    {
        Workspace ws;
        const QId t = *ws.doc.addExperimental(u"t"_s, u"s"_s, {0, 1, 2});
        const QId x = *ws.doc.addExperimental(u"x"_s, u"m"_s, {0, 2, 4});
        const QId v = *ws.doc.addCalculated(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        ws.addTable({t, x, v});
        const ViewId g = ws.addGraph();
        Plot &p = ws.graphMut(g)->plots[0];
        p.addCurve(t, x);
        fit::ModelKind user;
        user.type = fit::ModelKind::User;
        user.expression = u"a*t + b*x*0"_s;
        p.fits.push_back(FitSpec::make(user, t, x, PALETTE[1]));
        const QId u = *ws.doc.createUncertainty(x, u"0.1"_s);
        QStringList usages;
        for (const auto &us : ws.usages(x))
            usages << us.text();
        QVERIFY(usages.contains(u"tableur « Tableur 1 »"_s));
        QVERIFY(usages.contains(u"graphe « Graphe 1 »"_s));
        QVERIFY(usages.contains(u"formule de « v »"_s));
        QVERIFY(usages.contains(u"incertitude rattachée « u_x »"_s));
        QVERIFY(std::any_of(usages.begin(), usages.end(), [](const QString &s) { return s.startsWith(u"modèle utilisateur"_s); }));
        QCOMPARE(ws.usages(u).size(), size_t(1));
        QVERIFY(ws.renameQuantity(x, u"pos"_s).isEmpty());
        QCOMPARE(*ws.doc.get(v)->formula, u"deriv(pos, t)"_s);
        QCOMPARE(ws.graphs[0].plots[0].fits[0].kind.expression, u"a*t + b*pos*0"_s);
        ws.deleteQuantity(x);
        QVERIFY(std::find(ws.tables[0].columns.begin(), ws.tables[0].columns.end(), x) == ws.tables[0].columns.end());
        QVERIFY(ws.graphs[0].plots[0].curves.empty());
        QVERIFY(!ws.doc.get(v)->error.isEmpty());
    }

    void phyRoundtrip()
    {
        Workspace ws;
        const QId t = *ws.doc.addExperimental(u"t"_s, u"s"_s, {0, 1, 2});
        const QId x = *ws.doc.addExperimental(u"x"_s, u"m"_s, {0, 2, 4});
        const QId v = *ws.doc.addCalculated(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        ws.addTable({t, x, v});
        const ViewId g = ws.addGraph();
        CurveSpec &c = ws.graphMut(g)->plots[0].addCurve(t, x);
        c.errors = ErrorBars::Both;
        c.capSize = 4.0;
        ws.graphMut(g)->plots[0].x.label = u"temps"_s;
        ws.graphMut(g)->plots[0].y.format = NumFormat{NumFormat::Fixed, 2};
        ws.doc.setValue(t, 1, std::nullopt);
        ws.meta.title = u"Chute libre"_s;
        const QString text = phy::save(ws, QJsonObject{{u"dock"_s, 1}});
        QString err;
        auto loaded = phy::load(text, &err);
        QVERIFY2(loaded, qPrintable(err));
        QCOMPARE(loaded->layout.toObject().value(u"dock"_s).toInt(), 1);
        QCOMPARE(loaded->ws.meta.title, u"Chute libre"_s);
        QVERIFY(std::isnan(loaded->ws.doc.values(t)[1]));
        QCOMPARE(*loaded->ws.doc.get(v)->formula, u"deriv(x, t)"_s);
        QCOMPARE(loaded->ws.graphs, ws.graphs);
        QCOMPARE(loaded->ws.tables, ws.tables);
        // nouvelle vue après rechargement : identifiant libre
        Workspace w3 = loaded->ws;
        const ViewId id = w3.addGraph();
        QCOMPARE(std::count_if(w3.graphs.begin(), w3.graphs.end(), [&](const GraphView &gv) { return gv.id == id; }), 1);
        const QId n = *w3.doc.addExperimental(u"n"_s, {}, {});
        QVERIFY(n > v);
    }

    void phyFromMeVRegVersionOne()
    {
        // fichier écrit par MeVReg (format 1)
        const QString v1 = uR"({
          "format": "mevreg-phy", "version": 1, "app_version": "0.1.0",
          "workspace": {
            "doc": {
              "quantities": [
                {"id": 1, "name": "t", "unit": "s", "values": [0, 1, null, 3], "kind": {"type": "Experimental"}},
                {"id": 2, "name": "y", "unit": "", "values": [], "kind": {"type": "Calculated", "formula": "2*t"}}
              ],
              "parameters": [], "next_id": 3
            },
            "tables": [{"id": 1, "name": "Tableur 1", "columns": [1, 2]}],
            "graphs": [{"id": 2, "name": "G", "rows": 1, "cols": 1, "link_x": false, "plots": [{
              "title": {"text": "T", "visible": true, "size": 15.0, "align": "Center", "bold": true},
              "x": {"label": null, "unit": null, "auto": true, "min": 0.0, "max": 1.0, "log": false, "include_zero": false,
                    "major_step": null, "minor_divisions": null, "grid_major": true, "grid_minor": false,
                    "format": {"Fixed": {"decimals": 2}}, "label_size": 13.0, "tick_size": 11.5},
              "curves": [{"x": 1, "y": 2, "side": "Right", "color": [31, 119, 180, 255], "marker": "Disc", "marker_size": 6.0,
                          "line": "Monotone", "line_width": 1.5, "dash": "Dashed", "errors": "Vertical", "label": null,
                          "in_legend": true, "visible": true, "bar_width": null}],
              "legend": {"visible": true, "pos": "TopLeft", "size": 11.5},
              "annotations": [{"kind": {"type": "VLine", "x": 2.5}, "color": [90, 90, 90, 255], "side": "Left", "label": "E", "dash": "Dashed"}],
              "tangents": [],
              "fits": [{"kind": {"type": "Polynomial", "degree": 2}, "x": 1, "y": 2, "range": [0.0, 3.0], "fixed": [["a0", 1.0]],
                        "initial": [], "color": [214, 39, 40, 255], "visible": true, "side": "Left"}]
            }]}]
          },
          "layout": {"surfaces": []}
        })"_s;
        QString err;
        auto l = phy::load(v1, &err);
        QVERIFY2(l, qPrintable(err));
        QCOMPARE(l->ws.doc.values(2)[3], 6.0);
        const Plot &p = l->ws.graphs[0].plots[0];
        QCOMPARE(p.x.format.kind, NumFormat::Fixed);
        QCOMPARE(p.x.format.decimals, 2);
        QCOMPARE(p.curves[0].side, YSide::Right);
        QCOMPARE(p.curves[0].marker, Marker::Disc);
        QCOMPARE(p.curves[0].line, LineKind::Monotone);
        QCOMPARE(p.curves[0].dash, Dash::Dashed);
        QCOMPARE(p.curves[0].errors, ErrorBars::Vertical);
        QCOMPARE(p.legend.pos, LegendPos::TopLeft);
        QCOMPARE(p.annotations[0].x, 2.5);
        QCOMPARE(p.annotations[0].label, u"E"_s);
        QCOMPARE(p.fits[0].kind.type, fit::ModelKind::Polynomial);
        QCOMPARE(p.fits[0].kind.degree, 2);
        QCOMPARE(p.fits[0].range->second, 3.0);
        QCOMPARE(p.fits[0].fixed[0].first, u"a0"_s);
        QVERIFY(!phy::load(u"{}"_s, &err));
        QVERIFY(!phy::load(uR"({"format":"mevreg-phy","version":99,"workspace":{}})"_s, &err));
        QVERIFY(err.contains(u"plus récente"_s));
    }

    void pythonIdentifierUse()
    {
        QVERIFY(pythonUses(u"v = np.gradient(x, t)"_s, u"x"_s));
        QVERIFY(!pythonUses(u"np.x = 1  # x"_s, u"x"_s));
        QVERIFY(!pythonUses(u"print('x')"_s, u"x"_s));
    }

    // --- CSV et copier-coller ---
    void csvFrench()
    {
        QString err;
        const auto p = csv::parse(u"t (s);x (m);commentaire\n0;0,5;\n0,1;0,72;a\n0,2;;\n"_s, &err);
        QVERIFY(p);
        QCOMPARE(p->columns.size(), size_t(2));
        QCOMPARE(p->columns[0].name, u"t"_s);
        QCOMPARE(p->columns[0].unit, u"s"_s);
        QCOMPARE(p->columns[0].values, (std::vector<double>{0, 0.1, 0.2}));
        QCOMPARE(p->columns[1].values[1], 0.72);
        QCOMPARE(p->columns[1].values.size(), size_t(2));
        QCOMPARE(p->ignored, 1);
        const auto q = csv::parse(u"time,voltage\ns,V\n0.0,1.5\n0.5,2.5\n"_s, &err);
        QCOMPARE(q->columns[0].unit, u"s"_s);
        QCOMPARE(q->columns[1].name, u"voltage"_s);
        QCOMPARE(q->columns[1].values, (std::vector<double>{1.5, 2.5}));
        const auto r = csv::parse(u"1 2\n3 4\n"_s, &err);
        QCOMPARE(r->columns[0].name, u"c1"_s);
        QCOMPARE(r->columns[1].values, (std::vector<double>{2, 4}));
        QVERIFY(!csv::parse(u""_s, &err));
        QVERIFY(!csv::parse(u"a;b\nc;d"_s, &err));
        QCOMPARE(csv::sanitizeName(u"Tension U"_s, u"c1"_s), u"Tension_U"_s);
        QCOMPARE(csv::sanitizeName(u"2t"_s, u"c1"_s), u"g_2t"_s);
        QCOMPARE(csv::sanitizeName(u"***"_s, u"c4"_s), u"c4"_s);
        QCOMPARE(csv::splitHeader(u"U [V]"_s), std::make_pair(u"U"_s, u"V"_s));
        QCOMPARE(csv::splitHeader(u"x / m"_s), std::make_pair(u"x"_s, u"m"_s));
    }

    void csvDetectAndRead()
    {
        using csv::Decimal;
        // séparateurs devinés d'après le nombre de cases des lignes
        auto sepOf = [](const char *text) { return csv::detect(csv::textLines(QString::fromUtf8(text))).separator; };
        QCOMPARE(sepOf("t\tx\n0,5\t1\n"), QChar(u'\t'));
        QCOMPARE(sepOf("t (s);x (m)\n0,5;1\n0,6;2\n"), QChar(u';'));
        QCOMPARE(sepOf("t,x\n0.5,1\n0.6,2\n"), QChar(u','));
        // des nombres à virgule séparés par des espaces : la virgule n'est pas le séparateur
        QCOMPARE(sepOf("0,5 1,2\n0,6 1,3\n"), csv::SPACES);
        // séparateur décimal : celui des nombres du fichier ; avec la virgule entre les valeurs, le point
        QCOMPARE(csv::detect(csv::textLines(u"a;b\n1.5;2.5\n"_s)).decimal, Decimal::Point);
        QCOMPARE(csv::detect(csv::textLines(u"a;b\n1,5;2,5\n"_s)).decimal, Decimal::Comma);
        QCOMPARE(csv::detect(csv::textLines(u"a;b\n1;2\n"_s), Decimal::Point).decimal, Decimal::Point);
        QCOMPARE(csv::detect(csv::textLines(u"a,b\n1,2\n"_s)).decimal, Decimal::Point);
        // l'autre signe rend la case illisible plutôt que d'être deviné
        QCOMPARE(csv::number(u"1,5"_s, Decimal::Comma), std::optional<double>(1.5));
        QVERIFY(!csv::number(u"1.5"_s, Decimal::Comma));
        QVERIFY(!csv::number(u"1,234"_s, Decimal::Point));
        QCOMPARE(csv::number(u"1 234,5"_s, Decimal::Comma), std::optional<double>(1234.5));
        // guillemets : un séparateur entre guillemets fait partie de la case
        QCOMPARE(csv::splitLine(u"\"a;b\";\"c\"\"d\"; e "_s, u';'), (QStringList{u"a;b"_s, u"c\"d"_s, u"e"_s}));
        // fins de ligne des anciens Mac, marque d'ordre des octets, lignes vides de la fin
        QCOMPARE(csv::textLines(QString(QChar(0xfeff)) + u"a\r1\r\r"_s), (QStringList{u"a"_s, u"1"_s}));

        // titre, noms, unités sur leur ligne, ligne vide, case illisible, ligne d'un autre nombre de cases
        const QStringList lines = csv::textLines(u"Pendule\ntemps;angle\ns;°\n0;1,5\n;\n0,1;x\n0,2;1,2;9\n"_s);
        csv::ReadOptions o = csv::detect(lines);
        QCOMPARE(o.nameLine, 1);
        QCOMPARE(o.unitLine, 2);
        QCOMPARE(o.firstLine, 3);
        csv::Reading r = csv::read(lines, o);
        QCOMPARE(r.columns.size(), size_t(3));
        QCOMPARE(r.columns[0].name, u"temps"_s);
        QCOMPARE(r.columns[1].unit, u"°"_s);
        QCOMPARE(r.columns[0].values, (std::vector<double>{0, 0.1, 0.2}));
        QCOMPARE(r.skipped, 1);
        QCOMPARE(r.badCells, 1);
        QCOMPARE(r.firstBad.line, 5);
        QCOMPARE(r.firstBad.text, u"x"_s);
        QCOMPARE(r.usualWidth, 2);
        QCOMPARE(r.ragged, 1);
        QCOMPARE(r.firstRagged, 6);
        QCOMPARE(r.rows, (std::vector<int>{3, 5, 6}));
        // les lignes vides gardées : une rangée de cases vides
        o.skipEmpty = false;
        r = csv::read(lines, o);
        QCOMPARE(r.rows.size(), size_t(4));
        QVERIFY(std::isnan(r.columns[0].values[1]));
        // sans ligne des noms : c1, c2…
        o.nameLine = o.unitLine = -1;
        QCOMPARE(csv::read(lines, o).columns[1].name, u"c2"_s);
        // mauvais séparateur décimal : les cases sont signalées, et celles que l'autre lirait
        o = csv::detect(lines);
        o.decimal = Decimal::Point;
        r = csv::read(lines, o);
        QCOMPARE(r.badCells, 5);
        QCOMPARE(r.otherDecimal, 4);
    }

    void csvExportRoundtrip()
    {
        Document doc;
        const QId t = *doc.addExperimental(u"t"_s, u"s"_s, {0, 0.1, NAN, 0.3});
        const QId u = *doc.addExperimental(u"U"_s, u"V"_s, {1.25, 2.0});
        const QString s = csv::exportText(doc, {t, u}, csv::FRENCH);
        QVERIFY(s.startsWith(u"t (s);U (V)\r\n0;1,25\r\n0,1;2\r\n;\r\n0,3;\r\n"_s));
        QString err;
        const auto p = csv::parse(s, &err);
        const auto ids = csv::import(doc, *p);
        QCOMPARE(doc.name(ids[0]), u"t2"_s);
        QCOMPARE(doc.values(ids[0])[3], 0.3);
        QVERIFY(std::isnan(doc.values(ids[0])[2]));
    }

    // --- export vers Python ---
    void numpyExpressions()
    {
        auto np = [](const QString &src) { return pyexport::toNumpy(expr::parse(src).expr->root()); };
        QCOMPARE(np(u"0.5*m*v^2"_s), u"0.5*m*v**2"_s);
        QCOMPARE(np(u"deriv(x, t)"_s), u"np.gradient(x, t)"_s);
        QCOMPARE(np(u"ln(a) + log(b)"_s), u"np.log(a) + np.log10(b)"_s);
        QCOMPARE(np(u"a - (b - c)"_s), u"a - (b - c)"_s);
        QCOMPARE(np(u"(a + b)*c"_s), u"(a + b)*c"_s);
        QCOMPARE(np(u"-x^2"_s), u"-x**2"_s);
        QCOMPARE(np(u"(-x)^2"_s), u"(-x)**2"_s);
        QCOMPARE(np(u"2^3^2"_s), u"2**3**2"_s);
        QCOMPARE(np(u"(2^3)^2"_s), u"(2**3)**2"_s);
        QCOMPARE(np(u"a*x + (-2)"_s), u"a*x - 2"_s);
        QCOMPARE(np(u"a - (-b)"_s), u"a + b"_s);
        QCOMPARE(np(u"a/(b*c)"_s), u"a/(b*c)"_s);
        QCOMPARE(np(u"sin(2*pi*f*t)"_s), u"np.sin(2*np.pi*f*t)"_s);
        QCOMPARE(np(u"max(x)"_s), u"np.nanmax(x)"_s);
        QCOMPARE(pyexport::pyName(u"lambda"_s), u"lambda_"_s);
    }

    void fullPythonScript()
    {
        Workspace ws;
        const QId t = *ws.doc.addExperimental(u"t"_s, u"s"_s, {0, 0.1, 0.2, NAN});
        const QId x = *ws.doc.addExperimental(u"x"_s, u"m"_s, {0, 0.05, 0.2, 0.45});
        ws.doc.addCalculated(u"Ec"_s, u"J"_s, u"0.5*m*v^2"_s);
        const QId v = *ws.doc.addCalculated(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        ws.doc.addParameter(u"m"_s, u"kg"_s, 0.1);
        ws.doc.createUncertainty(x, u"0.01"_s);
        const ViewId g = ws.addGraph();
        Plot &p = ws.graphMut(g)->plots[0];
        p.addCurve(t, x).errors = ErrorBars::Vertical;
        p.addCurve(t, v).side = YSide::Right;
        fit::ModelKind parabola;
        parabola.type = fit::ModelKind::Parabola;
        p.fits.push_back(FitSpec::make(parabola, t, x, PALETTE[3]));
        updateFits(ws.graphMut(g)->plots[0], ws.doc);
        const QString s = pyexport::exportScript(ws);
        // relancé dans Capytale, le script repart d'une figure vide (sinon il redessine par-dessus)
        QVERIFY(s.contains(u"plt.clf()"_s) && s.indexOf(u"plt.clf()"_s) < s.indexOf(u"plt.figure("_s));
        QVERIFY(s.contains(u"import numpy as np"_s));
        QVERIFY2(s.contains(u"t = np.array([0.0, 0.1, 0.2, np.nan])"_s), qPrintable(s));
        QVERIFY(s.indexOf(u"v = np.gradient(x, t)"_s) < s.indexOf(u"Ec = 0.5*m*v**2"_s));
        QVERIFY(s.contains(u"ax.errorbar(t, x, yerr=u_x"_s));
        QVERIFY(s.contains(u"ax2 = ax.twinx()"_s));
        QVERIFY(s.contains(u"xm = np.linspace("_s));
        QVERIFY(s.contains(u"plt.show()"_s));
    }

    // --- annotations, incertitudes côté par côté, étendue et nom des modèles ---
    void annotationsSidesAndModelDrawing()
    {
        Document doc;
        const QId t = *doc.addExperimental(u"t"_s, u"s"_s, {0, 1, 2, 3, 4});
        const QId x = *doc.addExperimental(u"x"_s, u"m"_s, {1, 3, 5, 7, 9});
        const QId up = *doc.addExperimental(u"haut"_s, u"m"_s, {0.5});
        const QId down = *doc.addExperimental(u"bas"_s, u"m"_s, {0.2});
        Plot plot;
        CurveSpec &c = plot.addCurve(t, x);
        c.errors = ErrorBars::Rectangles;
        c.errYHigh = up;
        c.errYLow = down;
        Annotation line;
        line.kind = Annotation::Line;
        line.x = 0;
        line.y = 1;
        line.x2 = 2;
        line.y2 = 5;
        line.locked = true;
        plot.annotations.push_back(line);
        Annotation text;
        text.kind = Annotation::Text;
        text.text = u"repère"_s;
        text.size = 18;
        plot.annotations.push_back(text);
        fit::ModelKind affine;
        affine.type = fit::ModelKind::Affine;
        FitSpec f = FitSpec::make(affine, t, x, PALETTE[3]);
        f.label = u"droite d'étalonnage"_s;
        f.draw = FitDraw::Custom;
        f.drawRange = {-2, 10};
        plot.fits.push_back(f);
        // enregistrées et relues telles quelles
        const Plot back = plotFromJson(plotToJson(plot));
        QVERIFY(back == plot);
        QCOMPARE(back.curves[0].errYHigh, std::optional<QId>(up));
        QVERIFY(!back.curves[0].errXLow);
        QCOMPARE(back.annotations[0].kind, Annotation::Line);
        QVERIFY(back.annotations[0].locked);
        QCOMPARE(back.annotations[1].size, 18.0);
        QCOMPARE(back.fits[0].draw, FitDraw::Custom);
        // une courbe retirée emporte sa méthode des tangentes et ses modèles
        {
            Plot q = plot;
            TangentSpec ts;
            ts.x = t;
            ts.y = x;
            q.tangents.push_back(ts);
            q.removeCurve(0);
            QVERIFY(q.curves.empty() && q.tangents.empty() && q.fits.empty());
        }
        // grandeur d'une incertitude utilisée par le graphe ; supprimée : la courbe reste
        QVERIFY(plot.uses(up));
        Plot removed = plot;
        removed.removeQuantity(up);
        QCOMPARE(removed.curves.size(), size_t(1));
        QVERIFY(!removed.curves[0].errYHigh && removed.curves[0].errYLow);
        // valeurs à tracer : incertitudes de chaque côté, droite par deux points, fonction du modèle
        updateFits(plot, doc);
        const PlotData data = resolve(plot, doc, NumberStyle{});
        QCOMPARE(data.series[0].uyHigh, std::vector<double>{0.5});
        QCOMPARE(data.series[0].uyLow, std::vector<double>{0.2});
        QVERIFY(data.series[0].uxLow.empty());
        QCOMPARE(data.overlays.size(), size_t(1));
        QCOMPARE(data.overlays[0].kind, Overlay::Line);
        QCOMPARE(data.overlays[0].a, 2.0);
        QCOMPARE(data.overlays[0].b, 1.0);
        QCOMPARE(data.labels.front().size, 18.0);
        const Series &model = data.series.back();
        QCOMPARE(*model.legend, u"droite d'étalonnage"_s);
        QVERIFY(model.function);
        QCOMPARE(model.domain, (std::optional<std::pair<double, double>>(std::pair(-2.0, 10.0))));
        // la fonction se calcule hors des points (x = −2 et x = 10)
        const std::vector<double> y = model.function({-2.0, 10.0});
        QVERIFY(std::abs(y[0] - (-3.0)) < 1e-6 && std::abs(y[1] - 21.0) < 1e-6);
        // sur les seuls points : pas de fonction
        plot.fits[0].draw = FitDraw::Points;
        updateFits(plot, doc);
        QVERIFY(!resolve(plot, doc, NumberStyle{}).series.back().function);
    }

    // --- valeurs à tracer ---
    void resolveCurvesAndFits()
    {
        Document doc;
        const QId t = *doc.addExperimental(u"t"_s, u"s"_s, {0, 1, 2, 3, 4});
        const QId x = *doc.addExperimental(u"x"_s, u"m"_s, {1, 3.1, 4.9, 7, 9});
        doc.createUncertainty(x, u"0.2"_s);
        Plot plot;
        plot.addCurve(t, x).errors = ErrorBars::Vertical;
        fit::ModelKind affine;
        affine.type = fit::ModelKind::Affine;
        plot.fits.push_back(FitSpec::make(affine, t, x, PALETTE[3]));
        updateFits(plot, doc);
        QVERIFY(plot.fits[0].result);
        QVERIFY(std::abs(plot.fits[0].result->params[0] - 2.0) < 0.05);
        QCOMPARE(plot.fits[0].paramNames, (QStringList{u"a"_s, u"b"_s}));
        const PlotData data = resolve(plot, doc, NumberStyle{});
        QCOMPARE(data.series.size(), size_t(2));
        QCOMPARE(data.series[0].uy, std::vector<double>(5, 0.2));
        QCOMPARE(data.xName, u"t"_s);
        QCOMPARE(data.xUnit, u"s"_s);
        QVERIFY(!data.series[1].autoscale);
        // pas de nouvel ajustement si rien n'a changé ; plage de modélisation
        const auto rev = plot.fits[0].fittedRevision;
        updateFits(plot, doc);
        QCOMPARE(plot.fits[0].fittedRevision, rev);
        plot.fits[0].range = std::make_pair(2.0, 4.0);
        updateFits(plot, doc);
        QCOMPARE(plot.fits[0].result->n, size_t(3));
        // grandeurs absentes : ignorées
        Plot empty;
        empty.addCurve(10, 11);
        empty.fits.push_back(FitSpec::make(affine, 10, 11, PALETTE[1]));
        updateFits(empty, Document{});
        QVERIFY(!empty.fits[0].result);
        QVERIFY(resolve(empty, Document{}, NumberStyle{}).series.empty());
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
