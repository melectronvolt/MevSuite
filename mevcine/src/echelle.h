// Échelle spatiale : un étalon tracé sur l'image et sa longueur réelle. Les points de l'étalon sont
// en pixels de l'image native de la vidéo (après rotation).

#pragma once

#include "vecteur.h"

struct Echelle {
    Vecteur p1;
    Vecteur p2;
    /// longueur de l'étalon en mètre
    double longueur_reelle_etalon = 1.0;

    /// vrai si l'échelle a été faite (les deux points sont distincts)
    bool est_definie() const { return p1 != p2 && etalon_pixels() > 1.0; }

    double etalon_pixels() const { return (p1 - p2).module(); }

    /// nombre de mètres par pixel (1 si l'échelle n'est pas définie)
    double metres_par_pixel() const { return est_definie() ? longueur_reelle_etalon / etalon_pixels() : 1.0; }

    /// nombre de pixels par mètre (1 si l'échelle n'est pas définie)
    double pixels_par_metre() const { return est_definie() ? etalon_pixels() / longueur_reelle_etalon : 1.0; }

    bool operator==(const Echelle &) const = default;
};
