// Guide, référence Python, illustrations et mesures d’exemple intégrés au programme.
// F1 extrait la notice dans le cache de l’utilisateur et ouvre la page dans
// le navigateur. Les illustrations et les exemples restent disponibles hors connexion.

#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace help {

/// Page de la notice, et page de la référence Python (fonctions de numpy, matplotlib, pandas, mevreg).
inline const QString NOTICE = QStringLiteral("notice.html");
inline const QString PYTHON = QStringLiteral("python.html");

/// Fichiers du site, tels qu'ils sont écrits sur le disque.
QStringList fichiers();

/// Écrit la notice sur le disque et renvoie le chemin de `page`, ou vide en cas d'échec (`erreur`
/// reçoit alors la raison).
QString ecrit(const QString &page, QString *erreur);

/// Ouverture d'une adresse dans le navigateur (QDesktopServices::openUrl) ; les essais la remplacent.
extern std::function<bool(const QUrl &)> openUrl;

} // namespace help
