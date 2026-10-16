// Tableaux à une dimension du mini-numpy et leurs calculs.
//
// Les valeurs sont stockées en `double` ; le type (`Kind`) reproduit celui de numpy
// (booléens, entiers, flottants, complexes) pour l'affichage et pour les règles de calcul
// (entier + entier = entier, division = flottant…).

#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace arr {

enum class Kind { Bool, Int, Float, Complex };

/// Nom numpy du type (`int64`, `float64`…).
const char *kindName(Kind k);

/// Erreur de calcul, transformée en exception Python par le module natif.
struct Error : std::runtime_error {
    enum Type { Value, Index, Type_ } type;
    Error(const QString &m, Type t = Value) : std::runtime_error(m.toStdString()), type(t) {}
};

struct Arr {
    Kind kind = Kind::Float;
    std::vector<double> re;
    /// Parties imaginaires (vide sauf pour les complexes).
    std::vector<double> im;

    static Arr make(Kind k, std::vector<double> re);
    static Arr flt(std::vector<double> re) { return make(Kind::Float, std::move(re)); }
    static Arr cplx(std::vector<double> re, std::vector<double> im) { return {Kind::Complex, std::move(re), std::move(im)}; }
    static Arr scalar(Kind k, double re, double im);

    size_t size() const { return re.size(); }
    double imAt(size_t i) const { return kind == Kind::Complex ? im[i] : 0.0; }
    /// Conversion vers un autre type (comme `astype`).
    Arr astype(Kind k) const;
};

/// Élément seul : valeur et type.
struct Scalar {
    Kind kind = Kind::Float;
    double re = 0.0, im = 0.0;
};

enum class BinOp { Add, Sub, Mul, Div, FloorDiv, Mod, Pow, Lt, Le, Gt, Ge, Eq, Ne, And, Or, Xor, Max, Min, Atan2, Hypot };

std::optional<BinOp> parseOp(const QString &s);
/// Opération élément par élément `a op b`, avec la diffusion de numpy.
Arr binop(const Arr &a, const Arr &b, BinOp op);
/// Fonction d'une variable (`sin`, `abs`, `isnan`…).
Arr unary(const Arr &a, const QString &name);
/// Arrondi à `decimals` décimales, au pair le plus proche (comme `numpy.round`).
Arr round(const Arr &a, int decimals);
/// Réduction (`sum`, `mean`, `std`…) ; `ddof` pour `std` et `var`.
Scalar reduce(const Arr &a, const QString &name, double ddof);
/// Sommes et produits cumulés, différences successives.
Arr cumulative(const Arr &a, const QString &name);
/// `numpy.gradient(f, x)` à une dimension.
std::vector<double> gradient(const std::vector<double> &f, const std::vector<double> &x);
/// `numpy.trapezoid(y, x)` ; `x` vide : pas `dx`.
double trapezoid(const std::vector<double> &y, const std::vector<double> &x, double dx);
/// Indices d'une tranche Python `start:stop:step`.
std::vector<size_t> sliceIndices(size_t n, std::optional<long long> start, std::optional<long long> stop, std::optional<long long> step);
/// Normalise un indice (négatif compté depuis la fin).
size_t index(size_t n, long long i);
Arr take(const Arr &a, const std::vector<size_t> &idx);
Arr linspace(double start, double stop, size_t num, bool endpoint);
Arr arange(double start, double stop, double step, bool isInt);
/// Moindres carrés polynomiaux, coefficients du plus haut degré au plus bas.
std::vector<double> polyfit(const std::vector<double> &x, const std::vector<double> &y, size_t deg);
std::vector<double> polyval(const std::vector<double> &p, const std::vector<double> &x);
/// Transformée de Fourier discrète, ou inverse (normalisée), sur `n` points.
Arr fft(const Arr &a, bool inverse, std::optional<size_t> n);
/// Fréquences des composantes, calculées exactement comme numpy.
Arr fftfreq(size_t n, double d, bool half);

/// Générateur pseudo-aléatoire (xoshiro256**) reproductible.
class Rng {
public:
    explicit Rng(std::uint64_t seed);
    double uniform();
    double normal();
    std::uint64_t below(std::uint64_t n);

private:
    std::uint64_t next();
    std::uint64_t m_s[4];
    std::optional<double> m_spare;
};

} // namespace arr
