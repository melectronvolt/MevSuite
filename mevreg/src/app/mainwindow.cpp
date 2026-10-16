#include "app/mainwindow.h"

#include "app/about.h"
#include "app/calculationspanel.h"
#include "app/csvimport.h"
#include "app/dialogs.h"
#include "app/docking.h"
#include "app/drawingview.h"
#include "app/examples.h"
#include "app/graphwidget.h"
#include "app/help.h"
#include "app/icons.h"
#include "app/modelingpanel.h"
#include "app/pyexamples.h"
#include "app/quantitiespanel.h"
#include "app/scripting.h"
#include "app/scriptpanel.h"
#include "app/statspanel.h"
#include "app/tableview.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/csv.h"
#include "core/fit.h"
#include "plot/report.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionDockWidget>
#include <QStyleOptionMenuItem>
#include <QStylePainter>
#include <QSvgGenerator>
#include <QTabBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <climits>
#include <cmath>
#include <numeric>
#include <set>

using namespace Qt::StringLiterals;

namespace {

QString fileName(const QString &path)
{
    return QFileInfo(path).fileName();
}

QString plural(size_t n)
{
    return n > 1 ? u"s"_s : QString();
}

/// Colonnes de la disposition : Grandeurs à gauche, tableurs et scripts au centre, graphes à
/// droite, modélisation et statistiques tout à droite ; on glisse ainsi une grandeur de l'une à
/// l'autre.
enum class Zone { Left, Center, Right, Side };

Zone zoneOf(TabKey::Kind k)
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
int shareOf(Zone z)
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
int minWidth(TabKey::Kind k)
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
bool isPanel(TabKey::Kind k)
{
    switch (k) {
    case TabKey::Quantities:
    case TabKey::Calculations:
    case TabKey::Modeling:
    case TabKey::Statistics: return true;
    default: return false;
    }
}

/// Texte d'une étiquette de la couleur donnée (la palette suit le thème).
void setTextColor(QWidget *widget, const QColor &color)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText, color);
    palette.setColor(QPalette::ButtonText, color);
    widget->setPalette(palette);
}

/// Fichier de mesures (CSV et textes voisins) : importé dans le document, après un aperçu.
bool isMeasureFile(const QString &path)
{
    static const QStringList suffixes = {u"csv"_s, u"txt"_s, u"tsv"_s, u"dat"_s};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

const char *iconOf(TabKey::Kind k)
{
    switch (k) {
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

/// Information permanente de la barre d'état, séparée du message par un filet vertical de 1 px
/// couleur Mid et 10 px de marge (une bordure de feuille de style n'y est pas dessinée).
class StatusInfo : public QLabel {
public:
    explicit StatusInfo(QWidget *parent) : QLabel(parent) { setContentsMargins(theme::px(11), 0, theme::px(10), 0); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QLabel::paintEvent(event);
        QPainter(this).fillRect(QRect(0, 0, 1, height()), QApplication::palette().color(QPalette::Mid));
    }
};

/// Exportation d'un dessin : 900 pixels sur le plus grand côté, plus la règle.
QSizeF drawingSize(const drawing::Drawing &d)
{
    const double w = std::abs(d.desc.xmax - d.desc.xmin), h = std::abs(d.desc.ymax - d.desc.ymin);
    if (w <= 0 || h <= 0)
        return {900, 600};
    if (w >= h)
        return {900, std::max(150.0, 900.0 * h / w + 56.0)};
    return {std::max(200.0, 900.0 * w / h + 56.0), 900};
}

QImage drawingImage(const drawing::Drawing &d, double scale)
{
    const QSizeF size = drawingSize(d);
    QImage img((size * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    drawing::paint(p, d, size);
    return img;
}

} // namespace

// --- ligne du menu Fenêtres -------------------------------------------------------------------

namespace {

/// Ligne du menu Fenêtres, dessinée comme les autres entrées du menu (coche, icône, nom), avec une
/// croix à droite pour fermer la fenêtre pour de bon.
class WindowRow : public QWidget {
public:
    WindowRow(QMenu *menu, const QIcon &icon, const QString &text, bool shown, std::function<void()> open, std::function<void()> close)
        : QWidget(menu), m_menu(menu), m_icon(icon), m_text(text), m_shown(shown), m_open(std::move(open))
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        m_close = new QToolButton(this);
        m_close->setObjectName(u"closeWindow"_s);
        m_close->setIcon(icons::get("close"));
        m_close->setAutoRaise(true);
        m_close->setFocusPolicy(Qt::NoFocus);
        m_close->setToolTip(u"Fermer « %1 » : supprimé du document (Ctrl+Z pour annuler)"_s.arg(text));
        const int side = fontMetrics().height();
        m_close->setIconSize(QSize(side * 3 / 4, side * 3 / 4));
        connect(m_close, &QToolButton::clicked, this, [this, close = std::move(close)] {
            m_menu->close();
            close();
        });
    }

    QSize sizeHint() const override
    {
        const QStyleOptionMenuItem opt = option();
        const QSize text(fontMetrics().horizontalAdvance(m_text), fontMetrics().height());
        const QSize item = style()->sizeFromContents(QStyle::CT_MenuItem, &opt, text, m_menu);
        return {item.width() + m_close->sizeHint().width() + 4, std::max(item.height(), m_close->sizeHint().height())};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QStyleOptionMenuItem opt = option();
        opt.rect = rect().adjusted(0, 0, -m_close->width() - 4, 0);
        if (underMouse() && !m_close->underMouse())
            opt.state |= QStyle::State_Selected;
        // fond de l'entrée survolée sur toute la largeur, croix comprise
        if (opt.state & QStyle::State_Selected)
            p.fillRect(rect(), palette().color(QPalette::Highlight));
        style()->drawControl(QStyle::CE_MenuItem, &opt, &p, m_menu);
    }
    void resizeEvent(QResizeEvent *) override
    {
        const QSize b = m_close->sizeHint();
        m_close->setGeometry(width() - b.width() - 2, (height() - b.height()) / 2, b.width(), b.height());
    }
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave || e->type() == QEvent::HoverMove)
            update();
        return QWidget::event(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        m_menu->close();
        m_open();
    }

private:
    QStyleOptionMenuItem option() const
    {
        QStyleOptionMenuItem opt;
        opt.initFrom(m_menu);
        opt.menuItemType = QStyleOptionMenuItem::Normal;
        opt.checkType = QStyleOptionMenuItem::NonExclusive;
        opt.checked = m_shown;
        opt.menuHasCheckableItems = true;
        opt.text = m_text;
        opt.icon = m_icon;
        opt.maxIconWidth = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, m_menu) + 4;
        opt.font = m_menu->font();
        opt.reservedShortcutWidth = 0;
        opt.state &= ~QStyle::State_HasFocus;
        return opt;
    }

    QMenu *m_menu;
    QIcon m_icon;
    QString m_text;
    bool m_shown;
    std::function<void()> m_open;
    QToolButton *m_close;
};

} // namespace

// --- TabKey, Dock ------------------------------------------------------------------------------

QString TabKey::text() const
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

std::optional<TabKey> TabKey::parse(const QString &s)
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
    setObjectName(k.text());
    setWidget(content);
    setWindowIcon(icons::get(iconOf(k.kind)));
    // ni « DockWidgetMovable » ni « DockWidgetFloatable » : Qt ne déplace ni ne détache plus la
    // fenêtre lui-même ; c'est le glissement par les repères, et le bouton Détacher de la barre
    setFeatures(QDockWidget::DockWidgetClosable);
    m_title = new DockTitle(this);
    setTitleBarWidget(m_title);
    // largeur minimale relative à la police (elle suit l'agrandissement de l'interface), et jamais
    // moins que celle que demande le contenu
    const int own = theme::baseMinimumWidth(content);
    theme::setMinimumWidth(content, own > 0 ? std::min(own, minWidth(k.kind)) : minWidth(k.kind));
    m_panel = isPanel(k.kind);
    if (m_panel)
        fitPanelColor();
}

void Dock::setDetached(bool on)
{
    if (on == m_detached)
        return;
    m_detached = on;
    m_title->setDetached(on);
    // fenêtre à part : le cadre est celui du système (déplacer, agrandir, fermer), sous Wayland
    // comme ailleurs ; elle reste liée à la fenêtre principale (au-dessus d'elle, fermée avec elle)
    setWindowFlags(on ? Qt::Window : Qt::Widget);
}

void Dock::fitPanelColor()
{
    // Fond peint par `paintEvent` (barre de titre comprise), le contenu étant transparent. En
    // sombre, Window = Light dans la palette locale : Fusion dessine le bord des champs (cases,
    // listes, tableaux) d'après Window, et sur le fond sombre du panneau il disparaîtrait.
    const QColor c = theme::isDark() ? QApplication::palette().color(QPalette::Light) : theme::panelColor();
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
        QPainter(this).fillRect(rect(), theme::panelColor());
}

void Dock::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (m_panel && event->type() == QEvent::PaletteChange)
        fitPanelColor();
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
                Q_EMIT dragRequested(this, me->globalPosition().toPoint());
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
    Q_EMIT closeRequested(this);
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
    m_close->setIcon(icons::get("close"));
    m_close->setToolTip(u"Fermer la fenêtre"_s);
    connect(m_detach, &QToolButton::clicked, this, [this] { Q_EMIT m_dock->detachRequested(m_dock, !m_detached); });
    connect(m_close, &QToolButton::clicked, this, [this] { Q_EMIT m_dock->closeRequested(m_dock); });
    setDetached(false);
}

void DockTitle::setDetached(bool on)
{
    m_detached = on;
    // détachée : « Rattacher » en toutes lettres (la place ne manque pas), Fermer est dans le cadre
    m_detach->setIcon(icons::get(on ? "attach" : "detach"));
    m_detach->setText(on ? u"Rattacher"_s : QString());
    m_detach->setToolButtonStyle(on ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    m_detach->setToolTip(on ? u"Remettre cette fenêtre dans la fenêtre principale de MeVReg"_s
                            : u"Détacher : ouvrir dans une fenêtre à part, que l'on peut placer sur un autre écran"_s);
    m_close->setVisible(!on);
    fit();
    update();
}

void DockTitle::fit()
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
        fit();
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

// --- MainWindow --------------------------------------------------------------------------------

MainWindow::MainWindow(Store &store, QWidget *parent) : QMainWindow(parent), m_store(store), m_baseFont(QApplication::font())
{
    setWindowIcon(QIcon(u":/icons/mevreg.svg"_s));
    setMinimumSize(MIN_WINDOW);
    resize(DEFAULT_WINDOW);
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setDocumentMode(true);

    buildMenus();
    buildToolbar();
    buildMenuCorner();
    // aucune fenêtre ouverte : la page d'accueil occupe la place
    m_emptyHint = buildWelcomePage();
    setCentralWidget(m_emptyHint);
    buildStatusBar();
    buildZoom();

    connect(&store, &Store::messageRequested, this, &MainWindow::setStatus);
    connect(&store, &Store::workspaceChanged, this, &MainWindow::refresh);
    connect(&store, &Store::dirtyChanged, this, [this] {
        updateTitle();
        updateStatus();
    });
    connect(&store, &Store::scriptStateChanged, this, [this] { updateDockTitles(); });
    connect(&store, &Store::scriptApplied, this, &MainWindow::onScriptApplied);
    connect(&store, &Store::displayChanged, this, [this] {
        updateStatus();
        update();
    });

    setStatus(u"Créez un tableur pour saisir vos mesures, ou ouvrez un fichier."_s);
    refresh();
}

// --- menus et barre d'outils -------------------------------------------------------------------

void MainWindow::openDialog()
{
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    const QString p = QFileDialog::getOpenFileName(this, u"Ouvrir"_s, dir, u"Documents MeVReg et mesures (*.phy *.csv *.txt *.tsv *.dat);;Espace de travail MeVReg (*.phy);;Mesures CSV (*.csv *.txt *.tsv *.dat);;Tous les fichiers (*)"_s);
    if (p.isEmpty())
        return;
    // des mesures s'ajoutent au document ; un document le remplace (enregistrer d'abord ?)
    if (isMeasureFile(p))
        openPath(p);
    else
        guarded([this, p] { openPath(p); });
}

void MainWindow::buildMenus()
{
    // Les menus donnent tout ; la barre d'outils, ce qui sert le plus (texte avec émoji, donné
    // par `iconText` : le menu garde son libellé et son icône).
    QMenu *file = m_menuFile = menuBar()->addMenu(u"&Fichier"_s);
    file->addAction(icons::get("new"), u"Nouveau"_s, QKeySequence::New, this, [this] { guarded([this] { newFile(); }); });
    QAction *open = file->addAction(icons::get("open"), u"Ouvrir…"_s, QKeySequence::Open, this, &MainWindow::openDialog);
    open->setObjectName(u"open"_s);
    open->setToolTip(u"Ouvrir un document MeVReg (.phy) ou un fichier de mesures (CSV) (Ctrl+O)"_s);
    m_recentMenu = file->addMenu(icons::get("list"), u"Fichiers récents"_s);
    connect(m_recentMenu, &QMenu::aboutToShow, this, [this] {
        m_recentMenu->clear();
        if (m_store.prefs.recent.isEmpty())
            m_recentMenu->addAction(u"Aucun"_s)->setEnabled(false);
        for (const QString &r : m_store.prefs.recent) {
            QAction *a = m_recentMenu->addAction(fileName(r), this, [this, r] { guarded([this, r] { openPath(r); }); });
            a->setToolTip(r);
            a->setStatusTip(r);
        }
    });
    file->addSeparator();
    QAction *saveAction = file->addAction(icons::get("save"), u"Enregistrer"_s, QKeySequence::Save, this, [this] { save(false); });
    saveAction->setObjectName(u"save"_s);
    saveAction->setToolTip(u"Enregistrer l'espace de travail (Ctrl+S)"_s);
    file->addAction(u"Enregistrer sous…"_s, QKeySequence::SaveAs, this, [this] { save(true); });
    file->addSeparator();
    QAction *rep = file->addAction(icons::get("pdf"), u"Compte rendu du TP en PDF…"_s, this, [this] { reportPdf(false); });
    rep->setStatusTip(u"Tableaux, calculs, graphes, modèles, dessins et scripts, avec l'en-tête du TP"_s);
    file->addAction(icons::get("print"), u"Imprimer le compte rendu…"_s, QKeySequence::Print, this, [this] { reportPdf(true); });
    file->addSeparator();
    file->addAction(icons::get("csv"), u"Importer un fichier CSV…"_s, this, [this] { importCsv(std::nullopt); });
    file->addAction(icons::get("export"), u"Exporter toutes les grandeurs en CSV…"_s, this, [this] {
        std::vector<QId> ids;
        for (const auto &q : m_store.ws.doc.quantities)
            ids.push_back(q.id);
        exportCsv(ids);
    });
    file->addAction(icons::get("python"), u"Exporter vers Python (Capytale)…"_s, this, [this] { dialogs::pythonExport(*this); });
    file->addSeparator();
    file->addAction(icons::get("info"), u"Informations du TP…"_s, this, [this] { dialogs::meta(*this); });
    file->addSeparator();
    file->addAction(icons::get("close"), u"Quitter"_s, QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(u"É&dition"_s);
    m_undo = edit->addAction(icons::get("undo"), u"Annuler"_s, QKeySequence::Undo, this, [this] { m_store.undo(); });
    m_undo->setToolTip(u"Annuler la dernière modification (Ctrl+Z)"_s);
    m_redo = edit->addAction(icons::get("redo"), u"Rétablir"_s, this, [this] { m_store.redo(); });
    m_redo->setShortcuts({QKeySequence(u"Ctrl+Y"_s), QKeySequence(u"Ctrl+Shift+Z"_s)});
    m_redo->setToolTip(u"Rétablir la modification annulée (Ctrl+Y)"_s);
    edit->addSeparator();
    edit->addAction(icons::get("gear"), u"Préférences…"_s, QKeySequence::Preferences, this, [this] { dialogs::preferences(*this); });

    QMenu *data = menuBar()->addMenu(u"Do&nnées"_s);
    data->addAction(icons::get("plus"), u"Nouvelle grandeur…"_s, this, [this] { dialogs::newQuantity(*this, false); });
    data->addAction(icons::get("function"), u"Nouvelle grandeur calculée…"_s, this, [this] { dialogs::newQuantity(*this, true); });
    data->addAction(icons::get("sliders"), u"Nouveau paramètre…"_s, this, [this] { dialogs::newParameter(*this); });
    data->addSeparator();
    data->addAction(icons::get("table"), u"Nouveau tableur"_s, this, [this] { newTable(m_store.ws.orphans()); });

    QMenu *graphs = menuBar()->addMenu(u"&Graphes"_s);
    graphs->addAction(icons::get("chart"), u"Nouveau graphe"_s, this, [this] { newGraph(); });
    QMenu *yx = graphs->addMenu(icons::get("scatter"), u"Nouveau graphe y(x)"_s);
    connect(yx, &QMenu::aboutToShow, this, [this, yx] {
        yx->clear();
        const auto &qs = m_store.ws.doc.quantities;
        if (qs.size() < 2) {
            yx->addAction(u"Il faut au moins deux grandeurs"_s)->setEnabled(false);
            return;
        }
        const QId x = qs.front().id;
        for (size_t k = 1; k < qs.size(); ++k) {
            const QId y = qs[k].id;
            yx->addAction(u"%1 en fonction de %2"_s.arg(qs[k].name, qs.front().name), this, [this, x, y] { newGraph(std::pair(x, y)); });
        }
    });

    QMenu *an = menuBar()->addMenu(u"&Analyse"_s);
    an->addAction(icons::get("model"), u"Modélisation"_s, this, [this] { openTab(TabKey{TabKey::Modeling}); });
    an->addAction(icons::get("stats"), u"Statistiques"_s, this, [this] { openTab(TabKey{TabKey::Statistics}); });
    an->addSeparator();
    an->addAction(icons::get("derivative"), u"Dérivée…"_s, this, [this] { dialogs::derivative(*this, false); });
    an->addAction(u"Dérivée seconde…"_s, this, [this] { dialogs::derivative(*this, true); });
    an->addAction(icons::get("integral"), u"Intégrale…"_s, this, [this] { dialogs::integral(*this); });
    an->addAction(icons::get("wave"), u"Spectre (analyse de Fourier)…"_s, this, [this] { dialogs::spectrum(*this); });
    an->addSeparator();
    an->addAction(icons::get("tangent"), u"Méthode des tangentes…"_s, this, [this] { dialogs::tangents(*this); });
    an->addAction(u"Méthode de la dérivée…"_s, this, [this] { dialogs::derivativeMethod(*this); });

    m_pythonMenu = menuBar()->addMenu(u"&Python"_s);
    connect(m_pythonMenu, &QMenu::aboutToShow, this, [this] {
        m_pythonMenu->clear();
        m_pythonMenu->addAction(icons::get("python"), u"Nouveau script"_s, this, [this] { newScript(); });
        // exemples courts par thème : chacun s'ouvre dans un nouveau script, prêt à exécuter
        QMenu *examples = m_pythonMenu->addMenu(icons::get("flask"), u"Exemples"_s);
        for (const QString &cat : pyexamples::categories()) {
            QMenu *sub = examples->addMenu(cat);
            for (const auto &ex : pyexamples::all())
                if (ex.category == cat)
                    sub->addAction(ex.title, this, [this, code = ex.code, title = ex.title] { newScript(code, title); });
        }
        if (!m_store.ws.scripts.empty()) {
            m_pythonMenu->addAction(icons::get("play"), u"Exécuter tous les scripts"_s, this, [this] {
                flushScripts();
                runAllScripts();
            });
            m_pythonMenu->addSeparator();
            for (const auto &s : m_store.ws.scripts) {
                const ViewId id = s.id;
                m_pythonMenu->addAction(icons::get("python"), s.name, this, [this, id] { openTab(TabKey{TabKey::Script, id}); });
            }
        }
        m_pythonMenu->addSeparator();
        m_pythonMenu->addAction(icons::get("export"), u"Exporter vers Python (Capytale)…"_s, this, [this] { dialogs::pythonExport(*this); });
    });

    m_windowsMenu = menuBar()->addMenu(u"Fe&nêtres"_s);
    connect(m_windowsMenu, &QMenu::aboutToShow, this, [this] {
        m_windowsMenu->clear();
        auto add = [this](const char *icon, const QString &text, const TabKey &k) {
            QAction *a = m_windowsMenu->addAction(icons::get(icon), text, this, [this, k] { openTab(k); });
            a->setCheckable(true);
            a->setChecked(isOpen(k));
        };
        add("list", u"Grandeurs"_s, TabKey{TabKey::Quantities});
        add("function", u"Calculs et paramètres"_s, TabKey{TabKey::Calculations});
        add("model", u"Modélisation"_s, TabKey{TabKey::Modeling});
        add("stats", u"Statistiques"_s, TabKey{TabKey::Statistics});
        // tableurs, graphes, scripts, dessins : cochés s'ils sont affichés (sinon cachés), une
        // croix pour les fermer (supprimer du document)
        if (!m_store.ws.tables.empty() || !m_store.ws.graphs.empty() || !m_store.ws.scripts.empty() || !m_store.drawings.empty())
            m_windowsMenu->addSection(u"Tableurs, graphes, scripts — ✕ : fermer"_s);
        auto row = [this](const char *icon, const QString &text, const TabKey &k) {
            auto *action = new QWidgetAction(m_windowsMenu);
            action->setText(text);
            auto *w = new WindowRow(m_windowsMenu, icons::get(icon), text, isOpen(k), [this, k] { openTab(k); }, [this, k] { deleteWindow(k); });
            w->setObjectName(u"windowRow:"_s + k.text());
            action->setDefaultWidget(w);
            m_windowsMenu->addAction(action);
        };
        for (const auto &t : m_store.ws.tables)
            row("table", t.name, TabKey{TabKey::Table, t.id});
        for (const auto &g : m_store.ws.graphs)
            row("chart", g.name, TabKey{TabKey::Graph, g.id});
        for (const auto &s : m_store.ws.scripts)
            row("python", s.name, TabKey{TabKey::Script, s.id});
        for (const auto &[name, d] : m_store.drawings)
            row("paint", name, TabKey{TabKey::Drawing, 0, name});
        m_windowsMenu->addSeparator();
        m_windowsMenu->addAction(icons::get("layout"), u"Disposition par défaut"_s, this, [this] { defaultLayout(); });
        // une fenêtre détachée se rattache par son bouton « Rattacher », ou toutes d'un coup ici
        std::vector<QPointer<Dock>> detached;
        for (const auto &[k, d] : m_docks)
            if (d && d->detached())
                detached.emplace_back(d);
        if (!detached.empty())
            m_windowsMenu->addAction(icons::get("attach"), u"Rattacher les fenêtres détachées"_s, this, [this, detached] {
                for (const QPointer<Dock> &d : detached)
                    if (d)
                        attachDock(d);
            });
    });

    QMenu *help = menuBar()->addMenu(u"&Aide"_s);
    QAction *manual = help->addAction(icons::get("help"), u"Notice d'utilisation…"_s, QKeySequence::HelpContents, this, [this] { openHelp(); });
    manual->setObjectName(u"manual"_s);
    QAction *aboutAction = help->addAction(icons::get("info"), u"À propos de MeVReg…"_s, this, [this] { about::show(this); });
    aboutAction->setMenuRole(QAction::AboutRole);
    QAction *aboutQt = help->addAction(u"À propos de Qt…"_s, qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);
}

void MainWindow::buildToolbar()
{
    // Une seule barre, fixe, sous les menus : les fenêtres de travail, icônes dessinées au trait (de
    // la couleur du texte du thème). Ouvrir, enregistrer, annuler et rétablir sont
    // dans les menus (et sur la page d'accueil) ; le thème et la langue à droite des menus.
    m_toolbar = addToolBar(u"Barre d'outils"_s);
    m_toolbar->setObjectName(u"toolbar"_s);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolbar->setIconSize(QSize(20, 20));
    auto add = [this](const char *icon, const QString &text, const QString &tip, auto fn) {
        QAction *a = m_toolbar->addAction(icons::get(icon), text, this, fn);
        a->setToolTip(tip);
        return a;
    };
    add("list", u"Grandeurs"_s, u"Toutes les grandeurs du document : en créer, les ajouter aux tableurs et aux graphes"_s,
        [this] { openTab(TabKey{TabKey::Quantities}); });
    add("table", u"Tableur"_s, u"Nouveau tableur, pour saisir des mesures"_s, [this] { newTable(m_store.ws.orphans()); });
    add("scatter", u"Graphe"_s, u"Nouveau graphe"_s, [this] { newGraph(); });
    add("python", u"Python"_s, u"Nouveau script Python"_s, [this] { newScript(); });
}

void MainWindow::buildMenuCorner()
{
    // À droite de la barre de menus, sur sa ligne : le thème et la langue. Barre de menus
    // dans la fenêtre, même avec un menu global du bureau, qui cacherait ces boutons.
    menuBar()->setNativeMenuBar(false);
    // thème et langue : un clic passe à l'autre
    m_actTheme = new QAction(this);
    m_actTheme->setObjectName(u"theme"_s);
    connect(m_actTheme, &QAction::triggered, this, [this] {
        m_store.prefs.theme = theme::isDark() ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark;
        // la nouvelle palette (changeEvent) met à jour le bouton et redessine les fenêtres
        theme::choose(m_store.prefs.theme);
        m_store.prefs.save();
    });
    m_actLanguage = new QAction(this);
    m_actLanguage->setObjectName(u"language"_s);
    m_actLanguage->setIcon(icons::flag(icons::Lang::Fr));
    // le bouton garde sa place, comme dans les autres logiciels de la suite ; désactivé, il montre
    // encore sa bulle
    m_actLanguage->setText(u"MeVReg n'existe pour l'instant qu'en français."_s);
    m_actLanguage->setEnabled(false);

    m_menuCorner = new QWidget(menuBar());
    m_menuCorner->setObjectName(u"menuCorner"_s);
    auto *layout = new QHBoxLayout(m_menuCorner);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(2);
    // le thème et la langue ; la notice est dans le menu Aide (F1), un bouton de plus faisait doublon
    for (QAction *action : {m_actTheme, m_actLanguage}) {
        auto *button = new QToolButton(m_menuCorner);
        button->setObjectName(action->objectName() + u"Button"_s);
        button->setDefaultAction(action);
        // la notice en texte seul ; le thème et la langue en icône seule
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        m_cornerButtons.push_back(button);
    }
    menuBar()->setCornerWidget(m_menuCorner, Qt::TopRightCorner);
    updateThemeButton();
    sizeMenuCorner();
}

void MainWindow::sizeMenuCorner()
{
    // Boutons de la hauteur des titres des menus : la barre garde sa hauteur standard. Icônes de
    // 20 px × agrandissement, réduites s'il le faut pour tenir dans cette hauteur.
    int height = menuBar()->actionGeometry(m_menuFile->menuAction()).height();
    if (height <= 0) {
        // barre pas encore disposée : hauteur d'un titre de menu calculée comme le fait Qt
        QStyleOptionMenuItem option;
        option.initFrom(menuBar());
        option.menuItemType = QStyleOptionMenuItem::Normal;
        option.text = m_menuFile->title();
        const QSize text = menuBar()->fontMetrics().size(Qt::TextShowMnemonic, option.text);
        height = menuBar()->style()->sizeFromContents(QStyle::CT_MenuBarItem, &option, text, menuBar()).height();
    }
    const int icon = std::min(static_cast<int>(std::lround(20 * m_uiZoom)), height - 4);
    for (QToolButton *button : m_cornerButtons) {
        button->setIconSize(QSize(icon, icon));
        button->setFixedHeight(height);
    }
    // la barre de menus ne revoit la place du coin que quand on le lui redonne (sinon « Notice »
    // peut rester à la largeur de l'ancienne police, coupé)
    m_menuCorner->adjustSize();
    menuBar()->setCornerWidget(m_menuCorner, Qt::TopRightCorner);
    menuBar()->updateGeometry();
}

void MainWindow::updateThemeButton()
{
    const bool dark = theme::isDark();
    m_actTheme->setIcon(icons::theme(dark));
    m_actTheme->setText(dark ? u"Thème sombre. Cliquer pour passer au thème clair."_s : u"Thème clair. Cliquer pour passer au thème sombre."_s);
}

void MainWindow::buildStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);
    m_statusIcon = new QLabel(bar);
    m_statusIcon->setContentsMargins(6, 0, 0, 0);
    // un message long (chemin du fichier, erreur détaillée) passe à la ligne
    m_statusLabel = new QLabel(bar);
    m_statusLabel->setObjectName(u"statusMessage"_s);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setContentsMargins(0, 4, 0, 4);
    m_statusLabel->installEventFilter(this);
    m_statusFile = new StatusInfo(bar);
    m_statusFile->setObjectName(u"documentStatus"_s);
    m_statusErrors = new StatusInfo(bar);
    m_statusErrors->setObjectName(u"errorStatus"_s);
    m_statusErrors->hide();
    bar->addWidget(m_statusIcon);
    bar->addWidget(m_statusLabel, 1);
    bar->addPermanentWidget(m_statusErrors);
    bar->addPermanentWidget(m_statusFile);
    styleStatusBar();
}

void MainWindow::setStatus(const QString &text, icons::Status kind)
{
    m_status = {text, kind};
    m_statusLabel->setText(text);
    fitStatusBar();
    const int side = m_statusLabel->fontMetrics().height();
    m_statusIcon->setPixmap(icons::status(kind).pixmap(QSize(side, side), devicePixelRatioF()));
    m_statusIcon->setVisible(!text.isEmpty());
    QColor color = QApplication::palette().color(QPalette::WindowText);
    switch (kind) {
    case icons::Status::Info: break;
    case icons::Status::Success: color = theme::successColor(); break;
    case icons::Status::Warning: color = theme::warnColor(); break;
    case icons::Status::Error: color = theme::errorColor(); break;
    }
    setTextColor(m_statusLabel, color);
}

void MainWindow::fitStatusBar()
{
    // Hauteur donnée à la barre : celle que QLabel estime coupe le texte dès une vingtaine de
    // caractères (et la barre changeait de hauteur d'un message à l'autre). Ici, une ligne si le
    // message tient dans la largeur qu'il occupe, plusieurs sinon.
    const int line = std::max(m_statusLabel->heightForWidth(QWIDGETSIZE_MAX), m_statusFile->sizeHint().height());
    statusBar()->setFixedHeight(std::max(line, m_statusLabel->heightForWidth(m_statusLabel->width())) + theme::px(6));
}

void MainWindow::styleStatusBar()
{
    // Bande distincte du reste de la fenêtre : fond un peu plus foncé, filet au-dessus ; document
    // et formules en erreur séparés du message par un filet vertical. Couleurs données ici, la
    // feuille de style l'emportant sur la palette des étiquettes qu'elle décrit.
    const QPalette palette = QApplication::palette();
    const QColor background = palette.color(QPalette::Window).darker(theme::isDark() ? 135 : 107);
    statusBar()->setStyleSheet(u"QStatusBar { background: %1; border-top: 1px solid %2; }"
                               u"QStatusBar::item { border: none; }"
                               u"QLabel#documentStatus { color: %3; }"
                               u"QLabel#errorStatus { color: %4; }"_s.arg(background.name(), palette.color(QPalette::Mid).name(),
                                                                         theme::weakColor().name(), theme::errorColor().name()));
    setStatus(m_status.first, m_status.second);
}

QWidget *MainWindow::buildWelcomePage()
{
    // Épurée : le logiciel, puis saisir des mesures dans un tableur ou ouvrir son fichier ; la notice
    // est dans le menu Aide, le thème à droite des menus.
    auto *page = new QWidget(this);
    page->setObjectName(u"welcome"_s);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    const auto centered = [&](QLabel *label) {
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignHCenter);
        layout->addWidget(label);
        return label;
    };
    // tailles relatives à la police de l'interface : elles suivent l'agrandissement
    const auto scaled = [](QWidget *w, double factor, bool bold = false) { theme::setRelativeFont(w, factor, bold ? QFont::Bold : QFont::Normal); };
    layout->addStretch(1);
    auto *logo = new QLabel(page);
    logo->setObjectName(u"welcomeLogo"_s);
    logo->setPixmap(about::icon().pixmap(theme::px(QSize(96, 96)), devicePixelRatioF()));
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(8);
    scaled(centered(new QLabel(about::NAME, page)), 2.2, true);
    QLabel *tagline = centered(new QLabel(about::TAGLINE, page));
    tagline->setObjectName(u"welcomeTagline"_s);
    scaled(tagline, 1.15);
    setTextColor(tagline, theme::weakColor());

    layout->addSpacing(32);
    m_welcomeTable = new QPushButton(icons::get("table"), u"Nouveau tableur"_s, page);
    m_welcomeTable->setObjectName(u"welcomeTable"_s);
    m_welcomeTable->setToolTip(u"Un tableur vide, pour saisir vos mesures"_s);
    connect(m_welcomeTable, &QPushButton::clicked, this, [this] { newTable(m_store.ws.orphans()); });
    // pas d'exemples livrés : élèves et professeurs ouvrent leurs propres fichiers
    m_welcomeOpen = new QPushButton(icons::get("open"), u"Ouvrir…"_s, page);
    m_welcomeOpen->setObjectName(u"welcomeOpen"_s);
    m_welcomeOpen->setToolTip(u"Ouvrir un document MeVReg (.phy) ou un fichier de mesures (CSV)"_s);
    connect(m_welcomeOpen, &QPushButton::clicked, this, &MainWindow::openDialog);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    buttons->addStretch();
    for (QPushButton *button : {m_welcomeTable, m_welcomeOpen}) {
        scaled(button, 1.1);
        theme::setIconSize(button, QSize(20, 20));
        buttons->addWidget(button);
    }
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addSpacing(10);
    QLabel *hint = centered(new QLabel(u"ou glissez un fichier .phy ou CSV sur la fenêtre"_s, page));
    hint->setObjectName(u"welcomeHint"_s);
    setTextColor(hint, theme::weakColor());
    layout->addStretch(1);
    sizeWelcomeButtons();
    return page;
}

void MainWindow::sizeWelcomeButtons()
{
    int width = 0;
    int height = 0;
    for (QPushButton *button : {m_welcomeTable, m_welcomeOpen}) {
        button->setMinimumSize(0, 0);
        width = std::max(width, button->sizeHint().width());
        height = std::max(height, button->sizeHint().height());
    }
    for (QPushButton *button : {m_welcomeTable, m_welcomeOpen})
        button->setMinimumSize(width + height, int(height * 1.4));
}

// --- agrandissement de l'interface ------------------------------------------------------------

void MainWindow::buildZoom()
{
    // Ctrl + (ou Ctrl =) / Ctrl − / Ctrl 0 : agrandissement de l'interface par pas de 10 %, de 50 à
    // 300 %, borné par la largeur de la fenêtre pour qu'elle reste utilisable
    const auto zoomShortcut = [this](const QKeySequence &keys, auto step) {
        auto *action = new QAction(this);
        action->setShortcut(keys);
        connect(action, &QAction::triggered, this, [this, step] { setUiZoom(step(m_uiZoom)); });
        addAction(action);
    };
    const auto zoomIn = [](double z) { return std::round(z * 10.0 + 1.0) / 10.0; };
    zoomShortcut(QKeySequence::ZoomIn, zoomIn);
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), zoomIn);
    zoomShortcut(QKeySequence::ZoomOut, [](double z) { return std::round(z * 10.0 - 1.0) / 10.0; });
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), [](double) { return 1.0; });
    // agrandissement des préférences, borné par la taille de départ ; il est rétabli quand la
    // fenêtre prend sa taille (`resizeEvent`)
    applyUiZoom(std::min(m_store.prefs.uiZoom, maxUiZoom()));
}

double MainWindow::maxUiZoom() const
{
    // Il reste toujours au moins la largeur minimale de la fenêtre pour la barre d'outils, trois
    // colonnes de fenêtres et la page d'accueil ; par pas de 10 %, pour qu'un changement de taille
    // de la fenêtre ne change pas l'agrandissement à chaque pixel.
    const double fit = std::floor(width() * 10.0 / MIN_WINDOW.width() + 1e-9) / 10.0;
    return std::clamp(fit, 1.0, UI_ZOOM_MAX);
}

void MainWindow::setUiZoom(double zoom)
{
    const double z = std::clamp(zoom, UI_ZOOM_MIN, maxUiZoom());
    if (z != m_store.prefs.uiZoom) {
        m_store.prefs.uiZoom = z;
        m_store.prefs.save();
    }
    applyUiZoom(z);
}

void MainWindow::applyUiZoom(double zoom)
{
    if (zoom == m_uiZoom && theme::zoom() == zoom)
        return;
    m_uiZoom = zoom;
    theme::setZoom(zoom);
    // police du système multipliée ; les mesures du style suivent (`theme`)
    QFont font = m_baseFont;
    if (m_baseFont.pointSizeF() > 0.0)
        font.setPointSizeF(m_baseFont.pointSizeF() * zoom);
    else
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(m_baseFont.pixelSize() * zoom))));
    QApplication::setFont(font);
    // tailles données en pixels (largeurs minimales des fenêtres, icônes, polices relatives…)
    theme::rescale();
    m_toolbar->setIconSize(theme::px(QSize(20, 20)));
    sizeMenuCorner();
    if (auto *logo = m_emptyHint ? m_emptyHint->findChild<QLabel *>(u"welcomeLogo"_s) : nullptr)
        logo->setPixmap(about::icon().pixmap(theme::px(QSize(96, 96)), devicePixelRatioF()));
    if (m_welcomeTable)
        sizeWelcomeButtons();
    if (m_statusLabel) {
        m_statusIcon->setContentsMargins(theme::px(6), 0, 0, 0);
        m_statusLabel->setContentsMargins(0, theme::px(4), 0, theme::px(4));
        for (QLabel *info : {m_statusFile, m_statusErrors})
            info->setContentsMargins(theme::px(11), 0, theme::px(10), 0);
        // les étiquettes décrites par la feuille de style de la barre d'état ne prennent la nouvelle
        // police qu'avec elle (`styleStatusBar` réaffiche aussi le message, à sa hauteur)
        styleStatusBar();
        updateStatus();
    }
    // tableurs, graphes et panneaux se redessinent à la nouvelle taille
    Q_EMIT m_store.displayChanged();
    // la barre de menus ne mesure ses titres avec la nouvelle police qu'une fois l'événement
    // traité : le coin, l'accueil et la barre d'état sont mesurés de nouveau ensuite
    QTimer::singleShot(0, this, [this] {
        sizeMenuCorner();
        if (m_welcomeTable)
            sizeWelcomeButtons();
        if (m_statusLabel)
            fitStatusBar();
    });
}

void MainWindow::openHelp(const QString &page)
{
    QString error;
    const QString path = help::write(page.isEmpty() ? help::NOTICE : page, &error);
    if (path.isEmpty()) {
        setStatus(u"Impossible d'écrire la notice : %1"_s.arg(error), icons::Status::Error);
        return;
    }
    help::openUrl(QUrl::fromLocalFile(path));
    setStatus(u"Notice ouverte dans le navigateur : %1"_s.arg(QDir::toNativeSeparators(path)), icons::Status::Success);
}

QMenu *MainWindow::createPopupMenu()
{
    Dock *dock = dockAt(QCursor::pos());
    if (!dock)
        return nullptr;
    auto *menu = new QMenu(this);
    dockMenu(menu, dock);
    return menu;
}

Dock *MainWindow::dockAt(QPoint globalPos) const
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

void MainWindow::dockMenu(QMenu *menu, Dock *dock)
{
    const TabKey key = dock->key;
    auto rename = [this, key](const QString &title, const QString &current, const std::function<void(Workspace &, const QString &)> &apply) {
        bool ok = false;
        const QString n = QInputDialog::getText(this, title, u"Nom :"_s, QLineEdit::Normal, current, &ok).trimmed();
        if (ok && !n.isEmpty() && n != current)
            m_store.edit([&](Workspace &ws) { apply(ws, n); });
    };
    switch (key.kind) {
    case TabKey::Table:
        if (const TableView *t = m_store.ws.table(key.id)) {
            const QString cur = t->name;
            menu->addAction(icons::get("pencil"), u"Renommer le tableur…"_s, this, [rename, key, cur] {
                rename(u"Renommer le tableur"_s, cur, [key](Workspace &ws, const QString &n) { ws.tableMut(key.id)->name = n; });
            });
            QAction *del = menu->addAction(icons::get("trash"), u"Supprimer ce tableur"_s, this, [this, key] { deleteWindow(key); });
            del->setStatusTip(u"Les grandeurs restent dans le document"_s);
        }
        break;
    case TabKey::Graph:
        if (const GraphView *g = m_store.ws.graph(key.id)) {
            const QString cur = g->name;
            menu->addAction(icons::get("pencil"), u"Renommer le graphe…"_s, this, [rename, key, cur] {
                rename(u"Renommer le graphe"_s, cur, [key](Workspace &ws, const QString &n) { ws.graphMut(key.id)->name = n; });
            });
            menu->addAction(icons::get("trash"), u"Supprimer ce graphe"_s, this, [this, key] { deleteWindow(key); });
        }
        break;
    case TabKey::Script:
        if (const Script *s = m_store.ws.script(key.id)) {
            const QString cur = s->name;
            menu->addAction(icons::get("pencil"), u"Renommer le script…"_s, this, [rename, key, cur] {
                rename(u"Renommer le script"_s, cur, [key](Workspace &ws, const QString &n) { ws.scriptMut(key.id)->name = n; });
            });
            menu->addAction(icons::get("trash"), u"Supprimer ce script"_s, this, [this, key] { deleteWindow(key); });
        }
        break;
    default:
        break;
    }
    if (!menu->isEmpty())
        menu->addSeparator();
    const QPointer<Dock> d(dock);
    if (dock->detached())
        menu->addAction(icons::get("attach"), u"Rattacher à la fenêtre principale"_s, this, [this, d] { attachDock(d); });
    else
        menu->addAction(icons::get("detach"), u"Détacher dans une fenêtre à part"_s, this, [this, d] { detachDock(d); });
    menu->addAction(icons::get("eye-off"), u"Cacher la fenêtre"_s, this, [this, d] {
        if (d)
            removeDock(d);
    })->setStatusTip(u"Elle se retrouve dans le menu Fenêtres"_s);
    menu->addAction(icons::get("close"), u"Fermer la fenêtre…"_s, this, [this, d] { requestClose(d); });
}

// --- état affiché ------------------------------------------------------------------------------

void MainWindow::refresh()
{
    // fenêtres d'objets disparus (annulation, suppression)
    std::vector<Dock *> gone;
    for (const auto &[k, d] : m_docks) {
        if (!d)
            continue;
        const TabKey &key = d->key;
        const bool exists = (key.kind == TabKey::Table && m_store.ws.table(key.id)) || (key.kind == TabKey::Graph && m_store.ws.graph(key.id))
                            || (key.kind == TabKey::Script && m_store.ws.script(key.id))
                            || (key.kind != TabKey::Table && key.kind != TabKey::Graph && key.kind != TabKey::Script);
        if (!exists)
            gone.push_back(d.data());
    }
    for (Dock *d : gone)
        removeDock(d);
    updateDockTitles();
    updateTitle();
    updateStatus();
    m_undo->setEnabled(m_store.canUndo());
    m_redo->setEnabled(m_store.canRedo());
}

void MainWindow::updateDockTitles()
{
    for (const auto &[k, d] : m_docks) {
        if (!d)
            continue;
        const TabKey &key = d->key;
        QString title;
        switch (key.kind) {
        case TabKey::Quantities: title = u"Grandeurs"_s; break;
        case TabKey::Calculations: title = u"Calculs"_s; break;
        case TabKey::Table: title = m_store.ws.table(key.id) ? m_store.ws.table(key.id)->name : u"Tableur"_s; break;
        case TabKey::Graph: title = m_store.ws.graph(key.id) ? m_store.ws.graph(key.id)->name : u"Graphe"_s; break;
        case TabKey::Script:
            title = m_store.ws.script(key.id) ? m_store.ws.script(key.id)->name : u"Script"_s;
            if (m_store.isRunning(key.id))
                title += u" ⏳"_s;
            break;
        case TabKey::Drawing: title = key.name; break;
        case TabKey::Modeling: title = u"Modélisation"_s; break;
        case TabKey::Statistics: title = u"Statistiques"_s; break;
        }
        if (d->windowTitle() != title)
            d->setWindowTitle(title);
    }
}

void MainWindow::updateTitle()
{
    // les scripts rapportent leurs chemins relatifs au dossier du document
    m_store.documentFolder = m_path.isEmpty() ? QString() : QFileInfo(m_path).absolutePath();
    // « MeVReg — document » ; sous-titre tant que rien n'a été commencé
    const bool blank = m_path.isEmpty() && m_store.ws.isEmpty() && !m_store.dirty();
    const QString name = m_path.isEmpty() ? u"Sans titre"_s : fileName(m_path);
    setWindowTitle(u"%1 — %2"_s.arg(about::NAME, blank ? about::SUBTITLE : name + (m_store.dirty() ? u" •"_s : QString())));
}

void MainWindow::updateStatus()
{
    // à droite : document · titre du TP · nombre de grandeurs, puis les formules en erreur
    const QString name = m_path.isEmpty() ? u"Sans titre"_s : fileName(m_path);
    QStringList parts{name + (m_store.dirty() ? u" (modifié)"_s : QString())};
    if (!m_store.ws.meta.title.trimmed().isEmpty())
        parts << m_store.ws.meta.title.trimmed();
    const size_t nq = m_store.ws.doc.quantities.size();
    parts << u"%1 grandeur%2"_s.arg(nq).arg(plural(nq));
    const QString full = parts.join(u" · "_s);
    m_statusFile->setText(m_statusFile->fontMetrics().elidedText(full, Qt::ElideMiddle, std::max(width() / 3, theme::px(160))));
    m_statusFile->setToolTip(m_path.isEmpty() ? full : QDir::toNativeSeparators(m_path));
    QStringList errors;
    for (const auto &q : m_store.ws.doc.quantities)
        if (!q.error.isEmpty())
            errors << u"%1 : %2"_s.arg(q.name, q.error);
    m_statusErrors->setVisible(!errors.isEmpty());
    if (!errors.isEmpty()) {
        m_statusErrors->setText(u"%1 formule%2 en erreur"_s.arg(errors.size()).arg(plural(errors.size())));
        m_statusErrors->setToolTip(errors.join(u'\n'));
    }
}

void MainWindow::updateEmptyHint()
{
    const bool any = std::any_of(m_docks.begin(), m_docks.end(), [](const auto &p) { return p.second && !p.second->detached(); });
    // sans fenêtre (toutes fermées ou détachées) : l'invitation à démarrer occupe la place ; sinon
    // elle s'efface et les fenêtres ancrées prennent toute la largeur
    m_emptyHint->setVisible(!any);
}

// --- fenêtres ancrées --------------------------------------------------------------------------

QWidget *MainWindow::createContent(const TabKey &key)
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

bool MainWindow::isOpen(const TabKey &key) const
{
    auto it = m_docks.find(key.text());
    return it != m_docks.end() && it->second;
}

QList<TabKey> MainWindow::openTabs() const
{
    QList<TabKey> out;
    for (const auto &[k, d] : m_docks)
        if (d)
            out << d->key;
    return out;
}

void MainWindow::openTab(const TabKey &key)
{
    if (key.kind == TabKey::Table && !m_store.ws.table(key.id))
        return;
    if (key.kind == TabKey::Graph && !m_store.ws.graph(key.id))
        return;
    if (key.kind == TabKey::Script && !m_store.ws.script(key.id))
        return;
    if (auto it = m_docks.find(key.text()); it != m_docks.end() && it->second) {
        Dock *d = it->second;
        d->show();
        d->raise();
        if (d->detached())
            d->activateWindow();
        d->widget()->setFocus();
        return;
    }
    Dock *dock = createDock(key);
    m_docks[key.text()] = dock;
    // Modélisation : dans une fenêtre à part, à droite ; une colonne de plus ne laissait plus rien
    // voir du graphe et des tableurs (elle se rattache par son bouton)
    if (key.kind == TabKey::Modeling && isVisible()) {
        QSize size = theme::px(QSize(560, 700)).expandedTo(dock->minimumSizeHint());
        if (const QScreen *s = screen())
            size = size.boundedTo(s->availableGeometry().size() - QSize(40, 80));
        const QRect frame = frameGeometry();
        detachDock(dock, QRect(QPoint(frame.right() - size.width() - theme::px(24), frame.top() + theme::px(80)), size));
        updateDockTitles();
        return;
    }
    place(dock);
    updateDockTitles();
    updateEmptyHint();
    dock->show();
    dock->raise();
}

void MainWindow::closeTab(const TabKey &key)
{
    if (auto it = m_docks.find(key.text()); it != m_docks.end() && it->second)
        removeDock(it->second);
}

Dock *MainWindow::createDock(const TabKey &key)
{
    auto *dock = new Dock(key, createContent(key), this);
    connect(dock, &Dock::closeRequested, this, &MainWindow::requestClose);
    connect(dock, &Dock::dragRequested, this, &MainWindow::beginDockDrag);
    connect(dock, &Dock::detachRequested, this, [this](Dock *d, bool detach) {
        if (detach)
            detachDock(d);
        else
            attachDock(d);
    });
    return dock;
}

void MainWindow::requestClose(Dock *dock)
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
        removeDock(dock);
        return;
    }
    const QString title = dock->windowTitle();
    if (!m_store.prefs.confirmClose) {
        removeDock(dock);
        m_store.message(u"« %1 » est caché ; menu Fenêtres pour le retrouver"_s.arg(title));
        return;
    }
    // la fenêtre détachée porte la question (elle peut être sur un autre écran)
    QWidget *owner = dock->detached() ? static_cast<QWidget *>(dock) : this;
    // espaces insécables dans les guillemets : le nom n'est pas coupé en fin de ligne
    const QString name = u"« "_s + title + u" »"_s;
    auto *box = new QMessageBox(QMessageBox::Question, u"Cacher ou fermer "_s + name, u"Cacher ou fermer la fenêtre %1 ?"_s.arg(name),
                                QMessageBox::NoButton, owner);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setObjectName(u"confirmClose"_s);
    box->setInformativeText(u"Cachée, elle se retrouve dans le menu Fenêtres → %1.\nFermée, %2 (Ctrl+Z pour annuler)."_s.arg(name, lost));
    QPushButton *hide = box->addButton(u"Cacher"_s, QMessageBox::AcceptRole);
    hide->setObjectName(u"hide"_s);
    QPushButton *close = box->addButton(u"Fermer"_s, QMessageBox::DestructiveRole);
    close->setObjectName(u"close"_s);
    box->addButton(u"Annuler"_s, QMessageBox::RejectRole);
    box->setDefaultButton(hide);
    auto *never = new QCheckBox(u"Toujours cacher sans demander"_s, box);
    never->setToolTip(u"La question revient avec Édition → Préférences"_s);
    box->setCheckBox(never);
    const QPointer<Dock> target(dock);
    connect(box, &QMessageBox::buttonClicked, this, [this, hide, close, never, target, key](QAbstractButton *clicked) {
        if (clicked == hide) {
            if (never->isChecked()) {
                m_store.prefs.confirmClose = false;
                m_store.prefs.save();
            }
            if (target)
                removeDock(target);
        } else if (clicked == close) {
            deleteWindow(key);
        }
    });
    box->open();
}

void MainWindow::deleteWindow(const TabKey &key)
{
    closeTab(key);
    switch (key.kind) {
    case TabKey::Table:
        m_store.edit([&](Workspace &ws) { std::erase_if(ws.tables, [&](const TableView &t) { return t.id == key.id; }); });
        m_store.message(u"Tableur supprimé ; ses grandeurs restent dans le document (Ctrl+Z pour annuler)"_s);
        break;
    case TabKey::Graph:
        m_store.edit([&](Workspace &ws) { std::erase_if(ws.graphs, [&](const GraphView &g) { return g.id == key.id; }); });
        m_store.message(u"Graphe supprimé (Ctrl+Z pour annuler)"_s);
        break;
    case TabKey::Script:
        m_store.stopScript(key.id);
        m_store.edit([&](Workspace &ws) { std::erase_if(ws.scripts, [&](const Script &s) { return s.id == key.id; }); });
        m_store.message(u"Script supprimé ; les grandeurs qu'il a créées restent dans le document (Ctrl+Z pour annuler)"_s);
        break;
    case TabKey::Drawing:
        m_store.drawings.erase(key.name);
        m_store.message(u"Dessin « %1 » supprimé ; le script le refait à sa prochaine exécution"_s.arg(key.name));
        break;
    default:
        break;
    }
}

void MainWindow::widenDock(QDockWidget *dock, int width)
{
    if (!dock)
        return;
    if (dock->isWindow()) {
        dock->resize(std::max(dock->width(), width), dock->height());
        return;
    }
    // colonnes de la disposition : celle de la fenêtre prend ce que les autres peuvent céder
    const docking::Node tree = docking::capture(this, dockedDocks());
    if (tree.leaf() || tree.orientation != Qt::Horizontal)
        return;
    const auto in = [dock](const docking::Node &n) {
        const auto ds = docking::docksIn(n);
        return std::find(ds.begin(), ds.end(), dock) != ds.end();
    };
    const auto least = [](const docking::Node &n) {
        int m = 0;
        for (QDockWidget *d : docking::docksIn(n))
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
        docks << docking::docksIn(c).front();
        sizes << w;
    }
    resizeDocks(docks, sizes, Qt::Horizontal);
}

void MainWindow::detachDock(Dock *dock, std::optional<QRect> geometry)
{
    if (!dock || dock->detached() || !isOpen(dock->key))
        return;
    // à peu près à sa place à l'écran (Wayland laisse le système choisir), au moins assez grande
    const QSize size = geometry ? geometry->size() : dock->size().expandedTo(theme::px(QSize(480, 360)));
    const QPoint at = geometry ? geometry->topLeft() : dock->mapToGlobal(QPoint(theme::px(24), theme::px(24)));
    removeDockWidget(dock);
    dock->setDetached(true);
    // raccourcis de la fenêtre principale (annuler, enregistrer, agrandissement, notice…) actifs
    // aussi dans la fenêtre détachée
    dock->addActions(windowShortcuts());
    dock->resize(size);
    dock->move(at);
    dock->show();
    dock->raise();
    dock->activateWindow();
    updateEmptyHint();
    m_store.message(u"« %1 » est dans une fenêtre à part : bouton « Rattacher » pour la remettre ici"_s.arg(dock->windowTitle()));
}

void MainWindow::attachDock(Dock *dock)
{
    if (!dock || !dock->detached())
        return;
    for (QAction *a : windowShortcuts())
        dock->removeAction(a);
    dock->setDetached(false);
    // à la place que lui donne son genre, comme une fenêtre ouverte
    place(dock);
    dock->show();
    dock->raise();
    updateEmptyHint();
    activateWindow();
}

QList<QAction *> MainWindow::windowShortcuts() const
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

void MainWindow::removeDock(Dock *dock)
{
    // le texte d'un script en cours de saisie est rangé dans le document avant
    if (auto *sp = qobject_cast<ScriptPanel *>(dock->widget()))
        sp->flush();
    for (auto it = m_docks.begin(); it != m_docks.end();) {
        if (!it->second || it->second == dock)
            it = m_docks.erase(it);
        else
            ++it;
    }
    removeDockWidget(dock);
    dock->hide();
    dock->deleteLater();
    updateEmptyHint();
}

void MainWindow::closeAllDocks()
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
    updateEmptyHint();
}

void MainWindow::place(Dock *dock)
{
    const TabKey &key = dock->key;
    const Zone zone = zoneOf(key.kind);
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
            sibling = find([&](const TabKey &k) { return zoneOf(k.kind) == zone; });
            break;
        case Zone::Center:
            // un script ou l'aide s'ouvre en onglet sur les tableurs
            sibling = find([&](const TabKey &k) { return k.kind == TabKey::Table; });
            if (!sibling)
                sibling = find([&](const TabKey &k) { return zoneOf(k.kind) == Zone::Center; });
            break;
        case Zone::Right:
            sibling = find([&](const TabKey &k) { return zoneOf(k.kind) == Zone::Right; });
            break;
        }
    }
    // pas la place d'une colonne de plus : Modélisation et Statistiques s'ouvrent en onglet sur
    // les tableurs, pour garder le graphe visible
    if (!sibling && zone == Zone::Side) {
        std::set<Zone> zones;
        int need = theme::px(minWidth(key.kind));
        for (Dock *d : docked) {
            if (zones.insert(zoneOf(d->key.kind)).second)
                need += theme::px(minWidth(d->key.kind));
        }
        if (need > width() - 20) {
            sibling = find([&](const TabKey &k) { return zoneOf(k.kind) == Zone::Center; });
            if (!sibling)
                sibling = find([&](const TabKey &k) { return zoneOf(k.kind) == Zone::Left; });
        }
    }
    if (sibling) {
        tabifyDockWidget(sibling, dock);
        return;
    }
    // une colonne de plus, à sa place dans l'ordre des colonnes ; les autres fenêtres gardent la
    // disposition choisie et se partagent le reste de la largeur
    docking::Node tree = docking::capture(this, std::vector<QDockWidget *>(docked.begin(), docked.end()));
    if (!isVisible() || docking::docksIn(tree).empty()) {
        relayout();
        return;
    }
    auto zoneRank = [](const docking::Node &n) {
        int z = INT_MAX;
        for (QDockWidget *d : docking::docksIn(n))
            if (auto *dk = qobject_cast<Dock *>(d))
                z = std::min(z, static_cast<int>(zoneOf(dk->key.kind)));
        return z;
    };
    std::vector<int> columns;
    if (!tree.leaf() && tree.orientation == Qt::Horizontal)
        for (const docking::Node &c : tree.children)
            columns.push_back(zoneRank(c));
    else
        columns.push_back(zoneRank(tree));
    const auto after = std::find_if(columns.begin(), columns.end(), [&](int z) { return z > static_cast<int>(zone); });
    int shares = shareOf(zone);
    for (int z : columns)
        shares += shareOf(static_cast<Zone>(std::clamp(z, 0, static_cast<int>(Zone::Side))));
    const int width = std::max(theme::px(minWidth(key.kind)), tree.rect.width() * shareOf(zone) / shares);
    docking::insertColumn(tree, dock, static_cast<size_t>(after - columns.begin()), width, style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, this));
    docking::apply(this, tree);
}

std::vector<QDockWidget *> MainWindow::dockedDocks() const
{
    std::vector<QDockWidget *> out;
    for (const auto &[k, d] : m_docks)
        if (d && !d->isFloating() && dockWidgetArea(d) != Qt::NoDockWidgetArea)
            out.push_back(d);
    return out;
}

void MainWindow::beginDockDrag(Dock *dock, QPoint globalPos)
{
    if (!dock || dock->isFloating() || m_dropOverlay)
        return;
    const std::vector<QDockWidget *> docked = dockedDocks();
    if (docked.size() < 2 || std::find(docked.begin(), docked.end(), dock) == docked.end())
        return;
    auto *overlay = new docking::Overlay(this, dock, docked);
    m_dropOverlay = overlay;
    QPointer<Dock> d(dock);
    overlay->onDrop = [this, d](const docking::Target &t) {
        if (d)
            moveDock(d, t);
    };
    overlay->start(globalPos);
}

bool MainWindow::moveDock(Dock *dock, const docking::Target &target)
{
    docking::Node tree = docking::capture(this, dockedDocks());
    if (!docking::move(tree, dock, target, dock->size(), style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, this)))
        return false;
    docking::apply(this, tree);
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
        QTimer::singleShot(0, this, &MainWindow::fitStatusBar);
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
            beginDockDrag(dock, me->globalPosition().toPoint());
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

void MainWindow::relayout()
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
        columns[zoneOf(d->key.kind)].push_back(d);
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
        widths << shareOf(zone);
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

void MainWindow::defaultLayout()
{
    flushScripts();
    closeAllDocks();
    const Workspace &ws = m_store.ws;
    if (ws.isEmpty())
        return;
    std::vector<TabKey> keys = {TabKey{TabKey::Quantities}, TabKey{TabKey::Calculations}};
    for (const auto &t : ws.tables)
        keys.push_back(TabKey{TabKey::Table, t.id});
    for (const auto &g : ws.graphs)
        keys.push_back(TabKey{TabKey::Graph, g.id});
    for (const TabKey &k : keys) {
        Dock *dock = createDock(k);
        m_docks[k.text()] = dock;
        addDockWidget(Qt::LeftDockWidgetArea, dock);
    }
    updateDockTitles();
    updateEmptyHint();
    relayout();
    // les premières fenêtres de chaque colonne au premier plan
    for (const TabKey &k : {TabKey{TabKey::Quantities}})
        if (isOpen(k))
            m_docks[k.text()]->raise();
    if (!ws.tables.empty())
        m_docks[TabKey{TabKey::Table, ws.tables.front().id}.text()]->raise();
    if (!ws.graphs.empty())
        m_docks[TabKey{TabKey::Graph, ws.graphs.front().id}.text()]->raise();
}

QJsonValue MainWindow::layoutJson() const
{
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

void MainWindow::restoreLayout(const QJsonValue &layout)
{
    const QJsonObject o = layout.toObject();
    // disposition de la version Rust de MeVReg, ou absente : disposition par défaut
    if (o.value(u"app"_s).toString() != u"mevreg" || !o.value(u"docks"_s).isArray()) {
        defaultLayout();
        return;
    }
    closeAllDocks();
    for (const QJsonValue &v : o.value(u"docks"_s).toArray()) {
        const auto key = TabKey::parse(v.toString());
        if (!key)
            continue;
        if ((key->kind == TabKey::Table && !m_store.ws.table(key->id)) || (key->kind == TabKey::Graph && !m_store.ws.graph(key->id))
            || (key->kind == TabKey::Script && !m_store.ws.script(key->id)))
            continue;
        Dock *dock = createDock(*key);
        m_docks[key->text()] = dock;
        addDockWidget(Qt::LeftDockWidgetArea, dock);
    }
    updateDockTitles();
    updateEmptyHint();
    if (m_docks.empty()) {
        defaultLayout();
        return;
    }
    if (!restoreState(QByteArray::fromBase64(o.value(u"state"_s).toString().toLatin1())))
        relayout();
}

void MainWindow::newTable(std::vector<QId> columns)
{
    ViewId id = 0;
    m_store.edit([&](Workspace &ws) { id = ws.addTable(std::move(columns)); });
    openTab(TabKey{TabKey::Table, id});
}

void MainWindow::newGraph(std::optional<std::pair<QId, QId>> curve)
{
    ViewId id = 0;
    m_store.edit([&](Workspace &ws) {
        id = ws.addGraph();
        if (curve)
            ws.graphMut(id)->plots[0].addCurve(curve->first, curve->second);
    });
    openTab(TabKey{TabKey::Graph, id});
}

void MainWindow::newScript(const QString &code, const QString &name)
{
    ViewId id = 0;
    m_store.edit([&](Workspace &ws) {
        id = ws.addScript(code.isEmpty() ? scripting::templateCode(ws) : code);
        if (!name.isEmpty())
            ws.scriptMut(id)->name = name;
    });
    openTab(TabKey{TabKey::Script, id});
}

void MainWindow::openModeling(ViewId graph, int plot, std::optional<int> fit)
{
    openTab(TabKey{TabKey::Modeling});
    if (ModelingPanel *mp = ModelingPanel::instance())
        mp->target(graph, plot, fit);
}

void MainWindow::openStatistics(QId q)
{
    openTab(TabKey{TabKey::Statistics});
    if (StatsPanel *sp = StatsPanel::instance())
        sp->setQuantity(q);
}

GraphPanel *MainWindow::graphPanel(ViewId id) const
{
    auto it = m_docks.find(TabKey{TabKey::Graph, id}.text());
    if (it == m_docks.end() || !it->second)
        return nullptr;
    return qobject_cast<GraphPanel *>(it->second->widget());
}

// --- fichiers ----------------------------------------------------------------------------------

void MainWindow::guarded(const std::function<void()> &then)
{
    if (!m_store.dirty()) {
        then();
        return;
    }
    QMessageBox box(QMessageBox::Warning, u"Modifications non enregistrées"_s, u"Le document a été modifié. Voulez-vous enregistrer les modifications ?"_s,
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.button(QMessageBox::Save)->setText(u"Enregistrer"_s);
    box.button(QMessageBox::Discard)->setText(u"Ne pas enregistrer"_s);
    box.button(QMessageBox::Cancel)->setText(u"Annuler"_s);
    box.setDefaultButton(QMessageBox::Save);
    switch (box.exec()) {
    case QMessageBox::Save:
        if (save(false))
            then();
        break;
    case QMessageBox::Discard:
        then();
        break;
    default:
        break;
    }
}

void MainWindow::setWorkspace(Workspace ws, const QJsonValue &layout)
{
    closeAllDocks();
    m_store.reset(std::move(ws));
    if (layout.isObject())
        restoreLayout(layout);
    else
        defaultLayout();
    // les scripts sont relancés à l'ouverture : leurs résultats ne sont pas enregistrés
    if (!m_store.ws.scripts.empty())
        QTimer::singleShot(0, this, &MainWindow::runAllScripts);
    refresh();
}

void MainWindow::newFile()
{
    setWorkspace(Workspace{}, QJsonValue());
    m_path.clear();
    updateTitle();
    updateStatus();
    setStatus(u"Nouvel espace de travail : créez un tableur pour saisir vos mesures."_s);
}

void MainWindow::loadExample(int index)
{
    setWorkspace(examples::build(index), QJsonValue());
    m_path.clear();
    m_store.setDirty(true);
    updateTitle();
    updateStatus();
    setStatus(u"Exemple ouvert : %1."_s.arg(examples::names()[index]), icons::Status::Success);
}

bool MainWindow::openPath(const QString &path)
{
    // fichier de mesures : aperçu avant import, dans le document ouvert
    if (isMeasureFile(path)) {
        csvimport::open(*this, path, std::nullopt);
        return true;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_store.message(u"Impossible de lire %1 : %2"_s.arg(fileName(path), f.errorString()), icons::Status::Error);
        return false;
    }
    QString error;
    auto loaded = phy::load(QString::fromUtf8(f.readAll()), &error);
    if (!loaded) {
        m_store.message(u"%1 : %2"_s.arg(fileName(path), error), icons::Status::Error);
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    setWorkspace(std::move(loaded->ws), loaded->layout);
    addRecent(m_path);
    m_store.message(u"%1 ouvert"_s.arg(fileName(path)));
    updateTitle();
    updateStatus();
    return true;
}

void MainWindow::addRecent(const QString &path)
{
    QStringList &r = m_store.prefs.recent;
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 8)
        r.removeLast();
    m_store.prefs.save();
}

void MainWindow::flushScripts()
{
    for (const auto &[k, d] : m_docks)
        if (d && d->key.kind == TabKey::Script)
            if (auto *sp = qobject_cast<ScriptPanel *>(d->widget()))
                sp->flush();
}

bool MainWindow::save(bool saveAs, const QString &to)
{
    flushScripts();
    m_store.commitNow();
    QString path = to.isEmpty() ? m_path : resolvePath(to, u"phy"_s);
    if (path.isEmpty() || (saveAs && to.isEmpty())) {
        const QString base = m_store.ws.meta.title.trimmed().isEmpty() ? u"tp"_s : csv::sanitizeName(m_store.ws.meta.title, u"tp"_s);
        const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
        path = QFileDialog::getSaveFileName(this, u"Enregistrer"_s, QDir(dir).filePath(base + u".phy"_s), u"Espace de travail MeVReg (*.phy)"_s);
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += u".phy"_s;
    }
    const QByteArray bytes = phy::save(m_store.ws, layoutJson()).toUtf8();
    // écrit à côté, puis mis à la place de l'ancien fichier : un échec (disque plein, clé USB
    // retirée) laisse le document précédent intact
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        m_store.message(u"Enregistrement impossible : %1 ; le fichier précédent n'a pas été modifié"_s.arg(f.errorString()), icons::Status::Error);
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_store.setDirty(false);
    addRecent(m_path);
    m_store.message(u"%1 enregistré"_s.arg(fileName(path)));
    updateTitle();
    updateStatus();
    return true;
}

void MainWindow::importCsv(std::optional<ViewId> table)
{
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, u"Importer un fichier de données"_s, dir, u"Fichiers de données (*.csv *.txt *.tsv *.dat);;Tous les fichiers (*)"_s);
    if (!path.isEmpty())
        csvimport::open(*this, path, table);
}

void MainWindow::exportCsv(const std::vector<QId> &ids)
{
    csvimport::exportFile(*this, ids);
}

QString MainWindow::askCsvPath()
{
    const QString base = m_store.ws.meta.title.trimmed().isEmpty() ? u"donnees"_s : csv::sanitizeName(m_store.ws.meta.title, u"donnees"_s);
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, u"Exporter en CSV"_s, QDir(dir).filePath(base + u".csv"_s), u"Fichier CSV (*.csv *.txt)"_s);
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += u".csv"_s;
    return path;
}

bool MainWindow::writeCsv(const QString &path, const std::vector<QId> &ids, const csv::ExportOptions &opts, csv::Encoding encoding)
{
    const QByteArray bytes = csv::encodeText(csv::exportText(m_store.ws.doc, ids, opts), encoding);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        m_store.message(u"Export impossible : %1"_s.arg(f.errorString()), icons::Status::Error);
        return false;
    }
    m_store.message(u"Données exportées : %1"_s.arg(fileName(path)), icons::Status::Success);
    return true;
}

// --- exports -----------------------------------------------------------------------------------

QString MainWindow::saveDialog(const QString &defaultName, exporter::Format fmt)
{
    QString filter, ext;
    switch (fmt) {
    case exporter::Format::Png:
        filter = u"Image PNG (*.png)"_s;
        ext = u"png"_s;
        break;
    case exporter::Format::Svg:
        filter = u"Image vectorielle SVG (*.svg)"_s;
        ext = u"svg"_s;
        break;
    case exporter::Format::Pdf:
        filter = u"Document PDF (*.pdf)"_s;
        ext = u"pdf"_s;
        break;
    }
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, u"Exporter"_s, QDir(dir).filePath(csv::sanitizeName(defaultName, u"graphe"_s) + u'.' + ext), filter);
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += u'.' + ext;
    return path;
}

QSizeF MainWindow::graphExportSize(ViewId id) const
{
    const GraphView *g = m_store.ws.graph(id);
    if (GraphPanel *gp = graphPanel(id); gp && gp->canvas()->width() > 200 && gp->canvas()->height() > 150)
        return QSizeF(gp->canvas()->size());
    return exporter::defaultSize(g ? g->rows : 1, g ? g->cols : 1);
}

void MainWindow::exportGraph(ViewId id, exporter::Format fmt)
{
    const GraphView *g = m_store.ws.graph(id);
    if (!g)
        return;
    const QString path = saveDialog(g->name, fmt);
    if (path.isEmpty())
        return;
    QString error;
    if (exporter::writeGraph(path, fmt, *g, m_store.ws, graphExportSize(id), m_store.style(), &error))
        m_store.message(u"Graphe exporté : %1"_s.arg(fileName(path)));
    else
        m_store.message(u"Export impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::copyGraphImage(ViewId id)
{
    const GraphView *g = m_store.ws.graph(id);
    if (!g)
        return;
    const QImage img = exporter::graphImage(*g, m_store.ws.doc, graphExportSize(id), 2.0, m_store.style());
    QGuiApplication::clipboard()->setImage(img);
    m_store.message(u"Image du graphe copiée"_s);
}

bool MainWindow::printPages(const report::PageWriter &pages, const QString &title)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(title);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    printer.setFullPage(true);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(u"Imprimer"_s);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    if (!pages.paint(printer)) {
        m_store.message(u"Impression impossible"_s, icons::Status::Error);
        return false;
    }
    m_store.message(u"Document envoyé à l'imprimante"_s);
    return true;
}

namespace {

report::PageWriter writerFor(const Workspace &ws)
{
    const auto [title, line] = exporter::headerLines(ws.meta);
    return report::PageWriter(title, line);
}

void tableInto(report::PageWriter &w, const Workspace &ws, ViewId id, const Prefs &prefs)
{
    const TableView *t = ws.table(id);
    if (!t)
        return;
    QStringList headers;
    std::vector<QStringList> cols;
    for (QId c : t->columns) {
        const Quantity *q = ws.doc.get(c);
        if (!q)
            continue;
        headers << labelWithUnit(q->name, q->unit);
        QStringList v;
        for (double x : q->values)
            v << formatSig(x, prefs.sigDigits, prefs.style());
        cols.push_back(v);
    }
    w.table(headers, cols);
}

void scriptInto(report::PageWriter &w, const Store &store, ViewId id)
{
    const Script *s = store.ws.script(id);
    if (!s)
        return;
    w.heading(u"Script Python « %1 »"_s.arg(s->name));
    w.code(s->code);
    auto it = store.runs.find(id);
    if (it == store.runs.end())
        return;
    const ScriptRun &r = it->second;
    if (!r.stdoutText.trimmed().isEmpty()) {
        w.paragraph(u"Sortie du script :"_s, 10.5);
        QString out = r.stdoutText;
        while (out.endsWith(u'\n'))
            out.chop(1);
        w.code(out);
    }
    if (r.error)
        w.paragraph(u"Erreur : "_s + r.error->message, 10.5);
}

} // namespace

void MainWindow::tablePdf(ViewId id, bool print)
{
    const TableView *t = m_store.ws.table(id);
    if (!t)
        return;
    report::PageWriter w = writerFor(m_store.ws);
    w.heading(t->name);
    tableInto(w, m_store.ws, id, m_store.prefs);
    if (print) {
        printPages(w, t->name);
        return;
    }
    const QString path = saveDialog(t->name, exporter::Format::Pdf);
    if (path.isEmpty())
        return;
    QString error;
    if (w.writePdf(path, t->name, &error))
        m_store.message(u"PDF enregistré : %1"_s.arg(fileName(path)));
    else
        m_store.message(u"PDF impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::scriptPdf(ViewId id, bool print)
{
    const Script *s = m_store.ws.script(id);
    if (!s)
        return;
    report::PageWriter w = writerFor(m_store.ws);
    scriptInto(w, m_store, id);
    if (print) {
        printPages(w, s->name);
        return;
    }
    const QString path = saveDialog(s->name, exporter::Format::Pdf);
    if (path.isEmpty())
        return;
    QString error;
    if (w.writePdf(path, s->name, &error))
        m_store.message(u"PDF enregistré : %1"_s.arg(fileName(path)));
    else
        m_store.message(u"PDF impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::reportPdf(bool print)
{
    flushScripts();
    const Workspace &ws = m_store.ws;
    const NumberStyle style = m_store.style();
    report::PageWriter w = writerFor(ws);
    if (!ws.meta.notes.trimmed().isEmpty())
        w.paragraph(ws.meta.notes.trimmed(), 11.0);
    for (const auto &t : ws.tables) {
        const bool any = std::any_of(t.columns.begin(), t.columns.end(), [&](QId c) {
            const Quantity *q = ws.doc.get(c);
            return q && q->count() > 0;
        });
        if (!any)
            continue;
        w.heading(t.name);
        tableInto(w, ws, t.id, m_store.prefs);
    }
    QStringList params;
    for (const auto &p : ws.doc.parameters)
        params << (u"%1 = %2 %3"_s.arg(p.name, formatSig(p.value, 6, style), prettyUnit(p.unit))).trimmed();
    QStringList formulas;
    for (const auto &q : ws.doc.quantities)
        if (q.formula)
            formulas << u"%1 = %2"_s.arg(q.name, *q.formula);
    if (!params.isEmpty() || !formulas.isEmpty()) {
        w.heading(u"Calculs"_s);
        if (!params.isEmpty())
            w.paragraph(u"Paramètres : "_s + params.join(u" ; "_s), 11.0);
        for (const QString &f : formulas)
            w.paragraph(f, 11.0);
    }
    for (const auto &g : ws.graphs) {
        bool hasData = false;
        for (int k = 0; k < g.visiblePlots(); ++k)
            hasData = hasData || !g.plots[k].curves.empty();
        if (!hasData)
            continue;
        const double h = std::min(340.0 * g.rows, 860.0);
        // le titre reste avec son graphe
        w.ensure(h + 60.0);
        w.heading(g.name);
        w.graph(g, exporter::resolveAll(g, ws.doc, style), h, style);
        for (int k = 0; k < g.visiblePlots(); ++k) {
            for (const auto &f : g.plots[k].fits) {
                if (!f.visible || !f.result)
                    continue;
                const fit::FitResult &r = *f.result;
                const QString xn = ws.doc.name(f.x), yn = ws.doc.name(f.y);
                const auto model = fit::Model::make(f.kind, xn, ws.doc.paramValues());
                if (!model)
                    continue;
                QString txt = u"Modèle %1 : %2"_s.arg(f.kind.label().toLower(), model->display(r.params, xn, yn, style));
                for (int i = 0; i < model->params().size() && static_cast<size_t>(i) < r.params.size(); ++i) {
                    const double u = static_cast<size_t>(i) < r.uncertainties.size() ? r.uncertainties[i] : NAN;
                    if (std::isfinite(u) && u > 0.0)
                        txt += u" ; %1 = %2"_s.arg(model->params()[i], roundMeasure(r.params[i], u, 2, style).toText(QString()));
                }
                txt += u" ; R² = "_s + formatSig(r.r2, 5, style);
                w.paragraph(txt, 10.5);
            }
        }
    }
    for (const auto &[name, d] : m_store.drawings) {
        const double ratio = std::abs((d.desc.ymax - d.desc.ymin) / (d.desc.xmax - d.desc.xmin));
        const double h = std::clamp(w.width() * ratio + 56.0, 150.0, 800.0);
        w.ensure(h + 60.0);
        w.heading(name);
        w.drawing(d, h);
    }
    for (const auto &s : ws.scripts)
        scriptInto(w, m_store, s.id);
    const QString title = ws.meta.title.trimmed().isEmpty() ? u"compte-rendu"_s : ws.meta.title.trimmed();
    if (print) {
        printPages(w, title);
        return;
    }
    const QString path = saveDialog(title, exporter::Format::Pdf);
    if (path.isEmpty())
        return;
    QString error;
    if (w.writePdf(path, title, &error))
        m_store.message(u"Compte rendu enregistré : %1 (%2 page%3)"_s.arg(fileName(path)).arg(w.pageCount()).arg(plural(static_cast<size_t>(w.pageCount()))));
    else
        m_store.message(u"PDF impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::printGraph(ViewId id)
{
    const GraphView *g = m_store.ws.graph(id);
    if (!g)
        return;
    report::PageWriter w = writerFor(m_store.ws);
    const QSizeF size = graphExportSize(id);
    // le graphe à la largeur de la page, dans les proportions de l'écran
    const double h = std::clamp(w.width() * size.height() / std::max(size.width(), 1.0), 200.0, report::PAGE_H - 220.0);
    w.graph(*g, exporter::resolveAll(*g, m_store.ws.doc, m_store.style()), h, m_store.style());
    printPages(w, g->name);
}

void MainWindow::exportDrawing(const QString &name, exporter::Format fmt)
{
    auto it = m_store.drawings.find(name);
    if (it == m_store.drawings.end())
        return;
    const drawing::Drawing &d = it->second;
    const QString path = saveDialog(name, fmt);
    if (path.isEmpty())
        return;
    const QSizeF size = drawingSize(d);
    bool ok = false;
    switch (fmt) {
    case exporter::Format::Png:
        ok = drawingImage(d, 2.0).save(path, "PNG");
        break;
    case exporter::Format::Svg: {
        QSvgGenerator gen;
        gen.setFileName(path);
        gen.setResolution(96);
        gen.setSize(size.toSize());
        gen.setViewBox(QRectF(QPointF(0, 0), size));
        gen.setTitle(name);
        QPainter p;
        if ((ok = p.begin(&gen))) {
            p.fillRect(QRectF(QPointF(0, 0), size), Qt::white);
            drawing::paint(p, d, size);
            p.end();
        }
        break;
    }
    case exporter::Format::Pdf: {
        QPdfWriter pdf(path);
        pdf.setResolution(96);
        pdf.setTitle(name);
        pdf.setCreator(u"MeVReg"_s);
        pdf.setPageSize(QPageSize(size * 72.0 / 96.0, QPageSize::Point, QString(), QPageSize::ExactMatch));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter p;
        if ((ok = p.begin(&pdf))) {
            p.fillRect(QRectF(QPointF(0, 0), size), Qt::white);
            drawing::paint(p, d, size);
            p.end();
        }
        break;
    }
    }
    if (ok)
        m_store.message(u"Dessin exporté : %1"_s.arg(fileName(path)));
    else
        m_store.message(u"Export impossible"_s, icons::Status::Error);
}

void MainWindow::copyDrawing(const QString &name)
{
    auto it = m_store.drawings.find(name);
    if (it == m_store.drawings.end())
        return;
    QGuiApplication::clipboard()->setImage(drawingImage(it->second, 1.5));
    m_store.message(u"Image du dessin copiée"_s);
}

void MainWindow::exportHistogram(exporter::Format fmt)
{
    StatsPanel *sp = StatsPanel::instance();
    if (!sp || !sp->quantity())
        return;
    const auto h = histogramPlot(m_store.ws.doc, *sp->quantity(), sp->bins(), m_store.style());
    if (!h)
        return;
    const QString path = saveDialog(u"histogramme"_s, fmt);
    if (path.isEmpty())
        return;
    QString error;
    if (exporter::writePlots(path, fmt, h->view, h->data, m_store.ws.meta, QSizeF(720, 460), m_store.style(), &error))
        m_store.message(u"Histogramme exporté : %1"_s.arg(fileName(path)));
    else
        m_store.message(u"Export impossible : %1"_s.arg(error), icons::Status::Error);
}

// --- Python ------------------------------------------------------------------------------------

void MainWindow::runAllScripts()
{
    for (const auto &s : m_store.ws.scripts)
        m_store.runScript(s.id);
}

void MainWindow::onScriptApplied(ViewId, const std::vector<ViewId> &openGraphs, const std::vector<std::pair<ViewId, QString>> &figs,
                                 const QStringList &newDrawings, const QJsonArray &actions)
{
    for (ViewId g : openGraphs)
        openTab(TabKey{TabKey::Graph, g});
    for (const auto &[g, path] : figs)
        savefig(g, path);
    for (const QString &name : newDrawings)
        openTab(TabKey{TabKey::Drawing, 0, name});
    // ordres du module mevreg : fenêtres, fichiers, messages
    runScriptActions(actions);
}

QString MainWindow::resolvePath(const QString &path, const QString &suffix) const
{
    // chemin relatif : à côté du document (sinon dans le dossier courant)
    QString p = path;
    if (QFileInfo(p).isRelative()) {
        const QString base = m_path.isEmpty() ? QDir::currentPath() : QFileInfo(m_path).absolutePath();
        p = QDir(base).filePath(p);
    }
    if (!suffix.isEmpty() && QFileInfo(p).suffix().isEmpty())
        p += u'.' + suffix;
    return p;
}

std::optional<TabKey> MainWindow::windowNamed(const QString &name) const
{
    static const std::vector<std::pair<QString, TabKey::Kind>> panels = {
        {u"Grandeurs"_s, TabKey::Quantities}, {u"Calculs"_s, TabKey::Calculations}, {u"Modélisation"_s, TabKey::Modeling},
        {u"Modelisation"_s, TabKey::Modeling}, {u"Statistiques"_s, TabKey::Statistics}};
    for (const auto &[n, k] : panels)
        if (n.compare(name, Qt::CaseInsensitive) == 0)
            return TabKey{k};
    for (const auto &t : m_store.ws.tables)
        if (t.name == name)
            return TabKey{TabKey::Table, t.id};
    for (const auto &g : m_store.ws.graphs)
        if (g.name == name)
            return TabKey{TabKey::Graph, g.id};
    for (const auto &sc : m_store.ws.scripts)
        if (sc.name == name)
            return TabKey{TabKey::Script, sc.id};
    if (m_store.drawings.count(name))
        return TabKey{TabKey::Drawing, 0, name};
    return std::nullopt;
}

void MainWindow::runScriptActions(const QJsonArray &actions)
{
    for (const QJsonValue &v : actions) {
        const QJsonObject o = v.toObject();
        const QString order = o.value(u"ordre"_s).toString(), name = o.value(u"nom"_s).toString();
        auto window = [&]() -> std::optional<TabKey> {
            const auto k = windowNamed(name);
            if (!k)
                m_store.message(u"mevreg : pas de fenêtre « %1 »"_s.arg(name), icons::Status::Warning);
            return k;
        };
        if (order == u"ouvrir" && name.compare(u"Notice"_s, Qt::CaseInsensitive) == 0) {
            openHelp();
        } else if (order == u"ouvrir") {
            if (const auto k = window())
                openTab(*k);
        } else if (order == u"cacher") {
            if (const auto k = window())
                closeTab(*k);
        } else if (order == u"fermer") {
            if (const auto k = window()) {
                if (k->kind == TabKey::Table || k->kind == TabKey::Graph || k->kind == TabKey::Script || k->kind == TabKey::Drawing)
                    deleteWindow(*k);
                else
                    closeTab(*k);
            }
        } else if (order == u"detacher" || order == u"rattacher") {
            if (const auto k = window()) {
                openTab(*k);
                if (Dock *d = m_docks[k->text()]; d)
                    order == u"detacher" ? detachDock(d) : attachDock(d);
            }
        } else if (order == u"disposition") {
            defaultLayout();
        } else if (order == u"enregistrer") {
            save(false, o.value(u"chemin"_s).toString());
        } else if (order == u"exporter_csv") {
            std::vector<QId> ids;
            if (o.contains(u"grandeurs"_s)) {
                for (const QJsonValue &g : o.value(u"grandeurs"_s).toArray())
                    if (const Quantity *q = m_store.ws.doc.byName(g.toString()))
                        ids.push_back(q->id);
            } else {
                for (const auto &q : m_store.ws.doc.quantities)
                    ids.push_back(q.id);
            }
            const QString sep = o.value(u"separateur"_s).toString(u";"_s);
            const csv::ExportOptions opts{sep.isEmpty() ? QChar(u';') : sep.at(0), o.value(u"virgule"_s).toBool(true)};
            writeCsv(resolvePath(o.value(u"chemin"_s).toString(), u"csv"_s), ids, opts);
        } else if (order == u"exporter_graphe") {
            if (const auto k = windowNamed(name); k && k->kind == TabKey::Graph)
                savefig(k->id, o.value(u"chemin"_s).toString());
            else
                m_store.message(u"mevreg : pas de graphe « %1 »"_s.arg(name), icons::Status::Warning);
        } else if (order == u"message") {
            m_store.message(o.value(u"texte"_s).toString());
        }
    }
}

void MainWindow::savefig(ViewId graph, const QString &path)
{
    const QString p = resolvePath(path, QString());
    const GraphView *g = m_store.ws.graph(graph);
    if (!g)
        return;
    QString error;
    if (exporter::writeGraph(p, exporter::formatOf(p), *g, m_store.ws, graphExportSize(graph), m_store.style(), &error))
        m_store.message(u"Figure enregistrée : %1"_s.arg(QDir::toNativeSeparators(p)));
    else
        m_store.message(u"savefig impossible : %1"_s.arg(error), icons::Status::Error);
}

// --- événements --------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    flushScripts();
    if (!m_allowClose && m_store.dirty()) {
        event->ignore();
        guarded([this] {
            m_allowClose = true;
            QTimer::singleShot(0, this, &QWidget::close);
        });
        return;
    }
    m_store.prefs.save();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &u : event->mimeData()->urls()) {
            const QString ext = QFileInfo(u.toLocalFile()).suffix().toLower();
            if (ext == u"phy" || ext == u"csv" || ext == u"txt" || ext == u"tsv" || ext == u"dat") {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &u : event->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == u"phy") {
            event->acceptProposedAction();
            QTimer::singleShot(0, this, [this, path] { guarded([this, path] { openPath(path); }); });
            return;
        }
        if (isMeasureFile(path)) {
            event->acceptProposedAction();
            // aperçu avant import, une fois le glisser-déposer terminé
            QTimer::singleShot(0, this, [this, path] { csvimport::open(*this, path, std::nullopt); });
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // taille de la fenêtre non agrandie, retrouvée au prochain lancement
    if (!(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) && width() >= 400 && height() >= 300) {
        m_previousWindowSize = m_windowSize;
        m_windowSize = size();
        m_store.prefs.windowSize = m_windowSize;
    }
    updateStatus();
    // agrandissement des préférences, borné par la nouvelle largeur de la fenêtre
    if (const double zoom = std::min(m_store.prefs.uiZoom, maxUiZoom()); zoom != m_uiZoom)
        applyUiZoom(zoom);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        m_store.prefs.maximized = isMaximized();
        // agrandissement signalé après le redimensionnement : la taille notée est celle de l'écran
        if ((isMaximized() || isFullScreen()) && m_windowSize == size() && m_previousWindowSize.isValid())
            m_store.prefs.windowSize = m_windowSize = m_previousWindowSize;
    } else if (event->type() == QEvent::PaletteChange && m_actTheme) {
        // Thème clair ou sombre (bouton, ou bureau suivi) : bouton du thème, couleurs de la barre
        // d'état et des textes discrets, puis tout ce qui est dessiné par le programme, une fois la
        // nouvelle palette transmise à toutes les fenêtres.
        QTimer::singleShot(0, this, [this] {
            updateThemeButton();
            styleStatusBar();
            if (auto *hint = m_emptyHint->findChild<QLabel *>(u"welcomeTagline"_s))
                setTextColor(hint, theme::weakColor());
            if (auto *hint = m_emptyHint->findChild<QLabel *>(u"welcomeHint"_s))
                setTextColor(hint, theme::weakColor());
            Q_EMIT m_store.displayChanged();
        });
    }
}
