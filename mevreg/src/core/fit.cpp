#include "core/fit.h"

#include "core/fft.h"
#include "core/linalg.h"

#include <QLocale>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace Qt::StringLiterals;

namespace fit {

using std::numbers::pi;
using linalg::Matrix;

// --- types de modèles -----------------------------------------------------------------------

std::vector<ModelKind> ModelKind::catalog()
{
    std::vector<ModelKind> out;
    for (int t = Constant; t <= User; ++t) {
        ModelKind k;
        k.type = static_cast<Type>(t);
        out.push_back(k);
    }
    return out;
}

QString ModelKind::label() const
{
    switch (type) {
    case Constant: return u"Constante"_s;
    case Linear: return u"Linéaire"_s;
    case Affine: return u"Affine"_s;
    case Polynomial: return u"Polynôme"_s;
    case Parabola: return u"Parabole"_s;
    case Power: return u"Puissance"_s;
    case ExpX: return u"Exponentiel en x"_s;
    case ExpInvX: return u"Exponentiel en 1/x"_s;
    case Charge: return u"Charge (montée exp.)"_s;
    case Discharge: return u"Décharge (décroiss. exp.)"_s;
    case Ln: return u"Log népérien"_s;
    case Log10: return u"Log décimal"_s;
    case Sine: return u"Sinus"_s;
    case Cosine: return u"Cosinus"_s;
    case DampedSine: return u"Sinus amorti"_s;
    case DampedCosine: return u"Cosinus amorti"_s;
    case User: return u"Modèle utilisateur"_s;
    }
    return {};
}

QString ModelKind::templ() const
{
    switch (type) {
    case Constant: return u"y = a"_s;
    case Linear: return u"y = a·x"_s;
    case Affine: return u"y = a·x + b"_s;
    case Polynomial: {
        QString s = u"y = a0 + a1·x"_s;
        if (degree >= 2)
            s += u" + … + a%1·x^%1"_s.arg(degree);
        return s;
    }
    case Parabola: return u"y = a·x² + b·x + c"_s;
    case Power: return u"y = a·x^n"_s;
    case ExpX: return u"y = A·exp(k·x)"_s;
    case ExpInvX: return u"y = A·exp(k/x)"_s;
    case Charge: return u"y = A·(1 − exp(−(x − Δ)/τ)) + V0"_s;
    case Discharge: return u"y = A·exp(−(x − Δ)/τ) + V0"_s;
    case Ln: return u"y = a·ln(x) + b"_s;
    case Log10: return u"y = a·log(x) + b"_s;
    case Sine: return u"y = A·sin(2π·x/T + φ) + B"_s;
    case Cosine: return u"y = A·cos(2π·x/T + φ) + B"_s;
    case DampedSine: return u"y = A·exp(−x/τ)·sin(2π·x/T + φ) + B"_s;
    case DampedCosine: return u"y = A·exp(−x/τ)·cos(2π·x/T + φ) + B"_s;
    case User: return u"y = "_s + expression;
    }
    return {};
}

std::vector<std::pair<QString, double>> ModelKind::defaultFixed() const
{
    if (type == Charge || type == Discharge)
        return {{u"Δ"_s, 0.0}};
    return {};
}

bool ModelKind::isLinear() const
{
    switch (type) {
    case Constant: case Linear: case Affine: case Polynomial: case Parabola: case Ln: case Log10:
        return true;
    default:
        return false;
    }
}

static const char16_t *const TYPE_NAMES[] = {
    u"Constant", u"Linear", u"Affine", u"Polynomial", u"Parabola", u"Power", u"ExpX", u"ExpInvX", u"Charge",
    u"Discharge", u"Ln", u"Log10", u"Sine", u"Cosine", u"DampedSine", u"DampedCosine", u"User",
};

QString ModelKind::typeName() const
{
    return QString::fromUtf16(TYPE_NAMES[type]);
}

std::optional<ModelKind::Type> ModelKind::typeFromName(const QString &name)
{
    for (int t = Constant; t <= User; ++t) {
        if (name == QString::fromUtf16(TYPE_NAMES[t]))
            return static_cast<Type>(t);
    }
    return std::nullopt;
}

static QStringList builtinParams(const ModelKind &k)
{
    switch (k.type) {
    case ModelKind::Constant:
    case ModelKind::Linear: return {u"a"_s};
    case ModelKind::Affine:
    case ModelKind::Ln:
    case ModelKind::Log10: return {u"a"_s, u"b"_s};
    case ModelKind::Polynomial: {
        QStringList p;
        for (int i = 0; i <= k.degree; ++i)
            p << u"a%1"_s.arg(i);
        return p;
    }
    case ModelKind::Parabola: return {u"a"_s, u"b"_s, u"c"_s};
    case ModelKind::Power: return {u"a"_s, u"n"_s};
    case ModelKind::ExpX:
    case ModelKind::ExpInvX: return {u"A"_s, u"k"_s};
    case ModelKind::Charge:
    case ModelKind::Discharge: return {u"A"_s, u"Δ"_s, u"τ"_s, u"V0"_s};
    case ModelKind::Sine:
    case ModelKind::Cosine: return {u"A"_s, u"T"_s, u"φ"_s, u"B"_s};
    case ModelKind::DampedSine:
    case ModelKind::DampedCosine: return {u"A"_s, u"τ"_s, u"T"_s, u"φ"_s, u"B"_s};
    case ModelKind::User: return {};
    }
    return {};
}

// --- modèle ----------------------------------------------------------------------------------

std::optional<Model> Model::make(const ModelKind &kind, const QString &xname, const std::vector<std::pair<QString, double>> &constants, QString *error)
{
    auto fail = [&](const QString &m) -> std::optional<Model> {
        if (error)
            *error = m;
        return std::nullopt;
    };
    Model m;
    m.m_kind = kind;
    if (kind.type == ModelKind::User) {
        QString src = kind.expression;
        if (auto def = expr::splitDefinition(kind.expression))
            src = def->second;
        auto parsed = expr::parse(src);
        if (!parsed.ok())
            return fail(parsed.error.message);
        bool usesX = false;
        for (const QString &v : parsed.expr->variables()) {
            if (v == xname || (v == u"x" && xname.isEmpty())) {
                usesX = true;
            } else if (std::any_of(constants.begin(), constants.end(), [&](const auto &c) { return c.first == v; }) || expr::constant(v)) {
                continue;
            } else {
                m.m_params << v;
            }
        }
        if (!usesX)
            return fail(u"l'expression doit utiliser la variable « %1 »"_s.arg(xname));
        if (m.m_params.isEmpty())
            return fail(u"l'expression n'a aucun paramètre à ajuster"_s);
        m.m_userExpr = parsed.expr;
        m.m_xname = xname;
        m.m_constants = constants;
        return m;
    }
    if (kind.type == ModelKind::Polynomial && kind.degree > 10)
        return fail(u"degré maximal : 10"_s);
    if (kind.type == ModelKind::Polynomial && kind.degree < 1)
        return fail(u"degré minimal : 1"_s);
    m.m_params = builtinParams(kind);
    return m;
}

std::vector<double> Model::eval(const std::vector<double> &p, const std::vector<double> &x) const
{
    if (m_userExpr) {
        const QString xname = m_xname;
        auto env = [&](const QString &name) -> std::optional<expr::Value> {
            if (name == xname || (xname.isEmpty() && name == u"x"))
                return expr::Value::vector(x);
            const qsizetype i = m_params.indexOf(name);
            if (i >= 0)
                return expr::Value::number(p[i]);
            for (const auto &[n, v] : m_constants) {
                if (n == name)
                    return expr::Value::number(v);
            }
            return std::nullopt;
        };
        auto v = m_userExpr->eval(env);
        if (!v)
            return std::vector<double>(x.size(), NAN);
        if (v->scalar)
            return std::vector<double>(x.size(), v->s);
        auto out = v->v;
        out.resize(x.size(), NAN);
        return out;
    }
    std::vector<double> out(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        out[i] = eval1(p, x[i]);
    return out;
}

double Model::eval1(const std::vector<double> &p, double x) const
{
    switch (m_kind.type) {
    case ModelKind::Constant: return p[0];
    case ModelKind::Linear: return p[0] * x;
    case ModelKind::Affine: return p[0] * x + p[1];
    case ModelKind::Polynomial: {
        double acc = 0.0;
        for (size_t i = p.size(); i-- > 0;)
            acc = acc * x + p[i];
        return acc;
    }
    case ModelKind::Parabola: return (p[0] * x + p[1]) * x + p[2];
    case ModelKind::Power: return p[0] * std::pow(x, p[1]);
    case ModelKind::ExpX: return p[0] * std::exp(p[1] * x);
    case ModelKind::ExpInvX: return p[0] * std::exp(p[1] / x);
    case ModelKind::Charge: return p[0] * (1.0 - std::exp(-(x - p[1]) / p[2])) + p[3];
    case ModelKind::Discharge: return p[0] * std::exp(-(x - p[1]) / p[2]) + p[3];
    case ModelKind::Ln: return p[0] * std::log(x) + p[1];
    case ModelKind::Log10: return p[0] * std::log10(x) + p[1];
    case ModelKind::Sine: return p[0] * std::sin(2.0 * pi * x / p[1] + p[2]) + p[3];
    case ModelKind::Cosine: return p[0] * std::cos(2.0 * pi * x / p[1] + p[2]) + p[3];
    case ModelKind::DampedSine: return p[0] * std::exp(-x / p[1]) * std::sin(2.0 * pi * x / p[2] + p[3]) + p[4];
    case ModelKind::DampedCosine: return p[0] * std::exp(-x / p[1]) * std::cos(2.0 * pi * x / p[2] + p[3]) + p[4];
    case ModelKind::User: return NAN;
    }
    return NAN;
}

std::optional<std::vector<double>> Model::basis(double x) const
{
    switch (m_kind.type) {
    case ModelKind::Constant: return std::vector<double>{1.0};
    case ModelKind::Linear: return std::vector<double>{x};
    case ModelKind::Affine: return std::vector<double>{x, 1.0};
    case ModelKind::Polynomial: {
        std::vector<double> b;
        for (int i = 0; i <= m_kind.degree; ++i)
            b.push_back(std::pow(x, i));
        return b;
    }
    case ModelKind::Parabola: return std::vector<double>{x * x, x, 1.0};
    case ModelKind::Ln: return std::vector<double>{std::log(x), 1.0};
    case ModelKind::Log10: return std::vector<double>{std::log10(x), 1.0};
    default: return std::nullopt;
    }
}

QString Model::userSource() const
{
    if (m_kind.type != ModelKind::User)
        return {};
    if (auto def = expr::splitDefinition(m_kind.expression))
        return def->second;
    return m_kind.expression;
}

QString Model::display(const std::vector<double> &p, const QString &xname, const QString &yname, NumberStyle style) const
{
    auto v = [&](size_t i) { return formatSig(p[i], 4, style); };
    // valeur suivie d'un terme : « + 3,2 » ou « − 3,2 »
    auto signedv = [&](size_t i) { return p[i] < 0 ? u"− "_s + formatSig(-p[i], 4, style) : u"+ "_s + v(i); };
    auto shift = [&](size_t i) {
        if (p[i] == 0.0)
            return xname;
        if (p[i] < 0)
            return u"(%1 + %2)"_s.arg(xname, formatSig(-p[i], 4, style));
        return u"(%1 − %2)"_s.arg(xname, v(i));
    };
    QString rhs;
    switch (m_kind.type) {
    case ModelKind::Constant: rhs = v(0); break;
    case ModelKind::Linear: rhs = u"%1·%2"_s.arg(v(0), xname); break;
    case ModelKind::Affine: rhs = u"%1·%2 %3"_s.arg(v(0), xname, signedv(1)); break;
    case ModelKind::Polynomial:
        rhs = v(0);
        for (int i = 1; i <= m_kind.degree; ++i) {
            const QString pw = i == 1 ? QString() : superscript(QString::number(i));
            rhs += u" %1·%2%3"_s.arg(signedv(i), xname, pw);
        }
        break;
    case ModelKind::Parabola: rhs = u"%1·%2² %3·%2 %4"_s.arg(v(0), xname, signedv(1), signedv(2)); break;
    case ModelKind::Power: rhs = u"%1·%2^%3"_s.arg(v(0), xname, v(1)); break;
    case ModelKind::ExpX: rhs = u"%1·exp(%2·%3)"_s.arg(v(0), v(1), xname); break;
    case ModelKind::ExpInvX: rhs = u"%1·exp(%2/%3)"_s.arg(v(0), v(1), xname); break;
    case ModelKind::Charge: rhs = u"%1·(1 − exp(−%2/%3)) %4"_s.arg(v(0), shift(1), v(2), signedv(3)); break;
    case ModelKind::Discharge: rhs = u"%1·exp(−%2/%3) %4"_s.arg(v(0), shift(1), v(2), signedv(3)); break;
    case ModelKind::Ln: rhs = u"%1·ln(%2) %3"_s.arg(v(0), xname, signedv(1)); break;
    case ModelKind::Log10: rhs = u"%1·log(%2) %3"_s.arg(v(0), xname, signedv(1)); break;
    case ModelKind::Sine: rhs = u"%1·sin(2π·%2/%3 %4) %5"_s.arg(v(0), xname, v(1), signedv(2), signedv(3)); break;
    case ModelKind::Cosine: rhs = u"%1·cos(2π·%2/%3 %4) %5"_s.arg(v(0), xname, v(1), signedv(2), signedv(3)); break;
    case ModelKind::DampedSine:
        rhs = u"%1·exp(−%2/%3)·sin(2π·%2/%4 %5) %6"_s.arg(v(0), xname, v(1), v(2), signedv(3), signedv(4));
        break;
    case ModelKind::DampedCosine:
        rhs = u"%1·exp(−%2/%3)·cos(2π·%2/%4 %5) %6"_s.arg(v(0), xname, v(1), v(2), signedv(3), signedv(4));
        break;
    case ModelKind::User:
        rhs = userSource();
        for (qsizetype i = 0; i < m_params.size() && static_cast<size_t>(i) < p.size(); ++i)
            rhs = expr::renameIdentifier(rhs, m_params[i], formatSig(p[i], 4, style));
        break;
    }
    return yname + u" = "_s + rhs;
}

/// Nombre écrit pour une formule : point décimal, négatif entre parenthèses.
static QString num(double v)
{
    const QString e = QString::number(v, 'e', QLocale::FloatingPointShortest);
    const qsizetype k = e.indexOf(u'e');
    const QString mant = k >= 0 ? e.left(k) : e;
    const int exp = k >= 0 ? e.mid(k + 1).toInt() : 0;
    const QString txt = (exp >= -4 && exp < 6) ? QString::number(v, 'f', QLocale::FloatingPointShortest) : mant + u'e' + QString::number(exp);
    return v < 0 ? u'(' + txt + u')' : txt;
}

QString Model::formula(const std::vector<double> &p, const QString &x) const
{
    auto n = [&](size_t i) { return num(p[i]); };
    switch (m_kind.type) {
    case ModelKind::Constant: return u"%1+0*%2"_s.arg(n(0), x);
    case ModelKind::Linear: return u"%1*%2"_s.arg(n(0), x);
    case ModelKind::Affine: return u"%1*%2+%3"_s.arg(n(0), x, n(1));
    case ModelKind::Polynomial: {
        QString s = n(0);
        for (int i = 1; i <= m_kind.degree; ++i)
            s += u"+%1*%2^%3"_s.arg(n(i), x).arg(i);
        return s;
    }
    case ModelKind::Parabola: return u"%1*%2^2+%3*%2+%4"_s.arg(n(0), x, n(1), n(2));
    case ModelKind::Power: return u"%1*%2^%3"_s.arg(n(0), x, n(1));
    case ModelKind::ExpX: return u"%1*exp(%2*%3)"_s.arg(n(0), n(1), x);
    case ModelKind::ExpInvX: return u"%1*exp(%2/%3)"_s.arg(n(0), n(1), x);
    case ModelKind::Charge: return u"%1*(1-exp(-(%2-%3)/%4))+%5"_s.arg(n(0), x, n(1), n(2), n(3));
    case ModelKind::Discharge: return u"%1*exp(-(%2-%3)/%4)+%5"_s.arg(n(0), x, n(1), n(2), n(3));
    case ModelKind::Ln: return u"%1*ln(%2)+%3"_s.arg(n(0), x, n(1));
    case ModelKind::Log10: return u"%1*log(%2)+%3"_s.arg(n(0), x, n(1));
    case ModelKind::Sine: return u"%1*sin(2*pi*%2/%3+%4)+%5"_s.arg(n(0), x, n(1), n(2), n(3));
    case ModelKind::Cosine: return u"%1*cos(2*pi*%2/%3+%4)+%5"_s.arg(n(0), x, n(1), n(2), n(3));
    case ModelKind::DampedSine: return u"%1*exp(-%2/%3)*sin(2*pi*%2/%4+%5)+%6"_s.arg(n(0), x, n(1), n(2), n(3), n(4));
    case ModelKind::DampedCosine: return u"%1*exp(-%2/%3)*cos(2*pi*%2/%4+%5)+%6"_s.arg(n(0), x, n(1), n(2), n(3), n(4));
    case ModelKind::User: {
        QString s = userSource();
        for (qsizetype i = 0; i < m_params.size() && static_cast<size_t>(i) < p.size(); ++i)
            s = expr::renameIdentifier(s, m_params[i], u'(' + num(p[i]) + u')');
        return s;
    }
    }
    return {};
}

// --- ajustement --------------------------------------------------------------------------------

namespace {

double rSquared(const std::vector<double> &ys, double ssr)
{
    const double n = static_cast<double>(ys.size());
    double mean = 0.0;
    for (double y : ys)
        mean += y;
    mean /= n;
    double sst = 0.0;
    for (double y : ys)
        sst += (y - mean) * (y - mean);
    return sst > 0.0 ? 1.0 - ssr / sst : NAN;
}

QString domainError(const ModelKind &kind)
{
    switch (kind.type) {
    case ModelKind::Ln:
    case ModelKind::Log10: return u"le logarithme exige des abscisses strictement positives"_s;
    case ModelKind::Power: return u"le modèle puissance exige des abscisses positives"_s;
    case ModelKind::ExpInvX: return u"le modèle en 1/x exige des abscisses non nulles"_s;
    default: return u"le modèle n'est pas défini pour certaines abscisses"_s;
    }
}

bool isPoly(const Model &m)
{
    return m.kind().type == ModelKind::Polynomial || m.kind().type == ModelKind::Parabola;
}

/// Coefficients en ordre croissant (c0 + c1 u + …) à partir des paramètres.
std::vector<double> polyAscending(const Model &m, const std::vector<double> &p)
{
    if (m.kind().type == ModelKind::Parabola)
        return {p[2], p[1], p[0]};
    return p;
}

/// Indice (ordre des paramètres) du coefficient de degré i.
std::vector<size_t> polyOrder(const Model &m)
{
    if (m.kind().type == ModelKind::Parabola)
        return {2, 1, 0};
    std::vector<size_t> o(m.params().size());
    for (size_t i = 0; i < o.size(); ++i)
        o[i] = i;
    return o;
}

double binom(size_t n, size_t k)
{
    double acc = 1.0;
    for (size_t i = 0; i < k; ++i)
        acc = acc * static_cast<double>(n - i) / static_cast<double>(i + 1);
    return acc;
}

/// Passe de Σ c_k u^k avec u = (x − m)/s à Σ d_i x^i ; renvoie aussi ∂d_i/∂c_k.
std::pair<std::vector<double>, std::vector<std::vector<double>>> unscalePoly(const std::vector<double> &c, double m, double s)
{
    const size_t k = c.size();
    std::vector<std::vector<double>> jac(k, std::vector<double>(k, 0.0));
    for (size_t j = 0; j < k; ++j) {
        for (size_t i = 0; i <= j; ++i)
            jac[i][j] = binom(j, i) * std::pow(-m, static_cast<double>(j - i)) / std::pow(s, static_cast<double>(j));
    }
    std::vector<double> d(k, 0.0);
    for (size_t i = 0; i < k; ++i) {
        for (size_t j = 0; j < k; ++j)
            d[i] += jac[i][j] * c[j];
    }
    return {d, jac};
}

double ssrOf(const Model &model, const std::vector<double> &p, const std::vector<double> &xs, const std::vector<double> &ys);

std::optional<FitResult> fitLinear(const Model &model, const std::vector<double> &xs, const std::vector<double> &ys,
                                   const std::vector<std::optional<double>> &fixed, const std::vector<size_t> &freeIdx, QString *error)
{
    const size_t n = xs.size();
    // abscisses centrées et réduites pour les polynômes (meilleur conditionnement)
    const bool poly = isPoly(model);
    const bool anyFixed = freeIdx.size() < static_cast<size_t>(model.params().size());
    double shift = 0.0, scale = 1.0;
    if (poly && !anyFixed) {
        double mean = 0.0;
        for (double x : xs)
            mean += x;
        mean /= static_cast<double>(n);
        double var = 0.0;
        for (double x : xs)
            var += (x - mean) * (x - mean);
        const double sd = std::sqrt(var / static_cast<double>(n));
        shift = mean;
        scale = sd > 0.0 ? sd : 1.0;
    }
    Matrix a = Matrix::zeros(n, freeIdx.size());
    std::vector<double> b(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        const double u = (xs[i] - shift) / scale;
        const auto basis = *model.basis(u);
        for (double v : basis) {
            if (!std::isfinite(v)) {
                if (error)
                    *error = domainError(model.kind());
                return std::nullopt;
            }
        }
        double target = ys[i];
        for (size_t j = 0; j < fixed.size(); ++j) {
            if (fixed[j])
                target -= *fixed[j] * basis[j];
        }
        b[i] = target;
        for (size_t c = 0; c < freeIdx.size(); ++c)
            a(i, c) = basis[freeIdx[c]];
    }
    const auto sol = linalg::leastSquares(a, b);
    if (!sol) {
        if (error)
            *error = u"les points ne permettent pas de déterminer les paramètres (abscisses toutes identiques ?)"_s;
        return std::nullopt;
    }
    std::vector<double> p(fixed.size());
    for (size_t j = 0; j < fixed.size(); ++j)
        p[j] = fixed[j].value_or(0.0);
    for (size_t c = 0; c < freeIdx.size(); ++c)
        p[freeIdx[c]] = (*sol)[c];
    const auto cov = linalg::inverseSpd(a.gram());
    std::vector<double> pFinal = p;
    std::vector<double> unc(p.size(), NAN);
    double ssr = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double r = ys[i] - model.eval1(p, (xs[i] - shift) / scale);
        ssr += r * r;
    }
    const size_t dof = n > freeIdx.size() ? n - freeIdx.size() : 0;
    const double s2 = dof > 0 ? ssr / static_cast<double>(dof) : NAN;
    bool imprecise = false;
    if (poly && !anyFixed) {
        // retour aux coefficients en x : p(u) avec u = (x − m)/s
        const auto [cx, jac] = unscalePoly(polyAscending(model, p), shift, scale);
        pFinal = polyAscending(model, cx); // la parabole s'écrit dans l'ordre inverse
        // Abscisses très loin de zéro (x ≈ 10⁹ pour des écarts de 1) : les coefficients en x perdent
        // les chiffres utiles, et le polynôme qu'ils donnent ne passe plus par les points. R² est
        // alors celui de ce polynôme, avec un avertissement.
        const double ssrX = ssrOf(model, pFinal, xs, ys);
        double mean = 0.0, sst = 0.0;
        for (double y : ys)
            mean += y / static_cast<double>(n);
        for (double y : ys)
            sst += (y - mean) * (y - mean);
        if (!(ssrX <= ssr + 1e-6 * sst)) {
            imprecise = true;
            ssr = ssrX;
        }
        if (cov) {
            const auto order = polyOrder(model);
            const size_t k = cx.size();
            for (size_t i = 0; i < k; ++i) {
                double var = 0.0;
                for (size_t aa = 0; aa < k; ++aa) {
                    for (size_t bb = 0; bb < k; ++bb)
                        var += jac[i][aa] * (*cov)(order[aa], order[bb]) * jac[i][bb];
                }
                unc[order[i]] = std::sqrt(var * s2);
            }
        }
    } else if (cov) {
        for (size_t c = 0; c < freeIdx.size(); ++c)
            unc[freeIdx[c]] = std::sqrt((*cov)(c, c) * s2);
    }
    FitResult r;
    r.params = pFinal;
    r.uncertainties = unc;
    r.r2 = rSquared(ys, ssr);
    r.residualStd = std::sqrt(s2);
    r.n = n;
    r.iterations = 1;
    if (!cov)
        r.warning = u"paramètres non indépendants : incertitudes indéterminées"_s;
    else if (imprecise)
        r.warning = u"abscisses trop loin de zéro : les coefficients sont imprécis ; décalez les abscisses (par exemple x − x₀)"_s;
    return r;
}

double ssrOf(const Model &model, const std::vector<double> &p, const std::vector<double> &xs, const std::vector<double> &ys)
{
    const auto f = model.eval(p, xs);
    double s = 0.0;
    for (size_t i = 0; i < f.size(); ++i) {
        const double r = ys[i] - f[i];
        s += r * r;
    }
    return std::isfinite(s) ? s : std::numeric_limits<double>::infinity();
}

/// Jacobienne numérique (différences centrées) par rapport aux paramètres libres.
Matrix jacobian(const Model &model, const std::vector<double> &p, const std::vector<double> &xs, const std::vector<size_t> &freeIdx)
{
    const size_t n = xs.size();
    Matrix j = Matrix::zeros(n, freeIdx.size());
    std::vector<double> q = p;
    for (size_t c = 0; c < freeIdx.size(); ++c) {
        const size_t k = freeIdx[c];
        // Pas relatif au paramètre : une constante de temps de 1 ns (en secondes) ne se dérive pas avec
        // un pas de 1e-9. Un pas qui ne change pas le modèle (petit paramètre ajouté à de grandes
        // valeurs) est agrandi jusqu'à ce que l'écart dépasse les erreurs d'arrondi.
        double h = p[k] != 0.0 ? 1e-6 * std::abs(p[k]) : 1e-9;
        std::vector<double> fp, fm;
        for (int tries = 0;; ++tries) {
            q[k] = p[k] + h;
            fp = model.eval(q, xs);
            q[k] = p[k] - h;
            fm = model.eval(q, xs);
            double change = 0.0, size = 0.0;
            for (size_t i = 0; i < n; ++i) {
                if (std::isfinite(fp[i]) && std::isfinite(fm[i])) {
                    change = std::max(change, std::abs(fp[i] - fm[i]));
                    size = std::max({size, std::abs(fp[i]), std::abs(fm[i])});
                }
            }
            if (change > 1e-9 * size || tries == 6)
                break;
            h *= 1000.0;
        }
        q[k] = p[k];
        for (size_t i = 0; i < n; ++i) {
            const double d = (fp[i] - fm[i]) / (2.0 * h);
            j(i, c) = std::isfinite(d) ? d : 0.0;
        }
    }
    return j;
}

std::optional<FitResult> levenbergMarquardt(const Model &model, const std::vector<double> &xs, const std::vector<double> &ys,
                                            std::vector<double> p, const std::vector<size_t> &freeIdx, QString *error)
{
    const size_t n = xs.size();
    double s = ssrOf(model, p, xs, ys);
    if (!std::isfinite(s)) {
        if (error)
            *error = domainError(model.kind());
        return std::nullopt;
    }
    double lambda = 1e-3;
    int iterations = 0;
    bool converged = false;
    while (iterations < 500) {
        ++iterations;
        const auto f = model.eval(p, xs);
        std::vector<double> r(n);
        for (size_t i = 0; i < n; ++i)
            r[i] = ys[i] - f[i];
        const Matrix jac = jacobian(model, p, xs, freeIdx);
        Matrix a = jac.gram();
        auto g = jac.tMulVec(r);
        double maxDiag = 0.0;
        for (size_t i = 0; i < freeIdx.size(); ++i)
            maxDiag = std::max(maxDiag, a(i, i));
        if (maxDiag == 0.0)
            break;
        // Système équilibré (diagonale de 1) : avec des paramètres d'ordres de grandeur très
        // différents (A ≈ 2 V et τ ≈ 1e-9 s), il paraîtrait singulier à la résolution. La solution est
        // la même : (JᵀJ + λ diag(JᵀJ)) δ = Jᵀr.
        std::vector<double> d(freeIdx.size(), 1.0);
        for (size_t i = 0; i < freeIdx.size(); ++i) {
            if (a(i, i) > 0.0 && std::isfinite(a(i, i)))
                d[i] = std::sqrt(a(i, i));
        }
        for (size_t i = 0; i < freeIdx.size(); ++i) {
            g[i] /= d[i];
            for (size_t j = 0; j < freeIdx.size(); ++j)
                a(i, j) /= d[i] * d[j];
        }
        bool improved = false;
        while (lambda < 1e16) {
            Matrix m = a;
            for (size_t i = 0; i < freeIdx.size(); ++i)
                m(i, i) += lambda * std::max(a(i, i), 1e-12);
            if (auto delta = linalg::solve(m, g)) {
                std::vector<double> q = p;
                for (size_t c = 0; c < freeIdx.size(); ++c)
                    q[freeIdx[c]] += (*delta)[c] / d[c];
                const double sNew = ssrOf(model, q, xs, ys);
                if (sNew < s) {
                    bool smallStep = true;
                    for (size_t c = 0; c < freeIdx.size(); ++c) {
                        if (std::abs((*delta)[c] / d[c]) > 1e-10 * (std::abs(p[freeIdx[c]]) + 1e-12))
                            smallStep = false;
                    }
                    const double rel = (s - sNew) / std::max(s, std::numeric_limits<double>::min());
                    p = q;
                    s = sNew;
                    lambda = std::max(lambda / 10.0, 1e-15);
                    improved = true;
                    if (rel < 1e-14 || smallStep || s == 0.0)
                        converged = true;
                    break;
                }
            }
            lambda *= 10.0;
        }
        if (!improved)
            converged = true; // plus aucune amélioration possible : minimum atteint
        if (converged)
            break;
    }
    const size_t dof = n > freeIdx.size() ? n - freeIdx.size() : 0;
    const double s2 = dof > 0 ? s / static_cast<double>(dof) : NAN;
    const auto cov = linalg::inverseSpd(jacobian(model, p, xs, freeIdx).gram());
    std::vector<double> unc(p.size(), NAN);
    if (cov) {
        for (size_t c = 0; c < freeIdx.size(); ++c)
            unc[freeIdx[c]] = std::sqrt((*cov)(c, c) * s2);
    }
    FitResult res;
    res.params = p;
    res.uncertainties = unc;
    res.r2 = rSquared(ys, s);
    res.residualStd = std::sqrt(s2);
    res.n = n;
    res.iterations = iterations;
    if (!cov)
        res.warning = u"paramètres non indépendants : fixez-en un pour obtenir les incertitudes"_s;
    else if (!converged)
        res.warning = u"la convergence n'est pas assurée : vérifiez les valeurs initiales"_s;
    return res;
}

double wrapPhase(double phi)
{
    double v = std::fmod(phi + pi, 2.0 * pi);
    if (v < 0)
        v += 2.0 * pi;
    v -= pi;
    if (v <= -pi)
        v += 2.0 * pi;
    return v;
}

/// Ramène amplitude et période à des valeurs positives, phase dans ]−π, π].
void normalize(const Model &model, std::vector<double> &p)
{
    size_t ia = 0, it, iphi;
    bool isSin;
    switch (model.kind().type) {
    case ModelKind::Sine: it = 1; iphi = 2; isSin = true; break;
    case ModelKind::Cosine: it = 1; iphi = 2; isSin = false; break;
    case ModelKind::DampedSine: it = 2; iphi = 3; isSin = true; break;
    case ModelKind::DampedCosine: it = 2; iphi = 3; isSin = false; break;
    default: return;
    }
    if (p[it] < 0.0) {
        // sin(−ωx + φ) = −sin(ωx − φ) ; cos(−ωx + φ) = cos(ωx − φ)
        p[it] = -p[it];
        p[iphi] = -p[iphi];
        if (isSin)
            p[ia] = -p[ia];
    }
    if (p[ia] < 0.0) {
        p[ia] = -p[ia];
        p[iphi] += pi;
    }
    p[iphi] = wrapPhase(p[iphi]);
}

/// Moindres carrés linéaires sur des fonctions de base ; renvoie (coefficients, Σr²).
std::optional<std::pair<std::vector<double>, double>> linearFit(const std::vector<std::vector<double>> &basis, const std::vector<double> &ys)
{
    const size_t n = ys.size(), p = basis.size();
    Matrix a = Matrix::zeros(n, p);
    for (size_t j = 0; j < p; ++j) {
        for (size_t i = 0; i < n; ++i) {
            if (!std::isfinite(basis[j][i]))
                return std::nullopt;
            a(i, j) = basis[j][i];
        }
    }
    const auto c = linalg::leastSquares(a, ys);
    if (!c)
        return std::nullopt;
    double ssr = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double f = 0.0;
        for (size_t j = 0; j < p; ++j)
            f += (*c)[j] * basis[j][i];
        ssr += (ys[i] - f) * (ys[i] - f);
    }
    return std::make_pair(*c, ssr);
}

std::vector<double> logspace(double a, double b, size_t n)
{
    std::vector<double> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = std::pow(10.0, a + (b - a) * static_cast<double>(i) / static_cast<double>(n - 1));
    return out;
}

double span(const std::vector<double> &xs)
{
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (double v : xs) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return hi > lo ? hi - lo : 1.0;
}

} // namespace

std::vector<double> initialGuess(const Model &model, const std::vector<double> &xs, const std::vector<double> &ys, const std::vector<std::optional<double>> &fixed)
{
    const size_t np = model.params().size();
    auto fix = [&](size_t i) -> std::optional<double> { return i < fixed.size() ? fixed[i] : std::nullopt; };
    const std::vector<double> ones(xs.size(), 1.0);
    const double w = span(xs);
    switch (model.kind().type) {
    case ModelKind::Power: {
        std::pair<double, std::vector<double>> best{std::numeric_limits<double>::infinity(), {1.0, 1.0}};
        std::vector<double> candidates;
        if (auto f = fix(1))
            candidates = {*f};
        else
            for (int i = -16; i <= 16; ++i)
                candidates.push_back(i * 0.25);
        for (double nn : candidates) {
            std::vector<double> b(xs.size());
            for (size_t i = 0; i < xs.size(); ++i)
                b[i] = std::pow(xs[i], nn);
            if (auto r = linearFit({b}, ys); r && r->second < best.first)
                best = {r->second, {r->first[0], nn}};
        }
        return best.second;
    }
    case ModelKind::ExpX:
    case ModelKind::ExpInvX: {
        const bool inv = model.kind().type == ModelKind::ExpInvX;
        double scale;
        if (inv) {
            std::vector<double> a(xs.size());
            for (size_t i = 0; i < xs.size(); ++i)
                a[i] = std::abs(xs[i]);
            std::sort(a.begin(), a.end());
            scale = std::max(a[a.size() / 2], 1e-300);
        } else {
            scale = 1.0 / w;
        }
        std::vector<double> ks{0.0};
        if (auto k = fix(1)) {
            ks = {*k};
        } else {
            for (double m : logspace(-2.0, 1.5, 36)) {
                ks.push_back(m * scale);
                ks.push_back(-m * scale);
            }
        }
        std::pair<double, std::vector<double>> best{std::numeric_limits<double>::infinity(), {1.0, 0.0}};
        for (double k : ks) {
            std::vector<double> b(xs.size());
            for (size_t i = 0; i < xs.size(); ++i)
                b[i] = inv ? std::exp(k / xs[i]) : std::exp(k * xs[i]);
            if (auto r = linearFit({b}, ys); r && r->second < best.first)
                best = {r->second, {r->first[0], k}};
        }
        return best.second;
    }
    case ModelKind::Charge:
    case ModelKind::Discharge: {
        const bool charge = model.kind().type == ModelKind::Charge;
        const double delta = fix(1).value_or(0.0);
        std::vector<double> taus;
        if (auto t = fix(2))
            taus = {*t};
        else
            for (double m : logspace(-2.5, 1.0, 50))
                taus.push_back(m * w);
        std::pair<double, std::vector<double>> best{std::numeric_limits<double>::infinity(), {1.0, delta, w / 5.0, 0.0}};
        for (double tau : taus) {
            std::vector<double> e(xs.size());
            for (size_t i = 0; i < xs.size(); ++i)
                e[i] = std::exp(-(xs[i] - delta) / tau);
            if (auto r = linearFit({ones, e}, ys); r && r->second < best.first) {
                const auto &c = r->first;
                // charge : y = (V0 + A) − A·e ; décharge : y = V0 + A·e
                const double a = charge ? -c[1] : c[1];
                const double v0 = charge ? c[0] + c[1] : c[0];
                best = {r->second, {a, delta, tau, v0}};
            }
        }
        return best.second;
    }
    case ModelKind::Sine:
    case ModelKind::Cosine:
    case ModelKind::DampedSine:
    case ModelKind::DampedCosine: {
        const auto t = model.kind().type;
        const bool damped = t == ModelKind::DampedSine || t == ModelKind::DampedCosine;
        const bool isSin = t == ModelKind::Sine || t == ModelKind::DampedSine;
        const size_t iT = damped ? 2 : 1;
        std::vector<double> periods;
        if (auto ft = fix(iT)) {
            periods = {*ft};
        } else if (auto f = fft::dominantFrequency(xs, ys); f && *f > 0.0) {
            for (int i = 0; i < 41; ++i)
                periods.push_back((0.8 + 0.4 * i / 40.0) / *f);
        } else {
            for (double m : logspace(-1.5, 0.7, 60))
                periods.push_back(m * w);
        }
        std::vector<double> taus{std::numeric_limits<double>::infinity()};
        if (damped) {
            if (auto ft = fix(1)) {
                taus = {*ft};
            } else {
                taus.clear();
                for (double m : logspace(-1.3, 1.3, 27))
                    taus.push_back(m * w);
                taus.push_back(1e6 * w);
            }
        }
        std::pair<double, std::vector<double>> best{std::numeric_limits<double>::infinity(), std::vector<double>(np, 0.0)};
        for (double per : periods) {
            const double om = 2.0 * pi / per;
            for (double tau : taus) {
                std::vector<double> s(xs.size()), c(xs.size());
                for (size_t i = 0; i < xs.size(); ++i) {
                    const double env = std::isfinite(tau) ? std::exp(-xs[i] / tau) : 1.0;
                    s[i] = env * std::sin(om * xs[i]);
                    c[i] = env * std::cos(om * xs[i]);
                }
                auto r = linearFit({ones, s, c}, ys);
                if (!r || !(r->second < best.first))
                    continue;
                const double bs = r->first[1], bc = r->first[2];
                const double amp = std::hypot(bs, bc);
                // sin(ωx + φ) = cosφ·sin ωx + sinφ·cos ωx ; cos(ωx + φ) = cosφ·cos ωx − sinφ·sin ωx
                const double phi = isSin ? std::atan2(bc, bs) : std::atan2(-bs, bc);
                best.first = r->second;
                if (damped)
                    best.second = {amp, std::min(tau, 1e6 * w), per, phi, r->first[0]};
                else
                    best.second = {amp, per, phi, r->first[0]};
            }
        }
        return best.second;
    }
    default:
        return std::vector<double>(np, 1.0);
    }
}

std::optional<FitResult> fit(const Model &model, const std::vector<double> &x, const std::vector<double> &y, const FitOptions &opts, QString *error)
{
    std::vector<double> xs, ys;
    for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
        if (std::isfinite(x[i]) && std::isfinite(y[i])) {
            xs.push_back(x[i]);
            ys.push_back(y[i]);
        }
    }
    const size_t np = model.params().size();
    std::vector<std::optional<double>> fixed = opts.fixed;
    fixed.resize(np);
    std::vector<size_t> freeIdx;
    for (size_t i = 0; i < np; ++i) {
        if (!fixed[i])
            freeIdx.push_back(i);
    }
    auto fail = [&](const QString &m) -> std::optional<FitResult> {
        if (error)
            *error = m;
        return std::nullopt;
    };
    if (freeIdx.empty())
        return fail(u"tous les paramètres sont fixés"_s);
    if (xs.size() < freeIdx.size()) {
        return fail(u"pas assez de points : %1 point%2 pour %3 paramètre%4 à ajuster"_s.arg(xs.size())
                        .arg(xs.size() > 1 ? u"s" : u"")
                        .arg(freeIdx.size())
                        .arg(freeIdx.size() > 1 ? u"s" : u""));
    }
    if (model.kind().isLinear())
        return fitLinear(model, xs, ys, fixed, freeIdx, error);
    std::vector<double> p0 = (opts.initial && opts.initial->size() == np) ? *opts.initial : initialGuess(model, xs, ys, fixed);
    p0.resize(np, 1.0);
    for (size_t i = 0; i < np; ++i) {
        if (fixed[i])
            p0[i] = *fixed[i];
    }
    auto res = levenbergMarquardt(model, xs, ys, p0, freeIdx, error);
    if (res)
        normalize(model, res->params);
    return res;
}

} // namespace fit
