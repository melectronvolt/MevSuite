#include "ecriture_image.h"

#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>

namespace ecriture_image {
namespace {

// L'encodeur PNG de Qt 6.11 abandonne ses tampons si le périphérique lui renvoie une
// erreur en cours de compression. Garder la première erreur et le laisser finir son
// encodage. Les blocs suivants sont écartés et l'appelant annule le fichier temporaire.
class SortieImage : public QIODevice {
public:
    explicit SortieImage(QIODevice &destination) : m_destination(destination) { open(WriteOnly | Unbuffered); }
    bool isSequential() const override { return true; }
    bool echec() const { return m_echec; }

protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *bloc, qint64 taille) override
    {
        qint64 position = 0;
        while (!m_echec && position < taille) {
            const qint64 n = m_destination.write(bloc + position, taille - position);
            if (n <= 0) {
                m_echec = true;
                setErrorString(m_destination.errorString());
            } else {
                position += n;
            }
        }
        return taille;
    }

private:
    QIODevice &m_destination;
    bool m_echec = false;
};

} // namespace

bool ecrit(QIODevice &sortie, const QImage &image, const QByteArray &format, QString *erreur)
{
    SortieImage flux(sortie);
    QImageWriter encodeur(&flux, format);
    if (encodeur.write(image) && !flux.echec())
        return true;
    if (erreur)
        *erreur = flux.echec() ? flux.errorString() : encodeur.errorString();
    return false;
}

bool enregistre(const QString &chemin, const QImage &image, const QByteArray &format, QString &erreur)
{
    const QFileInfo cible(chemin);
    if (cible.exists() && !cible.isFile()) {
        erreur = QSaveFile::tr("Cannot open file for writing");
        return false;
    }
    QSaveFile fichier(chemin);
    if (fichier.open(QIODevice::WriteOnly)) {
        if (!ecrit(fichier, image, format, &erreur))
            return false;
        if (fichier.commit())
            return true;
    }
    erreur = fichier.errorString();
    return false;
}

} // namespace ecriture_image
