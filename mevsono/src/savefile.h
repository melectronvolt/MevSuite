#pragma once

#include <QByteArray>
#include <QIODevice>

/// Écrit tous les octets ; faux en cas d'échec, `errorString()` en donne alors la cause.
/// `write()` peut n'écrire qu'une partie des octets sans signaler d'erreur (disque plein) : un
/// `QSaveFile` validé ensuite remplacerait l'ancien fichier par un fichier tronqué. On écrit donc la
/// suite jusqu'au bout ; si la place manque, cet essai-là échoue avec la vraie cause.
inline bool writeAll(QIODevice &device, const QByteArray &bytes)
{
    qint64 done = 0;
    while (done < bytes.size()) {
        const qint64 n = device.write(bytes.constData() + done, bytes.size() - done);
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}
