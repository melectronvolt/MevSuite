// Signalement des erreurs internes, au lieu de laisser le programme disparaître sans explication
// (sous Windows, il n'a pas de console) : le message est écrit dans un journal du dossier de
// configuration, puis montré dans une boîte de dialogue, qui rappelle que le travail en cours est
// sauvegardé automatiquement.
//
// Un plantage (accès mémoire invalide…) est noté aussi, avec la pile d'appels. Le programme qui
// plante ne peut plus rien afficher sans risque : il se relance avec `--crash`, et c'est ce second
// programme qui montre la boîte de dialogue.

#pragma once

#include <QString>
#include <QStringList>

namespace erreurs {

/// Installe les gestionnaires d'erreur fatale (exception non rattrapée, `qFatal`) et de plantage ;
/// `relance` faux (essais) : pas de relance après un plantage.
void installe(bool relance = true);

/// Journal et boîte de message, puis fermeture.
[[noreturn]] void signale(const QString &message);

/// Programme relancé après un plantage (`--crash <journal> <cause>`) : montre la boîte de message
/// et renvoie vrai ; faux sans cet argument.
bool montre_plantage(const QStringList &args);

} // namespace erreurs
