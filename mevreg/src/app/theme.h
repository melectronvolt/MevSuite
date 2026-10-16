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
void init();
/// Applique le thème choisi : `Light`, `Dark`, ou `Unknown` pour celui du bureau, suivi quand il
/// change. Une palette est fournie si le style n'en propose pas.
void choose(Qt::ColorScheme choice);
/// Thème choisi (`Unknown` : celui du bureau).
Qt::ColorScheme choice();
/// Vrai si l'interface est actuellement sombre (texte plus clair que le fond).
bool isDark();

/// Couleur d'accent de l'application.
QColor accent();
/// Couleurs des messages : erreur, avertissement, réussite ; lisibles sur fond clair comme sombre.
QColor errorColor();
QColor warnColor();
QColor successColor();
/// Textes discrets (indications) : PlaceholderText de la palette.
QColor weakColor();
/// Fond des panneaux (Grandeurs, Calculs, Modélisation…), un peu plus sombre que celui de la fenêtre.
QColor panelColor();
/// Couleur associée au type d'une grandeur (mesure, calculée, incertitude).
QColor kindColor(bool calculated, bool uncertainty);

/// Couleurs propres au tableur.
struct TableColors {
    QColor header, calculated, grid, selection, cursor, error, text, weak;
};
TableColors tableColors();

// --- agrandissement de l'interface (Ctrl + / Ctrl − / Ctrl 0) ----------------------------------
//
// La police de l'application est multipliée par la fenêtre principale ; ici, tout ce qui se mesure
// en pixels la suit : mesures du style (icônes des menus et des boutons, cases à cocher, barres de
// défilement, marges, onglets…), tailles des widgets données en pixels à 100 %, graphes à l'écran.

/// Agrandissement en cours (1 = 100 %).
double zoom();
/// Change l'agrandissement (la fenêtre principale change ensuite la police et appelle `rescale`).
void setZoom(double zoom);
/// Longueur donnée en pixels à 100 %, à l'agrandissement en cours.
int px(double length);
QSize px(QSize size);
/// Police à chasse fixe, de la taille de la police de l'interface (seule la famille est imposée :
/// elle suit l'agrandissement).
QFont fixedFont();

/// Tailles données en pixels à 100 % : appliquées à l'agrandissement en cours, puis de nouveau à
/// chaque changement (`rescale`). Une largeur minimale n'est jamais moindre que celle que demande
/// la disposition du widget (rien n'est rogné), revue quand son contenu change.
void setMinimumWidth(QWidget *widget, int width);
void setMaximumWidth(QWidget *widget, int width);
void setMinimumSize(QWidget *widget, QSize size);
void setFixedSize(QWidget *widget, QSize size);
void setFixedHeight(QWidget *widget, int height);
void setIconSize(QAbstractButton *button, QSize size);
/// Largeur minimale donnée par `setMinimumWidth` (à 100 %), 0 s'il n'y en a pas.
int baseMinimumWidth(const QWidget *widget);
/// Police relative à celle de l'interface : `factor` fois sa taille, avec la graisse donnée.
void setRelativeFont(QWidget *widget, double factor, QFont::Weight weight = QFont::Normal);
/// Applique de nouveau les tailles et polices ci-dessus à tous les widgets, après un changement
/// d'agrandissement.
void rescale();

} // namespace theme
