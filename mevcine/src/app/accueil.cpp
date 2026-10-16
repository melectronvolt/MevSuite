#include "accueil.h"

#include "a_propos.h"
#include "app.h"
#include "dessin.h"
#include "i18n.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/// Le haut des deux pages : le logo (96 points), le nom (2,2 fois le texte, en gras) et le
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

/// deux boutons côte à côte, centrés
QHBoxLayout *rangee(QWidget *a, QWidget *b)
{
    auto *r = new QHBoxLayout();
    r->setSpacing(12);
    r->addStretch(1);
    r->addWidget(a);
    r->addWidget(b);
    r->addStretch(1);
    return r;
}

/// les deux boutons de même taille, plus hauts que la normale
template <typename B> void meme_taille(B *a, B *b)
{
    QSize t;
    for (B *x : {a, b}) {
        x->setMinimumSize(0, 0);
        x->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        t = t.expandedTo(x->sizeHint());
    }
    for (B *x : {a, b})
        x->setFixedSize(t);
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Carte
// ------------------------------------------------------------------------------------------------

Carte::Carte(QWidget *parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    dessin::style_police(this, 1.1);
}

void Carte::textes(const QString &titre, const QString &ligne)
{
    if (titre == text() && ligne == m_ligne)
        return;
    setText(titre);
    setAccessibleName(titre);
    setAccessibleDescription(ligne);
    m_ligne = ligne;
    updateGeometry();
    update();
}

QSize Carte::sizeHint() const
{
    QFont titre = font();
    titre.setWeight(QFont::DemiBold);
    const QFontMetrics ft(titre), fl(dessin::police(1.0));
    const int marge = fl.height();
    const int largeur = std::max(ft.horizontalAdvance(text()), fl.horizontalAdvance(m_ligne));
    return {largeur + 3 * marge, ft.height() + fl.height() + 2 * marge};
}

void Carte::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    QColor fond = pal.color(QPalette::Button);
    if (isDown())
        fond = fond.darker(115);
    else if (underMouse())
        fond = fond.lighter(dessin::sombre(pal) ? 135 : 104);
    const bool en_vue = underMouse() || hasFocus();
    p.setPen(QPen(en_vue ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid), 1.0));
    p.setBrush(fond);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    // le titre en demi-gras, la ligne plus discrète, centrés ensemble
    QFont titre = font();
    titre.setWeight(QFont::DemiBold);
    const QFont police_ligne = dessin::police(1.0);
    const QFontMetrics ft(titre), fl(police_ligne);
    const int haut = (height() - ft.height() - fl.height()) / 2;
    p.setFont(titre);
    p.setPen(pal.color(QPalette::ButtonText));
    p.drawText(QRect(0, haut, width(), ft.height()), Qt::AlignCenter, text());
    p.setFont(police_ligne);
    p.setPen(pal.color(QPalette::PlaceholderText));
    p.drawText(QRect(0, haut + ft.height(), width(), fl.height()), Qt::AlignCenter, m_ligne);
}

void Carte::enterEvent(QEnterEvent *)
{
    update();
}

void Carte::leaveEvent(QEvent *)
{
    update();
}

void Carte::changeEvent(QEvent *event)
{
    QAbstractButton::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        updateGeometry();
}

// ------------------------------------------------------------------------------------------------
// PageAccueil
// ------------------------------------------------------------------------------------------------

PageAccueil::PageAccueil(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(u"accueil"_s);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(24, 24, 24, 24);
    m_sous_titre = entete(this, l);
    m_simplifie = new Carte(this);
    m_simplifie->setObjectName(u"carte_simplifie"_s);
    m_expert = new Carte(this);
    m_expert->setObjectName(u"carte_expert"_s);
    connect(m_simplifie, &Carte::clicked, this, [this] {
        m_app.change_mode(Mode::Simplifie);
        m_app.rafraichit();
    });
    connect(m_expert, &Carte::clicked, this, [this] {
        m_app.change_mode(Mode::Expert);
        m_app.rafraichit();
    });
    l->addLayout(rangee(m_simplifie, m_expert));
    m_remarque = pied(this, l);
}

void PageAccueil::maj()
{
    m_sous_titre->setText(trad("Étude cinématique de vidéos"));
    m_simplifie->textes(trad("Mode simplifié"), trad("Pas à pas, pour débuter"));
    m_expert->textes(trad("Mode expert"), trad("Toutes les fonctions"));
    m_remarque->setText(trad("Le mode se change à tout moment, sans perdre les mesures."));
    meme_taille(m_simplifie, m_expert);
}

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
    auto *r = new QHBoxLayout();
    r->addStretch(1);
    r->addWidget(m_ouvrir);
    r->addStretch(1);
    l->addLayout(r);
    m_glissez = pied(this, l);
}

void PageSansVideo::maj()
{
    m_sous_titre->setText(m_app.simplifie() ? trad("Mode simplifié") : trad("Mode expert"));
    m_ouvrir->setText(trad("📂 Ouvrir une vidéo…"));
    m_ouvrir->setToolTip(trad("Vidéos de téléphone (iPhone, Android) et d'appareil photo : MP4, MOV, AVI, MKV, WebM…\n"
                              "H.264, HEVC, AV1, VP9, ProRes ; HDR, vidéos en portrait, ralenti."));
    m_glissez->setText(trad("ou glissez une vidéo (ou un projet) sur la fenêtre"));
    // le bouton plus large et plus haut que la normale
    m_ouvrir->setMinimumSize(0, 0);
    m_ouvrir->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    const QSize t = m_ouvrir->sizeHint();
    m_ouvrir->setFixedSize(t.width() + t.height(), static_cast<int>(t.height() * 1.4));
}
