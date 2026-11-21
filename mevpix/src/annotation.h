#pragma once

#include "i18n.h"
#include "session.h"

struct OptionsAnnotation {
    bool etalon;
    /// 0 pour toutes les mesures, -1 pour aucune, sinon le numéro de la mesure.
    int mesure;
    bool coupe;
    bool graphique;
};

/// Même composition pour l'aperçu, le presse-papiers et le PNG. Une limite vide garde la pleine résolution.
QImage image_annotee(Seance &session, const Textes &tr, const OptionsAnnotation &options, QSize limite = {});
