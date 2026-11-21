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
QIcon drapeau(Lang lang);
QIcon dossier_ouvert();
/// Disque rouge de la commande d'enregistrement.
QIcon enregistrer();
QIcon disquette();

enum class Commande {
    Peripheriques, ToutVoir, ZoomExtrait, MemoriserSpectre, RetirerSpectre,
    Voie, Reglages, ModeSimple, ModeExpert, Notice, ChoisirPic, DetectionAuto
};
QIcon commande(Commande commande);

/// Onglets, aux couleurs du thème en cours : un signal et deux curseurs t₁, t₂ (mesure de la
/// période) ; les raies d'un spectre (étude des fréquences).
QIcon onglet_periode();
QIcon onglet_frequences();

/// Nature d'un message de la barre d'état.
enum class Status { Info, Success, Warning, Error };
QIcon etat(Status kind);
/// Point d'interrogation sur une pastille bleue (boîtes de question).
QIcon question();

} // namespace icons
