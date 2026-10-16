// Jeux de données d'exemple, pour découvrir le logiciel ou préparer un TP.

#pragma once

#include "core/workspace.h"

#include <QStringList>

namespace examples {

/// Noms des exemples, dans l'ordre du menu.
const QStringList &names();
/// Espace de travail de l'exemple `index`.
Workspace build(int index);

} // namespace examples
