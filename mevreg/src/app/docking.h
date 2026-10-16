// Déplacement des fenêtres ancrées. Qt choisit d'ordinaire la place d'une fenêtre glissée d'après
// la seule position du pointeur ; ici, des repères s'affichent sur la fenêtre survolée (au centre :
// en onglet ; sur un côté : à côté) et sur les bords de l'espace de travail (sur toute la hauteur
// ou toute la largeur), avec l'aperçu de la place que prendra la fenêtre. La disposition est
// relevée en arbre d'après la position des fenêtres, modifiée, puis refaite.

#pragma once

#include <QPointer>
#include <QRect>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QDockWidget;
class QMainWindow;
class QTabBar;

namespace docking {

enum class Side { Center, Left, Right, Top, Bottom };

/// Où poser une fenêtre : sur une pile d'onglets (désignée par l'une de ses fenêtres), ou sur un
/// bord de l'espace de travail (`group` nul).
struct Target {
    QDockWidget *group = nullptr;
    Side side = Side::Center;
    bool operator==(const Target &) const = default;
};

/// Pile d'onglets : fenêtres dans l'ordre des onglets, fenêtre affichée, place occupée (onglets
/// compris), dans le repère de la fenêtre principale.
struct Group {
    std::vector<QDockWidget *> tabs;
    QDockWidget *current = nullptr;
    QRect rect;
    QTabBar *tabBar = nullptr;
};

/// Disposition : nœuds partagés en colonnes (Qt::Horizontal) ou en lignes (Qt::Vertical), feuilles
/// en piles d'onglets.
struct Node {
    Qt::Orientation orientation = Qt::Horizontal;
    std::vector<Node> children;
    std::vector<QDockWidget *> tabs;
    QDockWidget *current = nullptr;
    QRect rect;
    bool leaf() const { return children.empty(); }
};

/// Piles d'onglets des fenêtres ancrées `docks` (les fenêtres flottantes sont ignorées).
std::vector<Group> groups(const QMainWindow *win, const std::vector<QDockWidget *> &docks);
/// Arbre de la disposition, relevé d'après la place des piles.
Node capture(const QMainWindow *win, const std::vector<QDockWidget *> &docks);
/// Refait la disposition de la fenêtre principale selon l'arbre.
void apply(QMainWindow *win, const Node &root);
/// Fenêtres de l'arbre, dans l'ordre.
std::vector<QDockWidget *> docksIn(const Node &n);

/// Largeur (ou hauteur) donnée à une fenêtre posée sur un bord de l'espace de travail.
int edgeExtent(const QRect &workspace, QSize dragged, Side side);
/// Place que prendra la fenêtre posée sur la cible.
QRect preview(const Target &t, const std::vector<Group> &groups, const QRect &workspace, QSize dragged);

/// Déplace `dock` vers la cible ; faux si c'est impossible (cible absente, fenêtre seule…).
bool move(Node &root, QDockWidget *dock, const Target &t, QSize dragged, int separator);
/// Ajoute `dock` en colonne, à la position `index` parmi les colonnes, large de `width` ; les autres
/// colonnes se partagent le reste de la largeur.
void insertColumn(Node &root, QDockWidget *dock, size_t index, int width, int separator);

/// Calque posé sur la fenêtre principale pendant le glissement d'une fenêtre ancrée : repères,
/// aperçu de la place prise ; Échap ou un clic droit annule.
class Overlay : public QWidget {
    Q_OBJECT

public:
    Overlay(QMainWindow *win, QDockWidget *dragged, const std::vector<QDockWidget *> &docks);

    /// Commence le glissement (le calque capture la souris et le clavier).
    void start(QPoint globalPos);
    /// Cible sous le point (repère de la fenêtre principale).
    std::optional<Target> targetAt(QPoint pos) const;
    /// Repères affichés quand le pointeur est en `pos` (pour les tests).
    std::vector<std::pair<Target, QRect>> markers(QPoint pos) const;
    /// Survol en `pos` (repère de la fenêtre principale).
    void hover(QPoint pos);
    /// Lâcher : `onDrop` reçoit la cible survolée, s'il y en a une.
    void finish();
    void cancel();

    std::function<void(const Target &)> onDrop;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    const Group *groupAt(QPoint pos) const;
    bool canDropOn(const Group &g) const;
    QString describe(const Target &t) const;
    void close();

    QPointer<QDockWidget> m_dragged;
    std::vector<Group> m_groups;
    QRect m_workspace;
    QPoint m_pos;
    std::optional<Target> m_target;
    bool m_done = false;
};

} // namespace docking
