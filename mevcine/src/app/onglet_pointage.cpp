#include "onglet_pointage.h"

#include "app.h"
#include "format.h"
#include "i18n.h"
#include "icones.h"

#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/// côté de la loupe, en points d'écran
constexpr int ZOOM_DIM = 136;
/// grossissement de la loupe par rapport à l'image affichée
constexpr double GROSSISSEMENT = 2.0;
/// déplacement de la souris, en points d'écran, au-delà duquel un appui devient un glissement
constexpr double SEUIL_GLISSEMENT = 6.0;

/// un texte qui passe à la ligne
QLabel *etiquette(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setWordWrap(true);
    return l;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// ZoneVideo
// ------------------------------------------------------------------------------------------------

ZoneVideo::ZoneVideo(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    setMouseTracking(true);
    setMinimumSize(200, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

dessin::Vue ZoneVideo::vue() const
{
    return dessin::Vue::ajuste(QRectF(rect()), m_app.dimensions());
}

void ZoneVideo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QImage &img = m_app.image;
    if (img.isNull()) {
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect(), Qt::AlignCenter, trad("Ouvrez une vidéo"));
        return;
    }
    const dessin::Vue v = vue();
    // l'image réduite à la taille de la vue, refaite seulement quand l'image ou la vue changent
    const qreal ppp = devicePixelRatioF();
    const QSize taille = (v.rect.size() * ppp).toSize();
    if (m_cle_cache != img.cacheKey() || m_cache.size() != taille) {
        m_cache = QPixmap::fromImage(img.scaled(taille, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        m_cache.setDevicePixelRatio(ppp);
        m_cle_cache = img.cacheKey();
    }
    p.drawPixmap(v.rect.topLeft(), m_cache);
    p.setClipRect(v.rect);
    dessine_sur_video(p, v);
}

void ZoneVideo::dessine_sur_video(QPainter &p, const dessin::Vue &vue) const
{
    const App &a = m_app;
    const Pointage &pt = a.pointage;
    QFont police = font();
    police.setPixelSize(12);
    p.setFont(police);
    const bool plusieurs = pt.nb_obj() > 1;
    // les pointages passés
    for (int i = 0; i < pt.len(); ++i) {
        const auto &ligne = pt.ligne(i);
        for (size_t j = 0; j < ligne.size(); ++j) {
            if (!ligne[j])
                continue;
            const int obj = static_cast<int>(j) + 1;
            const QPointF pos = vue.ecran(*ligne[j]);
            const QColor c = dessin::couleur(obj);
            dessin::croix(p, pos, 4.0, c);
            if (plusieurs) {
                p.setPen(c);
                p.drawText(pos + QPointF(-10.0, 10.0), QString::number(obj));
            }
        }
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    // la mesure à refaire est entourée
    if (a.refait && a.index >= 1 && a.index <= pt.len()) {
        p.setPen(QPen(dessin::ORANGE, 2.0));
        p.setBrush(Qt::NoBrush);
        for (const Position &q : pt.ligne(a.index - 1)) {
            if (q)
                p.drawEllipse(vue.ecran(*q), 9.0, 9.0);
        }
    }

    // le repère, une fois choisi en mode Simplifié
    const bool montre_origine = !a.simplifie() || a.guide.origine_choisie || a.glisse_origine;
    const bool montre_axes = !a.simplifie() || a.guide.axes_choisis;
    if (montre_origine) {
        const QPointF o = vue.ecran(pt.origine);
        if (montre_axes)
            dessin::repere(p, o, pt.sens_x, pt.sens_y, dessin::VERT_REPERE, dessin::longueur_axes(vue), true);
        dessin::origine(p, o, pt.sens_x, pt.sens_y, dessin::VERT_REPERE);
    }

    // l'étalon : en cours de sélection, en attente de sa longueur, ou défini
    const auto extremites = [&](Vecteur p1, Vecteur p2, const QColor &c) {
        const QPointF e1 = vue.ecran(p1), e2 = vue.ecran(p2);
        p.setPen(QPen(c, 2.0));
        p.drawLine(e1, e2);
        p.setPen(QPen(c, 1.5));
        p.setBrush(Qt::NoBrush);
        for (const QPointF &q : {e1, e2})
            p.drawEllipse(q, 4.0, 4.0);
    };
    const Echelle &e = pt.echelle;
    if (a.etat == Etat::C) {
        if (a.drag)
            extremites((*a.drag)[0], (*a.drag)[1], dessin::ROUGE);
    } else if (a.etalon_en_attente) {
        extremites(a.etalon_en_attente->first, a.etalon_en_attente->second, dessin::ORANGE);
    } else if (e.est_definie()) {
        extremites(e.p1, e.p2, dessin::VERT);
        const QPointF milieu = vue.ecran((e.p1 + e.p2) * 0.5);
        dessin::texte(p, milieu + QPointF(0, -8), texte_longueur(e.longueur_reelle_etalon, a.unite_etalon), 13.0,
                      Qt::white, QColor(0, 100, 0, 200), true);
    }

    // les zones à suivre automatiquement
    if (a.etat == Etat::AB || a.etat == Etat::B) {
        std::vector<std::pair<std::array<Vecteur, 2>, QColor>> zones;
        for (const auto &r : a.rects)
            zones.emplace_back(r, dessin::ROUGE);
        if (a.drag)
            zones.emplace_back(*a.drag, dessin::VERT);
        p.setBrush(Qt::NoBrush);
        for (const auto &[r, c] : zones) {
            const QPointF e1 = vue.ecran(r[0]), e2 = vue.ecran(r[1]);
            p.setPen(QPen(c, 1.0));
            p.drawRect(QRectF(e1, e2).normalized());
            p.drawLine(e1, e2);
            p.drawLine(QPointF(e1.x(), e2.y()), QPointF(e2.x(), e1.y()));
        }
    }
}

void ZoneVideo::maj_curseur(QPointF pos)
{
    const dessin::Vue v = vue();
    const bool dans_vue = v.rect.contains(pos);
    setCursor(m_app.curseur(dans_vue || m_glisse ? std::optional(v.image(pos)) : std::nullopt, v.echelle));
}

void ZoneVideo::mousePressEvent(QMouseEvent *event)
{
    if (m_appui)
        return;
    m_appui = event->position();
    m_bouton = event->button();
    m_glisse = false;
}

void ZoneVideo::mouseMoveEvent(QMouseEvent *event)
{
    const dessin::Vue v = vue();
    const QPointF pos = event->position();
    const bool dans_vue = v.rect.contains(pos);
    if (m_appui && m_bouton == Qt::LeftButton) {
        if (!m_glisse && QLineF(*m_appui, pos).length() > SEUIL_GLISSEMENT) {
            m_glisse = true;
            m_app.geste({Geste::Type::DebutGlisse, v.image(*m_appui), v.image(pos)}, v);
        } else if (m_glisse) {
            m_app.geste({Geste::Type::Glisse, v.image(*m_appui), v.image(pos)}, v);
        }
    }
    m_app.souris_bouge(dans_vue || m_glisse ? std::optional(v.image(pos)) : std::nullopt, v.echelle);
    maj_curseur(pos);
}

void ZoneVideo::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_appui || event->button() != m_bouton)
        return;
    const dessin::Vue v = vue();
    const Vecteur origine = v.image(*m_appui), position = v.image(event->position());
    const bool glisse = m_glisse;
    const Qt::MouseButton bouton = m_bouton;
    m_appui.reset();
    m_glisse = false;
    m_bouton = Qt::NoButton;
    if (bouton == Qt::LeftButton)
        m_app.geste({glisse ? Geste::Type::FinGlisse : Geste::Type::Clic, origine, position}, v);
    else if (bouton == Qt::RightButton && !glisse)
        m_app.geste({Geste::Type::ClicDroit, origine, position}, v);
    maj_curseur(event->position());
}

void ZoneVideo::leaveEvent(QEvent *)
{
    if (!m_glisse)
        m_app.souris_bouge(std::nullopt, vue().echelle);
}

// ------------------------------------------------------------------------------------------------
// Loupe
// ------------------------------------------------------------------------------------------------

Loupe::Loupe(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    setFixedSize(ZOOM_DIM, ZOOM_DIM);
}

void Loupe::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF r(rect());
    const App &a = m_app;
    p.fillRect(r, palette().color(QPalette::Base));
    if (a.image.isNull() || !a.souris || a.etat == Etat::B) {
        p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        return;
    }
    // grossissement par rapport à l'image d'origine : celui de l'image affichée, doublé
    const double k = GROSSISSEMENT * a.vue_echelle;
    p.save();
    p.setClipRect(r);
    p.fillRect(r, Qt::black);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.translate(r.center());
    p.scale(k, k);
    p.translate(-a.souris->x, -a.souris->y);
    p.drawImage(QPointF(0, 0), a.image);
    p.restore();
    // croix de visée
    const QPointF c = r.center();
    const double rayon = ZOOM_DIM / 20.0;
    p.setPen(QPen(dessin::ROUGE, 1.0));
    p.drawLine(QPointF(c.x(), r.top()), QPointF(c.x(), c.y() - rayon));
    p.drawLine(QPointF(c.x(), c.y() + rayon), QPointF(c.x(), r.bottom()));
    p.drawLine(QPointF(r.left(), c.y()), QPointF(c.x() - rayon, c.y()));
    p.drawLine(QPointF(c.x() + rayon, c.y()), QPointF(r.right(), c.y()));
    p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 64, 255), 1.0));
    p.drawEllipse(c, rayon, rayon);
}

// ------------------------------------------------------------------------------------------------
// TableCoordonnees
// ------------------------------------------------------------------------------------------------

TableCoordonnees::TableCoordonnees(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void TableCoordonnees::montre(std::vector<Ligne> lignes)
{
    const bool forme = lignes.size() != m_lignes.size();
    m_lignes = std::move(lignes);
    if (forme)
        updateGeometry();
    update();
}

void TableCoordonnees::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // agrandissement de l'interface : le tableau change de taille
    if (event->type() == QEvent::FontChange)
        updateGeometry();
}

int TableCoordonnees::largeur_unites() const
{
    QFont entete = dessin::police(1.0);
    entete.setWeight(QFont::DemiBold);
    const QFontMetrics fm(entete);
    return std::max(fm.horizontalAdvance(u"px"_s), fm.horizontalAdvance(u"m"_s)) + 14;
}

int TableCoordonnees::largeur_valeurs() const
{
    return QFontMetrics(dessin::police_chiffres()).horizontalAdvance(u"-0,00000"_s) + 12;
}

int TableCoordonnees::hauteur_ligne() const
{
    return QFontMetrics(dessin::police_chiffres()).height() + 8;
}

QSize TableCoordonnees::sizeHint() const
{
    const int lignes = static_cast<int>(std::max<size_t>(m_lignes.size(), 1)) + 1;
    return {largeur_unites() + 2 * largeur_valeurs() + 1, lignes * hauteur_ligne() + 1};
}

void TableCoordonnees::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = palette();
    const int wu = largeur_unites(), wv = largeur_valeurs(), h = hauteur_ligne();
    const int n = static_cast<int>(m_lignes.size());
    const QRect tout(0, 0, wu + 2 * wv, (n + 1) * h);
    // les en-têtes sur un fond teinté, les valeurs sur le fond des champs de saisie
    const QColor fond_entetes = pal.color(QPalette::Button);
    p.fillRect(tout, pal.color(QPalette::Base));
    p.fillRect(QRect(0, 0, tout.width(), h), fond_entetes);
    p.fillRect(QRect(0, h, wu, n * h), fond_entetes);
    // les en-têtes en demi-gras, comme les titres
    QFont gras = dessin::police(1.0);
    gras.setWeight(QFont::DemiBold);
    p.setFont(gras);
    p.setPen(pal.color(QPalette::ButtonText));
    p.drawText(QRect(wu, 0, wv, h), Qt::AlignCenter, u"x"_s);
    p.drawText(QRect(wu + wv, 0, wv, h), Qt::AlignCenter, u"y"_s);
    for (int i = 0; i < n; ++i) {
        p.setFont(gras);
        p.setPen(pal.color(QPalette::ButtonText));
        p.drawText(QRect(0, (i + 1) * h, wu, h), Qt::AlignCenter, m_lignes[i].unite);
        p.setFont(dessin::police_chiffres());
        p.setPen(pal.color(QPalette::Text));
        const auto valeur = [](const QString &v) { return v.isEmpty() ? u"–"_s : v; };
        p.drawText(QRect(wu, (i + 1) * h, wv - 6, h), Qt::AlignRight | Qt::AlignVCenter, valeur(m_lignes[i].x));
        p.drawText(QRect(wu + wv, (i + 1) * h, wv - 6, h), Qt::AlignRight | Qt::AlignVCenter, valeur(m_lignes[i].y));
    }
    // le quadrillage : un trait plus marqué sépare les en-têtes des valeurs
    const QColor trait = pal.color(QPalette::Mid);
    p.setPen(QPen(trait, 1.0));
    for (int i = 2; i <= n; ++i)
        p.drawLine(wu, i * h, tout.right(), i * h);
    p.drawLine(wu + wv, 0, wu + wv, tout.bottom());
    p.setPen(QPen(trait.darker(dessin::sombre(pal) ? 70 : 125), 1.0));
    p.drawLine(0, h, tout.right(), h);
    p.drawLine(wu, 0, wu, tout.bottom());
    p.setPen(QPen(trait, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(tout);
}

// ------------------------------------------------------------------------------------------------
// PanneauLoupe
// ------------------------------------------------------------------------------------------------

PanneauLoupe::PanneauLoupe(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_titre = dessin::titre_partie(this);
    l->addWidget(m_titre);
    auto *ligne = new QHBoxLayout();
    ligne->setSpacing(10);
    m_loupe = new Loupe(app, this);
    m_coordonnees = new TableCoordonnees(this);
    ligne->addWidget(m_loupe, 0, Qt::AlignTop);
    ligne->addWidget(m_coordonnees, 0, Qt::AlignTop);
    ligne->addStretch(1);
    l->addLayout(ligne);
}

void PanneauLoupe::maj()
{
    const App &a = m_app;
    m_titre->setText(a.label_zoom());
    m_loupe->update();
    const auto c = a.souris ? a.coords(*a.souris) : std::nullopt;
    const bool metre = a.pointage.echelle.est_definie();
    const bool pixels = !a.simplifie() || !metre;
    std::vector<TableCoordonnees::Ligne> lignes;
    if (pixels)
        lignes.push_back({u"px"_s, c ? c->xpx : QString(), c ? c->ypx : QString()});
    if (metre)
        lignes.push_back({u"m"_s, c && c->metre ? c->metre->first : QString(), c && c->metre ? c->metre->second : QString()});
    m_coordonnees->montre(std::move(lignes));
    m_coordonnees->setToolTip(trad("Coordonnées du point visé dans le repère choisi : en pixel, et en mètre une fois "
                                   "l'échelle définie"));
}

// ------------------------------------------------------------------------------------------------
// ControlesImage
// ------------------------------------------------------------------------------------------------

ControlesImage::ControlesImage(App &app, QWidget *parent, bool dans_plein_ecran)
    : QWidget(parent)
    , m_app(app)
    , m_dans_plein_ecran(dans_plein_ecran)
{
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(6, 2, 6, 4);
    const auto outil = [this](const QString &texte) {
        auto *b = new QToolButton(this);
        b->setText(texte);
        return b;
    };
    m_gauche = outil(u"⟲"_s);
    m_droite = outil(u"⟳"_s);
    connect(m_gauche, &QToolButton::clicked, this, [this] {
        m_app.tourne_image(-90);
        m_app.rafraichit();
    });
    connect(m_droite, &QToolButton::clicked, this, [this] {
        m_app.tourne_image(90);
        m_app.rafraichit();
    });
    m_image = new QLabel(this);
    m_numero = new QSpinBox(this);
    m_sur = new QLabel(this);
    m_glissiere = new QSlider(Qt::Horizontal, this);
    const auto change = [this](int i) {
        if (m_app.controle_image_actif() && m_app.video) {
            m_app.change_image(i);
            m_app.rafraichit();
        }
    };
    connect(m_numero, &QSpinBox::valueChanged, this, change);
    connect(m_glissiere, &QSlider::valueChanged, this, change);
    m_defaire = outil(u"↶"_s);
    m_refaire = outil(u"↷"_s);
    connect(m_defaire, &QToolButton::clicked, this, [this] {
        m_app.efface_point_precedent();
        m_app.rafraichit();
    });
    connect(m_refaire, &QToolButton::clicked, this, [this] {
        m_app.refait_point_suivant();
        m_app.rafraichit();
    });
    m_plein_ecran = outil(QString());
    m_plein_ecran->setObjectName(u"plein_ecran"_s);
    m_plein_ecran->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_plein_ecran, &QToolButton::clicked, this, [this] {
        if (m_dans_plein_ecran)
            m_app.quitte_plein_ecran();
        else
            m_app.ouvre_plein_ecran();
        m_app.rafraichit();
    });
    for (QWidget *w : {static_cast<QWidget *>(m_gauche), static_cast<QWidget *>(m_droite), static_cast<QWidget *>(m_image),
                       static_cast<QWidget *>(m_numero), static_cast<QWidget *>(m_sur)})
        l->addWidget(w);
    l->addWidget(m_glissiere, 1);
    l->addWidget(m_defaire);
    l->addWidget(m_refaire);
    l->addSpacing(8);
    l->addWidget(m_plein_ecran);
}

void ControlesImage::maj()
{
    const App &a = m_app;
    const bool actif = a.controle_image_actif() && a.video;
    const bool rotation = a.rotation_permise() && !a.choix_origine;
    for (QToolButton *b : {m_gauche, m_droite}) {
        b->setVisible(!a.simplifie());
        b->setEnabled(rotation);
    }
    m_gauche->setToolTip(trad("Tourner la vidéo d'un quart de tour vers la gauche"));
    m_droite->setToolTip(trad("Tourner la vidéo d'un quart de tour vers la droite"));
    m_image->setText(trad("Image n°"));
    m_image->setToolTip(trad("Choix de l'image dans la vidéo"));
    m_image->setEnabled(actif);
    const int max = std::max(a.image_max, 1);
    {
        const QSignalBlocker b1(m_numero), b2(m_glissiere);
        m_numero->setRange(1, max);
        m_numero->setValue(a.index);
        m_glissiere->setRange(1, max);
        m_glissiere->setValue(a.index);
    }
    m_numero->setEnabled(actif);
    m_glissiere->setEnabled(actif);
    m_sur->setText(trad("sur {}", max));
    m_sur->setEnabled(actif);
    m_defaire->setEnabled(a.peut_defaire());
    m_defaire->setToolTip(trad("Annuler le dernier point (Ctrl+Z)"));
    m_refaire->setEnabled(a.peut_refaire());
    m_refaire->setToolTip(trad("Rétablir le point (Ctrl+Y)"));
    if (m_dans_plein_ecran) {
        m_plein_ecran->setText(trad("✕ Quitter le plein écran"));
        m_plein_ecran->setToolTip(trad("Revenir à la fenêtre, sans arrêter le pointage (Échap)"));
    } else {
        m_plein_ecran->setText(trad("Plein écran"));
        m_plein_ecran->setToolTip(
            trad("Pointer en plein écran : l'image occupe tout l'écran, la loupe reste affichée (F11). Pendant le pointage."));
        m_plein_ecran->setEnabled(a.plein_ecran_permis());
    }
}

// ------------------------------------------------------------------------------------------------
// BarreRepere
// ------------------------------------------------------------------------------------------------

BarreRepere::BarreRepere(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(8, 6, 8, 4);
    l->setSpacing(4);
    auto *ligne = new QHBoxLayout();
    ligne->setSpacing(8);
    const auto etape = [this, ligne] {
        auto *t = new QLabel(this);
        dessin::demi_gras(t);
        ligne->addWidget(t);
        return t;
    };
    const auto separation = [this, ligne] {
        ligne->addSpacing(6);
        auto *s = new QLabel(u"›"_s, this);
        dessin::discret(s);
        ligne->addWidget(s);
        ligne->addSpacing(6);
    };

    // 1. l'origine
    m_t_origine = etape();
    m_origine = new QPushButton(this);
    m_origine->setCheckable(true);
    connect(m_origine, &QPushButton::clicked, this, [this] {
        m_app.nouvelle_origine();
        m_app.rafraichit();
    });
    ligne->addWidget(m_origine);
    separation();

    // 2. le sens des axes : quatre petits repères
    m_t_axes = etape();
    for (size_t k = 0; k < ORIENTATIONS.size(); ++k) {
        const auto [sx, sy] = ORIENTATIONS[k];
        auto *b = new QToolButton(this);
        b->setCheckable(true);
        b->setIcon(icones::repere(sx, sy));
        connect(b, &QToolButton::clicked, this, [this, sx, sy] {
            if (!m_app.repere_modifiable())
                return;
            m_app.pointage.sens_x = sx;
            m_app.pointage.sens_y = sy;
            m_app.rafraichit();
        });
        m_axes[k] = b;
        ligne->addWidget(b);
    }
    separation();

    // 3. l'échelle
    m_t_echelle = etape();
    m_echelle = new QPushButton(this);
    connect(m_echelle, &QPushButton::clicked, this, [this] {
        m_app.demande_echelle();
        m_app.rafraichit();
    });
    ligne->addWidget(m_echelle);
    m_echelle_etat = new QLabel(this);
    ligne->addWidget(m_echelle_etat);
    ligne->addStretch(1);
    l->addLayout(ligne);

    // la consigne pendant le choix de l'origine ou de l'étalon
    m_ligne_consigne = new QWidget(this);
    auto *lc = new QHBoxLayout(m_ligne_consigne);
    lc->setContentsMargins(0, 0, 0, 0);
    m_consigne = new QLabel(m_ligne_consigne);
    m_consigne->setWordWrap(true);
    dessin::colore(m_consigne, dessin::Ton::Attention);
    m_annuler = new QPushButton(m_ligne_consigne);
    connect(m_annuler, &QPushButton::clicked, this, [this] {
        if (m_app.etat == Etat::C)
            m_app.annule_echelle();
        else if (m_app.choix_origine)
            m_app.nouvelle_origine();
        m_app.rafraichit();
    });
    lc->addWidget(m_consigne, 1);
    lc->addWidget(m_annuler);
    l->addWidget(m_ligne_consigne);
}

void BarreRepere::maj()
{
    App &a = m_app;
    const bool modifiable = a.repere_modifiable();
    m_t_origine->setText(trad("1. Origine"));
    m_origine->setText(trad("Placer l'origine"));
    m_origine->setToolTip(trad("Cliquez ensuite sur l'image ; l'origine se déplace aussi en la faisant glisser. "
                               "Les mesures sont recalculées."));
    m_origine->setChecked(a.choix_origine);
    m_origine->setEnabled(modifiable);

    m_t_axes->setText(trad("2. Axes"));
    // Les petits repères, à la taille du texte (qui suit l'agrandissement de l'interface) ; l'orientation
    // choisie est entourée. Les couleurs suivent le thème clair ou sombre.
    const int cote = fontMetrics().height() * 2;
    const QPalette pal = QApplication::palette();
    const QString style = u"QToolButton { border: 1px solid %1; border-radius: 4px; background: %2; padding: 2px; }"
                          "QToolButton:hover { border-color: %3; }"
                          "QToolButton:checked { border: 2px solid %3; background: %4; padding: 1px; }"_s.arg(
                              pal.color(QPalette::Mid).name(), pal.color(QPalette::Button).name(),
                              pal.color(QPalette::Highlight).name(), pal.color(QPalette::Base).name());
    for (QToolButton *b : m_axes) {
        b->setIconSize(QSize(cote, cote));
        if (b->styleSheet() != style)
            b->setStyleSheet(style);
        const size_t k = static_cast<size_t>(std::find(m_axes.begin(), m_axes.end(), b) - m_axes.begin());
        const auto [sx, sy] = ORIENTATIONS[k];
        const auto [tx, ty] = sens_en_mots(sx, sy);
        b->setChecked(a.pointage.sens_x == sx && a.pointage.sens_y == sy);
        b->setAccessibleName(tx + u", "_s + ty);
        b->setToolTip(tx + u", "_s + ty + u"\n"_s + trad("Les mesures sont recalculées à chaque changement"));
        b->setEnabled(modifiable && !a.choix_origine);
    }

    m_t_echelle->setText(trad("3. Échelle"));
    const Echelle &e = a.pointage.echelle;
    m_echelle->setText(e.est_definie() ? trad("Refaire l'échelle") : trad("Définir l'échelle"));
    m_echelle->setToolTip(trad("Sélectionnez un étalon sur l'image, puis indiquez sa longueur"));
    m_echelle->setEnabled(modifiable && !a.choix_origine);
    if (e.est_definie()) {
        m_echelle_etat->setText(trad("{} pour {} px", texte_longueur(e.longueur_reelle_etalon, a.unite_etalon),
                                     nombre_local(e.longueur_pixel_etalon(), 4)));
        m_echelle_etat->setToolTip(trad("{} pixels par mètre", a.texte_echelle()));
        dessin::colore(m_echelle_etat, dessin::Ton::Reussite);
    } else {
        m_echelle_etat->setText(trad("non définie : les mesures sont en pixel"));
        m_echelle_etat->setToolTip(QString());
        dessin::colore(m_echelle_etat, dessin::Ton::Discret);
    }

    const bool consigne = a.etat == Etat::C || a.choix_origine;
    m_ligne_consigne->setVisible(consigne);
    m_consigne->setText(a.etat == Etat::C ? a.consigne_etalon()
                                          : trad("Cliquez sur l'image pour placer l'origine (Échap : annuler)"));
    m_annuler->setText(trad("Annuler"));
}

// ------------------------------------------------------------------------------------------------
// ControlesPointage
// ------------------------------------------------------------------------------------------------

ControlesPointage::ControlesPointage(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(8, 6, 8, 6);
    const auto bouton = [this](auto action) {
        auto *b = new QPushButton(this);
        connect(b, &QPushButton::clicked, this, [this, action] {
            action();
            m_app.rafraichit();
        });
        return b;
    };
    m_loupe = new PanneauLoupe(app, this);
    l->addWidget(m_loupe);
    l->addSpacing(4);
    l->addWidget(dessin::trait(this));

    // le pointage : nombre d'objets, suivi automatique, démarrage
    m_t_pointage = dessin::titre_partie(this);
    l->addWidget(m_t_pointage);
    auto *ligne_objets = new QHBoxLayout();
    m_objets = new QLabel(this);
    m_nb_obj = new QSpinBox(this);
    m_nb_obj->setRange(1, 10);
    connect(m_nb_obj, &QSpinBox::valueChanged, this, [this](int n) {
        m_app.nb_obj = n;
        m_app.redimensionne_data();
        m_app.rafraichit();
    });
    ligne_objets->addWidget(m_objets);
    ligne_objets->addWidget(m_nb_obj);
    ligne_objets->addStretch(1);
    l->addLayout(ligne_objets);
    m_auto = new QCheckBox(this);
    connect(m_auto, &QCheckBox::toggled, this, [this](bool v) {
        m_app.suivi_auto = v;
        m_app.rafraichit();
    });
    l->addWidget(m_auto);
    m_demarrer = bouton([this] { m_app.debut_capture(); });
    m_arreter = bouton([this] { m_app.arrete_pointage(); });
    for (QPushButton *b : {m_demarrer, m_arreter})
        dessin::demi_gras(b);
    m_stop = bouton([this] { m_app.stop_calculs(); });
    m_stop->setStyleSheet(u"QPushButton { background-color: #d42020; color: white; border: 1px solid #a00000; "
                          "border-radius: 3px; padding: 4px; }"_s);
    m_pile_pointage = new QStackedWidget(this);
    for (QPushButton *b : {m_demarrer, m_arreter, m_stop})
        m_pile_pointage->addWidget(b);
    m_pile_pointage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    l->addWidget(m_pile_pointage);
    l->addSpacing(4);
    l->addWidget(dessin::trait(this));

    // la vidéo : cadence, et ce qu'il faut en savoir
    m_t_video = dessin::titre_partie(this);
    l->addWidget(m_t_video);
    m_nom_video = etiquette(this);
    m_nom_video->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info_video = etiquette(this);
    dessin::discret(m_info_video);
    l->addWidget(m_nom_video);
    l->addWidget(m_info_video);
    auto *ligne_ips = new QHBoxLayout();
    m_ips = new QLabel(this);
    m_saisie_ips = new QLineEdit(this);
    m_saisie_ips->setMaximumWidth(QFontMetrics(m_saisie_ips->font()).horizontalAdvance(u"0000,000"_s) + 16);
    connect(m_saisie_ips, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_app.ips = t;
        m_app.verifie_ips();
        m_app.rafraichit();
    });
    ligne_ips->addWidget(m_ips);
    ligne_ips->addWidget(m_saisie_ips);
    ligne_ips->addStretch(1);
    l->addLayout(ligne_ips);
    m_cadence = etiquette(this);
    dessin::colore(m_cadence, dessin::Ton::Attention);
    l->addWidget(m_cadence);
    l->addSpacing(4);
    l->addWidget(dessin::trait(this));
    m_recommencer = bouton([this] { m_app.confirme_si_mesures({Apres::Type::ToutRecommencer}); });
    l->addWidget(m_recommencer);
    l->addStretch(1);
}

void ControlesPointage::maj()
{
    App &a = m_app;
    const Etat etat = a.etat;
    const bool en_a = etat == Etat::A && !a.choix_origine;
    const bool modifiable = a.repere_modifiable() && !a.choix_origine;

    m_loupe->maj();

    m_t_pointage->setText(trad("Objets suivis"));
    m_objets->setText(trad("Objets à suivre :"));
    m_objets->setToolTip(trad("Nombre d'objets à suivre"));
    m_objets->setEnabled(en_a);
    {
        const QSignalBlocker b1(m_nb_obj), b2(m_auto);
        m_nb_obj->setValue(a.nb_obj);
        m_auto->setChecked(a.suivi_auto);
    }
    m_nb_obj->setEnabled(en_a);
    m_auto->setText(trad("Suivi automatique"));
    m_auto->setToolTip(trad("Le programme retrouve les objets sur chaque image : tracez d'abord un cadre autour de chacun"));
    m_auto->setEnabled(en_a);
    // pendant le pointage, « Arrêter » ; une fois arrêté, « Reprendre », avec les mesures déjà faites
    const bool arrete = etat == Etat::D && a.pointage_arrete;
    if (arrete) {
        m_demarrer->setText(trad("▶ Reprendre le pointage"));
        m_demarrer->setToolTip(trad("Continuer le pointage, avec les mesures déjà faites"));
    } else {
        m_demarrer->setText(trad("Démarrer le pointage"));
        m_demarrer->setToolTip(trad("Commencer le suivi des objets (manuel ou automatique)"));
    }
    m_demarrer->setEnabled(en_a || (arrete && !a.refait && !a.choix_origine));
    m_arreter->setText(trad("⏹ Arrêter le pointage"));
    m_arreter->setToolTip(trad("Finir le pointage : un clic sur l'image ne pointe plus, et les coordonnées s'affichent"));
    m_arreter->setEnabled(!a.refait && !a.choix_origine);
    m_pile_pointage->setCurrentWidget(etat == Etat::B                            ? m_stop
                                      : etat == Etat::D && !a.pointage_arrete ? m_arreter
                                                                              : m_demarrer);
    m_stop->setText(a.pile_detections.empty() ? u"STOP"_s : u"STOP (%1)"_s.arg(a.pile_detections.front()));
    m_stop->setToolTip(trad("Arrêter le suivi automatique"));

    m_t_video->setText(trad("Vidéo"));
    if (a.video) {
        const QSize d = a.video->dimensions();
        const double duree = a.image_max / std::max(a.fps, 1e-9);
        m_nom_video->setText(QFileInfo(a.filename).fileName());
        m_info_video->setText(a.image_max > 1 ? trad("{} × {} px · {} images · {} s", d.width(), d.height(), a.image_max,
                                                     nombre_local(duree, 3))
                                              : trad("{} × {} px · {} image · {} s", d.width(), d.height(), a.image_max,
                                                     nombre_local(duree, 3)));
    }
    const QString aide_ips = trad("Le nombre d'images par seconde est détecté automatiquement. Pour une vidéo au ralenti, "
                                  "indiquez la cadence de prise de vue (par exemple 240).");
    m_ips->setText(trad("Images par seconde :"));
    m_ips->setToolTip(aide_ips);
    m_ips->setEnabled(modifiable);
    if (m_saisie_ips->text() != a.ips) {
        const QSignalBlocker b(m_saisie_ips);
        m_saisie_ips->setText(a.ips);
    }
    m_saisie_ips->setToolTip(aide_ips);
    m_saisie_ips->setEnabled(modifiable);
    m_cadence->setVisible(a.pointage.cadence_variable());
    m_cadence->setText(trad("Cadence variable"));
    m_cadence->setToolTip(trad("Les images de cette vidéo ne sont pas régulièrement espacées dans le temps "
                               "(téléphone, images perdues) : les dates réelles des images sont utilisées. "
                               "Le nombre d'images par seconde est la cadence habituelle de la caméra."));

    m_recommencer->setText(trad("Tout recommencer"));
    m_recommencer->setToolTip(trad("Efface les mesures, l'échelle et le repère"));
    m_recommencer->setEnabled(etat == Etat::A || etat == Etat::D);
}

void ControlesPointage::maj_souris()
{
    m_loupe->maj();
}

// ------------------------------------------------------------------------------------------------
// OngletPointage
// ------------------------------------------------------------------------------------------------

OngletPointage::OngletPointage(App &app, QWidget *parent)
    : QWidget(parent)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    m_barre = new BarreRepere(app, this);
    l->addWidget(m_barre);
    l->addWidget(dessin::trait(this));
    // le panneau latéral, et l'image de la vidéo
    m_controles = new ControlesPointage(app, this);
    auto *droite = new QWidget(this);
    auto *ld = new QVBoxLayout(droite);
    ld->setContentsMargins(0, 0, 0, 0);
    m_zone = new ZoneVideo(app, droite);
    m_controles_image = new ControlesImage(app, droite);
    ld->addWidget(m_zone, 1);
    ld->addWidget(m_controles_image);
    l->addWidget(new dessin::SeparationLaterale(dessin::panneau_lateral(m_controles, this), droite, this), 1);
}

void OngletPointage::maj()
{
    m_barre->maj();
    m_controles->maj();
    m_controles_image->maj();
    m_zone->update();
}

void OngletPointage::maj_souris()
{
    m_controles->maj_souris();
    m_zone->update();
}
