// Notice d'utilisation : un petit site HTML autonome (une page par langue, feuille de style,
// captures d'écran) intégré au programme (ressources Qt). Au clic sur « Aide », les fichiers sont
// écrits sur le disque puis la page est ouverte dans le navigateur par défaut : cela fonctionne hors
// connexion, sur un poste du lycée comme sur une clé USB.

#pragma once

#include "i18n.h"
#include "settings.h"

#include <QString>
#include <QStringList>

namespace help {

/// Sous-dossier des fichiers de la notice, dans le dossier temporaire du système.
inline const QString DIR_NAME = QStringLiteral("mevsono-help");

/// Fichiers du site, tels qu'ils sont écrits sur le disque.
QStringList files();

/// Écrit la notice sur le disque et renvoie le chemin de la page à ouvrir, ou vide en cas d'échec
/// (`error` reçoit alors la raison). Elle est refaite à chaque ouverture dans le dossier temporaire
/// du système.
QString write(Lang lang, const SettingsFiles &files, QString *error);

} // namespace help
