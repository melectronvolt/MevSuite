#include "app/tableview.h"

#include "app/dialogs.h"
#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/csv.h"
#include "core/format.h"
#include "core/series.h"

#include <QApplication>
#include <QClipboard>
#include <QDrag>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;

namespace {

/// Hauteur d'une ligne du tableur : 22 px à 100 %, et toujours une ligne de texte avec sa marge.
int lineHeight(const QFont &font)
{
    return std::max(theme::px(22), QFontMetrics(font).height() + theme::px(4));
}

/// Colonne « + » au bout du tableur.
int plusWidth()
{
    return theme::px(40);
}

QString plural(size_t n)
{
    return n > 1 ? u"s"_s : QString();
}

/// Nom de colonne libre : x, y, z, puis a, b, c…
QString nextLetter(const Document &doc)
{
    for (const char *n : {"x", "y", "z", "t", "a", "b", "c", "d"}) {
        if (doc.checkName(QString::fromLatin1(n)).isEmpty())
            return QString::fromLatin1(n);
    }
    return doc.uniqueName(u"c"_s);
}

/// Saisie dans une cellule : Entrée valide et descend, Tab passe à droite, flèches haut/bas.
class CellDelegate : public QStyledItemDelegate {
public:
    CellDelegate(TableGrid *grid) : QStyledItemDelegate(grid), m_grid(grid) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *e = new QLineEdit(parent);
        e->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        e->setFrame(false);
        return e;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *e = static_cast<QLineEdit *>(editor);
        // saisie commencée par une touche : le texte tapé remplace la valeur
        if (e->text().isEmpty())
            e->setText(index.data(Qt::EditRole).toString());
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto *editor = qobject_cast<QLineEdit *>(object);
            const auto *k = static_cast<QKeyEvent *>(event);
            int dr = 0, dc = 0;
            switch (k->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Down: dr = 1; break;
            case Qt::Key_Up: dr = -1; break;
            case Qt::Key_Tab: dc = 1; break;
            case Qt::Key_Backtab: dc = -1; break;
            default: return QStyledItemDelegate::eventFilter(object, event);
            }
            if (editor) {
                Q_EMIT const_cast<CellDelegate *>(this)->commitData(editor);
                Q_EMIT const_cast<CellDelegate *>(this)->closeEditor(editor, QAbstractItemDelegate::NoHint);
                m_grid->moveCursorBy(dr, dc);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }

private:
    TableGrid *m_grid;
};

} // namespace

// --- modèle -----------------------------------------------------------------------------------

TableModel::TableModel(Store &store, ViewId id, QObject *parent) : QAbstractTableModel(parent), m_store(store), m_id(id)
{
    if (const TableView *t = m_store.ws.table(id)) {
        for (QId c : t->columns) {
            if (m_store.ws.doc.get(c))
                m_columns.push_back(c);
        }
    }
    m_rows = computeRows();
}

int TableModel::dataRows() const
{
    return static_cast<int>(m_store.ws.doc.rowCount(m_columns));
}

int TableModel::computeRows() const
{
    int n = std::max(dataRows() + 1, 40);
    if (fill)
        n = std::max(n, fill->to + 2);
    return n;
}

void TableModel::reload()
{
    std::vector<QId> cols;
    if (const TableView *t = m_store.ws.table(m_id)) {
        for (QId c : t->columns) {
            if (m_store.ws.doc.get(c))
                cols.push_back(c);
        }
    }
    const int oldCols = static_cast<int>(m_columns.size()) + 1;
    const int newCols = static_cast<int>(cols.size()) + 1;
    if (newCols > oldCols) {
        beginInsertColumns({}, oldCols, newCols - 1);
        m_columns = cols;
        endInsertColumns();
    } else if (newCols < oldCols) {
        beginRemoveColumns({}, newCols, oldCols - 1);
        m_columns = cols;
        endRemoveColumns();
    } else {
        m_columns = cols;
    }
    const int rows = computeRows();
    if (rows > m_rows) {
        beginInsertRows({}, m_rows, rows - 1);
        m_rows = rows;
        endInsertRows();
    } else if (rows < m_rows) {
        beginRemoveRows({}, rows, m_rows - 1);
        m_rows = rows;
        endRemoveRows();
    }
    Q_EMIT headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
    Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

int TableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows;
}

int TableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_columns.size()) + 1;
}

QId TableModel::quantityAt(int column) const
{
    return column >= 0 && static_cast<size_t>(column) < m_columns.size() ? m_columns[column] : 0;
}

bool TableModel::isCalculated(int column) const
{
    const Quantity *q = m_store.ws.doc.get(quantityAt(column));
    return q && q->isCalculated();
}

QVariant TableModel::data(const QModelIndex &index, int role) const
{
    const Quantity *q = m_store.ws.doc.get(quantityAt(index.column()));
    const int r = index.row();
    const theme::TableColors colors = theme::tableColors();
    std::optional<double> recopied;
    if (fill && r > fill->r1 && r <= fill->to) {
        for (const auto &[c, vals] : preview) {
            if (c == index.column() && static_cast<size_t>(r - fill->r1 - 1) < vals.size())
                recopied = vals[r - fill->r1 - 1];
        }
    }
    switch (role) {
    case Qt::DisplayRole: {
        if (!q)
            return {};
        if (recopied)
            return std::isfinite(*recopied) ? formatSig(*recopied, m_store.prefs.sigDigits, m_store.style()) : QString();
        if (r == 0 && q->values.empty() && !q->error.isEmpty())
            return u"⚠ "_s + q->error;
        const double v = q->value(static_cast<size_t>(r));
        return std::isnan(v) ? QString() : formatSig(v, m_store.prefs.sigDigits, m_store.style());
    }
    case Qt::EditRole: {
        if (!q)
            return {};
        const double v = q->value(static_cast<size_t>(r));
        return std::isfinite(v) ? formatSig(v, 15, m_store.style()) : QString();
    }
    case Qt::TextAlignmentRole:
        if (q && r == 0 && q->values.empty() && !q->error.isEmpty())
            return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    case Qt::BackgroundRole:
        if (recopied)
            return colors.selection;
        if (q && q->isCalculated())
            return colors.calculated;
        return {};
    case Qt::ForegroundRole:
        if (recopied)
            return colors.weak;
        if (q && r == 0 && q->values.empty() && !q->error.isEmpty())
            return colors.error;
        if (q && static_cast<size_t>(r) >= q->values.size())
            return colors.weak;
        return colors.text;
    case Qt::ToolTipRole:
        if (q && !q->error.isEmpty())
            return u"Erreur : "_s + q->error;
        return {};
    default:
        return {};
    }
}

bool TableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;
    const QId id = quantityAt(index.column());
    if (!id)
        return false;
    const QString t = value.toString().trimmed();
    const auto v = parseNumber(t);
    if (!v && !t.isEmpty()) {
        m_store.message(u"« %1 » n'est pas un nombre"_s.arg(t), icons::Status::Error);
        return false;
    }
    QString err;
    m_store.edit([&](Workspace &ws) { err = ws.doc.setValue(id, static_cast<size_t>(index.row()), v); });
    if (!err.isEmpty())
        m_store.message(err, icons::Status::Error);
    return err.isEmpty();
}

Qt::ItemFlags TableModel::flags(const QModelIndex &index) const
{
    if (!quantityAt(index.column()))
        return Qt::ItemIsEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!isCalculated(index.column()))
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal)
        return quantityAt(section) ? u"Glisser vers un graphe pour tracer cette grandeur ; Ctrl + clic sur d'autres en-têtes pour "
                                     u"glisser plusieurs colonnes ensemble (la première donne les abscisses).\n"
                                     u"Clic : sélectionner la colonne ; double-clic : nom, unité, formule."_s
                                   : u"Nouvelle grandeur"_s;
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    const Quantity *q = m_store.ws.doc.get(quantityAt(section));
    return q ? q->name : u"+"_s;
}

// --- en-tête ----------------------------------------------------------------------------------

TableHeader::TableHeader(MainWindow &win, TableModel *model, QWidget *parent) : QHeaderView(Qt::Horizontal, parent), m_win(win), m_model(model)
{
    setSectionsClickable(true);
    setHighlightSections(false);
    setAcceptDrops(true);
    setMouseTracking(true);
    setDefaultSectionSize(theme::px(96));
    setMinimumSectionSize(theme::px(48));
    setSectionResizeMode(QHeaderView::Interactive);
}

QSize TableHeader::sizeHint() const
{
    // deux lignes : le nom, puis l'unité et l'incertitude (police réduite)
    QSize s = QHeaderView::sizeHint();
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.86);
    s.setHeight(std::max(theme::px(42), fontMetrics().height() + QFontMetrics(small).height() + theme::px(10)));
    return s;
}

void TableHeader::paintSection(QPainter *p, const QRect &rect, int logical) const
{
    const theme::TableColors colors = theme::tableColors();
    const Store &store = m_win.store();
    const Quantity *q = store.ws.doc.get(m_model->quantityAt(logical));
    p->save();
    p->fillRect(rect, q && q->isCalculated() ? colors.calculated : colors.header);
    p->setPen(colors.grid);
    p->drawLine(rect.topRight(), rect.bottomRight());
    p->drawLine(rect.bottomLeft(), rect.bottomRight());
    if (logical == m_dropSection) {
        p->setPen(QPen(colors.cursor, 2));
        p->drawRect(rect.adjusted(1, 1, -2, -2));
    }
    if (!q) {
        const int side = theme::px(16);
        icons::get("plus").paint(p, QRect(rect.left() + theme::px(8), rect.center().y() - side / 2, side, side));
        p->restore();
        return;
    }
    const bool isUnc = store.ws.doc.uncertaintyOwner(q->id) != nullptr;
    // nom de la colonne : demi-gras, taille normale
    QFont f = font();
    f.setWeight(QFont::DemiBold);
    p->setFont(f);
    p->setPen(q->error.isEmpty() ? theme::kindColor(q->isCalculated(), isUnc) : colors.error);
    const QString title = q->isCalculated() ? u"ƒ "_s + q->name : q->name;
    const int m = theme::px(3);
    const QRect top(rect.left() + m, rect.top() + m, rect.width() - 2 * m, rect.height() / 2);
    p->drawText(top, Qt::AlignHCenter | Qt::AlignBottom, QFontMetrics(f).elidedText(title, Qt::ElideRight, top.width()));
    QString sub = prettyUnit(q->unit);
    if (q->uncertainty)
        sub = (sub.isEmpty() ? QString() : sub + u" "_s) + u"± "_s + store.ws.doc.name(*q->uncertainty);
    QFont sf = font();
    sf.setPointSizeF(sf.pointSizeF() * 0.86);
    p->setFont(sf);
    p->setPen(colors.weak);
    const QRect bottom(rect.left() + m, rect.top() + rect.height() / 2 + 1, rect.width() - 2 * m, rect.height() / 2 - 4);
    p->drawText(bottom, Qt::AlignHCenter | Qt::AlignTop, QFontMetrics(sf).elidedText(sub, Qt::ElideRight, bottom.width()));
    p->restore();
}

int TableHeader::handleAt(QPoint pos) const
{
    // même marge que QHeaderView, qui prend alors la main ; poignée de la colonne de gauche quand
    // le point est juste au début d'une colonne
    const int grip = style()->pixelMetric(QStyle::PM_HeaderGripMargin, nullptr, this);
    const int s = logicalIndexAt(pos);
    if (s < 0)
        return -1;
    const int last = m_model->columnCount() - 1;
    const int left = sectionViewportPosition(s), right = left + sectionSize(s);
    if (std::abs(pos.x() - right) <= grip)
        return s == last ? -1 : s;
    if (std::abs(pos.x() - left) <= grip) {
        const int v = visualIndex(s);
        return v > 0 ? logicalIndex(v - 1) : -1;
    }
    return -1;
}

void TableHeader::mousePressEvent(QMouseEvent *e)
{
    m_press = e->pos();
    m_pressSection = logicalIndexAt(e->pos());
    m_dragging = false;
    // poignée de largeur de colonne (au bord de la colonne « + » aussi) : laissée à QHeaderView
    m_onResizeHandle = handleAt(e->pos()) >= 0;
    if (m_onResizeHandle || e->button() != Qt::LeftButton)
        QHeaderView::mousePressEvent(e);
}

QPixmap TableHeader::dragPixmap(const std::vector<int> &columns) const
{
    int width = 0;
    for (int c : columns)
        width += sectionSize(c);
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(QSize(std::max(1, width), height()) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    int x = 0;
    for (int c : columns) {
        paintSection(&p, QRect(x, 0, sectionSize(c), height()), c);
        x += sectionSize(c);
    }
    return pm;
}

void TableHeader::mouseMoveEvent(QMouseEvent *e)
{
    if (m_onResizeHandle || !(e->buttons() & Qt::LeftButton)) {
        QHeaderView::mouseMoveEvent(e);
        // survol : main ouverte sur l'en-tête d'une grandeur (elle se glisse), flèches de Qt sur
        // la poignée de largeur
        const int s = logicalIndexAt(e->pos());
        const bool handle = handleAt(e->pos()) >= 0;
        if (!(e->buttons() & Qt::LeftButton) && !handle && m_model->quantityAt(s))
            viewport()->setCursor(Qt::OpenHandCursor);
        else
            viewport()->unsetCursor();
        return;
    }
    const QId id = m_model->quantityAt(m_pressSection);
    if (!m_dragging && id && (e->pos() - m_press).manhattanLength() > QApplication::startDragDistance()) {
        m_dragging = true;
        // en-tête d'une colonne sélectionnée : toutes les colonnes sélectionnées partent ensemble
        auto *grid = qobject_cast<TableGrid *>(parentWidget());
        std::vector<int> columns = grid ? grid->selectedColumns() : std::vector<int>{};
        if (std::find(columns.begin(), columns.end(), m_pressSection) == columns.end())
            columns = {m_pressSection};
        if (grid)
            grid->dragColumns(columns);
    }
}

void TableHeader::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_onResizeHandle) {
        QHeaderView::mouseReleaseEvent(e);
        m_onResizeHandle = false;
        return;
    }
    if (!m_dragging && e->button() == Qt::LeftButton && m_pressSection >= 0) {
        auto *grid = qobject_cast<TableGrid *>(parentWidget());
        if (!m_model->quantityAt(m_pressSection)) {
            dialogs::newColumn(m_win, m_model->tableId());
        } else if (grid) {
            grid->selectColumns(m_pressSection, e->modifiers());
        }
    }
    m_dragging = false;
    m_pressSection = -1;
}

void TableHeader::mouseDoubleClickEvent(QMouseEvent *e)
{
    // double-clic sur une poignée : la colonne reprend sa largeur automatique
    if (const int h = handleAt(e->pos()); h >= 0) {
        if (auto *grid = qobject_cast<TableGrid *>(parentWidget()))
            grid->resetColumnWidth(h);
        return;
    }
    const int s = logicalIndexAt(e->pos());
    if (const QId id = m_model->quantityAt(s))
        dialogs::editQuantity(m_win, id);
}

void TableHeader::contextMenuEvent(QContextMenuEvent *e)
{
    const int c = logicalIndexAt(e->pos());
    const QId id = m_model->quantityAt(c);
    const Quantity *q = m_win.store().ws.doc.get(id);
    auto *grid = qobject_cast<TableGrid *>(parentWidget());
    if (!q || !grid)
        return;
    MainWindow &win = m_win;
    const ViewId table = m_model->tableId();
    QMenu menu(this);
    auto *title = menu.addAction(q->name);
    title->setEnabled(false);
    menu.addSeparator();
    menu.addAction(icons::get("pencil"), u"Nom, unité, formule…"_s, this, [&win, id] { dialogs::editQuantity(win, id); });
    if (!q->isCalculated())
        menu.addAction(icons::get("numbers"), u"Remplir avec une suite de valeurs…"_s, this, [&win, id] { dialogs::fill(win, id); });
    menu.addAction(icons::get("plusminus"), u"Incertitude…"_s, this, [&win, id] { dialogs::newUncertainty(win, id); });
    menu.addAction(icons::get("sort"), u"Trier le tableur selon cette colonne"_s, this, [grid, c] { grid->sortBy(c); });
    menu.addAction(icons::get("stats"), u"Statistiques"_s, this, [&win, id] { win.openStatistics(id); });
    if (c > 0) {
        const QId first = m_model->quantityAt(0);
        menu.addAction(icons::get("chart"), u"Tracer en fonction de la 1re colonne"_s, this, [&win, first, id] { win.newGraph(std::make_pair(first, id)); });
    }
    menu.addAction(icons::get("copy"), u"Copier la colonne avec son en-tête"_s, this, [grid, c] {
        grid->selectColumnData(c);
        grid->copySelection(true);
    });
    menu.addSeparator();
    menu.addAction(icons::get("eye-off"), u"Retirer du tableur"_s, this, [&win, table, id] {
        win.store().edit([&](Workspace &ws) {
            if (TableView *t = ws.tableMut(table))
                std::erase(t->columns, id);
        });
    })->setToolTip(u"La grandeur reste dans le document"_s);
    menu.addAction(icons::get("trash"), u"Supprimer la grandeur…"_s, this, [&win, id] { dialogs::confirmDelete(win, id); });
    menu.exec(e->globalPos());
}

void TableHeader::dragEnterEvent(QDragEnterEvent *e)
{
    if (dnd::quantityFrom(e->mimeData()))
        e->acceptProposedAction();
}

void TableHeader::dragMoveEvent(QDragMoveEvent *e)
{
    if (!dnd::quantityFrom(e->mimeData()))
        return;
    e->acceptProposedAction();
    const int s = logicalIndexAt(e->position().toPoint());
    if (s != m_dropSection) {
        m_dropSection = s;
        viewport()->update();
    }
}

void TableHeader::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropSection = -1;
    viewport()->update();
}

void TableHeader::dropEvent(QDropEvent *e)
{
    const std::vector<QId> ids = dnd::quantitiesFrom(e->mimeData());
    int at = logicalIndexAt(e->position().toPoint());
    m_dropSection = -1;
    viewport()->update();
    if (ids.empty())
        return;
    if (at < 0)
        at = static_cast<int>(m_model->columns().size());
    e->acceptProposedAction();
    const ViewId table = m_model->tableId();
    // colonnes posées (ou déplacées) ensemble, dans leur ordre, devant la colonne visée
    m_win.store().edit([&](Workspace &ws) {
        TableView *t = ws.tableMut(table);
        if (!t)
            return;
        auto pos = static_cast<size_t>(std::clamp(at, 0, static_cast<int>(t->columns.size())));
        for (QId q : ids) {
            auto it = std::find(t->columns.begin(), t->columns.end(), q);
            if (it == t->columns.end())
                continue;
            const auto from = static_cast<size_t>(it - t->columns.begin());
            t->columns.erase(it);
            if (from < pos)
                --pos;
        }
        t->columns.insert(t->columns.begin() + static_cast<std::ptrdiff_t>(std::min(pos, t->columns.size())), ids.begin(), ids.end());
    });
}

// --- grille -----------------------------------------------------------------------------------

TableGrid::TableGrid(MainWindow &win, TableModel *model, QWidget *parent) : QTableView(parent), m_win(win), m_model(model)
{
    setModel(model);
    setHorizontalHeader(new TableHeader(win, model, this));
    m_distribute.setSingleShot(true);
    connect(&m_distribute, &QTimer::timeout, this, &TableGrid::distributeColumns);
    // largeur changée à la main : gardée (à 100 %), les autres colonnes se répartissent le reste
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int c, int, int width) {
        auto *header = qobject_cast<TableHeader *>(horizontalHeader());
        if (m_fitting || !header || !header->resizing() || c >= m_model->columnCount() - 1)
            return;
        if (const QId q = m_model->quantityAt(c)) {
            m_userWidths[q] = width / theme::zoom();
            m_distribute.start(120);
        }
    });
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    setItemDelegate(new CellDelegate(this));
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    setSelectionMode(QAbstractItemView::ContiguousSelection);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setWordWrap(false);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setMouseTracking(true);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    fitSizes();
    m_scrollTimer.setInterval(40);
    connect(&m_scrollTimer, &QTimer::timeout, this, [this] {
        verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        updateFill(m_lastPos);
    });
}

QRect TableGrid::selectionBounds() const
{
    const QModelIndexList sel = selectionModel()->selectedIndexes();
    if (sel.isEmpty()) {
        const QModelIndex c = currentIndex();
        return c.isValid() ? QRect(c.column(), c.row(), 1, 1) : QRect();
    }
    int r0 = INT_MAX, c0 = INT_MAX, r1 = -1, c1 = -1;
    for (const QModelIndex &i : sel) {
        r0 = std::min(r0, i.row());
        r1 = std::max(r1, i.row());
        c0 = std::min(c0, i.column());
        c1 = std::max(c1, i.column());
    }
    return QRect(QPoint(c0, r0), QPoint(c1, r1));
}

void TableGrid::moveCursorBy(int dRow, int dCol)
{
    const QModelIndex c = currentIndex();
    if (!c.isValid())
        return;
    const int r = std::clamp(c.row() + dRow, 0, m_model->rowCount() - 1);
    const int col = std::clamp(c.column() + dCol, 0, std::max(static_cast<int>(m_model->columns().size()) - 1, 0));
    const QModelIndex next = m_model->index(r, col);
    setCurrentIndex(next);
    scrollTo(next);
}

void TableGrid::selectColumnData(int column)
{
    selectColumns(column, Qt::NoModifier);
}

void TableGrid::selectColumns(int column, Qt::KeyboardModifiers modifiers)
{
    const int last = std::max(m_model->dataRows() - 1, 0);
    auto columnRange = [&](int from, int to) { return QItemSelection(m_model->index(0, from), m_model->index(last, to)); };
    const int anchor = currentIndex().isValid() ? currentIndex().column() : column;
    if (modifiers & Qt::ShiftModifier) {
        // de la colonne courante à celle-ci
        const int c0 = std::min(anchor, column), c1 = std::max(anchor, column);
        selectionModel()->select(columnRange(c0, c1), (modifiers & Qt::ControlModifier) ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect);
    } else if (modifiers & Qt::ControlModifier) {
        // ajoutée, ou retirée si elle l'était
        selectionModel()->select(columnRange(column, column), wholeColumnSelected(column) ? QItemSelectionModel::Deselect : QItemSelectionModel::Select);
    } else {
        selectionModel()->select(columnRange(column, column), QItemSelectionModel::ClearAndSelect);
    }
    selectionModel()->setCurrentIndex(m_model->index(0, column), QItemSelectionModel::NoUpdate);
    setFocus();
}

bool TableGrid::wholeColumnSelected(int column) const
{
    const int rows = m_model->dataRows();
    if (rows <= 0 || !m_model->quantityAt(column))
        return false;
    for (int r = 0; r < rows; ++r)
        if (!selectionModel()->isSelected(m_model->index(r, column)))
            return false;
    return true;
}

std::vector<int> TableGrid::selectedColumns(bool whole) const
{
    std::vector<int> out;
    for (const QModelIndex &i : selectionModel()->selectedIndexes())
        if (m_model->quantityAt(i.column()) && std::find(out.begin(), out.end(), i.column()) == out.end())
            out.push_back(i.column());
    if (whole)
        std::erase_if(out, [this](int c) { return !wholeColumnSelected(c); });
    // dans l'ordre des colonnes affichées
    std::sort(out.begin(), out.end(), [this](int a, int b) { return horizontalHeader()->visualIndex(a) < horizontalHeader()->visualIndex(b); });
    return out;
}

void TableGrid::dragColumns(const std::vector<int> &columns)
{
    std::vector<QId> ids;
    for (int c : columns)
        if (const QId q = m_model->quantityAt(c))
            ids.push_back(q);
    if (ids.empty())
        return;
    auto *drag = new QDrag(this);
    drag->setMimeData(dnd::quantitiesMime(ids));
    if (auto *header = qobject_cast<TableHeader *>(horizontalHeader())) {
        const QPixmap pm = header->dragPixmap(columns);
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(static_cast<int>(pm.width() / pm.devicePixelRatio() / 2), static_cast<int>(pm.height() / pm.devicePixelRatio() / 2)));
    }
    if (ids.size() > 1)
        m_win.store().message(u"Sur un graphe, la première colonne donne les abscisses, les suivantes les ordonnées"_s);
    drag->exec(Qt::CopyAction | Qt::MoveAction);
}

void TableGrid::copySelection(bool withHeaders)
{
    const QRect b = selectionBounds();
    if (b.isNull())
        return;
    const Store &store = m_win.store();
    const int c1 = std::min(b.right(), static_cast<int>(m_model->columns().size()) - 1);
    QString text;
    if (withHeaders) {
        QStringList h;
        for (int c = b.left(); c <= c1; ++c) {
            const Quantity *q = store.ws.doc.get(m_model->quantityAt(c));
            h << (q ? (q->unit.isEmpty() ? q->name : u"%1 (%2)"_s.arg(q->name, q->unit)) : QString());
        }
        text += h.join(u'\t') + u'\n';
    }
    for (int r = b.top(); r <= b.bottom(); ++r) {
        QStringList row;
        for (int c = b.left(); c <= c1; ++c) {
            const Quantity *q = store.ws.doc.get(m_model->quantityAt(c));
            const double v = q ? q->value(static_cast<size_t>(r)) : NAN;
            row << (std::isfinite(v) ? shortestStyled(v, store.style()) : QString());
        }
        text += row.join(u'\t') + u'\n';
    }
    QApplication::clipboard()->setText(text);
    const int n = b.bottom() - b.top() + 1;
    m_win.store().message(u"%1 ligne%2 copiée%2"_s.arg(n).arg(plural(n)));
}

void TableGrid::clearSelection()
{
    const QRect b = selectionBounds();
    if (b.isNull())
        return;
    m_win.store().edit([&](Workspace &ws) {
        for (int c = b.left(); c <= b.right(); ++c) {
            const Quantity *q = ws.doc.get(m_model->quantityAt(c));
            if (!q || q->isCalculated())
                continue;
            std::vector<double> vals = q->values;
            for (int r = b.top(); r <= b.bottom(); ++r) {
                if (static_cast<size_t>(r) < vals.size())
                    vals[r] = NAN;
            }
            ws.doc.setValues(q->id, vals);
        }
    });
}

void TableGrid::deleteSelectedRows()
{
    const QRect b = selectionBounds();
    if (b.isNull())
        return;
    std::vector<QId> ids;
    for (QId id : m_model->columns()) {
        if (const Quantity *q = m_win.store().ws.doc.get(id); q && !q->isCalculated())
            ids.push_back(id);
    }
    std::vector<size_t> rows;
    for (int r = b.top(); r <= b.bottom(); ++r)
        rows.push_back(static_cast<size_t>(r));
    m_win.store().edit([&](Workspace &ws) { ws.doc.deleteRows(ids, rows); });
    setCurrentIndex(m_model->index(b.top(), b.left()));
}

void TableGrid::insertRow()
{
    const QModelIndex c = currentIndex();
    std::vector<QId> ids;
    for (QId id : m_model->columns()) {
        if (const Quantity *q = m_win.store().ws.doc.get(id); q && !q->isCalculated())
            ids.push_back(id);
    }
    const size_t row = c.isValid() ? static_cast<size_t>(c.row()) : 0;
    m_win.store().edit([&](Workspace &ws) { ws.doc.insertRow(ids, row); });
}

void TableGrid::sortBy(int column)
{
    Store &store = m_win.store();
    const Quantity *key = store.ws.doc.get(m_model->quantityAt(column));
    if (!key)
        return;
    size_t n = 0;
    for (QId id : m_model->columns()) {
        if (const Quantity *q = store.ws.doc.get(id); q && !q->isCalculated())
            n = std::max(n, q->values.size());
    }
    const std::vector<double> kv = key->values;
    auto keyOf = [&](size_t i) { return i < kv.size() ? kv[i] : NAN; };
    std::vector<size_t> perm(n);
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::stable_sort(perm.begin(), perm.end(), [&](size_t a, size_t b) {
        const double x = keyOf(a), y = keyOf(b);
        if (std::isnan(x))
            return false;
        if (std::isnan(y))
            return true;
        return x < y;
    });
    if (key->isCalculated())
        store.message(u"Tri selon une grandeur calculée : les mesures sont réordonnées selon ses valeurs actuelles"_s, icons::Status::Info);
    store.edit([&](Workspace &ws) {
        for (QId id : m_model->columns()) {
            const Quantity *q = ws.doc.get(id);
            if (!q || q->isCalculated())
                continue;
            std::vector<double> vals;
            for (size_t i : perm)
                vals.push_back(q->value(i));
            ws.doc.setValues(id, vals);
        }
    });
}

void TableGrid::paste(const QString &text)
{
    const auto cells = csv::splitCells(text);
    if (cells.empty())
        return;
    Store &store = m_win.store();
    const bool hasHeader = std::any_of(cells.front().begin(), cells.front().end(), [](const QString &c) { return !c.isEmpty() && !parseNumber(c); });
    std::optional<csv::Parsed> parsed;
    if (hasHeader) {
        QString err;
        parsed = csv::parse(text, &err);
    }
    std::vector<csv::Column> blocks;
    if (parsed) {
        blocks = parsed->columns;
    } else {
        qsizetype w = 0;
        for (const auto &r : cells)
            w = std::max(w, r.size());
        for (qsizetype j = 0; j < w; ++j) {
            csv::Column c;
            for (const auto &r : cells)
                c.values.push_back(parseNumber(r.value(j)).value_or(NAN));
            blocks.push_back(c);
        }
    }
    const QModelIndex cur = currentIndex();
    const int row0 = cur.isValid() ? cur.row() : 0;
    const int col0 = cur.isValid() ? cur.column() : 0;
    const ViewId table = m_model->tableId();
    const std::vector<QId> cols = m_model->columns();
    int created = 0, written = 0;
    QStringList errors;
    store.edit([&](Workspace &ws) {
        for (size_t j = 0; j < blocks.size(); ++j) {
            const auto &[name, unit, vals] = blocks[j];
            const size_t target = static_cast<size_t>(col0) + j;
            if (target < cols.size()) {
                const Quantity *q = ws.doc.get(cols[target]);
                if (!q)
                    continue;
                if (q->isCalculated()) {
                    errors << u"« %1 » est calculée : colonne ignorée"_s.arg(q->name);
                    continue;
                }
                // colonne encore vide : elle prend le nom et l'unité de l'en-tête collé
                if (row0 == 0 && !name.isEmpty() && std::all_of(q->values.begin(), q->values.end(), [](double v) { return std::isnan(v); })) {
                    if (name != q->name && ws.doc.checkName(name, q->id).isEmpty())
                        ws.renameQuantity(q->id, name);
                    if (!unit.isEmpty())
                        ws.doc.setUnit(q->id, unit);
                }
                q = ws.doc.get(cols[target]);
                std::vector<double> values = q->values;
                if (values.size() < static_cast<size_t>(row0) + vals.size())
                    values.resize(static_cast<size_t>(row0) + vals.size(), NAN);
                for (size_t i = 0; i < vals.size(); ++i)
                    values[static_cast<size_t>(row0) + i] = vals[i];
                ws.doc.setValues(q->id, values);
                ++written;
            } else {
                const QString base = name.isEmpty() ? nextLetter(ws.doc) : ws.doc.uniqueName(name);
                std::vector<double> values(static_cast<size_t>(row0), NAN);
                values.insert(values.end(), vals.begin(), vals.end());
                if (auto id = ws.doc.addExperimental(base, unit, values)) {
                    if (TableView *t = ws.tableMut(table))
                        t->columns.push_back(*id);
                    ++created;
                }
            }
        }
    });
    const int n = written + created;
    QString msg = u"%1 colonne%2 collée%2"_s.arg(n).arg(plural(n));
    if (created > 0)
        msg += u" dont %1 nouvelle%2"_s.arg(created).arg(plural(created));
    // un seul message dans la barre d'état : les colonnes ignorées à la suite
    if (!errors.isEmpty())
        store.message(msg + u" ; "_s + errors.join(u" ; "_s), icons::Status::Warning);
    else
        store.message(msg);
}

void TableGrid::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::Copy)) {
        copySelection(false);
        return;
    }
    if (e->matches(QKeySequence::Cut)) {
        copySelection(false);
        clearSelection();
        return;
    }
    if (e->matches(QKeySequence::Paste)) {
        paste(QApplication::clipboard()->text());
        return;
    }
    if (e->matches(QKeySequence::SelectAll)) {
        const int last = std::max(m_model->dataRows() - 1, 0);
        const int cols = static_cast<int>(m_model->columns().size());
        if (cols > 0)
            selectionModel()->select(QItemSelection(m_model->index(0, 0), m_model->index(last, cols - 1)), QItemSelectionModel::ClearAndSelect);
        return;
    }
    if (state() != QAbstractItemView::EditingState) {
        switch (e->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            clearSelection();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (currentIndex().isValid() && (m_model->flags(currentIndex()) & Qt::ItemIsEditable))
                edit(currentIndex());
            else
                moveCursorBy(1, 0);
            return;
        case Qt::Key_Escape:
            selectionModel()->select(currentIndex(), QItemSelectionModel::ClearAndSelect);
            return;
        default:
            break;
        }
    }
    QTableView::keyPressEvent(e);
}

QRect TableGrid::handleRect() const
{
    if (!hasFocus() || state() == QAbstractItemView::EditingState || m_model->fill)
        return {};
    const QRect b = selectionBounds();
    if (b.isNull() || b.right() >= static_cast<int>(m_model->columns().size()))
        return {};
    bool anyExperimental = false;
    for (int c = b.left(); c <= b.right(); ++c)
        anyExperimental = anyExperimental || !m_model->isCalculated(c);
    if (!anyExperimental)
        return {};
    const QRect cell = visualRect(m_model->index(b.bottom(), b.right()));
    if (!cell.isValid())
        return {};
    // sur le bord inférieur, à l'écart de la poignée de largeur de colonne
    const int side = theme::px(7);
    return QRect(cell.right() - 2 * side, cell.bottom() - (side - 1) / 2, side, side);
}

void TableGrid::fitSizes()
{
    const int row = lineHeight(font());
    verticalHeader()->setMinimumSectionSize(row);
    verticalHeader()->setDefaultSectionSize(row);
    verticalHeader()->setMinimumWidth(theme::px(44));
    horizontalHeader()->setMinimumSectionSize(theme::px(48));
    horizontalHeader()->setDefaultSectionSize(theme::px(96));
    distributeColumns();
    horizontalHeader()->updateGeometry();
}

void TableGrid::distributeColumns()
{
    m_distribute.stop();
    auto *header = qobject_cast<TableHeader *>(horizontalHeader());
    // pendant qu'une poignée est tirée, les autres colonnes attendent
    if (header && header->resizing() && (QApplication::mouseButtons() & Qt::LeftButton)) {
        m_distribute.start(120);
        return;
    }
    QHeaderView *h = horizontalHeader();
    const int last = m_model->columnCount() - 1;
    if (last < 0)
        return;
    m_fitting = true;
    const int plus = std::max(plusWidth(), h->minimumSectionSize());
    const int least = theme::px(90);
    const int avail = std::max(0, viewport()->width() - plus);
    std::vector<int> widths(static_cast<size_t>(last), 0);
    int used = 0, free = 0;
    for (int c = 0; c < last; ++c) {
        if (const auto it = m_userWidths.find(m_model->quantityAt(c)); it != m_userWidths.end()) {
            widths[static_cast<size_t>(c)] = std::max(h->minimumSectionSize(), static_cast<int>(std::lround(it->second * theme::zoom())));
            used += widths[static_cast<size_t>(c)];
        } else {
            ++free;
        }
    }
    // les autres se partagent le reste de la largeur, au moins à leur largeur minimale
    const int share = free > 0 ? std::max(least, (avail - used) / free) : 0;
    int extra = free > 0 && share * free < avail - used ? avail - used - share * free : 0;
    for (int c = 0; c < last; ++c) {
        int &w = widths[static_cast<size_t>(c)];
        if (w == 0) {
            w = share + (extra > 0 ? 1 : 0);
            extra = std::max(0, extra - 1);
        }
        if (h->sectionSize(c) != w)
            h->resizeSection(c, w);
    }
    h->setSectionResizeMode(last, QHeaderView::Fixed);
    h->resizeSection(last, plus);
    m_fitting = false;
}

void TableGrid::resetColumnWidth(int column)
{
    m_userWidths.erase(m_model->quantityAt(column));
    distributeColumns();
}

void TableGrid::resizeEvent(QResizeEvent *e)
{
    QTableView::resizeEvent(e);
    // largeur de la zone connue une fois la barre de défilement placée
    m_distribute.start(0);
}

void TableGrid::changeEvent(QEvent *e)
{
    QTableView::changeEvent(e);
    if (e->type() == QEvent::FontChange)
        fitSizes();
}

void TableGrid::paintEvent(QPaintEvent *e)
{
    QTableView::paintEvent(e);
    QPainter p(viewport());
    const theme::TableColors colors = theme::tableColors();
    // curseur bien visible
    if (hasFocus() && currentIndex().isValid() && state() != QAbstractItemView::EditingState) {
        const QRect r = visualRect(currentIndex());
        const int w = theme::px(2);
        p.setPen(QPen(colors.cursor, w));
        p.drawRect(r.adjusted(w / 2, w / 2, -w / 2, -w / 2));
    }
    // aperçu de la recopie
    if (m_model->fill) {
        const FillDrag &f = *m_model->fill;
        const QRect a = visualRect(m_model->index(f.r0, f.c0));
        const QRect b = visualRect(m_model->index(f.to, f.c1));
        p.setPen(QPen(colors.cursor, 1, Qt::DashLine));
        p.drawRect(a.united(b).adjusted(0, 0, -1, -1));
    }
    const QRect h = handleRect();
    if (h.isValid()) {
        p.fillRect(h, colors.cursor);
        p.setPen(palette().color(QPalette::Base));
        p.drawRect(h.adjusted(-1, -1, 0, 0));
    }
}

void TableGrid::mousePressEvent(QMouseEvent *e)
{
    const QRect h = handleRect();
    if (e->button() == Qt::LeftButton && h.isValid() && h.adjusted(-theme::px(3), -theme::px(3), theme::px(3), theme::px(3)).contains(e->pos())) {
        const QRect b = selectionBounds();
        const int c1 = std::min(b.right(), static_cast<int>(m_model->columns().size()) - 1);
        m_model->fill = FillDrag{b.top(), b.bottom(), b.left(), c1, b.bottom()};
        m_lastPos = e->pos();
        viewport()->update();
        return;
    }
    // dans une colonne sélectionnée en entier : elle se glisse vers un graphe (le clic ordinaire
    // attend le relâcher)
    const QModelIndex at = indexAt(e->pos());
    if (e->button() == Qt::LeftButton && e->modifiers() == Qt::NoModifier && at.isValid() && wholeColumnSelected(at.column())
        && selectionModel()->isSelected(at)) {
        m_cellPress = CellPress{e->position(), e->globalPosition(), e->modifiers()};
        return;
    }
    QTableView::mousePressEvent(e);
}

void TableGrid::updateFill(QPoint pos)
{
    if (!m_model->fill)
        return;
    FillDrag &f = *m_model->fill;
    int row = rowAt(pos.y());
    if (pos.y() > viewport()->height() - 2) {
        row = rowAt(viewport()->height() - 2) + 1;
        if (!m_scrollTimer.isActive())
            m_scrollTimer.start();
    } else {
        m_scrollTimer.stop();
    }
    if (row < 0)
        row = f.r1;
    f.to = std::max(row, f.r1);
    // aperçu des valeurs recopiées
    m_model->preview.clear();
    const Store &store = m_win.store();
    if (f.to > f.r1) {
        for (int c = f.c0; c <= f.c1; ++c) {
            const Quantity *q = store.ws.doc.get(m_model->quantityAt(c));
            if (!q || q->isCalculated())
                continue;
            std::vector<double> sel;
            for (int r = f.r0; r <= f.r1; ++r)
                sel.push_back(q->value(static_cast<size_t>(r)));
            m_model->preview.emplace_back(c, series::extend(sel, static_cast<size_t>(f.to - f.r1)));
        }
    }
    m_model->reload();
    viewport()->update();
}

void TableGrid::mouseMoveEvent(QMouseEvent *e)
{
    if (m_model->fill) {
        m_lastPos = e->pos();
        updateFill(e->pos());
        return;
    }
    if (m_cellPress) {
        if ((e->buttons() & Qt::LeftButton) && (e->position() - m_cellPress->pos).manhattanLength() > QApplication::startDragDistance()) {
            m_cellPress.reset();
            dragColumns(selectedColumns(true));
        }
        return;
    }
    // poignée de recopie : croix ; colonne sélectionnée en entier : main ouverte (elle se glisse)
    const QRect h = handleRect();
    const QModelIndex at = indexAt(e->pos());
    if (h.isValid() && h.adjusted(-theme::px(3), -theme::px(3), theme::px(3), theme::px(3)).contains(e->pos()))
        viewport()->setCursor(Qt::CrossCursor);
    else if (!e->buttons() && at.isValid() && selectionModel()->isSelected(at) && wholeColumnSelected(at.column()))
        viewport()->setCursor(Qt::OpenHandCursor);
    else
        viewport()->setCursor(Qt::ArrowCursor);
    QTableView::mouseMoveEvent(e);
}

void TableGrid::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_model->fill) {
        m_scrollTimer.stop();
        applyFill();
        return;
    }
    if (m_cellPress) {
        // pas glissée : un clic ordinaire dans la case
        QMouseEvent press(QEvent::MouseButtonPress, m_cellPress->pos, m_cellPress->global, Qt::LeftButton, Qt::LeftButton, m_cellPress->modifiers);
        m_cellPress.reset();
        QTableView::mousePressEvent(&press);
    }
    QTableView::mouseReleaseEvent(e);
}

void TableGrid::applyFill()
{
    const FillDrag f = *m_model->fill;
    m_model->fill.reset();
    const auto preview = std::move(m_model->preview);
    m_model->preview.clear();
    if (f.to > f.r1) {
        m_win.store().edit([&](Workspace &ws) {
            for (const auto &[c, vals] : preview) {
                const Quantity *q = ws.doc.get(m_model->quantityAt(c));
                if (!q)
                    continue;
                std::vector<double> values = q->values;
                if (values.size() <= static_cast<size_t>(f.to))
                    values.resize(static_cast<size_t>(f.to) + 1, NAN);
                for (size_t k = 0; k < vals.size(); ++k)
                    values[static_cast<size_t>(f.r1) + 1 + k] = vals[k];
                ws.doc.setValues(q->id, values);
            }
        });
        selectionModel()->select(QItemSelection(m_model->index(f.r0, f.c0), m_model->index(f.to, f.c1)), QItemSelectionModel::ClearAndSelect);
        selectionModel()->setCurrentIndex(m_model->index(f.to, f.c1), QItemSelectionModel::NoUpdate);
    }
    m_model->reload();
    viewport()->update();
}

void TableGrid::contextMenuEvent(QContextMenuEvent *e)
{
    const QModelIndex idx = indexAt(e->pos());
    if (idx.isValid() && !selectionModel()->isSelected(idx))
        setCurrentIndex(idx);
    QMenu menu(this);
    menu.addAction(icons::get("copy"), u"Copier"_s, QKeySequence::Copy, this, [this] { copySelection(false); });
    menu.addAction(icons::get("copy"), u"Copier avec les en-têtes"_s, this, [this] { copySelection(true); });
    menu.addAction(icons::get("paste"), u"Coller"_s, QKeySequence::Paste, this, [this] { paste(QApplication::clipboard()->text()); });
    menu.addAction(icons::get("eraser"), u"Effacer"_s, QKeySequence::Delete, this, [this] { clearSelection(); });
    menu.addSeparator();
    menu.addAction(icons::get("row-insert"), u"Insérer une ligne"_s, this, [this] { insertRow(); });
    menu.addAction(icons::get("row-delete"), u"Supprimer les lignes"_s, this, [this] { deleteSelectedRows(); });
    menu.exec(e->globalPos());
}

void TableGrid::dragEnterEvent(QDragEnterEvent *e)
{
    if (dnd::quantityFrom(e->mimeData()))
        e->acceptProposedAction();
}

void TableGrid::dragMoveEvent(QDragMoveEvent *e)
{
    if (dnd::quantityFrom(e->mimeData()))
        e->acceptProposedAction();
}

void TableGrid::dropEvent(QDropEvent *e)
{
    const std::vector<QId> ids = dnd::quantitiesFrom(e->mimeData());
    if (ids.empty())
        return;
    e->acceptProposedAction();
    const ViewId table = m_model->tableId();
    m_win.store().edit([&](Workspace &ws) {
        TableView *t = ws.tableMut(table);
        if (!t)
            return;
        for (QId q : ids)
            if (std::find(t->columns.begin(), t->columns.end(), q) == t->columns.end())
                t->columns.push_back(q);
    });
}

// --- panneau ----------------------------------------------------------------------------------

TablePanel::TablePanel(MainWindow &win, ViewId id, QWidget *parent) : QWidget(parent), m_win(win), m_id(id)
{
    theme::setMinimumWidth(this, 300);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto *bar = new QWidget(this);
    auto *flow = new FlowLayout(bar, 2);
    m_model = new TableModel(win.store(), id, this);
    m_grid = new TableGrid(win, m_model, this);

    // mêmes boutons que dans la fenêtre Grandeurs : « Mesure » crée une grandeur expérimentale
    auto *addCol = toolButton("plus", u"Mesure"_s, u"Nouvelle grandeur expérimentale dans ce tableur"_s, bar);
    connect(addCol, &QToolButton::clicked, this, [this] { dialogs::newColumn(m_win, m_id); });
    auto *addCalc = toolButton("function", u"Calculée"_s, u"Nouvelle grandeur calculée"_s, bar);
    connect(addCalc, &QToolButton::clicked, this, [this] { dialogs::newQuantity(m_win, true, m_id); });
    auto *existingMenu = new QMenu(this);
    auto *addExisting = menuButton("list", u"Grandeur"_s, u"Ajouter à ce tableur une grandeur existante"_s, existingMenu, bar);
    connect(existingMenu, &QMenu::aboutToShow, this, [this, existingMenu] {
        existingMenu->clear();
        const Store &store = m_win.store();
        if (store.ws.doc.quantities.empty())
            existingMenu->addAction(u"Aucune grandeur"_s)->setEnabled(false);
        for (const auto &q : store.ws.doc.quantities) {
            const bool present = std::find(m_model->columns().begin(), m_model->columns().end(), q.id) != m_model->columns().end();
            const QId qid = q.id;
            auto *a = existingMenu->addAction(present ? u"✓ "_s + q.name : q.name, this, [this, qid] {
                m_win.store().edit([&](Workspace &ws) {
                    if (TableView *t = ws.tableMut(m_id))
                        t->columns.push_back(qid);
                });
            });
            a->setEnabled(!present);
        }
    });
    // icône et texte : les icônes seules (1·2·3, lignes) étaient trop petites pour être lues
    auto *fillB = toolButton("numbers", u"Remplir…"_s, u"Remplir la colonne sélectionnée avec une suite de valeurs (de … à … par pas de …)"_s, bar);
    connect(fillB, &QToolButton::clicked, this, [this] {
        const QModelIndex c = m_grid->currentIndex();
        const QId q = c.isValid() ? m_model->quantityAt(c.column()) : 0;
        if (q && !m_model->isCalculated(c.column()))
            dialogs::fill(m_win, q);
        else
            m_win.store().message(u"Sélectionnez d'abord une colonne de mesures"_s, icons::Status::Warning);
    });
    auto *insert = toolButton("row-insert", u"Insérer une ligne"_s, u"Insérer une ligne au-dessus du curseur"_s, bar);
    connect(insert, &QToolButton::clicked, this, [this] { m_grid->insertRow(); });
    auto *del = toolButton("row-delete", u"Supprimer les lignes"_s, u"Supprimer les lignes sélectionnées"_s, bar);
    connect(del, &QToolButton::clicked, this, [this] { m_grid->deleteSelectedRows(); });
    auto *fileMenu = new QMenu(this);
    auto *file = menuButton("csv", u"CSV, PDF"_s, u"Importer, exporter en CSV ou en PDF, imprimer"_s, fileMenu, bar);
    fileMenu->addAction(icons::get("csv"), u"Importer un fichier CSV…"_s, this, [this] { m_win.importCsv(m_id); });
    fileMenu->addAction(icons::get("export"), u"Exporter le tableur en CSV…"_s, this, [this] { m_win.exportCsv(m_model->columns()); });
    fileMenu->addSeparator();
    fileMenu->addAction(icons::get("pdf"), u"PDF du tableur…"_s, this, [this] { m_win.tablePdf(m_id, false); });
    fileMenu->addAction(icons::get("print"), u"Imprimer…"_s, this, [this] { m_win.tablePdf(m_id, true); });
    m_count = new QLabel(bar);
    QPalette pal = m_count->palette();
    pal.setColor(QPalette::WindowText, theme::weakColor());
    m_count->setPalette(pal);
    for (QWidget *w : std::initializer_list<QWidget *>{addCol, addCalc, addExisting, fillB, insert, del, file, m_count})
        flow->addWidget(w);
    layout->addWidget(bar);
    layout->addWidget(m_grid, 1);
    connect(&win.store(), &Store::workspaceChanged, this, &TablePanel::refresh);
    connect(&win.store(), &Store::displayChanged, this, &TablePanel::refresh);
    refresh();
}

void TablePanel::refresh()
{
    m_model->reload();
    // colonnes ajoutées ou retirées : la largeur est de nouveau répartie (une nouvelle colonne ne
    // garde pas la largeur étroite de la colonne « + »)
    m_grid->distributeColumns();
    m_grid->horizontalHeader()->viewport()->update();
    const int n = m_model->dataRows();
    m_count->setText(u"%1 ligne%2"_s.arg(n).arg(plural(static_cast<size_t>(n))));
}
