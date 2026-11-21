// Fenêtres de travail du mode Expert. Leur identifiant sert aussi à retrouver
// la disposition des tableurs et des graphes à la réouverture de la séance.
#include "app/mainwindow.h"

#include "app/calculationspanel.h"
#include "app/docking.h"
#include "app/drawingview.h"
#include "app/graphwidget.h"
#include "app/guide.h"
#include "app/modelingpanel.h"
#include "app/quantitiespanel.h"
#include "app/scriptpanel.h"
#include "app/statspanel.h"
#include "app/tableview.h"
#include "app/theme.h"
#include "app/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyleOptionDockWidget>
#include <QStylePainter>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <climits>
#include <numeric>
#include <set>

using namespace Qt::StringLiterals;

namespace {

/// Colonnes de la disposition : Grandeurs à gauche, tableurs et scripts au centre, graphes à
/// droite, modélisation et statistiques tout à droite ; on glisse ainsi une grandeur de l'une à
/// l'autre.
enum class Zone { Left, Center, Right, Side };

Zone zone_de(TabKey::Kind k)
{
    switch (k) {
    case TabKey::Quantities:
    case TabKey::Calculations: return Zone::Left;
    case TabKey::Table:
    case TabKey::Script: return Zone::Center;
    case TabKey::Graph:
    case TabKey::Drawing: return Zone::Right;
    case TabKey::Modeling:
    case TabKey::Statistics: return Zone::Side;
    }
    return Zone::Center;
}

/// Part de la largeur donnée à chaque colonne.
int part_de(Zone z)
{
    switch (z) {
    case Zone::Left: return 22;
    case Zone::Center: return 38;
    case Zone::Right: return 40;
    case Zone::Side: return 28;
    }
    return 30;
}

/// Largeur minimale d'une fenêtre pour que son contenu reste lisible, à 100 % (`theme::px`).
int largeur_min(TabKey::Kind k)
{
    switch (k) {
    case TabKey::Quantities:
    case TabKey::Calculations: return 270;
    case TabKey::Table: return 300;
    case TabKey::Graph:
    case TabKey::Drawing: return 340;
    case TabKey::Script: return 380;
    default: return 320;
    }
}

/// Fenêtres de réglages et de résultats : fond des panneaux latéraux de la suite.
bool est_panneau(TabKey::Kind k)
{
    switch (k) {
    case TabKey::Quantities:
    case TabKey::Calculations:
    case TabKey::Modeling:
    case TabKey::Statistics: return true;
    default: return false;
    }
}

} // namespace

const char *TabKey::icone() const
{
    switch (kind) {
    case TabKey::Quantities: return "list";
    case TabKey::Calculations: return "function";
    case TabKey::Table: return "table";
    case TabKey::Graph: return "chart";
    case TabKey::Script: return "python";
    case TabKey::Drawing: return "paint";
    case TabKey::Modeling: return "model";
    case TabKey::Statistics: return "stats";
    }
    return "dots";
}

// --- TabKey, Dock ------------------------------------------------------------------------------

QString TabKey::texte() const
{
    switch (kind) {
    case Quantities: return u"quantities"_s;
    case Calculations: return u"calculations"_s;
    case Table: return u"table:%1"_s.arg(id);
    case Graph: return u"graph:%1"_s.arg(id);
    case Script: return u"script:%1"_s.arg(id);
    case Drawing: return u"drawing:"_s + name;
    case Modeling: return u"modeling"_s;
    case Statistics: return u"statistics"_s;
    }
    return {};
}

std::optional<TabKey> TabKey::analyse(const QString &s)
{
    static const std::vector<std::pair<QString, Kind>> simple = {
        {u"quantities"_s, Quantities}, {u"calculations"_s, Calculations}, {u"modeling"_s, Modeling}, {u"statistics"_s, Statistics}};
    for (const auto &[n, k] : simple)
        if (s == n)
            return TabKey{k};
    const qsizetype colon = s.indexOf(u':');
    if (colon < 0)
        return std::nullopt;
    const QString head = s.left(colon), rest = s.mid(colon + 1);
    if (head == u"drawing")
        return TabKey{Drawing, 0, rest};
    bool ok = false;
    const ViewId id = rest.toULongLong(&ok);
    if (!ok)
        return std::nullopt;
    if (head == u"table")
        return TabKey{Table, id};
    if (head == u"graph")
        return TabKey{Graph, id};
    if (head == u"script")
        return TabKey{Script, id};
    return std::nullopt;
}

Dock::Dock(const TabKey &k, QWidget *content, QWidget *parent) : QDockWidget(parent), key(k)
{
    setObjectName(k.texte());
    setWidget(content);
    setWindowIcon(icons::obtient(k.icone()));
    // ni « DockWidgetMovable » ni « DockWidgetFloatable » : Qt ne déplace ni ne détache plus la
    // fenêtre lui-même ; c'est le glissement par les repères, et le bouton Détacher de la barre
    setFeatures(QDockWidget::DockWidgetClosable);
    m_title = new DockTitle(this);
    setTitleBarWidget(m_title);
    // largeur minimale relative à la police (elle suit l'agrandissement de l'interface), et jamais
    // moins que celle que demande le contenu
    const int own = theme::largeur_min_base(content);
    theme::definit_largeur_min(content, own > 0 ? std::min(own, largeur_min(k.kind)) : largeur_min(k.kind));
    m_panel = est_panneau(k.kind);
    if (m_panel)
        couleur_panneau_modele();
}

void Dock::definit_detache(bool on)
{
    if (on == m_detached)
        return;
    m_detached = on;
    m_title->definit_detache(on);
    // fenêtre à part : le cadre est celui du système (déplacer, agrandir, fermer), sous Wayland
    // comme ailleurs ; elle reste liée à la fenêtre principale (au-dessus d'elle, fermée avec elle)
    setWindowFlags(on ? Qt::Window : Qt::Widget);
}

void Dock::couleur_panneau_modele()
{
    // Fond peint par `paintEvent` (barre de titre comprise), le contenu étant transparent. En
    // sombre, Window = Light dans la palette locale : Fusion dessine le bord des champs (cases,
    // listes, tableaux) d'après Window, et sur le fond sombre du panneau il disparaîtrait.
    const QColor c = theme::est_sombre() ? QApplication::palette().color(QPalette::Light) : theme::couleur_panneau();
    if (palette().color(QPalette::Window) == c)
        return;
    // seul ce rôle est imposé : les autres couleurs suivent toujours la palette du thème
    QPalette p;
    p.setColor(QPalette::Window, c);
    setPalette(p);
}

void Dock::paintEvent(QPaintEvent *)
{
    // le titre est dessiné par sa barre
    if (m_panel)
        QPainter(this).fillRect(rect(), theme::couleur_panneau());
}

void Dock::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (m_panel && event->type() == QEvent::PaletteChange)
        couleur_panneau_modele();
    if (event->type() == QEvent::WindowTitleChange) {
        m_title->updateGeometry();
        m_title->update();
    }
}

bool Dock::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        // barre de titre (les boutons Détacher et Fermer reçoivent leurs propres clics) ; une
        // fenêtre détachée se déplace par le cadre du système
        if (me->button() == Qt::LeftButton && !m_detached && widget() && me->position().y() < widget()->geometry().top()) {
            m_press = me->position().toPoint();
            return true;
        }
        break;
    }
    case QEvent::MouseMove:
        if (m_press) {
            auto *me = static_cast<QMouseEvent *>(event);
            if ((me->position().toPoint() - *m_press).manhattanLength() >= QApplication::startDragDistance()) {
                m_press.reset();
                Q_EMIT glisser_demande(this, me->globalPosition().toPoint());
            }
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        if (m_press) {
            m_press.reset();
            return true;
        }
        break;
    default:
        break;
    }
    return QDockWidget::event(event);
}

void Dock::closeEvent(QCloseEvent *event)
{
    // cadre d'une fenêtre détachée : la fenêtre principale demande confirmation, puis la détruit
    event->ignore();
    Q_EMIT fermeture_demandee(this);
}

// --- DockTitle ---------------------------------------------------------------------------------

DockTitle::DockTitle(Dock *dock) : QWidget(dock), m_dock(dock)
{
    auto *layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->addStretch(1);
    auto button = [this, layout](const QString &name) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(b);
        return b;
    };
    m_detach = button(u"detachButton"_s);
    m_close = button(u"closeButton"_s);
    m_close->setIcon(icons::obtient("close"));
    m_close->setToolTip(u"Fermer la fenêtre"_s);
    connect(m_detach, &QToolButton::clicked, this, [this] { Q_EMIT m_dock->detachement_demande(m_dock, !m_detached); });
    connect(m_close, &QToolButton::clicked, this, [this] { Q_EMIT m_dock->fermeture_demandee(m_dock); });
    definit_detache(false);
}

void DockTitle::definit_detache(bool on)
{
    m_detached = on;
    // détachée : « Rattacher » en toutes lettres (la place ne manque pas), Fermer est dans le cadre
    m_detach->setIcon(icons::obtient(on ? "attach" : "detach"));
    m_detach->setText(on ? u"Rattacher"_s : QString());
    m_detach->setToolButtonStyle(on ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    m_detach->setToolTip(on ? u"Remettre cette fenêtre dans la fenêtre principale de MeVReg"_s
                            : u"Détacher : ouvrir dans une fenêtre à part, que l'on peut placer sur un autre écran"_s);
    m_close->setVisible(!on);
    ajuste();
    update();
}

void DockTitle::ajuste()
{
    // icônes de la hauteur d'une ligne de texte, boutons un peu plus grands : ils suivent la
    // police, donc l'agrandissement de l'interface
    const int line = fontMetrics().height();
    const int pad = std::max(4, line / 3);
    for (QToolButton *b : {m_detach, m_close}) {
        b->setIconSize(QSize(line, line));
        if (b->toolButtonStyle() == Qt::ToolButtonIconOnly)
            b->setFixedSize(line + 2 * pad, line + pad);
        else {
            b->setMinimumSize(0, 0);
            b->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            b->setFixedHeight(line + pad);
        }
    }
    layout()->setContentsMargins(theme::px(8), 2, 2, 2);
    updateGeometry();
}

QSize DockTitle::sizeHint() const
{
    const QMargins m = layout()->contentsMargins();
    const int h = std::max(fontMetrics().height() + theme::px(8), m_detach->sizeHint().height()) + m.top() + m.bottom();
    return {layout()->sizeHint().width() + fontMetrics().horizontalAdvance(m_dock->windowTitle()), h};
}

QSize DockTitle::minimumSizeHint() const
{
    return {layout()->minimumSize().width() + fontMetrics().averageCharWidth() * 4, sizeHint().height()};
}

void DockTitle::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        ajuste();
}

void DockTitle::paintEvent(QPaintEvent *)
{
    // fenêtre détachée : le titre est dans le cadre du système
    if (m_detached)
        return;
    // titre en demi-gras, à la taille normale, comme ceux des panneaux de la suite
    QPainter p(this);
    QFont f = font();
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const int left = layout()->contentsMargins().left();
    const int right = (m_detach->isVisible() ? m_detach->geometry().left() : width()) - theme::px(6);
    const QRect r(left, 0, std::max(0, right - left), height());
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(m_dock->windowTitle(), Qt::ElideRight, r.width()));
}

QMenu *MainWindow::createPopupMenu()
{
    Dock *dock = fenetre_en(QCursor::pos());
    if (!dock)
        return nullptr;
    auto *menu = new QMenu(this);
    menu_fenetre(menu, dock);
    return menu;
}

Dock *MainWindow::fenetre_en(QPoint globalPos) const
{
    QWidget *w = QApplication::widgetAt(globalPos);
    // onglet d'une pile de fenêtres : la fenêtre est rangée dans les données de l'onglet
    if (auto *tb = qobject_cast<QTabBar *>(w)) {
        const int i = tb->tabAt(tb->mapFromGlobal(globalPos));
        if (i >= 0) {
            auto *d = reinterpret_cast<QWidget *>(tb->tabData(i).value<quintptr>());
            for (const auto &[k, p] : m_docks)
                if (p && static_cast<QWidget *>(p.data()) == d)
                    return p.data();
        }
        return nullptr;
    }
    for (; w; w = w->parentWidget())
        if (auto *d = qobject_cast<Dock *>(w))
            return d;
    return nullptr;
}

void MainWindow::menu_fenetre(QMenu *menu, Dock *dock)
{
    const TabKey key = dock->key;
    auto rename = [this, key](const QString &title, const QString &current, const std::function<void(Seance &, const QString &)> &apply) {
        bool ok = false;
        const QString n = QInputDialog::getText(this, title, u"Nom :"_s, QLineEdit::Normal, current, &ok).trimmed();
        if (ok && !n.isEmpty() && n != current)
            m_store.edite([&](Seance &ws) { apply(ws, n); });
    };
    switch (key.kind) {
    case TabKey::Table:
        if (const TableView *t = m_store.ws.tableur(key.id)) {
            const QString cur = t->name;
            menu->addAction(icons::obtient("pencil"), u"Renommer le tableur…"_s, this, [rename, key, cur] {
                rename(u"Renommer le tableur"_s, cur, [key](Seance &ws, const QString &n) { ws.tableur_modifiable(key.id)->name = n; });
            });
            QAction *del = menu->addAction(icons::obtient("trash"), u"Supprimer ce tableur"_s, this, [this, key] { supprime_fenetre(key); });
            del->setStatusTip(u"Les grandeurs restent dans le document"_s);
        }
        break;
    case TabKey::Graph:
        if (const GraphView *g = m_store.ws.graphe(key.id)) {
            const QString cur = g->name;
            menu->addAction(icons::obtient("pencil"), u"Renommer le graphe…"_s, this, [rename, key, cur] {
                rename(u"Renommer le graphe"_s, cur, [key](Seance &ws, const QString &n) { ws.graphe_modifiable(key.id)->name = n; });
            });
            menu->addAction(icons::obtient("trash"), u"Supprimer ce graphe"_s, this, [this, key] { supprime_fenetre(key); });
        }
        break;
    case TabKey::Script:
        if (const Script *s = m_store.ws.script(key.id)) {
            const QString cur = s->name;
            menu->addAction(icons::obtient("pencil"), u"Renommer le script…"_s, this, [rename, key, cur] {
                rename(u"Renommer le script"_s, cur, [key](Seance &ws, const QString &n) { ws.script_modifiable(key.id)->name = n; });
            });
            menu->addAction(icons::obtient("trash"), u"Supprimer ce script"_s, this, [this, key] { supprime_fenetre(key); });
        }
        break;
    default:
        break;
    }
    if (!menu->isEmpty())
        menu->addSeparator();
    const QPointer<Dock> d(dock);
    if (dock->detache())
        menu->addAction(icons::obtient("attach"), u"Rattacher à la fenêtre principale"_s, this, [this, d] { rattache_fenetre(d); });
    else
        menu->addAction(icons::obtient("detach"), u"Détacher dans une fenêtre à part"_s, this, [this, d] { detache_fenetre(d); });
    menu->addAction(icons::obtient("eye-off"), u"Cacher la fenêtre"_s, this, [this, d] {
        if (d)
            retire_fenetre(d);
    })->setStatusTip(u"Elle se retrouve dans le menu Fenêtres"_s);
    menu->addAction(icons::obtient("close"), u"Fermer la fenêtre…"_s, this, [this, d] { demande_fermeture(d); });
}

// --- fenêtres ancrées --------------------------------------------------------------------------

QWidget *MainWindow::cree_contenu(const TabKey &key)
{
    switch (key.kind) {
    case TabKey::Quantities: return new QuantitiesPanel(*this);
    case TabKey::Calculations: return new CalculationsPanel(*this);
    case TabKey::Table: return new TablePanel(*this, key.id);
    case TabKey::Graph: return new GraphPanel(*this, key.id);
    case TabKey::Script: return new ScriptPanel(*this, key.id);
    case TabKey::Drawing: return new DrawingView(*this, key.name);
    case TabKey::Modeling: return new ModelingPanel(*this);
    case TabKey::Statistics: return new StatsPanel(*this);
    }
    return new QWidget;
}

bool MainWindow::est_ouvert(const TabKey &key) const
{
    auto it = m_docks.find(key.texte());
    return it != m_docks.end() && it->second;
}

QList<TabKey> MainWindow::onglets_ouverts() const
{
    QList<TabKey> out;
    for (const auto &[k, d] : m_docks)
        if (d)
            out << d->key;
    return out;
}

void MainWindow::ouvre_onglet(const TabKey &key)
{
    if (m_modeSimple) {
        if (key.kind == TabKey::Table && m_store.ws.tableur(key.id))
            m_guide->ouvre_tableur(key.id);
        else if (key.kind == TabKey::Graph && m_store.ws.graphe(key.id))
            m_guide->ouvre_graphe(key.id);
        else
            affiche_statut(u"Cette fenêtre est accessible dans Mode › Expert."_s);
        return;
    }
    if (key.kind == TabKey::Table && !m_store.ws.tableur(key.id))
        return;
    if (key.kind == TabKey::Graph && !m_store.ws.graphe(key.id))
        return;
    if (key.kind == TabKey::Script && !m_store.ws.script(key.id))
        return;
    if (auto it = m_docks.find(key.texte()); it != m_docks.end() && it->second) {
        Dock *d = it->second;
        d->show();
        d->raise();
        if (d->detache())
            d->activateWindow();
        d->widget()->setFocus();
        return;
    }
    Dock *dock = cree_fenetre(key);
    m_docks[key.texte()] = dock;
    // Modélisation : dans une fenêtre à part, à droite ; une colonne de plus ne laissait plus rien
    // voir du graphe et des tableurs (elle se rattache par son bouton)
    if (key.kind == TabKey::Modeling && isVisible()) {
        QSize size = theme::px(QSize(560, 700)).expandedTo(dock->minimumSizeHint());
        if (const QScreen *s = screen())
            size = size.boundedTo(s->availableGeometry().size() - QSize(40, 80));
        const QRect frame = frameGeometry();
        detache_fenetre(dock, QRect(QPoint(frame.right() - size.width() - theme::px(24), frame.top() + theme::px(80)), size));
        maj_titres_fenetres();
        return;
    }
    place(dock);
    maj_titres_fenetres();
    maj_indication_vide();
    dock->show();
    dock->raise();
}

void MainWindow::ferme_onglet(const TabKey &key)
{
    if (auto it = m_docks.find(key.texte()); it != m_docks.end() && it->second)
        retire_fenetre(it->second);
}

void MainWindow::cache_onglet(const TabKey &key)
{
    // la fenêtre s'en va, son contenu reste dans le document
    auto it = m_docks.find(key.texte());
    if (it == m_docks.end() || !it->second)
        return;
    const QString title = it->second->windowTitle();
    retire_fenetre(it->second);
    m_store.message(u"Fenêtre « %1 » cachée ; menu Fenêtres pour la réafficher"_s.arg(title));
}

Dock *MainWindow::cree_fenetre(const TabKey &key)
{
    auto *dock = new Dock(key, cree_contenu(key), this);
    connect(dock, &Dock::fermeture_demandee, this, &MainWindow::demande_fermeture);
    connect(dock, &Dock::glisser_demande, this, &MainWindow::commence_glisser_fenetre);
    connect(dock, &Dock::detachement_demande, this, [this](Dock *d, bool detach) {
        if (detach)
            detache_fenetre(d);
        else
            rattache_fenetre(d);
    });
    return dock;
}

void MainWindow::demande_fermeture(Dock *dock)
{
    if (!dock)
        return;
    const TabKey key = dock->key;
    QString lost;
    switch (key.kind) {
    case TabKey::Table: lost = u"le tableur est supprimé (ses grandeurs restent dans le document)"_s; break;
    case TabKey::Graph: lost = u"le graphe est supprimé, avec ses réglages"_s; break;
    case TabKey::Script: lost = u"le script est supprimé (les grandeurs qu'il a créées restent)"_s; break;
    case TabKey::Drawing: lost = u"le dessin est supprimé"_s; break;
    default:
        // Grandeurs, Calculs, Modélisation, Statistiques, Notice : rien n'est perdu, la barre
        // d'outils, le menu Fenêtres ou les menus les rouvrent tels quels
        retire_fenetre(dock);
        return;
    }
    const QString title = dock->windowTitle();
    if (!m_store.prefs.confirmClose) {
        retire_fenetre(dock);
        m_store.message(u"« %1 » est caché ; menu Fenêtres pour le retrouver"_s.arg(title));
        return;
    }
    // la fenêtre détachée porte la question (elle peut être sur un autre écran)
    QWidget *owner = dock->detache() ? static_cast<QWidget *>(dock) : this;
    // espaces insécables dans les guillemets : le nom n'est pas coupé en fin de ligne
    const QString name = u"« "_s + title + u" »"_s;
    auto *box = new QMessageBox(QMessageBox::Question, u"Cacher ou fermer "_s + name, u"Cacher ou fermer la fenêtre %1 ?"_s.arg(name),
                                QMessageBox::NoButton, owner);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setObjectName(u"confirmClose"_s);
    box->setInformativeText(u"Cachée, elle se retrouve dans le menu Fenêtres → %1.\nFermée, %2 (Ctrl+Z pour annuler)."_s.arg(name, lost));
    QPushButton *hide = box->addButton(u"Cacher"_s, QMessageBox::AcceptRole);
    hide->setIcon(icons::obtient("eye-off"));
    hide->setObjectName(u"hide"_s);
    QPushButton *close = box->addButton(u"Fermer"_s, QMessageBox::DestructiveRole);
    close->setIcon(icons::obtient("close"));
    close->setObjectName(u"close"_s);
    box->addButton(u"Annuler"_s, QMessageBox::RejectRole)->setIcon(icons::obtient("close"));
    box->setDefaultButton(hide);
    auto *never = new QCheckBox(u"Toujours cacher sans demander"_s, box);
    never->setToolTip(u"La question revient avec Édition → Préférences"_s);
    box->setCheckBox(never);
    const QPointer<Dock> target(dock);
    connect(box, &QMessageBox::buttonClicked, this, [this, hide, close, never, target, key](QAbstractButton *clicked) {
        if (clicked == hide) {
            if (never->isChecked()) {
                m_store.prefs.confirmClose = false;
                m_store.prefs.enregistre();
            }
            if (target)
                retire_fenetre(target);
        } else if (clicked == close) {
            supprime_fenetre(key);
        }
    });
    box->open();
}

void MainWindow::supprime_fenetre(const TabKey &key)
{
    ferme_onglet(key);
    switch (key.kind) {
    case TabKey::Table:
        m_store.edite([&](Seance &ws) { std::erase_if(ws.tables, [&](const TableView &t) { return t.id == key.id; }); });
        m_store.message(u"Tableur supprimé ; ses grandeurs restent dans le document (Ctrl+Z pour annuler)"_s);
        break;
    case TabKey::Graph:
        m_store.edite([&](Seance &ws) { std::erase_if(ws.graphs, [&](const GraphView &g) { return g.id == key.id; }); });
        m_store.message(u"Graphe supprimé (Ctrl+Z pour annuler)"_s);
        break;
    case TabKey::Script:
        m_store.arrete_script(key.id);
        m_store.edite([&](Seance &ws) { std::erase_if(ws.scripts, [&](const Script &s) { return s.id == key.id; }); });
        m_store.message(u"Script supprimé ; les grandeurs qu'il a créées restent dans le document (Ctrl+Z pour annuler)"_s);
        break;
    case TabKey::Drawing:
        m_store.drawings.erase(key.name);
        m_store.edite([&](Seance &ws) {
            for (auto &s : ws.scripts)
                s.dessins.removeAll(key.name);
        });
        m_store.message(u"Dessin « %1 » supprimé ; le script le refait à sa prochaine exécution"_s.arg(key.name));
        break;
    default:
        break;
    }
}

void MainWindow::elargit_fenetre(QDockWidget *dock, int width)
{
    if (!dock)
        return;
    if (dock->isWindow()) {
        dock->resize(std::max(dock->width(), width), dock->height());
        return;
    }
    // colonnes de la disposition : celle de la fenêtre prend ce que les autres peuvent céder
    const docking::Node tree = docking::capture(this, fenetres_ancrees());
    if (tree.feuille() || tree.orientation != Qt::Horizontal)
        return;
    const auto in = [dock](const docking::Node &n) {
        const auto ds = docking::fenetres_dans(n);
        return std::find(ds.begin(), ds.end(), dock) != ds.end();
    };
    const auto least = [](const docking::Node &n) {
        int m = 0;
        for (QDockWidget *d : docking::fenetres_dans(n))
            m = std::max(m, std::max(d->minimumWidth(), d->minimumSizeHint().width()));
        return m;
    };
    const size_t k = static_cast<size_t>(std::find_if(tree.children.begin(), tree.children.end(), in) - tree.children.begin());
    if (k >= tree.children.size())
        return;
    const int need = width - tree.children[k].rect.width();
    if (need <= 0)
        return;
    int spare = 0;
    for (size_t i = 0; i < tree.children.size(); ++i)
        if (i != k)
            spare += std::max(0, tree.children[i].rect.width() - least(tree.children[i]));
    const int take = std::min(need, spare);
    if (take <= 0)
        return;
    QList<QDockWidget *> docks;
    QList<int> sizes;
    for (size_t i = 0; i < tree.children.size(); ++i) {
        const docking::Node &c = tree.children[i];
        int w = c.rect.width();
        if (i == k)
            w += take;
        else if (spare > 0)
            w -= static_cast<int>(std::lround(static_cast<double>(take) * std::max(0, w - least(c)) / spare));
        docks << docking::fenetres_dans(c).front();
        sizes << w;
    }
    resizeDocks(docks, sizes, Qt::Horizontal);
}

void MainWindow::detache_fenetre(Dock *dock, std::optional<QRect> geometry)
{
    if (!dock || dock->detache() || !est_ouvert(dock->key))
        return;
    // à peu près à sa place à l'écran (Wayland laisse le système choisir), au moins assez grande
    const QSize size = geometry ? geometry->size() : dock->size().expandedTo(theme::px(QSize(480, 360)));
    const QPoint at = geometry ? geometry->topLeft() : dock->mapToGlobal(QPoint(theme::px(24), theme::px(24)));
    removeDockWidget(dock);
    dock->definit_detache(true);
    // raccourcis de la fenêtre principale (annuler, enregistrer, agrandissement, notice…) actifs
    // aussi dans la fenêtre détachée
    dock->addActions(raccourcis_fenetres());
    dock->resize(size);
    dock->move(at);
    dock->show();
    dock->raise();
    dock->activateWindow();
    maj_indication_vide();
    m_store.message(u"« %1 » est dans une fenêtre à part : bouton « Rattacher » pour la remettre ici"_s.arg(dock->windowTitle()));
}

void MainWindow::rattache_fenetre(Dock *dock)
{
    if (!dock || !dock->detache())
        return;
    for (QAction *a : raccourcis_fenetres())
        dock->removeAction(a);
    dock->definit_detache(false);
    // à la place que lui donne son genre, comme une fenêtre ouverte
    place(dock);
    dock->show();
    dock->raise();
    maj_indication_vide();
    activateWindow();
}

QList<QAction *> MainWindow::raccourcis_fenetres() const
{
    QList<QAction *> out;
    std::function<void(const QList<QAction *> &)> walk = [&](const QList<QAction *> &actions) {
        for (QAction *a : actions) {
            if (a->menu())
                walk(a->menu()->actions());
            else if (!a->shortcuts().isEmpty() && !out.contains(a))
                out << a;
        }
    };
    walk(menuBar()->actions());
    walk(actions());
    return out;
}

void MainWindow::retire_fenetre(Dock *dock)
{
    // le texte d'un script en cours de saisie est rangé dans le document avant
    if (auto *sp = qobject_cast<ScriptPanel *>(dock->widget()))
        sp->transmet_saisie();
    for (auto it = m_docks.begin(); it != m_docks.end();) {
        if (!it->second || it->second == dock)
            it = m_docks.erase(it);
        else
            ++it;
    }
    removeDockWidget(dock);
    dock->hide();
    dock->deleteLater();
    maj_indication_vide();
}

void MainWindow::ferme_toutes_fenetres()
{
    std::vector<Dock *> all;
    for (const auto &[k, d] : m_docks)
        if (d)
            all.push_back(d);
    m_docks.clear();
    for (Dock *d : all) {
        removeDockWidget(d);
        d->hide();
        // détruites tout de suite : les panneaux ne doivent plus voir l'ancien espace de travail
        delete d;
    }
    maj_indication_vide();
}

void MainWindow::place(Dock *dock)
{
    const TabKey &key = dock->key;
    const Zone zone = zone_de(key.kind);
    // fenêtres ancrées (pas les fenêtres flottantes), sauf la nouvelle
    std::vector<Dock *> docked;
    for (const auto &[k, d] : m_docks)
        if (d && d != dock && !d->isFloating() && dockWidgetArea(d) != Qt::NoDockWidgetArea)
            docked.push_back(d);
    if (docked.empty()) {
        addDockWidget(Qt::LeftDockWidgetArea, dock);
        return;
    }
    // à côté d'une fenêtre du même genre si possible, sinon de la même colonne
    auto find = [&](const std::function<bool(const TabKey &)> &pred) -> Dock * {
        for (Dock *d : docked)
            if (pred(d->key))
                return d;
        return nullptr;
    };
    Dock *sibling = find([&](const TabKey &k) { return k.kind == key.kind; });
    if (!sibling) {
        switch (zone) {
        case Zone::Left:
        case Zone::Side:
            sibling = find([&](const TabKey &k) { return zone_de(k.kind) == zone; });
            break;
        case Zone::Center:
            // un script ou l'aide s'ouvre en onglet sur les tableurs
            sibling = find([&](const TabKey &k) { return k.kind == TabKey::Table; });
            if (!sibling)
                sibling = find([&](const TabKey &k) { return zone_de(k.kind) == Zone::Center; });
            break;
        case Zone::Right:
            sibling = find([&](const TabKey &k) { return zone_de(k.kind) == Zone::Right; });
            break;
        }
    }
    // pas la place d'une colonne de plus : Modélisation et Statistiques s'ouvrent en onglet sur
    // les tableurs, pour garder le graphe visible
    if (!sibling && zone == Zone::Side) {
        std::set<Zone> zones;
        int need = theme::px(largeur_min(key.kind));
        for (Dock *d : docked) {
            if (zones.insert(zone_de(d->key.kind)).second)
                need += theme::px(largeur_min(d->key.kind));
        }
        if (need > width() - 20) {
            sibling = find([&](const TabKey &k) { return zone_de(k.kind) == Zone::Center; });
            if (!sibling)
                sibling = find([&](const TabKey &k) { return zone_de(k.kind) == Zone::Left; });
        }
    }
    if (sibling) {
        tabifyDockWidget(sibling, dock);
        return;
    }
    // une colonne de plus, à sa place dans l'ordre des colonnes ; les autres fenêtres gardent la
    // disposition choisie et se partagent le reste de la largeur
    docking::Node tree = docking::capture(this, std::vector<QDockWidget *>(docked.begin(), docked.end()));
    if (!isVisible() || docking::fenetres_dans(tree).empty()) {
        redispose();
        return;
    }
    auto zoneRank = [](const docking::Node &n) {
        int z = INT_MAX;
        for (QDockWidget *d : docking::fenetres_dans(n))
            if (auto *dk = qobject_cast<Dock *>(d))
                z = std::min(z, static_cast<int>(zone_de(dk->key.kind)));
        return z;
    };
    std::vector<int> columns;
    if (!tree.feuille() && tree.orientation == Qt::Horizontal)
        for (const docking::Node &c : tree.children)
            columns.push_back(zoneRank(c));
    else
        columns.push_back(zoneRank(tree));
    const auto after = std::find_if(columns.begin(), columns.end(), [&](int z) { return z > static_cast<int>(zone); });
    int shares = part_de(zone);
    for (int z : columns)
        shares += part_de(static_cast<Zone>(std::clamp(z, 0, static_cast<int>(Zone::Side))));
    const int width = std::max(theme::px(largeur_min(key.kind)), tree.rect.width() * part_de(zone) / shares);
    docking::insere_colonne(tree, dock, static_cast<size_t>(after - columns.begin()), width, style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, this));
    docking::applique(this, tree);
}

std::vector<QDockWidget *> MainWindow::fenetres_ancrees() const
{
    std::vector<QDockWidget *> out;
    for (const auto &[k, d] : m_docks)
        if (d && !d->isFloating() && dockWidgetArea(d) != Qt::NoDockWidgetArea)
            out.push_back(d);
    return out;
}

void MainWindow::commence_glisser_fenetre(Dock *dock, QPoint globalPos)
{
    if (!dock || dock->isFloating() || m_dropOverlay)
        return;
    const std::vector<QDockWidget *> docked = fenetres_ancrees();
    if (docked.size() < 2 || std::find(docked.begin(), docked.end(), dock) == docked.end())
        return;
    auto *overlay = new docking::Overlay(this, dock, docked);
    m_dropOverlay = overlay;
    QPointer<Dock> d(dock);
    overlay->onDrop = [this, d](const docking::Target &t) {
        if (d)
            deplace_fenetre(d, t);
    };
    overlay->commence(globalPos);
}

bool MainWindow::deplace_fenetre(Dock *dock, const docking::Target &target)
{
    docking::Node tree = docking::capture(this, fenetres_ancrees());
    if (!docking::deplace(tree, dock, target, dock->size(), style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, this)))
        return false;
    docking::applique(this, tree);
    dock->raise();
    if (dock->widget())
        dock->widget()->setFocus();
    return true;
}

void MainWindow::childEvent(QChildEvent *event)
{
    QMainWindow::childEvent(event);
    // barres d'onglets des piles de fenêtres, créées par Qt au besoin
    if (event->type() == QEvent::ChildPolished)
        if (auto *bar = qobject_cast<QTabBar *>(event->child()))
            bar->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // le message a changé de largeur : il tient peut-être sur une ligne de plus ou de moins
    if (watched == m_statusLabel && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &MainWindow::ajuste_barre_etat);
    auto *bar = qobject_cast<QTabBar *>(watched);
    if (!bar || bar->parent() != this)
        return QMainWindow::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        m_tabPress.reset();
        const int i = me->button() == Qt::LeftButton ? bar->tabAt(me->position().toPoint()) : -1;
        if (i >= 0) {
            // la fenêtre de l'onglet est rangée dans ses données
            const auto *w = reinterpret_cast<const QWidget *>(bar->tabData(i).value<quintptr>());
            for (const auto &[k, d] : m_docks)
                if (d && static_cast<const QWidget *>(d.data()) == w)
                    m_tabPress = TabPress{bar, d.data()};
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!m_tabPress || m_tabPress->bar != bar || !(me->buttons() & Qt::LeftButton))
            break;
        // dans sa barre, l'onglet change de place parmi les autres ; tiré hors de la barre, il
        // déplace sa fenêtre
        const int margin = 2 * QApplication::startDragDistance();
        if (bar->rect().adjusted(-margin, -margin, margin, margin).contains(me->position().toPoint()))
            break;
        const QPointer<Dock> dock = m_tabPress->dock;
        m_tabPress.reset();
        QMouseEvent release(QEvent::MouseButtonRelease, me->position(), me->globalPosition(), Qt::LeftButton, Qt::NoButton, me->modifiers());
        QApplication::sendEvent(bar, &release);
        if (dock)
            commence_glisser_fenetre(dock, me->globalPosition().toPoint());
        return true;
    }
    case QEvent::MouseButtonRelease:
        m_tabPress.reset();
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::redispose()
{
    std::map<Zone, std::vector<Dock *>> columns;
    std::vector<Dock *> all;
    // l'ordre des colonnes est fixe ; dans une colonne, les onglets sont rangés par genre puis par ordre de création
    for (const auto &[k, d] : m_docks) {
        if (!d || d->isFloating())
            continue;
        all.push_back(d);
    }
    std::stable_sort(all.begin(), all.end(), [](Dock *a, Dock *b) {
        return static_cast<int>(a->key.kind) < static_cast<int>(b->key.kind) || (a->key.kind == b->key.kind && a->key.id < b->key.id);
    });
    for (Dock *d : all)
        columns[zone_de(d->key.kind)].push_back(d);
    for (Dock *d : all)
        removeDockWidget(d);
    Dock *prev = nullptr;
    QList<QDockWidget *> heads;
    QList<int> widths;
    for (const auto &[zone, docks] : columns) {
        if (docks.empty())
            continue;
        Dock *head = docks.front();
        if (!prev)
            addDockWidget(Qt::LeftDockWidgetArea, head);
        else
            splitDockWidget(prev, head, Qt::Horizontal);
        prev = head;
        heads << head;
        widths << part_de(zone);
    }
    for (const auto &[zone, docks] : columns)
        for (size_t k = 1; k < docks.size(); ++k)
            tabifyDockWidget(docks.front(), docks[k]);
    for (Dock *d : all)
        d->show();
    for (const auto &[zone, docks] : columns)
        if (!docks.empty())
            docks.front()->raise();
    if (heads.size() > 1) {
        const int total = std::accumulate(widths.begin(), widths.end(), 0);
        QList<int> px;
        for (int w : widths)
            px << std::max(theme::px(200), width() * w / total);
        resizeDocks(heads, px, Qt::Horizontal);
    }
}

void MainWindow::disposition_par_defaut()
{
    if (m_modeSimple) {
        m_dispositionExpert = {};
        m_guide->rafraichit();
        return;
    }
    transmet_saisie_scripts();
    ferme_toutes_fenetres();
    const Seance &ws = m_store.ws;
    if (ws.est_vide())
        return;
    std::vector<TabKey> keys = {TabKey{TabKey::Quantities}, TabKey{TabKey::Calculations}};
    for (const auto &t : ws.tables)
        keys.push_back(TabKey{TabKey::Table, t.id});
    for (const auto &g : ws.graphs)
        keys.push_back(TabKey{TabKey::Graph, g.id});
    for (const TabKey &k : keys) {
        Dock *dock = cree_fenetre(k);
        m_docks[k.texte()] = dock;
        addDockWidget(Qt::LeftDockWidgetArea, dock);
    }
    maj_titres_fenetres();
    maj_indication_vide();
    redispose();
    // les premières fenêtres de chaque colonne au premier plan
    for (const TabKey &k : {TabKey{TabKey::Quantities}})
        if (est_ouvert(k))
            m_docks[k.texte()]->raise();
    if (!ws.tables.empty())
        m_docks[TabKey{TabKey::Table, ws.tables.front().id}.texte()]->raise();
    if (!ws.graphs.empty())
        m_docks[TabKey{TabKey::Graph, ws.graphs.front().id}.texte()]->raise();
}

QJsonValue MainWindow::disposition_json() const
{
    if (m_modeSimple)
        return m_dispositionExpert;
    QJsonArray docks;
    for (const auto &[k, d] : m_docks)
        if (d)
            docks.append(k);
    QJsonObject o;
    o[u"app"_s] = u"mevreg"_s;
    o[u"docks"_s] = docks;
    o[u"state"_s] = QString::fromLatin1(saveState().toBase64());
    return o;
}

void MainWindow::restaure_disposition(const QJsonValue &layout)
{
    const QJsonObject o = layout.toObject();
    // disposition d'un autre format, ou absente : disposition par défaut
    if (o.value(u"app"_s).toString() != u"mevreg" || !o.value(u"docks"_s).isArray()) {
        disposition_par_defaut();
        return;
    }
    ferme_toutes_fenetres();
    for (const QJsonValue &v : o.value(u"docks"_s).toArray()) {
        const auto key = TabKey::analyse(v.toString());
        if (!key)
            continue;
        if ((key->kind == TabKey::Table && !m_store.ws.tableur(key->id)) || (key->kind == TabKey::Graph && !m_store.ws.graphe(key->id))
            || (key->kind == TabKey::Script && !m_store.ws.script(key->id)))
            continue;
        Dock *dock = cree_fenetre(*key);
        m_docks[key->texte()] = dock;
        addDockWidget(Qt::LeftDockWidgetArea, dock);
    }
    maj_titres_fenetres();
    maj_indication_vide();
    if (m_docks.empty()) {
        disposition_par_defaut();
        return;
    }
    if (!restoreState(QByteArray::fromBase64(o.value(u"state"_s).toString().toLatin1())))
        redispose();
}

void MainWindow::nouveau_tableur(std::vector<QId> columns)
{
    ViewId id = 0;
    m_store.edite([&](Seance &ws) { id = ws.ajoute_tableur(std::move(columns)); });
    ouvre_onglet(TabKey{TabKey::Table, id});
}

void MainWindow::nouveau_graphe(std::optional<std::pair<QId, QId>> curve)
{
    ViewId id = 0;
    m_store.edite([&](Seance &ws) {
        id = ws.ajoute_graphe();
        if (curve) {
            auto &c = ws.graphe_modifiable(id)->plots[0].ajoute_courbe(curve->first, curve->second);
            if (m_modeSimple) {
                const auto *x = ws.doc.obtient(c.x), *y = ws.doc.obtient(c.y);
                if (x && x->uncertainty) c.errors = ErrorBars::Both;
                else if (y && y->uncertainty) c.errors = ErrorBars::Vertical;
            }
        }
    });
    ouvre_onglet(TabKey{TabKey::Graph, id});
}

void MainWindow::nouveau_script(const QString &code)
{
    ViewId id = 0;
    m_store.edite([&](Seance &ws) { id = ws.ajoute_script(code); });
    ouvre_onglet(TabKey{TabKey::Script, id});
}

void MainWindow::ouvre_modelisation(ViewId graph, int plot, std::optional<int> fit)
{
    ouvre_onglet(TabKey{TabKey::Modeling});
    if (ModelingPanel *mp = ModelingPanel::instance())
        mp->cible(graph, plot, fit);
}

void MainWindow::ouvre_statistiques(QId q)
{
    if (!m_store.ws.doc.obtient(q)) return;
    if (m_modeSimple) {
        if (!m_statsGuide) {
            auto *dlg = new QDialog(this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setWindowTitle(u"Statistiques d’une grandeur"_s);
            auto *layout = new QVBoxLayout(dlg);
            auto *zone = new QScrollArea;
            zone->setWidgetResizable(true);
            zone->setFrameShape(QFrame::NoFrame);
            m_statsGuide = new StatsPanel(*this);
            zone->setWidget(m_statsGuide);
            layout->addWidget(zone);
            auto *boutons = new QDialogButtonBox(dlg);
            boutons->addButton(u"Fermer"_s, QDialogButtonBox::RejectRole)->setIcon(icons::obtient("close"));
            connect(boutons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
            layout->addWidget(boutons);
            dlg->resize(theme::px(QSize(520, 540)).boundedTo(size()));
            dlg->open();
        }
        m_statsGuide->definit_grandeur(q);
        return;
    }
    const TabKey cle{TabKey::Statistics};
    ouvre_onglet(cle);
    const auto it = m_docks.find(cle.texte());
    if (it != m_docks.end() && it->second)
        if (auto *panneau = qobject_cast<StatsPanel *>(it->second->widget()))
            panneau->definit_grandeur(q);
}

GraphPanel *MainWindow::panneau_graphe(ViewId id) const
{
    auto it = m_docks.find(TabKey{TabKey::Graph, id}.texte());
    if (it == m_docks.end() || !it->second)
        return nullptr;
    return qobject_cast<GraphPanel *>(it->second->widget());
}
