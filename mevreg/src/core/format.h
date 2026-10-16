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
std::optional<double> parseNumber(const QString &s);

/// Au plus `sig` chiffres significatifs, sans zéros inutiles ; notation scientifique
/// pour les très grands et très petits nombres. NaN donne une chaîne vide.
QString formatSig(double v, int sig, NumberStyle style);
/// Nombre fixe de décimales (« -0,00 » devient « 0,00 »).
QString formatFixed(double v, int decimals, NumberStyle style);
/// Notation scientifique `1,23×10⁻⁴`.
QString formatSci(double v, int decimals, NumberStyle style);
/// Écriture décimale la plus courte qui redonne exactement `v`, sans exposant (« 0.1 », « 2 », « 0.0000001 »).
QString shortest(double v);
/// Comme `shortest`, mais avec la virgule décimale si demandé.
QString shortestStyled(double v, NumberStyle style);
/// Écriture d'un flottant comme `repr(float)` en Python : « 2.0 », « 0.1 », « 1e-05 », « 1.5e+20 ».
QString pythonFloatRepr(double v);

/// Chiffres et signes remplacés par leurs équivalents en exposant.
QString superscript(const QString &s);
/// Unité saisie au clavier mise en forme : `m.s-1` → `m·s⁻¹`, `m/s^2` → `m/s²`.
QString prettyUnit(const QString &unit);
/// « nom (unité) ».
QString labelWithUnit(const QString &name, const QString &unit);
/// Remplace le trait d'union initial par le signe moins typographique « − ».
QString typographicMinus(const QString &s);

/// Résultat de mesure arrondi d'après son incertitude.
struct RoundedMeasure {
    QString value;
    QString uncertainty;
    /// Puissance de dix mise en facteur commun, 0 si aucune.
    int exponent = 0;

    /// `(1,234 ± 0,012)×10⁻³ m` ou `(1,234 ± 0,012) m`.
    QString toText(const QString &unit) const;
};

/// L'incertitude garde `sigU` chiffres significatifs, la valeur est arrondie à la même décimale.
RoundedMeasure roundMeasure(double value, double u, int sigU, NumberStyle style);
