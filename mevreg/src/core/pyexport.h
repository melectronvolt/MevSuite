// « Exporter vers Python » : script numpy + matplotlib reproduisant les données, les grandeurs
// calculées et les graphes, prêt pour Capytale.

#pragma once

#include "core/expr.h"

#include <QString>

class Seance;

namespace pyexport {

/// Nom utilisable en Python (mots réservés suffixés de « _ »).
QString nom_python(const QString &name);
/// Écriture Python d'un nombre (`np.nan` pour une case vide).
QString nombre_python(double v);
/// Traduit une expression du tableur en expression numpy.
QString en_numpy(const expr::Node *e);
/// Script Python complet.
QString exporte_script(const Seance &ws);

} // namespace pyexport
