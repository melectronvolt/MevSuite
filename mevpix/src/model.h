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

    double longueur_px() const;
    /// Longueur minimale commune à l'étalon, aux mesures et à la coupe, en pixels de la photo.
    static constexpr double LONGUEUR_MIN_PX = 1.0;
    bool est_mesurable() const { return longueur_px() >= LONGUEUR_MIN_PX; }
    /// Point situé à la fraction `t` du segment (0 = a, 1 = b).
    QPointF point_en(double t) const;
    /// Fraction du segment (entre 0 et 1) du point le plus proche de `p`.
    double projette(QPointF p) const;
    /// Vecteur unitaire perpendiculaire au segment (nul si le segment est dégénéré).
    QPointF normale() const;

    bool operator==(const Segment &) const = default;
};

double longueur(QPointF v);

/// Contraint `b` pour que le segment (a, b) soit horizontal ou vertical.
QPointF aligne_sur_axe(QPointF a, QPointF b);

/// Ces numéros identifient l'unité dans les fichiers .mevpix.
enum class Unite { Micrometre = 0, Millimetre = 1, Centimetre = 2, Metre = 3 };

inline constexpr std::array<Unite, 4> UNITES = {Unite::Micrometre, Unite::Millimetre, Unite::Centimetre, Unite::Metre};

QString symbole_unite(Unite unite);

/// Correspondance pixels → longueur réelle, obtenue à partir d'un étalon.
struct Etalonnage {
    /// Longueur réelle de l'étalon, dans `unite`.
    double longueurReelle;
    /// Longueur de l'étalon en pixels au moment de la validation.
    double referencePx;
    Unite unite;
    bool operator==(const Etalonnage &) const = default;

    /// Vérifie que les deux conversions (unités par pixel, pixels par unité) sont représentables.
    /// Si elle est fournie, la diagonale garantit que toute longueur sur la photo pourra être mesurée.
    static std::optional<Etalonnage> fabrique(double referencePx, double longueurReelle, Unite unite,
                                               double diagonalePx = 0.0);

    double unites_par_pixel() const { return longueurReelle / referencePx; }
    double en_reel(double px) const { return px * unites_par_pixel(); }
    /// Pour l'étiquette sur l'image : écart relatif de 0,2 % au plus, même longueur réelle et même unité.
    bool correspond(const Etalonnage &other) const;
};

/// La notation scientifique évite de remplir un champ avec des centaines de zéros.
bool notation_scientifique(double valeur);
/// Adapte les décimales à la précision demandée. Les longueurs usuelles gardent une écriture fixe.
QString texte_nombre(double v, int sig, QChar dec);
/// Nombre de décimales nécessaires pour écrire `v` avec `sig` chiffres significatifs.
int decimales_significatives(double v, int sig);
/// Formate un nombre avec un nombre fixe de décimales et le séparateur décimal `dec`.
QString texte_decimal(double v, int decimals, QChar dec);
/// Écriture la plus courte qui redonne exactement `v`, sans exposant (« 0.1 », « 2 », « 0.0000012 »).
QString plus_court(double v);
/// Remplace le point décimal par `dec` (virgule en français).
QString avec_separateur_decimal(QString s, QChar dec);
/// Longueur affichée à l'élève : en unité étalonnée, ou en pixels à défaut.
QString texte_longueur(double px, const std::optional<Etalonnage> &calib, QChar dec);
/// Tableau des mesures (colonnes séparées par des tabulations), prêt à coller dans un tableur.
/// `header` : titres des deux colonnes. Les longueurs sont écrites comme à l'écran.
QString tableau_mesures(const std::vector<double> &lengthsPx, const std::optional<Etalonnage> &calib, QChar dec,
                      const std::array<QString, 2> &header);
/// Lit un nombre saisi par l'élève, avec virgule ou point décimal.
std::optional<double> lit_nombre(const QString &s);
