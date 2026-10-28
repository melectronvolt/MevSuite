// Tests de l'interface : fenêtre principale (page d'accueil, barre d'outils, barre d'état, thème,
// écran de démarrage), exemples, enregistrement et réouverture, réglages des graphes, modélisation,
// scripts, déplacement des fenêtres. Avec MEVREG_SHOTS=dossier, des captures d'écran de contrôle y
// sont enregistrées.

#include "app/about.h"
#include "app/crash.h"
#include "app/csvimport.h"
#include "app/dialogs.h"
#include "app/docking.h"
#include "app/examples.h"
#include "app/graphsettings.h"
#include "app/graphwidget.h"
#include "app/help.h"
#include "app/mainwindow.h"
#include "app/modelingpanel.h"
#include "app/pyexamples.h"
#include "app/scriptpanel.h"
#include "app/statspanel.h"
#include "app/tableview.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/resources.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QHeaderView>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QtTest>

#include <cstdint>
#include <stdexcept>
#include <thread>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

using namespace Qt::StringLiterals;

namespace {

/// Plantage pour l'essai du journal : accès mémoire invalide, sans fichier core (Linux).
[[noreturn]] void crashNow()
{
#ifdef Q_OS_LINUX
    prctl(PR_SET_DUMPABLE, 0);
#endif
    volatile std::uintptr_t address = 0;
    *reinterpret_cast<volatile int *>(address) = 0;
    std::abort();
}

/// Exception que rien ne rattrape, dans un fil de calcul (essai du journal), sans fichier core.
[[noreturn]] void throwInThread()
{
#ifdef Q_OS_LINUX
    prctl(PR_SET_DUMPABLE, 0);
#endif
    std::thread([] { throw std::runtime_error("essai d'exception"); }).join();
    std::abort();
}

void shot(QWidget *w, const QString &name)
{
    const QString dir = qEnvironmentVariable("MEVREG_SHOTS");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QApplication::processEvents();
    w->grab().save(QDir(dir).filePath(name + u".png"_s));
}

void settle(int ms = 60)
{
    for (int k = 0; k < 4; ++k) {
        QApplication::processEvents();
        QTest::qWait(ms / 4);
    }
}

Dock *dockOf(MainWindow &win, const TabKey &k)
{
    for (Dock *d : win.findChildren<Dock *>())
        if (d->key == k)
            return d;
    return nullptr;
}

/// Accepte la première boîte de dialogue ouverte (bouton par défaut).
QDialog *openDialog(MainWindow &win)
{
    for (QDialog *d : win.findChildren<QDialog *>())
        if (d->isVisible())
            return d;
    return nullptr;
}

/// Contenu rogné dans `root` : un widget plus étroit que le minimum de sa disposition, ou une zone
/// défilante dont le contenu dépasse en largeur ; description du premier trouvé, vide sinon.
QString clipped(QWidget *root)
{
    QList<QWidget *> all = root->findChildren<QWidget *>();
    all.prepend(root);
    for (QWidget *w : all) {
        if (!w->isVisible() || w->width() <= 0)
            continue;
        const QString name = u"%1 « %2 » (%3, parent %4)"_s.arg(QString::fromLatin1(w->metaObject()->className()), w->objectName(),
                                                               root->parentWidget() ? root->parentWidget()->windowTitle() : QString(),
                                                               QString::fromLatin1(w->parentWidget() ? w->parentWidget()->metaObject()->className() : ""));
        if (w->layout() && w->layout()->minimumSize().width() > w->width() + 1) {
            QString widest;
            int most = 0;
            for (int k = 0; k < w->layout()->count(); ++k)
                if (QLayoutItem *item = w->layout()->itemAt(k); item->minimumSize().width() > most) {
                    most = item->minimumSize().width();
                    widest = item->widget() ? QString::fromLatin1(item->widget()->metaObject()->className()) : u"disposition"_s;
                }
            return u"%1 : %2 px pour un minimum de %3 px (%4 : %5 px)"_s.arg(name).arg(w->width()).arg(w->layout()->minimumSize().width()).arg(widest).arg(most);
        }
        if (auto *area = qobject_cast<QScrollArea *>(w); area && area->widget() && area->widget()->width() > area->viewport()->width() + 1)
            return u"%1 : contenu de %2 px dans %3 px"_s.arg(name).arg(area->widget()->width()).arg(area->viewport()->width());
    }
    return {};
}

/// Marge sous la zone de tracé du premier graphe (graduations et titre de l'axe des abscisses).
double bottomMargin(MainWindow &win, ViewId graph)
{
    GraphPanel *gp = win.graphPanel(graph);
    if (!gp || gp->canvas()->layouts().empty())
        return 0;
    const PlotLayout &l = gp->canvas()->layouts().front();
    return l.cell.bottom() - l.area.bottom();
}

/// Remet l'interface à 100 % en fin d'essai, même après un échec (la police de toute
/// l'application est changée).
struct ZoomReset {
    MainWindow &win;
    ~ZoomReset()
    {
        win.setUiZoom(1.0);
        QApplication::processEvents();
    }
};

TableGrid *firstGrid(MainWindow &win)
{
    for (Dock *d : win.findChildren<Dock *>())
        if (d->key.kind == TabKey::Table)
            if (auto *t = qobject_cast<TablePanel *>(d->widget()))
                return t->grid();
    return nullptr;
}

} // namespace

/// La plateforme « offscreen » ne gère ni les tailles conseillées des fenêtres, ni la capture de la
/// souris par une fenêtre (Qt garde celle d'un widget) : messages sans intérêt.
QtMessageHandler g_previous = nullptr;

void quiet(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (msg.contains(u"propagateSizeHints"_s) || msg.contains(u"does not support grabbing"_s) || msg.contains(u"does not support raise"_s))
        return;
    if (g_previous)
        g_previous(type, ctx, msg);
}

class TestApp : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        g_previous = qInstallMessageHandler(quiet);
        QStandardPaths::setTestModeEnabled(true);
        ensureCoreResources();
        theme::init();
        theme::choose(Qt::ColorScheme::Light);
        // jamais de navigateur ouvert pendant les essais
        help::openUrl = [](const QUrl &) { return true; };
    }

    void emptyWorkspace()
    {
        Store store;
        MainWindow win(store);
        // taille normale par défaut, jamais agrandie d'office
        QCOMPARE(win.size(), DEFAULT_WINDOW);
        win.show();
        settle();
        QVERIFY(!win.isMaximized());
        QVERIFY(win.openTabs().isEmpty());
        QVERIFY(win.centralWidget()->isVisible());
        QCOMPARE(win.windowTitle(), u"MeVReg — "_s + about::SUBTITLE);
        shot(&win, u"accueil"_s);
        // une première fenêtre efface l'invitation
        win.openTab(TabKey{TabKey::Quantities});
        settle();
        QCOMPARE(win.openTabs().size(), 1);
        QVERIFY(!win.centralWidget()->isVisible());
        // sans grandeur, les indications restent en haut de la fenêtre, sous ses boutons
        Dock *q = dockOf(win, TabKey{TabKey::Quantities});
        QVERIFY(q);
        for (QLabel *l : q->widget()->findChildren<QLabel *>())
            if (l->isVisible())
                QVERIFY2(l->geometry().bottom() < q->widget()->height() / 3, qPrintable(l->text()));
        shot(q, u"grandeurs-vide"_s);
        win.closeTab(TabKey{TabKey::Quantities});
        settle();
        QVERIFY(win.openTabs().isEmpty());
        QVERIFY(win.centralWidget()->isVisible());
    }

    void welcomePage()
    {
        // épurée : logo, nom, sous-titre, deux boutons de même taille, une ligne discrète ; elle
        // tient sans chevauchement dans la taille minimale de la fenêtre, en clair comme en sombre
        for (const bool dark : {false, true}) {
            theme::choose(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            Store store;
            MainWindow win(store);
            win.resize(MIN_WINDOW);
            win.show();
            settle();
            QCOMPARE(win.size(), MIN_WINDOW);
            QWidget *page = win.centralWidget();
            QCOMPARE(page->objectName(), u"welcome"_s);
            const QList<QPushButton *> buttons = page->findChildren<QPushButton *>();
            QCOMPARE(buttons.size(), 2);
            QCOMPARE(buttons[0]->size(), buttons[1]->size());
            QCOMPARE(buttons[0]->text(), u"Nouveau tableur"_s);
            QVERIFY(!buttons[0]->icon().isNull() && !buttons[1]->icon().isNull());
            // pas d'exemples livrés : ouvrir son propre fichier
            QCOMPARE(buttons[1]->text(), u"Ouvrir…"_s);
            QVERIFY(!buttons[1]->menu());
            const QList<QAction *> fileMenu = win.menuBar()->actions().front()->menu()->actions();
            QVERIFY(std::none_of(fileMenu.begin(), fileMenu.end(), [](QAction *a) { return a->text().contains(u"Exemple"_s); }));
            QCOMPARE(page->findChildren<QLabel *>().size(), 4);
            std::vector<QRect> rects;
            for (QWidget *w : page->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
                if (!w->isVisible())
                    continue;
                QVERIFY2(page->rect().contains(w->geometry()), qPrintable(w->objectName()));
                for (const QRect &r : rects)
                    QVERIFY2(!r.intersects(w->geometry()), qPrintable(w->objectName()));
                rects.push_back(w->geometry());
            }
            // le texte du nom tient en entier (pas de passage à la ligne coupé)
            for (QLabel *l : page->findChildren<QLabel *>())
                QVERIFY2(l->height() >= l->heightForWidth(l->width()), qPrintable(l->text()));
            shot(&win, dark ? u"accueil-minimale-sombre"_s : u"accueil-minimale"_s);
        }
        theme::choose(Qt::ColorScheme::Light);
    }

    void toolbarAndMenus()
    {
        Store store;
        MainWindow win(store);
        win.show();
        settle();
        // une seule barre d'outils, fixe, icônes dessinées et texte, sans menu contextuel
        const QList<QToolBar *> bars = win.findChildren<QToolBar *>();
        QCOMPARE(bars.size(), 1);
        QToolBar *bar = bars.front();
        QVERIFY(!bar->isMovable() && !bar->isFloatable());
        QCOMPARE(bar->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QCOMPARE(bar->contextMenuPolicy(), Qt::PreventContextMenu);
        // les fenêtres de travail seulement : ni séparateur, ni espace, ni notice, thème ou langue
        QStringList texts;
        for (QAction *a : bar->actions()) {
            QVERIFY2(!a->isSeparator(), "séparateur");
            auto *b = qobject_cast<QToolButton *>(bar->widgetForAction(a));
            QVERIFY2(b && b->isVisible(), qPrintable(a->text()));
            texts << b->text();
            QVERIFY2(!b->icon().isNull(), qPrintable(b->text()));
        }
        QCOMPARE(texts, (QStringList{u"Grandeurs"_s, u"Tableur"_s, u"Graphe"_s, u"Python"_s}));
        // en haut à droite, dans la barre de menus : le thème, puis la langue ; pas de bouton de
        // notice (menu Aide, F1)
        QMenuBar *menuBar = win.menuBar();
        QVERIFY(!menuBar->isNativeMenuBar());
        QWidget *corner = menuBar->cornerWidget(Qt::TopRightCorner);
        QVERIFY(corner);
        QCOMPARE(corner->findChildren<QToolButton *>().size(), 2);
        auto *themeButton = corner->findChild<QToolButton *>(u"themeButton"_s);
        auto *language = corner->findChild<QToolButton *>(u"languageButton"_s);
        QVERIFY(themeButton && language);
        QVERIFY(themeButton->isVisible() && language->isVisible());
        QVERIFY(language->x() > themeButton->geometry().right());
        for (QToolButton *b : {themeButton, language}) {
            QCOMPARE(b->focusPolicy(), Qt::NoFocus);
            QVERIFY(b->autoRaise());
        }
        QCOMPARE(themeButton->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(language->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QVERIFY(!themeButton->icon().isNull() && !language->icon().isNull());
        // la barre de menus garde la hauteur d'un titre de menu, à 100 % comme à 200 %
        const QList<QAction *> titles = menuBar->actions();
        const auto checkHeight = [&] {
            const int title = menuBar->actionGeometry(titles.front()).height();
            QVERIFY2(menuBar->height() <= title + 2, qPrintable(u"%1 > %2"_s.arg(menuBar->height()).arg(title)));
            for (QToolButton *b : {themeButton, language}) {
                QVERIFY(b->height() <= title);
                QVERIFY(b->iconSize().height() <= b->height());
            }
        };
        checkHeight();
        win.resize(2000, 900);
        const ZoomReset reset{win};
        settle();
        win.setUiZoom(2.0);
        settle();
        QCOMPARE(win.uiZoom(), 2.0);
        checkHeight();
        QVERIFY(themeButton->iconSize().height() > 20);
        win.setUiZoom(1.0);
        settle();
        // le coin tient dans la largeur minimale de la fenêtre
        win.resize(MIN_WINDOW);
        settle();
        QVERIFY2(corner->geometry().right() < menuBar->width(), qPrintable(QString::number(menuBar->width())));
        QVERIFY(corner->geometry().left() > menuBar->actionGeometry(titles.back()).right());
        QVERIFY(language->isVisible());
        // drapeau français désactivé, avec sa bulle
        QVERIFY(!language->isEnabled());
        QCOMPARE(language->toolTip(), u"MeVReg n'existe pour l'instant qu'en français."_s);
        // menus : pas de thème ni de langue, Aide en dernier
        const QList<QAction *> menus = win.menuBar()->actions();
        QCOMPARE(menus.back()->text(), u"&Aide"_s);
        for (QAction *m : menus) {
            QVERIFY(!m->text().contains(u"Thème"_s) && !m->text().contains(u"Langue"_s));
            for (QChar c : m->text())
                QVERIFY2(c.unicode() < 0x2000, qPrintable(m->text()));
        }
        QStringList helpMenu;
        for (QAction *a : menus.back()->menu()->actions())
            helpMenu << a->text();
        QCOMPARE(helpMenu, (QStringList{u"Notice d'utilisation…"_s, u"À propos de MeVReg…"_s, u"À propos de Qt…"_s}));
        QCOMPARE(menus.back()->menu()->actions().front()->shortcut(), QKeySequence(QKeySequence::HelpContents));
        // la notice s'ouvre dans le navigateur (menu Aide) : ses fichiers sont écrits sur le disque
        QUrl opened;
        help::openUrl = [&opened](const QUrl &url) {
            opened = url;
            return true;
        };
        menus.back()->menu()->actions().front()->trigger();
        settle();
        help::openUrl = [](const QUrl &) { return true; };
        QVERIFY(opened.isLocalFile());
        QCOMPARE(QFileInfo(opened.toLocalFile()).fileName(), help::NOTICE);
        for (const QString &name : help::files())
            QVERIFY2(QFileInfo(QFileInfo(opened.toLocalFile()).dir().filePath(name)).size() > 0, qPrintable(name));
    }

    /// Pages de la notice : liens internes et images présents, version remplacée, libellés des
    /// boutons et des menus cités tels qu'ils sont dans le logiciel.
    void noticePages()
    {
        QString error;
        const QString page = help::write(help::NOTICE, &error);
        QVERIFY2(!page.isEmpty(), qPrintable(error));
        const QDir dir = QFileInfo(page).dir();
        for (const QString &name : {help::NOTICE, help::PYTHON}) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString html = QString::fromUtf8(f.readAll());
            QVERIFY(!html.contains(u"{{version}}"_s));
            QVERIFY(html.contains(QString::fromLatin1(MEVREG_VERSION)));
            static const QRegularExpression ids(u"id=\"([^\"]+)\""_s), refs(u"(?:href|src)=\"([^\"#:]*)(?:#([^\"]*))?\""_s);
            QSet<QString> anchors;
            for (auto m = ids.globalMatch(html); m.hasNext();)
                anchors.insert(m.next().captured(1));
            for (auto m = refs.globalMatch(html); m.hasNext();) {
                const auto r = m.next();
                const QString file = r.captured(1), anchor = r.captured(2);
                if (!file.isEmpty())
                    QVERIFY2(QFileInfo::exists(dir.filePath(file)), qPrintable(name + u" : "_s + file));
                if (file.isEmpty() && !anchor.isEmpty())
                    QVERIFY2(anchors.contains(anchor), qPrintable(name + u" : #"_s + anchor));
            }
        }
        // les fonctions du module mevreg sont toutes décrites dans la référence
        QFile py(u":/python/mevreg.py"_s), ref(dir.filePath(help::PYTHON));
        QVERIFY(py.open(QIODevice::ReadOnly) && ref.open(QIODevice::ReadOnly));
        const QString code = QString::fromUtf8(py.readAll()), doc = QString::fromUtf8(ref.readAll());
        static const QRegularExpression defs(u"^def ([a-z][a-z_]*)\\("_s, QRegularExpression::MultilineOption);
        for (auto m = defs.globalMatch(code); m.hasNext();) {
            const QString fn = m.next().captured(1);
            QVERIFY2(doc.contains(u"mr."_s + fn + u"("_s), qPrintable(fn));
        }
    }

    void themeButton()
    {
        Store store;
        MainWindow win(store);
        win.show();
        win.loadExample(5);
        settle();
        auto *button = win.findChild<QToolButton *>(u"themeButton"_s);
        QVERIFY(button);
        QVERIFY(!theme::isDark());
        QCOMPARE(button->toolTip(), u"Thème clair. Cliquer pour passer au thème sombre."_s);
        // un clic : thème sombre, palette de la suite, réglage noté
        button->click();
        settle(120);
        QVERIFY(theme::isDark());
        QCOMPARE(store.prefs.theme, Qt::ColorScheme::Dark);
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(32, 33, 36));
        QCOMPARE(QApplication::palette().color(QPalette::Base), QColor(22, 23, 25));
        QCOMPARE(button->toolTip(), u"Thème sombre. Cliquer pour passer au thème clair."_s);
        // les fenêtres de réglages ont le fond plus sombre des panneaux ; leur palette locale garde
        // le bord des champs visible
        Dock *q = dockOf(win, TabKey{TabKey::Quantities});
        QVERIFY(q);
        const auto background = [q] { return q->grab().toImage().pixelColor(q->width() / 2, 2).rgb(); };
        QCOMPARE(background(), QColor(32, 33, 36).darker(125).rgb());
        QCOMPARE(q->palette().color(QPalette::Window), QColor(78, 80, 86));
        shot(&win, u"theme-sombre"_s);
        // un second clic : retour au thème clair
        button->click();
        settle(120);
        QVERIFY(!theme::isDark());
        QCOMPARE(store.prefs.theme, Qt::ColorScheme::Light);
        QCOMPARE(background(), QApplication::palette().color(QPalette::Window).darker(104).rgb());
        shot(&win, u"theme-clair"_s);
    }

    void statusBar()
    {
        Store store;
        MainWindow win(store);
        win.show();
        settle();
        QStatusBar *bar = win.statusBar();
        auto *message = bar->findChild<QLabel *>(u"statusMessage"_s);
        auto *document = bar->findChild<QLabel *>(u"documentStatus"_s);
        QVERIFY(message && document);
        const int line = message->fontMetrics().height();
        // une ligne de haut
        QVERIFY2(bar->height() < 2 * line + 8, qPrintable(QString::number(bar->height())));
        QCOMPARE(document->text(), u"Sans titre · 0 grandeur"_s);
        // message typé : couleur de sa nature, toujours une ligne
        win.loadExample(5);
        settle();
        store.message(u"Graphe exporté : essai.png"_s);
        settle();
        QCOMPARE(message->palette().color(QPalette::WindowText), theme::successColor());
        QVERIFY(bar->height() < 2 * line + 8);
        QVERIFY(document->text().contains(u"4 grandeurs"_s));
        shot(bar, u"barre-etat"_s);
        store.message(u"Export impossible : disque plein"_s, icons::Status::Error);
        settle();
        QCOMPARE(message->palette().color(QPalette::WindowText), theme::errorColor());
        shot(bar, u"barre-etat-erreur"_s);
        // un message trop long passe à la ligne, sans être coupé
        store.message(QStringList(30, u"mesure"_s).join(u' '), icons::Status::Warning);
        win.resize(MIN_WINDOW);
        settle(120);
        QVERIFY(bar->height() >= 2 * line);
        QVERIFY(message->height() >= message->heightForWidth(message->width()));
        // et la barre revient à une ligne
        store.message(u"Prêt"_s, icons::Status::Info);
        settle(120);
        QVERIFY(bar->height() < 2 * line + 8);
    }

    void splash()
    {
        Store store;
        MainWindow win(store);
        win.show();
        settle();
        auto *splash = new Splash(&win);
        settle();
        QVERIFY(splash->isVisible());
        QCOMPARE(splash->geometry(), win.rect());
        shot(&win, u"demarrage"_s);
        // un clic l'efface
        QPointer<Splash> alive(splash);
        QTest::mouseClick(splash, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!alive, 3000);
    }

    void examplesOpen()
    {
        for (int i = 0; i < examples::names().size(); ++i) {
            Store store;
            MainWindow win(store);
            win.resize(1440, 880);
            win.show();
            win.loadExample(i);
            settle(120);
            for (const auto &q : store.ws.doc.quantities)
                QVERIFY2(q.error.isEmpty(), qPrintable(u"exemple %1, %2 : %3"_s.arg(i).arg(q.name, q.error)));
            QVERIFY(!store.ws.tables.empty());
            QVERIFY(!store.ws.graphs.empty());
            QVERIFY(win.isOpen(TabKey{TabKey::Quantities}));
            QVERIFY(win.isOpen(TabKey{TabKey::Table, store.ws.tables.front().id}));
            QVERIFY(win.isOpen(TabKey{TabKey::Graph, store.ws.graphs.front().id}));
            QVERIFY(store.dirty());
            // les fenêtres ancrées occupent toute la largeur
            Dock *g = dockOf(win, TabKey{TabKey::Graph, store.ws.graphs.front().id});
            QVERIFY(g && g->isVisible());
            QVERIFY(g->width() > 300);
            // chaque modèle des exemples est ajusté
            for (const auto &gv : store.ws.graphs)
                for (const auto &p : gv.plots)
                    for (const auto &f : p.fits)
                        QVERIFY2(f.result.has_value(), qPrintable(u"exemple %1 : %2"_s.arg(i).arg(f.resultError)));
            if (i == 7) {
                // le script de l'exemple est relancé à l'ouverture et ouvre sa fenêtre de dessin
                QTRY_VERIFY_WITH_TIMEOUT(!store.drawings.empty(), 10000);
                settle(200);
                QVERIFY(win.isOpen(TabKey{TabKey::Drawing, 0, store.drawings.begin()->first}));
            }
            shot(&win, u"exemple-%1"_s.arg(i));
        }
    }

    void darkTheme()
    {
        theme::choose(Qt::ColorScheme::Dark);
        Store store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        win.loadExample(5);
        settle(120);
        QVERIFY(theme::isDark());
        shot(&win, u"exemple-5-sombre"_s);
        // panneaux en sombre : Calculs, Modélisation, Statistiques
        if (Dock *c = dockOf(win, TabKey{TabKey::Calculations}))
            c->raise();
        win.openModeling(store.ws.graphs.front().id, 0, 0);
        settle(120);
        shot(&win, u"panneaux-sombre"_s);
        theme::choose(Qt::ColorScheme::Light);
    }

    void saveAndReopen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"tp.phy"_s);
        QList<TabKey> tabs;
        Workspace saved;
        {
            Store store;
            MainWindow win(store);
            win.resize(1300, 800);
            win.show();
            win.loadExample(0);
            settle();
            win.openTab(TabKey{TabKey::Statistics});
            settle();
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(phy::save(store.ws, QJsonValue()).toUtf8());
            f.close();
            QVERIFY(win.openPath(path));
            settle();
            win.openTab(TabKey{TabKey::Statistics});
            settle();
            // enregistrement sans boîte de dialogue : le chemin est déjà connu
            QVERIFY(win.save(false));
            QVERIFY(!store.dirty());
            tabs = win.openTabs();
            saved = store.ws;
            // fichier de contrôle, pour l'ouvrir aussi avec MeVReg
            if (const QString dir = qEnvironmentVariable("MEVREG_SHOTS"); !dir.isEmpty()) {
                QDir().mkpath(dir);
                QFile::remove(QDir(dir).filePath(u"exemple-0.phy"_s));
                QFile::copy(path, QDir(dir).filePath(u"exemple-0.phy"_s));
            }
        }
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        QVERIFY(win.openPath(path));
        settle();
        QCOMPARE(store.ws.doc.quantities.size(), saved.doc.quantities.size());
        QCOMPARE(store.ws.graphs.size(), saved.graphs.size());
        QVERIFY(store.ws.graphs.front().plots == saved.graphs.front().plots);
        // la disposition des fenêtres est retrouvée
        QCOMPARE(win.openTabs().size(), tabs.size());
        for (const TabKey &k : tabs)
            QVERIFY2(win.isOpen(k), qPrintable(k.text()));
    }

    /// Un document reçu (pas enregistré sur ce poste) ne lance ses scripts qu'après accord ; une fois
    /// accepté, ou enregistré ici, il les lance sans question.
    void receivedScriptsAskFirst()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"recu.phy"_s);
        {
            Workspace ws;
            const ViewId sid = ws.addScript(u"import mevreg as mr\nmr.creer(\"z\", mr.valeurs(\"t\") * 2)\n"_s);
            ws.scriptMut(sid)->linked = true;
            ws.doc.addExperimental(u"t"_s, u"s"_s, {1, 2});
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(phy::save(ws, QJsonValue()).toUtf8());
        }
        Store store;
        store.prefs.trustedDocuments.clear();
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        auto clickButton = [](QMessageBox *box, const QString &name) {
            for (QAbstractButton *b : box->buttons())
                if (b->objectName() == name)
                    b->click();
        };
        // refus (bouton par défaut) : rien ne se lance, même quand les données du script lié changent
        QVERIFY(win.openPath(path));
        settle(40);
        auto *box = win.findChild<QMessageBox *>(u"confirmScripts"_s);
        QVERIFY(box && box->isVisible());
        QCOMPARE(box->defaultButton()->objectName(), u"norun"_s);
        shot(box, u"scripts-confirmation"_s);
        clickButton(box, u"norun"_s);
        settle(40);
        store.edit([](Workspace &ws) { ws.doc.setValues(ws.doc.byName(u"t"_s)->id, {3.0}); });
        settle(300);
        QVERIFY(!store.ws.doc.byName(u"z"_s));
        // accord : le script se lance, et ce document ne pose plus la question
        QVERIFY(win.openPath(path));
        settle(40);
        box = win.findChild<QMessageBox *>(u"confirmScripts"_s);
        QVERIFY(box && box->isVisible());
        clickButton(box, u"run"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store.ws.doc.byName(u"z"_s), 10000);
        settle(40);
        QVERIFY(win.openPath(path));
        settle(40);
        QVERIFY(!win.findChild<QMessageBox *>(u"confirmScripts"_s));
        QTRY_VERIFY_WITH_TIMEOUT(store.ws.doc.byName(u"z"_s), 10000);
    }

    /// Annuler et rétablir par le menu Édition (ils ne sont plus dans la barre d'outils) : grisés quand
    /// il n'y a rien à annuler ou à rétablir, raccourcis Ctrl+Z et Ctrl+Y.
    void undoRedoFromEditMenu()
    {
        Store store;
        MainWindow win(store);
        win.show();
        win.loadExample(5);
        settle();
        QMenu *edit = nullptr;
        for (QAction *m : win.menuBar()->actions())
            if (m->text() == u"É&dition"_s)
                edit = m->menu();
        QVERIFY(edit);
        QAction *undo = nullptr, *redo = nullptr;
        for (QAction *a : edit->actions()) {
            if (a->text() == u"Annuler"_s)
                undo = a;
            if (a->text() == u"Rétablir"_s)
                redo = a;
        }
        QVERIFY(undo && redo);
        QCOMPARE(undo->shortcut(), QKeySequence(QKeySequence::Undo));
        QVERIFY(redo->shortcuts().contains(QKeySequence(u"Ctrl+Y"_s)));
        // plus dans la barre d'outils
        for (QAction *a : win.findChild<QToolBar *>()->actions())
            QVERIFY(a != undo && a != redo);
        QVERIFY(!redo->isEnabled());
        const size_t n = store.ws.doc.quantities.size();
        store.edit([](Workspace &ws) { ws.doc.addExperimental(u"m"_s, u"kg"_s, {1, 2, 3}); });
        settle();
        QVERIFY(undo->isEnabled());
        undo->trigger();
        settle();
        QCOMPARE(store.ws.doc.quantities.size(), n);
        QVERIFY(redo->isEnabled());
        redo->trigger();
        settle();
        QCOMPARE(store.ws.doc.quantities.size(), n + 1);
        QVERIFY(!redo->isEnabled());
        // le raccourci Ctrl+Z, fenêtre active
        win.activateWindow();
        QTest::keyClick(&win, Qt::Key_Z, Qt::ControlModifier);
        settle();
        QCOMPARE(store.ws.doc.quantities.size(), n);
    }

    void undoRedo()
    {
        Store store;
        MainWindow win(store);
        win.show();
        win.loadExample(5);
        settle();
        const size_t n = store.ws.doc.quantities.size();
        store.edit([](Workspace &ws) { ws.doc.addExperimental(u"m"_s, u"kg"_s, {1, 2, 3}); });
        settle();
        QCOMPARE(store.ws.doc.quantities.size(), n + 1);
        store.undo();
        settle();
        QCOMPARE(store.ws.doc.quantities.size(), n);
        store.redo();
        settle();
        QCOMPARE(store.ws.doc.quantities.size(), n + 1);
        // supprimer un graphe ferme sa fenêtre ; l'annulation le fait revenir
        const ViewId gid = store.ws.graphs.front().id;
        QVERIFY(win.isOpen(TabKey{TabKey::Graph, gid}));
        store.edit([](Workspace &ws) { ws.graphs.clear(); });
        settle();
        QVERIFY(!win.isOpen(TabKey{TabKey::Graph, gid}));
        store.undo();
        settle();
        QVERIFY(store.ws.graph(gid));
    }

    void graphSettings()
    {
        Store store;
        MainWindow win(store);
        win.resize(1500, 900);
        win.show();
        win.loadExample(5);
        settle();
        const ViewId gid = store.ws.graphs.front().id;
        GraphPanel *gp = win.graphPanel(gid);
        QVERIFY(gp);
        // une droite de référence, pour voir la section Annotations remplie
        store.edit([&](Workspace &ws) {
            Annotation a;
            a.kind = Annotation::HLine;
            a.y = 5.0;
            ws.graphMut(gid)->plots[0].annotations.push_back(a);
        });
        const std::vector<std::pair<SettingsSection, QString>> sections = {
            {SettingsSection::Axes, u"axes"_s}, {SettingsSection::Title, u"titre"_s}, {SettingsSection::Models, u"modeles"_s},
            {SettingsSection::Annotations, u"annotations"_s}, {SettingsSection::Curves, u"courbes"_s}};
        for (const auto &[s, name] : sections) {
            gp->openSettings(s);
            settle(100);
            GraphSettings *gs = gp->findChild<GraphSettings *>();
            QVERIFY(gs && gs->isVisible());
            shot(gs, u"section-"_s + name);
        }
        shot(&win, u"reglages-courbes"_s);
        // changer le type de barres d'incertitude reconstruit la page sans rien perdre
        store.edit([&](Workspace &ws) { ws.graphMut(gid)->plots[0].curves[0].errors = ErrorBars::Rectangles; });
        settle(100);
        store.edit([&](Workspace &ws) { ws.graphMut(gid)->plots[0].curves[0].capSize = 0.0; });
        settle(100);
        QCOMPARE(store.ws.graph(gid)->plots[0].curves[0].errors, ErrorBars::Rectangles);
        shot(&win, u"reglages-rectangles"_s);
        gp->openSettings(SettingsSection::Axes, 1);
        settle(100);
        shot(&win, u"reglages-axes"_s);
    }

    void reticleAndZoom()
    {
        Store store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        win.loadExample(5);
        settle();
        const ViewId gid = store.ws.graphs.front().id;
        GraphCanvas *c = win.graphPanel(gid)->canvas();
        settle(100);
        QVERIFY(!c->layouts().empty());
        const PlotLayout l = c->layouts().front();
        // réticule accroché au 6e point : sa valeur et son incertitude
        const Quantity *iq = store.ws.doc.byName(u"I"_s), *uq = store.ws.doc.byName(u"U"_s);
        const QPointF p(l.x.toPx(iq->values[5]), l.y.toPx(uq->values[5]));
        QTest::mouseMove(c, (p + QPointF(3, -2)).toPoint());
        settle(60);
        shot(win.graphPanel(gid), u"reticule"_s);
        // zoom par un rectangle tracé à la souris
        c->setTool(Tool::Zoom);
        const QPoint a = l.area.topLeft().toPoint() + QPoint(40, 40), b = l.area.center().toPoint();
        QTest::mousePress(c, Qt::LeftButton, {}, a);
        QTest::mouseMove(c, (a + b) / 2);
        QTest::mouseMove(c, b);
        QTest::mouseRelease(c, Qt::LeftButton, {}, b);
        settle(60);
        const Plot &pl = store.ws.graph(gid)->plots[0];
        QVERIFY(!pl.x.autoScale);
        QVERIFY(pl.x.max < l.x.max);
        shot(win.graphPanel(gid), u"zoom"_s);
        // retour à l'échelle automatique
        store.undo();
        settle(60);
        QVERIFY(store.ws.graph(gid)->plots[0].x.autoScale);
    }

    void modeling()
    {
        Store store;
        MainWindow win(store);
        win.resize(1500, 900);
        win.show();
        win.loadExample(0);
        settle();
        const ViewId gid = store.ws.graphs.front().id;
        win.openModeling(gid, 0, 0);
        settle(100);
        QVERIFY(ModelingPanel::instance());
        QVERIFY(win.isOpen(TabKey{TabKey::Modeling}));
        shot(&win, u"modelisation"_s);
        // nouveau modèle : la grille de vignettes attend un choix
        win.openModeling(gid, 0, -1);
        settle(100);
        shot(&win, u"modelisation-nouveau"_s);
        // plage tracée sur le graphe : appliquée au modèle affiché
        win.openModeling(gid, 0, 0);
        settle(60);
        QVERIFY(ModelingPanel::instance()->applyRange(gid, 0, 0.1, 0.5));
        settle(60);
        const auto &f = store.ws.graph(gid)->plots[0].fits[0];
        QVERIFY(f.range.has_value());
        QVERIFY(f.result.has_value());
        QVERIFY(f.result->n < 16);
    }

    void statistics()
    {
        Store store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        win.loadExample(4);
        settle();
        win.openStatistics(store.ws.doc.byName(u"T"_s)->id);
        settle(100);
        QVERIFY(StatsPanel::instance());
        QCOMPARE(*StatsPanel::instance()->quantity(), store.ws.doc.byName(u"T"_s)->id);
        const auto h = histogramPlot(store.ws.doc, *StatsPanel::instance()->quantity(), std::nullopt, store.style());
        QVERIFY(h.has_value());
        shot(&win, u"statistiques"_s);
    }

    void scriptRun()
    {
        Store store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        win.loadExample(1);
        settle();
        win.newScript();
        settle();
        const ViewId sid = store.ws.scripts.back().id;
        Dock *d = dockOf(win, TabKey{TabKey::Script, sid});
        QVERIFY(d);
        auto *panel = qobject_cast<ScriptPanel *>(d->widget());
        QVERIFY(panel);
        panel->editor()->setPlainText(u"import numpy as np\nimport matplotlib.pyplot as plt\n"
                                      "P = u_C * i\ngrandeur(\"P\", P, \"W\")\nprint(\"Pmax =\", P.max())\n"
                                      "plt.plot(t, P, 'r-')\nplt.xlabel('t (s)')\nplt.ylabel('P (W)')\nplt.show()\nx = 1/0\n"_s);
        QSignalSpy spy(&store, &Store::scriptApplied);
        panel->flush();
        store.runScript(sid);
        QVERIFY(spy.wait(10000));
        settle(100);
        QVERIFY(store.ws.doc.byName(u"P"_s));
        const ScriptRun &r = store.runs.at(sid);
        QVERIFY(r.stdoutText.contains(u"Pmax"_s));
        QVERIFY(r.error.has_value());
        QCOMPARE(r.error->line.value_or(-1), 10);
        // la figure est devenue un graphe du logiciel, ouvert
        QVERIFY(std::any_of(store.ws.graphs.begin(), store.ws.graphs.end(), [&](const GraphView &g) { return g.origin && g.origin->script == sid; }));
        d->raise();
        settle();
        shot(&win, u"script"_s);
    }

    /// Images de la notice (tools/images_notice.py les tire des captures « notice-… ») : des
    /// documents ouverts comme ceux d'un élève, pas des exemples.
    void noticeImages()
    {
        QTemporaryDir dir;
        const auto document = [&](MainWindow &win, int example, const QString &file) {
            win.loadExample(example);
            settle();
            const QString path = dir.filePath(file);
            QVERIFY(win.save(true, path));
            QVERIFY(win.openPath(path));
            settle(120);
        };
        {
            Store store;
            MainWindow win(store);
            win.resize(1280, 800);
            win.show();
            settle();
            shot(&win, u"notice-accueil"_s);
            document(win, 1, u"decharge.phy"_s);
            shot(&win, u"notice-fenetre"_s);
        }
        {
            Store store;
            MainWindow win(store);
            win.resize(1280, 800);
            win.show();
            document(win, 0, u"chute.phy"_s);
            shot(&win, u"notice-tableur"_s);
            // la fenêtre Modélisation, à part
            const ViewId g = store.ws.graphs.front().id;
            win.openModeling(g, 0, 0);
            settle(150);
            Dock *m = dockOf(win, TabKey{TabKey::Modeling});
            QVERIFY(m && m->detached());
            shot(m, u"notice-modele"_s);
        }
        {
            Store store;
            MainWindow win(store);
            win.resize(1280, 800);
            win.show();
            document(win, 5, u"ohm.phy"_s);
            const ViewId g = store.ws.graphs.front().id;
            win.graphPanel(g)->openSettings(SettingsSection::Curves, -1);
            settle(150);
            shot(&win, u"notice-graphe"_s);
        }
        {
            Store store;
            MainWindow win(store);
            win.resize(1280, 800);
            win.show();
            document(win, 1, u"decharge.phy"_s);
            win.newScript(u"import numpy as np\nimport matplotlib.pyplot as plt\nimport mevreg as mr\n\n"
                          "# puissance reçue par le condensateur\nP = u_C * i\nmr.creer(\"P\", P, \"W\")\n"
                          "print(\"Pmax =\", round(1000 * abs(P).max(), 1), \"mW\")\n\n"
                          "plt.plot(t, P, \"r-\")\nplt.xlabel(\"t (s)\")\nplt.ylabel(\"P (W)\")\n"
                          "plt.title(\"Puissance\")\nplt.show()\n"_s,
                          u"Puissance"_s);
            settle();
            const ViewId sid = store.ws.scripts.back().id;
            QSignalSpy spy(&store, &Store::scriptApplied);
            store.runScript(sid);
            QVERIFY(spy.wait(10000));
            settle(150);
            QVERIFY(!store.runs.at(sid).error);
            shot(&win, u"notice-python"_s);
        }
    }

    /// Formules saisies : virgule décimale ou séparateur d'arguments.
    void formulaCommas()
    {
        QCOMPARE(dialogs::formulaText(u"0,05*x"_s), u"0.05*x"_s);
        QCOMPARE(dialogs::formulaText(u"sin(0,5)"_s), u"sin(0.5)"_s);
        QCOMPARE(dialogs::formulaText(u"min(2,3)"_s), u"min(2,3)"_s);
        QCOMPARE(dialogs::formulaText(u"pow(2,3)"_s), u"pow(2,3)"_s);
        QCOMPARE(dialogs::formulaText(u"atan2(1,2) + 1,5"_s), u"atan2(1,2) + 1.5"_s);
        QCOMPARE(dialogs::formulaText(u"max (sin(0,5),1)"_s), u"max (sin(0.5),1)"_s);
        QCOMPARE(dialogs::formulaText(u"integ(y, t)"_s), u"integ(y, t)"_s);
    }

    /// Scripts : ordres de mevreg dans l'ordre du script, scripts liés qui lisent par mr.valeurs,
    /// « Arrêter » sur un script en attente, scripts de l'ancien document oubliés.
    void pythonOrderAndJobs()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.newTable({});
        settle();
        store.edit([](Workspace &ws) { ws.doc.addExperimental(u"x"_s, QString(), {1.0}); });
        settle();
        auto run = [&](const QString &code) -> ViewId {
            win.newScript(code);
            settle(20);
            const ViewId sid = store.ws.scripts.back().id;
            QSignalSpy spy(&store, &Store::scriptApplied);
            store.runScript(sid);
            spy.wait(10000);
            settle(40);
            return sid;
        };
        // supprimer puis recréer : la grandeur existe, avec ses nouvelles valeurs
        run(u"import mevreg as mr\nmr.supprimer(\"x\")\nmr.creer(\"x\", [2, 3])\n"_s);
        QVERIFY(store.ws.doc.byName(u"x"_s));
        QCOMPARE(store.ws.doc.byName(u"x"_s)->values, (std::vector<double>{2.0, 3.0}));
        // créer, renommer, recréer sous le même nom : chaque grandeur garde les valeurs de sa création
        run(u"import mevreg as mr\nmr.creer(\"r\", [1])\nmr.renommer(\"r\", \"a\")\nmr.creer(\"r\", [2])\n"_s);
        QCOMPARE(store.ws.doc.byName(u"a"_s)->values, (std::vector<double>{1.0}));
        QCOMPARE(store.ws.doc.byName(u"r"_s)->values, (std::vector<double>{2.0}));
        // mr.creer puis plt.plot : la grandeur garde son unité et va dans le tableur des résultats
        const ViewId plotted = run(u"import mevreg as mr\nimport numpy as np\nimport matplotlib.pyplot as plt\n"
                                   "P = np.array([1.0, 4.0])\nmr.creer(\"P\", P, \"W\")\nplt.plot(x, P)\nplt.show()\n"_s);
        QVERIFY(store.ws.doc.byName(u"P"_s));
        QCOMPARE(store.ws.doc.byName(u"P"_s)->unit, u"W"_s);
        const QString results = u"Résultats de "_s + store.ws.script(plotted)->name;
        const QId pid = store.ws.doc.byName(u"P"_s)->id;
        QVERIFY(std::any_of(store.ws.tables.begin(), store.ws.tables.end(), [&](const TableView &t) {
            return t.name == results && std::find(t.columns.begin(), t.columns.end(), pid) != t.columns.end();
        }));
        // grandeur modifiée par le script puis tracée : pas de copie « _py »
        run(u"import matplotlib.pyplot as plt\na = a * 1000\ngrandeur(\"a\", a, \"mm\")\nplt.plot(a, a)\nplt.show()\n"_s);
        QCOMPARE(store.ws.doc.byName(u"a"_s)->values, (std::vector<double>{1000.0}));
        QVERIFY(!store.ws.doc.byName(u"a_py"_s));
        // script lié qui lit x par mr.valeurs : relancé quand x change
        const ViewId linked = run(u"import mevreg as mr\nmr.creer(\"y\", mr.valeurs(\"x\") * 10)\n"_s);
        QCOMPARE(store.ws.doc.byName(u"y"_s)->values, (std::vector<double>{20.0, 30.0}));
        store.edit([&](Workspace &ws) { ws.scriptMut(linked)->linked = true; });
        settle(100);
        QSignalSpy relaunched(&store, &Store::scriptApplied);
        store.edit([&](Workspace &ws) { ws.doc.setValues(ws.doc.byName(u"x"_s)->id, {5.0}); });
        QVERIFY(relaunched.wait(10000));
        settle(40);
        QCOMPARE(store.ws.doc.byName(u"y"_s)->values, (std::vector<double>{50.0}));
        store.edit([&](Workspace &ws) { ws.scriptMut(linked)->linked = false; });
        settle(40);
        // « Arrêter » sur un script qui attend son tour : il ne tourne pas
        win.newScript(u"import time\ntime.sleep(1.5)\n"_s);
        const ViewId slow = store.ws.scripts.back().id;
        win.newScript(u"import mevreg as mr\nmr.creer(\"jamais\", [1])\n"_s);
        const ViewId queued = store.ws.scripts.back().id;
        settle(20);
        store.runScript(slow);
        store.runScript(queued);
        store.stopScript(queued);
        QTRY_VERIFY_WITH_TIMEOUT(!store.isRunning(slow) && !store.isRunning(queued), 10000);
        settle(100);
        QVERIFY(!store.ws.doc.byName(u"jamais"_s));
        QVERIFY(store.runs.at(queued).error && store.runs.at(queued).error->interrupted);
        // un script encore en cours quand un autre document s'ouvre : ses résultats sont oubliés
        win.newScript(u"import time\nimport mevreg as mr\ntime.sleep(1)\nmr.creer(\"ancienne\", [42])\n"_s);
        const ViewId old = store.ws.scripts.back().id;
        settle(20);
        store.runScript(old);
        settle(100);
        // le nouveau document a un script de même numéro : c'est à lui que l'ancien résultat
        // était appliqué
        Workspace fresh;
        while (fresh.scripts.empty() || fresh.scripts.back().id < old)
            fresh.addScript(u"print(1)"_s);
        QCOMPARE(fresh.scripts.back().id, old);
        store.reset(std::move(fresh));
        QTest::qWait(1800);
        settle(100);
        QVERIFY(!store.ws.doc.byName(u"ancienne"_s));
    }

    /// Module mevreg : grandeurs aux noms protégés (np…) lisibles, une seule borne d'axe, incertitudes
    /// côté par côté, annotations verrouillées puis effacées, méthode des tangentes.
    void pythonMevregCompleteness()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.newTable({});
        settle();
        store.edit([](Workspace &ws) {
            QVERIFY(ws.doc.addExperimental(u"np"_s, QString(), {1.0, 2.0}));
            ws.doc.addExperimental(u"t"_s, u"s"_s, {0.0, 1.0, 2.0, 3.0});
            ws.doc.addExperimental(u"x"_s, u"m"_s, {1.0, 2.0, 3.0, 4.0});
            ws.doc.addExperimental(u"bas"_s, u"m"_s, {0.1, 0.1, 0.1, 0.1});
            ws.doc.addExperimental(u"haut"_s, u"m"_s, {0.3, 0.3, 0.3, 0.3});
        });
        settle();
        win.newScript(uR"py(import mevreg as mr
mr.creer("double", mr.valeurs("np") * 2)
print(min(3, 4), "np" in mr.grandeurs())
mr.graphe("G", x="t", y="x")
mr.courbe("G", "t", "x", y_bas="bas", y_haut="haut")
mr.axes("G", ymin=0)
mr.verticale("G", 1.5, texte="E", verrou=True)
mr.texte("G", 0, 4, "note")
)py"_s);
        settle(20);
        const ViewId sid = store.ws.scripts.back().id;
        QSignalSpy spy(&store, &Store::scriptApplied);
        store.runScript(sid);
        QVERIFY(spy.wait(10000));
        settle(60);
        QVERIFY2(!store.runs.at(sid).error, store.runs.at(sid).error ? qPrintable(store.runs.at(sid).error->traceback) : "");
        QCOMPARE(store.runs.at(sid).stdoutText.trimmed(), u"3 True"_s);
        QCOMPARE(store.ws.doc.byName(u"double"_s)->values, (std::vector<double>{2.0, 4.0}));
        const GraphView *g = nullptr;
        for (const auto &gv : store.ws.graphs)
            if (gv.name == u"G")
                g = &gv;
        QVERIFY(g);
        const Plot &p = g->plots[0];
        // une seule borne : l'autre est prise sur les valeurs, avec une marge
        QVERIFY(!p.y.autoScale);
        QCOMPARE(p.y.min, 0.0);
        QVERIFY2(p.y.max > 4.0 && p.y.max < 4.5, qPrintable(QString::number(p.y.max)));
        QCOMPARE(p.curves.size(), size_t(1));
        QCOMPARE(p.curves[0].errYLow, store.ws.doc.byName(u"bas"_s)->id);
        QCOMPARE(p.curves[0].errYHigh, store.ws.doc.byName(u"haut"_s)->id);
        QVERIFY(p.curves[0].errors != ErrorBars::None);
        QCOMPARE(p.annotations.size(), size_t(2));
        QVERIFY(p.annotations[0].locked && !p.annotations[1].locked);
        // tangentes, puis tout effacer
        win.newScript(uR"py(import numpy as np
import mevreg as mr
mr.creer("V", np.linspace(0, 20, 41), "mL")
mr.creer("pH", 7 + 4 * np.tanh(1.5 * (mr.valeurs("V") - 12)))
mr.graphe("D", x="V", y="pH")
mr.tangentes("D", "V", "pH")
mr.effacer_annotations("G")
)py"_s);
        settle(20);
        const ViewId sid2 = store.ws.scripts.back().id;
        QSignalSpy spy2(&store, &Store::scriptApplied);
        store.runScript(sid2);
        QVERIFY(spy2.wait(10000));
        settle(60);
        QVERIFY(!store.runs.at(sid2).error);
        for (const auto &gv : store.ws.graphs) {
            if (gv.name == u"D")
                QCOMPARE(gv.plots[0].tangents.size(), size_t(1));
            if (gv.name == u"G")
                QVERIFY(gv.plots[0].annotations.empty());
        }
    }

    void pythonExamplesRun()
    {
        // chaque exemple du menu Python → Exemples s'exécute sans erreur, et le module mevreg fait
        // ce qu'il annonce
        QTemporaryDir dir;
        const QString previous = QDir::currentPath();
        QDir::setCurrent(dir.path());
        Store store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        settle();
        QSignalSpy messages(&store, &Store::messageRequested);
        for (const auto &ex : pyexamples::all()) {
            // « Enregistrer » sans nom de fichier ouvrirait la boîte d'enregistrement
            if (ex.code.contains(u"mr.enregistrer()"_s))
                continue;
            store.setDirty(false);
            win.newFile();
            settle(20);
            win.newScript(ex.code, ex.title);
            settle(20);
            const ViewId sid = store.ws.scripts.back().id;
            QSignalSpy spy(&store, &Store::scriptApplied);
            messages.clear();
            store.runScript(sid);
            QVERIFY2(spy.wait(10000), qPrintable(ex.title));
            settle(40);
            const ScriptRun &r = store.runs.at(sid);
            QVERIFY2(!r.error, qPrintable(ex.title + u" : "_s + (r.error ? r.error->traceback : QString())));
            for (const QString &m : r.messages)
                QVERIFY2(!m.contains(u"mevreg"_s) && !m.contains(u"inconnu"_s) && !m.contains(u"impossible"_s), qPrintable(ex.title + u" : "_s + m));
            for (const QList<QVariant> &m : messages)
                QVERIFY2(m.value(1).value<icons::Status>() != icons::Status::Error && m.value(1).value<icons::Status>() != icons::Status::Warning,
                         qPrintable(ex.title + u" : "_s + m.value(0).toString()));
            // quelques effets attendus
            if (ex.title == u"Modèle affine") {
                const GraphView *g = nullptr;
                for (const auto &gv : store.ws.graphs)
                    if (gv.name == u"Position")
                        g = &gv;
                QVERIFY(g && g->plots[0].fits.size() == 1 && g->plots[0].fits[0].result);
                QVERIFY(r.stdoutText.contains(u"pente"_s));
            } else if (ex.title == u"Grandeur calculée par une formule") {
                QVERIFY(store.ws.doc.byName(u"v"_s) && store.ws.doc.byName(u"v"_s)->isCalculated());
            } else if (ex.title == u"Renommer une grandeur") {
                QVERIFY(store.ws.doc.byName(u"position"_s) && !store.ws.doc.byName(u"x"_s));
            } else if (ex.title == u"Supprimer une grandeur") {
                QVERIFY(!store.ws.doc.byName(u"brouillon"_s));
            } else if (ex.title == u"Incertitude d'une grandeur") {
                QVERIFY(store.ws.doc.byName(u"x"_s)->uncertainty);
            } else if (ex.title == u"Droites et texte sur le graphe") {
                QCOMPARE(store.ws.graphs.back().plots[0].annotations.size(), size_t(4));
            } else if (ex.title == u"Détacher une fenêtre") {
                Dock *d = dockOf(win, TabKey{TabKey::Graph, store.ws.graphs.back().id});
                QVERIFY(d && d->detached());
                win.attachDock(d);
            } else if (ex.title == u"Exporter des grandeurs en CSV") {
                QVERIFY(QFile::exists(dir.filePath(u"mesures.csv"_s)));
            } else if (ex.title == u"Exporter l'image d'un graphe") {
                QVERIFY(QFile::exists(dir.filePath(u"position.png"_s)));
            } else if (ex.title == u"Cacher ou fermer une fenêtre") {
                QVERIFY(std::none_of(store.ws.graphs.begin(), store.ws.graphs.end(), [](const GraphView &g) { return g.name == u"Brouillon"; }));
            }
        }
        QDir::setCurrent(previous);
        store.setDirty(false);
    }

    void dockDragging()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        Dock *q = dockOf(win, TabKey{TabKey::Quantities});
        Dock *c = dockOf(win, TabKey{TabKey::Calculations});
        Dock *t = dockOf(win, TabKey{TabKey::Table, store.ws.tables.front().id});
        Dock *g = dockOf(win, TabKey{TabKey::Graph, store.ws.graphs.front().id});
        QVERIFY(q && c && t && g);
        const std::vector<QDockWidget *> all = {q, c, t, g};
        auto near = [](int a, int b, int tol = 3) { return std::abs(a - b) <= tol; };

        // la disposition relevée : trois colonnes, Grandeurs et Calculs en onglets
        docking::Node tree = docking::capture(&win, all);
        QCOMPARE(tree.orientation, Qt::Horizontal);
        QCOMPARE(tree.children.size(), size_t(3));
        QCOMPARE(tree.children[0].tabs.size(), size_t(2));
        // la refaire telle quelle ne change rien
        const QRect before = g->geometry();
        docking::apply(&win, tree);
        settle();
        QVERIFY2(near(g->geometry().left(), before.left()) && near(g->geometry().width(), before.width()),
                 qPrintable(u"%1 %2 → %3 %4"_s.arg(before.left()).arg(before.width()).arg(g->geometry().left()).arg(g->geometry().width())));

        // le graphe sous le tableur : la place du tableur partagée en deux
        const QRect tableBefore = t->geometry();
        QVERIFY(win.moveDock(g, {t, docking::Side::Bottom}));
        settle();
        QVERIFY(g->geometry().top() > t->geometry().bottom());
        QVERIFY(near(g->geometry().left(), t->geometry().left()));
        QVERIFY(near(g->geometry().width(), t->geometry().width()));
        QVERIFY(std::abs(g->geometry().height() - t->geometry().height()) <= 30);
        // la colonne du graphe disparue, le tableur prend la largeur libérée
        QVERIFY(t->geometry().width() > tableBefore.width() + 100);
        shot(&win, u"deplacement-dessous"_s);

        // Calculs sur toute la largeur, en bas
        QVERIFY(win.moveDock(c, {nullptr, docking::Side::Bottom}));
        settle();
        QVERIFY(win.tabifiedDockWidgets(q).isEmpty());
        QVERIFY(c->geometry().top() > g->geometry().bottom());
        QVERIFY(c->geometry().left() <= q->geometry().left() + 2);
        QVERIFY(c->geometry().right() >= g->geometry().right() - 2);

        // le graphe en onglet avec Grandeurs, au premier plan
        QVERIFY(win.moveDock(g, {q, docking::Side::Center}));
        settle();
        QVERIFY(win.tabifiedDockWidgets(q).contains(g));
        // Qt range hors de la fenêtre les onglets qui ne sont pas au premier plan
        QVERIFY(win.rect().contains(g->geometry().center()) && !win.rect().contains(q->geometry().center()));
        // une fenêtre ne se pose pas sur elle-même
        QVERIFY(!win.moveDock(t, {t, docking::Side::Left}));

        // glissement par la barre de titre : repères, aperçu, lâcher sur le bord droit
        const QPoint title(t->width() / 2, t->widget()->geometry().top() / 2);
        QTest::mousePress(t, Qt::LeftButton, {}, title);
        QTest::mouseMove(t, title + QPoint(30, 4));
        auto *ov = win.findChild<docking::Overlay *>();
        QVERIFY(ov && ov->isVisible());
        const QPoint onQ = g->geometry().center();
        const auto ms = ov->markers(onQ);
        QCOMPARE(ms.size(), size_t(9)); // cinq sur la pile survolée, quatre sur les bords
        QRect right;
        for (const auto &[target, r] : ms)
            if (!target.group && target.side == docking::Side::Right)
                right = r;
        QVERIFY(!right.isNull());
        QTest::mouseMove(ov, ms.front().second.center());
        QVERIFY(ov->targetAt(ms.front().second.center()) == (docking::Target{g, docking::Side::Center}));
        shot(&win, u"deplacement-reperes"_s);
        QTest::mouseMove(ov, ms[4].second.center());
        shot(&win, u"deplacement-reperes-dessous"_s);
        QTest::mouseMove(ov, right.center());
        shot(&win, u"deplacement-reperes-bord"_s);
        QTest::mouseRelease(ov, Qt::LeftButton, {}, right.center());
        settle();
        QVERIFY(!win.findChild<docking::Overlay *>());
        QVERIFY(t->geometry().left() > g->geometry().right());
        QVERIFY(t->geometry().bottom() >= c->geometry().bottom() - 2);
        shot(&win, u"deplacement-bord"_s);

        // Échap annule
        const QRect kept = t->geometry();
        QTest::mousePress(t, Qt::LeftButton, {}, title);
        QTest::mouseMove(t, title + QPoint(30, 4));
        ov = win.findChild<docking::Overlay *>();
        QVERIFY(ov);
        QTest::keyClick(ov, Qt::Key_Escape);
        QTest::mouseRelease(t, Qt::LeftButton, {}, title);
        settle();
        QVERIFY(!win.findChild<docking::Overlay *>());
        QCOMPARE(t->geometry(), kept);

        // un onglet tiré hors de sa barre déplace sa fenêtre
        QTabBar *bar = nullptr;
        for (QTabBar *b : win.findChildren<QTabBar *>(Qt::FindDirectChildrenOnly))
            if (b->isVisible() && b->count() == 2)
                bar = b;
        QVERIFY(bar);
        int qi = -1;
        for (int i = 0; i < bar->count(); ++i)
            if (reinterpret_cast<QWidget *>(bar->tabData(i).value<quintptr>()) == q)
                qi = i;
        QVERIFY(qi >= 0);
        const QPoint tab = bar->tabRect(qi).center();
        QTest::mousePress(bar, Qt::LeftButton, {}, tab);
        QTest::mouseMove(bar, tab + QPoint(0, 10));
        QVERIFY(!win.findChild<docking::Overlay *>());
        QTest::mouseMove(bar, tab + QPoint(0, 90));
        ov = win.findChild<docking::Overlay *>();
        QVERIFY(ov);
        const QPoint onT = t->geometry().center();
        QTest::mouseMove(ov, onT);
        const auto mt = ov->markers(onT);
        QVERIFY(mt.front().first == (docking::Target{t, docking::Side::Center}));
        QTest::mouseMove(ov, mt[1].second.center());
        QTest::mouseRelease(ov, Qt::LeftButton, {}, mt[1].second.center());
        settle();
        // Grandeurs à gauche du tableur, le graphe seul dans sa pile
        QVERIFY(win.tabifiedDockWidgets(g).isEmpty());
        QVERIFY(q->geometry().right() < t->geometry().left());
        QVERIFY(near(q->geometry().top(), t->geometry().top()));

        // une nouvelle colonne ne défait pas la disposition choisie
        const QRect gKept = g->geometry();
        win.openTab(TabKey{TabKey::Statistics});
        settle();
        Dock *st = dockOf(win, TabKey{TabKey::Statistics});
        QVERIFY(st && st->isVisible());
        QVERIFY(c->geometry().top() > g->geometry().bottom());
        QVERIFY(near(g->geometry().top(), gKept.top()));
        shot(&win, u"deplacement-nouvelle-colonne"_s);

        // détacher : une fenêtre à part, avec le cadre du système (Qt dessinait le sien, que
        // Wayland ne laisse pas déplacer), puis rattacher par son bouton
        QVERIFY(!g->detached() && g->titleBar()->detachButton()->isVisible());
        g->titleBar()->detachButton()->click();
        settle();
        QVERIFY(g->detached() && g->isWindow() && g->isVisible());
        QVERIFY(!(g->windowFlags() & Qt::FramelessWindowHint));
        QCOMPARE(win.dockWidgetArea(g), Qt::NoDockWidgetArea);
        QVERIFY(!g->titleBar()->closeButton()->isVisible());
        QCOMPARE(g->titleBar()->detachButton()->text(), u"Rattacher"_s);
        // les raccourcis de la fenêtre principale servent aussi dans la fenêtre détachée
        QVERIFY(std::any_of(g->actions().begin(), g->actions().end(), [](QAction *a) { return a->shortcut() == QKeySequence::Undo; }));
        shot(g, u"fenetre-detachee"_s);
        g->titleBar()->detachButton()->click();
        settle();
        QVERIFY(!g->detached() && !g->isWindow() && win.dockWidgetArea(g) != Qt::NoDockWidgetArea);
        QVERIFY(g->actions().isEmpty());
        QVERIFY(g->titleBar()->closeButton()->isVisible());

        // ou par le menu Fenêtres
        win.detachDock(g);
        settle();
        QVERIFY(g->detached());
        QMenu *windows = nullptr;
        for (QAction *a : win.menuBar()->actions())
            if (a->text() == u"Fenê&tres")
                windows = a->menu();
        QVERIFY(windows);
        Q_EMIT windows->aboutToShow();
        QAction *redock = nullptr;
        for (QAction *a : windows->actions())
            if (a->text() == u"Rattacher les fenêtres détachées")
                redock = a;
        QVERIFY(redock);
        redock->trigger();
        settle();
        QVERIFY(!g->detached() && win.dockWidgetArea(g) != Qt::NoDockWidgetArea);
        QVERIFY(win.rect().contains(g->geometry().center()) || !win.tabifiedDockWidgets(g).isEmpty());
    }

    void dockTitleBar()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        Dock *g = dockOf(win, TabKey{TabKey::Graph, store.ws.graphs.front().id});
        QVERIFY(g);
        // boutons Détacher et Fermer : icônes de la hauteur d'une ligne de texte, au moins
        const int line = g->titleBar()->fontMetrics().height();
        for (QToolButton *b : {g->titleBar()->detachButton(), g->titleBar()->closeButton()}) {
            QVERIFY(b->isVisible());
            QVERIFY2(b->iconSize().height() >= line && b->height() > line, qPrintable(u"%1 px pour une ligne de %2 px"_s.arg(b->height()).arg(line)));
        }
        QVERIFY(g->titleBar()->height() >= g->titleBar()->closeButton()->height());
        shot(g->titleBar(), u"barre-de-titre"_s);
        // ils suivent l'agrandissement de l'interface
        ZoomReset reset{win};
        win.setUiZoom(1.5);
        settle();
        QVERIFY(g->titleBar()->closeButton()->iconSize().height() >= g->titleBar()->fontMetrics().height());
        QVERIFY(g->titleBar()->closeButton()->iconSize().height() > line);
    }

    void closeConfirmation()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        const TabKey gk{TabKey::Graph, store.ws.graphs.front().id};
        Dock *g = dockOf(win, gk);
        QVERIFY(g);
        auto clickButton = [](QMessageBox *box, const QString &name) {
            for (QAbstractButton *b : box->buttons())
                if (b->objectName() == name || b->text() == name)
                    b->click();
        };
        // un graphe : cacher ou fermer ?
        g->titleBar()->closeButton()->click();
        settle(40);
        auto *box = win.findChild<QMessageBox *>(u"confirmClose"_s);
        QVERIFY(box && box->isVisible());
        QVERIFY(box->informativeText().contains(u"menu Fenêtres"_s));
        shot(box, u"fermeture-confirmation"_s);
        // Annuler : rien ne change
        clickButton(box, u"Annuler"_s);
        settle(40);
        QVERIFY(win.isOpen(gk));
        // Cacher : la fenêtre s'en va, le graphe reste et se retrouve dans le menu Fenêtres
        g->titleBar()->closeButton()->click();
        settle(40);
        clickButton(win.findChild<QMessageBox *>(u"confirmClose"_s), u"hide"_s);
        settle(40);
        QVERIFY(!win.isOpen(gk));
        QVERIFY(store.ws.graph(gk.id));
        QMenu *windows = nullptr;
        for (QAction *a : win.menuBar()->actions())
            if (a->text() == u"Fenê&tres")
                windows = a->menu();
        QVERIFY(windows);
        Q_EMIT windows->aboutToShow();
        QWidget *row = windows->findChild<QWidget *>(u"windowRow:"_s + gk.text());
        QVERIFY(row);
        QTest::mouseClick(row, Qt::LeftButton, {}, QPoint(10, row->height() / 2));
        settle();
        QVERIFY(win.isOpen(gk));
        // Fermer : le graphe est supprimé du document (Ctrl+Z le rétablit)
        dockOf(win, gk)->titleBar()->closeButton()->click();
        settle(40);
        clickButton(win.findChild<QMessageBox *>(u"confirmClose"_s), u"close"_s);
        settle(40);
        QVERIFY(!win.isOpen(gk));
        QVERIFY(!store.ws.graph(gk.id));
        store.undo();
        settle();
        QVERIFY(store.ws.graph(gk.id));
        // la croix du menu Fenêtres ferme aussi
        Q_EMIT windows->aboutToShow();
        row = windows->findChild<QWidget *>(u"windowRow:"_s + gk.text());
        QVERIFY(row);
        row->findChild<QToolButton *>(u"closeWindow"_s)->click();
        settle();
        QVERIFY(!store.ws.graph(gk.id));
        store.undo();
        settle();
        // Grandeurs : rien à perdre, pas de question
        const TabKey qk{TabKey::Quantities};
        dockOf(win, qk)->titleBar()->closeButton()->click();
        settle(40);
        QVERIFY(!win.findChild<QMessageBox *>(u"confirmClose"_s));
        QVERIFY(!win.isOpen(qk));
        // « Toujours cacher sans demander »
        win.openTab(gk);
        settle();
        dockOf(win, gk)->titleBar()->closeButton()->click();
        settle(40);
        box = win.findChild<QMessageBox *>(u"confirmClose"_s);
        QVERIFY(box && box->checkBox());
        box->checkBox()->setChecked(true);
        clickButton(box, u"hide"_s);
        settle(40);
        QVERIFY(!win.isOpen(gk) && !store.prefs.confirmClose && store.ws.graph(gk.id));
        win.openTab(gk);
        settle();
        dockOf(win, gk)->titleBar()->closeButton()->click();
        settle(40);
        QVERIFY(!win.findChild<QMessageBox *>(u"confirmClose"_s));
        QVERIFY(!win.isOpen(gk) && store.ws.graph(gk.id));
        store.prefs.confirmClose = true;
        store.prefs.save();
        // un script en cours de saisie est rangé dans le document avant la fermeture
        win.newScript();
        settle();
        const ViewId sid = store.ws.scripts.back().id;
        auto *sp = qobject_cast<ScriptPanel *>(dockOf(win, TabKey{TabKey::Script, sid})->widget());
        QVERIFY(sp);
        CodeEditor *editor = sp->editor();
        QVERIFY(editor);
        editor->setPlainText(u"x = 1  # pas encore rangé"_s);
        win.closeTab(TabKey{TabKey::Script, sid});
        settle();
        QCOMPARE(store.ws.script(sid)->code, u"x = 1  # pas encore rangé"_s);
    }

    /// Menu Fenêtres : un clic cache une fenêtre affichée (cochée), un autre la réaffiche ; rien
    /// n'est supprimé du document.
    void windowsMenuToggles()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        QMenu *windows = nullptr;
        for (QAction *a : win.menuBar()->actions())
            if (a->text() == u"Fenê&tres")
                windows = a->menu();
        QVERIFY(windows);
        // un graphe : ligne du menu
        const TabKey gk{TabKey::Graph, store.ws.graphs.front().id};
        QVERIFY(win.isOpen(gk));
        auto clickRow = [&] {
            Q_EMIT windows->aboutToShow();
            QWidget *row = windows->findChild<QWidget *>(u"windowRow:"_s + gk.text());
            QVERIFY(row);
            QTest::mouseClick(row, Qt::LeftButton, {}, QPoint(10, row->height() / 2));
            settle();
        };
        clickRow();
        QVERIFY(!win.isOpen(gk));
        QVERIFY(store.ws.graph(gk.id));
        clickRow();
        QVERIFY(win.isOpen(gk));
        // Grandeurs : entrée cochable du menu
        const TabKey qk{TabKey::Quantities};
        auto triggerQuantities = [&] {
            Q_EMIT windows->aboutToShow();
            QAction *quantities = nullptr;
            for (QAction *a : windows->actions())
                if (a->text() == u"Grandeurs")
                    quantities = a;
            QVERIFY(quantities && quantities->isCheckable());
            QCOMPARE(quantities->isChecked(), win.isOpen(qk));
            quantities->trigger();
            settle();
        };
        win.openTab(qk);
        settle();
        triggerQuantities();
        QVERIFY(!win.isOpen(qk));
        triggerQuantities();
        QVERIFY(win.isOpen(qk));
    }

    /// Réglages du graphe, incertitudes : cocher « une grandeur pour chaque côté » fait apparaître
    /// les listes (le panneau, refait pendant le clic, détruisait la case : plantage).
    void errorSidesCheckbox()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        const ViewId g = store.ws.graphs.front().id;
        store.edit([&](Workspace &ws) {
            for (auto &c : ws.graphMut(g)->plots.front().curves)
                c.errors = ErrorBars::Vertical;
        });
        GraphPanel *gp = win.graphPanel(g);
        QVERIFY(gp);
        gp->openSettings(SettingsSection::Curves);
        settle(120);
        auto *settings = gp->findChild<GraphSettings *>();
        QVERIFY(settings);
        QCheckBox *sides = nullptr;
        for (QCheckBox *c : settings->findChildren<QCheckBox *>(u"errorSides"_s))
            if (c->isVisible())
                sides = c;
        QVERIFY(sides);
        QVERIFY(!sides->isChecked());
        QTest::mouseClick(sides, Qt::LeftButton);
        settle();
        bool lists = false;
        for (QWidget *w : settings->findChildren<QWidget *>(u"errorYHigh"_s))
            lists = lists || w->isVisible();
        QVERIFY(lists);
    }

    /// Plantage (accès mémoire invalide) : le journal reçoit la cause et la pile d'appels. L'essai
    /// se relance lui-même, et c'est ce second programme qui plante.
    void crashLog()
    {
        if (const QString log = qEnvironmentVariable("MEVSUITE_ESSAI_PLANTAGE"); !log.isEmpty()) {
            crash::install(log, false);
            crashNow();
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(u"mevreg.log"_s);
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"MEVSUITE_ESSAI_PLANTAGE"_s, log);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {u"crashLog"_s});
        QVERIFY(child.waitForFinished(60000));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
        QVERIFY2(lines.size() >= 4, qPrintable(lines.join(u'\n')));
        QVERIFY(lines[0].startsWith(u"MeVReg "_s + about::VERSION));
#ifdef Q_OS_WIN
        QCOMPARE(lines[1], u"Exception 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)"_s);
#else
        QCOMPARE(lines[1], u"Signal 11 (SIGSEGV)"_s);
#endif
    }

    /// Exception que rien ne rattrape, dans un fil de calcul : le journal reçoit son message (sous
    /// Windows, le gestionnaire de plantage la laisse repartir vers std::terminate).
    void uncaughtExceptionLog()
    {
        if (const QString log = qEnvironmentVariable("MEVSUITE_ESSAI_EXCEPTION"); !log.isEmpty()) {
            crash::install(log, false);
            throwInThread();
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(u"mevreg.log"_s);
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"MEVSUITE_ESSAI_EXCEPTION"_s, log);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {u"uncaughtExceptionLog"_s});
        QVERIFY(child.waitForFinished(60000));
        QVERIFY(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0);
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
        QVERIFY2(lines.size() >= 2, qPrintable(lines.join(u'\n')));
        QVERIFY(lines[0].startsWith(u"MeVReg "_s + about::VERSION));
        // journal écrit en mode texte : \r\n sous Windows
        QCOMPARE(lines[1].trimmed(), u"essai d'exception"_s);
    }

    /// Coller un tableau dont une case n'est pas un nombre : la case est laissée vide, et la barre
    /// d'état le dit (avec la valeur en cause).
    void pasteReportsUnreadableCells()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.newTable({});
        settle();
        TableGrid *grid = firstGrid(win);
        QVERIFY(grid);
        QSignalSpy messages(&store, &Store::messageRequested);
        grid->setCurrentIndex(grid->model()->index(0, 0));
        grid->paste(u"1\t2\n3\toops"_s);
        settle();
        QVERIFY(!messages.isEmpty());
        const QString last = messages.last().at(0).toString();
        QVERIFY2(last.contains(u"1 case illisible laissée vide (« oops »)"_s), qPrintable(last));
        QCOMPARE(messages.last().at(1).value<icons::Status>(), icons::Status::Warning);
    }

    void tableColumns()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.newTable({});
        settle();
        TableGrid *grid = firstGrid(win);
        QVERIFY(grid);
        auto *header = qobject_cast<TableHeader *>(grid->horizontalHeader());
        QVERIFY(header);
        const ViewId table = store.ws.tables.front().id;
        auto addColumn = [&](const QString &name) {
            store.edit([&](Workspace &ws) {
                const QId q = *ws.doc.addExperimental(name, QString(), {0.1, 0.2});
                ws.tableMut(table)->columns.push_back(q);
            });
            settle();
        };
        // une colonne créée par « + » ne garde pas la largeur étroite de « + » : elle prend la largeur
        addColumn(u"x"_s);
        const int width = grid->viewport()->width();
        QVERIFY2(grid->columnWidth(0) + grid->columnWidth(1) >= width - 1, qPrintable(u"%1 px sur %2"_s.arg(grid->columnWidth(0)).arg(width)));
        // deux colonnes : la largeur partagée, sans barre de défilement ; la colonne « + » reste étroite
        addColumn(u"y"_s);
        QVERIFY(std::abs(grid->columnWidth(0) - grid->columnWidth(1)) <= 1);
        QVERIFY(grid->columnWidth(2) <= theme::px(48));
        QCOMPARE(grid->horizontalScrollBar()->maximum(), 0);
        // le bord entre les deux colonnes se tire à la souris (même à quelques points du trait) : la
        // première garde sa nouvelle largeur, la seconde prend le reste ; rien ne part en glisser
        {
            const int before = grid->columnWidth(0), total = grid->columnWidth(0) + grid->columnWidth(1);
            const int border = header->sectionViewportPosition(0) + before;
            const QPoint from(border - 5, header->height() / 2), to(border - 205, header->height() / 2);
            QCOMPARE(header->handleAt(from), 0);
            QTest::mousePress(header->viewport(), Qt::LeftButton, {}, from);
            QVERIFY(header->resizing());
            for (int k = 1; k <= 10; ++k) {
                QMouseEvent move(QEvent::MouseMove, QPointF(from + (to - from) * k / 10), header->viewport()->mapToGlobal(QPointF(from + (to - from) * k / 10)),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(header->viewport(), &move);
            }
            QTest::mouseRelease(header->viewport(), Qt::LeftButton, {}, to);
            settle(300);
            QVERIFY(!header->resizing());
            QVERIFY2(std::abs(grid->columnWidth(0) - (before - 200)) <= 2, qPrintable(u"%1 px au lieu de %2"_s.arg(grid->columnWidth(0)).arg(before - 200)));
            QVERIFY2(std::abs(grid->columnWidth(0) + grid->columnWidth(1) - total) <= 2, qPrintable(u"%1 + %2 px"_s.arg(grid->columnWidth(0)).arg(grid->columnWidth(1))));
            QVERIFY(!openDialog(win));
            // la largeur choisie résiste à un changement de taille de la fenêtre
            win.resize(1200, 800);
            settle(200);
            QVERIFY(std::abs(grid->columnWidth(0) - (before - 200)) <= 2);
            win.resize(1300, 800);
            settle(200);
            // double-clic sur la poignée : retour au partage égal
            const QPoint handle(header->sectionViewportPosition(0) + grid->columnWidth(0) - 2, header->height() / 2);
            QTest::mouseDClick(header->viewport(), Qt::LeftButton, {}, handle);
            settle(200);
            QVERIFY2(std::abs(grid->columnWidth(0) - grid->columnWidth(1)) <= 1, qPrintable(u"%1 et %2 px"_s.arg(grid->columnWidth(0)).arg(grid->columnWidth(1))));
        }
        // beaucoup de colonnes : une largeur minimale, puis la barre de défilement
        for (int k = 0; k < 24; ++k)
            addColumn(u"c%1"_s.arg(k));
        QVERIFY(grid->columnWidth(0) >= 90);
        QVERIFY(grid->horizontalScrollBar()->maximum() > 0);
        // la poignée de largeur est aussi au bord gauche de la colonne voisine (celle de « + » comprise)
        const int last = grid->model()->columnCount() - 1;
        const int edge = header->sectionViewportPosition(last);
        QCOMPARE(header->handleAt(QPoint(edge + 1, header->height() / 2)), last - 1);
        QCOMPARE(header->handleAt(QPoint(header->sectionViewportPosition(last) + theme::px(20), header->height() / 2)), -1);
        // cliquer près du bord de « + » ne demande pas de nouvelle grandeur
        QTest::mousePress(header->viewport(), Qt::LeftButton, {}, QPoint(edge + 1, header->height() / 2));
        QTest::mouseRelease(header->viewport(), Qt::LeftButton, {}, QPoint(edge + 1, header->height() / 2));
        settle(40);
        QVERIFY(!openDialog(win));
    }

    void dragColumnsToGraph()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        TableGrid *grid = firstGrid(win);
        QVERIFY(grid);
        // Ctrl + clic sur un second en-tête : les deux colonnes sélectionnées, glissées ensemble
        grid->selectColumns(0, Qt::NoModifier);
        grid->selectColumns(1, Qt::ControlModifier);
        QCOMPARE(grid->selectedColumns(true), (std::vector<int>{0, 1}));
        const QId t = store.ws.doc.byName(u"t"_s)->id, z = store.ws.doc.byName(u"z"_s)->id;
        std::unique_ptr<QMimeData> mime(dnd::quantitiesMime({t, z}));
        QCOMPARE(dnd::quantitiesFrom(mime.get()), (std::vector<QId>{t, z}));
        QCOMPARE(dnd::quantityFrom(mime.get()), std::optional<QId>(t));
        // déposées sur un graphe vide : z(t) en une fois
        win.newGraph();
        settle();
        const ViewId g = store.ws.graphs.back().id;
        graphops::dropQuantities(win, g, 0, {t, z}, 1);
        settle();
        QCOMPARE(store.ws.graph(g)->plots[0].curves.size(), size_t(1));
        QCOMPARE(store.ws.graph(g)->plots[0].curves[0].x, t);
        QCOMPARE(store.ws.graph(g)->plots[0].curves[0].y, z);
        // une seconde fois : déjà tracée, rien n'est ajouté
        graphops::dropQuantities(win, g, 0, {t, z}, 1);
        QCOMPARE(store.ws.graph(g)->plots[0].curves.size(), size_t(1));
        // Ctrl + clic sur une colonne sélectionnée : elle est retirée de la sélection
        grid->selectColumns(1, Qt::ControlModifier);
        QCOMPARE(grid->selectedColumns(true), (std::vector<int>{0}));
    }

    void graphSettingsKeepTheirWidth()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        const ViewId g = store.ws.graphs.front().id;
        GraphPanel *gp = win.graphPanel(g);
        QVERIFY(gp);
        auto *settings = gp->findChild<GraphSettings *>();
        QVERIFY(settings);
        gp->openSettings(SettingsSection::Curves);
        settle(120);
        // place prise au tableur et aux Grandeurs : le graphe reste visible à côté des réglages
        QVERIFY(gp->canvas()->isVisible());
        QVERIFY(settings->isVisible());
        // jamais écrasés : à leur largeur minimale au moins ; le graphe à côté s'il y a la place,
        // sinon caché, avec une explication
        QVERIFY2(settings->width() >= settings->minimumSizeHint().width(), qPrintable(u"%1 < %2"_s.arg(settings->width()).arg(settings->minimumSizeHint().width())));
        QVERIFY(clipped(gp).isEmpty());
        shot(gp, u"reglages-ouverts"_s);
        // le bouton ✕ des réglages les ferme
        auto *close = settings->findChild<QToolButton *>(u"closeSettings"_s);
        QVERIFY(close);
        close->click();
        settle();
        QVERIFY(!settings->isVisible());
        QVERIFY(gp->canvas()->isVisible());
    }

    void annotationsByMouse()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        const ViewId g = store.ws.graphs.front().id;
        GraphCanvas *canvas = win.graphPanel(g)->canvas();
        const PlotLayout l = canvas->layouts().front();
        // droite oblique par deux points, au milieu du graphe
        const double x0 = l.x.fromPx(l.area.left() + l.area.width() * 0.3), x1 = l.x.fromPx(l.area.left() + l.area.width() * 0.7);
        const double y0 = l.y.fromPx(l.area.top() + l.area.height() * 0.7), y1 = l.y.fromPx(l.area.top() + l.area.height() * 0.3);
        store.edit([&](Workspace &ws) {
            Annotation a;
            a.kind = Annotation::Line;
            a.x = x0;
            a.y = y0;
            a.x2 = x1;
            a.y2 = y1;
            ws.graphMut(g)->plots[0].annotations.push_back(a);
        });
        settle();
        const auto &as = [&]() -> const std::vector<Annotation> & { return store.ws.graph(g)->plots[0].annotations; };
        const QPoint mid = QPointF((l.x.toPx(x0) + l.x.toPx(x1)) / 2, (l.y.toPx(y0) + l.y.toPx(y1)) / 2).toPoint();
        // glissée : les deux points se déplacent de même
        QTest::mousePress(canvas, Qt::LeftButton, {}, mid);
        QTest::mouseMove(canvas, mid + QPoint(10, 20));
        QTest::mouseMove(canvas, mid + QPoint(20, 40));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, mid + QPoint(20, 40));
        settle();
        const Annotation moved = as()[0];
        QVERIFY(std::abs(l.x.toPx(moved.x) - l.x.toPx(x0) - 20) < 1.5 && std::abs(l.y.toPx(moved.y2) - l.y.toPx(y1) - 40) < 1.5);
        // inclinée par une poignée : le second point seul
        const QPoint h2 = QPointF(l.x.toPx(moved.x2), l.y.toPx(moved.y2)).toPoint();
        QTest::mousePress(canvas, Qt::LeftButton, {}, h2);
        QTest::mouseMove(canvas, h2 + QPoint(0, -30));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, h2 + QPoint(0, -30));
        settle();
        QCOMPARE(as()[0].x, moved.x);
        QCOMPARE(as()[0].y, moved.y);
        QVERIFY(std::abs(l.y.toPx(as()[0].y2) - (h2.y() - 30)) < 1.5);
        shot(canvas, u"droite-oblique"_s);
        // verrouillée : elle ne bouge plus
        store.edit([&](Workspace &ws) { ws.graphMut(g)->plots[0].annotations[0].locked = true; });
        settle();
        const Annotation locked = as()[0];
        const QPoint m2 = QPointF((l.x.toPx(locked.x) + l.x.toPx(locked.x2)) / 2, (l.y.toPx(locked.y) + l.y.toPx(locked.y2)) / 2).toPoint();
        QTest::mousePress(canvas, Qt::LeftButton, {}, m2);
        QTest::mouseMove(canvas, m2 + QPoint(30, 0));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, m2 + QPoint(30, 0));
        settle();
        QCOMPARE(as()[0].x, locked.x);
        QCOMPARE(as()[0].x2, locked.x2);
        // réglages : verrou et taille du texte à côté de la couleur
        store.edit([&](Workspace &ws) {
            Annotation t;
            t.kind = Annotation::Text;
            t.text = u"repère"_s;
            t.x = x0;
            t.y = y0;
            ws.graphMut(g)->plots[0].annotations.push_back(t);
        });
        win.graphPanel(g)->openSettings(SettingsSection::Annotations);
        settle(120);
        auto *settings = win.graphPanel(g)->findChild<GraphSettings *>();
        const QList<QToolButton *> locks = settings->findChildren<QToolButton *>(u"annotationLock"_s);
        QCOMPARE(locks.size(), 2);
        QVERIFY(locks[0]->isChecked() && !locks[1]->isChecked());
        shot(settings, u"reglages-annotations"_s);
    }

    void modelingDetachedWithRangeBars()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        const ViewId g = store.ws.graphs.front().id;
        // la fenêtre Modélisation s'ouvre à part
        win.openModeling(g, 0, 0);
        settle(120);
        Dock *m = dockOf(win, TabKey{TabKey::Modeling});
        QVERIFY(m && m->detached() && m->isWindow() && m->isVisible());
        ModelingPanel *mp = ModelingPanel::instance();
        QVERIFY(mp);
        QCOMPARE(mp->shownFit(g, 0), std::optional<int>(0));
        // nom du modèle dans la légende
        auto *label = mp->findChild<QLineEdit *>(u"fitLabel"_s);
        QVERIFY(label);
        label->setText(u"ma parabole"_s);
        Q_EMIT label->editingFinished();
        settle();
        QCOMPARE(store.ws.graph(g)->plots[0].fits[0].label, std::optional<QString>(u"ma parabole"_s));
        // plage : deux barres sur le graphe, glissées à la souris
        store.edit([&](Workspace &ws) { ws.graphMut(g)->plots[0].fits[0].range = std::pair(0.1, 0.5); });
        settle(120);
        GraphCanvas *canvas = win.graphPanel(g)->canvas();
        const PlotLayout l = canvas->layouts().front();
        const QPoint bar(static_cast<int>(std::lround(l.x.toPx(0.1))), static_cast<int>(l.area.center().y()));
        QTest::mousePress(canvas, Qt::LeftButton, {}, bar);
        QTest::mouseMove(canvas, QPoint(static_cast<int>(std::lround(l.x.toPx(0.2))), bar.y()));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(static_cast<int>(std::lround(l.x.toPx(0.2))), bar.y()));
        settle();
        const auto range = *store.ws.graph(g)->plots[0].fits[0].range;
        QVERIFY2(std::abs(range.first - 0.2) < 0.01 && range.second == 0.5, qPrintable(u"%1 %2"_s.arg(range.first).arg(range.second)));
        // étendue de la courbe tracée « de … à … » : ses barres (en pointillés) se glissent aussi
        store.edit([&](Workspace &ws) {
            FitSpec &f = ws.graphMut(g)->plots[0].fits[0];
            f.draw = FitDraw::Custom;
            f.drawRange = {0.02, 0.58};
        });
        settle(120);
        const QPoint end(static_cast<int>(std::lround(l.x.toPx(0.58))), static_cast<int>(l.area.center().y()));
        QTest::mousePress(canvas, Qt::LeftButton, {}, end);
        QTest::mouseMove(canvas, QPoint(static_cast<int>(std::lround(l.x.toPx(0.4))), end.y()));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(static_cast<int>(std::lround(l.x.toPx(0.4))), end.y()));
        settle();
        const auto drawn = store.ws.graph(g)->plots[0].fits[0].drawRange;
        QVERIFY2(std::abs(drawn.second - 0.4) < 0.01 && drawn.first == 0.02, qPrintable(u"%1 %2"_s.arg(drawn.first).arg(drawn.second)));
        shot(canvas, u"modelisation-plage"_s);
        // le modèle se rattache comme les autres fenêtres
        m->titleBar()->detachButton()->click();
        settle();
        QVERIFY(!m->detached());
    }

    void tangentsCanBeRemoved()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        const ViewId g = store.ws.graphs.front().id;
        const QId t = store.ws.doc.byName(u"t"_s)->id, z = store.ws.doc.byName(u"z"_s)->id;
        store.edit([&](Workspace &ws) {
            TangentSpec ts;
            ts.x = t;
            ts.y = z;
            ws.graphMut(g)->plots[0].tangents.push_back(ts);
        });
        win.graphPanel(g)->openSettings(SettingsSection::Models);
        settle(120);
        auto *settings = win.graphPanel(g)->findChild<GraphSettings *>();
        auto *lock = settings->findChild<QToolButton *>(u"tangentLock"_s);
        auto *del = settings->findChild<QToolButton *>(u"tangentDelete"_s);
        QVERIFY(lock && del);
        lock->click();
        settle();
        QVERIFY(store.ws.graph(g)->plots[0].tangents[0].locked);
        settings = win.graphPanel(g)->findChild<GraphSettings *>();
        settings->findChild<QToolButton *>(u"tangentDelete"_s)->click();
        settle();
        QVERIFY(store.ws.graph(g)->plots[0].tangents.empty());
    }

    void csvExportOptions()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(0);
        settle();
        const QId t = store.ws.doc.byName(u"t"_s)->id, z = store.ws.doc.byName(u"z"_s)->id;
        csvimport::exportFile(win, {t, z});
        settle(40);
        QDialog *d = openDialog(win);
        QVERIFY(d && d->objectName() == u"csvExport");
        auto *preview = d->findChild<QPlainTextEdit *>(u"preview"_s);
        auto *sep = d->findChild<QComboBox *>(u"separator"_s);
        auto *dec = d->findChild<QComboBox *>(u"decimal"_s);
        auto *head = d->findChild<QComboBox *>(u"header"_s);
        QVERIFY(preview && sep && dec && head);
        // en français : point-virgule, virgule décimale, « nom (unité) »
        QVERIFY2(preview->toPlainText().startsWith(u"t (s);z (m)\n0;1,49824"_s), qPrintable(preview->toPlainText()));
        shot(d, u"export-csv"_s);
        // tabulation, point décimal, unités sur la deuxième ligne
        sep->setCurrentIndex(2);
        dec->setCurrentIndex(1);
        head->setCurrentIndex(1);
        settle(20);
        QVERIFY2(preview->toPlainText().startsWith(u"t ⇥ z\ns ⇥ m\n0 ⇥ 1.49824"_s), qPrintable(preview->toPlainText()));
        // virgule entre les valeurs : le point décimal est imposé
        sep->setCurrentIndex(1);
        dec->setCurrentIndex(0);
        settle(20);
        QCOMPARE(dec->currentIndex(), 1);
        d->reject();
        settle(20);
        // écriture : codage Windows (accents), sans marque UTF-8
        QTemporaryDir dir;
        const QString path = dir.filePath(u"e.csv"_s);
        store.edit([&](Workspace &ws) { ws.doc.setUnit(t, u"µs"_s); });
        QVERIFY(win.writeCsv(path, {t}, csv::FRENCH, csv::Encoding::Windows1252));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.startsWith("t (\xb5s)"));
    }

    void csvImportPreview()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        settle();
        QTemporaryDir dir;
        // fichier « à la française » : titre, noms et unités, une ligne vide, une case illisible
        const QString path = dir.filePath(u"mesures.csv"_s);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Mesures du 12 octobre\nt (s);U (V);remarque\n0;1,5;\n\n0,1;1,8;ok\n0,2;2.1;\n0,3;2,4;\n");
        }
        csvimport::open(win, path, std::nullopt);
        settle(40);
        QDialog *d = openDialog(win);
        QVERIFY(d && d->objectName() == u"csvImport");
        auto *sep = d->findChild<QComboBox *>(u"separator"_s);
        auto *dec = d->findChild<QComboBox *>(u"decimal"_s);
        auto *names = d->findChild<QSpinBox *>(u"nameLine"_s);
        auto *first = d->findChild<QSpinBox *>(u"firstLine"_s);
        auto *skip = d->findChild<QCheckBox *>(u"skipEmpty"_s);
        auto *preview = d->findChild<QTableWidget *>(u"preview"_s);
        auto *summary = d->findChild<QLabel *>(u"summary"_s);
        QVERIFY(sep && dec && names && first && skip && preview && summary);
        // réglages devinés
        QCOMPARE(sep->currentData().value<QChar>(), QChar(u';'));
        QCOMPARE(dec->currentIndex(), 0);
        QCOMPARE(names->value(), 2);
        QCOMPARE(first->value(), 3);
        QVERIFY(skip->isChecked());
        // aperçu : trois colonnes, la troisième (sans nombre) décochée ; « 2.1 » signalé
        QCOMPARE(preview->columnCount(), 3);
        QCOMPARE(preview->item(1, 0)->text(), u"t"_s);
        QCOMPARE(preview->item(2, 1)->text(), u"V"_s);
        QCOMPARE(preview->item(0, 2)->checkState(), Qt::Unchecked);
        QVERIFY(summary->text().contains(u"2.1"_s));
        QVERIFY(summary->text().contains(u"1 ligne vide ignorée"_s));
        shot(d, u"import-csv"_s);
        // la colonne U renommée avant l'import
        preview->item(1, 1)->setText(u"Uc"_s);
        settle(40);
        QPushButton *ok = nullptr;
        for (QPushButton *b : d->findChildren<QPushButton *>())
            if (b->text() == u"Importer")
                ok = b;
        QVERIFY(ok && ok->isEnabled());
        ok->click();
        settle();
        const Quantity *t = store.ws.doc.byName(u"t"_s);
        const Quantity *u = store.ws.doc.byName(u"Uc"_s);
        QVERIFY(t && u && !store.ws.doc.byName(u"remarque"_s));
        QCOMPARE(t->unit, u"s"_s);
        QCOMPARE(t->values, (std::vector<double>{0, 0.1, 0.2, 0.3}));
        QCOMPARE(u->values.size(), size_t(4));
        QVERIFY(std::isnan(u->values[2]));
        QCOMPARE(store.ws.tables.size(), size_t(1));
        QCOMPARE(store.ws.tables.front().name, u"mesures"_s);

        // mauvais séparateur choisi : l'import est refusé tant qu'il n'y a pas de nombres
        csvimport::open(win, path, store.ws.tables.front().id);
        settle(40);
        d = openDialog(win);
        QVERIFY(d);
        sep = d->findChild<QComboBox *>(u"separator"_s);
        dec = d->findChild<QComboBox *>(u"decimal"_s);
        summary = d->findChild<QLabel *>(u"summary"_s);
        QVERIFY(summary->text().contains(u"au tableur « mesures »"_s));
        sep->setCurrentIndex(3); // espaces
        settle(40);
        ok = nullptr;
        for (QPushButton *b : d->findChildren<QPushButton *>())
            if (b->text() == u"Importer")
                ok = b;
        QVERIFY(ok && !ok->isEnabled());
        QVERIFY2(summary->text().contains(u"Rien à importer"_s), qPrintable(summary->text()));
        // mauvais séparateur décimal : les cases illisibles sont signalées, et ce que donnerait l'autre
        sep->setCurrentIndex(0);
        dec->setCurrentIndex(1);
        settle(40);
        QVERIFY(ok->isEnabled());
        QVERIFY2(summary->text().contains(u"seraient lues"_s), qPrintable(summary->text()));
        // virgule comme séparateur des valeurs : le séparateur décimal est forcément le point
        dec->setCurrentIndex(0);
        sep->setCurrentIndex(1);
        settle(40);
        QCOMPARE(dec->currentIndex(), 1);
        d->reject();
        settle(20);
    }

    void dialogsOpen()
    {
        Store store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.loadExample(5);
        settle();
        const QId u = store.ws.doc.byName(u"U"_s)->id;
        const std::vector<std::pair<QString, std::function<void()>>> all = {
            {u"grandeur"_s, [&] { dialogs::newQuantity(win, false); }},
            {u"calculee"_s, [&] { dialogs::newQuantity(win, true); }},
            {u"parametre"_s, [&] { dialogs::newParameter(win); }},
            {u"incertitude"_s, [&] { dialogs::newUncertainty(win, u); }},
            {u"suite"_s, [&] { dialogs::fill(win, u); }},
            {u"suppression"_s, [&] { dialogs::confirmDelete(win, u); }},
            {u"edition"_s, [&] { dialogs::editQuantity(win, u); }},
            {u"colonne"_s, [&] { dialogs::newColumn(win, store.ws.tables.front().id); }},
            {u"derivee"_s, [&] { dialogs::derivative(win, false); }},
            {u"integrale"_s, [&] { dialogs::integral(win); }},
            {u"spectre"_s, [&] { dialogs::spectrum(win); }},
            {u"tangentes"_s, [&] { dialogs::tangents(win); }},
            {u"methode-derivee"_s, [&] { dialogs::derivativeMethod(win); }},
            {u"infos"_s, [&] { dialogs::meta(win); }},
            {u"preferences"_s, [&] { dialogs::preferences(win); }},
            {u"python"_s, [&] { dialogs::pythonExport(win); }},
        };
        for (const auto &[name, open] : all) {
            open();
            settle(40);
            QDialog *d = openDialog(win);
            QVERIFY2(d, qPrintable(name));
            shot(d, u"dialogue-"_s + name);
            d->reject();
            settle(20);
        }
        // nom refusé : le message (sur deux lignes) agrandit la boîte sans écraser les champs, et
        // s'efface dès que le nom est corrigé
        {
            dialogs::newColumn(win, store.ws.tables.front().id);
            settle(40);
            QDialog *col = openDialog(win);
            QVERIFY(col);
            const QList<QLineEdit *> edits = col->findChildren<QLineEdit *>();
            QVERIFY(!edits.isEmpty());
            QLineEdit *nameEdit = edits.front();
            const int fieldHeight = nameEdit->height();
            nameEdit->setText(u"54j"_s);
            QPushButton *create = nullptr;
            for (QPushButton *b : col->findChildren<QPushButton *>())
                if (b->text() == u"Créer"_s)
                    create = b;
            QVERIFY(create);
            create->click();
            settle(60);
            QLabel *error = nullptr;
            for (QLabel *l : col->findChildren<QLabel *>())
                if (l->isVisible() && l->text().startsWith(u"⚠"_s))
                    error = l;
            QVERIFY2(error, "message d'erreur absent");
            QVERIFY(error->text().contains(u"54j"_s));
            QVERIFY2(nameEdit->height() >= fieldHeight, qPrintable(u"champ de %1 px au lieu de %2"_s.arg(nameEdit->height()).arg(fieldHeight)));
            QVERIFY(nameEdit->height() >= nameEdit->sizeHint().height());
            QVERIFY(error->height() >= error->heightForWidth(error->width()));
            shot(col, u"dialogue-colonne-erreur"_s);
            QTest::keyClicks(nameEdit, u"x"_s);
            settle(40);
            QVERIFY(!error->isVisible());
            col->reject();
            settle(20);
        }
        // une incertitude créée par la boîte de dialogue s'affiche en barres sur le graphe
        dialogs::newUncertainty(win, store.ws.doc.byName(u"I"_s)->id);
        settle(40);
        QDialog *d = openDialog(win);
        QVERIFY(d);
        auto *edit = d->findChild<QLineEdit *>();
        QVERIFY(edit);
        edit->setText(u"0,003"_s);
        QPushButton *ok = nullptr;
        for (QPushButton *b : d->findChildren<QPushButton *>())
            if (b->text() == u"Créer")
                ok = b;
        QVERIFY(ok);
        ok->click();
        settle(40);
        const Quantity *iq = store.ws.doc.byName(u"I"_s);
        QVERIFY(iq->uncertainty.has_value());
        QCOMPARE(store.ws.doc.get(*iq->uncertainty)->values.front(), 0.003);
    }

    void uiZoom()
    {
        // Ctrl + / Ctrl − / Ctrl 0 : polices, lignes du tableur, graduations des graphes plus grandes
        Store store;
        MainWindow win(store);
        win.resize(2000, 1100);
        const ZoomReset reset{win};
        win.show();
        win.loadExample(5);
        win.activateWindow();
        settle(120);
        QCOMPARE(win.uiZoom(), 1.0);
        QCOMPARE(win.maxUiZoom(), 2.0);
        const ViewId graph = store.ws.graphs.front().id;
        TableGrid *grid = firstGrid(win);
        QVERIFY(grid);
        const QFont font = QApplication::font();
        const double point = font.pointSizeF() > 0 ? font.pointSizeF() : font.pixelSize();
        const auto size = [] {
            const QFont f = QApplication::font();
            return f.pointSizeF() > 0 ? f.pointSizeF() : f.pixelSize();
        };
        const int row = grid->rowHeight(0);
        const int header = grid->horizontalHeader()->height();
        const int column = grid->columnWidth(0);
        const int viewportWidth = grid->viewport()->width();
        const double margin = bottomMargin(win, graph);
        QVERIFY(margin > 20);
        const int icon = win.findChild<QToolBar *>()->iconSize().height();
        shot(&win, u"agrandissement-100"_s);
        for (const double zoom : {1.5, 2.0}) {
            win.setUiZoom(zoom);
            settle(120);
            QCOMPARE(win.uiZoom(), zoom);
            QCOMPARE(theme::zoom(), zoom);
            QCOMPARE(store.prefs.uiZoom, zoom);
            QVERIFY(std::abs(size() - point * zoom) < 0.6);
            QVERIFY2(grid->rowHeight(0) >= std::floor(row * zoom * 0.95), qPrintable(u"%1 → %2"_s.arg(row).arg(grid->rowHeight(0))));
            QVERIFY(grid->horizontalHeader()->height() >= header * zoom * 0.9);
            // colonnes au moins à leur largeur minimale (agrandie), réparties sur toute la largeur
            QVERIFY2(grid->columnWidth(0) >= std::lround(90 * zoom) - 1, qPrintable(u"%1 → %2"_s.arg(column).arg(grid->columnWidth(0))));
            int total = 0;
            for (int c = 0; c < grid->model()->columnCount(); ++c)
                total += grid->columnWidth(c);
            QVERIFY2(total >= grid->viewport()->width() - 2, qPrintable(u"%1 px pour %2 px"_s.arg(total).arg(grid->viewport()->width())));
            // texte des graduations et marges du graphe agrandis dans la même proportion
            const double m = bottomMargin(win, graph);
            QVERIFY2(std::abs(m / margin - zoom) < 0.12 * zoom, qPrintable(u"marge %1 → %2"_s.arg(margin).arg(m)));
            QCOMPARE(win.findChild<QToolBar *>()->iconSize().height(), static_cast<int>(std::lround(icon * zoom)));
            // les points restent dans la zone de tracé, à leur place en pixels du widget ; le
            // réticule s'y accroche
            GraphCanvas *canvas = win.graphPanel(graph)->canvas();
            const PlotLayout &l = canvas->layouts().front();
            const Quantity *iq = store.ws.doc.byName(u"I"_s), *uq = store.ws.doc.byName(u"U"_s);
            const QPointF p(l.x.toPx(iq->values[5]), l.y.toPx(uq->values[5]));
            QVERIFY(l.area.contains(p) && canvas->rect().contains(l.area.toRect()));
            QTest::mouseMove(canvas, (p + QPointF(3, -2) * zoom).toPoint());
            settle(60);
            shot(win.graphPanel(graph), u"agrandissement-reticule-%1"_s.arg(std::lround(zoom * 100)));
            shot(&win, u"agrandissement-%1"_s.arg(std::lround(zoom * 100)));
        }
        // au plus ce que permet la largeur de la fenêtre ; l'agrandissement demandé revient quand
        // la fenêtre s'élargit
        win.setUiZoom(3.0);
        settle();
        QCOMPARE(win.uiZoom(), 2.0);
        // c'est l'agrandissement demandé qui est enregistré
        QCOMPARE(store.prefs.uiZoom, 3.0);
        win.resize(1500, 1000);
        settle();
        QCOMPARE(win.uiZoom(), 1.5);
        QCOMPARE(store.prefs.uiZoom, 3.0);
        win.resize(3000, 1100);
        settle();
        QCOMPARE(win.uiZoom(), 3.0);
        win.resize(2000, 1100);
        settle();
        QCOMPARE(win.uiZoom(), 2.0);
        // raccourcis : Ctrl 0, Ctrl +, Ctrl =, Ctrl −, par pas de 10 %
        win.activateWindow();
        QTest::keyClick(&win, Qt::Key_0, Qt::ControlModifier);
        settle();
        QCOMPARE(win.uiZoom(), 1.0);
        QTest::keyClick(&win, Qt::Key_Plus, Qt::ControlModifier);
        settle();
        QCOMPARE(win.uiZoom(), 1.1);
        QTest::keyClick(&win, Qt::Key_Equal, Qt::ControlModifier);
        settle();
        QCOMPARE(win.uiZoom(), 1.2);
        QTest::keyClick(&win, Qt::Key_Minus, Qt::ControlModifier);
        QTest::keyClick(&win, Qt::Key_Minus, Qt::ControlModifier);
        QTest::keyClick(&win, Qt::Key_Minus, Qt::ControlModifier);
        settle();
        QCOMPARE(win.uiZoom(), 0.9);
        QVERIFY(size() < point);
        // retour à 100 % : tout reprend sa taille
        QTest::keyClick(&win, Qt::Key_0, Qt::ControlModifier);
        settle(120);
        QCOMPARE(win.uiZoom(), 1.0);
        QCOMPARE(theme::zoom(), 1.0);
        QCOMPARE(store.prefs.uiZoom, 1.0);
        QCOMPARE(size(), point);
        QCOMPARE(grid->rowHeight(0), row);
        // colonnes réparties sur la largeur du tableur : les mêmes s'il a retrouvé sa largeur
        if (grid->viewport()->width() == viewportWidth)
            QCOMPARE(grid->columnWidth(0), column);
        QVERIFY(grid->columnWidth(0) >= 89);
        QVERIFY(std::abs(bottomMargin(win, graph) - margin) < 0.5);
    }

    void uiZoomNothingClipped()
    {
        // à 150 % et 200 %, en clair et en sombre : aucune fenêtre rognée ni hors de la fenêtre,
        // menus, barre d'outils et barre d'état en entier ; fenêtre de 2000 px, la plus étroite
        // qui permette 200 %
        for (const bool dark : {false, true}) {
            theme::choose(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            Store store;
            MainWindow win(store);
            win.resize(2000, 1100);
            const ZoomReset reset{win};
            win.show();
            win.loadExample(5);
            settle(120);
            const ViewId graph = store.ws.graphs.front().id;
            double zoom = 1.0;
            const auto check = [&] {
                for (Dock *d : win.findChildren<Dock *>()) {
                    // onglets à l'arrière-plan : Qt les place hors de la fenêtre
                    if (!d->isVisible() || !d->widget()->isVisible() || d->widget()->width() <= 0 || d->visibleRegion().isEmpty())
                        continue;
                    const QString why = clipped(d->widget());
                    QVERIFY2(why.isEmpty(), qPrintable(u"%1 % : "_s.arg(zoom * 100) + why));
                    if (!d->isFloating())
                        QVERIFY2(d->geometry().left() >= 0 && d->geometry().right() < win.width(),
                                 qPrintable(u"%1 % : %2 hors de la fenêtre"_s.arg(zoom * 100).arg(d->windowTitle())));
                }
                QToolBar *bar = win.findChild<QToolBar *>();
                for (QAction *a : bar->actions())
                    QVERIFY2(bar->widgetForAction(a)->isVisible(), qPrintable(a->text()));
                for (QAction *m : win.menuBar()->actions()) {
                    const QRect r = win.menuBar()->actionGeometry(m);
                    QVERIFY2(!r.isEmpty() && r.right() < win.menuBar()->cornerWidget(Qt::TopRightCorner)->x(), qPrintable(m->text()));
                }
                for (QToolButton *b : win.menuBar()->cornerWidget(Qt::TopRightCorner)->findChildren<QToolButton *>())
                    QVERIFY2(b->width() >= b->sizeHint().width(), qPrintable(b->objectName()));
                QVERIFY(win.statusBar()->height() >= win.statusBar()->fontMetrics().height());
            };
            for (const double z : {1.0, 1.5, 2.0}) {
                zoom = z;
                win.setUiZoom(zoom);
                settle(120);
                QCOMPARE(win.uiZoom(), zoom);
                const QString suffix = u"%1%2"_s.arg(std::lround(zoom * 100)).arg(dark ? u"-sombre"_s : QString());
                shot(&win, u"agrandissement-"_s + suffix);
                check();
                // les autres panneaux : modélisation, statistiques, script
                win.openModeling(graph, 0, 0);
                settle(120);
                check();
                shot(&win, u"agrandissement-modelisation-"_s + suffix);
                win.openStatistics(store.ws.doc.quantities.front().id);
                settle(120);
                check();
                shot(&win, u"agrandissement-statistiques-"_s + suffix);
                win.newScript();
                settle(120);
                check();
                shot(&win, u"agrandissement-script-"_s + suffix);
                // réglages d'un graphe à côté des trois colonnes : il faut la place d'une colonne
                // de plus (1200 px à 100 %)
                win.defaultLayout();
                win.resize(std::max(2000, static_cast<int>(1200 * zoom)), 1100);
                settle(120);
                win.graphPanel(graph)->openSettings(SettingsSection::Curves);
                settle(120);
                check();
                shot(&win, u"agrandissement-reglages-"_s + suffix);
                win.defaultLayout();
                win.resize(2000, 1100);
                settle(120);
            }
            // page d'accueil agrandie
            win.newFile();
            settle(120);
            QVERIFY(win.centralWidget()->isVisible());
            QVERIFY(clipped(win.centralWidget()).isEmpty());
            shot(&win, u"agrandissement-accueil"_s + (dark ? u"-sombre"_s : QString()));
            win.setUiZoom(1.0);
            settle();
            QCOMPARE(theme::zoom(), 1.0);
        }
        theme::choose(Qt::ColorScheme::Light);
    }

    void uiZoomLeavesExportsUnchanged()
    {
        // seul l'affichage à l'écran suit l'agrandissement : PNG et SVG identiques avant et après
        Store store;
        MainWindow win(store);
        win.resize(2000, 1100);
        const ZoomReset reset{win};
        win.show();
        win.loadExample(5);
        settle(120);
        const GraphView &g = store.ws.graphs.front();
        QTemporaryDir dir;
        const auto write = [&](const QString &name, exporter::Format fmt) {
            QString error;
            const QString path = dir.filePath(name);
            if (!exporter::writeGraph(path, fmt, g, store.ws, QSizeF(720, 460), store.style(), &error))
                return QByteArray();
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QImage screen = win.graphPanel(g.id)->canvas()->grab().toImage();
        const QByteArray png = write(u"avant.png"_s, exporter::Format::Png);
        const QByteArray svg = write(u"avant.svg"_s, exporter::Format::Svg);
        QVERIFY(!png.isEmpty() && !svg.isEmpty());
        const QImage image = exporter::graphImage(g, store.ws.doc, QSizeF(720, 460), 2.0, store.style());
        for (const double zoom : {1.5, 2.0}) {
            win.setUiZoom(zoom);
            settle(120);
            QCOMPARE(write(u"apres.png"_s, exporter::Format::Png), png);
            QCOMPARE(write(u"apres.svg"_s, exporter::Format::Svg), svg);
            QCOMPARE(exporter::graphImage(g, store.ws.doc, QSizeF(720, 460), 2.0, store.style()), image);
            // à l'écran, le graphe a changé
            QVERIFY(win.graphPanel(g.id)->canvas()->grab().toImage() != screen);
        }
        QTest::keyClick(&win, Qt::Key_0, Qt::ControlModifier);
        win.setUiZoom(1.0);
        settle();
        QCOMPARE(theme::zoom(), 1.0);
    }
};

QTEST_MAIN(TestApp)
#include "test_app.moc"
