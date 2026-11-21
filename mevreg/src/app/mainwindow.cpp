#include "app/mainwindow.h"

#include "app/about.h"
#include "app/dialogs.h"
#include "app/graphwidget.h"
#include "app/guide.h"
#include "app/help.h"
#include "app/icons.h"
#include "app/scriptpanel.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/csv.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionMenuItem>
#include <QStylePainter>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace {

QString pluriel(size_t n)
{
    return n > 1 ? u"s"_s : QString();
}

/// Texte d'une étiquette de la couleur donnée (la palette suit le thème).
void definit_couleur_texte(QWidget *widget, const QColor &color)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText, color);
    palette.setColor(QPalette::ButtonText, color);
    widget->setPalette(palette);
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

} // namespace

// --- ligne du menu Fenêtres -------------------------------------------------------------------

namespace {

/// Ligne du menu Fenêtres, dessinée comme les autres entrées du menu (coche, icône, nom), avec une
/// croix à droite pour fermer la fenêtre pour de bon.
class WindowRow : public QWidget {
public:
    WindowRow(QMenu *menu, const QString &text, const QIcon &icon, bool shown, std::function<void()> open, std::function<void()> close)
        : QWidget(menu), m_menu(menu), m_text(text), m_icon(icon), m_shown(shown), m_open(std::move(open))
    {
        setAccessibleName(text);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        m_close = new QToolButton(this);
        m_close->setObjectName(u"closeWindow"_s);
        m_close->setIcon(icons::obtient("close"));
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
    QString m_text;
    QIcon m_icon;
    bool m_shown;
    std::function<void()> m_open;
    QToolButton *m_close;
};

} // namespace

// --- MainWindow --------------------------------------------------------------------------------

MainWindow::~MainWindow()
{
    // La perte de focus peut valider une cellule. Masquer la fenêtre pendant
    // que ses panneaux et son état sont encore disponibles.
    hide();
    disconnect(&m_store, nullptr, this, nullptr);
}

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

    construit_menus();
    construit_barre_outils();
    construit_coin_des_menus();
    // aucune fenêtre ouverte : la page d'accueil occupe la place
    m_emptyHint = construit_page_accueil();
    setCentralWidget(m_emptyHint);
    construit_barre_etat();
    construit_zoom();

    connect(&store, &Store::message_demande, this, &MainWindow::affiche_statut);
    connect(&store, &Store::espace_travail_change, this, &MainWindow::rafraichit);
    m_delaiSecours.setSingleShot(true);
    auto planifie = [this] {
        if (!m_delaiSecours.isActive() || m_delaiSecours.remainingTime() > 2000)
            m_delaiSecours.start(2000);
    };
    connect(&store, &Store::espace_travail_change, this, planifie);
    connect(&store, &Store::code_script_change, this, planifie);
    connect(&store, &Store::modification_changee, this, planifie);
    connect(&m_delaiSecours, &QTimer::timeout, this, &MainWindow::ecrit_secours);
    connect(&store, &Store::modification_changee, this, [this] {
        maj_titre();
        maj_statut();
    });
    connect(&store, &Store::etat_script_change, this, [this] { maj_titres_fenetres(); });
    connect(&store, &Store::script_applique, this, &MainWindow::apres_script_applique);
    connect(&store, &Store::affichage_change, this, [this] {
        maj_statut();
        update();
    });

    choisit_mode(store.prefs.modeSimple);
    affiche_statut(u"Créez un tableur pour saisir vos mesures, ou ouvrez un fichier."_s);
    rafraichit();
}

// --- menus et barre d'outils -------------------------------------------------------------------

void MainWindow::construit_menus()
{
    // Les menus donnent tout ; la barre d'outils, ce qui sert le plus (texte avec émoji, donné
    // par `iconText` : le menu garde son libellé et son icône).
    QMenu *file = m_menuFile = menuBar()->addMenu(icons::obtient("open"), u"&Fichier"_s);
    file->addAction(icons::obtient("new"), u"Nouveau"_s, QKeySequence::New, this, [this] {
        if (confirme_remplacement())
            nouveau_fichier();
    });
    QAction *open = file->addAction(icons::obtient("open"), u"Ouvrir…"_s, QKeySequence::Open, this, &MainWindow::ouvre_dialogue);
    file->addAction(icons::obtient("restore"), u"Récupérer une séance interrompue…"_s, this, [this] { propose_recuperation(false); });
    open->setObjectName(u"open"_s);
    open->setToolTip(u"Ouvrir un document MeVReg (.phy) ou un fichier de mesures (CSV) (Ctrl+O)"_s);
    m_recentMenu = file->addMenu(icons::obtient("list"), u"Fichiers récents"_s);
    connect(m_recentMenu, &QMenu::aboutToShow, this, [this] {
        m_recentMenu->clear();
        if (m_store.prefs.recent.isEmpty())
            m_recentMenu->addAction(icons::obtient("info"), u"Aucun"_s)->setEnabled(false);
        for (const QString &r : m_store.prefs.recent) {
            QAction *a = m_recentMenu->addAction(icons::obtient("new"), QFileInfo(r).fileName(), this, [this, r] { demande_ouverture(r); });
            a->setToolTip(r);
            a->setStatusTip(r);
        }
    });
    file->addSeparator();
    QAction *saveAction = file->addAction(icons::obtient("save"), u"Enregistrer"_s, QKeySequence::Save, this, [this] { enregistre(false); });
    saveAction->setObjectName(u"save"_s);
    saveAction->setToolTip(u"Enregistrer l'espace de travail (Ctrl+S)"_s);
    file->addAction(icons::obtient("save-as"), u"Enregistrer sous…"_s, QKeySequence::SaveAs, this, [this] { enregistre(true); });
    file->addSeparator();
    QAction *rep = file->addAction(icons::obtient("pdf"), u"Compte rendu en PDF…"_s, this, [this] { rapport_pdf(false); });
    rep->setStatusTip(u"Tableaux, calculs, graphes, modèles, dessins et scripts, avec les informations du document"_s);
    file->addAction(icons::obtient("print"), u"Imprimer le compte rendu…"_s, QKeySequence::Print, this, [this] { rapport_pdf(true); });
    file->addSeparator();
    file->addAction(icons::obtient("csv"), u"Importer un fichier CSV…"_s, this, [this] { importe_csv(std::nullopt); });
    file->addAction(icons::obtient("export"), u"Exporter toutes les grandeurs en CSV…"_s, this, [this] {
        std::vector<QId> ids;
        for (const auto &q : m_store.ws.doc.quantities)
            ids.push_back(q.id);
        exporte_csv(ids);
    });
    file->addAction(icons::obtient("python"), u"Exporter vers Python (Capytale)…"_s, this, [this] { dialogs::export_python(*this); });
    file->addSeparator();
    file->addAction(icons::obtient("info"), u"Informations du document…"_s, this, [this] { dialogs::meta(*this); });
    file->addSeparator();
    file->addAction(icons::obtient("close"), u"Quitter"_s, QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(icons::obtient("pencil"), u"É&dition"_s);
    m_undo = edit->addAction(icons::obtient("undo"), u"Annuler"_s, QKeySequence::Undo, this, [this] { m_store.defait(); });
    m_undo->setToolTip(u"Annuler la dernière modification (Ctrl+Z)"_s);
    m_redo = edit->addAction(icons::obtient("redo"), u"Rétablir"_s, this, [this] { m_store.retablit(); });
    m_redo->setShortcuts({QKeySequence(u"Ctrl+Y"_s), QKeySequence(u"Ctrl+Shift+Z"_s)});
    m_redo->setToolTip(u"Rétablir la modification annulée (Ctrl+Y)"_s);
    edit->addSeparator();
    edit->addAction(icons::obtient("gear"), u"Préférences…"_s, QKeySequence::Preferences, this, [this] { dialogs::preferences(*this); });

    QMenu *data = menuBar()->addMenu(icons::obtient("table"), u"Do&nnées"_s);
    data->addAction(icons::obtient("plus"), u"Nouvelle grandeur…"_s, this, [this] { dialogs::nouvelle_grandeur(*this, false); });
    data->addAction(icons::obtient("function"), u"Nouvelle grandeur calculée…"_s, this, [this] { dialogs::nouvelle_grandeur(*this, true); });
    data->addAction(icons::obtient("sliders"), u"Nouveau paramètre…"_s, this, [this] { dialogs::nouveau_parametre(*this); });
    data->addSeparator();
    data->addAction(icons::obtient("table"), u"Nouveau tableur"_s, this, [this] { nouveau_tableur(m_store.ws.orphelins()); });

    QMenu *graphs = menuBar()->addMenu(icons::obtient("chart"), u"&Graphes"_s);
    graphs->addAction(icons::obtient("chart"), u"Nouveau graphe"_s, this, [this] { nouveau_graphe(); });
    QMenu *yx = graphs->addMenu(icons::obtient("scatter"), u"Nouveau graphe y(x)"_s);
    connect(yx, &QMenu::aboutToShow, this, [this, yx] {
        yx->clear();
        const auto &qs = m_store.ws.doc.quantities;
        if (qs.size() < 2) {
            yx->addAction(icons::obtient("info"), u"Il faut au moins deux grandeurs"_s)->setEnabled(false);
            return;
        }
        const QId x = qs.front().id;
        for (size_t k = 1; k < qs.size(); ++k) {
            const QId y = qs[k].id;
            yx->addAction(icons::obtient("scatter"), u"%1 en fonction de %2"_s.arg(qs[k].name, qs.front().name), this, [this, x, y] { nouveau_graphe(std::pair(x, y)); });
        }
    });

    QMenu *an = menuBar()->addMenu(icons::obtient("model"), u"Ana&lyse"_s);
    an->addAction(icons::obtient("model"), u"Modélisation"_s, this, [this] { ouvre_onglet(TabKey{TabKey::Modeling}); });
    an->addAction(icons::obtient("stats"), u"Statistiques"_s, this, [this] { ouvre_onglet(TabKey{TabKey::Statistics}); });
    an->addSeparator();
    an->addAction(icons::obtient("derivative"), u"Dérivée…"_s, this, [this] { dialogs::derivee(*this, false); });
    an->addAction(icons::obtient("derivative2"), u"Dérivée seconde…"_s, this, [this] { dialogs::derivee(*this, true); });
    an->addAction(icons::obtient("integral"), u"Intégrale…"_s, this, [this] { dialogs::integrale(*this); });
    an->addAction(icons::obtient("wave"), u"Spectre (analyse de Fourier)…"_s, this, [this] { dialogs::spectre(*this); });
    an->addSeparator();
    an->addAction(icons::obtient("tangent"), u"Méthode des tangentes…"_s, this, [this] { dialogs::tangentes(*this); });
    an->addAction(icons::obtient("slope"), u"Maximum de pente…"_s, this, [this] { dialogs::methode_derivee(*this); });

    m_pythonMenu = menuBar()->addMenu(icons::obtient("python"), u"&Python"_s);
    connect(m_pythonMenu, &QMenu::aboutToShow, this, [this] {
        m_pythonMenu->clear();
        m_pythonMenu->addAction(icons::obtient("python"), u"Nouveau script"_s, this, [this] { nouveau_script(); });
        if (!m_store.ws.scripts.empty()) {
            m_pythonMenu->addAction(icons::obtient("play"), u"Exécuter tous les scripts"_s, this, [this] {
                transmet_saisie_scripts();
                // demandé par l'utilisateur : les scripts d'un document reçu aussi
                m_store.blockedScripts.clear();
                lance_tous_scripts();
            });
            m_pythonMenu->addSeparator();
            for (const auto &s : m_store.ws.scripts) {
                const ViewId id = s.id;
                m_pythonMenu->addAction(icons::obtient("python"), s.name, this, [this, id] { ouvre_onglet(TabKey{TabKey::Script, id}); });
            }
        }
        m_pythonMenu->addSeparator();
        m_pythonMenu->addAction(icons::obtient("export"), u"Exporter vers Python (Capytale)…"_s, this, [this] { dialogs::export_python(*this); });
    });

    m_windowsMenu = menuBar()->addMenu(icons::obtient("layout"), u"Fenê&tres"_s);
    connect(m_windowsMenu, &QMenu::aboutToShow, this, [this] {
        m_windowsMenu->clear();
        // un clic cache une fenêtre affichée (cochée), affiche une fenêtre cachée
        auto toggle = [this](const TabKey &k) {
            if (est_ouvert(k))
                cache_onglet(k);
            else
                ouvre_onglet(k);
        };
        auto add = [this, toggle](const QString &text, const TabKey &k) {
            QAction *a = m_windowsMenu->addAction(icons::obtient(k.icone()), text, this, [toggle, k] { toggle(k); });
            a->setCheckable(true);
            a->setChecked(est_ouvert(k));
        };
        add(u"Grandeurs"_s, TabKey{TabKey::Quantities});
        add(u"Calculs"_s, TabKey{TabKey::Calculations});
        add(u"Modélisation"_s, TabKey{TabKey::Modeling});
        add(u"Statistiques"_s, TabKey{TabKey::Statistics});
        // tableurs, graphes, scripts, dessins : cochés s'ils sont affichés (sinon cachés), une
        // croix pour les fermer (supprimer du document)
        if (!m_store.ws.tables.empty() || !m_store.ws.graphs.empty() || !m_store.ws.scripts.empty() || !m_store.drawings.empty())
            m_windowsMenu->addSection(u"Tableurs, graphes, scripts — ✕ : fermer"_s);
        auto row = [this, toggle](const QString &text, const TabKey &k) {
            auto *action = new QWidgetAction(m_windowsMenu);
            action->setText(text);
            action->setIcon(icons::obtient(k.icone()));
            auto *w = new WindowRow(m_windowsMenu, text, action->icon(), est_ouvert(k), [toggle, k] { toggle(k); }, [this, k] { supprime_fenetre(k); });
            w->setObjectName(u"windowRow:"_s + k.texte());
            action->setDefaultWidget(w);
            connect(action, &QAction::triggered, this, [toggle, k] { toggle(k); });
            m_windowsMenu->addAction(action);
        };
        for (const auto &t : m_store.ws.tables)
            row(t.name, TabKey{TabKey::Table, t.id});
        for (const auto &g : m_store.ws.graphs)
            row(g.name, TabKey{TabKey::Graph, g.id});
        for (const auto &s : m_store.ws.scripts)
            row(s.name, TabKey{TabKey::Script, s.id});
        std::set<QString> dessins;
        for (const auto &s : m_store.ws.scripts)
            dessins.insert(s.dessins.begin(), s.dessins.end());
        for (const auto &[name, d] : m_store.drawings)
            dessins.insert(name);
        for (const QString &nom : dessins)
            row(nom, TabKey{TabKey::Drawing, 0, nom});
        m_windowsMenu->addSeparator();
        m_windowsMenu->addAction(icons::obtient("layout"), u"Disposition par défaut"_s, this, [this] { disposition_par_defaut(); });
        // une fenêtre détachée se rattache par son bouton « Rattacher », ou toutes d'un coup ici
        std::vector<QPointer<Dock>> detached;
        for (const auto &[k, d] : m_docks)
            if (d && d->detache())
                detached.emplace_back(d);
        if (!detached.empty())
            m_windowsMenu->addAction(icons::obtient("attach"), u"Rattacher les fenêtres détachées"_s, this, [this, detached] {
                for (const QPointer<Dock> &d : detached)
                    if (d)
                        rattache_fenetre(d);
            });
    });

    m_menusExperts = {data->menuAction(), graphs->menuAction(), an->menuAction(), m_pythonMenu->menuAction(), m_windowsMenu->menuAction()};
    QMenu *mode = menuBar()->addMenu(icons::obtient("sliders"), u"&Mode"_s);
    auto *groupe = new QActionGroup(this);
    m_modeSimpleAction = mode->addAction(icons::obtient("steps"), u"Simplifié"_s, this, [this] { choisit_mode(true); });
    m_modeExpertAction = mode->addAction(icons::obtient("sliders"), u"Expert"_s, this, [this] { choisit_mode(false); });
    for (auto *action : {m_modeSimpleAction, m_modeExpertAction}) {
        action->setCheckable(true);
        groupe->addAction(action);
    }
    QMenu *help = menuBar()->addMenu(icons::obtient("book"), u"&Aide"_s);
    QAction *manual = help->addAction(icons::obtient("help"), u"Notice d'utilisation…"_s, QKeySequence::HelpContents, this, [this] { ouvre_notice(); });
    manual->setObjectName(u"manual"_s);
    QAction *aboutAction = help->addAction(icons::obtient("info"), u"À propos de MeVReg…"_s, this, [this] { about::montre(this); });
    aboutAction->setMenuRole(QAction::AboutRole);
    QAction *aboutQt = help->addAction(icons::obtient("info"), u"À propos de Qt…"_s, qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);
}

void MainWindow::construit_barre_outils()
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
        QAction *a = m_toolbar->addAction(icons::obtient(icon), text, this, fn);
        a->setToolTip(tip);
        return a;
    };
    add("list", u"Grandeurs"_s, u"Toutes les grandeurs du document : en créer, les ajouter aux tableurs et aux graphes"_s,
        [this] { ouvre_onglet(TabKey{TabKey::Quantities}); });
    add("table", u"Tableur"_s, u"Nouveau tableur, pour saisir des mesures"_s, [this] { nouveau_tableur(m_store.ws.orphelins()); });
    add("scatter", u"Graphe"_s, u"Nouveau graphe"_s, [this] { nouveau_graphe(); });
    add("python", u"Python"_s, u"Nouveau script Python"_s, [this] { nouveau_script(); });
}

void MainWindow::construit_coin_des_menus()
{
    // À droite de la barre de menus, sur sa ligne : le thème et la langue. Barre de menus
    // dans la fenêtre, même avec un menu global du bureau, qui cacherait ces boutons.
    menuBar()->setNativeMenuBar(false);
    // thème et langue : un clic passe à l'autre
    m_actTheme = new QAction(this);
    m_actTheme->setObjectName(u"theme"_s);
    connect(m_actTheme, &QAction::triggered, this, [this] {
        m_store.prefs.theme = theme::est_sombre() ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark;
        // la nouvelle palette (changeEvent) met à jour le bouton et redessine les fenêtres
        theme::choisit(m_store.prefs.theme);
        m_store.prefs.enregistre();
    });
    m_actLanguage = new QAction(this);
    m_actLanguage->setObjectName(u"language"_s);
    m_actLanguage->setIcon(icons::drapeau(icons::Lang::Fr));
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
    maj_bouton_theme();
    dimensionne_coin_des_menus();
}

void MainWindow::dimensionne_coin_des_menus()
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

void MainWindow::maj_bouton_theme()
{
    const bool dark = theme::est_sombre();
    m_actTheme->setIcon(icons::theme(dark));
    m_actTheme->setText(dark ? u"Thème sombre. Cliquer pour passer au thème clair."_s : u"Thème clair. Cliquer pour passer au thème sombre."_s);
}

void MainWindow::construit_barre_etat()
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
    style_barre_etat();
}

void MainWindow::affiche_statut(const QString &text, icons::Status kind)
{
    m_status = {text, kind};
    m_statusLabel->setText(text);
    ajuste_barre_etat();
    const int side = m_statusLabel->fontMetrics().height();
    m_statusIcon->setPixmap(icons::etat(kind).pixmap(QSize(side, side), devicePixelRatioF()));
    m_statusIcon->setVisible(!text.isEmpty());
    QColor color = QApplication::palette().color(QPalette::WindowText);
    switch (kind) {
    case icons::Status::Info: break;
    case icons::Status::Success: color = theme::couleur_reussite(); break;
    case icons::Status::Warning: color = theme::couleur_attention(); break;
    case icons::Status::Error: color = theme::couleur_erreur(); break;
    }
    definit_couleur_texte(m_statusLabel, color);
}

void MainWindow::ajuste_barre_etat()
{
    // Hauteur donnée à la barre : celle que QLabel estime coupe le texte dès une vingtaine de
    // caractères (et la barre changeait de hauteur d'un message à l'autre). Ici, une ligne si le
    // message tient dans la largeur qu'il occupe, plusieurs sinon.
    const int line = std::max(m_statusLabel->heightForWidth(QWIDGETSIZE_MAX), m_statusFile->sizeHint().height());
    statusBar()->setFixedHeight(std::max(line, m_statusLabel->heightForWidth(m_statusLabel->width())) + theme::px(6));
}

void MainWindow::style_barre_etat()
{
    // Bande distincte du reste de la fenêtre : fond un peu plus foncé, filet au-dessus ; document
    // et formules en erreur séparés du message par un filet vertical. Couleurs données ici, la
    // feuille de style l'emportant sur la palette des étiquettes qu'elle décrit.
    const QPalette palette = QApplication::palette();
    const QColor background = palette.color(QPalette::Window).darker(theme::est_sombre() ? 135 : 107);
    statusBar()->setStyleSheet(u"QStatusBar { background: %1; border-top: 1px solid %2; }"
                               u"QStatusBar::item { border: none; }"
                               u"QLabel#documentStatus { color: %3; }"
                               u"QLabel#errorStatus { color: %4; }"_s.arg(background.name(), palette.color(QPalette::Mid).name(),
                                                                         theme::couleur_attenuee().name(), theme::couleur_erreur().name()));
    affiche_statut(m_status.first, m_status.second);
}

QWidget *MainWindow::construit_page_accueil()
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
    const auto scaled = [](QWidget *w, double factor, bool bold = false) { theme::definit_police_relative(w, factor, bold ? QFont::Bold : QFont::Normal); };
    layout->addStretch(1);
    auto *logo = new QLabel(page);
    logo->setObjectName(u"welcomeLogo"_s);
    logo->setPixmap(about::icone().pixmap(theme::px(QSize(96, 96)), devicePixelRatioF()));
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(8);
    scaled(centered(new QLabel(about::NAME, page)), 2.2, true);
    QLabel *tagline = centered(new QLabel(about::TAGLINE, page));
    tagline->setObjectName(u"welcomeTagline"_s);
    scaled(tagline, 1.15);
    definit_couleur_texte(tagline, theme::couleur_attenuee());

    layout->addSpacing(32);
    m_welcomeTable = new QPushButton(icons::obtient("table"), u"Nouveau tableur"_s, page);
    m_welcomeTable->setObjectName(u"welcomeTable"_s);
    m_welcomeTable->setToolTip(u"Un tableur vide, pour saisir vos mesures"_s);
    connect(m_welcomeTable, &QPushButton::clicked, this, [this] {
        if (m_modeSimple) dialogs::tableur_guide(*this);
        else nouveau_tableur(m_store.ws.orphelins());
    });
    // pas d'exemples livrés : élèves et professeurs ouvrent leurs propres fichiers
    m_welcomeOpen = new QPushButton(icons::obtient("open"), u"Ouvrir…"_s, page);
    m_welcomeOpen->setObjectName(u"welcomeOpen"_s);
    m_welcomeOpen->setToolTip(u"Ouvrir un document MeVReg (.phy) ou un fichier de mesures (CSV)"_s);
    connect(m_welcomeOpen, &QPushButton::clicked, this, &MainWindow::ouvre_dialogue);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    buttons->addStretch();
    for (QPushButton *button : {m_welcomeTable, m_welcomeOpen}) {
        scaled(button, 1.1);
        theme::definit_taille_icone(button, QSize(20, 20));
        buttons->addWidget(button);
    }
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addSpacing(10);
    QLabel *hint = centered(new QLabel(u"ou glissez un fichier .phy ou CSV sur la fenêtre"_s, page));
    hint->setObjectName(u"welcomeHint"_s);
    definit_couleur_texte(hint, theme::couleur_attenuee());
    layout->addStretch(1);
    dimensionne_boutons_accueil();
    return page;
}

void MainWindow::dimensionne_boutons_accueil()
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

void MainWindow::construit_zoom()
{
    // Ctrl + (ou Ctrl =) / Ctrl − / Ctrl 0 : agrandissement de l'interface par pas de 10 %, de 50 à
    // 300 %, borné par la largeur de la fenêtre pour qu'elle reste utilisable
    const auto zoomShortcut = [this](const QKeySequence &keys, auto step) {
        auto *action = new QAction(this);
        action->setShortcut(keys);
        connect(action, &QAction::triggered, this, [this, step] { change_agrandissement(step(m_uiZoom)); });
        addAction(action);
    };
    // agrandir : pas sous l'agrandissement demandé (celui affiché, borné par une fenêtre étroite,
    // peut être plus petit ; le retenu ne doit pas baisser)
    const auto zoomIn = [this](double z) { return std::max(m_store.prefs.uiZoom, std::round(z * 10.0 + 1.0) / 10.0); };
    zoomShortcut(QKeySequence::ZoomIn, zoomIn);
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), zoomIn);
    zoomShortcut(QKeySequence::ZoomOut, [](double z) { return std::round(z * 10.0 - 1.0) / 10.0; });
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), [](double) { return 1.0; });
    // agrandissement des préférences, borné par la taille de départ ; il est rétabli quand la
    // fenêtre prend sa taille (`resizeEvent`)
    applique_agrandissement(std::min(m_store.prefs.uiZoom, agrandissement_max()));
}

double MainWindow::agrandissement_max() const
{
    // Il reste toujours au moins la largeur minimale de la fenêtre pour la barre d'outils, trois
    // colonnes de fenêtres et la page d'accueil ; par pas de 10 %, pour qu'un changement de taille
    // de la fenêtre ne change pas l'agrandissement à chaque pixel.
    const double fit = std::floor(width() * 10.0 / MIN_WINDOW.width() + 1e-9) / 10.0;
    return std::clamp(fit, 1.0, UI_ZOOM_MAX);
}

void MainWindow::change_agrandissement(double zoom)
{
    // L'agrandissement demandé est retenu ; celui qui s'affiche est borné par la largeur de la
    // fenêtre, et revient à la demande quand la fenêtre s'élargit (`resizeEvent`), comme MeVSono.
    const double z = std::clamp(zoom, UI_ZOOM_MIN, UI_ZOOM_MAX);
    if (z != m_store.prefs.uiZoom) {
        m_store.prefs.uiZoom = z;
        m_store.prefs.enregistre();
    }
    applique_agrandissement(std::min(z, agrandissement_max()));
}

void MainWindow::applique_agrandissement(double zoom)
{
    if (zoom == m_uiZoom && theme::zoom() == zoom)
        return;
    m_uiZoom = zoom;
    theme::definit_zoom(zoom);
    // police du système multipliée ; les mesures du style suivent (`theme`)
    QFont font = m_baseFont;
    if (m_baseFont.pointSizeF() > 0.0)
        font.setPointSizeF(m_baseFont.pointSizeF() * zoom);
    else
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(m_baseFont.pixelSize() * zoom))));
    QApplication::setFont(font);
    // tailles données en pixels (largeurs minimales des fenêtres, icônes, polices relatives…)
    theme::reechelonne();
    m_toolbar->setIconSize(theme::px(QSize(20, 20)));
    dimensionne_coin_des_menus();
    if (auto *logo = m_emptyHint ? m_emptyHint->findChild<QLabel *>(u"welcomeLogo"_s) : nullptr)
        logo->setPixmap(about::icone().pixmap(theme::px(QSize(96, 96)), devicePixelRatioF()));
    if (m_welcomeTable)
        dimensionne_boutons_accueil();
    if (m_statusLabel) {
        m_statusIcon->setContentsMargins(theme::px(6), 0, 0, 0);
        m_statusLabel->setContentsMargins(0, theme::px(4), 0, theme::px(4));
        for (QLabel *info : {m_statusFile, m_statusErrors})
            info->setContentsMargins(theme::px(11), 0, theme::px(10), 0);
        // les étiquettes décrites par la feuille de style de la barre d'état ne prennent la nouvelle
        // police qu'avec elle (`style_barre_etat` réaffiche aussi le message, à sa hauteur)
        style_barre_etat();
        maj_statut();
    }
    // tableurs, graphes et panneaux se redessinent à la nouvelle taille
    Q_EMIT m_store.affichage_change();
    // la barre de menus ne mesure ses titres avec la nouvelle police qu'une fois l'événement
    // traité : le coin, l'accueil et la barre d'état sont mesurés de nouveau ensuite
    QTimer::singleShot(0, this, [this] {
        dimensionne_coin_des_menus();
        if (m_welcomeTable)
            dimensionne_boutons_accueil();
        if (m_statusLabel)
            ajuste_barre_etat();
    });
}

void MainWindow::ouvre_notice(const QString &page, const QString &section)
{
    QString error;
    const QString path = help::ecrit(page.isEmpty() ? help::NOTICE : page, &error);
    if (path.isEmpty()) {
        affiche_statut(u"Impossible d'écrire la notice : %1"_s.arg(error), icons::Status::Error);
        return;
    }
    QUrl url = QUrl::fromLocalFile(path);
    url.setFragment(section);
    if (!help::openUrl(url)) {
        affiche_statut(u"Ouvrez la notice dans votre navigateur : %1"_s.arg(QDir::toNativeSeparators(path)), icons::Status::Warning);
        return;
    }
    affiche_statut(u"Notice ouverte dans le navigateur : %1"_s.arg(QDir::toNativeSeparators(path)), icons::Status::Success);
}

// --- état affiché ------------------------------------------------------------------------------

void MainWindow::rafraichit()
{
    // fenêtres d'objets disparus (annulation, suppression)
    std::vector<Dock *> gone;
    for (const auto &[k, d] : m_docks) {
        if (!d)
            continue;
        const TabKey &key = d->key;
        const bool exists = (key.kind == TabKey::Table && m_store.ws.tableur(key.id)) || (key.kind == TabKey::Graph && m_store.ws.graphe(key.id))
                            || (key.kind == TabKey::Script && m_store.ws.script(key.id))
                            || (key.kind != TabKey::Table && key.kind != TabKey::Graph && key.kind != TabKey::Script);
        if (!exists)
            gone.push_back(d.data());
    }
    for (Dock *d : gone)
        retire_fenetre(d);
    maj_titres_fenetres();
    maj_titre();
    maj_statut();
    m_undo->setEnabled(m_store.peut_annuler());
    m_redo->setEnabled(m_store.peut_retablir());
    if (m_modeSimple) {
        m_guide->rafraichit();
        maj_indication_vide();
    }
}

void MainWindow::maj_titres_fenetres()
{
    for (const auto &[k, d] : m_docks) {
        if (!d)
            continue;
        const TabKey &key = d->key;
        QString title;
        switch (key.kind) {
        case TabKey::Quantities: title = u"Grandeurs"_s; break;
        case TabKey::Calculations: title = u"Calculs"_s; break;
        case TabKey::Table: title = m_store.ws.tableur(key.id) ? m_store.ws.tableur(key.id)->name : u"Tableur"_s; break;
        case TabKey::Graph: title = m_store.ws.graphe(key.id) ? m_store.ws.graphe(key.id)->name : u"Graphe"_s; break;
        case TabKey::Script:
            title = m_store.ws.script(key.id) ? m_store.ws.script(key.id)->name : u"Script"_s;
            if (m_store.est_en_cours(key.id))
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

void MainWindow::maj_titre()
{
    // les scripts rapportent leurs chemins relatifs au dossier du document
    m_store.documentFolder = m_path.isEmpty() ? QString() : QFileInfo(m_path).absolutePath();
    // « MeVReg — document » ; sous-titre tant que rien n'a été commencé
    const bool blank = m_path.isEmpty() && m_store.ws.est_vide() && !m_store.est_modifie();
    const QString name = m_path.isEmpty() ? u"Sans titre"_s : QFileInfo(m_path).fileName();
    setWindowTitle(u"%1 — %2"_s.arg(about::NAME, blank ? about::SUBTITLE : name + (m_store.est_modifie() ? u" •"_s : QString())));
}

void MainWindow::maj_statut()
{
    // à droite : document · titre du TP · nombre de grandeurs, puis les formules en erreur
    const QString name = m_path.isEmpty() ? u"Sans titre"_s : QFileInfo(m_path).fileName();
    QStringList parts{name + (m_store.est_modifie() ? u" (modifié)"_s : QString())};
    if (!m_store.ws.meta.title.trimmed().isEmpty())
        parts << m_store.ws.meta.title.trimmed();
    const size_t nq = m_store.ws.doc.quantities.size();
    parts << u"%1 grandeur%2"_s.arg(nq).arg(pluriel(nq));
    const QString full = parts.join(u" · "_s);
    m_statusFile->setText(m_statusFile->fontMetrics().elidedText(full, Qt::ElideMiddle, std::max(width() / 3, theme::px(160))));
    m_statusFile->setToolTip(m_path.isEmpty() ? full : QDir::toNativeSeparators(m_path));
    QStringList errors;
    for (const auto &q : m_store.ws.doc.quantities)
        if (!q.error.isEmpty())
            errors << u"%1 : %2"_s.arg(q.name, q.error);
    m_statusErrors->setVisible(!errors.isEmpty());
    if (!errors.isEmpty()) {
        m_statusErrors->setText(u"%1 formule%2 en erreur"_s.arg(errors.size()).arg(pluriel(errors.size())));
        m_statusErrors->setToolTip(errors.join(u'\n'));
    }
}

void MainWindow::choisit_mode(bool simple)
{
    (simple ? m_modeSimpleAction : m_modeExpertAction)->setChecked(true);
    if (simple == m_modeSimple)
        return;
    if (simple) {
        transmet_saisie_scripts();
        m_dispositionExpert = disposition_json();
        for (const auto &[nom, dock] : m_docks)
            if (dock) dock->hide();
        if (!m_guide) {
            m_guide = new Guide(*this);
            m_guide->hide();
        }
    }
    m_modeSimple = simple;
    if (m_store.prefs.modeSimple != simple) {
        m_store.prefs.modeSimple = simple;
        m_store.prefs.enregistre();
    }
    for (auto *menu : m_menusExperts)
        menu->setVisible(!simple);
    if (!simple) {
        if (m_docks.empty()) {
            restaure_disposition(m_dispositionExpert);
        } else {
            if (!restoreState(QByteArray::fromBase64(m_dispositionExpert.toObject().value(u"state"_s).toString().toLatin1())))
                disposition_par_defaut();
            for (const auto &[nom, dock] : m_docks)
                if (dock && dock->detache()) dock->show();
        }
    }
    m_toolbar->setVisible(!simple);
    rafraichit();
    maj_indication_vide();
}

void MainWindow::maj_indication_vide()
{
    if (!m_emptyHint)
        return;
    const bool guide = m_modeSimple && !m_store.ws.est_vide();
    QWidget *page = guide ? static_cast<QWidget *>(m_guide) : m_emptyHint;
    if (centralWidget() != page) {
        if (QWidget *precedente = takeCentralWidget()) {
            precedente->hide();
            precedente->setParent(this);
        }
        setCentralWidget(page);
    }
    if (m_modeSimple) {
        page->show();
        return;
    }
    const bool any = std::any_of(m_docks.begin(), m_docks.end(), [](const auto &p) { return p.second && !p.second->detache(); });
    // sans fenêtre (toutes fermées ou détachées) : l'invitation à démarrer occupe la place ; sinon
    // elle s'efface et les fenêtres ancrées prennent toute la largeur
    m_emptyHint->setVisible(!any);
}

// --- Python ------------------------------------------------------------------------------------

void MainWindow::lance_tous_scripts()
{
    for (const auto &s : m_store.ws.scripts)
        if (!m_store.blockedScripts.count(s.id))
            m_store.lance_script(s.id);
}

void MainWindow::apres_script_applique(ViewId, const std::vector<ViewId> &openGraphs, const std::vector<std::pair<ViewId, QString>> &figs,
                                 const QStringList &newDrawings, const QJsonArray &actions)
{
    for (ViewId g : openGraphs)
        ouvre_onglet(TabKey{TabKey::Graph, g});
    for (const auto &[g, path] : figs)
        enregistre_figure(g, path);
    for (const QString &name : newDrawings)
        ouvre_onglet(TabKey{TabKey::Drawing, 0, name});
    // ordres du module mevreg : fenêtres, fichiers, messages
    lance_actions_script(actions);
}

QString MainWindow::resout_chemin(const QString &path, const QString &suffix) const
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

std::optional<TabKey> MainWindow::fenetre_nommee(const QString &name) const
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
    if (m_store.drawings.count(name) || m_store.ws.script_du_dessin(name))
        return TabKey{TabKey::Drawing, 0, name};
    return std::nullopt;
}

void MainWindow::lance_actions_script(const QJsonArray &actions)
{
    for (const QJsonValue &v : actions) {
        const QJsonObject o = v.toObject();
        const QString order = o.value(u"ordre"_s).toString(), name = o.value(u"nom"_s).toString();
        auto window = [&]() -> std::optional<TabKey> {
            const auto k = fenetre_nommee(name);
            if (!k)
                m_store.message(u"mevreg : pas de fenêtre « %1 »"_s.arg(name), icons::Status::Warning);
            return k;
        };
        if (order == u"ouvrir" && name.compare(u"Notice"_s, Qt::CaseInsensitive) == 0) {
            ouvre_notice();
        } else if (order == u"ouvrir") {
            if (const auto k = window())
                ouvre_onglet(*k);
        } else if (order == u"cacher") {
            if (const auto k = window())
                ferme_onglet(*k);
        } else if (order == u"fermer") {
            if (const auto k = window()) {
                if (k->kind == TabKey::Table || k->kind == TabKey::Graph || k->kind == TabKey::Script || k->kind == TabKey::Drawing)
                    supprime_fenetre(*k);
                else
                    ferme_onglet(*k);
            }
        } else if (order == u"detacher" || order == u"rattacher") {
            if (const auto k = window()) {
                ouvre_onglet(*k);
                if (Dock *d = m_docks[k->texte()]; d)
                    order == u"detacher" ? detache_fenetre(d) : rattache_fenetre(d);
            }
        } else if (order == u"disposition") {
            disposition_par_defaut();
        } else if (order == u"enregistrer") {
            enregistre(false, o.value(u"chemin"_s).toString());
        } else if (order == u"exporter_csv") {
            std::vector<QId> ids;
            if (o.contains(u"grandeurs"_s)) {
                for (const QJsonValue &g : o.value(u"grandeurs"_s).toArray())
                    if (const Grandeur *q = m_store.ws.doc.par_nom(g.toString()))
                        ids.push_back(q->id);
            } else {
                for (const auto &q : m_store.ws.doc.quantities)
                    ids.push_back(q.id);
            }
            const QString sep = o.value(u"separateur"_s).toString(u";"_s);
            const csv::ExportOptions opts{sep.isEmpty() ? QChar(u';') : sep.at(0), o.value(u"virgule"_s).toBool(true)};
            ecrit_csv(resout_chemin(o.value(u"chemin"_s).toString(), u"csv"_s), ids, opts);
        } else if (order == u"exporter_graphe") {
            if (const auto k = fenetre_nommee(name); k && k->kind == TabKey::Graph)
                enregistre_figure(k->id, o.value(u"chemin"_s).toString());
            else
                m_store.message(u"mevreg : pas de graphe « %1 »"_s.arg(name), icons::Status::Warning);
        } else if (order == u"message") {
            m_store.message(o.value(u"texte"_s).toString());
        }
    }
}

void MainWindow::enregistre_figure(ViewId graph, const QString &path)
{
    const QString p = resout_chemin(path, QString());
    const GraphView *g = m_store.ws.graphe(graph);
    if (!g)
        return;
    QString error;
    if (exporter::ecrit_graphe(p, exporter::format_de(p), *g, m_store.ws, taille_export_graphe(graph), m_store.style(), &error))
        m_store.message(u"Figure enregistrée : %1"_s.arg(QDir::toNativeSeparators(p)));
    else
        m_store.message(u"savefig impossible : %1"_s.arg(error), icons::Status::Error);
}

// --- événements --------------------------------------------------------------------------------

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // taille de la fenêtre non agrandie, retrouvée au prochain lancement
    if (!(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) && width() >= 400 && height() >= 300) {
        m_previousWindowSize = m_windowSize;
        m_windowSize = size();
        m_store.prefs.windowSize = m_windowSize;
    }
    maj_statut();
    // agrandissement des préférences, borné par la nouvelle largeur de la fenêtre
    if (const double zoom = std::min(m_store.prefs.uiZoom, agrandissement_max()); zoom != m_uiZoom)
        applique_agrandissement(zoom);
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
            maj_bouton_theme();
            style_barre_etat();
            if (auto *hint = m_emptyHint->findChild<QLabel *>(u"welcomeTagline"_s))
                definit_couleur_texte(hint, theme::couleur_attenuee());
            if (auto *hint = m_emptyHint->findChild<QLabel *>(u"welcomeHint"_s))
                definit_couleur_texte(hint, theme::couleur_attenuee());
            Q_EMIT m_store.affichage_change();
        });
    }
}
