#pragma once

#include <QString>

class QLayout;
class QWidget;

namespace mevsuite {

inline const QString SITE_WEB = QStringLiteral("sciences-physiques.net");

QLayout *coordonnees_auteur(const QString &libelleAuteur, const QString &libelleContact, const QString &libelleSite,
                            const QString &nom, const QString &courriel, QWidget *parent);

} // namespace mevsuite
