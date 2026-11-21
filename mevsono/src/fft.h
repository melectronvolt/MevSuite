// Transformée de Fourier rapide (algorithme de Cooley-Tukey, base 2). Toutes les tailles utilisées
// par MeVSono sont des puissances de 2 : tailles de FFT proposées, extrait complété par des zéros,
// autocorrélation.

#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace fft {

using Complex = std::complex<double>;

/// Plus petite puissance de 2 supérieure ou égale à `n` (1 pour 0).
/// Lève std::length_error si la taille demandée dépasse la dernière puissance représentable.
size_t puissance_de_deux_suivante(size_t n);

/// Transformée de taille fixe : tables (permutation, facteurs de rotation) calculées une fois, puis
/// réutilisées pour chaque segment.
class Plan {
public:
    /// La taille doit être une puissance de 2. Les transformées exigent un tableau
    /// de cette taille. Une taille incorrecte lève std::invalid_argument.
    explicit Plan(size_t n);

    size_t taille() const { return m_n; }
    /// Transformée directe, sur place : X(k) = Σ x(i) exp(−2iπ ik/n).
    void directe(std::vector<Complex> &donnees) const;
    /// Transformée inverse, sur place et non normalisée (pas de division par n).
    void inverse(std::vector<Complex> &donnees) const;

private:
    void transforme(std::vector<Complex> &donnees, bool inverse) const;

    size_t m_n;
    /// Indice à échanger avec chaque indice (ordre des bits inversé).
    std::vector<size_t> m_indicesInverses;
    /// exp(−2iπ k/n), pour k < n/2.
    std::vector<Complex> m_rotations;
};

} // namespace fft
