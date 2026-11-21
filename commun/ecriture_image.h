#pragma once

#include <QByteArray>

class QImage;
class QIODevice;
class QString;

namespace ecriture_image {

/// Encode un PNG ou un JPEG dans une sortie déjà ouverte, en vérifiant les écritures.
bool ecrit(QIODevice &sortie, const QImage &image, const QByteArray &format, QString *erreur = nullptr);

/// Remplace le fichier seulement quand l'image entière a été écrite.
bool enregistre(const QString &chemin, const QImage &image, const QByteArray &format, QString &erreur);

} // namespace ecriture_image
