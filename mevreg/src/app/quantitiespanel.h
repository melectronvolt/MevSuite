// Fenêtre Grandeurs : toutes les grandeurs du document, leur type, leurs usages ; on les glisse
// vers les tableurs et les graphes.

#pragma once

#include "core/document.h"

#include <QWidget>

class MainWindow;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

class QuantitiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit QuantitiesPanel(MainWindow &win, QWidget *parent = nullptr);

    /// Menu d'une grandeur (aussi proposé ailleurs).
    static void fillMenu(QMenu *menu, MainWindow &win, QId id, QWidget *owner);

private:
    void refresh();
    void itemChanged(QTreeWidgetItem *item, int column);

    MainWindow &m_win;
    QTreeWidget *m_tree;
    QLabel *m_empty;
    /// « Glissez une grandeur… » : seulement quand il y en a.
    QLabel *m_dragHint;
    /// Largeur des colonnes Unité ou Valeurs changée à la main : gardée.
    bool m_userSized = false;
    bool m_filling = false;
    bool m_pending = false;
};
