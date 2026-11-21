// Icônes dessinées par le programme, nettes à toutes les tailles et identiques sous Linux et
// Windows (les drapeaux en émoji ne s'affichent pas sous Windows) : soleil et lune du bouton de
// thème, drapeaux du bouton de langue, commandes et pictogrammes de la barre d'état.

#pragma once

#include "i18n.h"

#include <QIcon>

namespace icons {

enum class Commande { Ouvrir, Enregistrer, EnregistrerSous, ImageAnnotee, Quitter, Notice, Etalonner, Mesurer, Profil };
QIcon commande(Commande commande);

/// Thème clair (soleil) ou sombre (lune).
QIcon theme(bool dark);
/// Drapeau de la langue : France, Royaume-Uni.
QIcon drapeau(Lang lang);
/// Loupe avec un signe + ou −, de la couleur du texte des boutons.
QIcon loupe(bool agrandir, const QColor &trait);
/// Flèche de déplacement de l'image, orientée selon le vecteur unitaire `sens`.
QIcon fleche(QPointF sens, const QColor &trait);

/// Nature d'un message de la barre d'état.
enum class Status { Info, Success, Warning, Error };
QIcon etat(Status kind);
/// Point d'interrogation sur une pastille bleue (boîtes de question).
QIcon question();

} // namespace icons
