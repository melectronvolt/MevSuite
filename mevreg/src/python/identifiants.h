// Noms lus par un script pour suivre les mesures et signaler les renommages.
#pragma once

#include <QStringList>

namespace python {

/// Noms de variables dans le code et les expressions des chaînes formatées.
/// Les commentaires, le texte littéral et les noms d’attributs sont ignorés.
QStringList identifiants(const QString &code);

} // namespace python
