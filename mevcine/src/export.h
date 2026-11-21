// Les deux sorties de MeVCine : un fichier CSV standard, et un texte Python (tableaux numpy `lst_t`,
// `lst_x`, `lst_y`) à coller dans un éditeur Python ou un notebook. Les commentaires du texte Python
// sont dans la langue de l'interface.
//
// Chaque objet garde la même série de dates. Les positions absentes restent vides en CSV et valent
// np.nan en Python. La date zéro est celle de la première image mesurée.

#pragma once

#include "pointage.h"

#include <QString>

#include <optional>

/// Le fichier CSV, au format standard (RFC 4180) : virgule entre les champs, point décimal, une
/// ligne d'en-tête, fins de ligne CRLF. Il se lit tel quel avec `numpy.genfromtxt`, `pandas.read_csv`
/// ou un tableur.
QString texte_csv(const Pointage &p);

/// Écrit le fichier CSV ; renvoie le message d'erreur, vide en cas de succès.
QString ecrit_csv(const QString &chemin, const Pointage &p);

/// Le texte copié pour Python : les dates et les positions sous forme de tableaux numpy, avec des
/// noms clairs (`lst_t`, `lst_x`, `lst_y`, ou `lst_x1`, `lst_y1`... pour plusieurs objets)
QString texte_python(const Pointage &p, const std::optional<QString> &video);
