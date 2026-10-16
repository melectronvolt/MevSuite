// Fenêtre Modélisation : choix du modèle sur une grille de vignettes, plage de points,
// paramètres fixés ou valeurs initiales, résultats avec leurs incertitudes.

#pragma once

#include "core/plotspec.h"

#include <QTimer>
#include <QWidget>

#include <optional>

class MainWindow;
class QScrollArea;
class QVBoxLayout;

class ModelingPanel : public QWidget {
    Q_OBJECT

public:
    explicit ModelingPanel(MainWindow &win, QWidget *parent = nullptr);
    ~ModelingPanel() override;

    /// Fenêtre Modélisation ouverte, s'il y en a une.
    static ModelingPanel *instance();

    /// Affiche un graphe ; `fit` : modèle à afficher, -1 pour un nouveau modèle, vide pour le
    /// dernier modèle de la première courbe.
    void target(ViewId graph, int plot, std::optional<int> fit);
    /// Plage tracée à la souris sur un graphe : appliquée au modèle affiché s'il s'agit de ce
    /// graphe ; vrai si elle a servi.
    bool applyRange(ViewId graph, int plot, double a, double b);
    /// Modèle affiché, s'il s'agit de ce graphe (ses bornes de plage se glissent sur le graphe).
    std::optional<int> shownFit(ViewId graph, int plot) const;

private:
    void scheduleRebuild();
    void tryRebuild();
    void rebuild();
    void applyUserExpr();
    /// Remet la sélection (graphe, courbe, modèle) en accord avec l'espace de travail.
    bool validate();
    void build(QVBoxLayout *l);
    void choose(const fit::ModelKind &kind);
    void editFit(const std::function<void(FitSpec &)> &f);
    QString resultsText() const;

    MainWindow &m_win;
    QScrollArea *m_scroll;
    QWidget *m_page = nullptr;
    QTimer m_rebuild;
    std::optional<ViewId> m_graph;
    int m_plot = 0;
    int m_curve = 0;
    std::optional<int> m_fit;
    /// « Nouveau modèle » choisi : aucun modèle affiché, la grille attend un choix.
    bool m_new = false;
    bool m_gridOpen = false;
    int m_degree = 3;
    QString m_userExpr;
    QTimer m_exprTimer;
};
