// Couleurs et graisse communes à la zone image, au panneau latéral et au graphique du profil.

#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

/// Graisse des titres des panneaux et des graphiques, et des noms des curseurs : demi-gras, moins
/// lourd que le gras.
inline constexpr QFont::Weight TITLE_WEIGHT = QFont::DemiBold;

// Couleurs dessinées sur l'image, toujours vives (lisibles sur une photo) : étalon, mesures, ligne de
// coupe, curseurs A et B.
inline const QColor COLOR_CALIB(255, 215, 0);
inline const QColor COLOR_MEASURE(0, 229, 255);
inline const QColor COLOR_CUT(255, 64, 255);
inline const QColor COLOR_A(255, 153, 0);
inline const QColor COLOR_B(124, 252, 0);

/// Vrai si le thème du bureau est sombre (texte plus clair que le fond).
inline bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
}

// Couleurs des curseurs hors de l'image (panneau, graphique du profil) : vives sur fond sombre, plus
// foncées sur fond clair, où les vives se lisent mal (même teinte). Sur l'image, toujours les vives.

/// Curseur A.
inline QColor cursorColorA(const QPalette &palette)
{
    return isDark(palette) ? COLOR_A : QColor(215, 105, 0);
}

/// Curseur B.
inline QColor cursorColorB(const QPalette &palette)
{
    return isDark(palette) ? COLOR_B : QColor(40, 150, 0);
}

/// Mesure (curseurs t des autres logiciels de la suite).
inline QColor cursorColorT(const QPalette &palette)
{
    return isDark(palette) ? COLOR_MEASURE : QColor(0, 135, 150);
}

/// Couleur des avertissements, lisible sur fond clair comme sur fond sombre.
inline QColor warnColor(const QPalette &palette)
{
    return isDark(palette) ? QColor(255, 143, 0) : QColor(200, 90, 0);
}

/// Couleur des messages de réussite (étalonnage validé, profil exporté…).
inline QColor successColor(const QPalette &palette)
{
    return isDark(palette) ? QColor(110, 215, 130) : QColor(20, 125, 55);
}

/// Couleur des erreurs.
inline QColor errorColor(const QPalette &palette)
{
    return isDark(palette) ? QColor(255, 90, 90) : QColor(200, 0, 0);
}
