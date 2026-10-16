#include "guide.h"

#include "app.h"
#include "dessin.h"
#include "format.h"
#include "i18n.h"
#include "onglet_pointage.h"
#include "tableau.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

QString nom(Etape e)
{
    switch (e) {
    case Etape::Origine: return trad("Origine");
    case Etape::Axes: return trad("Axes");
    case Etape::Echelle: return trad("Échelle");
    case Etape::Pointage: return trad("Pointage");
    case Etape::Tableau: return trad("Tableau");
    case Etape::Export: return trad("Export");
    }
    return {};
}

std::optional<Etape> suivante(Etape e)
{
    const int i = static_cast<int>(e) + 1;
    return i < static_cast<int>(TOUTES_LES_ETAPES.size()) ? std::optional(TOUTES_LES_ETAPES[i]) : std::nullopt;
}

std::optional<Etape> precedente(Etape e)
{
    const int i = static_cast<int>(e) - 1;
    return i >= 0 ? std::optional(TOUTES_LES_ETAPES[i]) : std::nullopt;
}

std::pair<QString, QString> sens_en_mots(int sens_x, int sens_y)
{
    return {sens_x > 0 ? trad("x vers la droite") : trad("x vers la gauche"),
            sens_y > 0 ? trad("y vers le haut") : trad("y vers le bas")};
}

namespace {

QLabel *etiquette(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setWordWrap(true);
    return l;
}

/// titre d'une étape : en demi-gras, à la taille du texte
QLabel *titre(QWidget *parent)
{
    auto *l = etiquette(parent);
    dessin::demi_gras(l);
    return l;
}

/// une consigne
QLabel *consigne(QWidget *parent)
{
    return etiquette(parent);
}

/// une remarque, plus discrète
QLabel *remarque(QWidget *parent)
{
    auto *l = etiquette(parent);
    dessin::discret(l);
    return l;
}

/// un message de réussite
QLabel *reussite(QWidget *parent)
{
    auto *l = etiquette(parent);
    dessin::colore(l, dessin::Ton::Reussite);
    return l;
}

/// un message qui attend une action
QLabel *attention(QWidget *parent)
{
    auto *l = etiquette(parent);
    dessin::colore(l, dessin::Ton::Attention);
    return l;
}

QPushButton *bouton(QWidget *parent)
{
    return new QPushButton(parent);
}

/// une page d'étape : ses widgets, les uns sous les autres
QWidget *page(QWidget *parent, std::initializer_list<QWidget *> widgets)
{
    auto *w = new QWidget(parent);
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    for (QWidget *x : widgets) {
        x->setParent(w);
        l->addWidget(x);
    }
    return w;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// BoutonAxes
// ------------------------------------------------------------------------------------------------

BoutonAxes::BoutonAxes(int sens_x, int sens_y, QWidget *parent)
    : QAbstractButton(parent)
    , m_sx(sens_x)
    , m_sy(sens_y)
{
    setCheckable(true);
    setAutoExclusive(false);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
}

namespace {
/// hauteur du repère dessiné en haut d'un bouton d'orientation des axes, noms des axes compris
constexpr double HAUTEUR_REPERE = 96.0;
} // namespace

QSize BoutonAxes::sizeHint() const
{
    // les deux lignes de la description, à la taille du texte de l'interface
    const QFontMetrics fm(font());
    const auto [tx, ty] = sens_en_mots(m_sx, m_sy);
    const int texte = std::max(fm.horizontalAdvance(tx), fm.horizontalAdvance(ty));
    return {std::max(130, texte + 16), static_cast<int>(HAUTEUR_REPERE) + 2 * fm.height() + 10};
}

void BoutonAxes::changeEvent(QEvent *event)
{
    QAbstractButton::changeEvent(event);
    // agrandissement de l'interface : le bouton change de taille
    if (event->type() == QEvent::FontChange)
        updateGeometry();
}

QString BoutonAxes::description() const
{
    const auto [x, y] = sens_en_mots(m_sx, m_sy);
    return x + u", "_s + y;
}

void BoutonAxes::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor fond = pal.color(QPalette::Button);
    if (isChecked())
        fond = pal.color(QPalette::Highlight).lighter(dessin::sombre(pal) ? 60 : 170);
    else if (underMouse())
        fond = fond.lighter(dessin::sombre(pal) ? 130 : 105);
    p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    p.setBrush(fond);
    p.drawRoundedRect(r, 6, 6);
    if (isChecked()) {
        p.setPen(QPen(dessin::VERT, 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), 6, 6);
    }
    const QColor couleur = pal.color(QPalette::ButtonText);
    // le repère dessiné, noms des axes compris, occupe le haut du bouton
    const QPointF o(width() / 2.0, 46.0);
    dessin::repere(p, o, m_sx, m_sy, couleur, 26.0, false);
    p.setPen(Qt::NoPen);
    p.setBrush(couleur);
    p.drawEllipse(o, 2.5, 2.5);
    // la description, sur deux lignes, sous le repère
    const auto [tx, ty] = sens_en_mots(m_sx, m_sy);
    const QFontMetricsF fm(font());
    p.setFont(font());
    p.setPen(couleur);
    int k = 0;
    for (const QString &l : {tx, ty}) {
        p.drawText(QRectF(0, HAUTEUR_REPERE + fm.height() * k, width(), fm.height()), Qt::AlignCenter, l);
        ++k;
    }
}

// ------------------------------------------------------------------------------------------------
// App : les étapes
// ------------------------------------------------------------------------------------------------

bool App::etape_faite(Etape e) const
{
    switch (e) {
    case Etape::Origine: return guide.origine_choisie;
    case Etape::Axes: return guide.axes_choisis;
    case Etape::Echelle: return pointage.echelle.est_definie() && !etalon_en_attente;
    case Etape::Pointage:
    case Etape::Tableau: return pointage.a_des_pointages();
    case Etape::Export: return copie_a_jour();
    }
    return false;
}

bool App::etape_accessible(Etape e) const
{
    for (Etape x : TOUTES_LES_ETAPES) {
        if (x >= e)
            break;
        if (!etape_faite(x))
            return false;
    }
    return true;
}

void App::va_a_l_etape(Etape e)
{
    if (!etape_accessible(e) || etat == Etat::B || etat == Etat::AB)
        return;
    if (etat == Etat::C)
        annule_echelle();
    annule_refait();
    if (e >= Etape::Pointage && etat == Etat::A)
        debut_capture();
    guide.etape = e;
    choix_origine = e == Etape::Origine;
    statut.clear();
    if (e == Etape::Pointage)
        affiche_point_attendu();
}

void App::valide_saisie_etalon()
{
    const auto l = parse_longueur(guide.saisie_etalon, unite_etalon);
    if (l) {
        guide.erreur_etalon.reset();
        // une unité écrite après le nombre remplace celle de la liste
        change_unite_etalon(l->unite);
        valide_longueur_etalon(l->metres());
    } else {
        guide.erreur_etalon = l.error();
    }
}

// ------------------------------------------------------------------------------------------------
// PageGuide
// ------------------------------------------------------------------------------------------------

PageGuide::PageGuide(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *haut = new QVBoxLayout(this);
    haut->setContentsMargins(0, 0, 0, 0);
    haut->setSpacing(0);

    // le bandeau des six étapes
    auto *bandeau = new QWidget(this);
    auto *lb = new QHBoxLayout(bandeau);
    lb->setContentsMargins(8, 4, 8, 4);
    for (Etape e : TOUTES_LES_ETAPES) {
        auto *b = bouton(bandeau);
        b->setCheckable(true);
        b->setFlat(true);
        connect(b, &QPushButton::clicked, this, [this, e] {
            if (m_app.guide.etape != e)
                m_app.va_a_l_etape(e);
            m_app.rafraichit();
        });
        m_etapes[static_cast<size_t>(e)] = b;
        lb->addWidget(b);
        if (e != Etape::Export) {
            auto *s = new QLabel(u"›"_s, bandeau);
            dessin::discret(s);
            lb->addWidget(s);
        }
    }
    lb->addStretch(1);
    haut->addWidget(bandeau);
    haut->addWidget(dessin::trait(this));

    auto *corps = new QHBoxLayout();
    corps->setContentsMargins(0, 0, 0, 0);
    corps->setSpacing(0);
    haut->addLayout(corps, 1);

    // À gauche, le panneau latéral : la loupe et les coordonnées du point visé en haut, les consignes
    // de l'étape (qui défilent si la fenêtre est trop basse), puis Précédent et Suivant, toujours
    // visibles
    auto *gauche = new dessin::FondLateral(this);
    auto *lg = new QVBoxLayout(gauche);
    // à droite, la place du filet
    lg->setContentsMargins(8, 6, 9, 6);
    m_loupe = new PanneauLoupe(app, gauche);
    lg->addWidget(m_loupe);
    lg->addSpacing(4);
    lg->addWidget(dessin::trait(gauche));
    auto *consignes = new QWidget(gauche);
    auto *lcons = new QVBoxLayout(consignes);
    lcons->setContentsMargins(0, 4, 4, 4);
    m_refait = new QWidget(consignes);
    auto *lr = new QVBoxLayout(m_refait);
    lr->setContentsMargins(0, 0, 0, 0);
    m_refait_texte = attention(m_refait);
    m_refait_annuler = bouton(m_refait);
    connect(m_refait_annuler, &QPushButton::clicked, this, [this] {
        m_app.annule_refait();
        m_app.rafraichit();
    });
    lr->addWidget(m_refait_texte);
    lr->addWidget(m_refait_annuler, 0, Qt::AlignLeft);
    lr->addWidget(dessin::trait(m_refait));
    lcons->addWidget(m_refait);
    m_pages = new QStackedWidget(consignes);
    for (QWidget *p : {etape_origine(), etape_axes(), etape_echelle(), etape_pointage(), etape_tableau(), etape_export()}) {
        p->setSizePolicy(QSizePolicy::Preferred, m_pages->count() == 0 ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        m_pages->addWidget(p);
    }
    lcons->addWidget(m_pages);
    lcons->addStretch(1);
    lg->addWidget(new dessin::DefilementLateral(consignes, gauche), 1);
    lg->addWidget(dessin::trait(gauche));
    auto *navigation = new QHBoxLayout();
    m_precedent = bouton(gauche);
    m_suivant = bouton(gauche);
    dessin::demi_gras(m_suivant);
    connect(m_precedent, &QPushButton::clicked, this, [this] {
        if (const auto p = precedente(m_app.guide.etape))
            m_app.va_a_l_etape(*p);
        m_app.rafraichit();
    });
    connect(m_suivant, &QPushButton::clicked, this, [this] {
        if (const auto s = suivante(m_app.guide.etape))
            m_app.va_a_l_etape(*s);
        m_app.rafraichit();
    });
    navigation->addWidget(m_precedent);
    navigation->addStretch(1);
    navigation->addWidget(m_suivant);
    lg->addLayout(navigation);

    // l'image, et le tableau des mesures à droite
    auto *separation = new QSplitter(Qt::Horizontal, this);
    auto *centre = new QWidget(separation);
    auto *lc = new QVBoxLayout(centre);
    lc->setContentsMargins(0, 0, 0, 0);
    m_zone = new ZoneVideo(app, centre);
    m_controles_image = new ControlesImage(app, centre);
    lc->addWidget(m_zone, 1);
    lc->addWidget(m_controles_image);
    m_droite = new QWidget(separation);
    m_droite->setMinimumWidth(240);
    auto *ld = new QVBoxLayout(m_droite);
    ld->setContentsMargins(6, 6, 6, 4);
    m_titre_tableau = dessin::titre_partie(m_droite);
    m_tableau = new TableauMesures(app, m_droite);
    ld->addWidget(m_titre_tableau);
    ld->addWidget(m_tableau, 1);
    separation->addWidget(centre);
    separation->addWidget(m_droite);
    separation->setStretchFactor(0, 1);
    separation->setCollapsible(0, false);
    separation->setCollapsible(1, false);
    separation->setSizes({700, 300});
    corps->addWidget(new dessin::SeparationLaterale(gauche, separation, this), 1);
}

QWidget *PageGuide::etape_origine()
{
    m_o_titre = titre(this);
    m_o_consigne = consigne(this);
    m_o_reussite = reussite(this);
    m_o_remarque = remarque(this);
    m_o_rotation = new QWidget(this);
    auto *l = new QVBoxLayout(m_o_rotation);
    l->setContentsMargins(0, 10, 0, 0);
    m_o_couchee = etiquette(m_o_rotation);
    auto *boutons = new QHBoxLayout();
    m_o_gauche = bouton(m_o_rotation);
    m_o_droite = bouton(m_o_rotation);
    connect(m_o_gauche, &QPushButton::clicked, this, [this] {
        m_app.tourne_image(-90);
        m_app.rafraichit();
    });
    connect(m_o_droite, &QPushButton::clicked, this, [this] {
        m_app.tourne_image(90);
        m_app.rafraichit();
    });
    boutons->addWidget(m_o_gauche);
    boutons->addWidget(m_o_droite);
    boutons->addStretch(1);
    l->addWidget(m_o_couchee);
    l->addLayout(boutons);
    QWidget *w = page(this, {m_o_titre, m_o_consigne, m_o_reussite, m_o_remarque, m_o_rotation});
    static_cast<QVBoxLayout *>(w->layout())->addStretch(1);
    return w;
}

QWidget *PageGuide::etape_axes()
{
    m_a_titre = titre(this);
    m_a_consigne = consigne(this);
    auto *grille = new QWidget(this);
    auto *g = new QGridLayout(grille);
    g->setContentsMargins(0, 0, 0, 0);
    g->setSpacing(8);
    for (size_t k = 0; k < ORIENTATIONS.size(); ++k) {
        const auto [sx, sy] = ORIENTATIONS[k];
        auto *b = new BoutonAxes(sx, sy, grille);
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(b, &QAbstractButton::clicked, this, [this, sx, sy] {
            m_app.pointage.sens_x = sx;
            m_app.pointage.sens_y = sy;
            m_app.guide.axes_choisis = true;
            m_app.rafraichit();
        });
        m_a_boutons[k] = b;
        g->addWidget(b, static_cast<int>(k / 2), static_cast<int>(k % 2));
    }
    g->setColumnStretch(2, 1);
    m_a_reussite = reussite(this);
    m_a_remarque = remarque(this);
    QWidget *w = page(this, {m_a_titre, m_a_consigne, grille, m_a_reussite, m_a_remarque});
    static_cast<QVBoxLayout *>(w->layout())->addStretch(1);
    return w;
}

QWidget *PageGuide::etape_echelle()
{
    m_e_titre = titre(this);
    m_e_consigne_a = consigne(this);
    m_e_attention = attention(this);
    m_e_annuler = bouton(this);
    m_e_selection = bouton(this);
    connect(m_e_annuler, &QPushButton::clicked, this, [this] {
        m_app.annule_echelle();
        m_app.rafraichit();
    });
    connect(m_e_selection, &QPushButton::clicked, this, [this] {
        m_app.demande_echelle();
        m_app.rafraichit();
    });
    m_e_consigne_b = consigne(this);
    m_e_ligne = new QWidget(this);
    auto *l = new QHBoxLayout(m_e_ligne);
    l->setContentsMargins(0, 0, 0, 0);
    m_saisie = new QLineEdit(m_e_ligne);
    m_e_unite = new QComboBox(m_e_ligne);
    for (Unite u : TOUTES_LES_UNITES)
        m_e_unite->addItem(symbole(u), static_cast<int>(u));
    connect(m_e_unite, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_app.change_unite_etalon(static_cast<Unite>(m_e_unite->itemData(i).toInt()));
        m_app.guide.erreur_etalon.reset();
        m_app.rafraichit();
    });
    m_e_valider = bouton(m_e_ligne);
    connect(m_saisie, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_app.guide.saisie_etalon = t;
        m_app.guide.erreur_etalon.reset();
        m_app.rafraichit();
    });
    const auto valide = [this] {
        m_app.valide_saisie_etalon();
        m_app.rafraichit();
    };
    connect(m_saisie, &QLineEdit::returnPressed, this, valide);
    connect(m_e_valider, &QPushButton::clicked, this, valide);
    l->addWidget(m_saisie);
    l->addWidget(m_e_unite);
    l->addWidget(m_e_valider);
    l->addStretch(1);
    m_e_remarque = remarque(this);
    m_e_erreur = etiquette(this);
    dessin::colore(m_e_erreur, dessin::Ton::Erreur);
    m_e_etat = etiquette(this);
    QWidget *w = page(this, {m_e_titre, m_e_consigne_a, m_e_attention, m_e_annuler, m_e_selection, m_e_consigne_b,
                             m_e_ligne, m_e_remarque, m_e_erreur, m_e_etat});
    auto *lw = static_cast<QVBoxLayout *>(w->layout());
    lw->setAlignment(m_e_annuler, Qt::AlignLeft);
    lw->setAlignment(m_e_selection, Qt::AlignLeft);
    lw->insertSpacing(lw->indexOf(m_e_consigne_b), 10);
    lw->addStretch(1);
    return w;
}

QWidget *PageGuide::etape_pointage()
{
    m_p_titre = titre(this);
    m_p_consigne1 = consigne(this);
    m_p_consigne2 = consigne(this);
    m_p_remarque = remarque(this);
    m_p_nombre = etiquette(this);
    m_p_derniere = remarque(this);
    m_p_annuler = bouton(this);
    connect(m_p_annuler, &QPushButton::clicked, this, [this] {
        m_app.efface_point_precedent();
        m_app.rafraichit();
    });
    // « Vidéo au ralenti ? » : une partie qui se déplie
    m_p_ralenti = new QToolButton(this);
    m_p_ralenti->setCheckable(true);
    m_p_ralenti->setAutoRaise(true);
    m_p_ralenti->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_p_ralenti->setArrowType(Qt::RightArrow);
    m_p_ralenti_contenu = new QWidget(this);
    auto *l = new QVBoxLayout(m_p_ralenti_contenu);
    l->setContentsMargins(16, 0, 0, 0);
    m_p_ralenti_texte = remarque(m_p_ralenti_contenu);
    auto *ligne = new QHBoxLayout();
    m_p_ips = new QLabel(m_p_ralenti_contenu);
    m_p_saisie_ips = new QLineEdit(m_p_ralenti_contenu);
    m_p_saisie_ips->setFixedWidth(70);
    connect(m_p_saisie_ips, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_app.ips = t;
        m_app.verifie_ips();
        m_app.rafraichit();
    });
    ligne->addWidget(m_p_ips);
    ligne->addWidget(m_p_saisie_ips);
    ligne->addStretch(1);
    m_p_cadence = remarque(m_p_ralenti_contenu);
    l->addWidget(m_p_ralenti_texte);
    l->addLayout(ligne);
    l->addWidget(m_p_cadence);
    m_p_ralenti_contenu->hide();
    connect(m_p_ralenti, &QToolButton::toggled, this, [this](bool ouvert) {
        m_p_ralenti->setArrowType(ouvert ? Qt::DownArrow : Qt::RightArrow);
        m_p_ralenti_contenu->setVisible(ouvert);
    });
    QWidget *w = page(this, {m_p_titre, m_p_consigne1, m_p_consigne2, m_p_remarque, m_p_nombre, m_p_derniere,
                             m_p_annuler, m_p_ralenti, m_p_ralenti_contenu});
    auto *lw = static_cast<QVBoxLayout *>(w->layout());
    lw->setAlignment(m_p_annuler, Qt::AlignLeft);
    lw->insertSpacing(lw->indexOf(m_p_nombre), 8);
    lw->insertSpacing(lw->indexOf(m_p_ralenti), 8);
    lw->addStretch(1);
    return w;
}

QWidget *PageGuide::etape_tableau()
{
    m_t_titre = titre(this);
    m_t_consigne1 = consigne(this);
    m_t_consigne2 = consigne(this);
    m_t_reprendre = bouton(this);
    connect(m_t_reprendre, &QPushButton::clicked, this, [this] {
        m_app.va_a_l_etape(Etape::Pointage);
        m_app.rafraichit();
    });
    m_t_remarque = remarque(this);
    QWidget *w = page(this, {m_t_titre, m_t_consigne1, m_t_consigne2, m_t_reprendre, m_t_remarque});
    auto *lw = static_cast<QVBoxLayout *>(w->layout());
    lw->setAlignment(m_t_reprendre, Qt::AlignLeft);
    lw->addStretch(1);
    return w;
}

QWidget *PageGuide::etape_export()
{
    m_x_titre = titre(this);
    m_x_consigne1 = consigne(this);
    m_x_copier = bouton(this);
    connect(m_x_copier, &QPushButton::clicked, this, [this] {
        m_app.copie_python();
        m_app.rafraichit();
    });
    m_x_reussite = reussite(this);
    m_x_consigne2 = consigne(this);
    m_x_csv = bouton(this);
    connect(m_x_csv, &QPushButton::clicked, this, [this] {
        m_app.export_csv();
        m_app.rafraichit();
    });
    m_x_projet = bouton(this);
    connect(m_x_projet, &QPushButton::clicked, this, [this] {
        m_app.enregistre_ui();
        m_app.rafraichit();
    });
    m_x_apercu = etiquette(this);
    m_x_texte = new QPlainTextEdit(this);
    m_x_texte->setReadOnly(true);
    m_x_texte->setLineWrapMode(QPlainTextEdit::NoWrap);
    dessin::chiffres(m_x_texte);
    m_x_texte->setMaximumHeight(200);
    QWidget *w = page(this, {m_x_titre, m_x_consigne1, m_x_copier, m_x_reussite, m_x_consigne2, m_x_csv, m_x_projet,
                             m_x_apercu, m_x_texte});
    auto *lw = static_cast<QVBoxLayout *>(w->layout());
    for (QWidget *b : {static_cast<QWidget *>(m_x_copier), static_cast<QWidget *>(m_x_csv), static_cast<QWidget *>(m_x_projet)})
        lw->setAlignment(b, Qt::AlignLeft);
    lw->insertSpacing(lw->indexOf(m_x_consigne2), 10);
    lw->insertSpacing(lw->indexOf(m_x_projet), 10);
    lw->insertSpacing(lw->indexOf(m_x_apercu), 10);
    lw->addStretch(1);
    return w;
}

void PageGuide::maj()
{
    App &a = m_app;
    const Etape courante = a.guide.etape;

    // le bandeau des étapes
    for (Etape e : TOUTES_LES_ETAPES) {
        QPushButton *b = m_etapes[static_cast<size_t>(e)];
        const bool est_courante = e == courante;
        const QString coche = a.etape_faite(e) && !est_courante ? u"✔ "_s : QString();
        b->setText(u"%1%2. %3"_s.arg(coche).arg(numero(e)).arg(nom(e)));
        b->setChecked(est_courante);
        b->setEnabled(a.etape_accessible(e));
    }

    // une mesure à refaire
    m_refait->setVisible(a.refait.has_value());
    m_refait_texte->setText(trad("Cliquez sur l'objet pour refaire la mesure de l'image {}.", a.index));
    m_refait_annuler->setText(trad("Annuler (Échap)"));

    if (m_pages->currentIndex() != static_cast<int>(courante)) {
        m_pages->setCurrentIndex(static_cast<int>(courante));
        // la pile prend la hauteur de l'étape affichée, et non celle de la plus grande
        for (int i = 0; i < m_pages->count(); ++i)
            m_pages->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                              i == m_pages->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        m_pages->adjustSize();
    }
    switch (courante) {
    case Etape::Origine:
        m_o_titre->setText(trad("Étape 1 : l'origine du repère"));
        m_o_consigne->setText(trad("Cliquez sur l'image à l'endroit choisi pour l'origine O du repère."));
        m_o_reussite->setText(u"✔ "_s + trad("Origine placée. Pour la déplacer, cliquez ailleurs ou faites-la glisser."));
        m_o_reussite->setVisible(a.guide.origine_choisie);
        m_o_remarque->setText(trad("Vous pourrez la changer plus tard, sans perdre vos mesures."));
        m_o_rotation->setVisible(a.rotation_permise());
        m_o_couchee->setText(trad("La vidéo est couchée ? Tournez-la :"));
        m_o_gauche->setText(trad("⟲ vers la gauche"));
        m_o_droite->setText(trad("⟳ vers la droite"));
        break;
    case Etape::Axes:
        m_a_titre->setText(trad("Étape 2 : l'orientation des axes"));
        m_a_consigne->setText(trad("Choisissez le sens de l'axe des abscisses (x) et celui de l'axe des ordonnées (y) :"));
        for (BoutonAxes *b : m_a_boutons) {
            b->setChecked(a.guide.axes_choisis && a.pointage.sens_x == b->sens_x() && a.pointage.sens_y == b->sens_y());
            b->setAccessibleName(b->description());
            // la description change avec la langue
            b->updateGeometry();
            b->update();
        }
        m_a_reussite->setText(u"✔ "_s + trad("Axes choisis : le repère est dessiné sur l'image."));
        m_a_reussite->setVisible(a.guide.axes_choisis);
        m_a_remarque->setText(trad("L'origine se déplace encore en la faisant glisser sur l'image. Vous pourrez tout "
                                   "changer plus tard, sans perdre vos mesures."));
        break;
    case Etape::Echelle: {
        m_e_titre->setText(trad("Étape 3 : l'échelle"));
        m_e_consigne_a->setText(trad("a) Sélectionnez l'étalon : un objet de longueur connue, visible sur l'image."));
        const bool etalon_trace = a.etalon_en_attente || a.pointage.echelle.est_definie();
        const bool selection = a.etat == Etat::C;
        m_e_attention->setVisible(selection);
        m_e_attention->setText(a.consigne_etalon());
        m_e_annuler->setVisible(selection);
        m_e_annuler->setText(trad("Annuler"));
        m_e_selection->setVisible(!selection);
        m_e_selection->setText(etalon_trace ? trad("Refaire la sélection de l'étalon") : trad("Sélectionner l'étalon"));
        m_e_consigne_b->setText(trad("b) Indiquez sa longueur réelle, et choisissez l'unité :"));
        const bool actif = etalon_trace && !selection;
        m_e_ligne->setEnabled(actif);
        if (m_saisie->text() != a.guide.saisie_etalon) {
            const QSignalBlocker bloque(m_saisie);
            m_saisie->setText(a.guide.saisie_etalon);
        }
        m_saisie->setPlaceholderText(trad("longueur"));
        // la largeur du champ suit l'agrandissement de l'interface
        m_saisie->setMinimumWidth(m_saisie->fontMetrics().horizontalAdvance(u"2,5×10^-3"_s) + 16);
        m_saisie->setMaximumWidth(m_saisie->fontMetrics().horizontalAdvance(u"2,5×10^-3 mm"_s) + 16);
        {
            const QSignalBlocker bloque(m_e_unite);
            m_e_unite->setCurrentIndex(m_e_unite->findData(static_cast<int>(a.unite_etalon)));
        }
        m_e_unite->setToolTip(trad("L'unité de la longueur : millimètre, centimètre ou mètre"));
        if (a.guide.focus_saisie && actif) {
            m_saisie->setFocus();
            a.guide.focus_saisie = false;
        }
        m_e_valider->setText(trad("Valider"));
        m_e_remarque->setText(trad("Exemples d'écriture : 0,85 · 8,5E-1 · 2,5×10^-3. L'unité peut aussi s'écrire après "
                                   "le nombre : 85 cm."));
        m_e_erreur->setVisible(a.guide.erreur_etalon.has_value());
        m_e_erreur->setText(a.guide.erreur_etalon.value_or(QString()));
        const Echelle &e = a.pointage.echelle;
        if (a.etalon_en_attente) {
            m_e_etat->setText(trad("L'étalon est sélectionné : indiquez sa longueur, puis validez."));
            dessin::colore(m_e_etat, dessin::Ton::Attention);
            m_e_etat->show();
        } else if (e.est_definie()) {
            m_e_etat->setText(u"✔ "_s
                              + trad("Échelle : {} pour {} pixels.", texte_longueur(e.longueur_reelle_etalon, a.unite_etalon),
                                     nombre_local(e.longueur_pixel_etalon(), 4)));
            dessin::colore(m_e_etat, dessin::Ton::Reussite);
            m_e_etat->show();
        } else {
            m_e_etat->hide();
        }
        break;
    }
    case Etape::Pointage:
        m_p_titre->setText(trad("Étape 4 : le pointage"));
        m_p_consigne1->setText(trad("Avec la glissière sous l'image, placez-vous sur la première image utile."));
        m_p_consigne2->setText(trad("Cliquez ensuite sur l'objet : l'image suivante s'affiche toute seule."));
        m_p_remarque->setText(trad("La loupe, en haut à gauche, aide à viser précisément."));
        m_p_nombre->setText(trad("Points mesurés : {}", a.pointage.nb_images_pointees()));
        m_p_derniere->setText(trad("C'est la dernière image de la vidéo."));
        m_p_derniere->setVisible(a.pointage.derniere_image() == a.image_max);
        m_p_annuler->setText(trad("↶ Annuler le dernier point"));
        m_p_annuler->setEnabled(a.peut_defaire());
        m_p_ralenti->setText(trad("Vidéo au ralenti ?"));
        m_p_ralenti_texte->setText(trad("Les dates sont calculées avec la cadence de la vidéo. Pour une vidéo au ralenti, "
                                        "indiquez la cadence de prise de vue (par exemple 240 images par seconde)."));
        m_p_ips->setText(trad("Images par seconde :"));
        if (m_p_saisie_ips->text() != a.ips) {
            const QSignalBlocker bloque(m_p_saisie_ips);
            m_p_saisie_ips->setText(a.ips);
        }
        m_p_cadence->setText(trad("Cadence variable : les dates réelles des images sont utilisées."));
        m_p_cadence->setVisible(a.pointage.cadence_variable());
        break;
    case Etape::Tableau:
        m_t_titre->setText(trad("Étape 5 : le tableau des mesures"));
        m_t_consigne1->setText(
            trad("Vérifiez vos mesures dans le tableau, à droite : la date t et les coordonnées x et y de l'objet."));
        m_t_consigne2->setText(trad("Une mesure est fausse ? Cliquez sur ⌖ dans sa ligne, puis sur l'objet dans l'image."));
        m_t_reprendre->setText(trad("◀ Reprendre le pointage"));
        m_t_reprendre->setEnabled(!a.refait);
        m_t_remarque->setText(
            trad("Vous pouvez aussi revenir aux étapes 1 à 3 (origine, axes, échelle) : les mesures sont recalculées."));
        break;
    case Etape::Export: {
        m_x_titre->setText(trad("Étape 6 : l'export"));
        m_x_consigne1->setText(trad("Copiez vos mesures, puis collez-les (Ctrl+V) dans votre programme Python :"));
        m_x_copier->setText(trad("📋 Copier pour Python"));
        m_x_reussite->setText(u"✔ "_s + trad("Copié ! Collez avec Ctrl+V dans votre programme Python."));
        m_x_reussite->setVisible(a.copie_a_jour());
        m_x_consigne2->setText(trad("Ou enregistrez-les dans un fichier CSV (tableur, Python…) :"));
        m_x_csv->setText(trad("💾 Enregistrer en CSV…"));
        m_x_projet->setText(trad("Enregistrer le projet, pour continuer plus tard…"));
        m_x_apercu->setText(trad("Aperçu du texte copié :"));
        const QString texte = a.texte_python();
        if (m_x_texte->toPlainText() != texte)
            m_x_texte->setPlainText(texte);
        break;
    }
    }

    // la navigation
    const auto p = precedente(courante), s = suivante(courante);
    m_precedent->setVisible(p.has_value());
    m_precedent->setText(trad("◀ Précédent"));
    m_precedent->setEnabled(a.etat != Etat::C);
    m_suivant->setVisible(s.has_value());
    m_suivant->setText(courante == Etape::Pointage ? trad("Terminer le pointage ▶") : trad("Suivant ▶"));
    m_suivant->setEnabled(a.etape_faite(courante) && a.etat != Etat::C && !a.refait);

    m_loupe->maj();
    m_controles_image->maj();
    m_zone->update();
    m_droite->setVisible(courante >= Etape::Pointage);
    if (m_droite->isVisible()) {
        m_titre_tableau->setText(trad("Tableau des mesures"));
        m_tableau->maj();
    }
}

void PageGuide::maj_souris()
{
    m_loupe->maj();
    m_zone->update();
}
