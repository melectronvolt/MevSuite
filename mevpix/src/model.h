// Géométrie et étalonnage : code pur, sans interface.

#pragma once

#include <QPointF>
#include <QString>

#include <array>
#include <optional>
#include <vector>

/// Segment en coordonnées pixel de l'image (origine en haut à gauche).
struct Segment {
    QPointF a;
    QPointF b;

    double lenPx() const;
    /// Point situé à la fraction `t` du segment (0 = a, 1 = b).
    QPointF pointAt(double t) const;
    /// Fraction du segment (entre 0 et 1) du point le plus proche de `p`.
    double project(QPointF p) const;
    /// Vecteur unitaire perpendiculaire au segment (nul si le segment est dégénéré).
    QPointF normal() const;

    bool operator==(const Segment &) const = default;
};

double length(QPointF v);

/// Contraint `b` pour que le segment (a, b) soit horizontal ou vertical.
QPointF snapAxis(QPointF a, QPointF b);

enum class Unit { Micrometre, Millimetre, Centimetre, Metre };

inline constexpr std::array<Unit, 4> ALL_UNITS = {Unit::Micrometre, Unit::Millimetre, Unit::Centimetre, Unit::Metre};

QString unitSymbol(Unit unit);

/// Correspondance pixels → longueur réelle, obtenue à partir d'un étalon.
struct Calibration {
    /// Longueur réelle de l'étalon, dans `unit`.
    double realLen;
    /// Longueur de l'étalon en pixels au moment de la validation.
    double refPx;
    Unit unit;

    /// Vide si l'étalon est trop court ou la longueur saisie invalide.
    static std::optional<Calibration> make(double refPx, double realLen, Unit unit);

    double unitsPerPx() const { return realLen / refPx; }
    double toReal(double px) const { return px * unitsPerPx(); }
    /// Même échelle : étalon identique à 0,5 px près, même longueur réelle et même unité.
    bool matches(const Calibration &other) const;
};

/// Formate un nombre avec `sig` chiffres significatifs et le séparateur décimal `dec`.
QString fmtNum(double v, int sig, QChar dec);
/// Nombre de décimales nécessaires pour écrire `v` avec `sig` chiffres significatifs.
int sigDecimals(double v, int sig);
/// Formate un nombre avec un nombre fixe de décimales et le séparateur décimal `dec`.
QString fmtDec(double v, int decimals, QChar dec);
/// Écriture la plus courte qui redonne exactement `v`, sans exposant (« 0.1 », « 2 », « 0.0000012 »).
QString shortest(double v);
/// Remplace le point décimal par `dec` (virgule en français).
QString withDecimal(QString s, QChar dec);
/// Longueur affichée à l'élève : en unité étalonnée, ou en pixels à défaut.
QString fmtLen(double px, const std::optional<Calibration> &calib, QChar dec);
/// Tableau des mesures (colonnes séparées par des tabulations), prêt à coller dans un tableur.
/// `header` : titres des deux colonnes. Les longueurs sont écrites comme à l'écran.
QString measuresTable(const std::vector<double> &lengthsPx, const std::optional<Calibration> &calib, QChar dec,
                      const std::array<QString, 2> &header);
/// Lit un nombre saisi par l'élève, avec virgule ou point décimal.
std::optional<double> parseNum(const QString &s);
