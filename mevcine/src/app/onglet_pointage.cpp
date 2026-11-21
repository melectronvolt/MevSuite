#include "onglet_pointage.h"

#include "app.h"
#include "format.h"
#include "i18n.h"
#include "icones.h"
#include "tableau.h"

#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
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

#include <algorithm>
#include <utility>

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
    auto *l = new QVBoxLayout(this);
    l->addStretch();
    m_erreur = new QLabel(this);
    m_reessayer = new QPushButton(this);
    m_reessayer->setIcon(icones::commande(icones::Commande::Recommencer));
    l->addWidget(m_erreur, 0, Qt::AlignHCenter);
    l->addWidget(m_reessayer, 0, Qt::AlignHCenter);
    l->addStretch();
    m_erreur->hide();
    m_reessayer->hide();
    connect(m_reessayer, &QPushButton::clicked, this, [this] {
        m_app.video->oublie_cache_et_flux();
        m_app.image_cle.reset();
        m_app.statut.clear();
        m_app.rafraichit();
    });
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(200, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ZoneVideo::actualise()
{
    const bool erreur = m_app.video && m_app.image.isNull() && m_image_survolee.isNull();
    m_erreur->setVisible(erreur);
    m_reessayer->setVisible(erreur);
    if (erreur) {
        m_erreur->setText(trad("Image {} illisible", m_app.index));
        m_reessayer->setText(trad("Réessayer"));
    }
    update();
}

dessin::Vue ZoneVideo::vue() const
{
    return dessin::Vue::ajuste(QRectF(rect()), m_app.dimensions());
}

bool ZoneVideo::survole_mesure(int image, int objet)
{
    if (!m_app.video || m_app.etat == Etat::SuiviAutomatique || m_app.refait ||
        (objet && !m_app.pointage.point_de_l_objet(objet, image) && !Pointage::absence_signalee(m_app.pointage.saisie(objet, image))))
        return false;
    if (image != m_ligne_survolee) {
        const auto source = m_app.video->image(image + 1);
        if (!source)
            return false;
        m_image_survolee = *source;
    }
    m_ligne_survolee = image;
    m_objet_survole = objet;
    setAccessibleDescription(objet ? trad("Aperçu · image {} · objet {}", image + 1, objet) : trad("Aperçu · image {}", image + 1));
    actualise();
    return true;
}

void ZoneVideo::quitte_survol()
{
    if (m_image_survolee.isNull())
        return;
    m_image_survolee = {};
    m_ligne_survolee = -1;
    setAccessibleDescription({});
    actualise();
}

void ZoneVideo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QImage &img = m_image_survolee.isNull() ? m_app.image : m_image_survolee;
    if (img.isNull()) {
        p.setPen(palette().color(QPalette::WindowText));
        if (!m_app.video)
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
    if (!m_image_survolee.isNull())
        dessin::texte(p, v.rect.topLeft() + QPointF(8, 22), accessibleDescription(), fontMetrics().height(),
                      Qt::white, QColor(35, 35, 35), false);
}

void ZoneVideo::dessine_sur_video(QPainter &p, const dessin::Vue &vue) const
{
    const App &a = m_app;
    const Pointage &pt = a.pointage;
    QFont police = font();
    police.setPixelSize(12);
    p.setFont(police);
    const bool plusieurs = pt.nombre_objets() > 1;
    const auto dessine_point = [&](int objet, Vecteur point) {
        const QPointF position = vue.ecran(point);
        const QColor couleur = dessin::couleur(objet);
        dessin::croix(p, position, 4.0, couleur);
        if (plusieurs) {
            p.setPen(couleur);
            p.drawText(position + QPointF(-10.0, 10.0), QString::number(objet));
        }
    };
    const bool reprend_ligne = a.refait && !a.objet_repris;
    for (const auto &[index, positions] : pt.positions_par_image()) {
        for (size_t j = 0; j < positions.size(); ++j) {
            if (reprend_ligne && index == a.index - 1 && j < a.correction.size())
                continue;
            if (positions[j])
                dessine_point(static_cast<int>(j) + 1, *positions[j]);
        }
    }
    // La correction reste visible avant sa validation, même sur une ligne où tous les objets étaient perdus.
    if (reprend_ligne)
        for (size_t j = 0; j < a.correction.size(); ++j)
            dessine_point(static_cast<int>(j) + 1, a.correction[j]);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_ligne_survolee >= 0 && m_ligne_survolee < pt.nombre_images()) {
        p.setBrush(Qt::NoBrush);
        for (int obj = 1; obj <= pt.nombre_objets(); ++obj) {
            const Position q = pt.point_de_l_objet(obj, m_ligne_survolee);
            if (q && (m_objet_survole == 0 || m_objet_survole == obj)) {
                const QPointF centre = vue.ecran(*q);
                p.setPen(QPen(Qt::black, 4));
                p.drawEllipse(centre, 8, 8);
                p.setPen(QPen(Qt::yellow, 2));
                p.drawEllipse(centre, 8, 8);
            }
        }
    }
    // la mesure à refaire est entourée
    if (a.refait && a.index >= 1 && a.index <= pt.nombre_images()) {
        p.setPen(QPen(dessin::ORANGE, 2.0));
        p.setBrush(Qt::NoBrush);
        for (int obj = 1; obj <= pt.nombre_objets(); ++obj) {
            const auto q = pt.point_de_l_objet(obj, a.index - 1);
            if (q && obj == a.objet_courant)
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
    if (a.etat == Etat::Etalonnage) {
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

    if (const auto cible = a.visee_proposee(); cible && !montre_apercu()) {
        const QPointF centre = vue.ecran(*cible);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 3));
        p.drawEllipse(centre, 9, 9);
        p.setPen(QPen(QColor(106, 236, 250), 2, Qt::DashLine));
        p.drawEllipse(centre, 9, 9);
        p.setPen(QPen(Qt::black, 3));
        p.drawPoint(centre);
        p.setPen(QPen(Qt::white, 1));
        p.drawPoint(centre);
    }
    // les zones à suivre automatiquement
    if (a.etat == Etat::SelectionMotifs || a.etat == Etat::SuiviAutomatique) {
        p.setBrush(Qt::NoBrush);
        const auto dessine_cadre = [&](const std::array<Vecteur, 2> &coins, const QColor &couleur) {
            const QRectF cadre = QRectF(vue.ecran(coins[0]), vue.ecran(coins[1])).normalized();
            p.setPen(QPen(couleur, 1.0));
            p.drawRect(cadre);
            p.drawLine(cadre.topLeft(), cadre.bottomRight());
            p.drawLine(cadre.bottomLeft(), cadre.topRight());
        };
        for (const auto &cadre : a.cadres_des_motifs)
            dessine_cadre(cadre, dessin::ROUGE);
        if (a.drag)
            dessine_cadre(*a.drag, dessin::VERT);
    }
}

void ZoneVideo::maj_curseur(QPointF pos)
{
    const dessin::Vue v = vue();
    const bool dans_vue = v.rect.contains(pos);
    setCursor(m_app.curseur(dans_vue || m_glisse ? std::optional(v.image(pos)) : std::nullopt, v.echelle));
}

void ZoneVideo::keyPressEvent(QKeyEvent *event)
{
    if (const auto cible = m_app.visee_proposee(); cible && m_app.zone_pointage() == this && !montre_apercu() && !m_appui) {
        if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
            if (!event->isAutoRepeat()) {
                m_app.clic_pointage(*cible, Pointage::Saisie::Assistee);
                m_app.rafraichit();
            }
            event->accept();
            return;
        }
        if (event->modifiers() == Qt::ShiftModifier) {
            Vecteur pas;
            switch (event->key()) {
            case Qt::Key_Left:
                pas.x = -1;
                break;
            case Qt::Key_Right:
                pas.x = 1;
                break;
            case Qt::Key_Up:
                pas.y = -1;
                break;
            case Qt::Key_Down:
                pas.y = 1;
                break;
            default:
                QWidget::keyPressEvent(event);
                return;
            }
            m_app.ajuste_visee(pas);
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void ZoneVideo::mousePressEvent(QMouseEvent *event)
{
    if (!m_image_survolee.isNull()) {
        quitte_survol();
        return;
    }
    if (m_appui || !m_app.image_affichee())
        return;
    if (m_app.guide.visee_assistee && m_app.simplifie())
        setFocus(Qt::MouseFocusReason);
    m_image_appui = m_app.index;
    m_appui = event->position();
    m_bouton = event->button();
    m_glisse = false;
}

void ZoneVideo::mouseMoveEvent(QMouseEvent *event)
{
    const dessin::Vue v = vue();
    const QPointF pos = event->position();
    const bool dans_vue = v.rect.contains(pos);
    if (m_appui && (m_image_appui != m_app.index || !m_app.image_affichee())) {
        m_appui.reset();
        m_glisse = false;
    }
    if (m_appui && m_bouton == Qt::LeftButton) {
        if (!m_glisse && QLineF(*m_appui, pos).length() > SEUIL_GLISSEMENT) {
            m_glisse = true;
            m_app.geste({Geste::Type::DebutGlisse, v.image(*m_appui), v.image(pos)}, v);
        } else if (m_glisse) {
            m_app.geste({Geste::Type::Glisse, v.image(*m_appui), v.image(pos)}, v);
        }
    }
    m_app.souris_bouge(dans_vue || m_glisse ? std::optional(v.image(pos)) : std::nullopt);
    maj_curseur(pos);
}

void ZoneVideo::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_appui || event->button() != m_bouton)
        return;
    if (m_image_appui != m_app.index || !m_app.image_affichee()) {
        m_appui.reset();
        m_glisse = false;
        return;
    }
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
        m_app.souris_bouge(std::nullopt);
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

std::optional<Vecteur> Loupe::centre() const
{
    if (const auto cible = m_app.visee_proposee())
        return cible;
    return m_app.souris;
}

double Loupe::grossissement() const
{
    const double zoom = GROSSISSEMENT * m_app.zone_pointage()->vue().echelle;
    return m_app.visee_proposee() ? std::max(3.0, zoom) : zoom;
}

std::optional<Loupe::VueAuClic> Loupe::vue_cliquable() const
{
    const auto cible = m_app.visee_proposee();
    if (!cible || m_app.zone_pointage()->montre_apercu() || m_app.zone_pointage()->window() != window())
        return {};
    return VueAuClic{m_app.image.cacheKey(), m_app.index, m_app.objet_courant, *cible, grossissement()};
}

void Loupe::actualise()
{
    const bool cliquable = vue_cliquable().has_value();
    const auto forme = cliquable ? Qt::CrossCursor : Qt::ArrowCursor;
    if (cursor().shape() != forme)
        setCursor(forme);
    const QString nom = trad("Loupe");
    const QString description = cliquable ? m_app.consigne_visee() : QString();
    if (accessibleName() != nom)
        setAccessibleName(nom);
    if (accessibleDescription() != description)
        setAccessibleDescription(description);
    update();
}

void Loupe::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_appui = vue_cliquable();
        if (m_app.visee_proposee() && m_app.zone_pointage()->window() == window())
            m_app.zone_pointage()->setFocus(Qt::MouseFocusReason);
    }
}

void Loupe::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const auto appui = std::exchange(m_appui, std::nullopt);
    const QRectF cadre(rect());
    if (!appui || appui != vue_cliquable() || !cadre.contains(event->position()))
        return;
    const QPointF delta = (event->position() - cadre.center()) / appui->zoom;
    // Tout le pixel central valide la cible exacte, même si le pointeur reste légèrement décentré.
    Vecteur p = appui->centre;
    if (std::abs(delta.x()) > 0.5 || std::abs(delta.y()) > 0.5)
        p = p + Vecteur(delta.x(), delta.y());
    if (p.x >= 0 && p.y >= 0 && p.x < m_app.image.width() && p.y < m_app.image.height()) {
        m_app.clic_pointage(p, Pointage::Saisie::Assistee);
        m_app.rafraichit();
    }
}

void Loupe::hideEvent(QHideEvent *event)
{
    m_appui.reset();
    QWidget::hideEvent(event);
}

void Loupe::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF r(rect());
    const App &a = m_app;
    p.fillRect(r, palette().color(QPalette::Base));
    const auto point = centre();
    if (a.image.isNull() || !point || a.etat == Etat::SuiviAutomatique) {
        p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        return;
    }
    const double k = grossissement();
    p.save();
    p.setClipRect(r);
    p.fillRect(r, Qt::black);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.translate(r.center());
    p.scale(k, k);
    p.translate(-point->x, -point->y);
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
    if (a.visee_proposee()) {
        p.setPen(QPen(Qt::black, 3));
        p.drawEllipse(c, rayon, rayon);
        p.setPen(QPen(QColor(106, 236, 250), 2, Qt::DashLine));
    } else {
        p.setPen(QPen(QColor(255, 64, 255), 1.0));
    }
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

void PanneauLoupe::actualise()
{
    const App &a = m_app;
    m_titre->setText(a.etiquette_loupe());
    m_loupe->actualise();
    const auto point = m_loupe->centre();
    const auto c = point ? a.releve_coordonnees(*point) : std::nullopt;
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
    const auto outil = [this](icones::Commande commande) {
        auto *b = new QToolButton(this);
        b->setIcon(icones::commande(commande));
        return b;
    };
    m_gauche = outil(icones::Commande::TournerGauche);
    m_droite = outil(icones::Commande::TournerDroite);
    connect(m_gauche, &QToolButton::clicked, this, [this] {
        m_app.pivote_video(-90);
        m_app.rafraichit();
    });
    connect(m_droite, &QToolButton::clicked, this, [this] {
        m_app.pivote_video(90);
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
    m_defaire = outil(icones::Commande::Annuler);
    m_refaire = outil(icones::Commande::Retablir);
    connect(m_defaire, &QToolButton::clicked, this, [this] {
        m_app.retire_mesure_avant();
        m_app.rafraichit();
    });
    connect(m_refaire, &QToolButton::clicked, this, [this] {
        m_app.retablit_point();
        m_app.rafraichit();
    });
    m_plein_ecran = outil(m_dans_plein_ecran ? icones::Commande::QuitterPleinEcran : icones::Commande::PleinEcran);
    m_plein_ecran->setObjectName(u"plein_ecran"_s);
    m_plein_ecran->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
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

void ControlesImage::actualise()
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
    m_gauche->setAccessibleName(m_gauche->toolTip());
    m_droite->setAccessibleName(m_droite->toolTip());
    m_image->setText(trad("Image n°"));
    m_image->setToolTip(trad("Choix de l'image dans la vidéo"));
    m_image->setEnabled(actif);
    const int max = std::max(a.nombre_images_video, 1);
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
    m_defaire->setEnabled(a.annulation_possible());
    m_defaire->setToolTip(trad("Annuler le dernier point (Ctrl+Z)"));
    m_refaire->setEnabled(a.retablissement_possible());
    m_refaire->setToolTip(trad("Rétablir le point (Ctrl+Y)"));
    m_defaire->setAccessibleName(m_defaire->toolTip());
    m_refaire->setAccessibleName(m_refaire->toolTip());
    if (m_dans_plein_ecran) {
        m_plein_ecran->setText(trad("Quitter le plein écran"));
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
    m_origine->setIcon(icones::commande(icones::Commande::Origine));
    m_origine->setCheckable(true);
    connect(m_origine, &QPushButton::clicked, this, [this] {
        m_app.place_origine();
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
            m_app.guide.axes_choisis = true;
            m_app.pointage.sens_x = sx;
            m_app.pointage.sens_y = sy;
            m_app.guide.axes_choisis = true;
            m_app.rafraichit();
        });
        m_axes[k] = b;
        ligne->addWidget(b);
    }
    separation();

    // 3. l'échelle
    m_t_echelle = etape();
    m_echelle = new QPushButton(this);
    m_echelle->setIcon(icones::commande(icones::Commande::Etalon));
    connect(m_echelle, &QPushButton::clicked, this, [this] {
        m_app.saisit_echelle();
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
    m_annuler->setIcon(icones::commande(icones::Commande::Fermer));
    connect(m_annuler, &QPushButton::clicked, this, [this] {
        if (m_app.etat == Etat::Etalonnage)
            m_app.annule_echelle();
        else if (m_app.choix_origine)
            m_app.place_origine();
        m_app.rafraichit();
    });
    lc->addWidget(m_consigne, 1);
    lc->addWidget(m_annuler);
    l->addWidget(m_ligne_consigne);
}

void BarreRepere::actualise()
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
                                     nombre_local(e.etalon_pixels(), 4)) + u"\n"_s + a.precision_etalon(e.etalon_pixels()));
        m_echelle_etat->setToolTip(trad("{} pixels par mètre", a.texte_echelle()));
        dessin::colore(m_echelle_etat, dessin::Ton::Reussite);
    } else {
        m_echelle_etat->setText(trad("non définie : les mesures sont en pixel"));
        m_echelle_etat->setToolTip(QString());
        dessin::colore(m_echelle_etat, dessin::Ton::Discret);
    }

    const bool consigne = a.etat == Etat::Etalonnage || a.choix_origine;
    m_ligne_consigne->setVisible(consigne);
    m_consigne->setText(a.etat == Etat::Etalonnage ? a.consigne_etalon()
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
    const auto bouton = [this](icones::Commande commande, auto action) {
        auto *b = new QPushButton(icones::commande(commande), QString(), this);
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
    m_nombre_objets = new QSpinBox(this);
    m_nombre_objets->setRange(1, 10);
    connect(m_nombre_objets, &QSpinBox::valueChanged, this, [this](int n) {
        m_app.nombre_objets_demandes = n;
        m_app.ajuste_nombre_objets();
        m_app.rafraichit();
    });
    ligne_objets->addWidget(m_objets);
    ligne_objets->addWidget(m_nombre_objets);
    ligne_objets->addStretch(1);
    l->addLayout(ligne_objets);
    m_auto = new QCheckBox(this);
    m_auto->setIcon(icones::robot());
    connect(m_auto, &QCheckBox::toggled, this, [this](bool v) {
        m_app.suivi_auto = v;
        m_app.rafraichit();
    });
    l->addWidget(m_auto);
    m_demarrer = bouton(icones::Commande::Demarrer, [this] { m_app.lance_pointage(); });
    m_arreter = bouton(icones::Commande::Arreter, [this] { m_app.arrete_pointage(); });
    for (QPushButton *b : {m_demarrer, m_arreter})
        dessin::demi_gras(b);
    m_stop = bouton(icones::Commande::Arreter, [this] { m_app.arrete_suivi(); });
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
        m_app.saisie_cadence = t;
        m_app.controle_cadence();
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
    m_recommencer = bouton(icones::Commande::Recommencer, [this] { m_app.confirme_si_mesures({Apres::Type::ToutRecommencer}); });
    l->addWidget(m_recommencer);
    l->addStretch(1);
}

void ControlesPointage::actualise()
{
    App &a = m_app;
    const Etat etat = a.etat;
    const bool en_preparation = etat == Etat::Preparation && !a.choix_origine;
    const bool modifiable = a.repere_modifiable() && !a.choix_origine;

    m_loupe->actualise();

    m_t_pointage->setText(trad("Objets suivis"));
    m_objets->setText(trad("Objets à suivre :"));
    m_objets->setToolTip(trad("Nombre d'objets à suivre"));
    m_objets->setEnabled(en_preparation);
    {
        const QSignalBlocker b1(m_nombre_objets), b2(m_auto);
        m_nombre_objets->setValue(a.nombre_objets_demandes);
        m_auto->setChecked(a.suivi_auto);
    }
    m_nombre_objets->setEnabled(en_preparation);
    m_auto->setText(trad("Suivi automatique"));
    m_auto->setToolTip(trad("Le programme retrouve les objets sur chaque image : tracez d'abord un cadre autour de chacun"));
    m_auto->setEnabled(en_preparation);
    // pendant le pointage, « Arrêter » ; une fois arrêté, « Reprendre », avec les mesures déjà faites
    const bool arrete = etat == Etat::Pointage && a.pointage_arrete;
    if (arrete) {
        m_demarrer->setText(trad("Reprendre le pointage"));
        m_demarrer->setToolTip(trad("Continuer le pointage, avec les mesures déjà faites"));
    } else {
        m_demarrer->setText(trad("Démarrer le pointage"));
        m_demarrer->setToolTip(trad("Commencer le suivi des objets (manuel ou automatique)"));
    }
    m_demarrer->setEnabled(en_preparation || (arrete && !a.refait && !a.choix_origine));
    m_arreter->setText(trad("Arrêter le pointage"));
    m_arreter->setToolTip(trad("Finir le pointage : un clic sur l'image ne pointe plus, et les coordonnées s'affichent"));
    m_arreter->setEnabled(!a.refait && !a.choix_origine);
    if (etat == Etat::SuiviAutomatique)
        m_pile_pointage->setCurrentWidget(m_stop);
    else if (etat == Etat::Pointage && !a.pointage_arrete)
        m_pile_pointage->setCurrentWidget(m_arreter);
    else
        m_pile_pointage->setCurrentWidget(m_demarrer);
    m_stop->setText(a.prochaine_image_du_suivi ? u"STOP (%1)"_s.arg(*a.prochaine_image_du_suivi) : u"STOP"_s);
    m_stop->setToolTip(trad("Arrêter le suivi automatique"));

    m_t_video->setText(trad("Vidéo"));
    if (a.video) {
        const QSize d = a.video->dimensions();
        const double duree = a.video->info.duree_a_la_cadence(a.cadence_prise_de_vue);
        m_nom_video->setText(QFileInfo(a.fichier_video).fileName());
        m_info_video->setText(a.nombre_images_video > 1 ? trad("{} × {} px · {} images · {} s", d.width(), d.height(), a.nombre_images_video,
                                                     nombre_local(duree, 3))
                                              : trad("{} × {} px · {} image · {} s", d.width(), d.height(), a.nombre_images_video,
                                                     nombre_local(duree, 3)));
    }
    const QString aide_ips = trad("Le nombre d'images par seconde est détecté automatiquement. Pour une vidéo au ralenti, "
                                  "indiquez la cadence de prise de vue (par exemple 240).");
    m_ips->setText(trad("Images par seconde :"));
    m_ips->setToolTip(aide_ips);
    m_ips->setEnabled(modifiable);
    if (m_saisie_ips->text() != a.saisie_cadence) {
        const QSignalBlocker b(m_saisie_ips);
        m_saisie_ips->setText(a.saisie_cadence);
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
    m_recommencer->setEnabled(etat == Etat::Preparation || etat == Etat::Pointage);
}

void ControlesPointage::maj_souris()
{
    m_loupe->actualise();
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
    auto *pointage = new QHBoxLayout();
    m_tableau = new TableauMesures(app, droite, TableauMesures::Presentation::Numeros, m_zone);
    pointage->addWidget(m_zone, 1);
    pointage->addWidget(m_tableau);
    ld->addLayout(pointage, 1);
    ld->addWidget(m_controles_image);
    l->addWidget(new dessin::SeparationLaterale(dessin::panneau_lateral(m_controles, this), droite, this), 1);
}

void OngletPointage::actualise()
{
    m_barre->actualise();
    m_controles->actualise();
    m_controles_image->actualise();
    m_tableau->actualise();
    m_zone->actualise();
}

void OngletPointage::maj_souris()
{
    m_controles->maj_souris();
    m_zone->update();
}
