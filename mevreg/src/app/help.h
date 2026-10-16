// Notice d'utilisation : un petit site HTML autonome (la notice, la référence Python, la feuille de
// style, les captures d'écran) intégré au programme (ressources Qt). Aide → Notice d'utilisation
// (F1) écrit ces fichiers dans le dossier temporaire puis ouvre la page dans le navigateur par
// défaut : cela fonctionne hors connexion, sur un poste du lycée comme sur une clé USB.

#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace help {

/// Sous-dossier des fichiers de la notice, dans le dossier temporaire du système.
inline const QString DIR_NAME = QStringLiteral("mevreg-help");

/// Page de la notice, et page de la référence Python (fonctions de numpy, matplotlib, pandas, mevreg).
inline const QString NOTICE = QStringLiteral("notice.html");
inline const QString PYTHON = QStringLiteral("python.html");

/// Fichiers du site, tels qu'ils sont écrits sur le disque.
QStringList files();

/// Écrit la notice sur le disque et renvoie le chemin de `page`, ou vide en cas d'échec (`error`
/// reçoit alors la raison).
QString write(const QString &page, QString *error);

/// Ouverture d'une adresse dans le navigateur (QDesktopServices::openUrl) ; les essais la remplacent.
extern std::function<bool(const QUrl &)> openUrl;

} // namespace help
