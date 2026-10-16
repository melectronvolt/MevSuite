// Courbes lissées passant par tous les points de mesure (interpolation).
//
// - spline cubique naturelle : courbe la plus « douce » ;
// - interpolation monotone (PCHIP, comme `scipy.interpolate.PchipInterpolator`) : sur chaque
//   intervalle, la courbe reste entre les valeurs des deux points voisins.

#pragma once

#include <optional>
#include <utility>
#include <vector>

namespace spline {

enum class Kind { Cubic, Monotone };

/// Interpolant cubique d'Hermite par morceaux : valeurs et pentes aux nœuds.
struct Hermite {
    std::vector<double> x, y, d;

    /// x strictement croissant, au moins 2 points.
    static std::optional<Hermite> make(const std::vector<double> &x, const std::vector<double> &y, Kind kind);
    double eval(double t) const;
    double deriv(double t) const;
    /// Points échantillonnés : `perSegment` intervalles par segment, nœuds compris.
    std::vector<std::pair<double, double>> sample(int perSegment) const;

private:
    size_t segment(double t) const;
    double evalIn(size_t k, double t) const;
};

/// Courbe lissée passant par les points, dans l'ordre des données. Si x n'est pas monotone,
/// x et y sont interpolés en fonction de la longueur parcourue le long des points.
std::vector<std::pair<double, double>> smoothCurve(const std::vector<double> &x, const std::vector<double> &y, Kind kind, int perSegment);

} // namespace spline
