// Grandeurs calculées à partir des pointages : le tableau des mesures (dates et positions) et, pour
// le grapheur du mode Expert, les vitesses et les accélérations. MeVCine ne calcule aucune énergie :
// c'est le travail de l'élève.

#pragma once

#include "pointage.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

/// Une série de valeurs, une par image (rien si non évaluable)
struct Serie {
    /// nom de la grandeur, suivi du numéro d'objet s'il y en a plusieurs (« vx », « vx2 »)
    QString nom;
    std::vector<std::optional<double>> valeurs;
};

/// Nom d'une grandeur pour un objet : « x » s'il n'y a qu'un objet suivi, « x1 », « x2 »... sinon.
/// Ce sont aussi les noms des exports (`lst_x`).
QString nom_grandeur(const QString &lettres, int obj, int nb_obj);

/// Calcule les grandeurs cinématiques de tous les objets : x, y, v, vx, vy, a, ax, ay
std::vector<Serie> grandeurs(const Pointage &p);

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

    static Tableau construit(const Pointage &p);
    bool operator==(const Tableau &) const = default;
};
