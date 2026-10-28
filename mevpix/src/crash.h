// Dernier recours en cas d'erreur fatale : le message est écrit dans `mevpix.log` (à côté du
// fichier de réglages) et affiché dans une boîte de message avant la fermeture. Sans cela, la
// version Windows, qui n'a pas de console, disparaîtrait sans explication.
//
// Un plantage (accès mémoire invalide…) est noté aussi, avec la pile d'appels. Le programme qui
// plante ne peut plus rien afficher sans risque : il se relance avec `--crash`, et c'est ce second
// programme qui montre la boîte de message.

#pragma once

#include "i18n.h"

#include <QString>
#include <QStringList>

namespace crash {

/// Installe les gestionnaires d'erreur fatale (exception non rattrapée, `qFatal`) et de plantage :
/// journal, puis boîte de message (montrée par une copie du programme, relancée avec `--crash`, si
/// l'erreur survient hors du fil de l'interface). `relaunch` faux (essais) : pas de relance.
void install(const QString &logPath, Lang lang, bool relaunch = true);

/// Journal et boîte de message, puis fermeture.
[[noreturn]] void report(const QString &message);

/// Programme relancé après un plantage (`--crash <journal> <cause>`) : montre la boîte de message
/// et renvoie vrai ; faux sans cet argument.
bool showCrash(const QStringList &args, Lang lang);

} // namespace crash
