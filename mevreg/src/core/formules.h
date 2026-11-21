// Ordre commun au recalcul du tableur et à l'écriture du script Python.
#pragma once

#include "core/expr.h"

class Document;

struct OrdreFormules {
    /// Une entrée par colonne du document, pour retrouver la formule déjà lue.
    std::vector<expr::Parsed> analyses;
    std::vector<size_t> calculables;
    /// Colonnes qui dépendent directement ou indirectement d'une boucle.
    std::vector<size_t> bloquees;
    /// Pour une colonne d'incertitude, indices des grandeurs qui l'utilisent.
    std::vector<std::vector<size_t>> mesuresParIncertitude;
    /// Grandeurs calculées après leur propre incertitude. Seule leur longueur
    /// peut nécessiter un deuxième passage, sans créer de dépendance de valeurs.
    std::vector<size_t> mesuresDifferees;
};

OrdreFormules ordonne_formules(const Document &doc);
