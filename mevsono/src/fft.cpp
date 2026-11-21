#include "fft.h"

#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace fft {

size_t puissance_de_deux_suivante(size_t n)
{
    if (n > std::bit_floor(std::numeric_limits<size_t>::max()))
        throw std::length_error("Taille de FFT trop grande");
    return std::bit_ceil(n);
}

Plan::Plan(size_t n)
    : m_n(n)
{
    if (!std::has_single_bit(n))
        throw std::invalid_argument("La taille de FFT doit être une puissance de deux");
    m_indicesInverses.resize(n);
    m_rotations.resize(n / 2);
    // Permutation initiale. Par exemple, pour huit points, l'indice 3
    // (011 en binaire) s'échange avec l'indice 6 (110).
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        m_indicesInverses[i] = j;
    }
    // Calcul direct de chaque rotation pour éviter d'accumuler les erreurs d'arrondi.
    for (size_t k = 0; k < n / 2; ++k) {
        const double angle = -2.0 * std::numbers::pi * double(k) / double(n);
        m_rotations[k] = {std::cos(angle), std::sin(angle)};
    }
}

void Plan::directe(std::vector<Complex> &donnees) const
{
    transforme(donnees, false);
}

void Plan::inverse(std::vector<Complex> &donnees) const
{
    transforme(donnees, true);
}

void Plan::transforme(std::vector<Complex> &donnees, bool inverse) const
{
    if (donnees.size() != m_n)
        throw std::invalid_argument("La longueur du signal diffère de la taille de FFT");
    for (size_t i = 1; i < m_n; ++i) {
        if (i < m_indicesInverses[i])
            std::swap(donnees[i], donnees[m_indicesInverses[i]]);
    }
    // Chaque étape combine deux transformées de même taille. Avec la rotation w,
    // les deux résultats sont a + w × b et a − w × b. Expliciter le produit
    // complexe allège cette boucle sur les valeurs finies du signal.
    const double signe = inverse ? -1.0 : 1.0;
    for (size_t largeur = 2; largeur <= m_n; largeur *= 2) {
        const size_t moitie = largeur / 2;
        const size_t pasRotation = m_n / largeur;
        for (size_t debut = 0; debut < m_n; debut += largeur) {
            for (size_t k = 0; k < moitie; ++k) {
                const Complex &rotation = m_rotations[k * pasRotation];
                const double cosinus = rotation.real(), sinus = signe * rotation.imag();
                const Complex a = donnees[debut + k];
                const Complex b = donnees[debut + k + moitie];
                const double reel = b.real() * cosinus - b.imag() * sinus;
                const double imaginaire = b.real() * sinus + b.imag() * cosinus;
                donnees[debut + k] = {a.real() + reel, a.imag() + imaginaire};
                donnees[debut + k + moitie] = {a.real() - reel, a.imag() - imaginaire};
            }
        }
    }
}

} // namespace fft
