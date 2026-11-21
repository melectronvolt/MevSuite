#pragma once

#include "pointage.h"
#include "prefs.h"

namespace seance {

/// Les projets et les secours utilisent exactement le même texte.
QString texte(prefs::Config reglages, const Pointage &pointage, std::optional<Pointage::ProchainClic> en_cours);

/// Rétablit les positions et leur provenance. Le repère, l'échelle et le nombre d'objets doivent
/// déjà être réglés. Renvoie la place à reprendre si une série manuelle était interrompue.
std::optional<Pointage::ProchainClic> restaure_mesures(const prefs::Projet &projet, Pointage &pointage);

/// Reconnaît la vidéo même si le projet et la vidéo ont changé de dossier ensemble.
QString signature_video(const QString &chemin);

}
