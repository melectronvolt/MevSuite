#pragma once

#include <span>

/// Valeur du signal à une position exprimée en échantillons (éventuellement entre deux).
/// La position se trouve dans l’extrait. La fraction de bande appartient à ]0, 1].
/// Elle vaut 1 pour toute la bande d'origine, ou par exemple 1/3 pour
/// passer de 48 à 16 kHz. Les échantillons en dehors de l'extrait valent zéro.
double echantillon_filtre(std::span<const float> extrait, double position, double bande);
