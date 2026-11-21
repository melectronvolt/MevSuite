#pragma once
// Écriture complète d'un fichier.

#include <QByteArray>
#include <QIODevice>

/// Écrit tous les octets ; faux en cas d'échec, `errorString()` en donne alors la cause.
/// `write()` peut n'écrire qu'une partie des octets sans signaler d'erreur (disque plein) : un
/// `QSaveFile` validé ensuite remplacerait l'ancien fichier par un fichier tronqué. On écrit donc la
/// suite jusqu'au bout ; si la place manque, cet essai-là échoue avec la vraie cause.
inline bool ecrit_tout(QIODevice &f, const QByteArray &octets)
{
    qint64 fait = 0;
    while (fait < octets.size()) {
        const qint64 n = f.write(octets.constData() + fait, octets.size() - fait);
        if (n <= 0)
            return false;
        fait += n;
    }
    return true;
}
