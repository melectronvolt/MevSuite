// Fenêtre graphique : dessin, zoom, réticule accroché aux points, déplacement des points,
// sélection de plage, dépôt de grandeurs, réglages.

#pragma once

#include "plot/renderer.h"

#include <QPixmap>
#include <QWidget>

#include <optional>
#include <vector>

class GraphSettings;
class MainWindow;
class QLabel;
class QSplitter;
class QToolButton;

namespace graphops {

/// Grandeur déposée sur un graphe : abscisse si déposée sur l'axe x (`zone` 0) ou si le graphe
/// est vide, sinon nouvelle courbe en fonction de l'abscisse courante (`zone` 2 : axe de droite).
void depose_grandeur(MainWindow &win, ViewId graph, int plot, QId q, std::optional<int> zone);
/// Grandeurs déposées ensemble (colonnes d'un tableur) : une seule, comme `depose_grandeur` ;
/// plusieurs, la première en abscisse et les suivantes en ordonnées.
void depose_grandeurs(MainWindow &win, ViewId graph, int plot, const std::vector<QId> &ids, std::optional<int> zone);
/// Échelle automatique sur tous les axes d'un graphe.
void echelle_auto(Plot &p);
/// Recopie les bornes x du graphe `p` sur les autres si les abscisses sont liées.
void synchronise_x(GraphView &g, int p);

} // namespace graphops

enum class Tool { Reticle, Zoom, Pan, MovePoints, Range };

/// Sections des réglages d'un graphe.
enum class SettingsSection { Curves, Axes, Title, Models, Annotations };

class GraphCanvas : public QWidget {
    Q_OBJECT

public:
    GraphCanvas(MainWindow &win, ViewId id, QWidget *parent = nullptr);

    void choisit_outil(Tool t);
    Tool outil() const { return m_tool; }
    const std::vector<PlotLayout> &dispositions() const { return m_layouts; }
    /// Graphe sous un point du widget.
    std::optional<int> trace_en(QPointF p) const;
    void invalide();
    /// Courbe mise en évidence (réglages ouverts sur elle).
    void met_en_surbrillance(std::optional<std::pair<int, int>> h);

Q_SIGNALS:
    void reglages_demandes(SettingsSection section, int axis);
    void trace_selectionne(int plot);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    struct Nearest {
        size_t series;
        size_t index;
        double dist;
        QPointF pos;
    };
    struct Drag {
        enum Kind { None, ZoomRect, Pan, Point, Tangent, Range, Annotation, FitRange } kind = None;
        int plot = 0;
        QPointF start, last;
        int curve = 0;
        size_t index = 0;
        int tangent = 0;
        double startX = 0;
        /// Annotation glissée : partie saisie, et l'annotation avant le glissement.
        int part = 0;
        ::Annotation original;
    };
    /// Annotation sous le pointeur : indice, partie saisie (0 : la droite ou le texte ; 1, 2 :
    /// poignées d'une droite oblique).
    struct AnnotationHit {
        size_t index;
        int part;
    };

    void rendu();
    std::optional<Nearest> point_le_plus_proche(int plot, QPointF p) const;
    std::optional<AnnotationHit> annotation_en(int plot, QPointF p) const;
    /// Barre de plage du modèle affiché par la fenêtre Modélisation sous le pointeur : (modèle,
    /// borne 0 ou 1).
    std::optional<std::pair<int, int>> plage_modele_en(int plot, QPointF p) const;
    void dessine_plage_modele(QPainter &p);
    /// Annotation déplacée de (dx, dy) pixels depuis sa place d'origine, ou poignée amenée en `p`.
    ::Annotation annotation_deplacee(const PlotLayout &l, const ::Annotation &a, int part, QPointF from, QPointF p) const;
    /// Curseur de l'outil en cours.
    QCursor curseur_outil() const;
    /// Poignées et cadre de l'annotation sélectionnée.
    void dessine_selection(QPainter &p);
    void dessine_surcouche(QPainter &p);
    void lecture(QPainter &p, QPointF at, const QStringList &lines);
    int zone_depot(int plot, QPointF p) const;

    MainWindow &m_win;
    ViewId m_id;
    Tool m_tool = Tool::Reticle;
    QPixmap m_cache;
    bool m_dirty = true;
    std::vector<PlotLayout> m_layouts;
    std::vector<PlotData> m_data;
    std::optional<QPointF> m_hover;
    Drag m_drag;
    std::optional<int> m_dropPlot;
    std::optional<std::pair<int, int>> m_highlight;
    QPointF m_contextPos;
    /// Annotation sélectionnée (graphe, indice) : ses poignées sont affichées.
    std::optional<std::pair<int, size_t>> m_selected;
};

class GraphPanel : public QWidget {
    Q_OBJECT

public:
    GraphPanel(MainWindow &win, ViewId id, QWidget *parent = nullptr);

    ViewId identifiant() const { return m_id; }
    GraphCanvas *canevas() const { return m_canvas; }
    void ouvre_reglages(SettingsSection section, int axis = -1);
    int trace_choisi() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rafraichit();
    /// Réglages ouverts : à côté du graphe s'il y a la place pour les deux, sinon devant lui (le
    /// graphe est caché). Ils ne sont jamais écrasés.
    void reglages_modele();
    /// Largeur que demandent le graphe et ses réglages côte à côte.
    int largeur_cote_a_cote() const;

    MainWindow &m_win;
    ViewId m_id;
    GraphCanvas *m_canvas;
    GraphSettings *m_settings;
    QSplitter *m_splitter;
    /// « Le graphe est caché… » quand les réglages passent devant lui.
    QLabel *m_narrowHint;
    bool m_sideBySide = false;
    QToolButton *m_settingsButton;
    QToolButton *m_linkButton;
    std::vector<std::pair<Tool, QToolButton *>> m_tools;
};
