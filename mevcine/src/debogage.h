// Messages de débogage (option -d de la ligne de commande), écrits sur la sortie d'erreur.

#pragma once

#include <QString>

#include <atomic>
#include <cstdio>

/// niveau de verbosité des messages de débogage (option -d)
inline std::atomic<int> NIVEAU_DBG{0};

/// affiche un message de débogage si la verbosité est suffisante
inline void dbg_p(int niveau, const QString &message)
{
    if (niveau <= NIVEAU_DBG.load(std::memory_order_relaxed))
        std::fprintf(stderr, "%s\n", qUtf8Printable(message));
}
