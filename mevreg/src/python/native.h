// Module natif d’échange entre Python et le document MeVReg.

#pragma once

#include <QJsonArray>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "core/document.h"
#include "python/engine.h"

namespace python {

/// Ce que le script envoie au logiciel pendant son exécution.
struct HostOutput {
    std::vector<NamedValues> quantities;
    /// Valeurs de chaque appel à `grandeur()` (`mr.creer`), dans l'ordre : l'ordre « grandeur » du
    /// script renvoie à la sienne par son indice.
    std::vector<NamedValues> given;
    std::vector<FigureMsg> figures;
    std::vector<DrawingMsg> drawings;
    /// État du document vu par le script pendant son exécution.
    Document document;
    /// Ordres du module `mevreg` (grandeurs, graphes, fenêtres, fichiers), dans l'ordre.
    QJsonArray commands;
};

/// Sortie du script en cours (le fil Python est seul à y accéder).
HostOutput &sortie_hote();
/// Copie les valeurs pour le script.
PyObject *liste_reels(const std::vector<double> &v);
/// Création du module, à déclarer avant le démarrage de l'interpréteur.
PyObject *initialise_module();

} // namespace python
