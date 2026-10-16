// Suites de valeurs : remplissage d'une colonne « de a à b par pas de h » (comme
// `numpy.arange`, la dernière valeur comprise) ou « en n valeurs » (comme `numpy.linspace`),
// et recopie incrémentée façon tableur.

#pragma once

#include <QString>

#include <vector>

namespace series {

/// Nombre maximal de valeurs produites d'un coup.
inline constexpr size_t MAX_VALUES = 100000;

/// Résultat : valeurs, ou message d'erreur.
struct Result {
    std::vector<double> values;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

/// De `start` à `end` par pas de `step`, `end` compris s'il tombe sur la grille.
Result byStep(double start, double end, double step);
/// `n` valeurs régulièrement espacées de `start` à `end` compris.
Result byCount(double start, double end, size_t n);
/// Recopie incrémentée : à partir des valeurs sélectionnées (cases vides ignorées), les
/// `count` suivantes. Plusieurs valeurs sont prolongées par leur tendance linéaire.
std::vector<double> extend(const std::vector<double> &selected, size_t count);

} // namespace series
