// Exemples de scripts Python du menu Python → Exemples : courts, rangés par thème (grandeurs,
// graphes, modélisation, fenêtres et fichiers, divers), utilisables tels quels dans n'importe quel
// document (ils créent les données dont ils ont besoin).

#pragma once

#include <QString>

#include <vector>

namespace pyexamples {

struct Example {
    QString category;
    QString title;
    QString code;
};

/// Les thèmes, dans l'ordre du menu.
const std::vector<QString> &categories();
const std::vector<Example> &all();

} // namespace pyexamples
