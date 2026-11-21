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

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>

using namespace Qt::StringLiterals;
using std::numbers::pi;

namespace {

std::vector<double> espace_lineaire(double a, double b, int n)
{
    std::vector<double> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = a + (b - a) * i / (n - 1);
    return v;
}

double scalaire(const QString &src)
{
    auto v = expr::evalue_texte(src, {});
    return v && v->scalar ? v->s : NAN;
}

QString erreur_evaluation(const QString &src)
{
    expr::Error e;
    expr::evalue_texte(src, {}, &e);
    return e.message;
}

/// Exécute le script tel qu'un élève le reçoit. Le petit module de tracé des
/// tests enregistre les points, ce qui permet aussi de contrôler les courbes.
void verifie_export(const QString &script, const QString &verifications)
{
    QTemporaryDir dossier;
    QVERIFY(dossier.isValid());
    QFile fichier(dossier.filePath(u"seance.py"_s));
    QVERIFY(fichier.open(QIODevice::WriteOnly));
    const auto texte = (script + u'\n' + verifications).toUtf8();
    QCOMPARE(fichier.write(texte), texte.size());
    fichier.close();
    QProcess calcul;
    calcul.start(QString::fromUtf8(MEVREG_TEST_PYTHON), {u"-I"_s, QStringLiteral(TESTS_DIR "/capytale.py"), fichier.fileName()});
    QVERIFY(calcul.waitForFinished(30000));
    QCOMPARE(calcul.exitStatus(), QProcess::NormalExit);
    QVERIFY2(calcul.exitCode() == 0, calcul.readAllStandardError().constData());
}

constexpr NumberStyle DOT{false};
constexpr NumberStyle COMMA{true};

/// Générateur pseudo-aléatoire reproductible et bruit gaussien.
struct Rng {
    std::uint64_t s;
    double suivant()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<double>(s >> 11) / static_cast<double>(1ull << 53);
    }
    double gauss()
    {
        const double u1 = std::max(suivant(), 1e-300), u2 = suivant();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * pi * u2);
    }
};

} // namespace

class TestCore : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- nombres et unités ---
    void lecture_nombres()
    {
        QCOMPARE(lit_nombre(u"1,5"_s), 1.5);
        QCOMPARE(lit_nombre(u" -2.5e-3 "_s), -2.5e-3);
        QCOMPARE(lit_nombre(u"1 000,5"_s), 1000.5);
        QCOMPARE(lit_nombre(u"3E2"_s), 300.0);
        QVERIFY(!lit_nombre(u""_s));
        QVERIFY(!lit_nombre(u"abc"_s));
        QVERIFY(!lit_nombre(u"nan"_s));
        QCOMPARE(lit_nombre(u"−4"_s), -4.0);
    }

    void chiffres_significatifs()
    {
        QCOMPARE(format_significatif(1.23456789, 4, DOT), u"1.235"_s);
        QCOMPARE(format_significatif(1.5, 6, COMMA), u"1,5"_s);
        QCOMPARE(format_significatif(0.000123, 3, DOT), u"0.000123"_s);
        QCOMPARE(format_significatif(1.23e-7, 3, DOT), u"1.23e-7"_s);
        QCOMPARE(format_significatif(100.0, 6, DOT), u"100"_s);
        QCOMPARE(format_significatif(-0.5, 6, DOT), u"-0.5"_s);
        QCOMPARE(format_significatif(NAN, 6, DOT), QString());
        QCOMPARE(format_scientifique(1.5e-4, 2, COMMA), u"1,50×10⁻⁴"_s);
        QCOMPARE(format_scientifique(2.0, 1, DOT), u"2.0"_s);
        QCOMPARE(format_fixe(-0.0001, 2, DOT), u"0.00"_s);
        QCOMPARE(repr_python_reel(2.0), u"2.0"_s);
        QCOMPARE(repr_python_reel(0.1), u"0.1"_s);
        QCOMPARE(repr_python_reel(1e-5), u"1e-05"_s);
        QCOMPARE(repr_python_reel(1.5e20), u"1.5e+20"_s);
    }

    void unites()
    {
        QCOMPARE(etiquette_avec_unite(u"Tension (V)"_s, u"V"_s), u"Tension (V)"_s);
        QCOMPARE(etiquette_avec_unite(u"Tension (V)"_s, u"V"_s, -3), u"Tension (×10⁻³ V)"_s);
        QCOMPARE(unite_lisible(u"m.s-1"_s), u"m·s⁻¹"_s);
        QCOMPARE(unite_lisible(u"m/s^2"_s), u"m/s²"_s);
        QCOMPARE(unite_lisible(u"kg.m^-3"_s), u"kg·m⁻³"_s);
        QCOMPARE(unite_lisible(u"m2"_s), u"m²"_s);
        QCOMPARE(unite_lisible(u"mol.L-1"_s), u"mol·L⁻¹"_s);
        QCOMPARE(unite_lisible(u"V"_s), u"V"_s);
        QCOMPARE(unite_lisible(u"°C"_s), u"°C"_s);
        QCOMPARE(unite_lisible(u"10^3 Pa"_s), u"10³ Pa"_s);
    }

    void mesure_arrondie()
    {
        auto r = arrondit_mesure(9.81234, 0.0234, 2, COMMA);
        QCOMPARE(r.value, u"9,812"_s);
        QCOMPARE(r.uncertainty, u"0,023"_s);
        QCOMPARE(r.en_texte(u"m/s^2"_s), u"(9,812 ± 0,023) m/s²"_s);
        r = arrondit_mesure(9.81234, 0.0996, 2, DOT);
        QCOMPARE(r.value, u"9.81"_s);
        QCOMPARE(r.uncertainty, u"0.10"_s);
        r = arrondit_mesure(1.2346e-5, 2.1e-7, 2, DOT);
        QCOMPARE(r.exponent, -6);
        QCOMPARE(r.value, u"12.35"_s);
        QCOMPARE(r.uncertainty, u"0.21"_s);
        r = arrondit_mesure(1523.0, 12.0, 1, DOT);
        QCOMPARE(r.value, u"1520"_s);
        QCOMPARE(r.uncertainty, u"10"_s);
    }

    // --- formules ---
    void arithmetique()
    {
        QCOMPARE(scalaire(u"1+2*3"_s), 7.0);
        QCOMPARE(scalaire(u"(1+2)*3"_s), 9.0);
        QCOMPARE(scalaire(u"2^3^2"_s), 512.0);
        QCOMPARE(scalaire(u"2**3"_s), 8.0);
        QCOMPARE(scalaire(u"-2^2"_s), -4.0);
        QCOMPARE(scalaire(u"2^-1"_s), 0.5);
        QCOMPARE(scalaire(u"10/4"_s), 2.5);
        QCOMPARE(scalaire(u"1e3 + 2.5E-1"_s), 1000.25);
        QCOMPARE(scalaire(u"3²"_s), 9.0);
        QCOMPARE(scalaire(u"2 × 3 − 1"_s), 5.0);
        QVERIFY(std::abs(scalaire(u"sin(pi/2)"_s) - 1.0) < 1e-15);
        QCOMPARE(scalaire(u"log(1000)"_s), 3.0);
        QVERIFY(std::abs(scalaire(u"ln(e)"_s) - 1.0) < 1e-15);
        QCOMPARE(scalaire(u"max(2, 5)"_s), 5.0);
        QVERIFY(std::isnan(scalaire(u"(-8)^(1/3)"_s)));
        QCOMPARE(scalaire(u"(-2)^3"_s), -8.0);
    }

    void vecteurs()
    {
        auto env = [](const QString &n) -> std::optional<expr::Value> {
            if (n == u"v")
                return expr::Value::vecteur({1, 2, 3});
            if (n == u"m")
                return expr::Value::nombre(2);
            if (n == u"b")
                return expr::Value::vecteur({10, 20});
            if (n == u"c")
                return expr::Value::vecteur({10});
            return std::nullopt;
        };
        QCOMPARE(expr::evalue_texte(u"0.5*m*v^2"_s, env)->v, (std::vector<double>{1, 4, 9}));
        QCOMPARE(expr::evalue_texte(u"mean(v)"_s, env)->s, 2.0);
        QCOMPARE(expr::evalue_texte(u"len(v)"_s, env)->s, 3.0);
        const auto r = expr::evalue_texte(u"v+b"_s, env)->v;
        QCOMPARE(r[0], 11.0);
        QCOMPARE(r[1], 22.0);
        QVERIFY(std::isnan(r[2]));
        QCOMPARE(expr::evalue_texte(u"v*c"_s, env)->v, (std::vector<double>{10, 20, 30}));
    }

    void erreurs_formules()
    {
        QVERIFY(erreur_evaluation(u"x+1"_s).contains(u"« x »"_s));
        QVERIFY(expr::analyse(u"2x"_s).error.message.contains(u"opérateur manquant"_s));
        QVERIFY(expr::analyse(u"0,5*m"_s).error.message.contains(u"point"_s));
        QVERIFY(expr::analyse(u"(1+2"_s).error.message.contains(u"parenthèse"_s));
        QVERIFY(!expr::analyse(u"1+"_s).ok());
        QVERIFY(!expr::analyse(u""_s).ok());
        QVERIFY(erreur_evaluation(u"foo(2)"_s).contains(u"inconnue"_s));
        QVERIFY(erreur_evaluation(u"sin(1, 2)"_s).contains(u"1 argument"_s));
    }

    void renommage()
    {
        QCOMPARE(expr::renomme_identifiant(u"0.5*m*v^2 + v"_s, u"v"_s, u"vitesse"_s), u"0.5*m*vitesse^2 + vitesse"_s);
        QCOMPARE(expr::renomme_identifiant(u"deriv(x, t)"_s, u"x"_s, u"pos"_s), u"deriv(pos, t)"_s);
        QCOMPARE(expr::renomme_identifiant(u"vv + v"_s, u"v"_s, u"w"_s), u"vv + w"_s);
        QCOMPARE(expr::renomme_identifiant(u"exp(t)"_s, u"exp"_s, u"z"_s), u"exp(t)"_s);
        QVERIFY(expr::utilise_identifiant(u"a*exp(t)"_s, u"t"_s));
        QVERIFY(!expr::utilise_identifiant(u"a*exp(t)"_s, u"exp"_s));
        QVERIFY(!expr::utilise_identifiant(u"tt"_s, u"t"_s));
        const auto e = expr::analyse(u"A*exp(-t/tau) + B"_s);
        QCOMPARE(e.expr->variables(), (QStringList{u"A"_s, u"t"_s, u"tau"_s, u"B"_s}));
        QVERIFY(expr::est_nom_valide(u"v_x"_s));
        QVERIFY(expr::est_nom_valide(u"Δt"_s));
        QVERIFY(expr::est_nom_valide(u"τ"_s));
        QVERIFY(!expr::est_nom_valide(u"2x"_s));
        QVERIFY(!expr::est_nom_valide(u"u(x)"_s));
        const auto d = expr::decoupe_definition(u"Ec = 0.5*m*v^2"_s);
        QCOMPARE(d->first, u"Ec"_s);
        QCOMPARE(d->second, u"0.5*m*v^2"_s);
    }

    // --- calculs numériques ---
    void gradient_du_sinus()
    {
        const auto t = espace_lineaire(0, 2 * pi, 629);
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

    void integrale()
    {
        const auto t = espace_lineaire(0, pi, 1001);
        std::vector<double> y(t.size());
        for (size_t i = 0; i < t.size(); ++i)
            y[i] = std::sin(t[i]);
        const auto c = numeric::trapezes_cumules(y, t);
        QCOMPARE(c[0], 0.0);
        QVERIFY(std::abs(c[1000] - 2.0) < 1e-5);
        QVERIFY(std::abs(numeric::trapezes(y, t) - 2.0) < 1e-5);
        QVERIFY(std::abs(numeric::aire_entre(y, t, 0.0, pi / 2) - 1.0) < 1e-5);
        QVERIFY(std::abs(numeric::aire_entre({1, 1}, {0, 10}, 2, 5) - 3.0) < 1e-12);
        QCOMPARE(numeric::interpole(0.5, {0, 1, 2}, {0, 10, 0}), 5.0);
        QCOMPARE(numeric::interpole(-1.0, {0, 1, 2}, {0, 10, 0}), 0.0);
        const auto s = numeric::infos_pas({0, 0.1, 0.2, 0.3});
        QVERIFY(std::abs(s->first - 0.1) < 1e-12 && s->second < 1e-9);
    }

    void derivee_integrale_aller_retour()
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
            const auto back = numeric::gradient(numeric::trapezes_cumules(y, t), t);
            const double h = 2.0 / (n - 1);
            for (int i = 1; i + 1 < n; ++i)
                QVERIFY(std::abs(back[i] - y[i]) < 10 * h * h * (1 + std::abs(a) + std::abs(b) + std::abs(c)) + 1e-9);
        }
    }

    void sequences()
    {
        QCOMPARE(series::par_pas(0, 1, 0.1).values, (std::vector<double>{0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0}));
        QCOMPARE(series::par_pas(0, 10, 2.5).values, (std::vector<double>{0, 2.5, 5, 7.5, 10}));
        QCOMPARE(series::par_pas(0, 1, 0.3).values, (std::vector<double>{0, 0.3, 0.6, 0.9}));
        QCOMPARE(series::par_pas(5, 1, -2).values, (std::vector<double>{5, 3, 1}));
        QVERIFY(!series::par_pas(0, 1, 0).ok());
        QVERIFY(!series::par_pas(0, 1, -0.1).ok());
        QVERIFY(!series::par_pas(0, 1e9, 1e-3).ok());
        QCOMPARE(series::par_nombre(0, 1, 5).values, (std::vector<double>{0, 0.25, 0.5, 0.75, 1}));
        QCOMPARE(series::par_nombre(0, 0.6, 7).values, (std::vector<double>{0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6}));
        QCOMPARE(series::prolonge({1, 3}, 3), (std::vector<double>{5, 7, 9}));
        QCOMPARE(series::prolonge({0, 0.1}, 3), (std::vector<double>{0.2, 0.3, 0.4}));
        QCOMPARE(series::prolonge({10, 8, 6}, 2), (std::vector<double>{4, 2}));
        QCOMPARE(series::prolonge({4.2}, 2), (std::vector<double>{4.2, 4.2}));
        QCOMPARE(series::prolonge({1, NAN, 3}, 2), (std::vector<double>{4, 5}));
    }

    void algebre_lineaire()
    {
        linalg::Matrix a{3, 3, {2, 1, -1, -3, -1, 2, -2, 1, 2}};
        const auto x = linalg::resout_systeme(a, {8, -11, -3});
        QVERIFY(x);
        QVERIFY(std::abs((*x)[0] - 2) < 1e-12 && std::abs((*x)[1] - 3) < 1e-12 && std::abs((*x)[2] + 1) < 1e-12);
        QVERIFY(!linalg::resout_systeme({2, 2, {1, 2, 2, 4}}, {1, 2}));
        const auto inv = linalg::inverse_spd({2, 2, {4, 2, 2, 3}});
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
        const auto p = linalg::moindres_carres(m, b);
        QVERIFY(p && std::abs((*p)[2] - 0.5) < 1e-6 && std::abs((*p)[1] + 2.0) < 1e-3);
    }

    void splines()
    {
        const std::vector<double> x{0, 1, 1.5, 3, 4, 6}, y{0, 2, 1, 5, 4, 4.5};
        for (auto kind : {spline::Kind::Cubic, spline::Kind::Monotone}) {
            const auto h = spline::Hermite::fabrique(x, y, kind);
            QVERIFY(h);
            for (size_t i = 0; i < x.size(); ++i)
                QVERIFY(std::abs(h->evalue(x[i]) - y[i]) < 1e-12);
        }
        const auto line = spline::Hermite::fabrique({0, 1, 2, 3}, {1, 3, 5, 7}, spline::Kind::Cubic);
        QVERIFY(std::abs(line->evalue(1.37) - (1 + 2 * 1.37)) < 1e-12);
        const auto nat = spline::Hermite::fabrique({0, 1, 2}, {0, 1, 0}, spline::Kind::Cubic);
        QVERIFY(std::abs(nat->evalue(0.5) - 0.6875) < 1e-12);
        const auto pchip = spline::Hermite::fabrique({0, 1, 2, 4}, {0, 1, 4, 5}, spline::Kind::Monotone);
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
            const auto h = spline::Hermite::fabrique(xs, ys, spline::Kind::Monotone);
            for (size_t s = 0; s + 1 < xs.size(); ++s) {
                const double lo = std::min(ys[s], ys[s + 1]), hi = std::max(ys[s], ys[s + 1]);
                for (int j = 0; j <= 20; ++j) {
                    const double v = h->evalue(xs[s] + (xs[s + 1] - xs[s]) * j / 20.0);
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
        const auto s = spline::courbe_lissee(cx, cy, spline::Kind::Cubic, 8);
        QCOMPARE(s.size(), size_t(12 * 8 + 1));
        for (const auto &[a, b] : s)
            QVERIFY(std::abs(std::hypot(a, b) - 1.0) < 0.03);
    }

    void spectres()
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
        auto s = fft::spectre_amplitude(t, y, {}, &err);
        QVERIFY(s);
        auto [f, a] = peak(*s);
        QVERIFY(std::abs(f - 440.0) < 1e-9);
        QVERIFY(std::abs(a - 2.0) < 1e-9);
        s = fft::spectre_amplitude(t, y, {fft::Window::Hann, 4, 0.01}, &err);
        std::tie(f, a) = peak(*s);
        QVERIFY(std::abs(f - 440.0) < 1e-9);
        QVERIFY(std::abs(a - 2.0) < 0.01);
        // carré de 1 V à 100 Hz : harmoniques impaires 4/(πn)
        std::vector<double> t2(10000), sq(10000);
        for (int i = 0; i < 10000; ++i) {
            t2[i] = i * 1e-5;
            sq[i] = std::sin(2 * pi * 100 * (t2[i] + 0.5e-5)) >= 0 ? 1.0 : -1.0;
        }
        s = fft::spectre_amplitude(t2, sq, {}, &err);
        auto at = [&](double fr) { return s->amplitude[static_cast<size_t>(std::lround(fr / s->freq[1]))]; };
        for (double h : {1.0, 3.0, 5.0, 7.0})
            QVERIFY(std::abs(at(100 * h) - 4 / (pi * h)) / (4 / (pi * h)) < 0.01);
        for (double h : {2.0, 4.0, 6.0})
            QVERIFY(at(100 * h) < 0.01);
        // composante continue, pas irrégulier refusé
        s = fft::spectre_amplitude(espace_lineaire(0, 63, 64), std::vector<double>(64, 3.0), {}, &err);
        QVERIFY(std::abs(s->amplitude[0] - 3.0) < 1e-12);
        QVERIFY(!fft::spectre_amplitude({0, 1, 2, 3.5, 4}, {0, 1, 0, -1, 0}, {}, &err));
        QVERIFY(err.contains(u"pas"_s));
        // fréquence dominante
        std::vector<double> t3(300), y3(300);
        for (int i = 0; i < 300; ++i) {
            t3[i] = i * 0.01;
            y3[i] = 1.5 + std::sin(2 * pi * 2.3 * t3[i] + 0.4);
        }
        QVERIFY(std::abs(*fft::frequence_dominante(t3, y3) - 2.3) < 0.02);
        // transformée exacte sur 4 points, comme numpy
        std::vector<fft::Complex> v{1, 2, 3, 4};
        fft::transforme(v, false);
        QCOMPARE(v[0], fft::Complex(10, 0));
        QCOMPARE(v[1], fft::Complex(-2, 2));
        QCOMPARE(v[2], fft::Complex(-2, 0));
        QCOMPARE(v[3], fft::Complex(-2, -2));
        // Bluestein (taille première) : aller-retour
        std::vector<fft::Complex> w(37);
        for (size_t i = 0; i < w.size(); ++i)
            w[i] = fft::Complex(std::cos(i * 0.3), std::sin(i * 0.7));
        auto ww = w;
        fft::transforme(ww, false);
        fft::transforme(ww, true);
        for (size_t i = 0; i < w.size(); ++i)
            QVERIFY(std::abs(ww[i] / 37.0 - w[i]) < 1e-12);
    }

    void series_incompletes_et_signal_constant()
    {
        const auto derivee = numeric::gradient_numpy({2, 4, 6}, {0, 1});
        QCOMPARE(derivee.size(), size_t(3));
        QVERIFY(std::all_of(derivee.begin(), derivee.end(), [](double v) { return std::isnan(v); }));
        QVERIFY(std::isnan(numeric::interpole(0.5, {0, 1}, {5})));
        QVERIFY(std::isnan(numeric::interpole(NAN, {0, 1}, {5, 7})));
        QVERIFY(std::isnan(numeric::interpole(0, {NAN}, {5})));

        // Les temps égaux conservent l’ordre de saisie. Une grille régulière
        // doit redonner la même interpolation qu'une lecture point par point.
        const auto [tempsTries, mesuresTriees] = numeric::paires_triees({3, 1, 0, 1}, {30, 10, 0, 12});
        const auto [grille, valeurs] = numeric::reechantillonne_uniforme(tempsTries, mesuresTriees, 7);
        QCOMPARE(grille, (numeric::Vec{0, 0.5, 1, 1.5, 2, 2.5, 3}));
        for (size_t i = 0; i < grille.size(); ++i)
            QCOMPARE(valeurs[i], numeric::interpole(grille[i], tempsTries, mesuresTriees));
        const auto pas = numeric::infos_pas({0, NAN, 1, 4});
        QVERIFY(pas);
        QCOMPARE(pas->first, 2.0);
        QCOMPARE(pas->second, 0.5);

        const auto temps = espace_lineaire(0, 63, 64);
        const std::vector<double> constante(64, 0.1);
        QVERIFY(!fft::frequence_dominante(temps, constante));
        const auto spectre = fft::spectre_amplitude(temps, constante, {}, nullptr);
        QVERIFY(spectre);
        QCOMPARE(spectre->amplitude.front(), 0.1);
    }

    void statistiques()
    {
        const auto s = stats::resume({9.81, 9.75, 9.86, 9.74, 9.79});
        QVERIFY(s);
        QCOMPARE(s->effectif, size_t(5));
        QVERIFY(std::abs(s->moyenne - 9.79) < 1e-12);
        QVERIFY(std::abs(s->ecartTypePopulation - 0.04335896677735741) < 1e-12);
        QVERIFY(std::abs(s->ecartTypeEchantillon - 0.04847679857416308) < 1e-12);
        QCOMPARE(s->minimum, 9.74);
        QCOMPARE(s->maximum, 9.86);
        const auto r = s->resultat(COMMA);
        QCOMPARE(r.value, u"9,790"_s);
        QCOMPARE(r.uncertainty, u"0,022"_s);
        QVERIFY(!stats::resume({}));
        QVERIFY(std::isnan(stats::resume({3.0})->ecartTypeEchantillon));
        const auto h = stats::histogramme({1, 2, 2, 3, 4}, 3);
        QCOMPARE(h->effectifs, (std::vector<size_t>{1, 2, 2}));
        QCOMPARE(h->bornes, (std::vector<double>{1, 2, 3, 4}));
        QCOMPARE(stats::classes_par_defaut(100), size_t(8));
    }

    void dispersion_autour_d_une_grande_valeur()
    {
        // Deux lectures séparées de 2 ont un écart-type de population de 1,
        // même lorsque leur moyenne exacte tombe entre deux doubles.
        const auto bilan = stats::resume({1e16, NAN, 1e16 + 2});
        QVERIFY(bilan);
        QCOMPARE(bilan->effectif, size_t(2));
        QCOMPARE(bilan->ecartTypePopulation, 1.0);
        QCOMPARE(bilan->ecartTypeEchantillon, std::sqrt(2.0));
        QCOMPARE(bilan->uA, 1.0);
        QCOMPARE(stats::resume({1e20, 3.0, -1e20})->moyenne, 1.0);
        for (const double valeur : {0.0, 1e20, std::numeric_limits<double>::max()}) {
            const auto classes = stats::histogramme({valeur, NAN, valeur}, 5);
            QVERIFY(classes);
            QCOMPARE(classes->bornes.size(), size_t(6));
            for (size_t i = 1; i < classes->bornes.size(); ++i)
                QVERIFY(classes->bornes[i] > classes->bornes[i - 1]);
            QCOMPARE(std::accumulate(classes->effectifs.begin(), classes->effectifs.end(), size_t{}), size_t(2));
        }
    }

    void tangentes_de_meme_pente_signee()
    {
        std::vector<double> x, y;
        for (int i = 0; i <= 100; ++i) {
            const double t = i / 10.;
            x.push_back(t);
            y.push_back(3 * std::tanh((t - 5) / 1.2) - 0.2 * t);
        }
        QString erreur;
        const auto c = titration::Curve::fabrique(x, y, &erreur);
        QVERIFY2(c, qPrintable(erreur));
        for (double contact : {1.0, 9.0}) {
            const auto t = c->tangentes(contact, &erreur);
            QVERIFY2(t, qPrintable(erreur));
            QVERIFY(t->slope < 0);
            QVERIFY(std::abs(c->pente(t->p1.first) - t->slope) < 1e-9);
            QVERIFY(std::abs(c->pente(t->p2.first) - t->slope) < 1e-9);
        }
    }

    void dosage_sans_saut_et_repere_recalcule()
    {
        const std::vector<double> v{0, 1, 2, 3, 4, 5, 6};
        for (const auto &p : {std::vector<double>(v.size(), 7), v, std::vector<double>{1, 1, 1, 1, 1, 1, 9}}) {
            QString erreur;
            QVERIFY(!titration::maximum_derivee(v, p, &erreur));
            QVERIFY(!erreur.isEmpty());
            QVERIFY(!titration::Curve::fabrique(v, p, &erreur));
        }
        Seance ws;
        auto &doc = ws.doc;
        const QId x = *doc.ajoute_experimentale(u"V"_s, u"mL"_s, v);
        const QId ph = *doc.ajoute_experimentale(u"pH"_s, {}, {1, 2, 6, 10, 11, 11, 11});
        const QId dy = *doc.ajoute_calculee(u"pente"_s, {}, u"deriv(pH, V)"_s);
        const auto g = ws.ajoute_graphe();
        auto &courbe = ws.graphe_modifiable(g)->plots.front().ajoute_courbe(x, dy);
        courbe.dosage = ph;
        const auto premier = volume_equivalence(courbe, doc);
        QVERIFY(premier);
        QCOMPARE(*premier, 2.0);
        doc.definit_valeurs(ph, {1, 1, 1, 2, 6, 10, 11});
        QCOMPARE(volume_equivalence(courbe, doc).value_or(-1), 4.0);
        auto lu = phy::charge(phy::enregistre(ws, {}), nullptr);
        QVERIFY(lu);
        QCOMPARE(lu->seance.graphs.front().plots.front().curves.front().dosage, courbe.dosage);
        doc.definit_valeurs(ph, std::vector<double>(v.size(), 7));
        QVERIFY(!volume_equivalence(courbe, doc));
    }

    void incertitude_negative_et_unites_modeles()
    {
        Document doc;
        const QId y = *doc.ajoute_experimentale(u"U"_s, u"V"_s, {2, -3, 4});
        const auto invalide = doc.cree_incertitude(y, u"0.1*U"_s);
        QVERIFY(!invalide);
        QVERIFY(invalide.error.contains(u"2"_s));
        QCOMPARE(doc.quantities.size(), 1U);
        doc.definit_valeurs(y, {2, 3, 4});
        const auto uy = doc.cree_incertitude(y, u"0.1*U"_s);
        QVERIFY(uy);
        doc.definit_valeurs(y, {2, -3, 4});
        QVERIFY(doc.obtient(*uy)->error.contains(u"2"_s));
        Plot p;
        p.ajoute_courbe(y, y).errors = ErrorBars::Both;
        QVERIFY(std::isnan(resout(p, doc, {}).series.front().uy.at(1)));
        doc.definit_valeurs(y, {2, 3, 4});
        QVERIFY(doc.obtient(*uy)->error.isEmpty());
        QCOMPARE(fit::ModelKind{fit::ModelKind::Affine}.unite_parametre(0, u"A"_s, u"V"_s), u"Ω"_s);
        QCOMPARE(fit::ModelKind{fit::ModelKind::Parabola}.unite_parametre(0, u"s"_s, u"m"_s), u"m/s²"_s);
    }

    void methodes_titrage()
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
        const auto c = titration::Curve::fabrique(v, p, &err);
        QVERIFY(c);
        QVERIFY(std::abs(c->saut() - 10.0) < 0.05);
        const auto t = c->tangentes(c->contact_par_defaut(), &err);
        QVERIFY(t);
        QVERIFY(std::abs(t->equivalence.first - 10.0) < 0.05);
        QVERIFY(std::abs(t->mediane(5.0) - 0.5 * (t->droite1(5.0) + t->droite2(5.0))) < 1e-12);
        const auto t2 = c->tangentes(9.0, &err);
        QVERIFY(std::abs(t2->equivalence.first - 10.0) < 0.1);
        const auto t3 = c->tangentes(11.0, &err);
        QVERIFY(t3->p1.first < 10.0 && t3->p2.first > 10.0);
        QVERIFY(std::abs(titration::maximum_derivee(v, p, &err)->first - 10.0) < 0.1);
        // courbe symétrique : V_E exact
        std::vector<double> v2, p2;
        for (int i = 0; i <= 80; ++i) {
            v2.push_back(i * 0.25);
            p2.push_back(7 + 4 * std::tanh((i * 0.25 - 10) / 0.4));
        }
        const auto c2 = titration::Curve::fabrique(v2, p2, &err);
        QVERIFY(std::abs(c2->tangentes(c2->contact_par_defaut(), &err)->equivalence.first - 10.0) < 1e-3);
        QVERIFY(std::abs(titration::maximum_derivee(v2, p2, &err)->first - 10.0) < 1e-6);
        QVERIFY(!titration::Curve::fabrique({1, 2}, {3, 4}, &err));
    }

    // --- modélisation ---
    void modeles_retrouvent_parametres()
    {
        auto check = [](fit::ModelKind kind, std::vector<double> truth, std::vector<double> xs, double noise, double tol) {
            const auto model = fit::Model::fabrique(kind, u"x"_s, {});
            QVERIFY(model);
            Rng rng{12345};
            auto ys = model->evalue(truth, xs);
            for (double &y : ys)
                y += noise * rng.gauss();
            std::vector<std::optional<double>> fixed;
            for (const QString &n : model->parametres()) {
                std::optional<double> f;
                for (const auto &[k, v] : kind.fixes_par_defaut()) {
                    if (k == n)
                        f = v;
                }
                fixed.push_back(f);
            }
            QString err;
            const auto r = fit::ajuste(*model, xs, ys, {fixed, std::nullopt}, &err);
            QVERIFY2(r, qPrintable(kind.etiquette() + u" : "_s + err));
            for (size_t i = 0; i < truth.size(); ++i) {
                const double scale = truth[i] == 0.0 ? 0.1 : std::abs(truth[i]);
                const double u = r->uncertainties[i];
                const bool ok = std::abs(r->params[i] - truth[i]) / scale < tol || (std::isfinite(u) && std::abs(r->params[i] - truth[i]) < 4 * u);
                QVERIFY2(ok, qPrintable(u"%1 : %2 = %3, attendu %4"_s.arg(kind.etiquette(), model->parametres()[i]).arg(r->params[i]).arg(truth[i])));
            }
            if (kind.type != fit::ModelKind::Constant)
                QVERIFY2(r->r2 > 0.9, qPrintable(kind.etiquette()));
        };
        using MK = fit::ModelKind;
        auto k = [](MK::Type t) {
            MK m;
            m.type = t;
            return m;
        };
        const auto x = espace_lineaire(0, 10, 80);
        const auto xp = espace_lineaire(0.5, 10, 80);
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
        check(k(MK::Discharge), {5.0, 0.0, 1e-3, 0.0}, espace_lineaire(0, 5e-3, 250), 0.02, 0.02);
        // charge en nanosecondes, durées en seconde : le pas de dérivation suit la constante de temps
        check(k(MK::Charge), {2.3, 0.0, 1e-9, 0.4}, espace_lineaire(0, 5e-9, 81), 0.0, 1e-6);
        check(k(MK::Sine), {0.8, 1.0 / 440.0, 1.0, 0.0}, espace_lineaire(0, 0.02, 400), 0.01, 0.02);
    }

    /// Parabole sur des abscisses très loin de zéro (x ≈ 10⁹) : les coefficients en x perdent leurs
    /// chiffres utiles ; R² est celui du polynôme donné, et un avertissement le signale.
    void polynome_loin_de_zero()
    {
        fit::ModelKind kind;
        kind.type = fit::ModelKind::Parabola;
        const auto model = fit::Model::fabrique(kind, u"x"_s, {});
        QVERIFY(model);
        for (const double x0 : {0.0, 1e9}) {
            std::vector<double> xs, ys;
            for (int i = 0; i <= 20; ++i) {
                const double t = i / 20.0;
                xs.push_back(x0 + t);
                ys.push_back(2.0 * t * t + 3.0 * t + 4.0);
            }
            QString err;
            const auto r = fit::ajuste(*model, xs, ys, {{}, std::nullopt}, &err);
            QVERIFY2(r, qPrintable(err));
            const auto f = model->evalue(r->params, xs);
            double ssr = 0.0, mean = 0.0, sst = 0.0;
            for (size_t i = 0; i < xs.size(); ++i) {
                ssr += (ys[i] - f[i]) * (ys[i] - f[i]);
                mean += ys[i] / double(xs.size());
            }
            for (double y : ys)
                sst += (y - mean) * (y - mean);
            // R² annoncé : celui des coefficients donnés
            QVERIFY2(std::abs(r->r2 - (1.0 - ssr / sst)) < 1e-6, qPrintable(u"x0 = %1 : R² = %2"_s.arg(x0).arg(r->r2)));
            QCOMPARE(r->warning.isEmpty(), x0 == 0.0);
        }
    }

    void incertitudes_affines()
    {
        fit::ModelKind affine;
        affine.type = fit::ModelKind::Affine;
        const auto m = fit::Model::fabrique(affine, u"x"_s, {});
        QString err;
        auto r = fit::ajuste(*m, {0, 1, 2, 3}, {1, 3, 5, 7}, {}, &err);
        QVERIFY(std::abs(r->params[0] - 2.0) < 1e-12 && std::abs(r->params[1] - 1.0) < 1e-12);
        QVERIFY(r->uncertainties[0] < 1e-10);
        QVERIFY(std::abs(r->r2 - 1.0) < 1e-12);
        const std::vector<double> x{0, 1, 2, 3}, y{1.1, 2.9, 5.2, 6.8};
        r = fit::ajuste(*m, x, y, {}, &err);
        QVERIFY(std::abs(r->params[0] - 1.94) < 1e-12);
        QVERIFY(std::abs(r->params[1] - 1.09) < 1e-12);
        double s2 = 0;
        for (int i = 0; i < 4; ++i)
            s2 += std::pow(y[i] - 1.94 * x[i] - 1.09, 2);
        s2 /= 2.0;
        QVERIFY(std::abs(r->uncertainties[0] - std::sqrt(s2 / 5.0)) < 1e-12);
        QVERIFY(std::abs(r->uncertainties[1] - std::sqrt(s2 * 14.0 / 20.0)) < 1e-12);
        // paramètre fixé
        const auto xs = espace_lineaire(0, 4, 20);
        std::vector<double> ys;
        for (double v : xs)
            ys.push_back(3 * v + 2);
        r = fit::ajuste(*m, xs, ys, {{std::nullopt, 0.0}, std::nullopt}, &err);
        QVERIFY(std::isnan(r->uncertainties[1]));
        QCOMPARE(r->params[1], 0.0);
        QVERIFY(r->params[0] > 3.0);
    }

    void erreurs_modeles_utilisateur()
    {
        using MK = fit::ModelKind;
        QString err;
        MK parabola;
        parabola.type = MK::Parabola;
        QVERIFY(!fit::ajuste(*fit::Model::fabrique(parabola, u"x"_s, {}), {1, 2}, {1, 2}, {}, &err));
        QVERIFY(err.contains(u"pas assez"_s));
        MK ln;
        ln.type = MK::Ln;
        QVERIFY(!fit::ajuste(*fit::Model::fabrique(ln, u"x"_s, {}), {-1, 2, 3}, {1, 2, 3}, {}, &err));
        QVERIFY(err.contains(u"logarithme"_s));
        MK user;
        user.type = MK::User;
        user.expression = u"a*t"_s;
        QVERIFY(!fit::Model::fabrique(user, u"x"_s, {}, &err));
        QVERIFY(err.contains(u"variable"_s));
        user.expression = u"2*x"_s;
        QVERIFY(!fit::Model::fabrique(user, u"x"_s, {}, &err));
        QVERIFY(err.contains(u"paramètre"_s));
        // g connu (paramètre du document), seul v0 est ajusté
        user.expression = u"z = v0*t - 0.5*g*t^2"_s;
        const auto m = fit::Model::fabrique(user, u"t"_s, {{u"g"_s, 9.81}});
        QCOMPARE(m->parametres(), QStringList{u"v0"_s});
        const auto t = espace_lineaire(0, 1, 20);
        std::vector<double> z;
        for (double v : t)
            z.push_back(4 * v - 0.5 * 9.81 * v * v);
        QVERIFY(std::abs(fit::ajuste(*m, t, z, {}, &err)->params[0] - 4.0) < 1e-8);
    }

    void sinusoides_parametres_fixes()
    {
        using K = fit::ModelKind;
        const auto t = espace_lineaire(0, 8, 41);
        for (auto type : {K::Sine, K::Cosine, K::DampedSine, K::DampedCosine}) {
            const auto modele = fit::Model::fabrique(K{type}, u"t"_s, {});
            QVERIFY(modele);
            const bool amorti = type == K::DampedSine || type == K::DampedCosine;
            const std::vector<double> valeurs = amorti ? std::vector<double>{-2, 6, -4, 7, 1}
                                                       : std::vector<double>{-2, -4, 7, 1};
            fit::FitOptions options;
            for (double v : valeurs)
                options.fixed.push_back(v);
            // Seul le décalage vertical est à déterminer.
            options.fixed.back().reset();
            options.initial = valeurs;
            options.initial->back() = 0;
            QString erreur;
            const auto resultat = fit::ajuste(*modele, t, modele->evalue(valeurs, t), options, &erreur);
            QVERIFY2(resultat, qPrintable(erreur));
            for (size_t i = 0; i + 1 < valeurs.size(); ++i)
                QCOMPARE(resultat->params[i], valeurs[i]);
            QVERIFY(std::abs(resultat->params.back() - 1) < 1e-8);
        }
    }

    void residus_formule_sur_une_serie()
    {
        fit::ModelKind type{fit::ModelKind::User};
        type.expression = u"U = a*(t-mean(t))"_s;
        const auto modele = fit::Model::fabrique(type, u"t"_s, {});
        QVERIFY(modele);
        const std::vector<double> t{0, 1, 2}, u{-2, 0, 2};
        QCOMPARE(modele->residus({2}, t, u), (std::vector<double>{0, 0, 0}));
        QCOMPARE(modele->residus({2}, t, {-1, 1}), (std::vector<double>{0, 0}));
        fit::FitOptions options;
        options.initial = std::vector<double>{2};
        QString erreur;
        auto resultat = fit::ajuste(*modele, t, u, options, &erreur);
        QVERIFY(resultat && resultat->residusNumeriques);
    }

    void formules_aller_retour()
    {
        // la formule exportée redonne les mêmes valeurs que le modèle
        for (auto kind : fit::ModelKind::catalogue()) {
            if (kind.type == fit::ModelKind::User)
                kind.expression = u"a*sin(t)+b"_s;
            const auto m = fit::Model::fabrique(kind, u"t"_s, {});
            QVERIFY(m);
            std::vector<double> p;
            for (qsizetype i = 0; i < m->parametres().size(); ++i)
                p.push_back(0.7 + 0.3 * i * (i % 2 == 0 ? 1 : -1));
            const auto xs = espace_lineaire(0.5, 3, 7);
            const auto expected = m->evalue(p, xs);
            const QString f = m->formule(p, u"abscisse"_s);
            auto env = [&](const QString &n) -> std::optional<expr::Value> {
                if (n == u"abscisse")
                    return expr::Value::vecteur(xs);
                return std::nullopt;
            };
            const auto got = expr::evalue_texte(f, env);
            QVERIFY2(got, qPrintable(f));
            for (size_t i = 0; i < xs.size(); ++i)
                QVERIFY2(std::abs(got->v[i] - expected[i]) < 1e-9 * (1 + std::abs(expected[i])), qPrintable(kind.etiquette() + u" : "_s + f));
            QVERIFY(!m->affichage(p, u"t"_s, u"y"_s, NumberStyle{}).isEmpty());
        }
    }

    // --- document et espace de travail ---
    void calculees_et_dependances()
    {
        Document d;
        const QId t = *d.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2, 3});
        const QId x = *d.ajoute_experimentale(u"x"_s, u"m"_s, {0, 1, 4, 9});
        Q_UNUSED(t);
        // ordre de création inverse de l'ordre de calcul
        const QId ec = *d.ajoute_calculee(u"Ec"_s, u"J"_s, u"0.5*m*v^2"_s);
        const QId v = *d.ajoute_calculee(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        QVERIFY(!d.obtient(ec)->error.isEmpty());
        d.ajoute_parametre(u"m"_s, u"kg"_s, 2.0);
        QVERIFY(d.obtient(ec)->error.isEmpty());
        QCOMPARE(d.valeurs(v), (std::vector<double>{1, 2, 4, 5}));
        QCOMPARE(d.valeurs(ec), (std::vector<double>{1, 4, 16, 25}));
        d.definit_valeur(x, 3, 10.0);
        QCOMPARE(d.valeurs(v)[3], 6.0);
    }

    void dependance_circulaire()
    {
        Document d;
        d.ajoute_experimentale(u"x"_s, {}, {1, 2});
        const QId a = *d.ajoute_calculee(u"a"_s, {}, u"b + 1"_s);
        const QId b = *d.ajoute_calculee(u"b"_s, {}, u"a * 2"_s);
        const QId c = *d.ajoute_calculee(u"c"_s, {}, u"x + 1"_s);
        QVERIFY(d.obtient(a)->error.contains(u"circulaire"_s));
        QVERIFY(d.obtient(b)->error.contains(u"circulaire"_s));
        QVERIFY(d.obtient(c)->error.isEmpty());
        const QId s = *d.ajoute_calculee(u"s"_s, {}, u"s + 1"_s);
        QVERIFY(!d.obtient(s)->error.isEmpty());
    }

    void formule_en_erreur_et_mesure_vide()
    {
        Document d;
        const QId x = *d.ajoute_experimentale(u"x"_s, {}, {1, 2, 3});
        const QId doubleX = *d.ajoute_calculee(u"doubleX"_s, {}, u"2*x"_s);
        const QId total = *d.ajoute_calculee(u"total"_s, {}, u"sum(doubleX)"_s);
        const QId suivant = *d.ajoute_calculee(u"suivant"_s, {}, u"total+1"_s);
        d.ajoute_experimentale(u"vide"_s, {}, {});
        const QId temoin = *d.ajoute_calculee(u"temoin"_s, {}, u"sum(vide)"_s);
        QCOMPARE(d.valeurs(total), std::vector<double>{12});
        d.retire_grandeur(x);
        QVERIFY(d.obtient(doubleX)->error.contains(u"x"_s));
        QVERIFY(d.obtient(total)->error.contains(u"doubleX"_s));
        QVERIFY(d.obtient(suivant)->error.contains(u"total"_s));
        QVERIFY(d.valeurs(total).empty() && d.valeurs(suivant).empty());
        QCOMPARE(d.valeurs(temoin), std::vector<double>{0});
        QString erreur;
        QVERIFY(!d.evalue(u"sum(total)"_s, &erreur));
        QVERIFY(erreur.contains(u"total"_s));
        d = Document::depuis_json(d.en_json());
        QVERIFY(d.obtient(total)->error.contains(u"doubleX"_s));
        d.ajoute_experimentale(u"x"_s, {}, {2, 3});
        QCOMPARE(d.valeurs(suivant), std::vector<double>{11});
        QVERIFY(d.obtient(suivant)->error.isEmpty());
    }

    void renommage_et_valeurs()
    {
        Document d;
        const QId t = *d.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2, 3});
        const QId x = *d.ajoute_experimentale(u"x"_s, u"m"_s, {0, 1, 4, 9});
        const QId v = *d.ajoute_calculee(u"v"_s, {}, u"deriv(x, t) + x*0"_s);
        QVERIFY(d.renomme_grandeur(x, u"pos"_s));
        QCOMPARE(*d.obtient(v)->formula, u"deriv(pos, t) + pos*0"_s);
        QVERIFY(d.obtient(v)->error.isEmpty());
        QVERIFY(d.renomme_grandeur(x, u"t"_s).error.contains(u"déjà"_s));
        QVERIFY(d.renomme_grandeur(x, u"sin"_s).error.contains(u"réservé"_s));
        QVERIFY(!d.renomme_grandeur(x, u"2a"_s));
        d.definit_valeur(t, 6, 6.0);
        QCOMPARE(d.valeurs(t).size(), size_t(7));
        QVERIFY(std::isnan(d.valeurs(t)[5]));
        d.definit_valeur(t, 6, std::nullopt);
        QCOMPARE(d.valeurs(t).size(), size_t(4));
        const QId c = *d.ajoute_calculee(u"c"_s, {}, u"2*t"_s);
        QVERIFY(!d.definit_valeur(c, 0, 1.0).isEmpty());
        d.supprime_lignes({t}, {0, 1});
        QCOMPARE(d.valeurs(t), (std::vector<double>{2, 3}));
        QCOMPARE(d.nom_unique(u"x"_s), u"x"_s);
        QCOMPARE(d.nom_unique(u"t"_s), u"t2"_s);
    }

    void diffusion_incertitude()
    {
        Document d;
        d.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2, 3});
        const QId x = *d.ajoute_experimentale(u"x"_s, u"m"_s, {0, 1, 4, 9});
        const QId u = *d.cree_incertitude(x, u"0.05"_s);
        QCOMPARE(d.obtient(u)->name, u"u_x"_s);
        QCOMPARE(d.valeurs(u), std::vector<double>(4, 0.05));
        QCOMPARE(d.obtient(x)->uncertainty, std::optional<QId>(u));
        QCOMPARE(d.proprietaire_incertitude(u)->id, x);
        d.retire_grandeur(u);
        QVERIFY(!d.obtient(x)->uncertainty);
    }

    void incertitude_dans_une_formule()
    {
        Seance ws;
        auto &doc = ws.doc;
        const QId tension = *doc.ajoute_experimentale(u"U"_s, u"V"_s, {1, 2, 3, 4});
        const QId inc = *doc.cree_incertitude(tension, u"0.2"_s);
        const QId n = *doc.ajoute_calculee(u"N"_s, {}, u"len(u_U)"_s);
        const QId moyenne = *doc.ajoute_calculee(u"u_moy"_s, u"V"_s, u"sqrt(sum(u_U^2))/len(U)"_s);
        auto verifie = [&](size_t lignes) {
            QCOMPARE(doc.valeurs(inc), std::vector<double>(lignes, 0.2));
            QCOMPARE(doc.valeurs(n), std::vector<double>{double(lignes)});
            QVERIFY(std::abs(doc.valeurs(moyenne).at(0) - 0.2 / std::sqrt(double(lignes))) < 1e-12);
            QString erreur;
            const auto relu = phy::charge(phy::enregistre(ws, {}), &erreur);
            QVERIFY2(relu, qPrintable(erreur));
            QCOMPARE(relu->seance.doc.valeurs(moyenne), doc.valeurs(moyenne));
            verifie_export(pyexport::exporte_script(relu->seance),
                           u"assert N == %1\nnp.testing.assert_allclose(u_moy, 0.2/np.sqrt(N))"_s.arg(lignes));
        };
        verifie(4);
        doc.definit_valeur(tension, 4, 5.0);
        verifie(5);
        doc.definit_valeur(tension, 4, std::nullopt);
        verifie(4);

        // Le nombre de lignes peut venir d'une grandeur calculée après l'incertitude.
        const QId offset = *doc.ajoute_calculee(u"offset"_s, {}, u"0.05"_s);
        const QId corrigee = *doc.ajoute_calculee(u"corrigee"_s, {}, u"U + offset"_s);
        doc.definit_incertitude(corrigee, offset);
        QCOMPARE(doc.valeurs(offset), std::vector<double>(4, 0.05));
        verifie_export(pyexport::exporte_script(ws), u"np.testing.assert_equal(offset, [0.05]*4)"_s);
        // Chaque ligne peut aussi avoir sa propre incertitude.
        doc.definit_formule(offset, u"U/10"_s);
        QCOMPARE(doc.valeurs(offset), (std::vector<double>{0.1, 0.2, 0.3, 0.4}));
        verifie_export(pyexport::exporte_script(ws), u"np.testing.assert_allclose(offset, U/10)"_s);
        doc.definit_valeurs(tension, {});
        QVERIFY(doc.valeurs(inc).empty());
        QCOMPARE(doc.valeurs(n), std::vector<double>{0.});
        verifie_export(pyexport::exporte_script(ws), u"assert np.size(u_U) == N == 0"_s);
        doc.definit_valeurs(tension, {2});
        verifie(1);
    }

    void longueurs_incertitudes_incompatibles()
    {
        Seance ws;
        auto &doc = ws.doc;
        doc.ajoute_experimentale(u"court"_s, {}, {1, 2});
        doc.ajoute_experimentale(u"longue"_s, {}, {1, 2, 3});
        const auto a = *doc.ajoute_calculee(u"a"_s, {}, u"1"_s);
        const auto b = *doc.ajoute_calculee(u"b"_s, {}, u"1 + 0*len(a)"_s);
        const auto c = *doc.ajoute_calculee(u"c"_s, {}, u"a + 0*len(b)"_s);
        const auto duo = *doc.ajoute_calculee(u"duo"_s, {}, u"court + 0*len(c)"_s);
        const auto trio = *doc.ajoute_calculee(u"trio"_s, {}, u"longue + 0*len(duo)"_s);
        const auto temoin = *doc.ajoute_calculee(u"temoin"_s, {}, u"sum(longue)"_s);
        // Plusieurs rattachements en boucle font alterner deux et trois lignes.
        doc.definit_incertitude(a, duo);
        doc.definit_incertitude(b, a);
        doc.definit_incertitude(c, b);
        doc.definit_incertitude(duo, b);
        doc.definit_incertitude(trio, c);
        QVERIFY(doc.valeurs(a).empty());
        QVERIFY(doc.obtient(a)->error.contains(u"longueurs"_s));
        QCOMPARE(doc.valeurs(temoin), std::vector<double>{6.});
        verifie_export(pyexport::exporte_script(ws), u"assert np.size(a) == 0 and temoin == 6"_s);
        doc.definit_incertitude(b, std::nullopt);
        QVERIFY(doc.obtient(a)->error.isEmpty());
        QCOMPARE(doc.valeurs(a), std::vector<double>{1.});
    }

    void usages_espace_travail_renommage()
    {
        Seance ws;
        const QId t = *ws.doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2});
        const QId x = *ws.doc.ajoute_experimentale(u"x"_s, u"m"_s, {0, 2, 4});
        const QId v = *ws.doc.ajoute_calculee(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        ws.ajoute_tableur({t, x, v});
        const ViewId g = ws.ajoute_graphe();
        Plot &p = ws.graphe_modifiable(g)->plots[0];
        p.ajoute_courbe(t, x);
        fit::ModelKind user;
        user.type = fit::ModelKind::User;
        user.expression = u"a*t + b*x*0"_s;
        p.fits.push_back(FitSpec::fabrique(user, t, x, PALETTE[1]));
        const QId u = *ws.doc.cree_incertitude(x, u"0.1"_s);
        QStringList usages;
        for (const auto &us : ws.usages(x))
            usages << us.texte();
        QVERIFY(usages.contains(u"tableur « Tableur 1 »"_s));
        QVERIFY(usages.contains(u"graphe « Graphe 1 »"_s));
        QVERIFY(usages.contains(u"formule de « v »"_s));
        QVERIFY(usages.contains(u"incertitude rattachée « u_x »"_s));
        QVERIFY(std::any_of(usages.begin(), usages.end(), [](const QString &s) { return s.startsWith(u"modèle utilisateur"_s); }));
        QCOMPARE(ws.usages(u).size(), size_t(1));
        QVERIFY(ws.renomme_grandeur(x, u"pos"_s).isEmpty());
        QCOMPARE(*ws.doc.obtient(v)->formula, u"deriv(pos, t)"_s);
        QCOMPARE(ws.graphs[0].plots[0].fits[0].kind.expression, u"a*t + b*pos*0"_s);
        ws.supprime_grandeur(x);
        QVERIFY(std::find(ws.tables[0].columns.begin(), ws.tables[0].columns.end(), x) == ws.tables[0].columns.end());
        QVERIFY(ws.graphs[0].plots[0].curves.empty());
        QVERIFY(!ws.doc.obtient(v)->error.isEmpty());
    }

    void phy_aller_retour()
    {
        Seance ws;
        const QId t = *ws.doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2});
        const QId x = *ws.doc.ajoute_experimentale(u"x"_s, u"m"_s, {0, 2, 4});
        const QId v = *ws.doc.ajoute_calculee(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        ws.ajoute_tableur({t, x, v});
        const ViewId g = ws.ajoute_graphe();
        CurveSpec &c = ws.graphe_modifiable(g)->plots[0].ajoute_courbe(t, x);
        c.errors = ErrorBars::Both;
        c.capSize = 4.0;
        ws.graphe_modifiable(g)->plots[0].x.label = u"temps"_s;
        ws.graphe_modifiable(g)->plots[0].y.format = NumFormat{NumFormat::Fixed, 2};
        ws.doc.definit_valeur(t, 1, std::nullopt);
        ws.meta.title = u"Chute libre"_s;
        const QByteArray text = phy::enregistre(ws, QJsonObject{{u"dock"_s, 1}});
        QString err;
        auto loaded = phy::charge(text, &err);
        QVERIFY2(loaded, qPrintable(err));
        QCOMPARE(loaded->disposition.toObject().value(u"dock"_s).toInt(), 1);
        QCOMPARE(loaded->seance.meta.title, u"Chute libre"_s);
        QVERIFY(std::isnan(loaded->seance.doc.valeurs(t)[1]));
        QCOMPARE(*loaded->seance.doc.obtient(v)->formula, u"deriv(x, t)"_s);
        QCOMPARE(loaded->seance.graphs, ws.graphs);
        QCOMPARE(loaded->seance.tables, ws.tables);
        // nouvelle vue après rechargement : identifiant libre
        Seance w3 = loaded->seance;
        const ViewId id = w3.ajoute_graphe();
        QCOMPARE(std::count_if(w3.graphs.begin(), w3.graphs.end(), [&](const GraphView &gv) { return gv.id == id; }), 1);
        const QId n = *w3.doc.ajoute_experimentale(u"n"_s, {}, {});
        QVERIFY(n > v);
    }

    void phy_verifie_la_structure_avant_ouverture()
    {
        auto fichier = [](const QByteArray &seance) {
            return R"({"format":"mevreg-phy","version":0,"workspace":)" + seance + "}";
        };
        QString erreur;
        // Une séance ancienne peut n'avoir encore aucun tableur, graphe ou paramètre.
        QVERIFY(phy::charge(fichier(R"({"doc":{}})"), &erreur));
        const QList<QByteArray> seancesIllisibles{
            R"({})",
            R"({"doc":{"quantities":{}}})",
            R"({"doc":{"quantities":[{"id":0}]}})",
            R"({"doc":{"quantities":[{"id":1}],"parameters":[{"id":1}]}})",
            R"({"doc":{},"graphs":[{"id":1}],"tables":[{"id":1}]})",
            R"({"doc":{"parameters":[{"id":1,"name":"R","value":"4,7"}]}})",
            R"({"doc":{"quantities":[{"id":1,"name":"U","values":[1,"2,5"]}]}})"
        };
        for (const auto &seance : seancesIllisibles) {
            erreur.clear();
            QVERIFY2(!phy::charge(fichier(seance), &erreur), seance.constData());
            QVERIFY(!erreur.isEmpty());
        }
    }

    void phy_version_un()
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
        auto l = phy::charge(v1.toUtf8(), &err);
        QVERIFY2(l, qPrintable(err));
        QCOMPARE(l->seance.doc.valeurs(2)[3], 6.0);
        const Plot &p = l->seance.graphs[0].plots[0];
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
        QVERIFY(!phy::charge("{}", &err));
        QVERIFY(!phy::charge(R"({"format":"mevreg-phy","version":99,"workspace":{}})", &err));
        QVERIFY(err.contains(u"plus récente"_s));
    }

    void usage_identifiants_python()
    {
        QVERIFY(usages_python(u"v = np.gradient(x, t)"_s, u"x"_s));
        QVERIFY(!usages_python(u"np.x = 1  # x"_s, u"x"_s));
        QVERIFY(!usages_python(u"print('x')"_s, u"x"_s));
    }

    // --- CSV et copier-coller ---
    void imports_de_la_suite()
    {
        const QString dest = qEnvironmentVariable("MEVSUITE_CSV_TEST");
        if (dest.isEmpty())
            QSKIP("Lancé avec les exports des trois logiciels par packaging/test-echanges.sh");
        const QStringList noms{u"profil-fr.csv"_s, u"profil-en.csv"_s, u"trajectoire.csv"_s,
                               u"spectre-fr.csv"_s, u"spectre-en.csv"_s};
        for (const QString &nom : noms) {
            QFile f(QDir(dest).filePath(nom));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const auto lignes = csv::lignes_texte(csv::decode_texte(f.readAll()));
            const auto lu = csv::lit(lignes, csv::detecte(lignes));
            QCOMPARE(lu.badCells, 0);
            Document doc;
            const auto ids = csv::importe(doc, csv::Parsed{lu.columns});
            if (nom.startsWith(u"profil")) {
                QCOMPARE(ids.size(), size_t(2));
                QCOMPARE(doc.obtient(ids[0])->unit, u"mm"_s);
                QCOMPARE(doc.valeurs(ids[0])[1], 0.1);
                QCOMPARE(doc.valeurs(ids[1])[1], 34.57);
            } else if (nom.startsWith(u"trajectoire")) {
                QCOMPARE(ids.size(), size_t(5));
                QCOMPARE(doc.obtient(ids[0])->unit, u"s"_s);
                QCOMPARE(doc.obtient(ids[1])->unit, u"m"_s);
                QCOMPARE(doc.valeurs(ids[1]).back(), 0.4);
                QCOMPARE(doc.valeurs(ids[1]).size(), size_t(4));
                QCOMPARE(doc.valeurs(ids[3]).size(), size_t(3));
            } else {
                QCOMPARE(ids.size(), size_t(3));
                QCOMPARE(doc.obtient(ids[0])->unit, u"Hz"_s);
                QCOMPARE(doc.obtient(ids[2])->unit, u"dB"_s);
                QCOMPARE(doc.valeurs(ids[0]).size(), size_t(513));
                QVERIFY(std::abs(doc.valeurs(ids[0])[1] - 44100.0 / 1024) < 0.0001);
                QVERIFY(doc.valeurs(ids[1])[10] > 0.4);
            }
        }
    }

    void csv_francais()
    {
        QString err;
        const auto p = csv::analyse(u"t (s);x (m);commentaire\n0;0,5;\n0,1;0,72;a\n0,2;;\n"_s, &err);
        QVERIFY(p);
        QCOMPARE(p->columns.size(), size_t(2));
        QCOMPARE(p->columns[0].name, u"t"_s);
        QCOMPARE(p->columns[0].unit, u"s"_s);
        QCOMPARE(p->columns[0].values, (std::vector<double>{0, 0.1, 0.2}));
        QCOMPARE(p->columns[1].values[1], 0.72);
        QCOMPARE(p->columns[1].values.size(), size_t(2));
        QCOMPARE(p->ignored, 1);
        const auto q = csv::analyse(u"time,voltage\ns,V\n0.0,1.5\n0.5,2.5\n"_s, &err);
        QCOMPARE(q->columns[0].unit, u"s"_s);
        QCOMPARE(q->columns[1].name, u"voltage"_s);
        QCOMPARE(q->columns[1].values, (std::vector<double>{1.5, 2.5}));
        const auto r = csv::analyse(u"1 2\n3 4\n"_s, &err);
        QCOMPARE(r->columns[0].name, u"c1"_s);
        QCOMPARE(r->columns[1].values, (std::vector<double>{2, 4}));
        QVERIFY(!csv::analyse(u""_s, &err));
        QVERIFY(!csv::analyse(u"a;b\nc;d"_s, &err));
        QCOMPARE(csv::nettoie_nom(u"Tension U"_s, u"c1"_s), u"Tension_U"_s);
        QCOMPARE(csv::nettoie_nom(u"2t"_s, u"c1"_s), u"g_2t"_s);
        QCOMPARE(csv::nettoie_nom(u"***"_s, u"c4"_s), u"c4"_s);
        QCOMPARE(csv::decoupe_en_tete(u"U [V]"_s), std::make_pair(u"U"_s, u"V"_s));
        QCOMPARE(csv::decoupe_en_tete(u"x / m"_s), std::make_pair(u"x"_s, u"m"_s));
    }

    void collage_rectangulaire()
    {
        const auto seule = csv::lit_collage(u"T (s)\n1,98\n2,02"_s);
        QCOMPARE(seule.columns.size(), 1U);
        QCOMPARE(seule.columns[0].unit, u"s"_s);
        QCOMPARE(seule.columns[0].values, (std::vector<double>{1.98, 2.02}));
        const auto incomplet = csv::lit_collage(u"t\tX\tU\ns\tm\tV\n0\t\tabsent\n1\t\t4.7"_s);
        QCOMPARE(incomplet.columns.size(), 3U);
        QCOMPARE(incomplet.rows, (std::vector<int>{2, 3}));
        QCOMPARE(incomplet.columns[2].unit, u"V"_s);
        QVERIFY(std::isnan(incomplet.columns[1].values[1]));
        QVERIFY(std::isnan(incomplet.columns[2].values[0]));
        QCOMPARE(incomplet.columns[2].values[1], 4.7);
        QCOMPARE(incomplet.firstBad.text, u"absent"_s);
        const auto manquante = csv::lit_collage(u"N/A\n2,30"_s);
        QVERIFY(manquante.columns[0].name.isEmpty() && std::isnan(manquante.columns[0].values[0]));
        QCOMPARE(manquante.columns[0].values[1], 2.3);
    }

    void csv_detection_lecture()
    {
        using csv::Decimal;
        // séparateurs devinés d'après le nombre de cases des lignes
        auto sepOf = [](const char *text) { return csv::detecte(csv::lignes_texte(QString::fromUtf8(text))).separator; };
        QCOMPARE(sepOf("t\tx\n0,5\t1\n"), QChar(u'\t'));
        QCOMPARE(sepOf("t (s);x (m)\n0,5;1\n0,6;2\n"), QChar(u';'));
        QCOMPARE(sepOf("t,x\n0.5,1\n0.6,2\n"), QChar(u','));
        // des nombres à virgule séparés par des espaces : la virgule n'est pas le séparateur
        QCOMPARE(sepOf("0,5 1,2\n0,6 1,3\n"), csv::SPACES);
        // séparateur décimal : celui des nombres du fichier ; avec la virgule entre les valeurs, le point
        QCOMPARE(csv::detecte(csv::lignes_texte(u"a;b\n1.5;2.5\n"_s)).decimal, Decimal::Point);
        QCOMPARE(csv::detecte(csv::lignes_texte(u"a;b\n1,5;2,5\n"_s)).decimal, Decimal::Comma);
        QCOMPARE(csv::detecte(csv::lignes_texte(u"a;b\n1;2\n"_s), Decimal::Point).decimal, Decimal::Point);
        QCOMPARE(csv::detecte(csv::lignes_texte(u"a,b\n1,2\n"_s)).decimal, Decimal::Point);
        // l'autre signe rend la case illisible plutôt que d'être deviné
        QCOMPARE(csv::nombre(u"1,5"_s, Decimal::Comma), std::optional<double>(1.5));
        QVERIFY(!csv::nombre(u"1.5"_s, Decimal::Comma));
        QVERIFY(!csv::nombre(u"1,234"_s, Decimal::Point));
        QCOMPARE(csv::nombre(u"1 234,5"_s, Decimal::Comma), std::optional<double>(1234.5));
        // guillemets : un séparateur entre guillemets fait partie de la case
        QCOMPARE(csv::decoupe_ligne(u"\"a;b\";\"c\"\"d\"; e "_s, u';'), (QStringList{u"a;b"_s, u"c\"d"_s, u"e"_s}));
        // fins de ligne des anciens Mac, marque d'ordre des octets, lignes vides de la fin
        QCOMPARE(csv::lignes_texte(QString(QChar(0xfeff)) + u"a\r1\r\r"_s), (QStringList{u"a"_s, u"1"_s}));
        // un nom entre guillemets sur deux lignes reste dans sa ligne (le saut devient une espace) ; un
        // guillemet qui ne se ferme jamais ne réunit pas les lignes
        QCOMPARE(csv::lignes_texte(u"t (s),\"U (V\r\ncrête)\"\r\n0,1\r\n"_s), (QStringList{u"t (s),\"U (V crête)\""_s, u"0,1"_s}));
        QCOMPARE(csv::lignes_texte(u"a;\"b\n1;2\n"_s), (QStringList{u"a;\"b"_s, u"1;2"_s}));
        {
            const QStringList two = csv::lignes_texte(u"t (s),\"U (V\ncrête)\"\n0,1.5\n1,2.5\n"_s);
            const csv::Reading head = csv::lit(two, csv::detecte(two));
            QCOMPARE(head.columns.size(), size_t(2));
            QCOMPARE(head.columns[1].name, u"U"_s);
            QCOMPARE(head.columns[1].unit, u"V crête"_s);
        }

        // titre, noms, unités sur leur ligne, ligne vide, case illisible, ligne d'un autre nombre de cases
        const QStringList lines = csv::lignes_texte(u"Pendule\ntemps;angle\ns;°\n0;1,5\n;\n0,1;x\n0,2;1,2;9\n"_s);
        csv::ReadOptions o = csv::detecte(lines);
        QCOMPARE(o.nameLine, 1);
        QCOMPARE(o.unitLine, 2);
        QCOMPARE(o.firstLine, 3);
        csv::Reading r = csv::lit(lines, o);
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
        r = csv::lit(lines, o);
        QCOMPARE(r.rows.size(), size_t(4));
        QVERIFY(std::isnan(r.columns[0].values[1]));
        // sans ligne des noms : c1, c2…
        o.nameLine = o.unitLine = -1;
        QCOMPARE(csv::lit(lines, o).columns[1].name, u"c2"_s);
        // mauvais séparateur décimal : les cases sont signalées, et celles que l'autre lirait
        o = csv::detecte(lines);
        o.decimal = Decimal::Point;
        r = csv::lit(lines, o);
        QCOMPARE(r.badCells, 5);
        QCOMPARE(r.otherDecimal, 4);
    }

    void csv_export_aller_retour()
    {
        Document doc;
        const QId t = *doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 0.1, NAN, 0.3});
        const QId u = *doc.ajoute_experimentale(u"U"_s, u"V"_s, {1.25, 2.0});
        const QString s = csv::texte_export(doc, {t, u}, csv::FRENCH);
        QVERIFY(s.startsWith(u"t (s);U (V)\r\n0;1,25\r\n0,1;2\r\n;\r\n0,3;\r\n"_s));
        QString err;
        const auto p = csv::analyse(s, &err);
        const auto ids = csv::importe(doc, *p);
        QCOMPARE(doc.nom(ids[0]), u"t2"_s);
        QCOMPARE(doc.valeurs(ids[0])[3], 0.3);
        QVERIFY(std::isnan(doc.valeurs(ids[0])[2]));
        // une unité qui contient le séparateur, ou un guillemet : le champ est entre guillemets
        const QId w = *doc.ajoute_experimentale(u"w"_s, u"V; crête"_s, {1});
        const QString q = csv::texte_export(doc, {t, w}, csv::FRENCH);
        const QString head = q.section(u"\r\n"_s, 0, 0);
        QCOMPARE(head, u"t (s);\"w (V; crête)\""_s);
        QCOMPARE(csv::decoupe_ligne(head, u';'), (QStringList{u"t (s)"_s, u"w (V; crête)"_s}));
    }

    void apercu_csv_et_fichier_complet()
    {
        Document doc;
        const auto t = *doc.ajoute_experimentale(u"t"_s, u"µs"_s, {0, 1, 2, 3, 4, 5, 6, 7});
        const auto u = *doc.ajoute_experimentale(u"U"_s, u"V;crête"_s, {0.5, NAN, 2});
        csv::ExportOptions options = csv::FRENCH;
        for (auto entete : {csv::ExportOptions::Header::NameUnit, csv::ExportOptions::Header::TwoLines}) {
            options.header = entete;
            const QString fichier = csv::texte_export(doc, {t, u}, options);
            const QStringList lignes = fichier.split(u"\r\n"_s, Qt::SkipEmptyParts);
            for (size_t limite : {0u, 1u, 8u, 100u}) {
                size_t total = 0;
                const QString apercu = csv::texte_export(doc, {t, u}, options, limite, &total);
                QCOMPARE(total, size_t(lignes.size()));
                QString debut;
                for (const QString &ligne : lignes.mid(0, qsizetype(limite)))
                    debut += ligne + u"\r\n"_s;
                QCOMPARE(apercu, debut);
            }
            QVERIFY(fichier.endsWith(u"7;\r\n"_s));
        }
    }

    // --- export vers Python ---
    void expressions_numpy()
    {
        auto np = [](const QString &src) { return pyexport::en_numpy(expr::analyse(src).expr->racine()); };
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
        QCOMPARE(pyexport::nom_python(u"lambda"_s), u"lambda_"_s);
    }

    void script_python_complet()
    {
        Seance ws;
        const QId t = *ws.doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 0.1, 0.2, NAN});
        const QId x = *ws.doc.ajoute_experimentale(u"x"_s, u"m"_s, {0, 0.05, 0.2, 0.45});
        ws.doc.ajoute_calculee(u"Ec"_s, u"J"_s, u"0.5*m*v^2"_s);
        const QId v = *ws.doc.ajoute_calculee(u"v"_s, u"m/s"_s, u"deriv(x, t)"_s);
        ws.doc.ajoute_parametre(u"m"_s, u"kg"_s, 0.1);
        ws.doc.cree_incertitude(x, u"0.01"_s);
        const ViewId g = ws.ajoute_graphe();
        Plot &p = ws.graphe_modifiable(g)->plots[0];
        p.ajoute_courbe(t, x).errors = ErrorBars::Vertical;
        p.ajoute_courbe(t, v).side = YSide::Right;
        fit::ModelKind parabola;
        parabola.type = fit::ModelKind::Parabola;
        p.fits.push_back(FitSpec::fabrique(parabola, t, x, PALETTE[3]));
        maj_modeles(ws.graphe_modifiable(g)->plots[0], ws.doc);
        const QString s = pyexport::exporte_script(ws);
        // relancé dans Capytale, le script repart d'une figure vide (sinon il redessine par-dessus)
        QVERIFY(s.contains(u"plt.clf()"_s) && s.indexOf(u"plt.clf()"_s) < s.indexOf(u"plt.figure("_s));
        QVERIFY(s.contains(u"import numpy as np"_s));
        QVERIFY2(s.contains(u"t = np.array([0.0, 0.1, 0.2, np.nan])"_s), qPrintable(s));
        // t a une case vide : la dérivée de l'export la saute, comme MeVReg
        QVERIFY(s.contains(u"def deriv(y, x):"_s));
        QVERIFY(s.indexOf(u"v = deriv(x, t)"_s) < s.indexOf(u"Ec = 0.5*m*v**2"_s));
        QVERIFY2(s.contains(u"ax.errorbar(t, x, yerr=_mevreg_incertitude(u_x)"_s), qPrintable(s));
        QVERIFY(s.contains(u"ax2 = ax.twinx()"_s));
        QVERIFY(s.contains(u"xm = np.linspace("_s));
        QVERIFY(s.contains(u"plt.show()"_s));
    }

    /// Le script exporté, exécuté par le vrai numpy (matplotlib remplacé par un module muet), donne
    /// les valeurs de MeVReg : cases vides des dérivées et primitives, noms réservés, et garde les
    /// réglages des graphes (incertitudes côté par côté, droites, textes, étendue et nom des modèles).
    void script_python_garde_resultats()
    {
        Seance ws;
        const QId t = *ws.doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2});
        const QId y = *ws.doc.ajoute_experimentale(u"y"_s, QString(), {1, NAN, 1});
        ws.doc.ajoute_calculee(u"I"_s, QString(), u"integ (y, t)"_s);
        ws.doc.ajoute_calculee(u"D"_s, QString(), u"deriv(y, t)"_s);
        ws.doc.ajoute_experimentale(u"class"_s, QString(), {1});
        ws.doc.ajoute_experimentale(u"class_"_s, QString(), {2});
        ws.doc.ajoute_calculee(u"d"_s, QString(), u"class_ - class"_s);
        const QId lo = *ws.doc.ajoute_experimentale(u"lo"_s, QString(), {0.1, 0.1, 0.1});
        const QId hi = *ws.doc.ajoute_experimentale(u"hi"_s, QString(), {0.3, 0.3, 0.3});
        // Incertitude constante d'un côté, une grandeur mesurée de l'autre.
        const QId w = *ws.doc.ajoute_experimentale(u"w"_s, QString(), {2, 3, 4});
        const QId court = *ws.doc.ajoute_experimentale(u"court"_s, {}, {0, 1});
        ws.doc.ajoute_experimentale(u"mes"_s, {}, {0.49999999999999994, 0.5, -2.5});
        ws.doc.ajoute_calculee(u"arrondi"_s, {}, u"round(mes)"_s);
        ws.doc.ajoute_calculee(u"petit"_s, {}, u"min(y, t)"_s);
        ws.doc.cree_incertitude(w, u"0.05"_s);
        ws.doc.recalcule();
        const ViewId g = ws.ajoute_graphe();
        Plot &p = ws.graphe_modifiable(g)->plots[0];
        CurveSpec &c = p.ajoute_courbe(t, y);
        c.errors = ErrorBars::Vertical;
        c.errYLow = lo;
        c.errYHigh = hi;
        CurveSpec &cw = p.ajoute_courbe(t, w);
        cw.errors = ErrorBars::Vertical;
        cw.errYHigh = hi;
        auto &tronquee = p.ajoute_courbe(t, court);
        tronquee.errors = ErrorBars::Vertical;
        tronquee.errYHigh = hi;
        fit::ModelKind affine;
        affine.type = fit::ModelKind::Affine;
        FitSpec f = FitSpec::fabrique(affine, t, y, PALETTE[3]);
        f.label = u"mon modèle"_s;
        f.draw = FitDraw::Custom;
        f.drawRange = {0.5, 1.5};
        p.fits.push_back(f);
        Annotation v;
        v.kind = Annotation::VLine;
        v.x = 1.0;
        v.label = u"E"_s;
        Annotation line;
        line.kind = Annotation::Line;
        Annotation text;
        text.kind = Annotation::Text;
        // texte sur deux lignes : le saut de ligne est échappé dans la chaîne Python
        text.text = u"note\nsur deux lignes"_s;
        p.annotations = {v, line, text};
        maj_modeles(p, ws.doc);
        ws.ajoute_script(u"import mevreg as mr\nmr.creer(\"z\", [3])\n"_s);
        const QString s = pyexport::exporte_script(ws);
        QVERIFY2(s.contains(u"yerr=[_mevreg_incertitude(np.broadcast_to(lo, np.shape(y))), _mevreg_incertitude(np.broadcast_to(hi, np.shape(y)))]"_s), qPrintable(s));
        QVERIFY(s.contains(u"label=\"mon modèle\""_s));
        QVERIFY(s.contains(u"xm = np.linspace(0.5, 1.5, 300)"_s));
        QVERIFY(s.contains(u".axvline(1.0"_s) && s.contains(u".axline((0.0, 0.0), (1.0, 1.0)"_s) && s.contains(u".text(0.0, 0.0, \"note\\nsur deux lignes\""_s));
        QVERIFY(s.contains(u"# Ce script utilise le module mevreg"_s) && s.contains(u"# import mevreg as mr"_s));
        QVERIFY(s.contains(u"class_ = "_s) && s.contains(u"class__ = "_s));
        verifie_export(s, uR"PY(
np.testing.assert_equal(arrondi, [0, 1, -3])
np.testing.assert_equal(petit, [0, 1, 1])
np.testing.assert_equal(I, [0, np.nan, 2])
np.testing.assert_equal(D, [0, np.nan, 0])
np.testing.assert_equal(d, [1])
)PY"_s);
    }

    void export_colonnes_inegales()
    {
        Seance ws;
        ws.meta.title = u"Relevé de mesures\nSérie du matin"_s;
        auto &d = ws.doc;
        d.ajoute_experimentale(u"x"_s, {}, {1, 2, 3});
        d.ajoute_experimentale(u"y"_s, {}, {4, 5});
        d.ajoute_experimentale(u"vide"_s, {}, {});
        d.ajoute_experimentale(u"seule"_s, {}, {2});
        const QStringList formules{
            u"x+y"_s, u"x-y"_s, u"(x+y)*y"_s, u"x/y"_s, u"y^x"_s,
            u"min(x,y)"_s, u"max(y,x)"_s, u"atan2(x,y)"_s, u"pow(x,y)"_s,
            u"x+seule"_s, u"2+vide"_s, u"vide-x"_s, u"min(vide)"_s,
            u"sum(y)+x"_s, u"len(y)+x"_s, u"len(sum(x))"_s, u"index(sum(x))"_s,
            u"max(index(x))"_s, u"sum(x/(y-4))"_s, u"mean(x/(y-4))"_s,
            u"deriv(x,y)"_s, u"integ(y,x)"_s, u"deriv(x,seule)"_s, u"integ(vide,seule)"_s
        };
        QString verifications;
        for (qsizetype i = 0; i < formules.size(); ++i) {
            const QString nom = u"resultat%1"_s.arg(i);
            const auto id = d.ajoute_calculee(nom, {}, formules[i]);
            QVERIFY(id);
            const Grandeur *q = d.obtient(*id);
            QVERIFY2(q->error.isEmpty(), qPrintable(q->error));
            QStringList attendu;
            for (double v : q->values)
                attendu << pyexport::nombre_python(v);
            verifications += u"np.testing.assert_allclose(np.atleast_1d(%1), [%2], equal_nan=True, err_msg='%3')\n"_s
                                .arg(nom, attendu.join(u", "_s), formules[i]);
        }
        d.ajoute_calculee(u"invalide"_s, {}, u"mesure_absente + 1"_s);
        d.ajoute_calculee(u"bloquee"_s, {}, u"sum(invalide)"_s);
        verifications += u"assert invalide.size == bloquee.size == 0\n"_s;
        verifie_export(pyexport::exporte_script(ws), verifications);
    }

    void export_modele_personnel()
    {
        Seance ws;
        const auto t = *ws.doc.ajoute_experimentale(u"temps"_s, u"s"_s, {0, 1, 2, 3});
        const auto u = *ws.doc.ajoute_experimentale(u"U"_s, u"V"_s, {1.1, 2.8, 5.2, 6.9});
        ws.doc.ajoute_experimentale(u"xm"_s, {}, {17, 18});
        ws.doc.ajoute_parametre(u"decalage"_s, u"V"_s, 1);
        Plot &plot = ws.graphe_modifiable(ws.ajoute_graphe())->plots.front();
        plot.ajoute_courbe(t, u);
        fit::ModelKind type{fit::ModelKind::User};
        type.expression = u"U = xm2*temps+b+decalage"_s;
        auto &modele = plot.fits.emplace_back(FitSpec::fabrique(type, t, u, PALETTE[1]));
        modele.label = u"étalonnage"_s;
        modele.draw = FitDraw::Custom;
        modele.drawRange = {0, 3};
        maj_modeles(plot, ws.doc);
        QVERIFY2(modele.result, qPrintable(modele.resultError));
        verifie_export(pyexport::exporte_script(ws), uR"PY(
x_modele, y_modele = plt.courbes["étalonnage"]
np.testing.assert_allclose(x_modele, np.linspace(0, 3, 300))
np.testing.assert_allclose(y_modele, 1.98*x_modele + 1.03)
np.testing.assert_equal(xm, [17, 18])
)PY"_s);
    }

    // --- annotations, incertitudes côté par côté, étendue et nom des modèles ---
    void annotations_cotes_et_trace_modele()
    {
        Document doc;
        const QId t = *doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2, 3, 4});
        const QId x = *doc.ajoute_experimentale(u"x"_s, u"m"_s, {1, 3, 5, 7, 9});
        const QId up = *doc.ajoute_experimentale(u"haut"_s, u"m"_s, {0.5});
        const QId down = *doc.ajoute_experimentale(u"bas"_s, u"m"_s, {0.2});
        Plot plot;
        CurveSpec &c = plot.ajoute_courbe(t, x);
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
        FitSpec f = FitSpec::fabrique(affine, t, x, PALETTE[3]);
        f.label = u"droite d'étalonnage"_s;
        f.draw = FitDraw::Custom;
        f.drawRange = {-2, 10};
        plot.fits.push_back(f);
        // enregistrées et relues telles quelles
        const Plot back = trace_depuis_json(trace_en_json(plot));
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
            q.retire_courbe(0);
            QVERIFY(q.curves.empty() && q.tangents.empty() && q.fits.empty());
        }
        // grandeur d'une incertitude utilisée par le graphe ; supprimée : la courbe reste
        QVERIFY(plot.utilise(up));
        Plot removed = plot;
        removed.retire_grandeur(up);
        QCOMPARE(removed.curves.size(), size_t(1));
        QVERIFY(!removed.curves[0].errYHigh && removed.curves[0].errYLow);
        // valeurs à tracer : incertitudes de chaque côté, droite par deux points, fonction du modèle
        maj_modeles(plot, doc);
        const PlotData data = resout(plot, doc, NumberStyle{});
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
        maj_modeles(plot, doc);
        QVERIFY(!resout(plot, doc, NumberStyle{}).series.back().function);
    }

    void ajustement_suit_ses_entrees()
    {
        Document doc;
        const QId t = *doc.ajoute_experimentale(u"t"_s, {}, {0, 1, 2, 3, 4});
        const QId mesures = *doc.ajoute_experimentale(u"U"_s, {}, {1, 3, 5, 7, 10});
        const QId y = *doc.ajoute_calculee(u"Uc"_s, {}, u"U*2"_s);
        const QId temperature = *doc.ajoute_experimentale(u"temperature"_s, {}, {20});
        const auto offset = *doc.ajoute_parametre(u"offset"_s, {}, 1);
        const auto autre = *doc.ajoute_parametre(u"pression"_s, {}, 1013);
        Plot plot;
        fit::ModelKind personnel{fit::ModelKind::User};
        personnel.expression = u"a*t + offset"_s;
        plot.fits.push_back(FitSpec::fabrique(personnel, t, y, PALETTE[0]));
        FitSpec &modele = plot.fits.front();
        auto verifie = [&] {
            QCOMPARE(maj_modeles(plot, doc), size_t(1));
            FitSpec temoin = modele;
            calcule_modele(temoin, doc);
            QVERIFY(temoin.result && modele.result);
            QCOMPARE(modele.result->params, temoin.result->params);
            QCOMPARE(modele.result->residualStd, temoin.result->residualStd);
        };
        verifie();
        for (int i = 0; i < 5; ++i) {
            doc.definit_valeur(temperature, 0, 21.0 + i);
            QCOMPARE(maj_modeles(plot, doc), size_t(0));
        }
        doc.definit_valeur_parametre(autre, 1000);
        QCOMPARE(maj_modeles(plot, doc), size_t(0));
        doc.definit_valeur(mesures, 1, 4);
        verifie();
        doc.definit_valeur(t, 1, 1.2);
        verifie();
        modele.range = {1, 4};
        verifie();
        doc.definit_valeur_parametre(offset, 2);
        verifie();
        doc.renomme_grandeur(t, u"temps"_s);
        QCOMPARE(maj_modeles(plot, doc), size_t(1));
        QVERIFY(!modele.result && !modele.resultError.isEmpty());
        doc.renomme_grandeur(t, u"t"_s);
        verifie();
    }

    // --- valeurs à tracer ---
    void resolution_courbes_modeles()
    {
        Document doc;
        const QId t = *doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2, 3, 4});
        const QId x = *doc.ajoute_experimentale(u"x"_s, u"m"_s, {1, 3.1, 4.9, 7, 9});
        doc.cree_incertitude(x, u"0.2"_s);
        Plot plot;
        plot.ajoute_courbe(t, x).errors = ErrorBars::Vertical;
        fit::ModelKind affine;
        affine.type = fit::ModelKind::Affine;
        plot.fits.push_back(FitSpec::fabrique(affine, t, x, PALETTE[3]));
        maj_modeles(plot, doc);
        QVERIFY(plot.fits[0].result);
        QVERIFY(std::abs(plot.fits[0].result->params[0] - 2.0) < 0.05);
        QCOMPARE(plot.fits[0].paramNames, (QStringList{u"a"_s, u"b"_s}));
        const PlotData data = resout(plot, doc, NumberStyle{});
        QCOMPARE(data.series.size(), size_t(2));
        QCOMPARE(data.series[0].uy, std::vector<double>(5, 0.2));
        QCOMPARE(data.xName, u"t"_s);
        QCOMPARE(data.xUnit, u"s"_s);
        QVERIFY(!data.series[1].autoscale);
        // pas de nouvel ajustement si rien n'a changé ; plage de modélisation
        const auto rev = plot.fits[0].fittedRevision;
        maj_modeles(plot, doc);
        QCOMPARE(plot.fits[0].fittedRevision, rev);
        plot.fits[0].range = std::make_pair(2.0, 4.0);
        maj_modeles(plot, doc);
        QCOMPARE(plot.fits[0].result->n, size_t(3));
        // grandeurs absentes : ignorées
        Plot empty;
        empty.ajoute_courbe(10, 11);
        empty.fits.push_back(FitSpec::fabrique(affine, 10, 11, PALETTE[1]));
        maj_modeles(empty, Document{});
        QVERIFY(!empty.fits[0].result);
        QVERIFY(resout(empty, Document{}, NumberStyle{}).series.empty());
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
