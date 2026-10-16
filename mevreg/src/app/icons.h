// Icônes de l'interface, dessinées au trait : nettes à toutes les tailles, de la couleur du
// texte du thème (clair ou sombre), grisées quand l'action est indisponible. Plus les icônes
// communes aux logiciels de la suite, identiques sous Linux et Windows (les drapeaux en émoji ne
// s'affichent pas sous Windows) : soleil et lune du bouton de thème, drapeaux du bouton de langue,
// pictogrammes de la barre d'état.

#pragma once

#include <QIcon>

namespace icons {

/// Icône par son nom (« plus », « chart », « crosshair »…).
QIcon get(const char *name);

/// Thème clair (soleil) ou sombre (lune).
QIcon theme(bool dark);
/// Langue d'un drapeau (MeVReg n'existe pour l'instant qu'en français).
enum class Lang { Fr, En };
/// Drapeau de la langue : France, Royaume-Uni.
QIcon flag(Lang lang);

/// Nature d'un message de la barre d'état.
enum class Status { Info, Success, Warning, Error };
QIcon status(Status kind);
/// Point d'interrogation sur une pastille bleue (boîtes de question).
QIcon question();

} // namespace icons
