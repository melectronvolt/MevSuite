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
#include <QMessageBox>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <numeric>
#include <set>

using namespace Qt::StringLiterals;

namespace {

/// Colonne « + » au bout du tableur.
int largeur_plus()
{
    return theme::px(40);
}

QString pluriel(size_t n)
{
    return n > 1 ? u"s"_s : QString();
}

/// Nom de colonne libre : x, y, z, puis a, b, c…
QString lettre_suivante(const Document &doc)
{
    for (const char *n : {"x", "y", "z", "t", "a", "b", "c", "d"}) {
        if (doc.verifie_nom(QString::fromLatin1(n)).isEmpty())
            return QString::fromLatin1(n);
    }
    return doc.nom_unique(u"c"_s);
}

/// Saisie dans une cellule : Entrée valide et descend, Tab passe à droite, flèches haut/bas.
class DelegueCellule : public QStyledItemDelegate {
public:
    explicit DelegueCellule(TableGrid *grille) : QStyledItemDelegate(grille), m_grille(grille) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        auto *saisie = new QLineEdit(parent);
        saisie->setAlignment(index.data(Qt::TextAlignmentRole).value<Qt::Alignment>());
        saisie->setFrame(false);
        return saisie;
    }

    void setEditorData(QWidget *editeur, const QModelIndex &index) const override
    {
        auto *saisie = static_cast<QLineEdit *>(editeur);
        // Un recalcul peut rafraîchir le tableur pendant la frappe, y compris
        // quand l'élève vient de vider la case.
        if (!saisie->isModified())
            saisie->setText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget *editeur, QAbstractItemModel *modele, const QModelIndex &cellule) const override
    {
        // Le texte affiché peut être arrondi. Ouvrir puis quitter une cellule
        // sans la modifier doit conserver tous les chiffres de la mesure.
        const auto *saisie = static_cast<QLineEdit *>(editeur);
        if (saisie->isModified())
            modele->setData(cellule, saisie->text(), Qt::EditRole);
    }

    bool eventFilter(QObject *objet, QEvent *evenement) override
    {
        if (evenement->type() == QEvent::KeyPress) {
            auto *saisie = qobject_cast<QLineEdit *>(objet);
            const auto *touche = static_cast<QKeyEvent *>(evenement);
            int decalageLigne = 0, decalageColonne = 0;
            switch (touche->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Down: decalageLigne = 1; break;
            case Qt::Key_Up: decalageLigne = -1; break;
            case Qt::Key_Tab: decalageColonne = 1; break;
            case Qt::Key_Backtab: decalageColonne = -1; break;
            default: return QStyledItemDelegate::eventFilter(objet, evenement);
            }
            if (saisie) {
                if (m_grille->valide_saisie())
                    m_grille->deplace_curseur_de(decalageLigne, decalageColonne);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(objet, evenement);
    }

private:
    TableGrid *m_grille;
};

} // namespace

// --- modèle -----------------------------------------------------------------------------------

TableModel::TableModel(Store &store, ViewId id, QObject *parent) : QAbstractTableModel(parent), m_store(store), m_id(id)
{
    if (const TableView *t = m_store.ws.tableur(id)) {
        for (QId c : t->columns) {
            if (m_store.ws.doc.obtient(c))
                m_columns.push_back(c);
        }
    }
    m_rows = calcule_lignes();
}

int TableModel::lignes_donnees() const
{
    return static_cast<int>(m_store.ws.doc.nombre_lignes(m_columns));
}

int TableModel::calcule_lignes() const
{
    int n = std::max(lignes_donnees() + 1, 40);
    if (fill)
        n = std::max(n, fill->to + 2);
    return n;
}

void TableModel::recharge()
{
    std::vector<QId> cols;
    if (const TableView *t = m_store.ws.tableur(m_id)) {
        for (QId c : t->columns) {
            if (m_store.ws.doc.obtient(c))
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
    const int rows = calcule_lignes();
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

QId TableModel::grandeur_en(int column) const
{
    return column >= 0 && static_cast<size_t>(column) < m_columns.size() ? m_columns[column] : 0;
}

bool TableModel::est_calculee(int column) const
{
    const Grandeur *q = m_store.ws.doc.obtient(grandeur_en(column));
    return q && q->est_calculee();
}

QVariant TableModel::data(const QModelIndex &index, int role) const
{
    const Grandeur *q = m_store.ws.doc.obtient(grandeur_en(index.column()));
    const int r = index.row();
    const theme::TableColors colors = theme::couleurs_tableur();
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
            return std::isfinite(*recopied) ? format_significatif(*recopied, m_store.prefs.sigDigits, m_store.style()) : QString();
        if (r == 0 && q->values.empty() && !q->error.isEmpty())
            return u"⚠ "_s + q->error;
        const double v = q->valeur(static_cast<size_t>(r));
        return std::isnan(v) ? QString() : format_significatif(v, m_store.prefs.sigDigits, m_store.style());
    }
    case Qt::EditRole: {
        if (!q)
            return {};
        const double v = q->valeur(static_cast<size_t>(r));
        return std::isfinite(v) ? format_significatif(v, 15, m_store.style()) : QString();
    }
    case Qt::TextAlignmentRole:
        if (q && r == 0 && q->values.empty() && !q->error.isEmpty())
            return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
        return QVariant::fromValue(Qt::Alignment(Qt::AlignCenter));
    case Qt::BackgroundRole:
        if (recopied)
            return colors.selection;
        if (q && q->est_calculee())
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
    const QId id = grandeur_en(index.column());
    if (!id)
        return false;
    const QString t = value.toString().trimmed();
    const auto v = lit_nombre(t);
    if (!v && !t.isEmpty()) {
        m_store.message(u"« %1 » n'est pas un nombre"_s.arg(t), icons::Status::Error);
        return false;
    }
    QString err;
    m_store.edite([&](Seance &ws) { err = ws.doc.definit_valeur(id, static_cast<size_t>(index.row()), v); });
    if (!err.isEmpty())
        m_store.message(err, icons::Status::Error);
    return err.isEmpty();
}

Qt::ItemFlags TableModel::flags(const QModelIndex &index) const
{
    if (!grandeur_en(index.column()))
        return Qt::ItemIsEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!est_calculee(index.column()))
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal)
        return grandeur_en(section) ? u"Glisser vers un graphe pour tracer cette grandeur ; Ctrl + clic sur d'autres en-têtes pour "
                                     u"glisser plusieurs colonnes ensemble (la première donne les abscisses).\n"
                                     u"Clic : sélectionner la colonne ; double-clic : nom, unité, formule."_s
                                   : u"Nouvelle grandeur"_s;
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    const Grandeur *q = m_store.ws.doc.obtient(grandeur_en(section));
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
    // deux lignes : le nom, puis l'unité et l'incertitude (indication : taille normale, couleur discrète)
    QSize s = QHeaderView::sizeHint();
    s.setHeight(std::max(theme::px(42), 2 * fontMetrics().height() + theme::px(10)));
    return s;
}

void TableHeader::paintSection(QPainter *p, const QRect &rect, int logical) const
{
    const theme::TableColors colors = theme::couleurs_tableur();
    const Store &store = m_win.depot();
    const Grandeur *q = store.ws.doc.obtient(m_model->grandeur_en(logical));
    p->save();
    p->fillRect(rect, q && q->est_calculee() ? colors.calculated : colors.header);
    p->setPen(colors.grid);
    p->drawLine(rect.topRight(), rect.bottomRight());
    p->drawLine(rect.bottomLeft(), rect.bottomRight());
    // poignée de largeur sous la souris (ou tirée) : le bord droit est souligné
    if (logical == m_hoverHandle || logical == m_resizeSection) {
        const int w = std::max(2, theme::px(3));
        p->fillRect(QRect(rect.right() - w + 1, rect.top(), w, rect.height()), colors.cursor);
    }
    if (logical == m_dropSection) {
        p->setPen(QPen(colors.cursor, 2));
        p->drawRect(rect.adjusted(1, 1, -2, -2));
    }
    if (!q) {
        const int side = theme::px(16);
        icons::obtient("plus").paint(p, QRect(rect.left() + theme::px(8), rect.center().y() - side / 2, side, side));
        p->restore();
        return;
    }
    const bool isUnc = store.ws.doc.proprietaire_incertitude(q->id) != nullptr;
    // nom de la colonne : demi-gras, taille normale
    QFont f = font();
    f.setWeight(QFont::DemiBold);
    p->setFont(f);
    p->setPen(q->error.isEmpty() ? theme::couleur_type(q->est_calculee(), isUnc) : colors.error);
    const QString title = q->est_calculee() ? u"ƒ "_s + q->name : q->name;
    const int m = theme::px(3);
    const QRect top(rect.left() + m, rect.top() + m, rect.width() - 2 * m, rect.height() / 2);
    p->drawText(top, Qt::AlignHCenter | Qt::AlignBottom, QFontMetrics(f).elidedText(title, Qt::ElideRight, top.width()));
    QString sub = unite_lisible(q->unit);
    if (q->uncertainty)
        sub = (sub.isEmpty() ? QString() : sub + u" "_s) + u"± "_s + store.ws.doc.nom(*q->uncertainty);
    const QFont sf = font();
    p->setFont(sf);
    p->setPen(colors.weak);
    const QRect bottom(rect.left() + m, rect.top() + rect.height() / 2 + 1, rect.width() - 2 * m, rect.height() / 2 - 4);
    p->drawText(bottom, Qt::AlignHCenter | Qt::AlignTop, QFontMetrics(sf).elidedText(sub, Qt::ElideRight, bottom.width()));
    p->restore();
}

int TableHeader::poignee_en(QPoint pos) const
{
    // zone de la poignée : quelques points de part et d'autre du bord droit d'une colonne (plus
    // large que celle de Qt, difficile à attraper) ; au début d'une colonne, c'est la poignée de la
    // colonne de gauche. La colonne « + » n'en a pas.
    const int grip = std::max(style()->pixelMetric(QStyle::PM_HeaderGripMargin, nullptr, this), theme::px(6));
    const int s = logicalIndexAt(pos);
    if (s < 0)
        return -1;
    const int last = m_model->columnCount() - 1;
    const int left = sectionViewportPosition(s), right = left + sectionSize(s);
    if (std::abs(pos.x() - right) <= grip)
        return s == last ? -1 : s;
    if (std::abs(pos.x() - left) <= grip) {
        const int v = visualIndex(s);
        const int previous = v > 0 ? logicalIndex(v - 1) : -1;
        return previous == last ? -1 : previous;
    }
    return -1;
}

void TableHeader::mousePressEvent(QMouseEvent *e)
{
    m_press = e->pos();
    m_pressSection = logicalIndexAt(e->pos());
    m_dragging = false;
    // poignée de largeur : la colonne se règle ici, sans passer par QHeaderView (sa zone, plus
    // étroite, ne coïncidait pas toujours avec celle-ci : la colonne partait en glisser-déposer)
    if (e->button() == Qt::LeftButton) {
        if (const int h = poignee_en(e->pos()); h >= 0) {
            m_resizeSection = h;
            m_resizeStartX = e->pos().x();
            m_resizeStartWidth = sectionSize(h);
            viewport()->setCursor(Qt::SplitHCursor);
            viewport()->update();
            e->accept();
            return;
        }
        return;
    }
    QHeaderView::mousePressEvent(e);
}

QPixmap TableHeader::image_glisser(const std::vector<int> &columns) const
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
    if (m_resizeSection >= 0) {
        // la largeur suit la souris ; TableGrid la retient (sectionResized)
        const int width = std::max(minimumSectionSize(), m_resizeStartWidth + e->pos().x() - m_resizeStartX);
        if (width != sectionSize(m_resizeSection))
            resizeSection(m_resizeSection, width);
        return;
    }
    if (!(e->buttons() & Qt::LeftButton)) {
        QHeaderView::mouseMoveEvent(e);
        // survol : flèches sur la poignée de largeur (bord souligné), main ouverte sur l'en-tête
        // d'une grandeur (elle se glisse)
        const int handle = poignee_en(e->pos());
        if (handle != m_hoverHandle) {
            m_hoverHandle = handle;
            viewport()->update();
        }
        if (handle >= 0)
            viewport()->setCursor(Qt::SplitHCursor);
        else if (m_model->grandeur_en(logicalIndexAt(e->pos())))
            viewport()->setCursor(Qt::OpenHandCursor);
        else
            viewport()->unsetCursor();
        return;
    }
    const QId id = m_model->grandeur_en(m_pressSection);
    if (!m_dragging && id && (e->pos() - m_press).manhattanLength() > QApplication::startDragDistance()) {
        m_dragging = true;
        // en-tête d'une colonne sélectionnée : toutes les colonnes sélectionnées partent ensemble
        auto *grid = qobject_cast<TableGrid *>(parentWidget());
        std::vector<int> columns = grid ? grid->colonnes_choisies() : std::vector<int>{};
        if (std::find(columns.begin(), columns.end(), m_pressSection) == columns.end())
            columns = {m_pressSection};
        if (grid)
            grid->glisse_colonnes(columns);
    }
}

void TableHeader::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_resizeSection >= 0) {
        m_resizeSection = -1;
        m_pressSection = -1;
        viewport()->update();
        // les autres colonnes se répartissent la largeur restante
        if (auto *grid = qobject_cast<TableGrid *>(parentWidget()))
            grid->repartit_colonnes();
        return;
    }
    if (!m_dragging && e->button() == Qt::LeftButton && m_pressSection >= 0) {
        auto *grid = qobject_cast<TableGrid *>(parentWidget());
        if (!m_model->grandeur_en(m_pressSection)) {
            dialogs::nouvelle_colonne(m_win, m_model->id_tableur());
        } else if (grid) {
            grid->choisit_colonnes(m_pressSection, e->modifiers());
        }
    }
    m_dragging = false;
    m_pressSection = -1;
}

void TableHeader::leaveEvent(QEvent *e)
{
    QHeaderView::leaveEvent(e);
    if (m_hoverHandle >= 0 && m_resizeSection < 0) {
        m_hoverHandle = -1;
        viewport()->update();
    }
}

void TableHeader::mouseDoubleClickEvent(QMouseEvent *e)
{
    // double-clic sur une poignée : la colonne reprend sa largeur automatique
    if (const int h = poignee_en(e->pos()); h >= 0) {
        if (auto *grid = qobject_cast<TableGrid *>(parentWidget()))
            grid->retablit_largeur_colonne(h);
        return;
    }
    const int s = logicalIndexAt(e->pos());
    if (const QId id = m_model->grandeur_en(s))
        dialogs::modifie_grandeur(m_win, id);
}

void TableHeader::contextMenuEvent(QContextMenuEvent *e)
{
    const int c = logicalIndexAt(e->pos());
    const QId id = m_model->grandeur_en(c);
    const Grandeur *q = m_win.depot().ws.doc.obtient(id);
    auto *grid = qobject_cast<TableGrid *>(parentWidget());
    if (!q || !grid)
        return;
    MainWindow &win = m_win;
    const ViewId table = m_model->id_tableur();
    QMenu menu(this);
    auto *title = menu.addAction(icons::obtient(q->est_calculee() ? "function" : "numbers"), q->name);
    title->setEnabled(false);
    menu.addSeparator();
    menu.addAction(icons::obtient("pencil"), u"Nom, unité, formule…"_s, this, [&win, id] { dialogs::modifie_grandeur(win, id); });
    if (!q->est_calculee())
        menu.addAction(icons::obtient("function"), u"Convertir en grandeur calculée…"_s, this, [&win, id] { dialogs::modifie_grandeur(win, id, true); });
    if (!q->est_calculee())
        menu.addAction(icons::obtient("numbers"), u"Remplir avec une suite de valeurs…"_s, this, [&win, id] { dialogs::remplit(win, id); });
    menu.addAction(icons::obtient("plusminus"), u"Incertitude…"_s, this, [&win, id] { dialogs::nouvelle_incertitude(win, id); });
    menu.addAction(icons::obtient("sort"), u"Trier le tableur selon cette colonne"_s, this, [grid, c] { grid->trie_par(c); });
    menu.addAction(icons::obtient("stats"), u"Statistiques"_s, this, [&win, id] { win.ouvre_statistiques(id); });
    if (c > 0) {
        const QId first = m_model->grandeur_en(0);
        menu.addAction(icons::obtient("chart"), u"Tracer en fonction de la 1re colonne"_s, this, [&win, first, id] { win.nouveau_graphe(std::make_pair(first, id)); });
    }
    menu.addAction(icons::obtient("copy"), u"Copier la colonne avec son en-tête"_s, this, [grid, c] {
        grid->choisit_donnees_colonne(c);
        grid->copie_selection(true);
    });
    menu.addSeparator();
    menu.addAction(icons::obtient("eye-off"), u"Retirer du tableur"_s, this, [&win, table, id] {
        win.depot().edite([&](Seance &ws) {
            if (TableView *t = ws.tableur_modifiable(table))
                std::erase(t->columns, id);
        });
    })->setToolTip(u"La grandeur reste dans le document"_s);
    menu.addAction(icons::obtient("trash"), u"Supprimer la grandeur…"_s, this, [&win, id] { dialogs::confirme_suppression(win, id); });
    menu.exec(e->globalPos());
}

void TableHeader::dragEnterEvent(QDragEnterEvent *e)
{
    if (dnd::grandeur_de(e->mimeData()))
        e->acceptProposedAction();
}

void TableHeader::dragMoveEvent(QDragMoveEvent *e)
{
    if (!dnd::grandeur_de(e->mimeData()))
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
    const std::vector<QId> ids = dnd::grandeurs_de(e->mimeData());
    int at = logicalIndexAt(e->position().toPoint());
    m_dropSection = -1;
    viewport()->update();
    if (ids.empty())
        return;
    if (at < 0)
        at = static_cast<int>(m_model->colonnes().size());
    e->acceptProposedAction();
    const ViewId table = m_model->id_tableur();
    // colonnes posées (ou déplacées) ensemble, dans leur ordre, devant la colonne visée
    m_win.depot().edite([&](Seance &ws) {
        TableView *t = ws.tableur_modifiable(table);
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
    connect(&m_distribute, &QTimer::timeout, this, &TableGrid::repartit_colonnes);
    // largeur changée à la main : gardée (à 100 %), les autres colonnes se répartissent le reste
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int c, int, int width) {
        auto *header = qobject_cast<TableHeader *>(horizontalHeader());
        if (m_fitting || !header || !header->redimensionnement() || c >= m_model->columnCount() - 1)
            return;
        if (const QId q = m_model->grandeur_en(c)) {
            m_userWidths[q] = width / theme::zoom();
            m_distribute.start(120);
        }
    });
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    setItemDelegate(new DelegueCellule(this));
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    setSelectionMode(QAbstractItemView::ContiguousSelection);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setWordWrap(false);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setMouseTracking(true);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(&win.depot(), &Store::affichage_change, this, &TableGrid::ajuste_tailles);
    ajuste_tailles();
    m_scrollTimer.setInterval(40);
    connect(&m_scrollTimer, &QTimer::timeout, this, [this] {
        verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        maj_remplissage(m_lastPos);
    });
}

QRect TableGrid::rectangle_selection() const
{
    QRect cadre;
    qint64 nombre = 0;
    for (const auto &plage : selectionModel()->selection()) {
        cadre = cadre.united(QRect(plage.left(), plage.top(), plage.width(), plage.height()));
        nombre += qint64(plage.width()) * plage.height();
    }
    // La poignée prolonge un bloc entier. Entre deux colonnes choisies avec Ctrl,
    // il peut rester une colonne que l'élève souhaite garder intacte.
    return nombre == qint64(cadre.width()) * cadre.height() ? cadre : QRect();
}

std::vector<size_t> TableGrid::lignes_choisies() const
{
    std::set<size_t> lignes;
    for (const auto &plage : selectionModel()->selection())
        for (int ligne = plage.top(); ligne <= plage.bottom(); ++ligne)
            lignes.insert(static_cast<size_t>(ligne));
    return {lignes.begin(), lignes.end()};
}

bool TableGrid::valide_saisie()
{
    const QModelIndex cellule = currentIndex();
    auto *saisie = qobject_cast<QLineEdit *>(indexWidget(cellule));
    if (!saisie || state() != QAbstractItemView::EditingState)
        return true;
    if (saisie->isModified() && !m_model->setData(cellule, saisie->text(), Qt::EditRole)) {
        saisie->setFocus();
        return false;
    }
    closeEditor(saisie, QAbstractItemDelegate::NoHint);
    return true;
}

void TableGrid::deplace_curseur_de(int dRow, int dCol)
{
    const QModelIndex c = currentIndex();
    if (!c.isValid())
        return;
    const int r = std::clamp(c.row() + dRow, 0, m_model->rowCount() - 1);
    const int col = std::clamp(c.column() + dCol, 0, std::max(static_cast<int>(m_model->colonnes().size()) - 1, 0));
    const QModelIndex next = m_model->index(r, col);
    setCurrentIndex(next);
    scrollTo(next);
}

void TableGrid::choisit_donnees_colonne(int column)
{
    choisit_colonnes(column, Qt::NoModifier);
}

void TableGrid::choisit_colonnes(int column, Qt::KeyboardModifiers modifiers)
{
    const int last = std::max(m_model->lignes_donnees() - 1, 0);
    auto columnRange = [&](int from, int to) { return QItemSelection(m_model->index(0, from), m_model->index(last, to)); };
    const int anchor = currentIndex().isValid() ? currentIndex().column() : column;
    if (modifiers & Qt::ShiftModifier) {
        // de la colonne courante à celle-ci
        const int c0 = std::min(anchor, column), c1 = std::max(anchor, column);
        selectionModel()->select(columnRange(c0, c1), (modifiers & Qt::ControlModifier) ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect);
    } else if (modifiers & Qt::ControlModifier) {
        // ajoutée, ou retirée si elle l'était
        selectionModel()->select(columnRange(column, column), colonne_entiere_choisie(column) ? QItemSelectionModel::Deselect : QItemSelectionModel::Select);
    } else {
        selectionModel()->select(columnRange(column, column), QItemSelectionModel::ClearAndSelect);
    }
    selectionModel()->setCurrentIndex(m_model->index(0, column), QItemSelectionModel::NoUpdate);
    setFocus();
}

bool TableGrid::colonne_entiere_choisie(int column) const
{
    const int rows = m_model->lignes_donnees();
    if (rows <= 0 || !m_model->grandeur_en(column))
        return false;
    for (int r = 0; r < rows; ++r)
        if (!selectionModel()->isSelected(m_model->index(r, column)))
            return false;
    return true;
}

std::vector<int> TableGrid::colonnes_choisies(bool whole) const
{
    std::set<int> colonnes;
    for (const auto &plage : selectionModel()->selection())
        for (int c = plage.left(); c <= plage.right(); ++c)
            if (m_model->grandeur_en(c))
                colonnes.insert(c);
    std::vector<int> out(colonnes.begin(), colonnes.end());
    if (whole)
        std::erase_if(out, [this](int c) { return !colonne_entiere_choisie(c); });
    // dans l'ordre des colonnes affichées
    std::sort(out.begin(), out.end(), [this](int a, int b) { return horizontalHeader()->visualIndex(a) < horizontalHeader()->visualIndex(b); });
    return out;
}

void TableGrid::glisse_colonnes(const std::vector<int> &columns)
{
    std::vector<QId> ids;
    for (int c : columns)
        if (const QId q = m_model->grandeur_en(c))
            ids.push_back(q);
    if (ids.empty())
        return;
    auto *drag = new QDrag(this);
    drag->setMimeData(dnd::mime_grandeurs(ids));
    if (auto *header = qobject_cast<TableHeader *>(horizontalHeader())) {
        const QPixmap pm = header->image_glisser(columns);
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(static_cast<int>(pm.width() / pm.devicePixelRatio() / 2), static_cast<int>(pm.height() / pm.devicePixelRatio() / 2)));
    }
    if (ids.size() > 1)
        m_win.depot().message(u"Sur un graphe, la première colonne donne les abscisses, les suivantes les ordonnées"_s);
    drag->exec(Qt::CopyAction | Qt::MoveAction);
}

void TableGrid::copie_selection(bool withHeaders)
{
    const auto colonnes = colonnes_choisies();
    if (colonnes.empty())
        return;
    const auto lignes = lignes_choisies();
    const Store &store = m_win.depot();
    QString text;
    if (withHeaders) {
        QStringList h;
        for (int c : colonnes) {
            const Grandeur *q = store.ws.doc.obtient(m_model->grandeur_en(c));
            h << (q ? (q->unit.isEmpty() ? q->name : u"%1 (%2)"_s.arg(q->name, q->unit)) : QString());
        }
        text += h.join(u'\t') + u'\n';
    }
    for (size_t r : lignes) {
        QStringList row;
        for (int c : colonnes) {
            const Grandeur *q = store.ws.doc.obtient(m_model->grandeur_en(c));
            const bool choisie = selectionModel()->isSelected(m_model->index(static_cast<int>(r), c));
            const double v = choisie && q ? q->valeur(r) : NAN;
            row << (std::isfinite(v) ? plus_court_style(v, store.style()) : QString());
        }
        text += row.join(u'\t') + u'\n';
    }
    QApplication::clipboard()->setText(text);
    const size_t n = lignes.size();
    m_win.depot().message(u"%1 ligne%2 copiée%2"_s.arg(n).arg(pluriel(n)));
}

void TableGrid::efface_selection()
{
    std::vector<PlageCases> plages;
    for (const auto &plage : selectionModel()->selection())
        for (int c = plage.left(); c <= plage.right(); ++c)
            if (const QId id = m_model->grandeur_en(c))
                plages.push_back({id, size_t(plage.top()), size_t(plage.bottom())});
    Store &store = m_win.depot();
    if (store.ws.doc.efface_cases(plages))
        store.modifie();
}

void TableGrid::supprime_lignes_choisies()
{
    const auto lignes = lignes_choisies();
    if (lignes.empty())
        return;
    const int colonne = selectionModel()->selection().front().left();
    const auto ids = m_win.depot().ws.mesures_liees(m_model->colonnes());
    QStringList horsTableur;
    const auto &colonnes = m_model->colonnes();
    for (QId id : ids)
        if (std::find(colonnes.begin(), colonnes.end(), id) == colonnes.end())
            horsTableur << m_win.depot().ws.doc.nom(id);
    if (!horsTableur.isEmpty() && QMessageBox::question(this, u"Supprimer des mesures liées"_s,
            u"Ces lignes seront aussi supprimées dans %1 pour préserver les couples de mesures. Continuer ?"_s.arg(horsTableur.join(u", "_s)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    m_win.depot().edite([&](Seance &ws) { ws.doc.supprime_lignes(ids, lignes); });
    setCurrentIndex(m_model->index(static_cast<int>(lignes.front()), colonne));
}

void TableGrid::insere_ligne()
{
    const QModelIndex c = currentIndex();
    const auto ids = m_win.depot().ws.mesures_liees(m_model->colonnes());
    const size_t row = c.isValid() ? static_cast<size_t>(c.row()) : 0;
    m_win.depot().edite([&](Seance &ws) { ws.doc.insere_ligne(ids, row); });
}

void TableGrid::trie_par(int column)
{
    Store &store = m_win.depot();
    const Grandeur *key = store.ws.doc.obtient(m_model->grandeur_en(column));
    if (!key)
        return;
    const auto ids = store.ws.mesures_liees(m_model->colonnes());
    size_t n = 0;
    for (QId id : ids) {
        if (const Grandeur *q = store.ws.doc.obtient(id); q && !q->est_calculee())
            n = std::max(n, q->values.size());
    }
    std::vector<size_t> ordre(n);
    std::iota(ordre.begin(), ordre.end(), size_t{0});
    // On trie les numéros de ligne avant de déplacer ensemble les mesures liées.
    std::stable_sort(ordre.begin(), ordre.end(), [key](size_t a, size_t b) {
        const double x = key->valeur(a), y = key->valeur(b);
        if (std::isnan(x))
            return false;
        if (std::isnan(y))
            return true;
        return x < y;
    });
    if (key->est_calculee())
        store.message(u"Tri selon une grandeur calculée : les mesures sont réordonnées selon ses valeurs actuelles"_s, icons::Status::Info);
    store.edite([&](Seance &ws) {
        ws.doc.permute_lignes(ids, ordre);
    });
}

void TableGrid::colle(const QString &text)
{
    const auto lecture = csv::lit_collage(text);
    const auto &blocks = lecture.columns;
    if (blocks.empty())
        return;
    Store &store = m_win.depot();
    const QModelIndex cur = currentIndex();
    const int row0 = cur.isValid() ? cur.row() : 0;
    const int col0 = cur.isValid() ? cur.column() : 0;
    const ViewId table = m_model->id_tableur();
    const std::vector<QId> cols = m_model->colonnes();
    int created = 0, written = 0;
    QStringList errors;
    store.edite([&](Seance &ws) {
        for (size_t j = 0; j < blocks.size(); ++j) {
            const auto &[name, unit, vals] = blocks[j];
            const size_t target = static_cast<size_t>(col0) + j;
            if (target < cols.size()) {
                const Grandeur *q = ws.doc.obtient(cols[target]);
                if (!q)
                    continue;
                if (q->est_calculee()) {
                    errors << u"« %1 » est calculée : colonne ignorée"_s.arg(q->name);
                    continue;
                }
                // colonne encore vide : elle prend le nom et l'unité de l'en-tête collé
                const bool vide = std::all_of(q->values.begin(), q->values.end(), [](double v) { return std::isnan(v); });
                if (!unit.isEmpty() && !q->unit.isEmpty() && !(row0 == 0 && vide)
                    && unite_lisible(unit) != unite_lisible(q->unit)) {
                    errors << u"« %1 » est en %2, les valeurs copiées sont en %3. Colonne ignorée."_s.arg(q->name, q->unit, unit);
                    continue;
                }
                if (row0 == 0 && !name.isEmpty() && vide) {
                    if (name != q->name && ws.doc.verifie_nom(name, q->id).isEmpty())
                        ws.renomme_grandeur(q->id, name);
                    if (!unit.isEmpty())
                        ws.doc.definit_unite(q->id, unit);
                }
                q = ws.doc.obtient(cols[target]);
                std::vector<double> values = q->values;
                if (values.size() < static_cast<size_t>(row0) + vals.size())
                    values.resize(static_cast<size_t>(row0) + vals.size(), NAN);
                for (size_t i = 0; i < vals.size(); ++i)
                    values[static_cast<size_t>(row0) + i] = vals[i];
                ws.doc.definit_valeurs(q->id, values);
                ++written;
            } else {
                const QString base = name.isEmpty() ? lettre_suivante(ws.doc) : ws.doc.nom_unique(name);
                std::vector<double> values(static_cast<size_t>(row0), NAN);
                values.insert(values.end(), vals.begin(), vals.end());
                if (auto id = ws.doc.ajoute_experimentale(base, unit, values)) {
                    if (TableView *t = ws.tableur_modifiable(table))
                        t->columns.push_back(*id);
                    ++created;
                }
            }
        }
    });
    const int n = written + created;
    QString msg = u"%1 colonne%2 collée%2"_s.arg(n).arg(pluriel(n));
    if (created > 0)
        msg += u" dont %1 nouvelle%2"_s.arg(created).arg(pluriel(created));
    if (lecture.badCells > 0)
        errors << u"%1 case%2 illisible%2 laissée%2 vide%2 (« %3 »)"_s.arg(lecture.badCells).arg(pluriel(lecture.badCells), lecture.firstBad.text);
    // un seul message dans la barre d'état : les colonnes et cases ignorées à la suite
    if (!errors.isEmpty())
        store.message(msg + u" ; "_s + errors.join(u" ; "_s), icons::Status::Warning);
    else
        store.message(msg);
}

void TableGrid::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::Copy)) {
        copie_selection(false);
        return;
    }
    if (e->matches(QKeySequence::Cut)) {
        copie_selection(false);
        efface_selection();
        return;
    }
    if (e->matches(QKeySequence::Paste)) {
        colle(QApplication::clipboard()->text());
        return;
    }
    if (e->matches(QKeySequence::SelectAll)) {
        const int last = std::max(m_model->lignes_donnees() - 1, 0);
        const int cols = static_cast<int>(m_model->colonnes().size());
        if (cols > 0)
            selectionModel()->select(QItemSelection(m_model->index(0, 0), m_model->index(last, cols - 1)), QItemSelectionModel::ClearAndSelect);
        return;
    }
    if (state() != QAbstractItemView::EditingState) {
        switch (e->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            efface_selection();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (currentIndex().isValid() && (m_model->flags(currentIndex()) & Qt::ItemIsEditable))
                edit(currentIndex());
            else
                deplace_curseur_de(1, 0);
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

QRect TableGrid::cadre_poignee() const
{
    if (!hasFocus() || state() == QAbstractItemView::EditingState || m_model->fill)
        return {};
    const QRect b = rectangle_selection();
    if (b.isNull() || b.right() >= static_cast<int>(m_model->colonnes().size()))
        return {};
    bool anyExperimental = false;
    for (int c = b.left(); c <= b.right(); ++c)
        anyExperimental = anyExperimental || !m_model->est_calculee(c);
    if (!anyExperimental)
        return {};
    const QRect cell = visualRect(m_model->index(b.bottom(), b.right()));
    if (!cell.isValid())
        return {};
    // La poignée et son contour restent dans la cellule que Qt repeint quand la sélection change.
    const int side = theme::px(7);
    return QRect(cell.right() - 2 * side, cell.bottom() - side, side, side);
}

QFont TableGrid::police_lecture(const Prefs &prefs)
{
    QFont f = QApplication::font();
    const double facteur = prefs.texteTableur / 100.0;
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * facteur);
    else
        f.setPixelSize(std::max(1, qRound(f.pixelSize() * facteur)));
    return f;
}

int TableGrid::hauteur_lecture(const Prefs &prefs, const QFont &f)
{
    return std::max(theme::px(prefs.hauteurLigne), QFontMetrics(f).height() + theme::px(4));
}

void TableGrid::ajuste_tailles()
{
    const Prefs &prefs = m_win.depot().prefs;
    const QFont lecture = police_lecture(prefs);
    if (font() != lecture)
        setFont(lecture);
    const int row = hauteur_lecture(prefs, lecture);
    verticalHeader()->setMinimumSectionSize(row);
    verticalHeader()->setDefaultSectionSize(row);
    verticalHeader()->setMinimumWidth(std::max(theme::px(44), fontMetrics().horizontalAdvance(QString::number(m_model->rowCount())) + theme::px(10)));
    horizontalHeader()->setMinimumSectionSize(theme::px(48));
    horizontalHeader()->setDefaultSectionSize(theme::px(96));
    repartit_colonnes();
    horizontalHeader()->updateGeometry();
}

void TableGrid::repartit_colonnes()
{
    m_distribute.stop();
    auto *header = qobject_cast<TableHeader *>(horizontalHeader());
    // pendant qu'une poignée est tirée, les autres colonnes attendent
    if (header && header->redimensionnement() && (QApplication::mouseButtons() & Qt::LeftButton)) {
        m_distribute.start(120);
        return;
    }
    QHeaderView *h = horizontalHeader();
    const int last = m_model->columnCount() - 1;
    if (last < 0)
        return;
    m_fitting = true;
    const int plus = std::max(largeur_plus(), h->minimumSectionSize());
    const int least = std::max(theme::px(90), fontMetrics().horizontalAdvance(u"−1,23456e+07"_s) + theme::px(10));
    const int avail = std::max(0, viewport()->width() - plus);
    std::vector<int> widths(static_cast<size_t>(last), 0);
    int used = 0, free = 0;
    for (int c = 0; c < last; ++c) {
        if (const auto it = m_userWidths.find(m_model->grandeur_en(c)); it != m_userWidths.end()) {
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

void TableGrid::retablit_largeur_colonne(int column)
{
    m_userWidths.erase(m_model->grandeur_en(column));
    repartit_colonnes();
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
        ajuste_tailles();
}

void TableGrid::paintEvent(QPaintEvent *e)
{
    QTableView::paintEvent(e);
    QPainter p(viewport());
    const theme::TableColors colors = theme::couleurs_tableur();
    // curseur bien visible
    if (hasFocus() && currentIndex().isValid() && state() != QAbstractItemView::EditingState) {
        const QRect r = visualRect(currentIndex());
        const int w = theme::px(2);
        p.save();
        p.setClipRect(r, Qt::IntersectClip);
        p.setPen(QPen(colors.cursor, w));
        p.drawRect(r.adjusted(w / 2, w / 2, -w / 2, -w / 2));
        p.restore();
    }
    // aperçu de la recopie
    if (m_model->fill) {
        const FillDrag &f = *m_model->fill;
        const QRect a = visualRect(m_model->index(f.r0, f.c0));
        const QRect b = visualRect(m_model->index(f.to, f.c1));
        p.setPen(QPen(colors.cursor, 1, Qt::DashLine));
        p.drawRect(a.united(b).adjusted(0, 0, -1, -1));
    }
    const QRect h = cadre_poignee();
    if (h.isValid()) {
        p.fillRect(h, colors.cursor);
        p.setPen(palette().color(QPalette::Base));
        p.drawRect(h.adjusted(-1, -1, 0, 0));
    }
}

void TableGrid::mousePressEvent(QMouseEvent *e)
{
    const QRect h = cadre_poignee();
    if (e->button() == Qt::LeftButton && h.isValid() && h.adjusted(-theme::px(3), -theme::px(3), theme::px(3), theme::px(3)).contains(e->pos())) {
        const QRect b = rectangle_selection();
        const int c1 = std::min(b.right(), static_cast<int>(m_model->colonnes().size()) - 1);
        m_model->fill = FillDrag{b.top(), b.bottom(), b.left(), c1, b.bottom()};
        m_lastPos = e->pos();
        viewport()->update();
        return;
    }
    // dans une colonne sélectionnée en entier : elle se glisse vers un graphe (le clic ordinaire
    // attend le relâcher)
    const QModelIndex at = indexAt(e->pos());
    if (e->button() == Qt::LeftButton && e->modifiers() == Qt::NoModifier && at.isValid() && colonne_entiere_choisie(at.column())
        && selectionModel()->isSelected(at)) {
        m_cellPress = CellPress{e->position(), e->globalPosition(), e->modifiers()};
        return;
    }
    QTableView::mousePressEvent(e);
}

void TableGrid::maj_remplissage(QPoint pos)
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
    const Store &store = m_win.depot();
    if (f.to > f.r1) {
        for (int c = f.c0; c <= f.c1; ++c) {
            const Grandeur *q = store.ws.doc.obtient(m_model->grandeur_en(c));
            if (!q || q->est_calculee())
                continue;
            std::vector<double> sel;
            for (int r = f.r0; r <= f.r1; ++r)
                sel.push_back(q->valeur(static_cast<size_t>(r)));
            m_model->preview.emplace_back(c, series::prolonge(sel, static_cast<size_t>(f.to - f.r1)));
        }
    }
    m_model->recharge();
    viewport()->update();
}

void TableGrid::mouseMoveEvent(QMouseEvent *e)
{
    if (m_model->fill) {
        m_lastPos = e->pos();
        maj_remplissage(e->pos());
        return;
    }
    if (m_cellPress) {
        if ((e->buttons() & Qt::LeftButton) && (e->position() - m_cellPress->pos).manhattanLength() > QApplication::startDragDistance()) {
            m_cellPress.reset();
            glisse_colonnes(colonnes_choisies(true));
        }
        return;
    }
    // poignée de recopie : croix ; colonne sélectionnée en entier : main ouverte (elle se glisse)
    const QRect h = cadre_poignee();
    const QModelIndex at = indexAt(e->pos());
    if (h.isValid() && h.adjusted(-theme::px(3), -theme::px(3), theme::px(3), theme::px(3)).contains(e->pos()))
        viewport()->setCursor(Qt::CrossCursor);
    else if (!e->buttons() && at.isValid() && selectionModel()->isSelected(at) && colonne_entiere_choisie(at.column()))
        viewport()->setCursor(Qt::OpenHandCursor);
    else
        viewport()->setCursor(Qt::ArrowCursor);
    QTableView::mouseMoveEvent(e);
}

void TableGrid::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_model->fill) {
        m_scrollTimer.stop();
        applique_remplissage();
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

void TableGrid::applique_remplissage()
{
    const FillDrag f = *m_model->fill;
    m_model->fill.reset();
    const auto preview = std::move(m_model->preview);
    m_model->preview.clear();
    if (f.to > f.r1) {
        m_win.depot().edite([&](Seance &ws) {
            for (const auto &[c, vals] : preview) {
                const Grandeur *q = ws.doc.obtient(m_model->grandeur_en(c));
                if (!q)
                    continue;
                std::vector<double> values = q->values;
                if (values.size() <= static_cast<size_t>(f.to))
                    values.resize(static_cast<size_t>(f.to) + 1, NAN);
                for (size_t k = 0; k < vals.size(); ++k)
                    values[static_cast<size_t>(f.r1) + 1 + k] = vals[k];
                ws.doc.definit_valeurs(q->id, values);
            }
        });
        selectionModel()->select(QItemSelection(m_model->index(f.r0, f.c0), m_model->index(f.to, f.c1)), QItemSelectionModel::ClearAndSelect);
        selectionModel()->setCurrentIndex(m_model->index(f.to, f.c1), QItemSelectionModel::NoUpdate);
    }
    m_model->recharge();
    viewport()->update();
}

void TableGrid::contextMenuEvent(QContextMenuEvent *e)
{
    const QModelIndex idx = indexAt(e->pos());
    if (idx.isValid() && !selectionModel()->isSelected(idx))
        setCurrentIndex(idx);
    QMenu menu(this);
    menu.addAction(icons::obtient("copy"), u"Copier"_s, QKeySequence::Copy, this, [this] { copie_selection(false); });
    menu.addAction(icons::obtient("copy"), u"Copier avec les en-têtes"_s, this, [this] { copie_selection(true); });
    menu.addAction(icons::obtient("paste"), u"Coller"_s, QKeySequence::Paste, this, [this] { colle(QApplication::clipboard()->text()); });
    menu.addAction(icons::obtient("eraser"), u"Effacer"_s, QKeySequence::Delete, this, [this] { efface_selection(); });
    menu.addSeparator();
    menu.addAction(icons::obtient("row-insert"), u"Insérer une ligne"_s, this, [this] { insere_ligne(); });
    menu.addAction(icons::obtient("row-delete"), u"Supprimer les lignes"_s, this, [this] { supprime_lignes_choisies(); });
    menu.exec(e->globalPos());
}

void TableGrid::dragEnterEvent(QDragEnterEvent *e)
{
    if (dnd::grandeur_de(e->mimeData()))
        e->acceptProposedAction();
}

void TableGrid::dragMoveEvent(QDragMoveEvent *e)
{
    if (dnd::grandeur_de(e->mimeData()))
        e->acceptProposedAction();
}

void TableGrid::dropEvent(QDropEvent *e)
{
    const std::vector<QId> ids = dnd::grandeurs_de(e->mimeData());
    if (ids.empty())
        return;
    e->acceptProposedAction();
    const ViewId table = m_model->id_tableur();
    m_win.depot().edite([&](Seance &ws) {
        TableView *t = ws.tableur_modifiable(table);
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
    theme::definit_largeur_min(this, 300);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto *bar = new QWidget(this);
    auto *flow = new FlowLayout(bar, 2);
    m_model = new TableModel(win.depot(), id, this);
    m_grid = new TableGrid(win, m_model, this);

    // mêmes boutons que dans la fenêtre Grandeurs : « Mesure » crée une grandeur expérimentale
    auto *addCol = bouton_outil("plus", u"Mesure"_s, u"Nouvelle grandeur expérimentale dans ce tableur"_s, bar);
    connect(addCol, &QToolButton::clicked, this, [this] { dialogs::nouvelle_colonne(m_win, m_id); });
    auto *addCalc = bouton_outil("function", u"Calculée"_s, u"Nouvelle grandeur calculée"_s, bar);
    connect(addCalc, &QToolButton::clicked, this, [this] { dialogs::nouvelle_grandeur(m_win, true, m_id); });
    auto *existingMenu = new QMenu(this);
    auto *addExisting = bouton_menu("list", u"Grandeur"_s, u"Ajouter à ce tableur une grandeur existante"_s, existingMenu, bar);
    connect(existingMenu, &QMenu::aboutToShow, this, [this, existingMenu] {
        existingMenu->clear();
        const Store &store = m_win.depot();
        if (store.ws.doc.quantities.empty())
            existingMenu->addAction(icons::obtient("info"), u"Aucune grandeur"_s)->setEnabled(false);
        for (const auto &q : store.ws.doc.quantities) {
            const bool present = std::find(m_model->colonnes().begin(), m_model->colonnes().end(), q.id) != m_model->colonnes().end();
            const QId qid = q.id;
            auto *a = existingMenu->addAction(icons::obtient(q.est_calculee() ? "function" : "numbers"), present ? u"✓ "_s + q.name : q.name, this, [this, qid] {
                m_win.depot().edite([&](Seance &ws) {
                    if (TableView *t = ws.tableur_modifiable(m_id))
                        t->columns.push_back(qid);
                });
            });
            a->setEnabled(!present);
        }
    });
    // icône et texte : les icônes seules (1·2·3, lignes) étaient trop petites pour être lues
    auto *fillB = bouton_outil("numbers", u"Remplir…"_s, u"Remplir la colonne sélectionnée avec une suite de valeurs (de … à … par pas de …)"_s, bar);
    connect(fillB, &QToolButton::clicked, this, [this] {
        const QModelIndex c = m_grid->currentIndex();
        const QId q = c.isValid() ? m_model->grandeur_en(c.column()) : 0;
        if (q && !m_model->est_calculee(c.column()))
            dialogs::remplit(m_win, q);
        else
            m_win.depot().message(u"Sélectionnez d'abord une colonne de mesures"_s, icons::Status::Warning);
    });
    auto *stats = bouton_outil("stats", u"Statistiques…"_s, u"Statistiques de la colonne sélectionnée"_s, bar);
    auto selection = [this] { return m_model->grandeur_en(std::max(0, m_grid->currentIndex().column())); };
    auto activeStats = [stats, selection] { stats->setEnabled(selection() != 0); };
    connect(m_grid->selectionModel(), &QItemSelectionModel::currentChanged, stats, activeStats);
    connect(m_model, &QAbstractItemModel::columnsInserted, stats, activeStats);
    connect(m_model, &QAbstractItemModel::columnsRemoved, stats, activeStats);
    activeStats();
    connect(stats, &QToolButton::clicked, this, [this, selection] { m_win.ouvre_statistiques(selection()); });
    auto *insert = bouton_outil("row-insert", u"Insérer une ligne"_s, u"Insérer une ligne au-dessus du curseur"_s, bar);
    connect(insert, &QToolButton::clicked, this, [this] { m_grid->insere_ligne(); });
    auto *del = bouton_outil("row-delete", u"Supprimer les lignes"_s, u"Supprimer les lignes sélectionnées"_s, bar);
    connect(del, &QToolButton::clicked, this, [this] { m_grid->supprime_lignes_choisies(); });
    auto *fileMenu = new QMenu(this);
    auto *file = bouton_menu("csv", u"CSV, PDF"_s, u"Importer, exporter en CSV ou en PDF, imprimer"_s, fileMenu, bar);
    fileMenu->addAction(icons::obtient("csv"), u"Importer un fichier CSV…"_s, this, [this] { m_win.importe_csv(m_id); });
    fileMenu->addAction(icons::obtient("export"), u"Exporter le tableur en CSV…"_s, this, [this] { m_win.exporte_csv(m_model->colonnes()); });
    fileMenu->addSeparator();
    fileMenu->addAction(icons::obtient("pdf"), u"PDF du tableur…"_s, this, [this] { m_win.tableur_pdf(m_id, false); });
    fileMenu->addAction(icons::obtient("print"), u"Imprimer…"_s, this, [this] { m_win.tableur_pdf(m_id, true); });
    m_count = new QLabel(bar);
    QPalette pal = m_count->palette();
    pal.setColor(QPalette::WindowText, theme::couleur_attenuee());
    m_count->setPalette(pal);
    for (QWidget *w : std::initializer_list<QWidget *>{addCol, addCalc, addExisting, fillB, stats, insert, del, file, m_count})
        flow->addWidget(w);
    layout->addWidget(bar);
    layout->addWidget(m_grid, 1);
    connect(&win.depot(), &Store::espace_travail_change, this, &TablePanel::rafraichit);
    connect(&win.depot(), &Store::affichage_change, this, &TablePanel::rafraichit);
    rafraichit();
}

void TablePanel::rafraichit()
{
    m_model->recharge();
    // colonnes ajoutées ou retirées : la largeur est de nouveau répartie (une nouvelle colonne ne
    // garde pas la largeur étroite de la colonne « + »)
    m_grid->repartit_colonnes();
    m_grid->horizontalHeader()->viewport()->update();
    const int n = m_model->lignes_donnees();
    m_count->setText(u"%1 ligne%2"_s.arg(n).arg(pluriel(static_cast<size_t>(n))));
}
