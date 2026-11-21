// Graphique dessiné avec QPainter : axes gradués, grille, réticule et valeur sous la
// souris. Molette : zoom horizontal autour de la souris ; Maj + molette, molette horizontale, clic
// droit ou milieu glissé : déplacement ; double-clic : vue complète. L'axe vertical est fixé par le
// graphique (signal ou spectre).

#pragma once

#include "i18n.h"

#include <QWidget>
#include <QTimer>
#include <functional>

#include <optional>
#include <span>
#include <utility>
#include <vector>

class QPainter;
class QPolygonF;

class PlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget *parent = nullptr);

    void definit_traductions(const Tr *tr);
    /// Épaisseur des traits et taille des repères, comme l'agrandissement de l'interface.
    void change_echelle_interface(double scale);
    /// Revient à la vue complète (nouveau son, double-clic, menu « Tout voir »).
    void reinitialise_vue();
    /// Le signal, le spectre ou l'échelle verticale a changé : recalculer la courbe au prochain dessin.
    void invalide_courbes();
    void annule_clic();
    /// Impose les bornes horizontales visibles (unités de l'axe).
    void definit_vue_x(double x0, double x1);

    /// Zone des courbes, à l'intérieur des axes.
    QRectF cadre_graphique() const;
    /// Abscisse à l'écran (pixels du widget) d'une valeur de l'axe horizontal, et inversement.
    double ecran_x(double x) const;
    double donnee_x(double px) const;
    double ecran_y(double y) const;
    double donnee_y(double py) const;
    double vue_x0() const { return m_x0; }
    double vue_x1() const { return m_x1; }

protected:
    /// Pendant le dessin, le cadre est déjà mesuré pour la police et le zoom en cours.
    double ecran_x(double x, const QRectF &cadre) const;
    double ecran_y(double y, const QRectF &cadre) const;

    struct Tick {
        double value;
        QString label;
    };

    /// Vrai si le graphique a quelque chose à tracer ; sinon `dessine_vide` le remplace.
    virtual bool a_des_donnees() const = 0;
    /// Graphique vide : une explication au centre.
    virtual void dessine_vide(QPainter &painter) = 0;
    /// Bornes horizontales de la vue complète.
    virtual std::pair<double, double> x_par_defaut() const = 0;
    /// Domaine que l'on peut explorer. Le spectre peut dépasser sa vue initiale de 5 kHz.
    virtual std::pair<double, double> domaine_x() const { return x_par_defaut(); }
    /// Prépare les points du tracé et donne les bornes verticales de cette image.
    virtual std::pair<double, double> prepare_courbes(size_t colonnes) = 0;
    /// Ordonnées extrêmes de la courbe dans la vue, segments aux bords compris.
    std::optional<std::pair<double, double>> extrema_visibles(std::span<const QPointF> points) const;
    std::pair<double, double> plage_y() const { return m_plageY; }
    virtual QString legende_axe_x() const = 0;
    virtual QString legende_axe_y() const = 0;
    /// Graduations de l'axe horizontal ; par défaut, des pas ronds (1, 2 ou 5 × 10ⁿ).
    virtual std::vector<Tick> graduations_x(double x0, double x1, double pixels) const;
    /// Texte du réticule, pour le point (`x`, `y`) sous la souris.
    virtual QString texte_survol(QPointF data) const = 0;
    /// Courbes et repères, dans la zone `r` (le dessin y est limité).
    virtual void dessine_donnees(QPainter &painter, const QRectF &r) = 0;
    /// Le pointeur a bougé (ou quitté le graphique : vide).
    virtual void survol_deplace(std::optional<QPointF>) {}

    // Bouton gauche : appui, glisser, relâchement, à la position `pos` (pixels du widget).
    void termine_clic();
    void differe_clic(std::function<void()> action);
    virtual void gauche_appuye(QPointF) {}
    virtual void gauche_deplace(QPointF) {}
    virtual void gauche_relache(QPointF) {}
    /// Clic droit sans glisser.
    virtual void clic_droit(QPointF) {}

    /// Graduations rondes entre `lo` et `hi`, pour environ `count` graduations.
    std::vector<Tick> graduations_rondes(double lo, double hi, double count) const;
    /// Ligne brisée de nombreux points, avec le stylo du `painter`, tracée segment par segment : d'un
    /// seul tenant, une longue ligne épaisse et lissée qui zigzague (le signal) prend plus d'une
    /// demi-seconde avec Qt 6.10 et 6.11 (5 ms par segments), et l'interface se fige à chaque image.
    static void trace_courbe(QPainter &painter, const QPolygonF &line);
    double echelle_interface() const { return m_uiScale; }
    const Tr *textes_interface() const { return m_tr; }
    std::optional<QPointF> survol() const { return m_hover; }
    /// Texte centré dans le graphique (graphique vide, calcul en cours).
    void dessine_message(QPainter &painter, const QString &text, double scale = 1.0);

    virtual double centre_zoom_clavier() const { return (m_x0 + m_x1) / 2; }
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /// Bornes à jour : vue complète demandée, ou première vue.
    void assure_vue();
    void decale_pixels(double pixels);
    /// Garde le repère à la même fraction de la largeur (0 à gauche, 0,5 au centre) tant que les bords le permettent.
    void place_vue(double largeur, double repere, double positionRelative);

    QTimer m_simpleClic;
    std::function<void()> m_actionClic;
    const Tr *m_tr = nullptr;
    double m_uiScale = 1.0;
    double m_x0 = 0.0;
    double m_x1 = 1.0;
    std::pair<double, double> m_plageY = {-1.0, 1.0};
    /// Le déplacement du réticule ou de la tête de lecture conserve les mêmes points à tracer.
    struct VueCourbes {
        double debut;
        double fin;
        size_t colonnes;
        bool operator==(const VueCourbes &) const = default;
    };
    std::optional<VueCourbes> m_vuePreparee;
    double m_largeurGraduationsY = 0.0;
    bool m_needsReset = true;
    std::optional<QPointF> m_hover;
    /// Glisser du bouton droit ou du milieu : dernière position, et déplacement depuis l'appui.
    std::optional<QPointF> m_panFrom;
    double m_panDistance = 0.0;
    Qt::MouseButton m_panButton = Qt::NoButton;
};
