// « Exporter vers Python » : script numpy + matplotlib reproduisant les données, les grandeurs
// calculées et les graphes, prêt pour Capytale.

#pragma once

#include "core/expr.h"

#include <QString>

class Workspace;

namespace pyexport {

/// Nom utilisable en Python (mots réservés suffixés de « _ »).
QString pyName(const QString &name);
/// Écriture Python d'un nombre (`np.nan` pour une case vide).
QString pyNum(double v);
/// Traduit une expression du tableur en expression numpy.
QString toNumpy(const expr::Node *e);
/// Script Python complet.
QString exportScript(const Workspace &ws);

} // namespace pyexport
