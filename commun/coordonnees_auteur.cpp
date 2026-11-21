#include "coordonnees_auteur.h"

#include <QGridLayout>
#include <QLabel>
#include <QTextDocument>

namespace {

class Lien : public QLabel {
public:
    Lien(const QString &cible, const QString &adresse, QWidget *parent)
        : QLabel(QStringLiteral("<a href=\"%1\">%2</a>").arg(cible.toHtmlEscaped(), adresse.toHtmlEscaped()), parent)
    {
        setTextFormat(Qt::RichText);
        setTextInteractionFlags(Qt::TextBrowserInteraction);
        setOpenExternalLinks(true);
        setWordWrap(true);
    }

    QSize sizeHint() const override
    {
        // Réserver la largeur de l'adresse entière. QLabel peut la couper au tiret si la fenêtre se rétrécit.
        QTextDocument document;
        document.setDocumentMargin(0);
        document.setDefaultFont(font());
        document.setHtml(text());
        return {qCeil(document.idealWidth()), qCeil(document.size().height())};
    }
};

} // namespace

QLayout *mevsuite::coordonnees_auteur(const QString &libelleAuteur, const QString &libelleContact, const QString &libelleSite,
                                      const QString &nom, const QString &courriel, QWidget *parent)
{
    auto *grille = new QGridLayout;
    grille->setContentsMargins(0, 0, 0, 0);
    grille->setColumnStretch(0, 1);
    grille->setColumnStretch(3, 1);
    grille->setHorizontalSpacing(8);
    grille->setVerticalSpacing(3);
    const auto ligne = [&](int rang, const QString &texte, QLabel *valeur) {
        auto *libelle = new QLabel(texte, parent);
        libelle->setFont(valeur->font());
        libelle->setBuddy(valeur);
        valeur->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        grille->addWidget(libelle, rang, 1, Qt::AlignRight | Qt::AlignTop);
        grille->addWidget(valeur, rang, 2);
    };

    auto *auteur = new QLabel(nom, parent);
    QFont police = auteur->font();
    if (police.pointSizeF() > 0)
        police.setPointSizeF(police.pointSizeF() * 1.3);
    else
        police.setPixelSize(qRound(police.pixelSize() * 1.3));
    auteur->setFont(police);
    auteur->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ligne(0, libelleAuteur, auteur);
    ligne(1, libelleContact, new Lien(QStringLiteral("mailto:") + courriel, courriel, parent));
    ligne(2, libelleSite, new Lien(QStringLiteral("https://") + SITE_WEB + u'/', SITE_WEB, parent));
    return grille;
}
