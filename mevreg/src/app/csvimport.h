// Import d'un fichier de données (CSV, texte) avec aperçu : le fichier tel qu'il est, ce que chaque
// ligne donne (noms, unités, valeurs, ignorée), les colonnes qui seront importées avec leur nom et
// leur unité (modifiables), et les problèmes repérés (cases qui ne sont pas des nombres, lignes
// d'un autre nombre de cases, noms déjà pris). Les réglages détectés se corrigent : séparateur des
// valeurs, séparateur décimal, lignes des noms et des unités, première ligne de valeurs, lignes
// vides.

#pragma once

#include "core/plotspec.h"

#include <optional>
#include <vector>

class MainWindow;
class QString;

namespace csvimport {

/// Ouvre l'aperçu du fichier `path` ; les grandeurs vont dans le tableur `table`, sinon dans un
/// nouveau tableur au nom du fichier. Un fichier illisible ou vide est signalé dans la barre d'état.
void open(MainWindow &win, const QString &path, std::optional<ViewId> table);

/// Export de grandeurs en CSV : séparateur des valeurs, séparateur décimal, en-têtes et codage
/// au choix, avec un aperçu des premières lignes, puis le nom du fichier.
void exportFile(MainWindow &win, const std::vector<QId> &ids);

} // namespace csvimport
