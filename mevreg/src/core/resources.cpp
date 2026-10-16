#include "core/resources.h"

#include <QtGlobal>

#include <mutex>

static void initCoreResources()
{
    // hors de tout espace de noms, comme le demande Qt
    Q_INIT_RESOURCE(resources);
}

void ensureCoreResources()
{
    static std::once_flag once;
    std::call_once(once, initCoreResources);
}
