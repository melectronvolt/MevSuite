// Thème clair ou sombre : celui du bureau, ou celui choisi avec le bouton soleil / lune.

#pragma once

#include <Qt>

namespace theme {

/// Applique le thème choisi : `Light`, `Dark`, ou `Unknown` pour celui du bureau, suivi quand il
/// change. À appeler une première fois après la création de `QApplication` et avant celle des
/// fenêtres, puis à chaque changement de choix. Une palette est fournie si le style n'en propose pas.
void choose(Qt::ColorScheme choice);

} // namespace theme
