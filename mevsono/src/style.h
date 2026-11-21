// Couleurs communes aux graphiques et au panneau des mesures.

#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

/// Graisse des titres des panneaux et des graphiques, et des noms des curseurs : demi-gras, moins
/// lourd que le gras.
inline constexpr QFont::Weight TITLE_WEIGHT = QFont::DemiBold;

inline const QColor COLOR_PLAYHEAD(255, 64, 64);

/// Vrai si le thème du bureau est sombre (texte plus clair que le fond).
inline bool est_sombre(const QPalette &palette)
{
    return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
}

// Couleurs des curseurs : vives sur fond sombre, plus foncées sur fond clair, où les vives se lisent
// mal (même teinte).

/// Curseur A (début de l'extrait).
inline QColor couleur_curseur_a(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(255, 153, 0) : QColor(215, 105, 0);
}

/// Curseur B (fin de l'extrait).
inline QColor couleur_curseur_b(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(124, 252, 0) : QColor(40, 150, 0);
}

/// Curseurs t₁ et t₂ de mesure, pic sous la souris.
inline QColor couleur_curseur_t(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(0, 229, 255) : QColor(0, 135, 150);
}

/// Repères et rangs des harmoniques.
inline QColor couleur_harmonique(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(255, 99, 180) : QColor(205, 35, 135);
}

/// Couleurs des messages de la barre d'état : avertissement, réussite, erreur ; lisibles sur fond
/// clair comme sur fond sombre.
inline QColor couleur_attention(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(255, 143, 0) : QColor(200, 90, 0);
}

inline QColor couleur_reussite(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(110, 215, 130) : QColor(20, 125, 55);
}

inline QColor couleur_erreur(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(255, 90, 90) : QColor(200, 0, 0);
}

/// Couleur du tracé du signal : un violet, qu'aucun curseur ne porte (t₁ et t₂, cyan, se perdaient
/// dans l'ancien bleu clair).
inline QColor couleur_signal(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(185, 160, 255) : QColor(95, 60, 175);
}

/// Couleur du tracé du spectre.
inline QColor couleur_spectre(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(255, 210, 90) : QColor(180, 90, 0);
}

/// Même teinte, plus transparente (repère d'un harmonique sans pic).
inline QColor attenuee(QColor color, double alpha)
{
    color.setAlphaF(float(color.alphaF() * alpha));
    return color;
}

/// Ombre de l'extrait sur le signal.
inline QColor nuance_couleur(const QPalette &palette)
{
    return est_sombre(palette) ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 18);
}
