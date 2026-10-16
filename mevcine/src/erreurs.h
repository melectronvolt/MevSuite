// Signalement des erreurs internes, au lieu de laisser le programme disparaître sans explication
// (sous Windows, il n'a pas de console) : le message est écrit dans un journal du dossier de
// configuration, puis montré dans une boîte de dialogue, qui rappelle que le travail en cours est
// sauvegardé automatiquement.

#pragma once

#include <QString>

namespace erreurs {

/// Installe les gestionnaires d'erreur fatale (exception non rattrapée, `qFatal`)
void installe();

/// Journal et boîte de message, puis fermeture.
[[noreturn]] void signale(const QString &message);

} // namespace erreurs
