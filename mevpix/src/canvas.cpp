#include "canvas.h"

#include "style.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Distance (pixels écran) en dessous de laquelle on attrape l'extrémité d'un segment.
constexpr double RAYON_POIGNEE = 9.0;
/// Longueur minimale (pixels écran) d'un nouveau segment, pour ignorer les simples clics.
constexpr double TRACE_MIN_ECRAN = 4.0;

} // namespace

CanevasImage::CanevasImage(Seance &seance, QWidget *parent)
    : QWidget(parent)
    , m_seance(seance)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumHeight(100);
}

void CanevasImage::definit_traductions(const Textes *tr)
{
    m_textes = tr;
    update();
}

void CanevasImage::change_echelle_interface(double facteur)
{
    m_echelleInterface = facteur;
    update();
}

void CanevasImage::zoome_au_centre(double facteur)
{
    if (!m_seance.image)
        return;
    dispose_vue();
    m_vue.zoome_autour(QRectF(rect()).center(), facteur);
    update();
}

void CanevasImage::decale_image(QPoint sens)
{
    if (!m_seance.image)
        return;
    dispose_vue();
    m_vue.deplace_vue(QPointF(sens.x() * width(), sens.y() * height()) / 10.0);
    update();
}

void CanevasImage::demande_ajustement()
{
    m_vue.demande_ajustement();
    update();
}

bool CanevasImage::annule_glisser()
{
    const bool deplacaitImage = std::exchange(m_deplacementVue, false);
    if (!m_geste) {
        if (deplacaitImage)
            maj_forme_curseur(m_dernierPointeur);
        return deplacaitImage;
    }
    const CibleTrace cible = m_geste->cible;
    m_geste.reset();
    m_traceProvisoire.reset();
    if (m_avantGlisser) {
        static_cast<Travail &>(m_seance) = std::move(*m_avantGlisser);
        m_avantGlisser.reset();
        emit modifie(cible);
    }
    maj_forme_curseur(m_dernierPointeur);
    update();
    return true;
}

void CanevasImage::dispose_vue()
{
    if (m_seance.image)
        m_vue.dispose(QSizeF(size()), m_seance.image->taille());
}

std::optional<CanevasImage::GesteImage> CanevasImage::saisit_en(QPointF pos) const
{
    std::optional<GesteImage> cible;
    const double rayon = RAYON_POIGNEE * m_echelleInterface;
    double distanceCarreeMax = rayon * rayon;
    const auto examine = [&](const GesteImage &geste, QPointF point) {
        const QPointF ecart = m_vue.vers_ecran(point) - pos;
        const double distanceCarree = QPointF::dotProduct(ecart, ecart);
        if (distanceCarree < distanceCarreeMax || (!cible && distanceCarree == distanceCarreeMax)) {
            cible = geste;
            distanceCarreeMax = distanceCarree;
        }
    };
    const auto examine_poignees = [&](CibleTrace segment, const Segment &trace) {
        examine(GesteImage{GesteImage::Poignee, {}, segment, false}, trace.a);
        examine(GesteImage{GesteImage::Poignee, {}, segment, true}, trace.b);
    };

    // Cette recherche a lieu à chaque survol. Lire les mesures directement évite de copier la liste.
    switch (m_seance.outil) {
    case Outil::Etalonner:
        if (m_seance.segmentEtalon)
            examine_poignees({CibleTrace::Etalon}, *m_seance.segmentEtalon);
        break;
    case Outil::Mesurer:
        for (int i = 0; i < m_seance.mesures.size(); ++i)
            examine_poignees({CibleTrace::Mesure, i}, m_seance.mesures.at(i));
        break;
    case Outil::Profil:
        if (m_seance.coupe) {
            examine_poignees({CibleTrace::Coupe}, *m_seance.coupe);
            // À distance égale, une extrémité garde la priorité, puis vient le curseur actif.
            for (int i : {m_seance.curseurActif, 1 - m_seance.curseurActif}) {
                const QPointF position = m_seance.coupe->point_en(m_seance.fractionsCurseurs[i]);
                examine(GesteImage{GesteImage::Curseur, {}, {CibleTrace::Coupe}, false, i}, position);
            }
        }
        break;
    }
    return cible;
}

void CanevasImage::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    m_dernierPointeur = pos;
    if (!m_seance.image)
        return;
    dispose_vue();
    if (event->button() == Qt::LeftButton && !m_deplacementVue) {
        m_geste = saisit_en(pos);
        if (m_geste) {
            m_avantGlisser = m_seance;
            if (m_geste->nature == GesteImage::Curseur) {
                m_seance.curseurActif = m_geste->indiceCurseur;
                emit modifie(m_geste->cible);
            }
        } else {
            // On peut finir un tracé au bord de la photo, mais il doit commencer sur la photo.
            const QPointF debut = m_vue.vers_image(pos);
            if (QRectF(QPointF(), m_seance.image->taille()).contains(debut))
                m_geste = GesteImage{GesteImage::NouveauTrace, debut};
        }
    } else if (!m_geste && (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)) {
        m_deplacementVue = true;
    }
    maj_forme_curseur(pos);
}

void CanevasImage::glisse_vers(QPointF pos, bool aligner)
{
    if (!m_geste || !m_seance.image)
        return;
    const QPointF p = m_seance.image->borne(m_vue.vers_image(pos));
    switch (m_geste->nature) {
    case GesteImage::NouveauTrace:
        m_traceProvisoire = Segment{m_geste->depart, aligner ? aligne_sur_axe(m_geste->depart, p) : p};
        break;
    case GesteImage::Poignee:
        if (Segment *seg = m_seance.segment(m_geste->cible)) {
            const QPointF extremiteFixe = m_geste->extremiteB ? seg->a : seg->b;
            const QPointF extremiteMobile = aligner ? aligne_sur_axe(extremiteFixe, p) : p;
            (m_geste->extremiteB ? seg->b : seg->a) = extremiteMobile;
            emit modifie(m_geste->cible);
        }
        break;
    case GesteImage::Curseur:
        if (m_seance.coupe) {
            m_seance.fractionsCurseurs[m_geste->indiceCurseur] = m_seance.coupe->projette(p);
            emit modifie(m_geste->cible);
        }
        break;
    }
    update();
}

void CanevasImage::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (m_deplacementVue) {
        m_vue.deplace_vue(pos - m_dernierPointeur);
        update();
    } else if (event->buttons() & Qt::LeftButton) {
        glisse_vers(pos, event->modifiers() & Qt::ShiftModifier);
    }
    m_dernierPointeur = pos;
    maj_forme_curseur(pos);
}

void CanevasImage::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_geste) {
        if (event->position() != m_dernierPointeur)
            glisse_vers(event->position(), event->modifiers() & Qt::ShiftModifier);
        // Pendant le geste, une poignée peut croiser l'autre. Au relâchement, une longueur
        // inférieure à un pixel ramène le segment à sa position de départ, comme Échap.
        if (m_geste->nature == GesteImage::Poignee) {
            const Segment *segment = m_seance.segment(m_geste->cible);
            if (segment && !segment->est_mesurable()) {
                annule_glisser();
                return;
            }
        }
        const std::optional<Segment> seg = std::exchange(m_traceProvisoire, std::nullopt);
        m_geste.reset();
        if (m_avantGlisser) {
            m_seance.memorise(std::move(*m_avantGlisser));
            m_avantGlisser.reset();
        }
        if (seg && seg->est_mesurable() && seg->longueur_px() * m_vue.zoom() >= TRACE_MIN_ECRAN)
            emit segment_trace(*seg);
        if (m_seance.outil == Outil::Profil && m_seance.coupe)
            emit profil_selectionne();
        update();
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_deplacementVue = m_deplacementVue && (event->buttons() & (Qt::RightButton | Qt::MiddleButton));
    }
    maj_forme_curseur(event->position());
}

void CanevasImage::keyPressEvent(QKeyEvent *event)
{
    // Maj enfoncée ou relâchée pendant le tracé : le segment suit sans attendre un mouvement de souris.
    if (event->key() == Qt::Key_Shift && m_geste)
        glisse_vers(m_dernierPointeur, true);
    else
        QWidget::keyPressEvent(event);
}

void CanevasImage::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift && m_geste)
        glisse_vers(m_dernierPointeur, false);
    else
        QWidget::keyReleaseEvent(event);
}

void CanevasImage::wheelEvent(QWheelEvent *event)
{
    if (!m_seance.image)
        return;
    // Molette : zoom centré sur la souris (un cran ≈ 10 %) ; pavé tactile : défilement fin.
    const double delta =
        event->pixelDelta().isNull() ? event->angleDelta().y() / 120.0 * 50.0 : double(event->pixelDelta().y());
    if (delta != 0.0) {
        dispose_vue();
        m_vue.zoome_autour(event->position(), std::exp(delta * 0.002));
        update();
    }
    event->accept();
}

bool CanevasImage::event(QEvent *event)
{
    // Si la fenêtre perd la souris avant le relâchement, un tracé provisoire est abandonné.
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate)
        annule_glisser();
    // Pincement sur un pavé tactile : zoom centré sur les doigts.
    if (event->type() == QEvent::NativeGesture && m_seance.image) {
        const auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            dispose_vue();
            m_vue.zoome_autour(gesture->position(), 1.0 + gesture->value());
            update();
            return true;
        }
    }
    return QWidget::event(event);
}

void CanevasImage::maj_forme_curseur(QPointF pos)
{
    if (!m_seance.image) {
        unsetCursor();
        return;
    }
    if (m_geste && m_geste->nature != GesteImage::NouveauTrace)
        setCursor(Qt::ClosedHandCursor);
    else if (m_deplacementVue)
        setCursor(Qt::SizeAllCursor);
    else if (!m_geste && saisit_en(pos))
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(Qt::CrossCursor);
}

void CanevasImage::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // Fond clair en thème clair, sombre en thème sombre ; un peu différent de celui des panneaux pour
    // que les bords de l'image restent visibles.
    const bool dark = est_sombre(palette());
    painter.fillRect(rect(), dark ? QColor(20, 20, 22) : QColor(222, 223, 226));

    if (m_seance.image) {
        dispose_vue();
        m_vue.dessine_image(painter, QSizeF(size()), *m_seance.image);
        dessine_surcouches(painter);
        if (m_vue.zoom() != m_zoomSignale) {
            m_zoomSignale = m_vue.zoom();
            emit zoom_change(m_zoomSignale);
        }
    }
    // Sans image, la fenêtre montre la page d'accueil à la place de la zone image.
}

void CanevasImage::dessine_surcouches(QPainter &painter) const
{
    if (!m_textes)
        return;
    const Textes &tr = *m_textes;
    const QChar d = tr.decimal;
    const auto &calib = m_seance.etalonnage;
    const auto versEcran = [&](QPointF p) { return m_vue.vers_ecran(p); };
    const double s = m_echelleInterface;

    // Chaque outil n'affiche que ses segments : l'étalon (jaune) avec Étalonner, les mesures (bleu)
    // avec Mesurer, la ligne de coupe (rose) avec Profil d'intensité.
    if (const auto &seg = m_seance.segmentEtalon; seg && m_seance.outil == Outil::Etalonner) {
        dessine_segment(painter, versEcran(seg->a), versEcran(seg->b), COLOR_CALIB, true, s);
        const QString value = calib && calib->correspond(Etalonnage{calib->longueurReelle, seg->longueur_px(), calib->unite})
                                  ? texte_nombre(calib->longueurReelle, 3, d) + u' ' + symbole_unite(calib->unite)
                                  : texte_nombre(seg->longueur_px(), 4, d) + u" px"_s;
        dessine_etiquette(painter, ancre_etiquette(m_vue, *seg, s), tr.calibTag(value), COLOR_CALIB, s);
    }

    for (qsizetype i = 0; m_seance.outil == Outil::Mesurer && i < m_seance.mesures.size(); ++i) {
        const Segment &seg = m_seance.mesures.at(i);
        dessine_segment(painter, versEcran(seg.a), versEcran(seg.b), COLOR_MEASURE, true, s);
        const QString text = tr.measureTag(static_cast<int>(i) + 1, texte_longueur(seg.longueur_px(), calib, d));
        dessine_etiquette(painter, ancre_etiquette(m_vue, seg, s), text, COLOR_MEASURE, s);
    }

    if (m_seance.outil == Outil::Profil && m_seance.coupe) {
        const Segment &seg = *m_seance.coupe;
        if (m_seance.largeurBande > 1) {
            const QPointF demiLargeur = seg.normale() * (m_seance.largeurBande / 2.0) * m_vue.zoom();
            QColor largeurBande = COLOR_CUT;
            largeurBande.setAlphaF(0.25f);
            dessine_bande(painter, versEcran(seg.a), versEcran(seg.b), demiLargeur, largeurBande);
        }
        dessine_segment(painter, versEcran(seg.a), versEcran(seg.b), COLOR_CUT, true, s);
        const std::pair<QString, QColor> curseurs[] = {{u"A"_s, COLOR_A}, {u"B"_s, COLOR_B}};
        for (int i : {1 - m_seance.curseurActif, m_seance.curseurActif}) {
            const QPointF p = versEcran(seg.point_en(m_seance.fractionsCurseurs[i]));
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(Qt::black, 1.5 * s));
            painter.setBrush(curseurs[i].second);
            painter.drawEllipse(p, 6.0 * s, 6.0 * s);
            painter.restore();
            dessine_etiquette(painter, p + QPointF(0.0, -10.0 * s), curseurs[i].first, curseurs[i].second, s, true);
        }
    }

    if (const auto &seg = m_traceProvisoire) {
        const QColor color = m_seance.outil == Outil::Etalonner ? COLOR_CALIB
                           : m_seance.outil == Outil::Mesurer   ? COLOR_MEASURE
                                                               : COLOR_CUT;
        dessine_segment(painter, versEcran(seg->a), versEcran(seg->b), color, false, s);
        const QString text = m_seance.outil == Outil::Etalonner ? texte_nombre(seg->longueur_px(), 4, d) + u" px"_s
                                                               : texte_longueur(seg->longueur_px(), calib, d);
        dessine_etiquette(painter, ancre_etiquette(m_vue, *seg, s), text, color, s);
    }
}
