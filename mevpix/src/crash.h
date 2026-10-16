// Dernier recours en cas d'erreur fatale : le message est écrit dans `mevpix.log` (à côté du
// fichier de réglages) et affiché dans une boîte de message avant la fermeture. Sans cela, la
// version Windows, qui n'a pas de console, disparaîtrait sans explication.

#pragma once

#include "i18n.h"

#include <QString>

namespace crash {

/// Installe les gestionnaires d'erreur fatale (exception non rattrapée, `qFatal`) : journal, puis
/// boîte de message si l'erreur survient dans le fil de l'interface.
void install(const QString &logPath, Lang lang);

/// Journal et boîte de message, puis fermeture.
[[noreturn]] void report(const QString &message);

} // namespace crash
