// Statistiques d'une série de mesures répétées.

#pragma once

#include "core/format.h"

#include <optional>
#include <vector>

namespace stats {

/// Écart-type avec `ddof` degrés de liberté retirés (0 : division par n, 1 : par n − 1).
double stdDev(const std::vector<double> &v, size_t ddof);

struct Summary {
    size_t n = 0;
    double min = 0, max = 0, mean = 0;
    /// Écart-type, division par n.
    double stdPop = 0;
    /// Écart-type corrigé s, division par n − 1.
    double stdSample = 0;
    /// Incertitude-type de type A : s/√n.
    double uA = 0;

    /// « moyenne ± u » arrondi (incertitude à deux chiffres significatifs).
    RoundedMeasure result(NumberStyle style) const { return roundMeasure(mean, uA, 2, style); }
};

/// Résumé des valeurs renseignées (cases vides ignorées).
std::optional<Summary> summarize(const std::vector<double> &values);
/// Nombre de classes par défaut (règle de Sturges), entre 3 et 30.
size_t defaultBins(size_t n);

struct Histogram {
    /// Bornes des classes : `counts.size() + 1` valeurs.
    std::vector<double> edges;
    std::vector<size_t> counts;
};

/// Histogramme à classes de même largeur, comme `numpy.histogram(v, bins)`.
std::optional<Histogram> histogram(const std::vector<double> &values, size_t bins);

} // namespace stats
