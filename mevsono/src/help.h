// Notice d'utilisation : un petit site HTML autonome (deux pages, captures, sons de TP et navigation)
// intégré au programme (ressources Qt). Au clic sur « Aide », les fichiers sont
// écrits sur le disque puis la page est ouverte dans le navigateur par défaut : cela fonctionne hors
// connexion, sur un poste du lycée comme sur une clé USB.

#pragma once

#include "i18n.h"

#include <QString>
#include <QStringList>

namespace help {

/// Dossier de la notice dans le cache de l’utilisateur, accessible au navigateur après la fermeture.
QString dossier();

/// Fichiers du site, tels qu'ils sont écrits sur le disque.
QStringList fichiers();

/// Écrit la notice sur le disque et renvoie le chemin de la page à ouvrir, ou vide en cas d'échec
/// (`error` reçoit alors la raison). Les fichiers absents ou différents de la version embarquée sont remplacés.
QString ecrit(Lang lang, QString *error);

} // namespace help
