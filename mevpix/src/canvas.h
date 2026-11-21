// Zone image : affichage, navigation (molette, clic droit), tracé et modification des segments.

#pragma once

#include "i18n.h"
#include "session.h"
#include "view.h"

#include <QWidget>

#include <optional>

class CanevasImage : public QWidget {
    Q_OBJECT

public:
    CanevasImage(Seance &seance, QWidget *parent = nullptr);

    void definit_traductions(const Textes *tr);
    /// Agrandissement de l'interface : épaisseur des traits, poignées et étiquettes.
    void change_echelle_interface(double facteur);
    void zoome_au_centre(double facteur);
    /// Déplace l'image d'un dixième de la zone visible dans le sens de la flèche.
    void decale_image(QPoint sens);
    void demande_ajustement();
    /// Abandonne le tracé ou le déplacement en cours (Échap, changement d'outil).
    bool annule_glisser();
    /// Zoom et position de l'image (conversions écran ↔ image des essais de l'interface).
    const VueImage &vue() const { return m_vue; }

signals:
    /// Nouveau segment tracé, assez long pour être gardé.
    void segment_trace(Segment seg);
    /// Indique quelle mesure, quel étalon ou quelle coupe vient de changer.
    void modifie(CibleTrace cible);
    /// Le geste sur la coupe est terminé, le curseur actif peut être ajusté au clavier.
    void profil_selectionne();
    /// Le zoom de l'image a changé (molette, ajustement à la fenêtre) : pixels écran par pixel image.
    void zoom_change(double zoom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct GesteImage {
        enum Nature {
            /// Tracé d'un nouveau segment depuis `depart` (coordonnées image).
            NouveauTrace,
            /// Déplacement d'une extrémité (`extremiteB` : extrémité b, sinon a).
            Poignee,
            /// Déplacement du curseur A (0) ou B (1) le long de la ligne de coupe.
            Curseur,
        } nature;
        QPointF depart;
        CibleTrace cible{CibleTrace::Etalon};
        bool extremiteB = false;
        int indiceCurseur = 0;
    };

    /// Poignée (extrémité de segment ou curseur A/B) la plus proche de `pos`, si elle est assez
    /// proche. À égalité, l'extrémité l'emporte : les curseurs restent déplaçables sur le graphique.
    std::optional<GesteImage> saisit_en(QPointF pos) const;
    void glisse_vers(QPointF pos, bool aligner);
    void maj_forme_curseur(QPointF pos);
    void dessine_surcouches(QPainter &painter) const;
    void dispose_vue();

    Seance &m_seance;
    const Textes *m_textes = nullptr;
    VueImage m_vue;
    std::optional<GesteImage> m_geste;
    std::optional<Travail> m_avantGlisser;
    /// Segment en cours de tracé.
    std::optional<Segment> m_traceProvisoire;
    bool m_deplacementVue = false;
    QPointF m_dernierPointeur;
    double m_echelleInterface = 1.0;
    /// Dernier zoom signalé par `zoom_change`.
    double m_zoomSignale = 0.0;
};
