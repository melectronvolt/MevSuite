// Le même filtre permet d'écouter un fichier à une autre cadence et de comparer
// deux instants séparés par une fraction d'échantillon sur une note aiguë.
#include "interpolation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

// Le sinus cardinal reconstruit les valeurs entre deux échantillons. La fenêtre
// de Blackman ramène doucement ses poids à zéro au bout de 48 lobes de chaque côté.
constexpr int LOBES = 48;
constexpr int PAS_PAR_LOBE = 128;
const auto poids = [] {
    std::array<double, LOBES * PAS_PAR_LOBE + 2> table{};
    table[0] = 1;
    for (int i = 1; i < LOBES * PAS_PAR_LOBE; ++i) {
        const double angle = std::numbers::pi * i / PAS_PAR_LOBE;
        const double fenetre = 0.42 + 0.5 * std::cos(angle / LOBES) + 0.08 * std::cos(2 * angle / LOBES);
        table[i] = std::sin(angle) / angle * fenetre;
    }
    return table;
}();

} // namespace

double echantillon_filtre(std::span<const float> extrait, double position, double bande)
{
    // Rétrécir la bande élargit la portion du signal à prendre en compte.
    const double rayon = LOBES / bande;
    const size_t debut = size_t(std::max(0., std::ceil(position - rayon)));
    const size_t fin = size_t(std::min(double(extrait.size()), std::floor(position + rayon) + 1));
    double somme = 0;
    for (size_t i = debut; i < fin; ++i) {
        const double distance = std::abs(double(i) - position) * bande * PAS_PAR_LOBE;
        const size_t indice = size_t(distance);
        const double coefficient = std::lerp(poids[indice], poids[indice + 1], distance - double(indice));
        somme += double(extrait[i]) * coefficient;
    }
    return somme * bande;
}
