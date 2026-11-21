// Profil d'intensité le long d'une ligne de coupe : code pur, sans interface.

#pragma once

#include "model.h"

#include <QImage>
#include <QString>

#include <array>
#include <optional>
#include <vector>

/// Numéros des canaux dans les fichiers de séance.
enum class Canal { GrisPondere = 0, Rouge = 1, Vert = 2, Bleu = 3 };

inline constexpr std::array<Canal, 4> CANAUX = {Canal::GrisPondere, Canal::Rouge, Canal::Vert,
                                                        Canal::Bleu};

struct Profil {
    /// Distance en pixels entre deux échantillons successifs (≈ 1).
    double pasPx = 1.0;
    /// Intensité de chaque échantillon, entre 0 et 255.
    std::vector<double> intensites;
    /// Nombre d'échantillons dont au moins un pixel de la bande est saturé (≥ 254) dans le canal choisi.
    int echantillonsSatures = 0;
    /// Échantillons dont la bande est tronquée par le bord de l'image.
    int tronques = 0;

    double longueur_px() const;
    /// Ordonnée du profil à la fraction `t`, entre les deux échantillons voisins.
    double valeur_en(double t) const;
};

/// Échantillonne l'image tous les pixels le long de la coupe, en moyennant sur une bande
/// perpendiculaire de `largeurBande` pixels. Chaque échantillon moyenne les points inclus dans la photo.
/// L’image est au format `QImage::Format_RGB888`.
Profil echantillonne_profil(const QImage &image, const Segment &coupe, int largeurBande, Canal canal);

/// Profil au format tableur, colonnes `x (unité)` et `I`. Avec une virgule décimale (français…),
/// le séparateur de colonnes est `;` ; avec un point décimal (anglais), c'est `,`.
QString en_csv(const Profil &profil, const std::optional<Etalonnage> &etalonnage, QChar decimale);

/// Complète le nom sans extension. La fenêtre vérifie ensuite si le fichier ainsi nommé existe.
QString avec_extension_csv(const QString &path);
