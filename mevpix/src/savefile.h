#pragma once

#include <QByteArray>
#include <QIODevice>

/// Écrit tous les octets ; faux en cas d'échec, `errorString()` en donne alors la cause.
/// `write()` peut n'écrire qu'une partie des octets sans signaler d'erreur (disque plein) : un
/// `QSaveFile` validé ensuite remplacerait l'ancien fichier par un fichier tronqué. On écrit donc la
/// suite jusqu'au bout ; si la place manque, cet essai-là échoue avec la vraie cause.
inline bool ecrit_tout(QIODevice &destination, const QByteArray &donnees)
{
    qint64 position = 0;
    while (position < donnees.size()) {
        const qint64 ecrits = destination.write(donnees.constData() + position, donnees.size() - position);
        if (ecrits <= 0)
            return false;
        position += ecrits;
    }
    return true;
}
