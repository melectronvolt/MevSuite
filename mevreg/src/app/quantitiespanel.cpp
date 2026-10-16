#include "app/quantitiespanel.h"

#include "app/dialogs.h"
#include "app/graphwidget.h"
#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/format.h"

#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

enum Column { Name, Unit, Count, Menu };

/// Liste qui glisse l'identifiant de la grandeur choisie.
class QuantityTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    bool editing() const { return state() == QAbstractItemView::EditingState; }

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(dnd::MIME)}; }
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        if (items.isEmpty())
            return nullptr;
        return dnd::quantityMime(items.first()->data(Name, Qt::UserRole).toULongLong());
    }
};

void warnScripts(MainWindow &win, const QString &old)
{
    for (const QString &s : win.store().ws.scriptsUsing(old))
        win.store().message(u"Le script « %1 » utilise encore l'ancien nom « %2 » : pensez à le modifier"_s.arg(s, old), icons::Status::Warning);
}

} // namespace

QuantitiesPanel::QuantitiesPanel(MainWindow &win, QWidget *parent) : QWidget(parent), m_win(win)
{
    theme::setMinimumWidth(this, 270);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto *bar = new QWidget(this);
    auto *flow = new FlowLayout(bar);
    auto *measure = toolButton("plus", u"Mesure"_s, u"Nouvelle grandeur expérimentale"_s, bar);
    auto *calc = toolButton("function", u"Calculée"_s, u"Nouvelle grandeur calculée par une formule"_s, bar);
    auto *import = toolButton("csv", u"Importer"_s, u"Importer un fichier CSV"_s, bar);
    flow->addWidget(measure);
    flow->addWidget(calc);
    flow->addWidget(import);
    connect(measure, &QToolButton::clicked, this, [this] { dialogs::newQuantity(m_win, false); });
    connect(calc, &QToolButton::clicked, this, [this] { dialogs::newQuantity(m_win, true); });
    connect(import, &QToolButton::clicked, this, [this] { m_win.importCsv(std::nullopt); });
    layout->addWidget(bar);
    m_dragHint = weakLabel(u"Glissez une grandeur vers un tableur ou un graphe."_s, this);
    layout->addWidget(m_dragHint);

    m_tree = new QuantityTree(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({u"Nom"_s, u"Unité"_s, u"Valeurs"_s, QString()});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setSectionResizeMode(Name, QHeaderView::Stretch);
    // unité et valeurs : à la largeur de leur contenu, puis à la main (le nom prend le reste)
    m_tree->header()->setSectionResizeMode(Unit, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(Count, QHeaderView::Interactive);
    connect(m_tree->header(), &QHeaderView::sectionResized, this, [this](int c) {
        if (!m_filling && (c == Unit || c == Count))
            m_userSized = true;
    });
    m_tree->header()->setSectionResizeMode(Menu, QHeaderView::Fixed);
    m_tree->header()->resizeSection(Menu, theme::px(30));
    m_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_tree, 1);
    m_empty = weakLabel(u"Aucune grandeur pour l'instant : « Mesure » crée une grandeur expérimentale, « Calculée » une grandeur définie par une formule."_s, this);
    layout->addWidget(m_empty);
    // sans grandeur (liste cachée), les textes restent en haut : la place libre va dessous
    layout->addStretch(0);
    connect(m_tree, &QTreeWidget::itemChanged, this, &QuantitiesPanel::itemChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        fillMenu(&menu, m_win, item->data(Name, Qt::UserRole).toULongLong(), this);
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });
    connect(&m_win.store(), &Store::workspaceChanged, this, &QuantitiesPanel::refresh);
    connect(&m_win.store(), &Store::displayChanged, this, &QuantitiesPanel::refresh);
    refresh();
}

void QuantitiesPanel::refresh()
{
    // pas pendant une saisie dans la liste : la mise à jour attend la fin de la saisie
    if (static_cast<QuantityTree *>(m_tree)->editing()) {
        if (!m_pending) {
            m_pending = true;
            connect(m_tree->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] {
                m_pending = false;
                QMetaObject::invokeMethod(this, &QuantitiesPanel::refresh, Qt::QueuedConnection);
            }, Qt::SingleShotConnection);
        }
        return;
    }
    const Workspace &ws = m_win.store().ws;
    // colonne du menu : elle suit l'agrandissement de l'interface
    m_tree->header()->resizeSection(Menu, theme::px(30));
    m_filling = true;
    const auto keep = m_tree->currentItem() ? m_tree->currentItem()->data(Name, Qt::UserRole).toULongLong() : 0;
    m_tree->clear();
    for (const auto &q : ws.doc.quantities) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        item->setData(Name, Qt::UserRole, QVariant::fromValue<qulonglong>(q.id));
        const Quantity *owner = ws.doc.uncertaintyOwner(q.id);
        const QColor color = q.error.isEmpty() ? theme::kindColor(q.isCalculated(), owner != nullptr) : theme::errorColor();
        item->setText(Name, q.name);
        QFont f = item->font(Name);
        f.setBold(true);
        item->setFont(Name, f);
        item->setForeground(Name, color);
        item->setText(Unit, q.unit);
        item->setToolTip(Unit, q.unit.isEmpty() ? QString() : u"Affichée : "_s + prettyUnit(q.unit));
        QString count = QString::number(q.count());
        if (owner)
            count += u"  u"_s;
        else if (q.isCalculated())
            count += u"  ƒ"_s;
        if (q.uncertainty)
            count += u"  ± "_s + ws.doc.name(*q.uncertainty);
        item->setText(Count, count);
        item->setForeground(Count, theme::weakColor());
        QString tip = owner ? u"Incertitude de « %1 »"_s.arg(owner->name)
                            : q.formula ? u"Grandeur calculée : %1 = %2"_s.arg(q.name, *q.formula) : u"Grandeur expérimentale"_s;
        if (!q.error.isEmpty())
            tip += u"\nErreur : "_s + q.error;
        const auto usages = ws.usages(q.id);
        if (usages.empty()) {
            tip += u"\nUtilisée nulle part"_s;
        } else {
            tip += u"\nUtilisée dans :"_s;
            for (const auto &u : usages)
                tip += u"\n  • "_s + u.text();
        }
        tip += u"\n\nGlisser vers un tableur ou un graphe ; double-clic pour renommer"_s;
        item->setToolTip(Name, tip);
        auto *more = new QToolButton(m_tree);
        more->setIcon(icons::get("dots"));
        more->setAutoRaise(true);
        more->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(more);
        const QId id = q.id;
        connect(menu, &QMenu::aboutToShow, menu, [this, menu, id] {
            menu->clear();
            fillMenu(menu, m_win, id, this);
        });
        more->setMenu(menu);
        m_tree->setItemWidget(item, Menu, more);
        if (q.id == keep)
            m_tree->setCurrentItem(item);
    }
    if (!m_userSized) {
        m_tree->resizeColumnToContents(Unit);
        m_tree->resizeColumnToContents(Count);
    }
    m_filling = false;
    m_empty->setVisible(ws.doc.quantities.empty());
    m_dragHint->setVisible(!ws.doc.quantities.empty());
    m_tree->setVisible(!ws.doc.quantities.empty());
}

void QuantitiesPanel::itemChanged(QTreeWidgetItem *item, int column)
{
    if (m_filling)
        return;
    Store &store = m_win.store();
    const QId id = item->data(Name, Qt::UserRole).toULongLong();
    const Quantity *q = store.ws.doc.get(id);
    if (!q)
        return;
    if (column == Name) {
        const QString text = item->text(Name).trimmed();
        if (text == q->name)
            return;
        const QString old = q->name;
        QString err;
        store.edit([&](Workspace &ws) { err = ws.renameQuantity(id, text); });
        if (!err.isEmpty())
            store.message(err, icons::Status::Error);
        else
            warnScripts(m_win, old);
    } else if (column == Unit) {
        const QString unit = item->text(Unit).trimmed();
        if (unit != q->unit)
            store.edit([&](Workspace &ws) { ws.doc.setUnit(id, unit); });
    }
}

void QuantitiesPanel::fillMenu(QMenu *menu, MainWindow &win, QId id, QWidget *owner)
{
    Store &store = win.store();
    const Quantity *q = store.ws.doc.get(id);
    if (!q)
        return;
    const bool calculated = q->isCalculated();
    const QString name = q->name;
    auto *title = menu->addAction(name);
    title->setEnabled(false);
    menu->addSeparator();
    if (auto *panel = qobject_cast<QuantitiesPanel *>(owner)) {
        menu->addAction(icons::get("pencil"), u"Renommer"_s, panel, [panel, id] {
            for (int i = 0; i < panel->m_tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *it = panel->m_tree->topLevelItem(i);
                if (it->data(Name, Qt::UserRole).toULongLong() == id)
                    panel->m_tree->editItem(it, Name);
            }
        });
    } else {
        menu->addAction(icons::get("pencil"), u"Nom, unité, formule…"_s, owner, [&win, id] { dialogs::editQuantity(win, id); });
    }
    // incertitude
    QMenu *unc = menu->addMenu(icons::get("plusminus"), u"Incertitude"_s);
    auto *none = unc->addAction(u"Aucune"_s, owner, [&store, id] { store.edit([&](Workspace &ws) { ws.doc.setUncertainty(id, std::nullopt); }); });
    none->setCheckable(true);
    none->setChecked(!q->uncertainty);
    for (const auto &o : store.ws.doc.quantities) {
        if (o.id == id)
            continue;
        const QId oid = o.id;
        auto *a = unc->addAction(o.name, owner, [&store, id, oid] { store.edit([&](Workspace &ws) { ws.doc.setUncertainty(id, oid); }); });
        a->setCheckable(true);
        a->setChecked(q->uncertainty == o.id);
    }
    unc->addSeparator();
    unc->addAction(icons::get("plus"), u"Nouvelle incertitude u_%1…"_s.arg(name), owner, [&win, id] { dialogs::newUncertainty(win, id); });
    // tableurs
    QMenu *tables = menu->addMenu(icons::get("table"), u"Ajouter au tableur"_s);
    for (const auto &t : store.ws.tables) {
        const bool present = std::find(t.columns.begin(), t.columns.end(), id) != t.columns.end();
        const ViewId tid = t.id;
        auto *a = tables->addAction(present ? u"✓ "_s + t.name : t.name, owner, [&win, tid, id] {
            win.store().edit([&](Workspace &ws) {
                if (TableView *tv = ws.tableMut(tid))
                    tv->columns.push_back(id);
            });
            win.openTab({TabKey::Table, tid, {}});
        });
        a->setEnabled(!present);
    }
    if (!store.ws.tables.empty())
        tables->addSeparator();
    tables->addAction(icons::get("plus"), u"Nouveau tableur"_s, owner, [&win, id] { win.newTable({id}); });
    // graphes
    QMenu *graphs = menu->addMenu(icons::get("chart"), u"Tracer dans un graphe"_s);
    for (const auto &g : store.ws.graphs) {
        const int pi = std::min(store.selectedPlot.count(g.id) ? store.selectedPlot.at(g.id) : 0, std::max(static_cast<int>(g.plots.size()) - 1, 0));
        if (g.plots.empty())
            continue;
        const Plot &plot = g.plots[pi];
        std::optional<QId> x;
        if (!plot.curves.empty())
            x = plot.curves.front().x;
        else if (store.pendingAbscissa.count(g.id))
            x = store.pendingAbscissa.at(g.id);
        QString label;
        bool enabled = true;
        if (x && *x == id) {
            label = u"✓ %1 (abscisse)"_s.arg(g.name);
            enabled = false;
        } else if (x && std::any_of(plot.curves.begin(), plot.curves.end(), [&](const CurveSpec &c) { return c.x == *x && c.y == id; })) {
            label = u"✓ "_s + g.name;
            enabled = false;
        } else if (x) {
            label = u"%1 : %2 en fonction de %3"_s.arg(g.name, name, store.ws.doc.name(*x));
        } else {
            label = u"%1 : %2 en abscisse"_s.arg(g.name, name);
        }
        const ViewId gid = g.id;
        auto *a = graphs->addAction(label, owner, [&win, gid, pi, id] {
            graphops::dropQuantity(win, gid, pi, id, std::nullopt);
            win.openTab({TabKey::Graph, gid, {}});
        });
        a->setEnabled(enabled);
    }
    if (!store.ws.graphs.empty())
        graphs->addSeparator();
    QMenu *newGraph = graphs->addMenu(icons::get("plus"), u"Nouveau graphe : %1 en fonction de"_s.arg(name));
    for (const auto &o : store.ws.doc.quantities) {
        if (o.id == id)
            continue;
        const QId xid = o.id;
        newGraph->addAction(o.name, owner, [&win, xid, id] { win.newGraph(std::make_pair(xid, id)); });
    }
    newGraph->setEnabled(store.ws.doc.quantities.size() > 1);
    menu->addAction(icons::get("stats"), u"Statistiques"_s, owner, [&win, id] { win.openStatistics(id); });
    if (!calculated)
        menu->addAction(icons::get("numbers"), u"Remplir avec une suite de valeurs…"_s, owner, [&win, id] { dialogs::fill(win, id); });
    if (calculated) {
        menu->addAction(icons::get("function"), u"Modifier la formule"_s, owner, [&win] { win.openTab({TabKey::Calculations, 0, {}}); });
        auto *freeze = menu->addAction(icons::get("snowflake"), u"Figer les valeurs"_s, owner,
                                       [&store, id] { store.edit([&](Workspace &ws) { ws.doc.freeze(id); }); });
        freeze->setToolTip(u"La grandeur devient expérimentale : ses valeurs ne sont plus recalculées"_s);
    }
    menu->addSeparator();
    menu->addAction(icons::get("trash"), u"Supprimer…"_s, owner, [&win, id] { dialogs::confirmDelete(win, id); });
}
