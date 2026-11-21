// Notice intégrée au programme. F1 prépare dans le cache un site utilisable hors connexion,
// avec les pages française et anglaise, leurs captures et les photos des TP.

#pragma once

#include "i18n.h"

#include <QString>
#include <QStringList>

namespace help {

/// Sous-dossier des fichiers de la notice, dans le cache personnel.
inline const QString DIR_NAME = QStringLiteral("mevpix-help");

/// Fichiers du site, tels qu'ils sont écrits sur le disque.
QStringList fichiers();

/// Actualise les fichiers absents ou différents, puis renvoie le chemin de la page à ouvrir, ou vide en cas d'échec
/// (`error` reçoit alors la raison). Un dossier temporaire privé prend le relais si le cache est inaccessible.
QString ecrit(Lang lang, QString *error = nullptr);

} // namespace help
