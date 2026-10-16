// Profil d'intensité le long d'une ligne de coupe : code pur, sans interface.

#pragma once

#include "model.h"

#include <QImage>
#include <QString>

#include <array>
#include <optional>
#include <vector>

enum class Channel { Luminance, Red, Green, Blue };

inline constexpr std::array<Channel, 4> ALL_CHANNELS = {Channel::Luminance, Channel::Red, Channel::Green,
                                                        Channel::Blue};

struct Profile {
    /// Distance en pixels entre deux échantillons successifs (≈ 1).
    double stepPx = 1.0;
    /// Intensité de chaque échantillon, entre 0 et 255.
    std::vector<double> values;
    /// Nombre d'échantillons dont au moins un pixel de la bande est saturé (≥ 254) dans le canal choisi.
    int saturated = 0;

    double lenPx() const;
    /// Intensité de l'échantillon le plus proche de la fraction `t` de la coupe.
    double valueAt(double t) const;
};

/// Échantillonne l'image tous les pixels le long de `seg`, en moyennant sur une bande
/// perpendiculaire de `bandWidth` pixels (ce qui atténue le quadrillage du papier).
/// `img` est au format `QImage::Format_RGB888`.
Profile sampleProfile(const QImage &img, const Segment &seg, int bandWidth, Channel channel);

/// Profil au format tableur, colonnes `x (unité)` et `I`. Avec une virgule décimale (français…),
/// le séparateur de colonnes est `;` ; avec un point décimal (anglais), c'est `,`.
QString toCsv(const Profile &profile, const std::optional<Calibration> &calib, QChar dec);

/// Ajoute `.csv` à un nom saisi sans extension (certaines boîtes de dialogue ne le font pas), sauf si
/// cela écraserait un fichier existant que l'élève n'a pas choisi.
QString withCsvExtension(const QString &path);
