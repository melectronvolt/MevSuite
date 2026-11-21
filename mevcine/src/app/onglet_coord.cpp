#include "onglet_coord.h"

#include "app.h"
#include "dessin.h"
#include "i18n.h"
#include "tableau.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

OngletCoord::OngletCoord(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *l = new QVBoxLayout(this);
    const auto bouton = [this](QWidget *parent, icones::Commande commande, auto action) {
        auto *b = new QPushButton(icones::commande(commande), QString(), parent);
        connect(b, &QPushButton::clicked, this, [this, action] {
            action();
            m_app.rafraichit();
        });
        return b;
    };

    // les exports, sur une ligne : ce qui compte, ce sont les coordonnées
    auto *groupe = new QGroupBox(this);
    auto *lg = new QHBoxLayout(groupe);
    m_titre_export = new QLabel(groupe);
    m_copier = bouton(groupe, icones::Commande::Copier, [this] { m_app.copie_python(); });
    m_copie = new QLabel(groupe);
    dessin::colore(m_copie, dessin::Ton::Reussite);
    m_csv = bouton(groupe, icones::Commande::ExporterCsv, [this] { m_app.exporte_csv(); });
    m_projet = bouton(groupe, icones::Commande::Enregistrer, [this] { m_app.ecrit_projet_ui(); });
    lg->addWidget(m_titre_export);
    lg->addWidget(m_copier);
    lg->addWidget(m_csv);
    lg->addWidget(m_projet);
    lg->addStretch(1);
    l->addWidget(groupe);
    l->addWidget(m_copie);

    // le tableau, sur toute la place qui reste : ses colonnes s'élargissent
    m_titre_tableau = dessin::titre_partie(this);
    m_tableau = new TableauMesures(app, this, TableauMesures::Presentation::Coordonnees);
    l->addWidget(m_titre_tableau);
    l->addWidget(m_tableau, 1);
}

void OngletCoord::actualise()
{
    m_titre_tableau->setText(trad("Tableau des mesures"));
    m_titre_export->setText(trad("Exporter les mesures :"));
    m_copier->setText(trad("Copier pour Python"));
    m_copier->setToolTip(
        trad("Copie lst_t, lst_x, lst_y (tableaux numpy) dans le presse-papiers, à coller dans un programme Python"));
    const QString sorties = m_app.etat_des_sorties();
    m_copie->setText(sorties);
    m_copie->setVisible(!sorties.isEmpty());
    m_csv->setText(trad("Enregistrer en CSV…"));
    m_csv->setToolTip(trad("Fichier CSV standard : virgule entre les champs, point décimal"));
    m_projet->setText(trad("Enregistrer le projet…"));
    m_projet->setToolTip(trad("Enregistre le chemin de la vidéo, le repère, l'échelle et les mesures, pour continuer plus tard"));
    m_tableau->actualise();
}
