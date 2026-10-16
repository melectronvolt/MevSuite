// Détection automatique d'objets mobiles par recherche de motif : on cherche l'endroit où la somme
// des carrés des différences entre le motif et l'image est minimale (méthode `TM_SQDIFF` d'OpenCV).
//
// Pour les grandes vidéos (1080p, 4K), on cherche d'abord dans des images réduites, puis on affine en
// pleine résolution autour des meilleurs candidats : la recherche est des dizaines de fois plus
// rapide, et la position trouvée reste précise au pixel près.
//
// Les images sont au format `QImage::Format_RGB888`.

#pragma once

#include <QImage>
#include <QPoint>

#include <optional>

namespace detect {

/// Trouve la position d'un motif dans une image.
/// @param motif l'image partielle à retrouver
/// @param image l'image où chercher
/// @param zone_proche coin haut-gauche de la détection précédente : on cherche d'abord dans son
///   voisinage
/// @return le coin haut-gauche de l'endroit où se trouve le motif
std::optional<QPoint> detect_part(const QImage &motif, const QImage &image, std::optional<QPoint> zone_proche);

/// découpe un motif dans une image ; le rectangle est rogné aux bords
std::optional<QImage> decoupe(const QImage &image, int x, int y, int w, int h);

/// réduit une image d'un facteur entier, par moyenne des pixels
QImage reduit(const QImage &img, int f);

/// Facteur de réduction pour la recherche grossière : 1 (aucune réduction) pour les vidéos de moins
/// de 1280 pixels de large ; le motif réduit doit garder au moins 12 pixels de côté.
int facteur(const QImage &image, const QImage &motif);

} // namespace detect
