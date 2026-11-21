#include "core/resources.h"

#include <QtGlobal>

#include <mutex>

static void initialise_ressources_coeur()
{
    // hors de tout espace de noms, comme le demande Qt
    Q_INIT_RESOURCE(resources);
}

void assure_ressources_coeur()
{
    static std::once_flag once;
    std::call_once(once, initialise_ressources_coeur);
}
