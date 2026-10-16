// Thème clair ou sombre : celui du bureau, ou celui choisi avec le bouton soleil/lune, à droite de la
// barre des menus. Et le style de la suite : celui du poste, la barre des menus un peu plus haute.

#pragma once

#include <QString>
#include <Qt>

namespace theme {

/// Applique le thème choisi : `Light`, `Dark`, ou `Unknown` pour celui du bureau, suivi quand il
/// change. À appeler une première fois après la création de `QApplication` et avant celle des
/// fenêtres (le style de la suite est installé à ce premier appel), puis à chaque changement de choix.
/// Une palette est fournie si le style n'en propose pas.
void choisit(Qt::ColorScheme choix);

/// le thème retenu dans la configuration (`theme = auto | light | dark`) : `auto`, ou une valeur
/// vide, suit le bureau
Qt::ColorScheme depuis_texte(const QString &texte);
QString en_texte(Qt::ColorScheme choix);

} // namespace theme
