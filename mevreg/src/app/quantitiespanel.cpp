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
#include <QEvent>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

enum Column { Name, Unit, Count, Menu };

/// Liste qui glisse l'identifiant de la grandeur choisie.
class QuantityTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    bool en_edition() const { return state() == QAbstractItemView::EditingState; }

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(dnd::MIME)}; }
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        if (items.isEmpty())
            return nullptr;
        return dnd::mime_grandeur(items.first()->data(Name, Qt::UserRole).toULongLong());
    }
};

void avertit_scripts(MainWindow &win, const QString &old)
{
    for (const QString &s : win.depot().ws.scripts_utilisant(old))
        win.depot().message(u"Le script « %1 » utilise encore l'ancien nom « %2 » : pensez à le modifier"_s.arg(s, old), icons::Status::Warning);
}

} // namespace

QuantitiesPanel::QuantitiesPanel(MainWindow &win, QWidget *parent) : QWidget(parent), m_win(win)
{
    theme::definit_largeur_min(this, 270);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto *bar = new QWidget(this);
    auto *flow = new FlowLayout(bar);
    auto *measure = bouton_outil("plus", u"Mesure"_s, u"Nouvelle grandeur expérimentale"_s, bar);
    auto *calc = bouton_outil("function", u"Calculée"_s, u"Nouvelle grandeur calculée par une formule"_s, bar);
    auto *import = bouton_outil("csv", u"Importer"_s, u"Importer un fichier CSV"_s, bar);
    flow->addWidget(measure);
    flow->addWidget(calc);
    flow->addWidget(import);
    connect(measure, &QToolButton::clicked, this, [this] { dialogs::nouvelle_grandeur(m_win, false); });
    connect(calc, &QToolButton::clicked, this, [this] { dialogs::nouvelle_grandeur(m_win, true); });
    connect(import, &QToolButton::clicked, this, [this] { m_win.importe_csv(std::nullopt); });
    layout->addWidget(bar);
    m_dragHint = etiquette_attenuee(u"Glissez une grandeur vers un tableur ou un graphe."_s, this);
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
    // toutes les colonnes se règlent à la main (poignée entre deux en-têtes) ; unité et valeurs
    // partent de la largeur de leur contenu, le nom prend le reste tant qu'il n'a pas été réglé
    m_tree->header()->setSectionResizeMode(Name, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(Unit, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(Count, QHeaderView::Interactive);
    m_tree->header()->setMinimumSectionSize(theme::px(30));
    connect(m_tree->header(), &QHeaderView::sectionResized, this, [this](int c) {
        if (m_filling)
            return;
        if (c == Unit || c == Count)
            m_userSized = true;
        if (c == Name)
            m_nameSized = true;
        else
            nom_modele();
    });
    m_tree->viewport()->installEventFilter(this);
    m_tree->header()->setSectionResizeMode(Menu, QHeaderView::Fixed);
    m_tree->header()->resizeSection(Menu, theme::px(30));
    m_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_tree, 1);
    m_empty = etiquette_attenuee(u"Aucune grandeur pour l'instant : « Mesure » crée une grandeur expérimentale, « Calculée » une grandeur définie par une formule."_s, this);
    layout->addWidget(m_empty);
    // sans grandeur (liste cachée), les textes restent en haut : la place libre va dessous
    layout->addStretch(0);
    connect(m_tree, &QTreeWidget::itemChanged, this, &QuantitiesPanel::element_change);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        remplit_menu(&menu, m_win, item->data(Name, Qt::UserRole).toULongLong(), this);
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });
    connect(&m_win.depot(), &Store::espace_travail_change, this, &QuantitiesPanel::rafraichit);
    connect(&m_win.depot(), &Store::affichage_change, this, &QuantitiesPanel::rafraichit);
    rafraichit();
}

void QuantitiesPanel::rafraichit()
{
    // pas pendant une saisie dans la liste : la mise à jour attend la fin de la saisie
    if (static_cast<QuantityTree *>(m_tree)->en_edition()) {
        if (!m_pending) {
            m_pending = true;
            connect(m_tree->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] {
                m_pending = false;
                QMetaObject::invokeMethod(this, &QuantitiesPanel::rafraichit, Qt::QueuedConnection);
            }, Qt::SingleShotConnection);
        }
        return;
    }
    const Seance &ws = m_win.depot().ws;
    // colonne du menu : elle suit l'agrandissement de l'interface
    m_tree->header()->resizeSection(Menu, theme::px(30));
    m_filling = true;
    const auto keep = m_tree->currentItem() ? m_tree->currentItem()->data(Name, Qt::UserRole).toULongLong() : 0;
    m_tree->clear();
    for (const auto &q : ws.doc.quantities) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        item->setData(Name, Qt::UserRole, QVariant::fromValue<qulonglong>(q.id));
        const Grandeur *owner = ws.doc.proprietaire_incertitude(q.id);
        const QColor color = q.error.isEmpty() ? theme::couleur_type(q.est_calculee(), owner != nullptr) : theme::couleur_erreur();
        item->setText(Name, q.name);
        QFont f = item->font(Name);
        f.setBold(true);
        item->setFont(Name, f);
        item->setForeground(Name, color);
        item->setText(Unit, q.unit);
        item->setToolTip(Unit, q.unit.isEmpty() ? QString() : u"Affichée : "_s + unite_lisible(q.unit));
        QString count = QString::number(q.effectif());
        if (owner)
            count += u"  u"_s;
        else if (q.est_calculee())
            count += u"  ƒ"_s;
        if (q.uncertainty)
            count += u"  ± "_s + ws.doc.nom(*q.uncertainty);
        item->setText(Count, count);
        item->setForeground(Count, theme::couleur_attenuee());
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
                tip += u"\n  • "_s + u.texte();
        }
        tip += u"\n\nGlisser vers un tableur ou un graphe ; double-clic pour renommer"_s;
        item->setToolTip(Name, tip);
        auto *more = new QToolButton(m_tree);
        more->setIcon(icons::obtient("dots"));
        more->setAutoRaise(true);
        more->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(more);
        const QId id = q.id;
        connect(menu, &QMenu::aboutToShow, menu, [this, menu, id] {
            menu->clear();
            remplit_menu(menu, m_win, id, this);
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
    nom_modele();
    m_empty->setVisible(ws.doc.quantities.empty());
    m_dragHint->setVisible(!ws.doc.quantities.empty());
    m_tree->setVisible(!ws.doc.quantities.empty());
}

void QuantitiesPanel::nom_modele()
{
    // le nom prend la largeur laissée par les autres colonnes, sauf s'il a été réglé à la main
    if (m_nameSized)
        return;
    const QHeaderView *h = m_tree->header();
    const int others = h->sectionSize(Unit) + h->sectionSize(Count) + h->sectionSize(Menu);
    const int width = std::max(theme::px(60), m_tree->viewport()->width() - others);
    if (h->sectionSize(Name) != width) {
        m_filling = true;
        m_tree->header()->resizeSection(Name, width);
        m_filling = false;
    }
}

bool QuantitiesPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree->viewport() && event->type() == QEvent::Resize)
        nom_modele();
    return QWidget::eventFilter(watched, event);
}

void QuantitiesPanel::element_change(QTreeWidgetItem *item, int column)
{
    if (m_filling)
        return;
    Store &store = m_win.depot();
    const QId id = item->data(Name, Qt::UserRole).toULongLong();
    const Grandeur *q = store.ws.doc.obtient(id);
    if (!q)
        return;
    if (column == Name) {
        const QString text = item->text(Name).trimmed();
        if (text == q->name)
            return;
        const QString old = q->name;
        QString err;
        store.edite([&](Seance &ws) { err = ws.renomme_grandeur(id, text); });
        if (!err.isEmpty())
            store.message(err, icons::Status::Error);
        else
            avertit_scripts(m_win, old);
    } else if (column == Unit) {
        const QString unit = item->text(Unit).trimmed();
        if (unit != q->unit)
            store.edite([&](Seance &ws) { ws.doc.definit_unite(id, unit); });
    }
}

void QuantitiesPanel::remplit_menu(QMenu *menu, MainWindow &win, QId id, QWidget *owner)
{
    Store &store = win.depot();
    const Grandeur *q = store.ws.doc.obtient(id);
    if (!q)
        return;
    const bool calculated = q->est_calculee();
    const QString name = q->name;
    auto *title = menu->addAction(icons::obtient(calculated ? "function" : "numbers"), name);
    title->setEnabled(false);
    menu->addSeparator();
    if (auto *panel = qobject_cast<QuantitiesPanel *>(owner)) {
        menu->addAction(icons::obtient("pencil"), u"Renommer"_s, panel, [panel, id] {
            for (int i = 0; i < panel->m_tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *it = panel->m_tree->topLevelItem(i);
                if (it->data(Name, Qt::UserRole).toULongLong() == id)
                    panel->m_tree->editItem(it, Name);
            }
        });
    } else {
        menu->addAction(icons::obtient("pencil"), u"Nom, unité, formule…"_s, owner, [&win, id] { dialogs::modifie_grandeur(win, id); });
    }
    if (!q->est_calculee())
        menu->addAction(icons::obtient("function"), u"Convertir en grandeur calculée…"_s, owner, [&win, id] { dialogs::modifie_grandeur(win, id, true); });
    // incertitude
    QMenu *unc = menu->addMenu(icons::obtient("plusminus"), u"Incertitude"_s);
    auto *none = unc->addAction(icons::obtient("unlink"), u"Aucune"_s, owner, [&store, id] { store.edite([&](Seance &ws) { ws.doc.definit_incertitude(id, std::nullopt); }); });
    none->setCheckable(true);
    none->setChecked(!q->uncertainty);
    for (const auto &o : store.ws.doc.quantities) {
        if (o.id == id)
            continue;
        const QId oid = o.id;
        auto *a = unc->addAction(icons::obtient("plusminus"), o.name, owner, [&store, id, oid] { store.edite([&](Seance &ws) { ws.doc.definit_incertitude(id, oid); }); });
        a->setCheckable(true);
        a->setChecked(q->uncertainty == o.id);
    }
    unc->addSeparator();
    unc->addAction(icons::obtient("plus"), u"Nouvelle incertitude u_%1…"_s.arg(name), owner, [&win, id] { dialogs::nouvelle_incertitude(win, id); });
    // tableurs
    QMenu *tables = menu->addMenu(icons::obtient("table"), u"Ajouter au tableur"_s);
    for (const auto &t : store.ws.tables) {
        const bool present = std::find(t.columns.begin(), t.columns.end(), id) != t.columns.end();
        const ViewId tid = t.id;
        auto *a = tables->addAction(icons::obtient("table"), present ? u"✓ "_s + t.name : t.name, owner, [&win, tid, id] {
            win.depot().edite([&](Seance &ws) {
                if (TableView *tv = ws.tableur_modifiable(tid))
                    tv->columns.push_back(id);
            });
            win.ouvre_onglet({TabKey::Table, tid, {}});
        });
        a->setEnabled(!present);
    }
    if (!store.ws.tables.empty())
        tables->addSeparator();
    tables->addAction(icons::obtient("plus"), u"Nouveau tableur"_s, owner, [&win, id] { win.nouveau_tableur({id}); });
    // graphes
    QMenu *graphs = menu->addMenu(icons::obtient("chart"), u"Tracer dans un graphe"_s);
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
            label = u"%1 : %2 en fonction de %3"_s.arg(g.name, name, store.ws.doc.nom(*x));
        } else {
            label = u"%1 : %2 en abscisse"_s.arg(g.name, name);
        }
        const ViewId gid = g.id;
        auto *a = graphs->addAction(icons::obtient("chart"), label, owner, [&win, gid, pi, id] {
            graphops::depose_grandeur(win, gid, pi, id, std::nullopt);
            win.ouvre_onglet({TabKey::Graph, gid, {}});
        });
        a->setEnabled(enabled);
    }
    if (!store.ws.graphs.empty())
        graphs->addSeparator();
    QMenu *newGraph = graphs->addMenu(icons::obtient("plus"), u"Nouveau graphe : %1 en fonction de"_s.arg(name));
    for (const auto &o : store.ws.doc.quantities) {
        if (o.id == id)
            continue;
        const QId xid = o.id;
        newGraph->addAction(icons::obtient("scatter"), o.name, owner, [&win, xid, id] { win.nouveau_graphe(std::make_pair(xid, id)); });
    }
    newGraph->setEnabled(store.ws.doc.quantities.size() > 1);
    menu->addAction(icons::obtient("stats"), u"Statistiques"_s, owner, [&win, id] { win.ouvre_statistiques(id); });
    if (!calculated)
        menu->addAction(icons::obtient("numbers"), u"Remplir avec une suite de valeurs…"_s, owner, [&win, id] { dialogs::remplit(win, id); });
    if (calculated) {
        menu->addAction(icons::obtient("function"), u"Modifier la formule"_s, owner, [&win] { win.ouvre_onglet({TabKey::Calculations, 0, {}}); });
        auto *freeze = menu->addAction(icons::obtient("snowflake"), u"Figer les valeurs"_s, owner,
                                       [&store, id] { store.edite([&](Seance &ws) { ws.doc.fige(id); }); });
        freeze->setToolTip(u"La grandeur devient expérimentale : ses valeurs ne sont plus recalculées"_s);
    }
    menu->addSeparator();
    menu->addAction(icons::obtient("trash"), u"Supprimer…"_s, owner, [&win, id] { dialogs::confirme_suppression(win, id); });
}
