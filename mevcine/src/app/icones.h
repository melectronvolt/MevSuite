// Icônes dessinées par le programme, nettes à toutes les tailles et identiques sous Linux et
// Windows (les drapeaux en émoji ne s'affichent pas sous Windows) : soleil et lune du bouton de
// thème, drapeaux du bouton de langue, pictogrammes de la barre d'état, petits repères des boutons
// d'orientation des axes, pictogrammes des onglets du mode Expert.

#pragma once

#include "i18n.h"

#include <QIcon>

namespace icones {

/// thème clair (soleil) ou sombre (lune)
QIcon theme(bool sombre);
/// drapeau de la langue : France, Royaume-Uni
QIcon drapeau(Langue l);

/// nature d'un message de la barre d'état
enum class Statut { Info, Reussite, Attention, Erreur };
QIcon statut(Statut s);
/// Point d'interrogation sur une pastille bleue (boîtes de question).
QIcon question();

/// un petit repère : sens de x et sens de y (1 : vers la droite, vers le haut)
QIcon repere(int sens_x, int sens_y);

/// Les onglets du mode Expert, au trait de la couleur du texte (elle suit le thème), avec une touche de
/// couleur : une mire (pointage), un tableau (coordonnées), des points sur une courbe (trajectoires),
/// une courbe dans ses axes (grapheur)
enum class Onglet { Pointage, Coordonnees, Trajectoires, Grapheur };
QIcon onglet(Onglet o);

} // namespace icones
