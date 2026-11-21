// La fenêtre « À propos » : MeVCine, son auteur, sa licence, et les composants libres qu'il utilise,
// avec leur licence.

#pragma once

#include <QDialog>
#include <QPixmap>
#include <QString>

namespace a_propos {

inline const QString NOM = QStringLiteral("MeVCine");
inline const QString AUTEUR = QStringLiteral("Rémi MEVAERE");
inline const QString COURRIEL = QStringLiteral("suite@sciences-physiques.net");
inline const QString VERSION = QStringLiteral(MEVCINE_VERSION);

/// le logo de MeVCine, en grand (256 points)
QPixmap icone();

} // namespace a_propos

class FenetreAPropos : public QDialog {
    Q_OBJECT

public:
    explicit FenetreAPropos(QWidget *parent);
};
