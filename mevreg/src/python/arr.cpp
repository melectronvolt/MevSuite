#include "python/arr.h"

#include "core/fft.h"
#include "core/linalg.h"
#include "core/numeric.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace arr {

const char *kindName(Kind k)
{
    switch (k) {
    case Kind::Bool: return "bool";
    case Kind::Int: return "int64";
    case Kind::Float: return "float64";
    case Kind::Complex: return "complex128";
    }
    return "float64";
}

Arr Arr::make(Kind k, std::vector<double> values)
{
    Arr a;
    a.kind = k;
    a.re = std::move(values);
    if (k == Kind::Complex)
        a.im.assign(a.re.size(), 0.0);
    return a;
}

Arr Arr::scalar(Kind k, double r, double i)
{
    if (k == Kind::Complex)
        return cplx({r}, {i});
    return make(k, {r});
}

Arr Arr::astype(Kind k) const
{
    Arr out;
    out.kind = k;
    out.re.resize(size());
    for (size_t i = 0; i < size(); ++i) {
        switch (k) {
        case Kind::Bool: out.re[i] = (re[i] != 0.0 || imAt(i) != 0.0) ? 1.0 : 0.0; break;
        case Kind::Int: out.re[i] = std::trunc(re[i]); break;
        default: out.re[i] = re[i];
        }
    }
    if (k == Kind::Complex) {
        out.im.resize(size());
        for (size_t i = 0; i < size(); ++i)
            out.im[i] = imAt(i);
    }
    return out;
}

namespace {

/// Taille commune de deux opérandes (un opérande d'une valeur s'étend à l'autre).
size_t broadcast(size_t a, size_t b)
{
    if (a == b || b == 1)
        return a;
    if (a == 1)
        return b;
    throw Error(u"les tableaux n'ont pas la même taille (%1 et %2) : operands could not be broadcast together with shapes (%1,) (%2,)"_s.arg(a).arg(b));
}

double at(const std::vector<double> &v, size_t i)
{
    return v.size() == 1 ? v[0] : v[i];
}

/// Reste du signe du diviseur, comme Python et numpy.
double pyMod(double a, double b)
{
    const double r = std::fmod(a, b);
    if (r != 0.0 && ((r < 0.0) != (b < 0.0)))
        return r + b;
    return r;
}

Kind numericKind(Kind k)
{
    return k == Kind::Bool ? Kind::Int : k;
}

Arr complexBinop(const Arr &a0, const Arr &b0, BinOp op, size_t n)
{
    const Arr a = a0.kind == Kind::Complex ? a0 : a0.astype(Kind::Complex);
    const Arr b = b0.kind == Kind::Complex ? b0 : b0.astype(Kind::Complex);
    std::vector<double> re, im;
    re.reserve(n);
    im.reserve(n);
    const bool cmp = op == BinOp::Eq || op == BinOp::Ne;
    for (size_t i = 0; i < n; ++i) {
        const double x = at(a.re, i), xi = at(a.im, i), y = at(b.re, i), yi = at(b.im, i);
        double r, j;
        switch (op) {
        case BinOp::Add: r = x + y; j = xi + yi; break;
        case BinOp::Sub: r = x - y; j = xi - yi; break;
        case BinOp::Mul: r = x * y - xi * yi; j = x * yi + xi * y; break;
        case BinOp::Div: {
            const double d = y * y + yi * yi;
            r = (x * y + xi * yi) / d;
            j = (xi * y - x * yi) / d;
            break;
        }
        case BinOp::Pow: {
            // exp(b·ln a)
            const double m = std::sqrt(x * x + xi * xi);
            if (m == 0.0) {
                r = (y == 0.0 && yi == 0.0) ? 1.0 : 0.0;
                j = 0.0;
            } else {
                const double lr = std::log(m), li = std::atan2(xi, x);
                const double er = y * lr - yi * li, ei = y * li + yi * lr;
                const double e = std::exp(er);
                r = e * std::cos(ei);
                j = e * std::sin(ei);
            }
            break;
        }
        case BinOp::Eq:
        case BinOp::Ne: {
            const bool eq = x == y && xi == yi;
            re.push_back((op == BinOp::Eq ? eq : !eq) ? 1.0 : 0.0);
            continue;
        }
        default:
            throw Error(u"opération impossible sur des nombres complexes"_s);
        }
        re.push_back(r);
        im.push_back(j);
    }
    if (cmp)
        return Arr::make(Kind::Bool, std::move(re));
    return Arr::cplx(std::move(re), std::move(im));
}

double roundHalfEven(double x)
{
    return std::nearbyint(x);
}

Arr complexUnary(const Arr &a, const QString &name)
{
    const size_t n = a.size();
    std::vector<double> re, im;
    const bool realOut = name == u"abs" || name == u"real" || name == u"imag" || name == u"angle";
    const bool boolOut = name == u"isnan" || name == u"isfinite";
    for (size_t i = 0; i < n; ++i) {
        const double x = a.re[i], y = a.im[i];
        double r = 0, j = 0;
        if (name == u"abs") {
            r = std::hypot(x, y);
        } else if (name == u"real") {
            r = x;
        } else if (name == u"imag") {
            r = y;
        } else if (name == u"angle") {
            r = std::atan2(y, x);
        } else if (name == u"neg") {
            r = -x;
            j = -y;
        } else if (name == u"conj" || name == u"conjugate") {
            r = x;
            j = -y;
        } else if (name == u"exp") {
            const double e = std::exp(x);
            r = e * std::cos(y);
            j = e * std::sin(y);
        } else if (name == u"log") {
            r = std::log(std::hypot(x, y));
            j = std::atan2(y, x);
        } else if (name == u"sqrt") {
            const double m = std::hypot(x, y);
            r = std::sqrt((m + x) / 2.0);
            j = std::copysign(std::sqrt((m - x) / 2.0), y == 0.0 ? 1.0 : y);
        } else if (name == u"isnan") {
            r = (std::isnan(x) || std::isnan(y)) ? 1.0 : 0.0;
        } else if (name == u"isfinite") {
            r = (std::isfinite(x) && std::isfinite(y)) ? 1.0 : 0.0;
        } else if (name == u"sin") {
            r = std::sin(x) * std::cosh(y);
            j = std::cos(x) * std::sinh(y);
        } else if (name == u"cos") {
            r = std::cos(x) * std::cosh(y);
            j = -std::sin(x) * std::sinh(y);
        } else {
            throw Error(u"%1 n'est pas disponible pour les nombres complexes"_s.arg(name));
        }
        re.push_back(r);
        im.push_back(j);
    }
    if (boolOut)
        return Arr::make(Kind::Bool, std::move(re));
    if (realOut)
        return Arr::flt(std::move(re));
    return Arr::cplx(std::move(re), std::move(im));
}

} // namespace

std::optional<BinOp> parseOp(const QString &s)
{
    static const QHash<QString, BinOp> ops = {
        {u"add"_s, BinOp::Add}, {u"sub"_s, BinOp::Sub}, {u"mul"_s, BinOp::Mul}, {u"div"_s, BinOp::Div},
        {u"floordiv"_s, BinOp::FloorDiv}, {u"mod"_s, BinOp::Mod}, {u"pow"_s, BinOp::Pow}, {u"lt"_s, BinOp::Lt},
        {u"le"_s, BinOp::Le}, {u"gt"_s, BinOp::Gt}, {u"ge"_s, BinOp::Ge}, {u"eq"_s, BinOp::Eq}, {u"ne"_s, BinOp::Ne},
        {u"and"_s, BinOp::And}, {u"or"_s, BinOp::Or}, {u"xor"_s, BinOp::Xor}, {u"maximum"_s, BinOp::Max},
        {u"minimum"_s, BinOp::Min}, {u"arctan2"_s, BinOp::Atan2}, {u"hypot"_s, BinOp::Hypot},
    };
    auto it = ops.find(s);
    if (it == ops.end())
        return std::nullopt;
    return *it;
}

Arr binop(const Arr &a, const Arr &b, BinOp op)
{
    const size_t n = broadcast(a.size(), b.size());
    const Kind top = std::max(numericKind(a.kind), numericKind(b.kind));
    if (top == Kind::Complex)
        return complexBinop(a, b, op, n);
    const bool bothInt = top == Kind::Int;
    Kind kind = top;
    switch (op) {
    case BinOp::Lt: case BinOp::Le: case BinOp::Gt: case BinOp::Ge: case BinOp::Eq: case BinOp::Ne:
        kind = Kind::Bool;
        break;
    case BinOp::And: case BinOp::Or: case BinOp::Xor:
        if (a.kind == Kind::Bool && b.kind == Kind::Bool)
            kind = Kind::Bool;
        else if (bothInt)
            kind = Kind::Int;
        else
            throw Error(u"les opérations &, |, ^ demandent des entiers ou des booléens"_s);
        break;
    case BinOp::Div: case BinOp::Atan2: case BinOp::Hypot:
        kind = Kind::Float;
        break;
    case BinOp::Pow:
        kind = (bothInt && std::all_of(b.re.begin(), b.re.end(), [](double v) { return v >= 0.0; })) ? Kind::Int : Kind::Float;
        break;
    case BinOp::Max: case BinOp::Min:
        if (a.kind == Kind::Bool && b.kind == Kind::Bool)
            kind = Kind::Bool;
        break;
    default:
        break;
    }
    std::vector<double> re(n);
    for (size_t i = 0; i < n; ++i) {
        const double x = at(a.re, i), y = at(b.re, i);
        double r = 0.0;
        switch (op) {
        case BinOp::Add: r = x + y; break;
        case BinOp::Sub: r = x - y; break;
        case BinOp::Mul: r = x * y; break;
        case BinOp::Div: r = x / y; break;
        case BinOp::FloorDiv: r = (bothInt && y == 0.0) ? 0.0 : std::floor(x / y); break;
        case BinOp::Mod: r = (bothInt && y == 0.0) ? 0.0 : pyMod(x, y); break;
        case BinOp::Pow: r = std::pow(x, y); break;
        case BinOp::Lt: r = x < y; break;
        case BinOp::Le: r = x <= y; break;
        case BinOp::Gt: r = x > y; break;
        case BinOp::Ge: r = x >= y; break;
        case BinOp::Eq: r = x == y; break;
        case BinOp::Ne: r = x != y; break;
        case BinOp::And:
            r = kind == Kind::Bool ? (x != 0.0 && y != 0.0) : static_cast<double>(static_cast<long long>(x) & static_cast<long long>(y));
            break;
        case BinOp::Or:
            r = kind == Kind::Bool ? (x != 0.0 || y != 0.0) : static_cast<double>(static_cast<long long>(x) | static_cast<long long>(y));
            break;
        case BinOp::Xor:
            r = kind == Kind::Bool ? ((x != 0.0) != (y != 0.0)) : static_cast<double>(static_cast<long long>(x) ^ static_cast<long long>(y));
            break;
        case BinOp::Max: r = (std::isnan(x) || std::isnan(y)) ? NAN : std::max(x, y); break;
        case BinOp::Min: r = (std::isnan(x) || std::isnan(y)) ? NAN : std::min(x, y); break;
        case BinOp::Atan2: r = std::atan2(x, y); break;
        case BinOp::Hypot: r = std::hypot(x, y); break;
        }
        re[i] = r;
    }
    return Arr::make(kind, std::move(re));
}

Arr unary(const Arr &a, const QString &name)
{
    if (a.kind == Kind::Complex)
        return complexUnary(a, name);
    const Kind intLike = a.kind == Kind::Bool ? Kind::Int : a.kind;
    auto mapKind = [&](Kind k, auto f) {
        std::vector<double> out(a.size());
        for (size_t i = 0; i < a.size(); ++i)
            out[i] = f(a.re[i]);
        return Arr::make(k, std::move(out));
    };
    if (name == u"neg")
        return mapKind(intLike, [](double x) { return -x; });
    if (name == u"abs")
        return mapKind(intLike, [](double x) { return std::abs(x); });
    if (name == u"invert") {
        if (a.kind == Kind::Bool)
            return mapKind(Kind::Bool, [](double x) { return x == 0.0 ? 1.0 : 0.0; });
        if (a.kind == Kind::Int)
            return mapKind(Kind::Int, [](double x) { return static_cast<double>(~static_cast<long long>(x)); });
        throw Error(u"l'opérateur ~ demande des booléens ou des entiers"_s);
    }
    if (name == u"sign")
        return mapKind(intLike, [](double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : x == 0 ? 0.0 : NAN; });
    if (name == u"isnan")
        return mapKind(Kind::Bool, [](double x) { return std::isnan(x) ? 1.0 : 0.0; });
    if (name == u"isfinite")
        return mapKind(Kind::Bool, [](double x) { return std::isfinite(x) ? 1.0 : 0.0; });
    if (name == u"isinf")
        return mapKind(Kind::Bool, [](double x) { return std::isinf(x) ? 1.0 : 0.0; });
    if (name == u"real")
        return Arr::make(a.kind, a.re);
    if (name == u"imag")
        return Arr::make(a.kind, std::vector<double>(a.size(), 0.0));
    if (name == u"conj" || name == u"conjugate")
        return a;
    if (name == u"angle")
        return mapKind(Kind::Float, [](double x) { return x < 0 ? std::numbers::pi : 0.0; });
    if (name == u"square")
        return mapKind(intLike, [](double x) { return x * x; });
    static const QHash<QString, double (*)(double)> funcs = {
        {u"sin"_s, [](double x) { return std::sin(x); }},
        {u"cos"_s, [](double x) { return std::cos(x); }},
        {u"tan"_s, [](double x) { return std::tan(x); }},
        {u"arcsin"_s, [](double x) { return std::asin(x); }},
        {u"arccos"_s, [](double x) { return std::acos(x); }},
        {u"arctan"_s, [](double x) { return std::atan(x); }},
        {u"sinh"_s, [](double x) { return std::sinh(x); }},
        {u"cosh"_s, [](double x) { return std::cosh(x); }},
        {u"tanh"_s, [](double x) { return std::tanh(x); }},
        {u"exp"_s, [](double x) { return std::exp(x); }},
        {u"log"_s, [](double x) { return std::log(x); }},
        {u"log10"_s, [](double x) { return std::log10(x); }},
        {u"log2"_s, [](double x) { return std::log2(x); }},
        {u"sqrt"_s, [](double x) { return std::sqrt(x); }},
        {u"floor"_s, [](double x) { return std::floor(x); }},
        {u"ceil"_s, [](double x) { return std::ceil(x); }},
        {u"trunc"_s, [](double x) { return std::trunc(x); }},
        {u"rint"_s, [](double x) { return std::nearbyint(x); }},
        {u"radians"_s, [](double x) { return x * (std::numbers::pi / 180.0); }},
        {u"deg2rad"_s, [](double x) { return x * (std::numbers::pi / 180.0); }},
        {u"degrees"_s, [](double x) { return x * (180.0 / std::numbers::pi); }},
        {u"rad2deg"_s, [](double x) { return x * (180.0 / std::numbers::pi); }},
        {u"exp2"_s, [](double x) { return std::exp2(x); }},
        {u"expm1"_s, [](double x) { return std::expm1(x); }},
        {u"log1p"_s, [](double x) { return std::log1p(x); }},
        {u"cbrt"_s, [](double x) { return std::cbrt(x); }},
        {u"reciprocal"_s, [](double x) { return 1.0 / x; }},
    };
    if (auto f = funcs.value(name))
        return mapKind(Kind::Float, f);
    throw Error(u"fonction inconnue : %1"_s.arg(name));
}

Arr round(const Arr &a, int decimals)
{
    const double p = std::pow(10.0, decimals);
    auto f = [p](double x) { return roundHalfEven(x * p) / p; };
    if (a.kind == Kind::Complex) {
        Arr out = a;
        for (double &x : out.re)
            x = f(x);
        for (double &x : out.im)
            x = f(x);
        return out;
    }
    if ((a.kind == Kind::Int || a.kind == Kind::Bool) && decimals >= 0)
        return a;
    std::vector<double> out(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        out[i] = f(a.re[i]);
    return Arr::make(std::max(a.kind, Kind::Int), std::move(out));
}

Scalar reduce(const Arr &a, const QString &name, double ddof)
{
    const size_t n = a.size();
    const bool intLike = a.kind == Kind::Int || a.kind == Kind::Bool;
    auto empty = [&] { return Error(u"%1 d'un tableau vide : zero-size array to reduction operation %1"_s.arg(name)); };
    auto sum = [&](const std::vector<double> &v) {
        double s = 0.0;
        for (double x : v)
            s += x;
        return s;
    };
    if (name == u"sum") {
        if (a.kind == Kind::Complex)
            return {Kind::Complex, sum(a.re), sum(a.im)};
        return {intLike ? Kind::Int : Kind::Float, sum(a.re), 0.0};
    }
    if (name == u"prod") {
        double p = 1.0;
        for (double x : a.re)
            p *= x;
        return {intLike ? Kind::Int : Kind::Float, p, 0.0};
    }
    if (name == u"mean") {
        if (a.kind == Kind::Complex)
            return {Kind::Complex, sum(a.re) / n, sum(a.im) / n};
        return {Kind::Float, n == 0 ? NAN : sum(a.re) / n, 0.0};
    }
    if (name == u"var" || name == u"std") {
        const double m = sum(a.re) / n;
        double ss = 0.0;
        for (double x : a.re)
            ss += (x - m) * (x - m);
        const double v = ss / (static_cast<double>(n) - ddof);
        return {Kind::Float, name == u"std" ? std::sqrt(v) : v, 0.0};
    }
    if (name == u"min" || name == u"max") {
        if (n == 0)
            throw empty();
        double best = a.re[0];
        for (double x : a.re) {
            if (std::isnan(x)) {
                best = NAN;
                break;
            }
            if ((name == u"min" && x < best) || (name == u"max" && x > best))
                best = x;
        }
        return {a.kind, best, 0.0};
    }
    if (name == u"argmin" || name == u"argmax") {
        if (n == 0)
            throw empty();
        size_t k = 0;
        for (size_t i = 0; i < n; ++i) {
            const double x = a.re[i];
            if (std::isnan(x)) {
                k = i;
                break;
            }
            if ((name == u"argmin" && x < a.re[k]) || (name == u"argmax" && x > a.re[k]))
                k = i;
        }
        return {Kind::Int, static_cast<double>(k), 0.0};
    }
    if (name == u"any" || name == u"all") {
        bool any = false, all = true;
        for (size_t i = 0; i < n; ++i) {
            const bool t = a.re[i] != 0.0 || a.imAt(i) != 0.0;
            any = any || t;
            all = all && t;
        }
        return {Kind::Bool, (name == u"any" ? any : all) ? 1.0 : 0.0, 0.0};
    }
    if (name == u"ptp") {
        const double r = reduce(a, u"max"_s, 0).re - reduce(a, u"min"_s, 0).re;
        return {std::max(a.kind, Kind::Int), r, 0.0};
    }
    if (name == u"median") {
        if (n == 0)
            return {Kind::Float, NAN, 0.0};
        std::vector<double> v = a.re;
        if (std::any_of(v.begin(), v.end(), [](double x) { return std::isnan(x); }))
            return {Kind::Float, NAN, 0.0};
        std::sort(v.begin(), v.end());
        return {Kind::Float, n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]), 0.0};
    }
    throw Error(u"réduction inconnue : %1"_s.arg(name));
}

Arr cumulative(const Arr &a, const QString &name)
{
    const Kind kind = a.kind == Kind::Bool ? Kind::Int : a.kind;
    if (name == u"cumsum") {
        std::vector<double> re(a.size());
        double acc = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            re[i] = acc += a.re[i];
        if (kind == Kind::Complex) {
            std::vector<double> im(a.size());
            double ai = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                im[i] = ai += a.im[i];
            return Arr::cplx(std::move(re), std::move(im));
        }
        return Arr::make(kind, std::move(re));
    }
    if (name == u"cumprod") {
        std::vector<double> re(a.size());
        double acc = 1.0;
        for (size_t i = 0; i < a.size(); ++i)
            re[i] = acc *= a.re[i];
        return Arr::make(std::min(kind, Kind::Float), std::move(re));
    }
    if (name == u"diff") {
        std::vector<double> re;
        for (size_t i = 1; i < a.size(); ++i)
            re.push_back(a.re[i] - a.re[i - 1]);
        if (kind == Kind::Complex) {
            std::vector<double> im;
            for (size_t i = 1; i < a.size(); ++i)
                im.push_back(a.im[i] - a.im[i - 1]);
            return Arr::cplx(std::move(re), std::move(im));
        }
        return Arr::make(kind, std::move(re));
    }
    throw Error(u"opération inconnue : %1"_s.arg(name));
}

std::vector<double> gradient(const std::vector<double> &f, const std::vector<double> &x)
{
    const size_t n = f.size();
    if (n < 2)
        throw Error(u"Shape of array too small to calculate a numerical gradient, at least (edge_order + 1) elements are required."_s);
    if (x.size() != 1 && x.size() != n)
        throw Error(u"np.gradient : les abscisses (%1 valeurs) doivent avoir la taille du tableau (%2)"_s.arg(x.size()).arg(n));
    return numeric::gradientNumpy(f, x);
}

double trapezoid(const std::vector<double> &y, const std::vector<double> &x, double dx)
{
    if (!x.empty() && x.size() != y.size())
        throw Error(u"np.trapezoid : x (%1 valeurs) et y (%2 valeurs) n'ont pas la même taille"_s.arg(x.size()).arg(y.size()));
    double s = 0.0;
    for (size_t i = 1; i < y.size(); ++i) {
        const double h = x.empty() ? dx : x[i] - x[i - 1];
        s += 0.5 * (y[i] + y[i - 1]) * h;
    }
    return s;
}

std::vector<size_t> sliceIndices(size_t n0, std::optional<long long> start, std::optional<long long> stop, std::optional<long long> step0)
{
    const long long n = static_cast<long long>(n0);
    const long long step = step0.value_or(1);
    if (step == 0)
        throw Error(u"slice step cannot be zero"_s);
    auto norm = [n](long long v, long long lo, long long hi) {
        if (v < 0)
            v += n;
        return std::clamp(v, lo, hi);
    };
    std::vector<size_t> out;
    if (step > 0) {
        const long long s = start ? norm(*start, 0, n) : 0;
        const long long e = stop ? norm(*stop, 0, n) : n;
        for (long long i = s; i < e; i += step)
            out.push_back(static_cast<size_t>(i));
    } else {
        const long long s = start ? norm(*start, -1, n - 1) : n - 1;
        const long long e = stop ? norm(*stop, -1, n - 1) : -1;
        for (long long i = s; i > e; i += step)
            out.push_back(static_cast<size_t>(i));
    }
    return out;
}

size_t index(size_t n, long long i)
{
    const long long k = i < 0 ? i + static_cast<long long>(n) : i;
    if (k < 0 || k >= static_cast<long long>(n))
        throw Error(u"index %1 is out of bounds for axis 0 with size %2"_s.arg(i).arg(n), Error::Index);
    return static_cast<size_t>(k);
}

Arr take(const Arr &a, const std::vector<size_t> &idx)
{
    Arr out;
    out.kind = a.kind;
    out.re.reserve(idx.size());
    for (size_t i : idx)
        out.re.push_back(a.re[i]);
    if (a.kind == Kind::Complex) {
        for (size_t i : idx)
            out.im.push_back(a.im[i]);
    }
    return out;
}

Arr linspace(double start, double stop, size_t num, bool endpoint)
{
    const size_t div = endpoint ? (num > 0 ? num - 1 : 0) : num;
    const double step = div > 0 ? (stop - start) / static_cast<double>(div) : 0.0;
    std::vector<double> v(num);
    for (size_t i = 0; i < num; ++i)
        v[i] = start + step * static_cast<double>(i);
    if (endpoint && num > 1)
        v[num - 1] = stop;
    return Arr::flt(std::move(v));
}

Arr arange(double start, double stop, double step, bool isInt)
{
    if (step == 0.0)
        throw Error(u"Maximum allowed size exceeded"_s);
    const double nn = std::max(std::ceil((stop - start) / step), 0.0);
    if (nn > 1e8)
        throw Error(u"np.arange : tableau trop grand"_s);
    const size_t n = static_cast<size_t>(nn);
    std::vector<double> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = start + step * static_cast<double>(i);
    return Arr::make(isInt ? Kind::Int : Kind::Float, std::move(v));
}

std::vector<double> polyfit(const std::vector<double> &x, const std::vector<double> &y, size_t deg)
{
    if (x.size() != y.size())
        throw Error(u"np.polyfit : x et y doivent avoir la même taille"_s);
    if (x.size() <= deg)
        throw Error(u"np.polyfit : il faut au moins %1 points pour un polynôme de degré %2"_s.arg(deg + 1).arg(deg));
    const size_t n = x.size();
    auto a = linalg::Matrix::zeros(n, deg + 1);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j <= deg; ++j)
            a(i, j) = std::pow(x[i], static_cast<double>(deg - j));
    }
    auto sol = linalg::leastSquares(a, y);
    if (!sol)
        throw Error(u"np.polyfit : système mal conditionné (abscisses identiques ?)"_s);
    return *sol;
}

std::vector<double> polyval(const std::vector<double> &p, const std::vector<double> &x)
{
    // schéma de Horner, comme numpy (y = y·x + p[i])
    std::vector<double> out(x.size());
    for (size_t k = 0; k < x.size(); ++k) {
        double acc = 0.0;
        for (double c : p)
            acc = acc * x[k] + c;
        out[k] = acc;
    }
    return out;
}

Arr fft(const Arr &a, bool inverse, std::optional<size_t> n)
{
    const size_t len = n.value_or(a.size());
    std::vector<fft::Complex> buf(len, 0.0);
    for (size_t i = 0; i < len && i < a.size(); ++i)
        buf[i] = {a.re[i], a.imAt(i)};
    if (len > 0) {
        fft::transform(buf, inverse);
        if (inverse) {
            for (auto &c : buf)
                c /= static_cast<double>(len);
        }
    }
    std::vector<double> re(len), im(len);
    for (size_t i = 0; i < len; ++i) {
        re[i] = buf[i].real();
        im[i] = buf[i].imag();
    }
    return Arr::cplx(std::move(re), std::move(im));
}

Arr fftfreq(size_t n, double d, bool half)
{
    const double val = 1.0 / (static_cast<double>(n) * d);
    std::vector<double> v;
    if (half) {
        for (size_t k = 0; k <= n / 2; ++k)
            v.push_back(static_cast<double>(k) * val);
        return Arr::flt(std::move(v));
    }
    const size_t pos = (n + 1) / 2;
    for (size_t k = 0; k < n; ++k)
        v.push_back(k < pos ? static_cast<double>(k) * val : (static_cast<double>(k) - static_cast<double>(n)) * val);
    return Arr::flt(std::move(v));
}

Rng::Rng(std::uint64_t seed)
{
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ull;
    auto next = [&z] {
        z += 0x9E3779B97F4A7C15ull;
        std::uint64_t x = z;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    };
    for (auto &s : m_s)
        s = next();
}

static std::uint64_t rotl(std::uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

std::uint64_t Rng::next()
{
    const std::uint64_t r = rotl(m_s[1] * 5, 7) * 9;
    const std::uint64_t t = m_s[1] << 17;
    m_s[2] ^= m_s[0];
    m_s[3] ^= m_s[1];
    m_s[1] ^= m_s[2];
    m_s[0] ^= m_s[3];
    m_s[2] ^= t;
    m_s[3] = rotl(m_s[3], 45);
    return r;
}

double Rng::uniform()
{
    return static_cast<double>(next() >> 11) / static_cast<double>(1ull << 53);
}

double Rng::normal()
{
    if (m_spare) {
        const double v = *m_spare;
        m_spare.reset();
        return v;
    }
    const double u1 = 1.0 - uniform();
    const double u2 = uniform();
    const double r = std::sqrt(-2.0 * std::log(u1));
    m_spare = r * std::sin(2.0 * std::numbers::pi * u2);
    return r * std::cos(2.0 * std::numbers::pi * u2);
}

std::uint64_t Rng::below(std::uint64_t n)
{
    return n == 0 ? 0 : next() % n;
}

} // namespace arr
