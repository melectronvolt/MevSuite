// Module natif `_mevreg` : tableaux (`Buf`) et calculs du mini-numpy, échanges avec le
// logiciel (grandeurs créées, figures, dessins), sources des modules Python intégrés.

#pragma once

#include <QJsonArray>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "python/arr.h"
#include "python/engine.h"

namespace python {

/// Ce que le script envoie au logiciel pendant son exécution.
struct HostOutput {
    std::vector<NamedValues> quantities;
    std::vector<FigureMsg> figures;
    std::vector<DrawingMsg> drawings;
    /// Paramètres du document (utilisables dans les cartes).
    std::vector<std::pair<QString, double>> docParams;
    /// Ordres du module `mevreg` (grandeurs, graphes, fenêtres, fichiers), dans l'ordre.
    QJsonArray commands;
};

/// Sortie du script en cours (le fil Python est seul à y accéder).
HostOutput &hostOutput();
/// Tableau Python (`_mevreg.Buf`) contenant `a` ; référence nouvelle.
PyObject *wrapBuf(arr::Arr a);
/// Création du module, à déclarer avant le démarrage de l'interpréteur.
PyObject *initModule();

} // namespace python
