// Icônes dessinées par le programme, nettes à toutes les tailles et identiques sous Linux et
// Windows (les drapeaux en émoji ne s'affichent pas sous Windows) : soleil et lune du bouton de
// thème, drapeaux du bouton de langue, pictogrammes des onglets et de la barre d'état.

#pragma once

#include "i18n.h"

#include <QIcon>

namespace icons {

/// Thème clair (soleil) ou sombre (lune).
QIcon theme(bool dark);
/// Drapeau de la langue : France, Royaume-Uni.
QIcon flag(Lang lang);

/// Onglets, aux couleurs du thème en cours : un signal et deux curseurs t₁, t₂ (mesure de la
/// période) ; les raies d'un spectre (étude des fréquences).
QIcon periodTab();
QIcon frequenciesTab();

/// Nature d'un message de la barre d'état.
enum class Status { Info, Success, Warning, Error };
QIcon status(Status kind);
/// Point d'interrogation sur une pastille bleue (boîtes de question).
QIcon question();

} // namespace icons
