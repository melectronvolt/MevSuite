// Réglages d'un graphe : courbes (style, incertitudes), axes, titre et légende, modèles,
// annotations.

#pragma once

#include "app/graphwidget.h"

#include <QTimer>
#include <QWidget>

#include <set>

class QComboBox;
class QScrollArea;
class QButtonGroup;
class QVBoxLayout;

class GraphSettings : public QWidget {
    Q_OBJECT

public:
    GraphSettings(MainWindow &win, ViewId id, QWidget *parent = nullptr);

    void showSection(SettingsSection section, int axis);
    void selectPlot(int plot);

Q_SIGNALS:
    void highlightChanged(std::optional<std::pair<int, int>> curve);
    /// Bouton ✕ des réglages.
    void closeRequested();

protected:
    void showEvent(QShowEvent *e) override;

private:
    void scheduleRebuild();
    /// Reconstruit dès que l'utilisateur n'est plus en train de saisir.
    void tryRebuild();
    void rebuild();
    int plotIndex() const;
    void buildCurves(QVBoxLayout *l);
    void buildAxes(QVBoxLayout *l);
    void buildTitle(QVBoxLayout *l);
    void buildModels(QVBoxLayout *l);
    void buildAnnotations(QVBoxLayout *l);
    /// Modifie le graphe affiché dans les réglages.
    void editPlot(const std::function<void(Plot &)> &f);

    MainWindow &m_win;
    ViewId m_id;
    QComboBox *m_plotCombo;
    QButtonGroup *m_sections;
    QScrollArea *m_scroll;
    QWidget *m_page = nullptr;
    QTimer m_rebuild;
    int m_focusAxis = -1;
    /// Une modification est arrivée pendant que les réglages étaient cachés.
    bool m_stale = false;
    std::vector<bool> m_expanded;
    /// Courbes dont les listes « une grandeur pour chaque côté » sont ouvertes.
    std::set<size_t> m_errorSides;
};
