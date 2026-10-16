// Graduations des axes et écriture des nombres associés.

#pragma once

#include "core/format.h"
#include "core/plotspec.h"

#include <QStringList>

#include <optional>
#include <vector>

namespace ticks {

/// Graduations calculées pour un axe.
struct Ticks {
    std::vector<double> major;
    std::vector<double> minor;
    QStringList labels;
    /// Puissance de dix mise en facteur (« ×10⁻³ »), 0 si aucune.
    int factorExp = 0;
};

/// Pas « rond » (1, 2 ou 5 × 10ⁿ) donnant environ `target` intervalles.
double niceStep(double range, double target);
/// Nombre de subdivisions mineures adapté à un pas principal.
int autoMinor(double step);
/// Graduations d'un axe linéaire sur [min, max].
Ticks linear(double min, double max, double target, std::optional<double> step, std::optional<int> minorDiv, NumFormat format, NumberStyle style);
/// Graduations d'un axe logarithmique (puissances de dix, sous-graduations 2…9).
Ticks logarithmic(double min, double max, NumberStyle style);

} // namespace ticks
