#include "jpeg.h"

#include <QFile>

namespace {

// Un marqueur JPEG commence par FF. Dans les pixels comprimés, un octet FF est
// écrit FF 00 pour le distinguer d'un marqueur. On lit par blocs, y compris si
// les deux octets d'un marqueur se trouvent de part et d'autre d'une jonction.
class LecteurMarqueurs {
public:
    explicit LecteurMarqueurs(QFile &fichier) : m_fichier(fichier) {}

    int octet() { return disponible() ? static_cast<unsigned char>(m_bloc[m_position++]) : -1; }
    void saute(int taille) { m_position += taille; }

    int suivant()
    {
        while (disponible()) {
            const auto prefixe = m_bloc.indexOf(char(0xff), m_position);
            if (prefixe < 0) {
                m_position = m_bloc.size();
                continue;
            }
            m_position = prefixe + 1;
            int code;
            do {
                code = octet();
            } while (code == 0xff);
            if (code != 0)
                return code;
        }
        return -1;
    }

private:
    bool disponible()
    {
        while (m_position >= m_bloc.size()) {
            m_position -= m_bloc.size();
            m_bloc = m_fichier.read(64 * 1024);
            if (m_bloc.isEmpty())
                return false;
        }
        return true;
    }

    QFile &m_fichier;
    QByteArray m_bloc;
    qsizetype m_position = 0;
};

} // namespace

bool jpeg_complet(QFile &fichier)
{
    LecteurMarqueurs flux(fichier);
    if (flux.octet() != 0xff || flux.octet() != 0xd8)
        return false;
    bool pixelsRencontres = false;
    for (int code = flux.suivant(); code >= 0; code = flux.suivant()) {
        if (code == 0xd9) // fin de l'image principale
            return pixelsRencontres;
        if (code == 0xd8) // une deuxième image commence avant la fin de la première
            return false;
        if (code == 0x01 || (code >= 0xd0 && code <= 0xd7))
            continue; // marqueurs autonomes, dont les repères de reprise dans les pixels

        // La longueur inclut ses deux octets. Sauter le segment en entier évite de
        // prendre la fin d'une miniature EXIF pour la fin de la photo.
        const int haut = flux.octet();
        const int bas = flux.octet();
        const int taille = haut * 256 + bas;
        if (bas < 0 || taille < 2)
            return false;
        flux.saute(taille - 2);
        if (code == 0xda) // un JPEG progressif contient plusieurs séries de pixels
            pixelsRencontres = true;
    }
    return false;
}
