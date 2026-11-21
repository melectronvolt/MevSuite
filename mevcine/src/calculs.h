// Grandeurs calculées à partir des pointages : le tableau des mesures (dates et positions) et, pour
// le grapheur du mode Expert, les vitesses et les accélérations. MeVCine ne calcule aucune énergie :
// c'est le travail de l'élève.

#pragma once

#include "pointage.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

/// La grandeur choisie sur un axe du grapheur. Les index d'image commencent à 0.
struct Grandeur {
    enum class Type { Temps, X, Y, Vitesse, Vx, Vy, Acceleration, Ax, Ay };
    Type type;
    int objet;
    QString nom;
    std::optional<double> valeur(const Pointage &pointage, int index_image) const;
};

/// Nom d'une grandeur pour un objet : « x » s'il n'y a qu'un objet suivi, « x1 », « x2 »... sinon.
/// Ce sont aussi les noms des exports (`lst_x`).
QString nom_grandeur(const QString &lettres, int obj, int nb_obj);

/// Noms proposés dans les deux listes du grapheur, même avant le premier pointage.
std::vector<Grandeur> grandeurs_disponibles(int nombre_objets);

/// unité d'une grandeur, d'après son nom (« vx2 » → « m/s »)
QString unite(const QString &nom, bool metre);

/// Une date à afficher, avec assez de décimales pour distinguer deux images successives
/// (4 décimales à 240 images par seconde)
QString texte_date(double t, double delta_t);

/// Le tableau des mesures, sous forme de textes : une ligne par image pointée, avec la date depuis
/// la première image pointée et les positions de chaque objet dans le repère choisi
struct Tableau {
    QStringList en_tetes;
    std::vector<QStringList> lignes;
    /// index (à partir de 0) de l'image de chaque ligne
    std::vector<int> images;

    /// Le tableau compact de pointage affiche seulement le numéro de l'image.
    static Tableau construit(const Pointage &p, bool numeros_seuls = false);
    bool operator==(const Tableau &) const = default;
};
