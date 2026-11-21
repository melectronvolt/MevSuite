// Tracé rapide d'un long signal : pour chaque colonne de pixels, le minimum et le maximum des
// échantillons qu'elle couvre. Des tables précalculées (minimum et maximum par blocs de 64, 128,
// 256… échantillons) évitent de parcourir tout le signal à chaque image.

#pragma once

#include <QPointF>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

class Envelope {
public:
    explicit Envelope(std::span<const float> echantillons);

    /// Minimum et maximum du premier échantillon inclus (`debut`) au premier exclu (`fin`).
    /// Fournir le même signal qu'à la construction et des indices compris entre 0 et sa taille.
    std::pair<float, float> min_max(std::span<const float> echantillons, size_t debut, size_t fin) const;

    /// Points à tracer pour la partie visible du signal (de `tempsDebut` à `tempsFin` secondes), sur `colonnes`
    /// colonnes de pixels : les échantillons eux-mêmes s'il y en a peu, sinon un minimum et un
    /// maximum par colonne (le tracé remplit alors l'enveloppe). Coordonnées : (temps, valeur).
    std::vector<QPointF> points(std::span<const float> echantillons, double cadence, double tempsDebut,
                                double tempsFin, size_t colonnes) const;

private:
    /// `m_niveaux[k]` : (minimum, maximum) de chaque bloc de `TAILLE_BLOC << k` échantillons.
    std::vector<std::vector<std::pair<float, float>>> m_niveaux;
};
