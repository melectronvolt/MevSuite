// Statistiques d'une série de mesures répétées.

#pragma once

#include "core/format.h"

#include <optional>
#include <vector>

namespace stats {

/// Écart-type des mesures finies. Le diviseur vaut n pour la population et n − 1 pour un échantillon.
double ecart_type(const std::vector<double> &mesures, size_t degresRetires);

struct Resume {
    size_t effectif = 0;
    double minimum = 0, maximum = 0, moyenne = 0;
    /// Écart-type, division par n.
    double ecartTypePopulation = 0;
    /// Écart-type corrigé s, division par n − 1.
    double ecartTypeEchantillon = 0;
    /// Incertitude-type de type A : s/√n.
    double uA = 0;

    /// « moyenne ± u » arrondi (incertitude à deux chiffres significatifs).
    RoundedMeasure resultat(NumberStyle style) const { return arrondit_mesure(moyenne, uA, 2, style); }
};

/// Résumé des valeurs renseignées (cases vides ignorées).
std::optional<Resume> resume(const std::vector<double> &mesures);
/// Nombre de classes par défaut (règle de Sturges), entre 3 et 30.
size_t classes_par_defaut(size_t effectif);

struct Histogramme {
    /// Bornes des classes : `effectifs.size() + 1` valeurs.
    std::vector<double> bornes;
    std::vector<size_t> effectifs;
};

/// Histogramme à classes de même largeur, comme `numpy.histogram(v, bins)`.
std::optional<Histogramme> histogramme(const std::vector<double> &mesures, size_t nombreClasses);

} // namespace stats
