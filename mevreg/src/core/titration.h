// Exploitation d'une courbe de dosage pH = f(V) : méthode des tangentes parallèles et
// méthode de la dérivée.

#pragma once

#include "core/spline.h"

#include <QString>

#include <optional>
#include <utility>
#include <vector>

namespace titration {

/// Résultat de la méthode des tangentes.
struct Tangents {
    /// Pente commune des deux tangentes.
    double slope = 0;
    /// Points de contact (avant et après le saut).
    std::pair<double, double> p1, p2;
    /// Ordonnées à l'origine des tangentes et de la médiane.
    double b1 = 0, b2 = 0, bMedian = 0;
    /// Point d'équivalence : intersection de la médiane et de la courbe.
    std::pair<double, double> equivalence;

    double line1(double x) const { return slope * x + b1; }
    double line2(double x) const { return slope * x + b2; }
    double median(double x) const { return slope * x + bMedian; }
};

/// Courbe de dosage lissée, prête pour les deux méthodes.
class Curve {
public:
    /// `nullopt` et le message dans `error` si la courbe est inexploitable.
    static std::optional<Curve> make(const std::vector<double> &v, const std::vector<double> &ph, QString *error);

    double value(double x) const { return m_spline.eval(x); }
    double slope(double x) const { return m_spline.deriv(x); }
    std::pair<double, double> range() const { return {m_xMin, m_xMax}; }
    /// Abscisse du saut (pente maximale en valeur absolue).
    double jump() const { return m_jump; }
    /// Point de contact proposé avant le saut : pente retombée à 10 % du maximum.
    double defaultContact() const;
    /// Tangentes parallèles, la première touchant la courbe en `x1`.
    std::optional<Tangents> tangents(double x1, QString *error) const;

private:
    spline::Hermite m_spline;
    double m_jump = 0, m_xMin = 0, m_xMax = 0;
};

/// Méthode de la dérivée : abscisse du maximum de |dpH/dV| et valeur de la dérivée.
std::optional<std::pair<double, double>> derivativeMaximum(const std::vector<double> &v, const std::vector<double> &ph, QString *error);

} // namespace titration
