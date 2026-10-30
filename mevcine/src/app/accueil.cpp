#include "accueil.h"

#include "a_propos.h"
#include "app.h"
#include "dessin.h"
#include "i18n.h"
#include "icones.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/// Le haut de la page : le logo (96 points), le nom (2,2 fois le texte, en gras) et le
/// sous-titre (1,15 fois, discret), centrés ; renvoie le sous-titre
QLabel *entete(QWidget *page, QVBoxLayout *l)
{
    const auto centre = [page, l](QLabel *label) {
        label->setParent(page);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignHCenter);
        l->addWidget(label);
        return label;
    };
    l->addStretch(1);
    auto *logo = new QLabel(page);
    logo->setPixmap(a_propos::icone().scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    l->addWidget(logo, 0, Qt::AlignHCenter);
    l->addSpacing(8);
    dessin::style_police(centre(new QLabel(a_propos::NOM)), 2.2, true);
    QLabel *sous_titre = centre(new QLabel);
    dessin::style_police(sous_titre, 1.15);
    dessin::discret(sous_titre);
    l->addSpacing(32);
    return sous_titre;
}

/// la ligne discrète, sous les boutons ; puis le bas de la page
QLabel *pied(QWidget *page, QVBoxLayout *l)
{
    l->addSpacing(10);
    auto *ligne = new QLabel(page);
    ligne->setWordWrap(true);
    ligne->setAlignment(Qt::AlignHCenter);
    dessin::discret(ligne);
    l->addWidget(ligne);
    l->addStretch(1);
    return ligne;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// PageSansVideo
// ------------------------------------------------------------------------------------------------

PageSansVideo::PageSansVideo(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(u"sans_video"_s);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(24, 24, 24, 24);
    m_sous_titre = entete(this, l);
    m_ouvrir = new QPushButton(this);
    m_ouvrir->setObjectName(u"ouvrir"_s);
    connect(m_ouvrir, &QPushButton::clicked, this, [this] {
        m_app.openfile_dossier_prefere();
        m_app.rafraichit();
    });
    // pas de vidéos d'exemple : on ouvre ses propres vidéos
    dessin::style_police(m_ouvrir, 1.1);
    // au premier démarrage, le choix du mode, à la place du bouton d'ouverture
    const auto mode = [this](const QString &nom, Mode m) {
        auto *b = new QPushButton(this);
        b->setObjectName(nom);
        dessin::style_police(b, 1.1);
        connect(b, &QPushButton::clicked, this, [this, m] {
            m_app.change_mode(m);
            m_app.affiche_statut(m == Mode::Simplifie ? trad("Mode simplifié choisi : le menu Mode permet d'en changer")
                                                      : trad("Mode expert choisi : le menu Mode permet d'en changer"),
                                 icones::Statut::Reussite);
            m_app.rafraichit();
        });
        return b;
    };
    m_simplifie = mode(u"mode_simplifie"_s, Mode::Simplifie);
    m_expert = mode(u"mode_expert"_s, Mode::Expert);
    auto *r = new QHBoxLayout();
    r->setSpacing(16);
    r->addStretch(1);
    r->addWidget(m_ouvrir);
    r->addWidget(m_simplifie);
    r->addWidget(m_expert);
    r->addStretch(1);
    l->addLayout(r);
    m_glissez = pied(this, l);
}

void PageSansVideo::maj()
{
    m_sous_titre->setText(trad("Étude cinématique de vidéos"));
    m_ouvrir->setText(trad("📂 Ouvrir une vidéo…"));
    m_ouvrir->setToolTip(trad("Vidéos de téléphone (iPhone, Android) et d'appareil photo : MP4, MOV, AVI, MKV, WebM…\n"
                              "H.264, HEVC, AV1, VP9, ProRes ; HDR, vidéos en portrait, ralenti."));
    m_simplifie->setText(trad("🪜 Mode simplifié\npas à pas, un seul objet"));
    m_simplifie->setToolTip(trad("Pour débuter : MeVCine guide chaque étape (échelle, origine, pointage, résultats)."));
    m_expert->setText(trad("🧰 Mode expert\ntoutes les fonctions"));
    m_expert->setToolTip(trad("Plusieurs objets, suivi automatique, trajectoires dans un autre référentiel, grapheur."));
    const bool choix = m_app.mode_a_choisir;
    m_ouvrir->setVisible(!choix);
    m_simplifie->setVisible(choix);
    m_expert->setVisible(choix);
    m_glissez->setText(choix ? trad("Choisissez votre façon de travailler : le menu Mode permet d'en changer à tout moment.")
                             : trad("ou glissez une vidéo (ou un projet) sur la fenêtre"));
    // les boutons plus larges et plus hauts que la normale ; ceux du mode, de la même taille
    for (QPushButton *b : {m_ouvrir, m_simplifie, m_expert}) {
        b->setMinimumSize(0, 0);
        b->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }
    QSize t = m_ouvrir->sizeHint();
    m_ouvrir->setFixedSize(t.width() + t.height(), static_cast<int>(t.height() * 1.4));
    t = m_simplifie->sizeHint().expandedTo(m_expert->sizeHint());
    const int ligne = m_ouvrir->fontMetrics().height();
    for (QPushButton *b : {m_simplifie, m_expert})
        b->setFixedSize(t.width() + 2 * ligne, t.height() + ligne);
}
