// Calculs numériques de base : dérivée, intégrale, interpolation.
//
// Toutes les fonctions tolèrent les cases vides (NaN) : elles travaillent sur les points où
// x et y sont renseignés et laissent les autres cases vides.

#pragma once

#include <optional>
#include <utility>
#include <vector>

namespace numeric {

using Vec = std::vector<double>;

/// Dérivée dy/dx comme `numpy.gradient(y, x)` sur les points renseignés.
Vec gradient(const Vec &y, const Vec &x);
/// `numpy.gradient(f, x)` sur des valeurs toutes renseignées, avec exactement les formules de
/// numpy : pas constant si `x` n'a qu'une valeur ou si tous ses écarts sont égaux.
Vec gradientNumpy(const Vec &f, const Vec &x);
/// Primitive par la méthode des trapèzes, nulle au premier point valide.
Vec cumulativeTrapezoid(const Vec &y, const Vec &x);
/// Intégrale par les trapèzes (comme `numpy.trapezoid`).
double trapezoid(const Vec &y, const Vec &x);
/// Aire sous la courbe affine par morceaux entre les abscisses `a` et `b`.
double areaBetween(const Vec &y, const Vec &x, double a, double b);
/// Couples (x, y) valides, triés par x croissant.
std::pair<Vec, Vec> sortedPairs(const Vec &x, const Vec &y);
/// Interpolation linéaire en `t`, x trié croissant ; hors des données : valeur du bord.
double interp(double t, const Vec &xs, const Vec &ys);
/// Pas d'échantillonnage : pas moyen et écart relatif maximal au pas moyen.
std::optional<std::pair<double, double>> stepInfo(const Vec &x);
/// Rééchantillonne y(x) sur une grille régulière de `n` points.
std::pair<Vec, Vec> resampleUniform(const Vec &x, const Vec &y, size_t n);
/// Bornes finies (min, max) d'une série.
std::optional<std::pair<double, double>> finiteRange(const Vec &v);

} // namespace numeric
