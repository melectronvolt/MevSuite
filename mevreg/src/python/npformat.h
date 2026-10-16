// Écriture des tableaux comme numpy (`str` et `repr`), pour qu'un `print` donne le même texte
// que dans Capytale.

#pragma once

#include "python/arr.h"

#include <QString>

namespace npformat {

/// `str(tableau)`
QString arrayStr(const arr::Arr &a);
/// `repr(tableau)`
QString arrayRepr(const arr::Arr &a);

} // namespace npformat
