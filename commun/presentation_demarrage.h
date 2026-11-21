#pragma once

#include <QIcon>
#include <QString>

class QWidget;

namespace mevsuite {

struct PresentationDemarrage {
    QIcon icone;
    QString nom;
    QString description;
    QString version;
    QString auteur;

    void dessine(QWidget &ecran, qreal opacite) const;
};

} // namespace mevsuite
