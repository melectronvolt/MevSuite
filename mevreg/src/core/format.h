// Mise en forme des nombres et des unités, lecture des nombres saisis.

#pragma once

#include <QString>

#include <optional>

/// Options d'affichage des nombres.
struct NumberStyle {
    bool decimalComma = true;

    bool operator==(const NumberStyle &) const = default;
};

/// Lit un nombre saisi par l'utilisateur : virgule ou point décimal, espaces ignorées,
/// notation scientifique acceptée. Une chaîne vide ou non numérique donne `nullopt`.
std::optional<double> lit_nombre(const QString &s);

/// Au plus `sig` chiffres significatifs, sans zéros inutiles ; notation scientifique
/// pour les très grands et très petits nombres. NaN donne une chaîne vide.
QString format_significatif(double v, int sig, NumberStyle style);
/// Nombre fixe de décimales (« -0,00 » devient « 0,00 »).
QString format_fixe(double v, int decimals, NumberStyle style);
/// Notation scientifique `1,23×10⁻⁴`.
QString format_scientifique(double v, int decimals, NumberStyle style);
/// Écriture décimale la plus courte qui redonne exactement `v`, sans exposant (« 0.1 », « 2 », « 0.0000001 »).
QString plus_court(double v);
/// Comme `shortest`, mais avec la virgule décimale si demandé.
QString plus_court_style(double v, NumberStyle style);
/// Écriture d'un flottant comme `repr(float)` en Python : « 2.0 », « 0.1 », « 1e-05 », « 1.5e+20 ».
QString repr_python_reel(double v);

/// Chiffres et signes remplacés par leurs équivalents en exposant.
QString exposant(const QString &s);
/// Unité saisie au clavier mise en forme : `m.s-1` → `m·s⁻¹`, `m/s^2` → `m/s²`.
QString unite_lisible(const QString &unit);
/// « nom (unité) », éventuellement avec le facteur commun des graduations.
QString etiquette_avec_unite(const QString &name, const QString &unit, int factorExp = 0);
/// Remplace le trait d'union initial par le signe moins typographique « − ».
QString moins_typographique(const QString &s);

/// Résultat de mesure arrondi d'après son incertitude.
struct RoundedMeasure {
    QString value;
    QString uncertainty;
    /// Puissance de dix mise en facteur commun, 0 si aucune.
    int exponent = 0;

    /// `(1,234 ± 0,012)×10⁻³ m` ou `(1,234 ± 0,012) m`.
    QString en_texte(const QString &unit) const;
};

/// L'incertitude garde `sigU` chiffres significatifs, la valeur est arrondie à la même décimale.
RoundedMeasure arrondit_mesure(double value, double u, int sigU, NumberStyle style);
