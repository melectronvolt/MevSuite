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
    explicit Envelope(std::span<const float> samples);

    /// Minimum et maximum des échantillons `first..last`.
    std::pair<float, float> minMax(std::span<const float> samples, size_t first, size_t last) const;

    /// Points à tracer pour la partie visible du signal (de `t0` à `t1` secondes), sur `columns`
    /// colonnes de pixels : les échantillons eux-mêmes s'il y en a peu, sinon un minimum et un
    /// maximum par colonne (le tracé remplit alors l'enveloppe). Coordonnées : (temps, valeur).
    std::vector<QPointF> points(std::span<const float> samples, double rate, double t0, double t1, size_t columns) const;

private:
    /// `m_levels[k]` : (minimum, maximum) de chaque bloc de `BASE_BLOCK << k` échantillons.
    std::vector<std::vector<std::pair<float, float>>> m_levels;
};
