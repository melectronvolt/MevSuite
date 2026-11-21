// Apparence : thème clair ou sombre (celui du bureau, ou celui choisi avec le bouton soleil / lune),
// couleurs de l'interface. Palettes et couleurs des messages communes aux logiciels de la suite.

#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QSize>
#include <Qt>

class QAbstractButton;
class QWidget;

namespace theme {

/// Installe le style et suit le thème du bureau ; à appeler une fois, après QApplication et avant
/// la création des fenêtres.
void initialise();
/// Applique le thème choisi : `Light`, `Dark`, ou `Unknown` pour celui du bureau, suivi quand il
/// change. Une palette est fournie si le style n'en propose pas.
void choisit(Qt::ColorScheme choice);
/// Thème choisi (`Unknown` : celui du bureau).
Qt::ColorScheme choix();
/// Vrai si l'interface est actuellement sombre (texte plus clair que le fond).
bool est_sombre();

/// Couleur d'accent de l'application.
QColor accent();
/// Couleurs des messages : erreur, avertissement, réussite ; lisibles sur fond clair comme sombre.
QColor couleur_erreur();
QColor couleur_attention();
QColor couleur_reussite();
/// Textes discrets (indications) : PlaceholderText de la palette.
QColor couleur_attenuee();
/// Fond des panneaux (Grandeurs, Calculs, Modélisation…), un peu plus sombre que celui de la fenêtre.
QColor couleur_panneau();
/// Couleur associée au type d'une grandeur (mesure, calculée, incertitude).
QColor couleur_type(bool calculated, bool uncertainty);

/// Couleurs propres au tableur.
struct TableColors {
    QColor header, calculated, grid, selection, cursor, error, text, weak;
};
TableColors couleurs_tableur();

// --- agrandissement de l'interface (Ctrl + / Ctrl − / Ctrl 0) ----------------------------------
//
// La police de l'application est multipliée par la fenêtre principale ; ici, tout ce qui se mesure
// en pixels la suit : mesures du style (icônes des menus et des boutons, cases à cocher, barres de
// défilement, marges, onglets…), tailles des widgets données en pixels à 100 %, graphes à l'écran.

/// Agrandissement en cours (1 = 100 %).
double zoom();
/// Change l'agrandissement (la fenêtre principale change ensuite la police et appelle `rescale`).
void definit_zoom(double zoom);
/// Longueur donnée en pixels à 100 %, à l'agrandissement en cours.
int px(double length);
QSize px(QSize size);
/// Police à chasse fixe, de la taille de la police de l'interface (seule la famille est imposée :
/// elle suit l'agrandissement).
QFont police_fixe();

/// Tailles données en pixels à 100 % : appliquées à l'agrandissement en cours, puis de nouveau à
/// chaque changement (`rescale`). Une largeur minimale n'est jamais moindre que celle que demande
/// la disposition du widget (rien n'est rogné), revue quand son contenu change.
void definit_largeur_min(QWidget *widget, int width);
void definit_largeur_max(QWidget *widget, int width);
void definit_taille_min(QWidget *widget, QSize size);
void definit_taille_fixe(QWidget *widget, QSize size);
void definit_hauteur_fixe(QWidget *widget, int height);
void definit_taille_icone(QAbstractButton *button, QSize size);
/// Largeur minimale donnée par `definit_largeur_min` (à 100 %), 0 s'il n'y en a pas.
int largeur_min_base(const QWidget *widget);
/// Police relative à celle de l'interface : `factor` fois sa taille, avec la graisse donnée.
void definit_police_relative(QWidget *widget, double factor, QFont::Weight weight = QFont::Normal);
/// Applique de nouveau les tailles et polices ci-dessus à tous les widgets, après un changement
/// d'agrandissement.
void reechelonne();

} // namespace theme
