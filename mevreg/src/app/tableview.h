// Tableur : saisie des mesures, sélection, copier-coller avec les tableurs usuels, poignée de
// recopie, glisser-déposer des grandeurs.

#pragma once

#include "core/plotspec.h"

#include <QAbstractTableModel>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QWidget>

#include <map>
#include <optional>
#include <vector>

class MainWindow;
class Store;
class QLabel;

/// Recopie incrémentée en cours : sélection (lignes r0..r1, colonnes c0..c1) prolongée jusqu'à `to`.
struct FillDrag {
    int r0 = 0, r1 = 0, c0 = 0, c1 = 0, to = 0;
};

class TableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    TableModel(Store &store, ViewId id, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /// Relit le tableur dans le document (nombre de lignes et de colonnes compris).
    void reload();
    const std::vector<QId> &columns() const { return m_columns; }
    /// Grandeur de la colonne, 0 pour la colonne « + ».
    QId quantityAt(int column) const;
    bool isCalculated(int column) const;
    int dataRows() const;
    ViewId tableId() const { return m_id; }

    std::optional<FillDrag> fill;
    /// Valeurs recopiées affichées pendant le glisser de la poignée.
    std::vector<std::pair<int, std::vector<double>>> preview;

private:
    int computeRows() const;
    Store &m_store;
    ViewId m_id;
    std::vector<QId> m_columns;
    int m_rows = 40;
};

/// En-tête des colonnes : nom (couleur du type), unité et incertitude ; glisser pour tracer.
class TableHeader : public QHeaderView {
    Q_OBJECT

public:
    TableHeader(MainWindow &win, TableModel *model, QWidget *parent = nullptr);
    /// Image des en-têtes des colonnes glissées, côte à côte.
    QPixmap dragPixmap(const std::vector<int> &columns) const;
    /// Largeur d'une colonne changée à la main en ce moment (poignée tirée).
    bool resizing() const { return m_onResizeHandle; }
    /// Colonne dont la poignée de largeur (bord droit) est sous le point, -1 sinon ; la colonne
    /// « + » n'en a pas.
    int handleAt(QPoint pos) const;

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    QSize sizeHint() const override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    MainWindow &m_win;
    TableModel *m_model;
    QPoint m_press;
    int m_pressSection = -1;
    bool m_dragging = false;
    bool m_onResizeHandle = false;
    int m_dropSection = -1;
};

class TableGrid : public QTableView {
    Q_OBJECT

public:
    TableGrid(MainWindow &win, TableModel *model, QWidget *parent = nullptr);

    /// Déplace le curseur après une saisie (Entrée, Tab, flèches).
    void moveCursorBy(int dRow, int dCol);
    /// Rectangle sélectionné : (ligne min, colonne min, ligne max, colonne max).
    QRect selectionBounds() const;
    void copySelection(bool withHeaders);
    void paste(const QString &text);
    void clearSelection();
    void deleteSelectedRows();
    void insertRow();
    void sortBy(int column);
    void selectColumnData(int column);
    /// Clic sur un en-tête : la colonne seule ; avec Ctrl, ajoutée ou retirée ; avec Maj, toutes
    /// les colonnes depuis la colonne courante.
    void selectColumns(int column, Qt::KeyboardModifiers modifiers);
    /// Colonnes de grandeurs qui ont des cases sélectionnées, de gauche à droite ; `whole` : celles
    /// dont toutes les valeurs sont sélectionnées.
    std::vector<int> selectedColumns(bool whole = false) const;
    /// Glisse les grandeurs des colonnes (vers un graphe : la première en abscisse, les suivantes en
    /// ordonnées ; vers un tableur).
    void dragColumns(const std::vector<int> &columns);
    /// Hauteur des lignes, largeur des colonnes et des en-têtes, d'après la police (elles suivent
    /// l'agrandissement de l'interface).
    void fitSizes();
    /// Colonnes réparties sur toute la largeur, chacune au moins à une largeur minimale (au-delà, la
    /// barre de défilement) ; une colonne élargie à la main garde sa largeur. La colonne « + »
    /// reste étroite.
    void distributeColumns();
    /// Colonne remise en largeur automatique (double-clic sur sa poignée).
    void resetColumnWidth(int column);

protected:
    void changeEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    QRect handleRect() const;
    void updateFill(QPoint pos);
    void applyFill();
    /// Toutes les valeurs de la colonne sont sélectionnées (clic sur son en-tête).
    bool wholeColumnSelected(int column) const;

    MainWindow &m_win;
    TableModel *m_model;
    QTimer m_scrollTimer;
    QPoint m_lastPos;
    /// Clic dans une colonne sélectionnée en entier : glissée si la souris bouge, sinon clic
    /// ordinaire (rejoué au relâcher).
    struct CellPress {
        QPointF pos, global;
        Qt::KeyboardModifiers modifiers;
    };
    std::optional<CellPress> m_cellPress;
    /// Largeurs données à la main (à 100 %, par grandeur), et répartition à refaire.
    std::map<QId, double> m_userWidths;
    QTimer m_distribute;
    bool m_fitting = false;
};

class TablePanel : public QWidget {
    Q_OBJECT

public:
    TablePanel(MainWindow &win, ViewId id, QWidget *parent = nullptr);

    TableGrid *grid() const { return m_grid; }

private:
    void refresh();

    MainWindow &m_win;
    ViewId m_id;
    TableModel *m_model;
    TableGrid *m_grid;
    QLabel *m_count;
};
