// Icônes dessinées par le programme, nettes à toutes les tailles et identiques sous Linux et
// Windows (les drapeaux en émoji ne s'affichent pas sous Windows) : soleil et lune du bouton de
// thème, drapeaux du bouton de langue, pictogrammes de la barre d'état, petits repères des boutons
// d'orientation des axes, pictogrammes des menus et des onglets du mode Expert.

#pragma once

#include "i18n.h"

#include <QIcon>

namespace icones {

/// thème clair (soleil) ou sombre (lune)
QIcon theme(bool sombre);
/// drapeau de la langue : France, Royaume-Uni
QIcon drapeau(Langue l);
enum class Commande {
    OuvrirVideo, OuvrirProjet, Recuperer, Enregistrer, Copier, ExporterCsv, Quitter,
    Annuler, Retablir, Recommencer, ModeSimplifie, ModeExpert, Notice,
    Origine, Etalon, Precedent, Suivant, TournerGauche, TournerDroite, Valider, Fermer,
    Demarrer, Arreter, PleinEcran, QuitterPleinEcran, Examiner, Supprimer, ViseeAssistee, Vecteur, PresSouris, TousVecteurs
};
QIcon commande(Commande commande);

/// nature d'un message de la barre d'état
enum class Statut { Info, Reussite, Attention, Erreur };
QIcon statut(Statut s);
/// Point d'interrogation sur une pastille bleue (boîtes de question).
QIcon question();
/// Robot des points issus du suivi automatique.
QIcon robot();

/// un petit repère : sens de x et sens de y (1 : vers la droite, vers le haut)
QIcon repere(int sens_x, int sens_y);

/// Les onglets du mode Expert, au trait de la couleur du texte (elle suit le thème), avec une touche de
/// couleur : une mire (pointage), un tableau (coordonnées), des points sur une courbe (trajectoires),
/// une courbe dans ses axes (grapheur)
enum class Onglet { Pointage, Coordonnees, Trajectoires, Grapheur };
QIcon onglet(Onglet o);

} // namespace icones
