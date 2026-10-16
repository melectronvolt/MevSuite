// Formatage des nombres à la manière de Python (`{:g}`), et analyse des nombres saisis par les
// élèves (longueur de l'étalon, cadence de la vidéo).

#pragma once

#include <QString>

#include <array>
#include <expected>
#include <optional>

/// équivalent de `f"{v:.{p}g}"` en Python
QString fmt_g(double v, int p);

/// analyse un nombre flottant saisi par un humain, en acceptant la virgule décimale
std::optional<double> parse_float(const QString &saisie);

/// le séparateur décimal de la langue de l'interface
QChar separateur_decimal();

/// un nombre à afficher dans l'interface : `p` chiffres significatifs, et le séparateur décimal de
/// la langue (les exports gardent le point)
QString nombre_local(double v, int p);

/// l'écriture la plus courte qui redonne exactement le nombre (« 0.04 », « 320 »), pour les fichiers
/// de projet
QString texte_reel(double v);

/// un nombre pour un programme Python : 6 chiffres significatifs, et toujours l'écriture d'un
/// flottant (« 120.0 » et non « 120 »), pour que numpy en fasse un tableau de flottants
QString fmt_py(double v);

/// les unités de la longueur de l'étalon, au choix de l'élève
enum class Unite { Millimetre, Centimetre, Metre };

inline constexpr std::array<Unite, 3> TOUTES_LES_UNITES = {Unite::Millimetre, Unite::Centimetre, Unite::Metre};

/// le symbole d'une unité : mm, cm, m
QString symbole(Unite u);
/// la longueur d'une unité, en mètre
double en_metre(Unite u);
/// l'unité d'un symbole (mm, cm, m), ou rien
std::optional<Unite> unite_depuis_symbole(const QString &s);
/// une longueur donnée en mètre, écrite dans une unité : « 85 cm »
QString texte_longueur(double metres, Unite u);

/// une longueur saisie, et l'unité dans laquelle elle l'a été
struct Longueur {
    double valeur;
    Unite unite;

    double metres() const { return valeur * en_metre(unite); }
};

/// Analyse la longueur réelle de l'étalon, que l'élève saisit dans l'unité choisie dans la liste
/// (mm, cm ou m).
///
/// Sont acceptés : la virgule ou le point décimal ; les puissances de 10 en notation E (`1E10`,
/// `1E-10`, `8,5e-1`, et même `E-10` pour 1E-10) ou avec `×10^` (`2,5×10^-3`, `2,5x10^-3`,
/// `2,5*10**-3`). L'unité peut aussi être écrite à la fin, avec ou sans espace (`85 cm`, `850mm`,
/// `0,85 m`) : elle remplace celle de la liste. Une autre unité (`km`, `kg`…) est refusée.
std::expected<Longueur, QString> parse_longueur(const QString &saisie, Unite unite);
