#include "presentation_demarrage.h"

#include "coordonnees_auteur.h"

#include <QFontMetrics>
#include <QPainter>
#include <QWidget>

#include <algorithm>

void mevsuite::PresentationDemarrage::dessine(QWidget &ecran, qreal opacite) const
{
    QPainter p(&ecran);
    p.setOpacity(opacite);
    p.fillRect(ecran.rect(), QColor(17, 26, 37));
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto police = [&](int pixels, QFont::Weight graisse = QFont::Normal) {
        QFont f = ecran.font();
        f.setPixelSize(pixels);
        f.setWeight(graisse);
        return f;
    };
    const QFont titre = police(56, QFont::DemiBold);
    const QFont sousTitre = police(18);
    const QFont petite = police(13);
    const QColor secondaire(173, 188, 203);

    const int marge = std::min(24, ecran.height() / 16);
    const QRect zone = ecran.rect().adjusted(marge, marge, -marge, -marge);
    const int hauteurTitre = QFontMetrics(titre).height();
    const int alignement = Qt::AlignCenter | Qt::TextWordWrap;
    const int hauteurDescription = QFontMetrics(sousTitre).boundingRect(zone, alignement, description).height();
    const int hauteurPetite = QFontMetrics(petite).height();
    const int hauteurCentrale = zone.height() - hauteurPetite - 16;
    const int cote = std::min({144, zone.width(), hauteurCentrale / 3});
    const int hauteurBloc = cote + 16 + hauteurTitre + 4 + hauteurDescription + 10 + hauteurPetite;
    int y = zone.top() + (hauteurCentrale - hauteurBloc) / 2;
    icone.paint(&p, QRect(zone.left() + (zone.width() - cote) / 2, y, cote, cote));

    const auto ligne = [&](int haut, int hauteur, const QString &texte, const QFont &f, const QColor &couleur) {
        p.setFont(f);
        p.setPen(couleur);
        p.drawText(QRect(zone.left(), haut, zone.width(), hauteur), alignement, texte);
    };
    y += cote + 16;
    ligne(y, hauteurTitre, nom, titre, Qt::white);
    y += hauteurTitre + 4;
    ligne(y, hauteurDescription, description, sousTitre, QColor(213, 222, 231));
    y += hauteurDescription + 10;
    ligne(y, hauteurPetite, version, petite, secondaire);
    ligne(zone.top() + zone.height() - hauteurPetite, hauteurPetite,
          auteur + QStringLiteral("  ·  ") + SITE_WEB, petite, secondaire);
}
