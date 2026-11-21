// Tests de l'interface : fenêtre principale (page d'accueil, barre d'outils, barre d'état, thème,
// écran de démarrage), enregistrement et réouverture, réglages des graphes, modélisation,
// scripts, déplacement des fenêtres. Avec MEVREG_SHOTS=dossier, des captures d'écran de contrôle y
// sont enregistrées.

#include "app/about.h"
#include "app/calculationspanel.h"
#include "app/crash.h"
#include "app/csvimport.h"
#include "app/dialogs.h"
#include "app/docking.h"
#include "app/graphsettings.h"
#include "app/graphwidget.h"
#include "app/help.h"
#include "app/mainwindow.h"
#include "app/modelingpanel.h"
#include "app/quantitiespanel.h"
#include "app/scriptpanel.h"
#include "app/statspanel.h"
#include "app/tableview.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/csv.h"
#include "core/resources.h"
#include "python/engine.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
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
#include <QTabWidget>
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

/// Tableur et courbe de test avec des incertitudes et un ajustement.
void prepare_graphe(MainWindow &win, bool electrique = false)
{
    Seance ws;
    std::vector<double> abscisses, ordonnees;
    for (int i = 0; i < 16; ++i) {
        const double x = 0.04 * i;
        abscisses.push_back(x);
        ordonnees.push_back(electrique ? 10 * x : 1.25 + x * x);
    }
    const QId x = *ws.doc.ajoute_experimentale(electrique ? u"I"_s : u"t"_s, electrique ? u"A"_s : u"s"_s, abscisses);
    const QId y = *ws.doc.ajoute_experimentale(electrique ? u"U"_s : u"z"_s, electrique ? u"V"_s : u"m"_s, ordonnees);
    const QId ux = *ws.doc.cree_incertitude(x, u"0.002"_s);
    const QId uy = *ws.doc.cree_incertitude(y, u"0.01"_s);
    ws.ajoute_tableur({x, y, ux, uy});
    Plot &plot = ws.graphe_modifiable(ws.ajoute_graphe())->plots.front();
    plot.ajoute_courbe(x, y).errors = ErrorBars::Both;
    const fit::ModelKind modele{electrique ? fit::ModelKind::Linear : fit::ModelKind::Parabola};
    plot.fits.push_back(FitSpec::fabrique(modele, x, y, PALETTE[3]));
    win.depot().reinitialise(std::move(ws));
    win.disposition_par_defaut();
}

/// Plantage pour l'essai du journal : accès mémoire invalide, sans fichier core (Linux).
[[noreturn]] void plante_maintenant()
{
#ifdef Q_OS_LINUX
    prctl(PR_SET_DUMPABLE, 0);
#endif
    volatile std::uintptr_t address = 0;
    *reinterpret_cast<volatile int *>(address) = 0;
    std::abort();
}

/// Exception que rien ne rattrape, dans un fil de calcul (essai du journal), sans fichier core.
[[noreturn]] void exception_dans_un_fil()
{
#ifdef Q_OS_LINUX
    prctl(PR_SET_DUMPABLE, 0);
#endif
    std::thread([] { throw std::runtime_error("essai d'exception"); }).join();
    std::abort();
}

void cliche(QWidget *w, const QString &name)
{
    const QString dir = qEnvironmentVariable("MEVREG_SHOTS");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QApplication::processEvents();
    w->grab().save(QDir(dir).filePath(name + u".png"_s));
}

/// Capture d'une fenêtre avec un menu ouvert par-dessus (le menu est une fenêtre à part, absente
/// de `grab`), rognée autour du menu.
void cliche_avec_menu(QWidget *w, QMenu *menu, const QString &name)
{
    const QString dir = qEnvironmentVariable("MEVREG_SHOTS");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QApplication::processEvents();
    QPixmap pm = w->grab();
    const QRect at(w->mapFromGlobal(menu->pos()), menu->size());
    {
        QPainter p(&pm);
        p.drawPixmap(at.topLeft(), menu->grab());
    }
    const QRect keep = QRect(0, 0, at.right() + 180, at.bottom() + 24) & w->rect();
    const qreal dpr = pm.devicePixelRatio();
    pm.copy(QRect(keep.topLeft() * dpr, keep.size() * dpr)).save(QDir(dir).filePath(name + u".png"_s));
}

void stabilise(int ms = 60)
{
    for (int k = 0; k < 4; ++k) {
        QApplication::processEvents();
        QTest::qWait(ms / 4);
    }
}

Dock *fenetre_de(MainWindow &win, const TabKey &k)
{
    for (Dock *d : win.findChildren<Dock *>())
        if (d->key == k)
            return d;
    return nullptr;
}

/// Accepte la première boîte de dialogue ouverte (bouton par défaut).
QDialog *ouvre_dialogue(MainWindow &win)
{
    for (QDialog *d : win.findChildren<QDialog *>())
        if (d->isVisible())
            return d;
    return nullptr;
}

/// Contenu rogné dans `root` : un widget plus étroit que le minimum de sa disposition, ou une zone
/// défilante dont le contenu dépasse en largeur ; description du premier trouvé, vide sinon.
QString rogne(QWidget *root)
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
double marge_basse(MainWindow &win, ViewId graph)
{
    GraphPanel *gp = win.panneau_graphe(graph);
    if (!gp || gp->canevas()->dispositions().empty())
        return 0;
    const PlotLayout &l = gp->canevas()->dispositions().front();
    return l.cell.bottom() - l.area.bottom();
}

/// Remet l'interface à 100 % en fin d'essai, même après un échec (la police de toute
/// l'application est changée).
struct ZoomReset {
    MainWindow &win;
    ~ZoomReset()
    {
        win.change_agrandissement(1.0);
        QApplication::processEvents();
    }
};

TableGrid *premiere_grille(MainWindow &win)
{
    for (Dock *d : win.findChildren<Dock *>())
        if (d->key.kind == TabKey::Table)
            if (auto *t = qobject_cast<TablePanel *>(d->widget()))
                return t->grille();
    return nullptr;
}

} // namespace

/// La plateforme « offscreen » ne gère ni les tailles conseillées des fenêtres, ni la capture de la
/// souris par une fenêtre (Qt garde celle d'un widget) : messages sans intérêt.
QtMessageHandler g_previous = nullptr;

void silencieux(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (msg.contains(u"propagateSizeHints"_s) || msg.contains(u"does not support grabbing"_s) || msg.contains(u"does not support raise"_s))
        return;
    if (g_previous)
        g_previous(type, ctx, msg);
}

class PosteExpert : public Store {
public:
    PosteExpert() { prefs.modeSimple = false; }
};

class TestApp : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        g_previous = qInstallMessageHandler(silencieux);
        // curseur de texte qui ne clignote pas : les captures sont les mêmes d'une
        // fois à l'autre
        QApplication::setCursorFlashTime(0);
        QStandardPaths::setTestModeEnabled(true);
        assure_ressources_coeur();
        theme::initialise();
        theme::choisit(Qt::ColorScheme::Light);
        // jamais de navigateur ouvert pendant les essais
        help::openUrl = [](const QUrl &) { return true; };
    }

    void parcours_simplifie_loi_ohm()
    {
        Store store;
        QVERIFY(store.prefs.modeSimple);
        MainWindow win(store);
        win.resize(1000, 700);
        win.show();
        stabilise();
        QCOMPARE(win.centralWidget()->objectName(), u"welcome"_s);
        auto bouton = [](QWidget *page, const QString &texte) -> QPushButton * {
            for (auto *b : page->findChildren<QPushButton *>())
                if (b->text() == texte && b->isVisible()) return b;
            return nullptr;
        };
        auto *debut = bouton(&win, u"Nouveau tableur"_s);
        QVERIFY(debut);
        debut->click();
        auto *dialogue = ouvre_dialogue(win);
        QVERIFY(dialogue);
        const auto champs = dialogue->findChildren<QLineEdit *>();
        QCOMPARE(champs.size(), 4);
        const QStringList saisie{u"I"_s, u"A"_s, u"U"_s, u"V"_s};
        for (int i = 0; i < champs.size(); ++i) champs[i]->setText(saisie[i]);
        auto *creer = bouton(dialogue, u"Créer le tableur"_s);
        QVERIFY(creer);
        creer->click();
        stabilise();
        QCOMPARE(win.centralWidget()->objectName(), u"guide"_s);
        auto *grille = win.centralWidget()->findChild<TableGrid *>();
        QVERIFY(grille);
        grille->setCurrentIndex(grille->model()->index(0, 0));
        grille->colle(u"0\t0\n0,1\t4,7\n0,2\t9,4\n0,3\t14,1"_s);
        auto *etapes = win.findChild<QTabWidget *>(u"etapesTP"_s);
        QVERIFY(etapes);
        etapes->setCurrentIndex(1);
        auto *trace = bouton(&win, u"Tracer les mesures"_s);
        QVERIFY(trace && trace->isEnabled());
        trace->click();
        QCOMPARE(store.ws.graphs.size(), 1U);
        trace->click();
        QCOMPARE(store.ws.graphs.size(), 1U);
        etapes->setCurrentIndex(2);
        auto *ajuste = bouton(&win, u"Ajuster aux mesures"_s);
        QVERIFY(ajuste && ajuste->isEnabled());
        ajuste->click();
        const auto &fit = store.ws.graphs.front().plots.front().fits.front();
        QVERIFY(fit.result);
        QVERIFY(std::abs(fit.result->params.front() - 47) < 1e-10);
        auto *residus = win.findChild<QCheckBox *>(u"residusGuide"_s);
        QVERIFY(residus && !residus->isEnabled());
        QVERIFY(fit.result->residusNumeriques);
        QCOMPARE(fit.result->texte_parametre(0, u"Ω"_s, store.style()), u"47 Ω"_s);
        store.edite([](Seance &ws) { ws.doc.definit_valeur(ws.doc.par_nom(u"U"_s)->id, 2, 9.2); });
        QVERIFY(residus->isEnabled());
        residus->click();
        const auto labels = win.centralWidget()->findChildren<QLabel *>();
        QVERIFY(std::any_of(labels.begin(), labels.end(), [](QLabel *l) { return l->isVisible() && l->text().contains(u"Ω"_s); }));
        const auto courbe = trace_residus(store.ws.doc, fit);
        QVERIFY(courbe && courbe->data.front().series.front().ys.size() == 4);
        cliche(&win, u"simplifie-ajustement"_s);
        store.edite([](Seance &ws) { ws.doc.definit_valeur(ws.doc.par_nom(u"U"_s)->id, 2, 9.4); });
        etapes->setCurrentIndex(3);
        auto *conclusion = win.findChild<QPlainTextEdit *>(u"conclusionTP"_s);
        QVERIFY(conclusion);
        conclusion->setPlainText(u"La résistance vaut 47 Ω."_s);
        win.choisit_mode(false);
        QVERIFY(!store.prefs.modeSimple);
        QVERIFY(!Prefs::charge().modeSimple);
        QVERIFY(win.est_ouvert({TabKey::Graph, store.ws.graphs.front().id}));
        win.choisit_mode(true);
        QCOMPARE(etapes->currentIndex(), 3);
        QVERIFY(Prefs::charge().modeSimple);
        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"seance.phy"_s);
        QVERIFY(win.enregistre(true, chemin));
        QVERIFY(win.ouvre_chemin(chemin));
        QCOMPARE(etapes->currentIndex(), 0);
        QCOMPARE(store.ws.meta.notes, u"La résistance vaut 47 Ω."_s);
        QCOMPARE(store.ws.graphs.front().plots.front().fits.size(), 1U);
    }

    void conflit_entre_deux_seances()
    {
        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"partage.phy"_s);
        Store premiere, seconde;
        MainWindow a(premiere), b(seconde);
        QVERIFY(a.enregistre(true, chemin));
        QVERIFY(b.ouvre_chemin(chemin));
        premiere.edite([](Seance &ws) { ws.meta.notes = u"Première séance"_s; });
        QVERIFY(a.enregistre(false));
        seconde.edite([](Seance &ws) { ws.meta.notes = u"Deuxième séance"_s; });
        QVERIFY(!b.enregistre(false));
        QVERIFY(seconde.est_modifie());
        QVERIFY(b.enregistre(true, dir.filePath(u"copie.phy"_s)));
        QVERIFY(a.ouvre_chemin(chemin));
        QCOMPARE(premiere.ws.meta.notes, u"Première séance"_s);
        QCOMPARE(seconde.ws.meta.notes, u"Deuxième séance"_s);
    }

    void derniere_frappe_avant_nouveau()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        win.nouveau_script(u"# Mesure "_s);
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath(u"seance.phy"_s);
        QVERIFY(win.enregistre(false, chemin));
        auto *panneau = win.findChild<ScriptPanel *>();
        QVERIFY(panneau);
        auto *editeur = panneau->editeur();
        editeur->moveCursor(QTextCursor::End);
        QTest::keyClicks(editeur, "A");

        QAction *nouveau = nullptr;
        for (auto *action : win.findChildren<QAction *>())
            if (action->shortcut() == QKeySequence(QKeySequence::New))
                nouveau = action;
        QVERIFY(nouveau);
        auto repond = [&](QMessageBox::StandardButton choix) {
            QTimer::singleShot(0, &win, [choix] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    box->button(choix)->click();
            });
            nouveau->trigger();
        };
        // Le clic suit immédiatement la frappe, sans attendre le délai de l'éditeur.
        repond(QMessageBox::Cancel);
        QCOMPARE(store.ws.scripts.size(), size_t(1));
        QCOMPARE(store.ws.scripts.front().code, u"# Mesure A"_s);
        QTest::keyClicks(editeur, "B");
        repond(QMessageBox::Save);
        QVERIFY(store.ws.scripts.empty());
        QFile fichier(chemin);
        QVERIFY(fichier.open(QIODevice::ReadOnly));
        const auto seance = phy::charge(fichier.readAll(), nullptr);
        QVERIFY(seance && seance->seance.scripts.size() == 1);
        QCOMPARE(seance->seance.scripts.front().code, u"# Mesure AB"_s);
    }

    void residus_affiches_modele_personnel()
    {
        Document doc;
        const auto t = *doc.ajoute_experimentale(u"t"_s, u"s"_s, {0, 1, 2, 3});
        const auto u = *doc.ajoute_experimentale(u"U"_s, u"V"_s, {1.1, 2.8, 5.2, 6.9});
        Plot plot;
        fit::ModelKind type{fit::ModelKind::User};
        type.expression = u"U = a*t + b"_s;
        auto &modele = plot.fits.emplace_back(FitSpec::fabrique(type, t, u, PALETTE[0]));
        maj_modeles(plot, doc);
        const auto trace = trace_residus(doc, modele);
        QVERIFY(trace && !trace->data.empty() && !trace->data.front().series.empty());
        const auto &serie = trace->data.front().series.front();
        QCOMPARE(serie.xs, doc.valeurs(t));
        QCOMPARE(trace->data.front().yUnit, u"V"_s);
        const std::vector<double> attendu{0.07, -0.21, 0.21, -0.07};
        QCOMPARE(serie.ys.size(), attendu.size());
        for (size_t i = 0; i < attendu.size(); ++i)
            QVERIFY(std::abs(serie.ys[i] - attendu[i]) < 1e-8);
    }

    void derniere_frappe_avant_export_python()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        win.nouveau_script(u"# Mesure "_s);
        auto *panneau = win.findChild<ScriptPanel *>();
        QVERIFY(panneau);
        panneau->editeur()->moveCursor(QTextCursor::End);
        QTest::keyClicks(panneau->editeur(), "C");
        dialogs::export_python(win);
        auto *dialogue = ouvre_dialogue(win);
        QVERIFY(dialogue);
        auto *apercu = dialogue->findChild<CodeEditor *>();
        QVERIFY(apercu && apercu->toPlainText().contains(u"# Mesure C\n"_s));
        dialogue->reject();
    }

    void historique_grandes_series()
    {
        Store st;
        Seance initial;
        const QId x = *initial.doc.ajoute_experimentale(u"x"_s, {}, std::vector<double>(50000, 0));
        const QId y = *initial.doc.ajoute_experimentale(u"y"_s, {}, std::vector<double>(50000, 2));
        initial.doc.ajoute_calculee(u"z"_s, {}, u"x+y"_s);
        st.reinitialise(std::move(initial));
        for (int i = 0; i < 100; ++i) {
            st.edite([&](Seance &ws) { ws.doc.definit_valeur(x, i, i + 1); });
            st.valide_maintenant();
        }
        for (int i = 99; i >= 0; --i) {
            st.defait();
            QCOMPARE(st.ws.doc.valeurs(x)[i], 0.0);
            if (i > 0) QCOMPARE(st.ws.doc.valeurs(x)[i - 1], double(i));
            QCOMPARE(st.ws.doc.par_nom(u"z"_s)->values[i], 2.0);
        }
        for (int i = 0; i < 100; ++i) st.retablit();
        QCOMPARE(st.ws.doc.valeurs(x)[99], 100.0);
        st.edite([&](Seance &ws) { ws.doc.insere_ligne({x, y}, 2); });
        st.defait();
        QCOMPARE(st.ws.doc.valeurs(x).size(), 50000U);
        st.retablit();
        QVERIFY(std::isnan(st.ws.doc.valeurs(y)[2]));
    }

    void spectre_avec_mesure_manquante()
    {
        PosteExpert st;
        MainWindow win(st);
        win.show();
        Seance ws;
        std::vector<double> t(32), u(32);
        for (size_t i = 0; i < t.size(); ++i) {
            t[i] = double(i) / 32;
            u[i] = std::sin(8 * std::acos(-1.0) * t[i]);
        }
        u[10] = NAN;
        const auto x = *ws.doc.ajoute_experimentale(u"t"_s, u"s"_s, t);
        const auto y = *ws.doc.ajoute_experimentale(u"U"_s, u"V"_s, u);
        ws.ajoute_tableur({x, y});
        st.reinitialise(std::move(ws));
        dialogs::spectre(win);
        auto *dialogue = ouvre_dialogue(win);
        QVERIFY(dialogue);
        QCheckBox *interpolation = nullptr;
        for (auto *option : dialogue->findChildren<QCheckBox *>())
            if (option->text().contains(u"échantillonner"_s)) interpolation = option;
        QVERIFY(interpolation && interpolation->isVisible());
        bool ligne = false;
        for (auto *l : dialogue->findChildren<QLabel *>())
            ligne |= l->isVisible() && l->text().contains(u"ligne 11"_s);
        QVERIFY(ligne);
        interpolation->setChecked(true);
        for (auto *b : dialogue->findChildren<QPushButton *>())
            if (b->isDefault()) b->click();
        QCOMPARE(st.ws.graphs.size(), 1U);
        const auto &c = st.ws.graphs.front().plots.front().curves.front();
        const auto &amplitude = st.ws.doc.valeurs(c.y);
        const auto maximum = std::max_element(amplitude.begin(), amplitude.end());
        QCOMPARE(st.ws.doc.valeurs(c.x).at(maximum - amplitude.begin()), 4.0);
    }

    void enregistre_cellule_en_cours()
    {
        QTemporaryDir dossier;
        PosteExpert store;
        const QId tension = *store.ws.doc.ajoute_experimentale(u"U"_s, u"V"_s, {1.0});
        const ViewId table = store.ws.ajoute_tableur({tension});
        MainWindow win(store);
        win.show();
        win.ouvre_onglet({TabKey::Table, table});
        stabilise();
        auto *grille = premiere_grille(win);
        QVERIFY(grille);
        const QString chemin = dossier.filePath(u"mesures.phy"_s);
        QVERIFY(win.enregistre(false, chemin));
        const auto cellule = grille->model()->index(0, 0);
        grille->setCurrentIndex(cellule);
        grille->edit(cellule);
        auto *saisie = grille->viewport()->findChild<QLineEdit *>();
        QVERIFY(saisie);
        saisie->selectAll();
        QTest::keyClicks(saisie, "4,7");
        QTest::keyClick(saisie, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(store.ws.doc.valeurs(tension).front(), 4.7);
        QFile fichier(chemin);
        QVERIFY(fichier.open(QIODevice::ReadOnly));
        const auto contenu = fichier.readAll();
        const auto relu = phy::charge(contenu, nullptr);
        QVERIFY(relu);
        QCOMPARE(relu->seance.doc.valeurs(tension).front(), 4.7);
        fichier.close();

        grille->edit(cellule);
        saisie = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        QVERIFY(saisie);
        saisie->selectAll();
        QTest::keyClicks(saisie, "illisible");
        QTest::keyClick(saisie, Qt::Key_Return);
        QVERIFY(saisie->isVisible());
        QVERIFY(!win.enregistre(false));
        QVERIFY(fichier.open(QIODevice::ReadOnly));
        QCOMPARE(fichier.readAll(), contenu);
        fichier.close();
        QTest::keyClick(saisie, Qt::Key_Escape);

        // Une séance illisible ne doit ni remplacer le travail ni détourner le
        // prochain Ctrl+S vers le fichier endommagé.
        QFile endommage(dossier.filePath(u"tronque.phy"_s));
        QVERIFY(endommage.open(QIODevice::WriteOnly));
        endommage.write("{");
        endommage.close();
        QVERIFY(!win.ouvre_chemin(endommage.fileName()));
        QCOMPARE(store.ws.doc.valeurs(tension).front(), 4.7);
        QVERIFY(win.enregistre(false));
        QVERIFY(endommage.open(QIODevice::ReadOnly));
        QCOMPARE(endommage.readAll(), QByteArray("{"));
    }

    void dependances_script_lie()
    {
        Seance ws;
        const auto u = *ws.doc.ajoute_experimentale(u"U"_s, u"V"_s, {1, 2});
        const auto r = *ws.doc.ajoute_parametre(u"R"_s, u"Ω"_s, 47);
        const auto script = ws.ajoute_script(u"import mevreg as mr\nprint(mr.unite('U'), mr.parametres())"_s);
        const auto avant = empreinte_entrees(ws, script);
        ws.doc.definit_unite(u, u"mV"_s);
        const auto apres = empreinte_entrees(ws, script);
        QVERIFY(avant != apres);
        ws.doc.definit_valeur_parametre(r, 100);
        QVERIFY(apres != empreinte_entrees(ws, script));
    }

    void lignes_liees_entre_tableurs()
    {
        Store st;
        MainWindow win(st);
        Seance ws;
        const QId t = *ws.doc.ajoute_experimentale(u"t"_s, {}, {2, 1, 3});
        const QId u = *ws.doc.ajoute_experimentale(u"U"_s, {}, {20, 10, 30});
        const QId temp = *ws.doc.ajoute_experimentale(u"T"_s, {}, {200, 100, 300});
        const QId libre = *ws.doc.ajoute_experimentale(u"B"_s, {}, {3, 2, 1});
        const ViewId table = ws.ajoute_tableur({t, u});
        ws.ajoute_tableur({t, temp});
        ws.ajoute_tableur({libre});
        st.reinitialise(std::move(ws));
        win.ouvre_onglet({TabKey::Table, table});
        auto *grille = win.centralWidget()->findChild<TableGrid *>();
        QVERIFY(grille);
        grille->trie_par(0);
        QCOMPARE(st.ws.doc.valeurs(temp), (std::vector<double>{100, 200, 300}));
        QCOMPARE(st.ws.doc.valeurs(libre), (std::vector<double>{3, 2, 1}));
        st.valide_maintenant();
        grille->setCurrentIndex(grille->model()->index(1, 0));
        grille->insere_ligne();
        QVERIFY(std::isnan(st.ws.doc.valeurs(temp)[1]));
        st.defait();
        st.defait();
        QCOMPARE(st.ws.doc.valeurs(temp), (std::vector<double>{200, 100, 300}));
    }

    void ajustement_apres_annulation()
    {
        Store store;
        Seance ws;
        const QId x = *ws.doc.ajoute_experimentale(u"x"_s, {}, {0, 1, 2, 3});
        const QId y = *ws.doc.ajoute_experimentale(u"y"_s, {}, {1, 3, 5, 7});
        const ViewId t = ws.ajoute_tableur({x, y});
        const ViewId g = ws.ajoute_graphe();
        auto &trace = ws.graphe_modifiable(g)->plots.front();
        trace.ajoute_courbe(x, y);
        trace.fits.push_back(FitSpec::fabrique({fit::ModelKind::Affine}, x, y, PALETTE[0]));
        store.reinitialise(std::move(ws));
        auto coefficients = [&] { return store.ws.graphe(g)->plots.front().fits.front().result->params; };
        const auto avant = coefficients();
        TableModel table(store, t);
        QVERIFY(table.setData(table.index(0, 1), u"9"_s, Qt::EditRole));
        const auto apres = coefficients();
        QVERIFY(avant != apres);
        store.defait();
        QCOMPARE(coefficients(), avant);
        store.retablit();
        QCOMPARE(coefficients(), apres);
    }

    void espace_travail_vide()
    {
        PosteExpert store;
        MainWindow win(store);
        // taille normale par défaut, jamais agrandie d'office
        QCOMPARE(win.size(), DEFAULT_WINDOW);
        QVERIFY(!win.isMaximized()); // avant que le gestionnaire de fenêtres ne lui attribue sa taille
        win.show();
        stabilise();
        QVERIFY(win.onglets_ouverts().isEmpty());
        QVERIFY(win.centralWidget()->isVisible());
        QCOMPARE(win.windowTitle(), u"MeVReg — "_s + about::SUBTITLE);
        cliche(&win, u"accueil"_s);
        // une première fenêtre efface l'invitation
        win.ouvre_onglet(TabKey{TabKey::Quantities});
        stabilise();
        QCOMPARE(win.onglets_ouverts().size(), 1);
        QVERIFY(!win.centralWidget()->isVisible());
        // sans grandeur, les indications restent en haut de la fenêtre, sous ses boutons
        Dock *q = fenetre_de(win, TabKey{TabKey::Quantities});
        QVERIFY(q);
        for (QLabel *l : q->widget()->findChildren<QLabel *>())
            if (l->isVisible())
                QVERIFY2(l->geometry().bottom() < q->widget()->height() / 3, qPrintable(l->text()));
        cliche(q, u"grandeurs-vide"_s);
        win.ferme_onglet(TabKey{TabKey::Quantities});
        stabilise();
        QVERIFY(win.onglets_ouverts().isEmpty());
        QVERIFY(win.centralWidget()->isVisible());
    }

    void page_accueil()
    {
        // épurée : logo, nom, sous-titre, deux boutons de même taille, une ligne discrète ; elle
        // tient sans chevauchement dans la taille minimale de la fenêtre, en clair comme en sombre
        for (const bool dark : {false, true}) {
            theme::choisit(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            PosteExpert store;
            MainWindow win(store);
            win.resize(MIN_WINDOW);
            win.show();
            stabilise();
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
            cliche(&win, dark ? u"accueil-minimale-sombre"_s : u"accueil-minimale"_s);
        }
        theme::choisit(Qt::ColorScheme::Light);
    }

    void barre_outils_et_menus()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        stabilise();
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
        stabilise();
        win.change_agrandissement(2.0);
        stabilise();
        QCOMPARE(win.agrandissement(), 2.0);
        checkHeight();
        QVERIFY(themeButton->iconSize().height() > 20);
        win.change_agrandissement(1.0);
        stabilise();
        // le coin tient dans la largeur minimale de la fenêtre
        win.resize(MIN_WINDOW);
        stabilise();
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
        stabilise();
        help::openUrl = [](const QUrl &) { return true; };
        QVERIFY(opened.isLocalFile());
        QCOMPARE(QFileInfo(opened.toLocalFile()).fileName(), help::NOTICE);
        for (const QString &name : help::fichiers())
            QVERIFY2(QFileInfo(QFileInfo(opened.toLocalFile()).dir().filePath(name)).size() > 0, qPrintable(name));
    }

    void pictogrammes_des_menus()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1280, 800);
        win.show();
        auto verifie = [](auto &&visite, QMenu *menu) -> void {
            Q_EMIT menu->aboutToShow();
            for (QAction *a : menu->actions()) {
                if (a->isSeparator()) continue;
                QVERIFY2(!a->icon().isNull(), qPrintable(menu->title() + u" › "_s + a->text()));
                if (QMenu *sub = a->menu()) visite(visite, sub);
            }
        };
        for (bool rempli : {false, true}) {
            if (rempli) {
                prepare_graphe(win, true);
                win.nouveau_script();
                store.prefs.recent << u"TP électricité.phy"_s;
            }
            for (bool simple : {true, false}) {
                win.choisit_mode(simple);
                stabilise();
                for (QAction *a : win.menuBar()->actions()) {
                    if (!a->isVisible()) continue;
                    QVERIFY(!a->icon().isNull());
                    const int texte = win.menuBar()->fontMetrics().size(Qt::TextShowMnemonic, a->text()).width();
                    QVERIFY2(win.menuBar()->actionGeometry(a).width() >= texte + 16, qPrintable(a->text()));
                    verifie(verifie, a->menu());
                }
                // L’ouverture d’un menu dynamique peut recréer les boutons d’un autre panneau.
                QList<QPointer<QMenu>> deroulants;
                for (auto *bouton : win.findChildren<QToolButton *>())
                    if (QMenu *menu = bouton->menu()) deroulants << QPointer<QMenu>(menu);
                for (const auto &menu : deroulants)
                    if (menu) verifie(verifie, menu.data());
            }
            if (rempli) {
                QMenu menu;
                QuantitiesPanel::remplit_menu(&menu, win, store.ws.doc.quantities.front().id, &win);
                verifie(verifie, &menu);
            }
        }
    }

    /// Pages de la notice : liens internes et images présents, version remplacée, libellés des
    /// boutons et des menus cités tels qu'ils sont dans le logiciel.
    void pages_notice()
    {
        QString error;
        const QString page = help::ecrit(help::NOTICE, &error);
        QVERIFY2(!page.isEmpty(), qPrintable(error));
        const QDir dir = QFileInfo(page).dir();
        QCOMPARE(QFileInfo(dir.absolutePath()).dir().absolutePath(), QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        QMap<QString, QString> pages;
        QMap<QString, QSet<QString>> anchors;
        for (const QString &name : {help::NOTICE, help::PYTHON}) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString html = QString::fromUtf8(f.readAll());
            pages.insert(name, html);
            QVERIFY(!html.contains(u"{{version}}"_s));
            QVERIFY(html.contains(QString::fromLatin1(MEVREG_VERSION)));
            static const QRegularExpression ids(u"id=\"([^\"]+)\""_s);
            for (auto m = ids.globalMatch(html); m.hasNext();) {
                const QString id = m.next().captured(1);
                QVERIFY2(!anchors[name].contains(id), qPrintable(name + u"#"_s + id));
                anchors[name].insert(id);
            }
        }
        static const QRegularExpression refs(u"(?:href|src)=\"([^\"#:]*)(?:#([^\"]*))?\""_s);
        for (auto it = pages.cbegin(); it != pages.cend(); ++it) {
            for (auto m = refs.globalMatch(it.value()); m.hasNext();) {
                const auto r = m.next();
                const QString target = r.captured(1).isEmpty() ? it.key() : r.captured(1);
                QVERIFY2(QFileInfo::exists(dir.filePath(target)), qPrintable(target));
                if (!r.captured(2).isEmpty())
                    QVERIFY2(anchors.value(target).contains(r.captured(2)), qPrintable(target + u"#"_s + r.captured(2)));
            }
        }
        for (const QString &name : help::fichiers()) {
            if (name.endsWith(u".png"_s)) {
                QVERIFY(!QImage(dir.filePath(name)).isNull());
                QVERIFY(pages.value(help::NOTICE).contains(u"src=\""_s + name + u'"'));
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

    void exercices_notice()
    {
        QString erreur;
        const QString page = help::ecrit(help::PYTHON, &erreur);
        QVERIFY2(!page.isEmpty(), qPrintable(erreur));
        QFile data(QFileInfo(page).dir().filePath(u"resistance.csv"_s));
        QVERIFY(data.open(QIODevice::ReadOnly));
        const auto mesures = csv::analyse(QString::fromUtf8(data.readAll()), &erreur);
        QVERIFY2(mesures && mesures->columns.size() == 2, qPrintable(erreur));
        QCOMPARE(mesures->columns[0].unit, u"A"_s);
        QCOMPARE(mesures->columns[1].unit, u"V"_s);
        QCOMPARE(mesures->columns[0].values, (std::vector<double>{0, 0.1, 0.2, 0.3}));
        QCOMPARE(mesures->columns[1].values, (std::vector<double>{0, 4.7, 9.4, 14.1}));
        QFile ref(page);
        QVERIFY(ref.open(QIODevice::ReadOnly));
        const QString html = QString::fromUtf8(ref.readAll());
        QMap<QString, QString> attendus{
            {u"ohm"_s, u"R = 47.00 Ω pour 4 points"_s},
            {u"repetitions"_s, u"4 mesures, moyenne = 10.00 cm\ns = 0.163 cm, u = 0.082 cm"_s},
            {u"son"_s, u"440 Hz, amplitude = 1.00"_s}
        };
        const QRegularExpression blocs(u"<code data-essai=\"([^\"]+)\">(.*?)</code>"_s, QRegularExpression::DotMatchesEverythingOption);
        for (auto m = blocs.globalMatch(html); m.hasNext();) {
            const auto r = m.next();
            QVERIFY(attendus.contains(r.captured(1)));
            const auto res = python::Engine::instance().lance_synchrone(r.captured(2), {});
            QVERIFY2(!res.error, res.error ? qPrintable(res.error->message) : "");
            QCOMPARE(res.stdoutText.trimmed(), attendus.take(r.captured(1)));
        }
        QVERIFY(attendus.isEmpty());
    }

    void bouton_theme()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        prepare_graphe(win, true);
        stabilise();
        auto *button = win.findChild<QToolButton *>(u"themeButton"_s);
        QVERIFY(button);
        QVERIFY(!theme::est_sombre());
        QCOMPARE(button->toolTip(), u"Thème clair. Cliquer pour passer au thème sombre."_s);
        // un clic : thème sombre, palette de la suite, réglage noté
        button->click();
        stabilise(120);
        QVERIFY(theme::est_sombre());
        QCOMPARE(store.prefs.theme, Qt::ColorScheme::Dark);
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(32, 33, 36));
        QCOMPARE(QApplication::palette().color(QPalette::Base), QColor(22, 23, 25));
        QCOMPARE(button->toolTip(), u"Thème sombre. Cliquer pour passer au thème clair."_s);
        // les fenêtres de réglages ont le fond plus sombre des panneaux ; leur palette locale garde
        // le bord des champs visible
        Dock *q = fenetre_de(win, TabKey{TabKey::Quantities});
        QVERIFY(q);
        const auto background = [q] { return q->grab().toImage().pixelColor(q->width() / 2, 2).rgb(); };
        QCOMPARE(background(), QColor(32, 33, 36).darker(125).rgb());
        QCOMPARE(q->palette().color(QPalette::Window), QColor(78, 80, 86));
        cliche(&win, u"theme-sombre"_s);
        // un second clic : retour au thème clair
        button->click();
        stabilise(120);
        QVERIFY(!theme::est_sombre());
        QCOMPARE(store.prefs.theme, Qt::ColorScheme::Light);
        QCOMPARE(background(), QApplication::palette().color(QPalette::Window).darker(104).rgb());
        cliche(&win, u"theme-clair"_s);
    }

    void barre_etat()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        stabilise();
        QStatusBar *bar = win.statusBar();
        auto *message = bar->findChild<QLabel *>(u"statusMessage"_s);
        auto *document = bar->findChild<QLabel *>(u"documentStatus"_s);
        QVERIFY(message && document);
        const int line = message->fontMetrics().height();
        // une ligne de haut
        QVERIFY2(bar->height() < 2 * line + 8, qPrintable(QString::number(bar->height())));
        QCOMPARE(document->text(), u"Sans titre · 0 grandeur"_s);
        // message typé : couleur de sa nature, toujours une ligne
        prepare_graphe(win, true);
        stabilise();
        store.message(u"Graphe exporté : essai.png"_s);
        stabilise();
        QCOMPARE(message->palette().color(QPalette::WindowText), theme::couleur_reussite());
        QVERIFY(bar->height() < 2 * line + 8);
        QVERIFY(document->text().contains(u"4 grandeurs"_s));
        cliche(bar, u"barre-etat"_s);
        store.message(u"Export impossible : disque plein"_s, icons::Status::Error);
        stabilise();
        QCOMPARE(message->palette().color(QPalette::WindowText), theme::couleur_erreur());
        cliche(bar, u"barre-etat-erreur"_s);
        // un message trop long passe à la ligne, sans être coupé
        store.message(QStringList(30, u"mesure"_s).join(u' '), icons::Status::Warning);
        win.resize(MIN_WINDOW);
        stabilise(120);
        QVERIFY(bar->height() >= 2 * line);
        QVERIFY(message->height() >= message->heightForWidth(message->width()));
        // et la barre revient à une ligne
        store.message(u"Prêt"_s, icons::Status::Info);
        stabilise(120);
        QVERIFY(bar->height() < 2 * line + 8);
    }

    void ecran_demarrage()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        stabilise();
        auto *splash = new Splash(&win);
        stabilise();
        QVERIFY(splash->isVisible());
        QCOMPARE(splash->geometry(), win.rect());
        cliche(&win, u"demarrage"_s);
        // un clic l'efface
        QPointer<Splash> alive(splash);
        QTest::mouseClick(splash, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!alive, 3000);
    }

    void theme_sombre()
    {
        theme::choisit(Qt::ColorScheme::Dark);
        PosteExpert store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        prepare_graphe(win, true);
        stabilise(120);
        QVERIFY(theme::est_sombre());
        cliche(&win, u"graphe-sombre"_s);
        // panneaux en sombre : Calculs, Modélisation, Statistiques
        if (Dock *c = fenetre_de(win, TabKey{TabKey::Calculations}))
            c->raise();
        win.ouvre_modelisation(store.ws.graphs.front().id, 0, 0);
        stabilise(120);
        cliche(&win, u"panneaux-sombre"_s);
        theme::choisit(Qt::ColorScheme::Light);
    }

    void enregistre_et_rouvre()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"tp.phy"_s);
        QList<TabKey> tabs;
        Seance saved;
        {
            PosteExpert store;
            MainWindow win(store);
            win.resize(1300, 800);
            win.show();
            prepare_graphe(win);
            stabilise();
            win.ouvre_onglet(TabKey{TabKey::Statistics});
            stabilise();
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(phy::enregistre(store.ws, QJsonValue()));
            f.close();
            QVERIFY(win.ouvre_chemin(path));
            stabilise();
            win.ouvre_onglet(TabKey{TabKey::Statistics});
            stabilise();
            // enregistrement sans boîte de dialogue : le chemin est déjà connu
            QVERIFY(win.enregistre(false));
            QVERIFY(!store.est_modifie());
            tabs = win.onglets_ouverts();
            saved = store.ws;
        }
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        QVERIFY(win.ouvre_chemin(path));
        stabilise();
        QCOMPARE(store.ws.doc.quantities.size(), saved.doc.quantities.size());
        QCOMPARE(store.ws.graphs.size(), saved.graphs.size());
        QVERIFY(store.ws.graphs.front().plots == saved.graphs.front().plots);
        // la disposition des fenêtres est retrouvée
        QCOMPARE(win.onglets_ouverts().size(), tabs.size());
        for (const TabKey &k : tabs)
            QVERIFY2(win.est_ouvert(k), qPrintable(k.texte()));
    }

    /// L'accord permet une exécution immédiate. À l'ouverture suivante,
    /// le script lié reprend le suivi à partir des mesures relues.
    void scripts_recus_demandent()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"recu.phy"_s);
        {
            Seance ws;
            const ViewId sid = ws.ajoute_script(u"import mevreg as mr\nmr.creer(\"z\", mr.valeurs(\"t\") * 2)\n"_s);
            ws.script_modifiable(sid)->linked = true;
            ws.doc.ajoute_experimentale(u"t"_s, u"s"_s, {1, 2});
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(phy::enregistre(ws, QJsonValue()));
        }
        PosteExpert store;
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
        QVERIFY(win.ouvre_chemin(path));
        stabilise(40);
        auto *box = win.findChild<QMessageBox *>(u"confirmScripts"_s);
        QVERIFY(box && box->isVisible());
        QCOMPARE(box->defaultButton()->objectName(), u"norun"_s);
        cliche(box, u"scripts-confirmation"_s);
        clickButton(box, u"norun"_s);
        stabilise(40);
        store.edite([](Seance &ws) { ws.doc.definit_valeurs(ws.doc.par_nom(u"t"_s)->id, {3.0}); });
        stabilise(300);
        QVERIFY(!store.ws.doc.par_nom(u"z"_s));
        // accord : le script se lance, et ce document ne pose plus la question
        QVERIFY(win.ouvre_chemin(path));
        stabilise(40);
        box = win.findChild<QMessageBox *>(u"confirmScripts"_s);
        QVERIFY(box && box->isVisible());
        clickButton(box, u"run"_s);
        QTRY_VERIFY_WITH_TIMEOUT(store.ws.doc.par_nom(u"z"_s), 10000);
        stabilise(40);
        QVERIFY(win.ouvre_chemin(path));
        stabilise(40);
        QVERIFY(!win.findChild<QMessageBox *>(u"confirmScripts"_s));
        QVERIFY(!store.ws.doc.par_nom(u"z"_s)); // ce fichier ne contient que t
        store.lance_scripts_lies();
        QVERIFY(!store.est_en_cours(store.ws.scripts.front().id));
        QVERIFY(!store.est_modifie());
        store.edite([](Seance &ws) { ws.doc.definit_valeur(ws.doc.par_nom(u"t"_s)->id, 0, 3.0); });
        QTRY_VERIFY_WITH_TIMEOUT(store.ws.doc.par_nom(u"z"_s), 10000);
        QCOMPARE(store.ws.doc.par_nom(u"z"_s)->values, (std::vector<double>{6, 4}));
    }

    void reouverture_conserve_correction_python()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"etalonnage.phy"_s);
        PosteExpert store;
        MainWindow win(store);
        win.show();
        Seance ws;
        const QId u = *ws.doc.ajoute_experimentale(u"U"_s, u"V"_s, {1, 2});
        ws.ajoute_tableur({u});
        const ViewId correction = ws.ajoute_script(u"import mevreg as mr\nmr.modifier('U', valeurs=2*mr.valeurs('U'))"_s);
        const ViewId dessin = ws.ajoute_script(u"import fenetre as fe\nd = fe.Dessin('Montage', 0, 1, 0, 1)\nd.montrer()"_s);
        store.reinitialise(std::move(ws));
        store.lance_script(correction);
        QTRY_VERIFY_WITH_TIMEOUT(!store.est_en_cours(correction), 10000);
        QCOMPARE(store.ws.doc.valeurs(u), (std::vector<double>{2, 4}));
        store.lance_script(dessin);
        QTRY_VERIFY_WITH_TIMEOUT(store.drawings.count(u"Montage"_s), 10000);
        QVERIFY(win.enregistre(false, path));
        for (int i = 0; i < 3; ++i) {
            QVERIFY(win.ouvre_chemin(path));
            stabilise(100);
            QCOMPARE(store.ws.doc.valeurs(u), (std::vector<double>{2, 4}));
            QVERIFY(!store.est_modifie());
            QVERIFY(store.runs.empty());
            QVERIFY(store.drawings.empty());
            QVERIFY(win.enregistre(false));
        }
        Dock *fenetre = fenetre_de(win, {TabKey::Drawing, 0, u"Montage"_s});
        QVERIFY(fenetre);
        auto *bouton = fenetre->findChild<QToolButton *>(u"regenererDessin"_s);
        QVERIFY(bouton && !bouton->isHidden());
        bouton->click();
        QTRY_VERIFY_WITH_TIMEOUT(store.drawings.count(u"Montage"_s), 10000);
        QVERIFY(!store.runs.count(correction));
        QCOMPARE(store.ws.doc.valeurs(u), (std::vector<double>{2, 4}));
        store.lance_script(correction);
        QTRY_VERIFY_WITH_TIMEOUT(!store.est_en_cours(correction), 10000);
        QCOMPARE(store.ws.doc.valeurs(u), (std::vector<double>{4, 8}));

        // Une ancienne séance garde le nom de la fenêtre, mais ne dit pas
        // lequel des deux scripts la dessinait. Le choix doit rester explicite.
        store.ws.script_modifiable(dessin)->dessins.clear();
        const QString ancien = dir.filePath(u"ancienne-seance.phy"_s);
        QVERIFY(win.enregistre(false, ancien));
        QVERIFY(win.ouvre_chemin(ancien));
        stabilise(80);
        fenetre = fenetre_de(win, {TabKey::Drawing, 0, u"Montage"_s});
        QVERIFY(fenetre);
        bouton = fenetre->findChild<QToolButton *>(u"regenererDessin"_s);
        QVERIFY(bouton);
        bouton->click();
        auto *choix = bouton->findChild<QMenu *>();
        QVERIFY(choix && choix->isVisible());
        QVERIFY(store.runs.empty());
        QAction *action = nullptr;
        for (QAction *a : choix->actions())
            if (a->text() == store.ws.script(dessin)->name)
                action = a;
        QVERIFY(action);
        action->trigger();
        choix->close();
        QTRY_VERIFY_WITH_TIMEOUT(store.drawings.count(u"Montage"_s), 10000);
        QCOMPARE(store.ws.doc.valeurs(u), (std::vector<double>{4, 8}));
    }

    /// Annuler et rétablir par le menu Édition (ils ne sont plus dans la barre d'outils) : grisés quand
    /// il n'y a rien à annuler ou à rétablir, raccourcis Ctrl+Z et Ctrl+Y.
    void annuler_retablir_menu_edition()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        prepare_graphe(win, true);
        stabilise();
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
        store.edite([](Seance &ws) { ws.doc.ajoute_experimentale(u"m"_s, u"kg"_s, {1, 2, 3}); });
        stabilise();
        QVERIFY(undo->isEnabled());
        undo->trigger();
        stabilise();
        QCOMPARE(store.ws.doc.quantities.size(), n);
        QVERIFY(redo->isEnabled());
        redo->trigger();
        stabilise();
        QCOMPARE(store.ws.doc.quantities.size(), n + 1);
        QVERIFY(!redo->isEnabled());
        // le raccourci Ctrl+Z, fenêtre active
        win.activateWindow();
        QTest::keyClick(&win, Qt::Key_Z, Qt::ControlModifier);
        stabilise();
        QCOMPARE(store.ws.doc.quantities.size(), n);
    }

    void annuler_retablir()
    {
        PosteExpert store;
        MainWindow win(store);
        win.show();
        prepare_graphe(win, true);
        stabilise();
        const size_t n = store.ws.doc.quantities.size();
        store.edite([](Seance &ws) { ws.doc.ajoute_experimentale(u"m"_s, u"kg"_s, {1, 2, 3}); });
        stabilise();
        QCOMPARE(store.ws.doc.quantities.size(), n + 1);
        store.defait();
        stabilise();
        QCOMPARE(store.ws.doc.quantities.size(), n);
        store.retablit();
        stabilise();
        QCOMPARE(store.ws.doc.quantities.size(), n + 1);
        // supprimer un graphe ferme sa fenêtre ; l'annulation le fait revenir
        const ViewId gid = store.ws.graphs.front().id;
        QVERIFY(win.est_ouvert(TabKey{TabKey::Graph, gid}));
        store.edite([](Seance &ws) { ws.graphs.clear(); });
        stabilise();
        QVERIFY(!win.est_ouvert(TabKey{TabKey::Graph, gid}));
        store.defait();
        stabilise();
        QVERIFY(store.ws.graphe(gid));
    }

    void reglages_graphe()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1500, 900);
        win.show();
        prepare_graphe(win, true);
        stabilise();
        const ViewId gid = store.ws.graphs.front().id;
        GraphPanel *gp = win.panneau_graphe(gid);
        QVERIFY(gp);
        // une droite de référence, pour voir la section Annotations remplie
        store.edite([&](Seance &ws) {
            Annotation a;
            a.kind = Annotation::HLine;
            a.y = 5.0;
            ws.graphe_modifiable(gid)->plots[0].annotations.push_back(a);
        });
        const std::vector<std::pair<SettingsSection, QString>> sections = {
            {SettingsSection::Axes, u"axes"_s}, {SettingsSection::Title, u"titre"_s}, {SettingsSection::Models, u"modeles"_s},
            {SettingsSection::Annotations, u"annotations"_s}, {SettingsSection::Curves, u"courbes"_s}};
        for (const auto &[s, name] : sections) {
            gp->ouvre_reglages(s);
            stabilise(100);
            GraphSettings *gs = gp->findChild<GraphSettings *>();
            QVERIFY(gs && gs->isVisible());
            cliche(gs, u"section-"_s + name);
        }
        cliche(&win, u"reglages-courbes"_s);
        // changer le type de barres d'incertitude reconstruit la page sans rien perdre
        store.edite([&](Seance &ws) { ws.graphe_modifiable(gid)->plots[0].curves[0].errors = ErrorBars::Rectangles; });
        stabilise(100);
        store.edite([&](Seance &ws) { ws.graphe_modifiable(gid)->plots[0].curves[0].capSize = 0.0; });
        stabilise(100);
        QCOMPARE(store.ws.graphe(gid)->plots[0].curves[0].errors, ErrorBars::Rectangles);
        cliche(&win, u"reglages-rectangles"_s);
        gp->ouvre_reglages(SettingsSection::Axes, 1);
        stabilise(100);
        cliche(&win, u"reglages-axes"_s);
    }

    void reticule_et_zoom()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        prepare_graphe(win, true);
        stabilise();
        const ViewId gid = store.ws.graphs.front().id;
        GraphCanvas *c = win.panneau_graphe(gid)->canevas();
        stabilise(100);
        QVERIFY(!c->dispositions().empty());
        const PlotLayout l = c->dispositions().front();
        // réticule accroché au 6e point : sa valeur et son incertitude
        const Grandeur *iq = store.ws.doc.par_nom(u"I"_s), *uq = store.ws.doc.par_nom(u"U"_s);
        const QPointF p(l.x.vers_px(iq->values[5]), l.y.vers_px(uq->values[5]));
        QTest::mouseMove(c, (p + QPointF(3, -2)).toPoint());
        stabilise(60);
        cliche(win.panneau_graphe(gid), u"reticule"_s);
        // zoom par un rectangle tracé à la souris
        c->choisit_outil(Tool::Zoom);
        const QPoint a = l.area.topLeft().toPoint() + QPoint(40, 40), b = l.area.center().toPoint();
        QTest::mousePress(c, Qt::LeftButton, {}, a);
        QTest::mouseMove(c, (a + b) / 2);
        QTest::mouseMove(c, b);
        QTest::mouseRelease(c, Qt::LeftButton, {}, b);
        stabilise(60);
        const Plot &pl = store.ws.graphe(gid)->plots[0];
        QVERIFY(!pl.x.autoScale);
        QVERIFY(pl.x.max < l.x.max);
        cliche(win.panneau_graphe(gid), u"zoom"_s);
        // retour à l'échelle automatique
        store.defait();
        stabilise(60);
        QVERIFY(store.ws.graphe(gid)->plots[0].x.autoScale);
    }

    void modelisation()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1500, 900);
        win.show();
        prepare_graphe(win);
        stabilise();
        const ViewId gid = store.ws.graphs.front().id;
        win.ouvre_modelisation(gid, 0, 0);
        stabilise(100);
        QVERIFY(ModelingPanel::instance());
        QVERIFY(win.est_ouvert(TabKey{TabKey::Modeling}));
        cliche(&win, u"modelisation"_s);
        // nouveau modèle : la grille de vignettes attend un choix
        win.ouvre_modelisation(gid, 0, -1);
        stabilise(100);
        cliche(&win, u"modelisation-nouveau"_s);
        // plage tracée sur le graphe : appliquée au modèle affiché
        win.ouvre_modelisation(gid, 0, 0);
        stabilise(60);
        QVERIFY(ModelingPanel::instance()->applique_plage(gid, 0, 0.1, 0.5));
        stabilise(60);
        const auto &f = store.ws.graphe(gid)->plots[0].fits[0];
        QVERIFY(f.range.has_value());
        QVERIFY(f.result.has_value());
        QVERIFY(f.result->n < 16);
        auto *voir = ModelingPanel::instance()->findChild<QCheckBox *>(u"voirResidus"_s);
        QVERIFY(voir && !voir->isChecked());
        voir->click();
        QTRY_VERIFY(ModelingPanel::instance()->findChild<QWidget *>(u"grapheResidus"_s));
        QVERIFY(ModelingPanel::instance()->findChild<QWidget *>(u"grapheResidus"_s)->isVisibleTo(&win));
    }


    void recuperation_de_seances()
    {
        QTemporaryDir dossier;
        QVERIFY(dossier.isValid());
        const QString titre = u"Essai%1"_s.arg(QFileInfo(dossier.path()).fileName().right(6));
        const QDir secours(QFileInfo(fichier_reglages()).dir().filePath(u"recuperation-mevreg"_s));
        const auto copies = [&] { return secours.entryList({titre + u"*.phy"_s}, QDir::Files); };
        {
            PosteExpert store1, store2;
            MainWindow seance1(store1), seance2(store2);
            prepare_graphe(seance1);
            prepare_graphe(seance2, true);
            store1.edite([&](Seance &ws) { ws.meta.title = titre + u"A"_s; });
            // Deuxième séance enregistrée sur un support qui devient inaccessible.
            const QString support = dossier.filePath(u"support"_s);
            QVERIFY(QDir().mkpath(support));
            QVERIFY(seance2.enregistre(false, QDir(support).filePath(titre + u"B.phy"_s)));
            QVERIFY(QDir(support).rename(support, dossier.filePath(u"retire"_s)));
            store2.edite([&](Seance &ws) { ws.meta.title = titre + u"B"_s; });
            QTRY_COMPARE_WITH_TIMEOUT(copies().size(), 2, 6000);
        } // arrêt sans closeEvent : les deux fichiers de secours restent récupérables
        PosteExpert store;
        MainWindow win(store);
        for (const QString &suffixe : {u"B"_s, u"A"_s}) {
            bool trouve = false;
            QTimer::singleShot(0, &win, [&] {
                auto *dialogue = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                if (!dialogue)
                    return;
                for (const QString &ligne : dialogue->comboBoxItems()) {
                    if (ligne.startsWith(titre + suffixe)) {
                        dialogue->setTextValue(ligne);
                        trouve = true;
                        break;
                    }
                }
                if (trouve)
                    dialogue->accept();
                else
                    dialogue->reject();
            });
            win.propose_recuperation(false);
            QVERIFY(trouve);
            QCOMPARE(store.ws.meta.title, titre + suffixe);
            QVERIFY(store.est_modifie());
            QVERIFY(win.chemin_actuel().isEmpty());
            QVERIFY(!store.ws.graphs.empty());
            QVERIFY(win.enregistre(false, dossier.filePath(suffixe + u".phy"_s)));
        }
        QVERIFY(copies().isEmpty());
    }

    void statistiques()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        prepare_graphe(win);
        stabilise();
        win.ouvre_statistiques(store.ws.doc.par_nom(u"z"_s)->id);
        stabilise(100);
        auto *stats = win.findChild<StatsPanel *>();
        QVERIFY(stats);
        QCOMPARE(*stats->grandeur_choisie(), store.ws.doc.par_nom(u"z"_s)->id);
        const auto h = trace_histogramme(store.ws.doc, *stats->grandeur_choisie(), std::nullopt, store.style());
        QVERIFY(h.has_value());
        cliche(&win, u"statistiques"_s);
    }

    void execution_script()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1440, 880);
        win.show();
        prepare_graphe(win, true);
        stabilise();
        win.nouveau_script();
        stabilise();
        const ViewId sid = store.ws.scripts.back().id;
        Dock *d = fenetre_de(win, TabKey{TabKey::Script, sid});
        QVERIFY(d);
        auto *panel = qobject_cast<ScriptPanel *>(d->widget());
        QVERIFY(panel);
        panel->editeur()->setPlainText(u"import numpy as np\nimport matplotlib.pyplot as plt\n"
                                      "P = U * I\ngrandeur(\"P\", P, \"W\")\nprint(\"Pmax =\", P.max())\n"
                                      "plt.plot(I, P, 'r-')\nplt.xlabel('I (A)')\nplt.ylabel('P (W)')\nplt.show()\n"_s);
        QSignalSpy spy(&store, &Store::script_applique);
        panel->transmet_saisie();
        store.lance_script(sid);
        QVERIFY(spy.wait(10000));
        stabilise(100);
        QVERIFY(store.ws.doc.par_nom(u"P"_s));
        const ScriptRun &r = store.runs.at(sid);
        QVERIFY(r.stdoutText.contains(u"Pmax"_s));
        QVERIFY(!r.error);
        // la figure est devenue un graphe du logiciel, ouvert
        QVERIFY(std::any_of(store.ws.graphs.begin(), store.ws.graphs.end(), [&](const GraphView &g) { return g.origin && g.origin->script == sid; }));
        QTemporaryDir dir;
        const auto avant = store.ws.doc.en_json();
        panel->editeur()->setPlainText(u"import mevreg as mr\nmr.modifier('P', valeurs=[999])\n"
                                      "mr.enregistrer(r'%1')\nprint('avant erreur')\n1/0\n"_s.arg(dir.filePath(u"erreur.phy"_s)));
        panel->transmet_saisie();
        store.lance_script(sid);
        QTRY_VERIFY_WITH_TIMEOUT(!store.est_en_cours(sid), 10000);
        QVERIFY(r.error && r.error->line == 5);
        QVERIFY(r.stdoutText.contains(u"avant erreur"));
        QCOMPARE(store.ws.doc.en_json(), avant);
        QVERIFY(!QFileInfo::exists(dir.filePath(u"erreur.phy"_s)));
        d->raise();
        stabilise();
        cliche(&win, u"script"_s);
    }

    /// Formules saisies : virgule décimale ou séparateur d'arguments.
    void virgules_formules()
    {
        QCOMPARE(dialogs::texte_formule(u"0,05*x"_s), u"0.05*x"_s);
        QCOMPARE(dialogs::texte_formule(u"sin(0,5)"_s), u"sin(0.5)"_s);
        QCOMPARE(dialogs::texte_formule(u"min(2,3)"_s), u"min(2,3)"_s);
        QCOMPARE(dialogs::texte_formule(u"pow(2,3)"_s), u"pow(2,3)"_s);
        QCOMPARE(dialogs::texte_formule(u"atan2(1,2) + 1,5"_s), u"atan2(1,2) + 1.5"_s);
        QCOMPARE(dialogs::texte_formule(u"max (sin(0,5),1)"_s), u"max (sin(0.5),1)"_s);
        QCOMPARE(dialogs::texte_formule(u"integ(y, t)"_s), u"integ(y, t)"_s);
        // la ligne « nom = expression » du panneau Calculs aussi
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.nouveau_tableur({});
        stabilise();
        win.ouvre_onglet(TabKey{TabKey::Calculations});
        stabilise();
        Dock *calc = fenetre_de(win, TabKey{TabKey::Calculations});
        auto *panel = calc ? calc->findChild<CalculationsPanel *>() : nullptr;
        QVERIFY(panel);
        QLineEdit *line = nullptr;
        for (QLineEdit *e : panel->findChildren<QLineEdit *>())
            if (e->placeholderText().startsWith(u"Ec ="_s))
                line = e;
        QVERIFY(line);
        line->setText(u"k = 0,5*2"_s);
        QTest::keyClick(line, Qt::Key_Return);
        stabilise();
        const Grandeur *k = store.ws.doc.par_nom(u"k"_s);
        QVERIFY(k && k->formula);
        QCOMPARE(*k->formula, u"0.5*2"_s);
    }

    /// Scripts : ordres de mevreg dans l'ordre du script, scripts liés qui lisent par mr.valeurs,
    /// « Arrêter » sur un script en attente, scripts de l'ancien document oubliés.
    void ordre_et_taches_python()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.nouveau_tableur({});
        stabilise();
        store.edite([](Seance &ws) { ws.doc.ajoute_experimentale(u"x"_s, QString(), {1.0}); });
        stabilise();
        auto run = [&](const QString &code) -> ViewId {
            win.nouveau_script(code);
            stabilise(20);
            const ViewId sid = store.ws.scripts.back().id;
            QSignalSpy spy(&store, &Store::script_applique);
            store.lance_script(sid);
            spy.wait(10000);
            stabilise(40);
            return sid;
        };
        // supprimer puis recréer : la grandeur existe, avec ses nouvelles valeurs
        run(u"import mevreg as mr\nmr.supprimer(\"x\")\nmr.creer(\"x\", [2, 3])\n"_s);
        QVERIFY(store.ws.doc.par_nom(u"x"_s));
        QCOMPARE(store.ws.doc.par_nom(u"x"_s)->values, (std::vector<double>{2.0, 3.0}));
        // créer, renommer, recréer sous le même nom : chaque grandeur garde les valeurs de sa création
        run(u"import mevreg as mr\nmr.creer(\"r\", [1])\nmr.renommer(\"r\", \"a\")\nmr.creer(\"r\", [2])\n"_s);
        QCOMPARE(store.ws.doc.par_nom(u"a"_s)->values, (std::vector<double>{1.0}));
        QCOMPARE(store.ws.doc.par_nom(u"r"_s)->values, (std::vector<double>{2.0}));
        // mr.creer puis plt.plot : la grandeur garde son unité et va dans le tableur des résultats
        const ViewId plotted = run(u"import mevreg as mr\nimport numpy as np\nimport matplotlib.pyplot as plt\n"
                                   "P = np.array([1.0, 4.0])\nmr.creer(\"P\", P, \"W\")\nplt.plot(x, P)\nplt.show()\n"_s);
        QVERIFY(store.ws.doc.par_nom(u"P"_s));
        QCOMPARE(store.ws.doc.par_nom(u"P"_s)->unit, u"W"_s);
        const QString results = u"Résultats de "_s + store.ws.script(plotted)->name;
        const QId pid = store.ws.doc.par_nom(u"P"_s)->id;
        QVERIFY(std::any_of(store.ws.tables.begin(), store.ws.tables.end(), [&](const TableView &t) {
            return t.name == results && std::find(t.columns.begin(), t.columns.end(), pid) != t.columns.end();
        }));
        // grandeur modifiée par le script puis tracée : pas de copie « _py »
        run(u"import matplotlib.pyplot as plt\na = a * 1000\ngrandeur(\"a\", a, \"mm\")\nplt.plot(a, a)\nplt.show()\n"_s);
        QCOMPARE(store.ws.doc.par_nom(u"a"_s)->values, (std::vector<double>{1000.0}));
        QVERIFY(!store.ws.doc.par_nom(u"a_py"_s));
        // script lié qui lit x par mr.valeurs : relancé quand x change
        const ViewId linked = run(u"import mevreg as mr\nmr.creer(\"y\", mr.valeurs(\"x\") * 10)\n"_s);
        QCOMPARE(store.ws.doc.par_nom(u"y"_s)->values, (std::vector<double>{20.0, 30.0}));
        store.edite([&](Seance &ws) { ws.script_modifiable(linked)->linked = true; });
        stabilise(100);
        QSignalSpy relaunched(&store, &Store::script_applique);
        store.edite([&](Seance &ws) { ws.doc.definit_valeurs(ws.doc.par_nom(u"x"_s)->id, {5.0}); });
        QVERIFY(relaunched.wait(10000));
        stabilise(40);
        QCOMPARE(store.ws.doc.par_nom(u"y"_s)->values, (std::vector<double>{50.0}));
        store.edite([&](Seance &ws) { ws.script_modifiable(linked)->linked = false; });
        stabilise(40);
        // « Arrêter » sur un script qui attend son tour : il ne tourne pas
        win.nouveau_script(u"import time\ntime.sleep(1.5)\n"_s);
        const ViewId slow = store.ws.scripts.back().id;
        win.nouveau_script(u"import mevreg as mr\nmr.creer(\"jamais\", [1])\n"_s);
        const ViewId queued = store.ws.scripts.back().id;
        stabilise(20);
        store.lance_script(slow);
        store.lance_script(queued);
        store.arrete_script(queued);
        QTRY_VERIFY_WITH_TIMEOUT(!store.est_en_cours(slow) && !store.est_en_cours(queued), 10000);
        stabilise(100);
        QVERIFY(!store.ws.doc.par_nom(u"jamais"_s));
        QVERIFY(store.runs.at(queued).error && store.runs.at(queued).error->interrupted);
        // un script encore en cours quand un autre document s'ouvre : ses résultats sont oubliés
        win.nouveau_script(u"import time\nimport mevreg as mr\ntime.sleep(1)\nmr.creer(\"ancienne\", [42])\n"_s);
        const ViewId old = store.ws.scripts.back().id;
        stabilise(20);
        store.lance_script(old);
        stabilise(100);
        // le nouveau document a un script de même numéro : c'est à lui que l'ancien résultat
        // était appliqué
        Seance fresh;
        while (fresh.scripts.empty() || fresh.scripts.back().id < old)
            fresh.ajoute_script(u"print(1)"_s);
        QCOMPARE(fresh.scripts.back().id, old);
        store.reinitialise(std::move(fresh));
        QTest::qWait(1800);
        stabilise(100);
        QVERIFY(!store.ws.doc.par_nom(u"ancienne"_s));
    }

    /// Module mevreg : grandeurs aux noms protégés (np…) lisibles, une seule borne d'axe, incertitudes
    /// côté par côté, annotations verrouillées puis effacées, méthode des tangentes.
    void module_mevreg_complet()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.nouveau_tableur({});
        stabilise();
        store.edite([](Seance &ws) {
            QVERIFY(ws.doc.ajoute_experimentale(u"np"_s, QString(), {1.0, 2.0}));
            ws.doc.ajoute_experimentale(u"t"_s, u"s"_s, {0.0, 1.0, 2.0, 3.0});
            ws.doc.ajoute_experimentale(u"x"_s, u"m"_s, {1.0, 2.0, 3.0, 4.0});
            ws.doc.ajoute_experimentale(u"bas"_s, u"m"_s, {0.1, 0.1, 0.1, 0.1});
            ws.doc.ajoute_experimentale(u"haut"_s, u"m"_s, {0.3, 0.3, 0.3, 0.3});
        });
        stabilise();
        win.nouveau_script(uR"py(import mevreg as mr
mr.creer("double", mr.valeurs("np") * 2)
print(min(3, 4), "np" in mr.grandeurs())
mr.graphe("G", x="t", y="x")
mr.courbe("G", "t", "x", y_bas="bas", y_haut="haut")
mr.axes("G", ymin=0)
mr.verticale("G", 1.5, texte="E", verrou=True)
mr.texte("G", 0, 4, "note")
)py"_s);
        stabilise(20);
        const ViewId sid = store.ws.scripts.back().id;
        QSignalSpy spy(&store, &Store::script_applique);
        store.lance_script(sid);
        QVERIFY(spy.wait(10000));
        stabilise(60);
        QVERIFY2(!store.runs.at(sid).error, store.runs.at(sid).error ? qPrintable(store.runs.at(sid).error->traceback) : "");
        QCOMPARE(store.runs.at(sid).stdoutText.trimmed(), u"3 True"_s);
        QCOMPARE(store.ws.doc.par_nom(u"double"_s)->values, (std::vector<double>{2.0, 4.0}));
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
        QCOMPARE(p.curves[0].errYLow, store.ws.doc.par_nom(u"bas"_s)->id);
        QCOMPARE(p.curves[0].errYHigh, store.ws.doc.par_nom(u"haut"_s)->id);
        QVERIFY(p.curves[0].errors != ErrorBars::None);
        QCOMPARE(p.annotations.size(), size_t(2));
        QVERIFY(p.annotations[0].locked && !p.annotations[1].locked);
        // tangentes, puis tout effacer
        win.nouveau_script(uR"py(import numpy as np
import mevreg as mr
mr.creer("V", np.linspace(0, 20, 41), "mL")
mr.creer("pH", 7 + 4 * np.tanh(1.5 * (mr.valeurs("V") - 12)))
mr.graphe("D", x="V", y="pH")
mr.tangentes("D", "V", "pH")
mr.effacer_annotations("G")
)py"_s);
        stabilise(20);
        const ViewId sid2 = store.ws.scripts.back().id;
        QSignalSpy spy2(&store, &Store::script_applique);
        store.lance_script(sid2);
        QVERIFY(spy2.wait(10000));
        stabilise(60);
        QVERIFY(!store.runs.at(sid2).error);
        for (const auto &gv : store.ws.graphs) {
            if (gv.name == u"D")
                QCOMPARE(gv.plots[0].tangents.size(), size_t(1));
            if (gv.name == u"G")
                QVERIFY(gv.plots[0].annotations.empty());
        }
    }

    void glisser_fenetres()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        Dock *q = fenetre_de(win, TabKey{TabKey::Quantities});
        Dock *c = fenetre_de(win, TabKey{TabKey::Calculations});
        Dock *t = fenetre_de(win, TabKey{TabKey::Table, store.ws.tables.front().id});
        Dock *g = fenetre_de(win, TabKey{TabKey::Graph, store.ws.graphs.front().id});
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
        docking::applique(&win, tree);
        stabilise();
        QVERIFY2(near(g->geometry().left(), before.left()) && near(g->geometry().width(), before.width()),
                 qPrintable(u"%1 %2 → %3 %4"_s.arg(before.left()).arg(before.width()).arg(g->geometry().left()).arg(g->geometry().width())));

        // le graphe sous le tableur : la place du tableur partagée en deux
        const QRect tableBefore = t->geometry();
        QVERIFY(win.deplace_fenetre(g, {t, docking::Side::Bottom}));
        stabilise();
        QVERIFY(g->geometry().top() > t->geometry().bottom());
        QVERIFY(near(g->geometry().left(), t->geometry().left()));
        QVERIFY(near(g->geometry().width(), t->geometry().width()));
        QVERIFY(std::abs(g->geometry().height() - t->geometry().height()) <= 30);
        // la colonne du graphe disparue, le tableur prend la largeur libérée
        QVERIFY(t->geometry().width() > tableBefore.width() + 100);
        cliche(&win, u"deplacement-dessous"_s);

        // Calculs sur toute la largeur, en bas
        QVERIFY(win.deplace_fenetre(c, {nullptr, docking::Side::Bottom}));
        stabilise();
        QVERIFY(win.tabifiedDockWidgets(q).isEmpty());
        QVERIFY(c->geometry().top() > g->geometry().bottom());
        QVERIFY(c->geometry().left() <= q->geometry().left() + 2);
        QVERIFY(c->geometry().right() >= g->geometry().right() - 2);

        // le graphe en onglet avec Grandeurs, au premier plan
        QVERIFY(win.deplace_fenetre(g, {q, docking::Side::Center}));
        stabilise();
        QVERIFY(win.tabifiedDockWidgets(q).contains(g));
        // Qt range hors de la fenêtre les onglets qui ne sont pas au premier plan
        QVERIFY(win.rect().contains(g->geometry().center()) && !win.rect().contains(q->geometry().center()));
        // une fenêtre ne se pose pas sur elle-même
        QVERIFY(!win.deplace_fenetre(t, {t, docking::Side::Left}));

        // glissement par la barre de titre : repères, aperçu, lâcher sur le bord droit
        const QPoint title(t->width() / 2, t->widget()->geometry().top() / 2);
        QTest::mousePress(t, Qt::LeftButton, {}, title);
        QTest::mouseMove(t, title + QPoint(30, 4));
        auto *ov = win.findChild<docking::Overlay *>();
        QVERIFY(ov && ov->isVisible());
        const QPoint onQ = g->geometry().center();
        const auto ms = ov->reperes(onQ);
        QCOMPARE(ms.size(), size_t(9)); // cinq sur la pile survolée, quatre sur les bords
        QRect right;
        for (const auto &[target, r] : ms)
            if (!target.group && target.side == docking::Side::Right)
                right = r;
        QVERIFY(!right.isNull());
        QTest::mouseMove(ov, ms.front().second.center());
        QVERIFY(ov->cible_en(ms.front().second.center()) == (docking::Target{g, docking::Side::Center}));
        cliche(&win, u"deplacement-reperes"_s);
        QTest::mouseMove(ov, ms[4].second.center());
        cliche(&win, u"deplacement-reperes-dessous"_s);
        QTest::mouseMove(ov, right.center());
        cliche(&win, u"deplacement-reperes-bord"_s);
        QTest::mouseRelease(ov, Qt::LeftButton, {}, right.center());
        stabilise();
        QVERIFY(!win.findChild<docking::Overlay *>());
        QVERIFY(t->geometry().left() > g->geometry().right());
        QVERIFY(t->geometry().bottom() >= c->geometry().bottom() - 2);
        cliche(&win, u"deplacement-bord"_s);

        // Échap annule
        const QRect kept = t->geometry();
        QTest::mousePress(t, Qt::LeftButton, {}, title);
        QTest::mouseMove(t, title + QPoint(30, 4));
        ov = win.findChild<docking::Overlay *>();
        QVERIFY(ov);
        QTest::keyClick(ov, Qt::Key_Escape);
        QTest::mouseRelease(t, Qt::LeftButton, {}, title);
        stabilise();
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
        const auto mt = ov->reperes(onT);
        QVERIFY(mt.front().first == (docking::Target{t, docking::Side::Center}));
        QTest::mouseMove(ov, mt[1].second.center());
        QTest::mouseRelease(ov, Qt::LeftButton, {}, mt[1].second.center());
        stabilise();
        // Grandeurs à gauche du tableur, le graphe seul dans sa pile
        QVERIFY(win.tabifiedDockWidgets(g).isEmpty());
        QVERIFY(q->geometry().right() < t->geometry().left());
        QVERIFY(near(q->geometry().top(), t->geometry().top()));

        // une nouvelle colonne ne défait pas la disposition choisie
        const QRect gKept = g->geometry();
        win.ouvre_onglet(TabKey{TabKey::Statistics});
        stabilise();
        Dock *st = fenetre_de(win, TabKey{TabKey::Statistics});
        QVERIFY(st && st->isVisible());
        QVERIFY(c->geometry().top() > g->geometry().bottom());
        QVERIFY(near(g->geometry().top(), gKept.top()));
        cliche(&win, u"deplacement-nouvelle-colonne"_s);

        // détacher : une fenêtre à part, avec le cadre du système (Qt dessinait le sien, que
        // Wayland ne laisse pas déplacer), puis rattacher par son bouton
        QVERIFY(!g->detache() && g->barre_titre()->bouton_detacher()->isVisible());
        g->barre_titre()->bouton_detacher()->click();
        stabilise();
        QVERIFY(g->detache() && g->isWindow() && g->isVisible());
        QVERIFY(!(g->windowFlags() & Qt::FramelessWindowHint));
        QCOMPARE(win.dockWidgetArea(g), Qt::NoDockWidgetArea);
        QVERIFY(!g->barre_titre()->bouton_fermer()->isVisible());
        QCOMPARE(g->barre_titre()->bouton_detacher()->text(), u"Rattacher"_s);
        // les raccourcis de la fenêtre principale servent aussi dans la fenêtre détachée
        QVERIFY(std::any_of(g->actions().begin(), g->actions().end(), [](QAction *a) { return a->shortcut() == QKeySequence::Undo; }));
        cliche(g, u"fenetre-detachee"_s);
        g->barre_titre()->bouton_detacher()->click();
        stabilise();
        QVERIFY(!g->detache() && !g->isWindow() && win.dockWidgetArea(g) != Qt::NoDockWidgetArea);
        QVERIFY(g->actions().isEmpty());
        QVERIFY(g->barre_titre()->bouton_fermer()->isVisible());

        // ou par le menu Fenêtres
        win.detache_fenetre(g);
        stabilise();
        QVERIFY(g->detache());
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
        stabilise();
        QVERIFY(!g->detache() && win.dockWidgetArea(g) != Qt::NoDockWidgetArea);
        QVERIFY(win.rect().contains(g->geometry().center()) || !win.tabifiedDockWidgets(g).isEmpty());
    }

    void barre_titre_fenetre()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        Dock *g = fenetre_de(win, TabKey{TabKey::Graph, store.ws.graphs.front().id});
        QVERIFY(g);
        // boutons Détacher et Fermer : icônes de la hauteur d'une ligne de texte, au moins
        const int line = g->barre_titre()->fontMetrics().height();
        for (QToolButton *b : {g->barre_titre()->bouton_detacher(), g->barre_titre()->bouton_fermer()}) {
            QVERIFY(b->isVisible());
            QVERIFY2(b->iconSize().height() >= line && b->height() > line, qPrintable(u"%1 px pour une ligne de %2 px"_s.arg(b->height()).arg(line)));
        }
        QVERIFY(g->barre_titre()->height() >= g->barre_titre()->bouton_fermer()->height());
        cliche(g->barre_titre(), u"barre-de-titre"_s);
        // ils suivent l'agrandissement de l'interface
        ZoomReset reset{win};
        win.change_agrandissement(1.5);
        stabilise();
        QVERIFY(g->barre_titre()->bouton_fermer()->iconSize().height() >= g->barre_titre()->fontMetrics().height());
        QVERIFY(g->barre_titre()->bouton_fermer()->iconSize().height() > line);
    }

    void confirmation_fermeture()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        const TabKey gk{TabKey::Graph, store.ws.graphs.front().id};
        Dock *g = fenetre_de(win, gk);
        QVERIFY(g);
        auto clickButton = [](QMessageBox *box, const QString &name) {
            for (QAbstractButton *b : box->buttons())
                if (b->objectName() == name || b->text() == name)
                    b->click();
        };
        // un graphe : cacher ou fermer ?
        g->barre_titre()->bouton_fermer()->click();
        stabilise(40);
        auto *box = win.findChild<QMessageBox *>(u"confirmClose"_s);
        QVERIFY(box && box->isVisible());
        QVERIFY(box->informativeText().contains(u"menu Fenêtres"_s));
        cliche(box, u"fermeture-confirmation"_s);
        // Annuler : rien ne change
        clickButton(box, u"Annuler"_s);
        stabilise(40);
        QVERIFY(win.est_ouvert(gk));
        // Cacher : la fenêtre s'en va, le graphe reste et se retrouve dans le menu Fenêtres
        g->barre_titre()->bouton_fermer()->click();
        stabilise(40);
        clickButton(win.findChild<QMessageBox *>(u"confirmClose"_s), u"hide"_s);
        stabilise(40);
        QVERIFY(!win.est_ouvert(gk));
        QVERIFY(store.ws.graphe(gk.id));
        QMenu *windows = nullptr;
        for (QAction *a : win.menuBar()->actions())
            if (a->text() == u"Fenê&tres")
                windows = a->menu();
        QVERIFY(windows);
        Q_EMIT windows->aboutToShow();
        QWidget *row = windows->findChild<QWidget *>(u"windowRow:"_s + gk.texte());
        QVERIFY(row);
        QTest::mouseClick(row, Qt::LeftButton, {}, QPoint(10, row->height() / 2));
        stabilise();
        QVERIFY(win.est_ouvert(gk));
        // Fermer : le graphe est supprimé du document (Ctrl+Z le rétablit)
        fenetre_de(win, gk)->barre_titre()->bouton_fermer()->click();
        stabilise(40);
        clickButton(win.findChild<QMessageBox *>(u"confirmClose"_s), u"close"_s);
        stabilise(40);
        QVERIFY(!win.est_ouvert(gk));
        QVERIFY(!store.ws.graphe(gk.id));
        store.defait();
        stabilise();
        QVERIFY(store.ws.graphe(gk.id));
        // la croix du menu Fenêtres ferme aussi
        Q_EMIT windows->aboutToShow();
        row = windows->findChild<QWidget *>(u"windowRow:"_s + gk.texte());
        QVERIFY(row);
        row->findChild<QToolButton *>(u"closeWindow"_s)->click();
        stabilise();
        QVERIFY(!store.ws.graphe(gk.id));
        store.defait();
        stabilise();
        // Grandeurs : rien à perdre, pas de question
        const TabKey qk{TabKey::Quantities};
        fenetre_de(win, qk)->barre_titre()->bouton_fermer()->click();
        stabilise(40);
        QVERIFY(!win.findChild<QMessageBox *>(u"confirmClose"_s));
        QVERIFY(!win.est_ouvert(qk));
        // « Toujours cacher sans demander »
        win.ouvre_onglet(gk);
        stabilise();
        fenetre_de(win, gk)->barre_titre()->bouton_fermer()->click();
        stabilise(40);
        box = win.findChild<QMessageBox *>(u"confirmClose"_s);
        QVERIFY(box && box->checkBox());
        box->checkBox()->setChecked(true);
        clickButton(box, u"hide"_s);
        stabilise(40);
        QVERIFY(!win.est_ouvert(gk) && !store.prefs.confirmClose && store.ws.graphe(gk.id));
        win.ouvre_onglet(gk);
        stabilise();
        fenetre_de(win, gk)->barre_titre()->bouton_fermer()->click();
        stabilise(40);
        QVERIFY(!win.findChild<QMessageBox *>(u"confirmClose"_s));
        QVERIFY(!win.est_ouvert(gk) && store.ws.graphe(gk.id));
        store.prefs.confirmClose = true;
        store.prefs.enregistre();
        // un script en cours de saisie est rangé dans le document avant la fermeture
        win.nouveau_script();
        stabilise();
        const ViewId sid = store.ws.scripts.back().id;
        auto *sp = qobject_cast<ScriptPanel *>(fenetre_de(win, TabKey{TabKey::Script, sid})->widget());
        QVERIFY(sp);
        CodeEditor *editor = sp->editeur();
        QVERIFY(editor);
        editor->setPlainText(u"x = 1  # pas encore rangé"_s);
        win.ferme_onglet(TabKey{TabKey::Script, sid});
        stabilise();
        QCOMPARE(store.ws.script(sid)->code, u"x = 1  # pas encore rangé"_s);
    }

    /// Menu Fenêtres : un clic cache une fenêtre affichée (cochée), un autre la réaffiche ; rien
    /// n'est supprimé du document.
    void menu_fenetres_bascule()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        QMenu *windows = nullptr;
        for (QAction *a : win.menuBar()->actions())
            if (a->text() == u"Fenê&tres")
                windows = a->menu();
        QVERIFY(windows);
        // un graphe : ligne du menu
        const TabKey gk{TabKey::Graph, store.ws.graphs.front().id};
        QVERIFY(win.est_ouvert(gk));
        auto clickRow = [&] {
            Q_EMIT windows->aboutToShow();
            QWidget *row = windows->findChild<QWidget *>(u"windowRow:"_s + gk.texte());
            QVERIFY(row);
            QTest::mouseClick(row, Qt::LeftButton, {}, QPoint(10, row->height() / 2));
            stabilise();
        };
        clickRow();
        QVERIFY(!win.est_ouvert(gk));
        QVERIFY(store.ws.graphe(gk.id));
        clickRow();
        QVERIFY(win.est_ouvert(gk));
        // Grandeurs : entrée cochable du menu
        const TabKey qk{TabKey::Quantities};
        auto triggerQuantities = [&] {
            Q_EMIT windows->aboutToShow();
            QAction *quantities = nullptr;
            for (QAction *a : windows->actions())
                if (a->text() == u"Grandeurs")
                    quantities = a;
            QVERIFY(quantities && quantities->isCheckable());
            QVERIFY(!quantities->icon().isNull());
            QVERIFY(std::any_of(windows->actions().begin(), windows->actions().end(),
                                [](QAction *a) { return a->text() == u"Calculs"; }));
            QCOMPARE(quantities->isChecked(), win.est_ouvert(qk));
            quantities->trigger();
            stabilise();
        };
        win.ouvre_onglet(qk);
        stabilise();
        triggerQuantities();
        QVERIFY(!win.est_ouvert(qk));
        triggerQuantities();
        QVERIFY(win.est_ouvert(qk));
    }

    /// Réglages du graphe, incertitudes : cocher « une grandeur pour chaque côté » fait apparaître
    /// les listes (le panneau, refait pendant le clic, détruisait la case : plantage).
    void cases_cotes_incertitude()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        const ViewId g = store.ws.graphs.front().id;
        store.edite([&](Seance &ws) {
            for (auto &c : ws.graphe_modifiable(g)->plots.front().curves)
                c.errors = ErrorBars::Vertical;
        });
        GraphPanel *gp = win.panneau_graphe(g);
        QVERIFY(gp);
        gp->ouvre_reglages(SettingsSection::Curves);
        stabilise(120);
        auto *settings = gp->findChild<GraphSettings *>();
        QVERIFY(settings);
        QCheckBox *sides = nullptr;
        for (QCheckBox *c : settings->findChildren<QCheckBox *>(u"errorSides"_s))
            if (c->isVisible())
                sides = c;
        QVERIFY(sides);
        QVERIFY(!sides->isChecked());
        QTest::mouseClick(sides, Qt::LeftButton);
        stabilise();
        bool lists = false;
        for (QWidget *w : settings->findChildren<QWidget *>(u"errorYHigh"_s))
            lists = lists || w->isVisible();
        QVERIFY(lists);
    }

    /// Plantage (accès mémoire invalide) : le journal reçoit la cause et la pile d'appels. L'essai
    /// se relance lui-même, et c'est ce second programme qui plante.
    void journal_plantage()
    {
        if (const QString log = qEnvironmentVariable("MEVSUITE_ESSAI_PLANTAGE"); !log.isEmpty()) {
            crash::installe(log, false);
            plante_maintenant();
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(u"mevreg.log"_s);
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"MEVSUITE_ESSAI_PLANTAGE"_s, log);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {u"journal_plantage"_s});
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
    void journal_exception_non_rattrapee()
    {
        if (const QString log = qEnvironmentVariable("MEVSUITE_ESSAI_EXCEPTION"); !log.isEmpty()) {
            crash::installe(log, false);
            exception_dans_un_fil();
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(u"mevreg.log"_s);
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"MEVSUITE_ESSAI_EXCEPTION"_s, log);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {u"journal_exception_non_rattrapee"_s});
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
    void collage_signale_cellules_illisibles()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.nouveau_tableur({});
        stabilise();
        TableGrid *grid = premiere_grille(win);
        QVERIFY(grid);
        QSignalSpy messages(&store, &Store::message_demande);
        grid->setCurrentIndex(grid->model()->index(0, 0));
        grid->colle(u"1\t2\n3\toops"_s);
        stabilise();
        QVERIFY(!messages.isEmpty());
        const QString last = messages.last().at(0).toString();
        QVERIFY2(last.contains(u"1 case illisible laissée vide (« oops »)"_s), qPrintable(last));
        QCOMPARE(messages.last().at(1).value<icons::Status>(), icons::Status::Warning);
    }

    void selection_discontinue_tableur()
    {
        PosteExpert store;
        MainWindow win(store);
        Seance ws;
        const auto a = *ws.doc.ajoute_experimentale(u"A"_s, {}, {1, 2, 3});
        const auto b = *ws.doc.ajoute_experimentale(u"B"_s, {}, {4, 5, 6});
        const auto c = *ws.doc.ajoute_experimentale(u"C"_s, {}, {7, 8, 9});
        const auto d = *ws.doc.ajoute_calculee(u"D"_s, {}, u"2*B"_s);
        const auto table = ws.ajoute_tableur({a, b, c, d});
        store.reinitialise(std::move(ws));
        win.ouvre_onglet({TabKey::Table, table});
        win.show();
        stabilise();
        auto *grille = premiere_grille(win);
        QVERIFY(grille);
        auto *entete = grille->horizontalHeader();
        for (int colonne : {0, 2}) {
            const QPoint milieu(entete->sectionViewportPosition(colonne) + entete->sectionSize(colonne)/2, entete->height()/2);
            QTest::mouseClick(entete->viewport(), Qt::LeftButton, colonne == 0 ? Qt::NoModifier : Qt::ControlModifier, milieu);
        }
        grille->copie_selection(true);
        QCOMPARE(QApplication::clipboard()->text(), u"A\tC\n1\t7\n2\t8\n3\t9\n"_s);
        QTest::keyClick(grille, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), u"1\t7\n2\t8\n3\t9\n"_s);
        QVERIFY(store.ws.doc.valeurs(a).empty() && store.ws.doc.valeurs(c).empty());
        QCOMPARE(store.ws.doc.valeurs(b), (std::vector<double>{4, 5, 6}));
        store.defait();
        QCOMPARE(store.ws.doc.valeurs(a), (std::vector<double>{1, 2, 3}));
        QCOMPARE(store.ws.doc.valeurs(c), (std::vector<double>{7, 8, 9}));

        auto *selection = grille->selectionModel();
        selection->select(grille->model()->index(0, 0), QItemSelectionModel::ClearAndSelect);
        selection->select(grille->model()->index(2, 2), QItemSelectionModel::Select);
        grille->copie_selection(false);
        QCOMPARE(QApplication::clipboard()->text(), u"1\t\n\t9\n"_s);
        QTest::keyClick(grille, Qt::Key_Delete);
        QVERIFY(std::isnan(store.ws.doc.valeurs(a).at(0)));
        QCOMPARE(store.ws.doc.valeurs(a).at(1), 2.0);
        QCOMPARE(store.ws.doc.valeurs(c), (std::vector<double>{7, 8}));
        grille->supprime_lignes_choisies();
        QCOMPARE(store.ws.doc.valeurs(b), std::vector<double>{5});
        QCOMPARE(store.ws.doc.valeurs(d), std::vector<double>{10});
        const auto revision = store.ws.doc.revision();
        grille->choisit_donnees_colonne(3);
        grille->efface_selection();
        grille->choisit_colonnes(3, Qt::ControlModifier);
        grille->efface_selection();
        QCOMPARE(store.ws.doc.revision(), revision);
    }

    void colonnes_tableur()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        win.nouveau_tableur({});
        stabilise();
        TableGrid *grid = premiere_grille(win);
        QVERIFY(grid);
        auto *header = qobject_cast<TableHeader *>(grid->horizontalHeader());
        QVERIFY(header);
        const ViewId table = store.ws.tables.front().id;
        auto addColumn = [&](const QString &name) {
            store.edite([&](Seance &ws) {
                const QId q = *ws.doc.ajoute_experimentale(name, QString(), {0.1, 0.2});
                ws.tableur_modifiable(table)->columns.push_back(q);
            });
            stabilise();
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
            QCOMPARE(header->poignee_en(from), 0);
            QTest::mousePress(header->viewport(), Qt::LeftButton, {}, from);
            QVERIFY(header->redimensionnement());
            for (int k = 1; k <= 10; ++k) {
                QMouseEvent move(QEvent::MouseMove, QPointF(from + (to - from) * k / 10), header->viewport()->mapToGlobal(QPointF(from + (to - from) * k / 10)),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(header->viewport(), &move);
            }
            QTest::mouseRelease(header->viewport(), Qt::LeftButton, {}, to);
            stabilise(300);
            QVERIFY(!header->redimensionnement());
            QVERIFY2(std::abs(grid->columnWidth(0) - (before - 200)) <= 2, qPrintable(u"%1 px au lieu de %2"_s.arg(grid->columnWidth(0)).arg(before - 200)));
            QVERIFY2(std::abs(grid->columnWidth(0) + grid->columnWidth(1) - total) <= 2, qPrintable(u"%1 + %2 px"_s.arg(grid->columnWidth(0)).arg(grid->columnWidth(1))));
            QVERIFY(!ouvre_dialogue(win));
            // la largeur choisie résiste à un changement de taille de la fenêtre
            win.resize(1200, 800);
            stabilise(200);
            QVERIFY(std::abs(grid->columnWidth(0) - (before - 200)) <= 2);
            win.resize(1300, 800);
            stabilise(200);
            // double-clic sur la poignée : retour au partage égal
            const QPoint handle(header->sectionViewportPosition(0) + grid->columnWidth(0) - 2, header->height() / 2);
            QTest::mouseDClick(header->viewport(), Qt::LeftButton, {}, handle);
            stabilise(200);
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
        QCOMPARE(header->poignee_en(QPoint(edge + 1, header->height() / 2)), last - 1);
        QCOMPARE(header->poignee_en(QPoint(header->sectionViewportPosition(last) + theme::px(20), header->height() / 2)), -1);
        // cliquer près du bord de « + » ne demande pas de nouvelle grandeur
        QTest::mousePress(header->viewport(), Qt::LeftButton, {}, QPoint(edge + 1, header->height() / 2));
        QTest::mouseRelease(header->viewport(), Qt::LeftButton, {}, QPoint(edge + 1, header->height() / 2));
        stabilise(40);
        QVERIFY(!ouvre_dialogue(win));
    }

    void colonnes_glissees_sur_graphe()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        TableGrid *grid = premiere_grille(win);
        QVERIFY(grid);
        // Ctrl + clic sur un second en-tête : les deux colonnes sélectionnées, glissées ensemble
        grid->choisit_colonnes(0, Qt::NoModifier);
        grid->choisit_colonnes(1, Qt::ControlModifier);
        QCOMPARE(grid->colonnes_choisies(true), (std::vector<int>{0, 1}));
        const QId t = store.ws.doc.par_nom(u"t"_s)->id, z = store.ws.doc.par_nom(u"z"_s)->id;
        std::unique_ptr<QMimeData> mime(dnd::mime_grandeurs({t, z}));
        QCOMPARE(dnd::grandeurs_de(mime.get()), (std::vector<QId>{t, z}));
        QCOMPARE(dnd::grandeur_de(mime.get()), std::optional<QId>(t));
        // déposées sur un graphe vide : z(t) en une fois
        win.nouveau_graphe();
        stabilise();
        const ViewId g = store.ws.graphs.back().id;
        graphops::depose_grandeurs(win, g, 0, {t, z}, 1);
        stabilise();
        QCOMPARE(store.ws.graphe(g)->plots[0].curves.size(), size_t(1));
        QCOMPARE(store.ws.graphe(g)->plots[0].curves[0].x, t);
        QCOMPARE(store.ws.graphe(g)->plots[0].curves[0].y, z);
        // une seconde fois : déjà tracée, rien n'est ajouté
        graphops::depose_grandeurs(win, g, 0, {t, z}, 1);
        QCOMPARE(store.ws.graphe(g)->plots[0].curves.size(), size_t(1));
        // Ctrl + clic sur une colonne sélectionnée : elle est retirée de la sélection
        grid->choisit_colonnes(1, Qt::ControlModifier);
        QCOMPARE(grid->colonnes_choisies(true), (std::vector<int>{0}));
    }

    void reglages_graphe_gardent_largeur()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        const ViewId g = store.ws.graphs.front().id;
        GraphPanel *gp = win.panneau_graphe(g);
        QVERIFY(gp);
        auto *settings = gp->findChild<GraphSettings *>();
        QVERIFY(settings);
        gp->ouvre_reglages(SettingsSection::Curves);
        stabilise(120);
        // place prise au tableur et aux Grandeurs : le graphe reste visible à côté des réglages
        QVERIFY(gp->canevas()->isVisible());
        QVERIFY(settings->isVisible());
        // jamais écrasés : à leur largeur minimale au moins ; le graphe à côté s'il y a la place,
        // sinon caché, avec une explication
        QVERIFY2(settings->width() >= settings->minimumSizeHint().width(), qPrintable(u"%1 < %2"_s.arg(settings->width()).arg(settings->minimumSizeHint().width())));
        QVERIFY(rogne(gp).isEmpty());
        cliche(gp, u"reglages-ouverts"_s);
        // le bouton ✕ des réglages les ferme
        auto *close = settings->findChild<QToolButton *>(u"closeSettings"_s);
        QVERIFY(close);
        close->click();
        stabilise();
        QVERIFY(!settings->isVisible());
        QVERIFY(gp->canevas()->isVisible());
    }

    void annotations_a_la_souris()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        const ViewId g = store.ws.graphs.front().id;
        GraphCanvas *canvas = win.panneau_graphe(g)->canevas();
        const PlotLayout l = canvas->dispositions().front();
        // droite oblique par deux points, au milieu du graphe
        const double x0 = l.x.depuis_px(l.area.left() + l.area.width() * 0.3), x1 = l.x.depuis_px(l.area.left() + l.area.width() * 0.7);
        const double y0 = l.y.depuis_px(l.area.top() + l.area.height() * 0.7), y1 = l.y.depuis_px(l.area.top() + l.area.height() * 0.3);
        store.edite([&](Seance &ws) {
            Annotation a;
            a.kind = Annotation::Line;
            a.x = x0;
            a.y = y0;
            a.x2 = x1;
            a.y2 = y1;
            ws.graphe_modifiable(g)->plots[0].annotations.push_back(a);
        });
        stabilise();
        const auto &as = [&]() -> const std::vector<Annotation> & { return store.ws.graphe(g)->plots[0].annotations; };
        const QPoint mid = QPointF((l.x.vers_px(x0) + l.x.vers_px(x1)) / 2, (l.y.vers_px(y0) + l.y.vers_px(y1)) / 2).toPoint();
        // glissée : les deux points se déplacent de même
        QTest::mousePress(canvas, Qt::LeftButton, {}, mid);
        QTest::mouseMove(canvas, mid + QPoint(10, 20));
        QTest::mouseMove(canvas, mid + QPoint(20, 40));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, mid + QPoint(20, 40));
        stabilise();
        const Annotation moved = as()[0];
        QVERIFY(std::abs(l.x.vers_px(moved.x) - l.x.vers_px(x0) - 20) < 1.5 && std::abs(l.y.vers_px(moved.y2) - l.y.vers_px(y1) - 40) < 1.5);
        // inclinée par une poignée : le second point seul
        const QPoint h2 = QPointF(l.x.vers_px(moved.x2), l.y.vers_px(moved.y2)).toPoint();
        QTest::mousePress(canvas, Qt::LeftButton, {}, h2);
        QTest::mouseMove(canvas, h2 + QPoint(0, -30));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, h2 + QPoint(0, -30));
        stabilise();
        QCOMPARE(as()[0].x, moved.x);
        QCOMPARE(as()[0].y, moved.y);
        QVERIFY(std::abs(l.y.vers_px(as()[0].y2) - (h2.y() - 30)) < 1.5);
        cliche(canvas, u"droite-oblique"_s);
        // verrouillée : elle ne bouge plus
        store.edite([&](Seance &ws) { ws.graphe_modifiable(g)->plots[0].annotations[0].locked = true; });
        stabilise();
        const Annotation locked = as()[0];
        const QPoint m2 = QPointF((l.x.vers_px(locked.x) + l.x.vers_px(locked.x2)) / 2, (l.y.vers_px(locked.y) + l.y.vers_px(locked.y2)) / 2).toPoint();
        QTest::mousePress(canvas, Qt::LeftButton, {}, m2);
        QTest::mouseMove(canvas, m2 + QPoint(30, 0));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, m2 + QPoint(30, 0));
        stabilise();
        QCOMPARE(as()[0].x, locked.x);
        QCOMPARE(as()[0].x2, locked.x2);
        // réglages : verrou et taille du texte à côté de la couleur
        store.edite([&](Seance &ws) {
            Annotation t;
            t.kind = Annotation::Text;
            t.text = u"repère"_s;
            t.x = x0;
            t.y = y0;
            ws.graphe_modifiable(g)->plots[0].annotations.push_back(t);
        });
        win.panneau_graphe(g)->ouvre_reglages(SettingsSection::Annotations);
        stabilise(120);
        auto *settings = win.panneau_graphe(g)->findChild<GraphSettings *>();
        const QList<QToolButton *> locks = settings->findChildren<QToolButton *>(u"annotationLock"_s);
        QCOMPARE(locks.size(), 2);
        QVERIFY(locks[0]->isChecked() && !locks[1]->isChecked());
        cliche(settings, u"reglages-annotations"_s);
    }

    void modelisation_detachee_barres_plage()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        const ViewId g = store.ws.graphs.front().id;
        // la fenêtre Modélisation s'ouvre à part
        win.ouvre_modelisation(g, 0, 0);
        stabilise(120);
        Dock *m = fenetre_de(win, TabKey{TabKey::Modeling});
        QVERIFY(m && m->detache() && m->isWindow() && m->isVisible());
        ModelingPanel *mp = ModelingPanel::instance();
        QVERIFY(mp);
        QCOMPARE(mp->modele_affiche(g, 0), std::optional<int>(0));
        // nom du modèle dans la légende
        auto *label = mp->findChild<QLineEdit *>(u"fitLabel"_s);
        QVERIFY(label);
        label->setText(u"ma parabole"_s);
        Q_EMIT label->editingFinished();
        stabilise();
        QCOMPARE(store.ws.graphe(g)->plots[0].fits[0].label, std::optional<QString>(u"ma parabole"_s));
        // plage : deux barres sur le graphe, glissées à la souris
        store.edite([&](Seance &ws) { ws.graphe_modifiable(g)->plots[0].fits[0].range = std::pair(0.1, 0.5); });
        stabilise(120);
        GraphCanvas *canvas = win.panneau_graphe(g)->canevas();
        const PlotLayout l = canvas->dispositions().front();
        const QPoint bar(static_cast<int>(std::lround(l.x.vers_px(0.1))), static_cast<int>(l.area.center().y()));
        QTest::mousePress(canvas, Qt::LeftButton, {}, bar);
        QTest::mouseMove(canvas, QPoint(static_cast<int>(std::lround(l.x.vers_px(0.2))), bar.y()));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(static_cast<int>(std::lround(l.x.vers_px(0.2))), bar.y()));
        stabilise();
        const auto range = *store.ws.graphe(g)->plots[0].fits[0].range;
        QVERIFY2(std::abs(range.first - 0.2) < 0.01 && range.second == 0.5, qPrintable(u"%1 %2"_s.arg(range.first).arg(range.second)));
        // étendue de la courbe tracée « de … à … » : ses barres (en pointillés) se glissent aussi
        store.edite([&](Seance &ws) {
            FitSpec &f = ws.graphe_modifiable(g)->plots[0].fits[0];
            f.draw = FitDraw::Custom;
            f.drawRange = {0.02, 0.58};
        });
        stabilise(120);
        const QPoint end(static_cast<int>(std::lround(l.x.vers_px(0.58))), static_cast<int>(l.area.center().y()));
        QTest::mousePress(canvas, Qt::LeftButton, {}, end);
        QTest::mouseMove(canvas, QPoint(static_cast<int>(std::lround(l.x.vers_px(0.4))), end.y()));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(static_cast<int>(std::lround(l.x.vers_px(0.4))), end.y()));
        stabilise();
        const auto drawn = store.ws.graphe(g)->plots[0].fits[0].drawRange;
        QVERIFY2(std::abs(drawn.second - 0.4) < 0.01 && drawn.first == 0.02, qPrintable(u"%1 %2"_s.arg(drawn.first).arg(drawn.second)));
        cliche(canvas, u"modelisation-plage"_s);
        // le modèle se rattache comme les autres fenêtres
        m->barre_titre()->bouton_detacher()->click();
        stabilise();
        QVERIFY(!m->detache());
    }

    void tangentes_supprimables()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        const ViewId g = store.ws.graphs.front().id;
        const QId t = store.ws.doc.par_nom(u"t"_s)->id, z = store.ws.doc.par_nom(u"z"_s)->id;
        store.edite([&](Seance &ws) {
            TangentSpec ts;
            ts.x = t;
            ts.y = z;
            ws.graphe_modifiable(g)->plots[0].tangents.push_back(ts);
        });
        win.panneau_graphe(g)->ouvre_reglages(SettingsSection::Models);
        stabilise(120);
        auto *settings = win.panneau_graphe(g)->findChild<GraphSettings *>();
        auto *lock = settings->findChild<QToolButton *>(u"tangentLock"_s);
        auto *del = settings->findChild<QToolButton *>(u"tangentDelete"_s);
        QVERIFY(lock && del);
        lock->click();
        stabilise();
        QVERIFY(store.ws.graphe(g)->plots[0].tangents[0].locked);
        settings = win.panneau_graphe(g)->findChild<GraphSettings *>();
        settings->findChild<QToolButton *>(u"tangentDelete"_s)->click();
        stabilise();
        QVERIFY(store.ws.graphe(g)->plots[0].tangents.empty());
    }

    void options_export_csv()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win);
        stabilise();
        const QId t = store.ws.doc.par_nom(u"t"_s)->id, z = store.ws.doc.par_nom(u"z"_s)->id;
        csvimport::exporte_fichier(win, {t, z});
        stabilise(40);
        QDialog *d = ouvre_dialogue(win);
        QVERIFY(d && d->objectName() == u"csvExport");
        auto *preview = d->findChild<QPlainTextEdit *>(u"preview"_s);
        auto *sep = d->findChild<QComboBox *>(u"separator"_s);
        auto *dec = d->findChild<QComboBox *>(u"decimal"_s);
        auto *head = d->findChild<QComboBox *>(u"header"_s);
        QVERIFY(preview && sep && dec && head);
        // en français : point-virgule, virgule décimale, « nom (unité) »
        QVERIFY2(preview->toPlainText().startsWith(u"t (s);z (m)\n0;1,25"_s), qPrintable(preview->toPlainText()));
        QCOMPARE(preview->blockCount(), 9);
        QVERIFY(preview->toPlainText().endsWith(u"… (17 lignes en tout)"_s));
        cliche(d, u"export-csv"_s);
        // tabulation, point décimal, unités sur la deuxième ligne
        sep->setCurrentIndex(2);
        dec->setCurrentIndex(1);
        head->setCurrentIndex(1);
        stabilise(20);
        QVERIFY2(preview->toPlainText().startsWith(u"t ⇥ z\ns ⇥ m\n0 ⇥ 1.25"_s), qPrintable(preview->toPlainText()));
        QVERIFY(preview->toPlainText().endsWith(u"… (18 lignes en tout)"_s));
        // virgule entre les valeurs : le point décimal est imposé
        sep->setCurrentIndex(1);
        dec->setCurrentIndex(0);
        stabilise(20);
        QCOMPARE(dec->currentIndex(), 1);
        d->reject();
        stabilise(20);
        // écriture : codage Windows (accents), sans marque UTF-8
        QTemporaryDir dir;
        const QString path = dir.filePath(u"e.csv"_s);
        store.edite([&](Seance &ws) { ws.doc.definit_unite(t, u"µs"_s); });
        QVERIFY(win.ecrit_csv(path, {t}, csv::FRENCH, csv::Encoding::Windows1252));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.startsWith("t (\xb5s)"));
        // Une unité écrite sur deux lignes garde ses guillemets dans le fichier
        // et ne prend qu'une ligne dans l'aperçu.
        store.edite([&](Seance &ws) { ws.doc.definit_unite(t, u"s\r\nécoulées"_s); });
        csvimport::exporte_fichier(win, {t, z});
        d = ouvre_dialogue(win);
        QVERIFY(d);
        preview = d->findChild<QPlainTextEdit *>(u"preview"_s);
        QVERIFY(preview);
        QCOMPARE(preview->blockCount(), 9);
        QVERIFY(preview->toPlainText().startsWith(u"\"t (s écoulées)\";z (m)"_s));
        QVERIFY(csv::texte_export(store.ws.doc, {t, z}, csv::FRENCH).startsWith(u"\"t (s\r\nécoulées)\";z (m)\r\n"_s));
    }

    void apercu_import_csv()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        stabilise();
        QTemporaryDir dir;
        // fichier « à la française » : titre, noms et unités, une ligne vide, une case illisible
        const QString path = dir.filePath(u"mesures.csv"_s);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Mesures du 12 octobre\nt (s);U (V);remarque\n0;1,5;\n\n0,1;1,8;ok\n0,2;2.1;\n0,3;2,4;\n");
        }
        csvimport::ouvre(win, path, std::nullopt);
        stabilise(40);
        QDialog *d = ouvre_dialogue(win);
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
        cliche(d, u"import-csv"_s);
        // la colonne U renommée avant l'import
        preview->item(1, 1)->setText(u"Uc"_s);
        stabilise(40);
        QPushButton *ok = nullptr;
        for (QPushButton *b : d->findChildren<QPushButton *>())
            if (b->text() == u"Importer")
                ok = b;
        QVERIFY(ok && ok->isEnabled());
        ok->click();
        stabilise();
        const Grandeur *t = store.ws.doc.par_nom(u"t"_s);
        const Grandeur *u = store.ws.doc.par_nom(u"Uc"_s);
        QVERIFY(t && u && !store.ws.doc.par_nom(u"remarque"_s));
        QCOMPARE(t->unit, u"s"_s);
        QCOMPARE(t->values, (std::vector<double>{0, 0.1, 0.2, 0.3}));
        QCOMPARE(u->values.size(), size_t(4));
        QVERIFY(std::isnan(u->values[2]));
        QCOMPARE(store.ws.tables.size(), size_t(1));
        QCOMPARE(store.ws.tables.front().name, u"mesures"_s);

        // mauvais séparateur choisi : l'import est refusé tant qu'il n'y a pas de nombres
        csvimport::ouvre(win, path, store.ws.tables.front().id);
        stabilise(40);
        d = ouvre_dialogue(win);
        QVERIFY(d);
        sep = d->findChild<QComboBox *>(u"separator"_s);
        dec = d->findChild<QComboBox *>(u"decimal"_s);
        summary = d->findChild<QLabel *>(u"summary"_s);
        QVERIFY(summary->text().contains(u"au tableur « mesures »"_s));
        sep->setCurrentIndex(3); // espaces
        stabilise(40);
        ok = nullptr;
        for (QPushButton *b : d->findChildren<QPushButton *>())
            if (b->text() == u"Importer")
                ok = b;
        QVERIFY(ok && !ok->isEnabled());
        QVERIFY2(summary->text().contains(u"Rien à importer"_s), qPrintable(summary->text()));
        // mauvais séparateur décimal : les cases illisibles sont signalées, et ce que donnerait l'autre
        sep->setCurrentIndex(0);
        dec->setCurrentIndex(1);
        stabilise(40);
        QVERIFY(ok->isEnabled());
        QVERIFY2(summary->text().contains(u"seraient lues"_s), qPrintable(summary->text()));
        // virgule comme séparateur des valeurs : le séparateur décimal est forcément le point
        dec->setCurrentIndex(0);
        sep->setCurrentIndex(1);
        stabilise(40);
        QCOMPARE(dec->currentIndex(), 1);
        d->reject();
        stabilise(20);
    }

    void dialogues_ouverts()
    {
        PosteExpert store;
        MainWindow win(store);
        win.resize(1300, 800);
        win.show();
        prepare_graphe(win, true);
        stabilise();
        const QId u = store.ws.doc.par_nom(u"U"_s)->id;
        const std::vector<std::pair<QString, std::function<void()>>> all = {
            {u"grandeur"_s, [&] { dialogs::nouvelle_grandeur(win, false); }},
            {u"calculee"_s, [&] { dialogs::nouvelle_grandeur(win, true); }},
            {u"parametre"_s, [&] { dialogs::nouveau_parametre(win); }},
            {u"incertitude"_s, [&] { dialogs::nouvelle_incertitude(win, u); }},
            {u"suite"_s, [&] { dialogs::remplit(win, u); }},
            {u"suppression"_s, [&] { dialogs::confirme_suppression(win, u); }},
            {u"edition"_s, [&] { dialogs::modifie_grandeur(win, u); }},
            {u"colonne"_s, [&] { dialogs::nouvelle_colonne(win, store.ws.tables.front().id); }},
            {u"derivee"_s, [&] { dialogs::derivee(win, false); }},
            {u"integrale"_s, [&] { dialogs::integrale(win); }},
            {u"spectre"_s, [&] { dialogs::spectre(win); }},
            {u"tangentes"_s, [&] { dialogs::tangentes(win); }},
            {u"methode-derivee"_s, [&] { dialogs::methode_derivee(win); }},
            {u"infos"_s, [&] { dialogs::meta(win); }},
            {u"preferences"_s, [&] { dialogs::preferences(win); }},
            {u"python"_s, [&] { dialogs::export_python(win); }},
        };
        for (const auto &[name, open] : all) {
            open();
            stabilise(40);
            QDialog *d = ouvre_dialogue(win);
            QVERIFY2(d, qPrintable(name));
            cliche(d, u"dialogue-"_s + name);
            d->reject();
            stabilise(20);
        }
        // nom refusé : le message (sur deux lignes) agrandit la boîte sans écraser les champs, et
        // s'efface dès que le nom est corrigé
        {
            dialogs::nouvelle_colonne(win, store.ws.tables.front().id);
            stabilise(40);
            QDialog *col = ouvre_dialogue(win);
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
            stabilise(60);
            QLabel *error = nullptr;
            for (QLabel *l : col->findChildren<QLabel *>())
                if (l->isVisible() && l->text().startsWith(u"⚠"_s))
                    error = l;
            QVERIFY2(error, "message d'erreur absent");
            QVERIFY(error->text().contains(u"54j"_s));
            QVERIFY2(nameEdit->height() >= fieldHeight, qPrintable(u"champ de %1 px au lieu de %2"_s.arg(nameEdit->height()).arg(fieldHeight)));
            QVERIFY(nameEdit->height() >= nameEdit->sizeHint().height());
            QVERIFY(error->height() >= error->heightForWidth(error->width()));
            cliche(col, u"dialogue-colonne-erreur"_s);
            QTest::keyClicks(nameEdit, u"x"_s);
            stabilise(40);
            QVERIFY(!error->isVisible());
            col->reject();
            stabilise(20);
        }
        // une incertitude créée par la boîte de dialogue s'affiche en barres sur le graphe
        dialogs::nouvelle_incertitude(win, store.ws.doc.par_nom(u"I"_s)->id);
        stabilise(40);
        QDialog *d = ouvre_dialogue(win);
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
        stabilise(40);
        const Grandeur *iq = store.ws.doc.par_nom(u"I"_s);
        QVERIFY(iq->uncertainty.has_value());
        QCOMPARE(store.ws.doc.obtient(*iq->uncertainty)->values.front(), 0.003);
    }

    void agrandissement()
    {
        // Ctrl + / Ctrl − / Ctrl 0 : polices, lignes du tableur, graduations des graphes plus grandes
        PosteExpert store;
        MainWindow win(store);
        win.resize(2000, 1100);
        const ZoomReset reset{win};
        win.show();
        prepare_graphe(win, true);
        win.activateWindow();
        stabilise(120);
        QCOMPARE(win.agrandissement(), 1.0);
        const auto plafond = [&] {
            // Le compositeur peut attribuer une largeur inférieure à la taille demandée.
            return std::clamp(std::floor(10.0 * win.width() / MIN_WINDOW.width()) / 10.0, 1.0, UI_ZOOM_MAX);
        };
        QCOMPARE(win.agrandissement_max(), plafond());
        const ViewId graph = store.ws.graphs.front().id;
        TableGrid *grid = premiere_grille(win);
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
        const double margin = marge_basse(win, graph);
        QVERIFY(margin > 20);
        const int icon = win.findChild<QToolBar *>()->iconSize().height();
        cliche(&win, u"agrandissement-100"_s);
        for (const double demande : {1.5, 2.0}) {
            win.change_agrandissement(demande);
            const double zoom = std::min(demande, plafond());
            stabilise(120);
            QCOMPARE(win.agrandissement(), zoom);
            QCOMPARE(theme::zoom(), zoom);
            QCOMPARE(store.prefs.uiZoom, demande);
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
            const double m = marge_basse(win, graph);
            QVERIFY2(std::abs(m / margin - zoom) < 0.12 * zoom, qPrintable(u"marge %1 → %2"_s.arg(margin).arg(m)));
            QCOMPARE(win.findChild<QToolBar *>()->iconSize().height(), static_cast<int>(std::lround(icon * zoom)));
            // les points restent dans la zone de tracé, à leur place en pixels du widget ; le
            // réticule s'y accroche
            GraphCanvas *canvas = win.panneau_graphe(graph)->canevas();
            const PlotLayout &l = canvas->dispositions().front();
            const Grandeur *iq = store.ws.doc.par_nom(u"I"_s), *uq = store.ws.doc.par_nom(u"U"_s);
            const QPointF p(l.x.vers_px(iq->values[5]), l.y.vers_px(uq->values[5]));
            QVERIFY(l.area.contains(p) && canvas->rect().contains(l.area.toRect()));
            QTest::mouseMove(canvas, (p + QPointF(3, -2) * zoom).toPoint());
            stabilise(60);
            cliche(win.panneau_graphe(graph), u"agrandissement-reticule-%1"_s.arg(std::lround(zoom * 100)));
            cliche(&win, u"agrandissement-%1"_s.arg(std::lround(zoom * 100)));
        }
        // au plus ce que permet la largeur de la fenêtre ; l'agrandissement demandé revient quand
        // la fenêtre s'élargit
        win.change_agrandissement(3.0);
        stabilise();
        QCOMPARE(win.agrandissement(), plafond());
        // c'est l'agrandissement demandé qui est enregistré
        QCOMPARE(store.prefs.uiZoom, 3.0);
        win.resize(1500, 1000);
        stabilise();
        QCOMPARE(win.agrandissement(), plafond());
        QCOMPARE(store.prefs.uiZoom, 3.0);
        win.resize(3000, 1100);
        stabilise();
        QCOMPARE(win.agrandissement(), plafond());
        win.resize(2000, 1100);
        stabilise();
        QCOMPARE(win.agrandissement(), plafond());
        // raccourcis : Ctrl 0, Ctrl +, Ctrl =, Ctrl −, par pas de 10 %
        win.activateWindow();
        QTest::keyClick(&win, Qt::Key_0, Qt::ControlModifier);
        stabilise();
        QCOMPARE(win.agrandissement(), 1.0);
        QTest::keyClick(&win, Qt::Key_Plus, Qt::ControlModifier);
        stabilise();
        QCOMPARE(win.agrandissement(), 1.1);
        QTest::keyClick(&win, Qt::Key_Equal, Qt::ControlModifier);
        stabilise();
        QCOMPARE(win.agrandissement(), 1.2);
        QTest::keyClick(&win, Qt::Key_Minus, Qt::ControlModifier);
        QTest::keyClick(&win, Qt::Key_Minus, Qt::ControlModifier);
        QTest::keyClick(&win, Qt::Key_Minus, Qt::ControlModifier);
        stabilise();
        QCOMPARE(win.agrandissement(), 0.9);
        QVERIFY(size() < point);
        // retour à 100 % : tout reprend sa taille
        QTest::keyClick(&win, Qt::Key_0, Qt::ControlModifier);
        stabilise(120);
        QCOMPARE(win.agrandissement(), 1.0);
        QCOMPARE(theme::zoom(), 1.0);
        QCOMPARE(store.prefs.uiZoom, 1.0);
        QCOMPARE(size(), point);
        QCOMPARE(grid->rowHeight(0), row);
        // colonnes réparties sur la largeur du tableur : les mêmes s'il a retrouvé sa largeur
        if (grid->viewport()->width() == viewportWidth)
            QCOMPARE(grid->columnWidth(0), column);
        QVERIFY(grid->columnWidth(0) >= 89);
        QVERIFY(std::abs(marge_basse(win, graph) - margin) < 0.5);
    }

    void agrandissement_sans_rognure()
    {
        // à 150 % et 200 %, en clair et en sombre : aucune fenêtre rognée ni hors de la fenêtre,
        // menus, barre d'outils et barre d'état en entier ; fenêtre de 2000 px, la plus étroite
        // qui permette 200 %
        for (const bool dark : {false, true}) {
            theme::choisit(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            PosteExpert store;
            MainWindow win(store);
            win.resize(2000, 1100);
            const ZoomReset reset{win};
            win.show();
            prepare_graphe(win, true);
            stabilise(120);
            const ViewId graph = store.ws.graphs.front().id;
            double zoom = 1.0;
            const auto check = [&] {
                for (Dock *d : win.findChildren<Dock *>()) {
                    // onglets à l'arrière-plan : Qt les place hors de la fenêtre
                    if (!d->isVisible() || !d->widget()->isVisible() || d->widget()->width() <= 0 || d->visibleRegion().isEmpty())
                        continue;
                    const QString why = rogne(d->widget());
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
                win.change_agrandissement(zoom);
                stabilise(120);
                QCOMPARE(win.agrandissement(), zoom);
                const QString suffix = u"%1%2"_s.arg(std::lround(zoom * 100)).arg(dark ? u"-sombre"_s : QString());
                cliche(&win, u"agrandissement-"_s + suffix);
                check();
                // les autres panneaux : modélisation, statistiques, script
                win.ouvre_modelisation(graph, 0, 0);
                stabilise(120);
                check();
                cliche(&win, u"agrandissement-modelisation-"_s + suffix);
                win.ouvre_statistiques(store.ws.doc.quantities.front().id);
                stabilise(120);
                check();
                cliche(&win, u"agrandissement-statistiques-"_s + suffix);
                win.nouveau_script();
                stabilise(120);
                check();
                cliche(&win, u"agrandissement-script-"_s + suffix);
                // réglages d'un graphe à côté des trois colonnes : il faut la place d'une colonne
                // de plus (1200 px à 100 %)
                win.disposition_par_defaut();
                win.resize(std::max(2000, static_cast<int>(1200 * zoom)), 1100);
                stabilise(120);
                win.panneau_graphe(graph)->ouvre_reglages(SettingsSection::Curves);
                stabilise(120);
                check();
                cliche(&win, u"agrandissement-reglages-"_s + suffix);
                win.disposition_par_defaut();
                win.resize(2000, 1100);
                stabilise(120);
            }
            // page d'accueil agrandie
            win.nouveau_fichier();
            stabilise(120);
            QVERIFY(win.centralWidget()->isVisible());
            QVERIFY(rogne(win.centralWidget()).isEmpty());
            cliche(&win, u"agrandissement-accueil"_s + (dark ? u"-sombre"_s : QString()));
            win.change_agrandissement(1.0);
            stabilise();
            QCOMPARE(theme::zoom(), 1.0);
        }
        theme::choisit(Qt::ColorScheme::Light);
    }

    void agrandissement_sans_effet_sur_exports()
    {
        // seul l'affichage à l'écran suit l'agrandissement : PNG et SVG identiques avant et après
        PosteExpert store;
        MainWindow win(store);
        win.resize(2000, 1100);
        const ZoomReset reset{win};
        win.show();
        prepare_graphe(win, true);
        stabilise(120);
        const GraphView &g = store.ws.graphs.front();
        QTemporaryDir dir;
        const auto write = [&](const QString &name, exporter::Format fmt) {
            QString error;
            const QString path = dir.filePath(name);
            if (!exporter::ecrit_graphe(path, fmt, g, store.ws, QSizeF(720, 460), store.style(), &error))
                return QByteArray();
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QImage screen = win.panneau_graphe(g.id)->canevas()->grab().toImage();
        const QByteArray png = write(u"avant.png"_s, exporter::Format::Png);
        const QByteArray svg = write(u"avant.svg"_s, exporter::Format::Svg);
        QVERIFY(!png.isEmpty() && !svg.isEmpty());
        const QImage image = exporter::image_graphe(g, store.ws.doc, QSizeF(720, 460), 2.0, store.style());
        for (const double zoom : {1.5, 2.0}) {
            win.change_agrandissement(zoom);
            stabilise(120);
            QCOMPARE(write(u"apres.png"_s, exporter::Format::Png), png);
            QCOMPARE(write(u"apres.svg"_s, exporter::Format::Svg), svg);
            QCOMPARE(exporter::image_graphe(g, store.ws.doc, QSizeF(720, 460), 2.0, store.style()), image);
            // à l'écran, le graphe a changé
            QVERIFY(win.panneau_graphe(g.id)->canevas()->grab().toImage() != screen);
        }
        QTest::keyClick(&win, Qt::Key_0, Qt::ControlModifier);
        win.change_agrandissement(1.0);
        stabilise();
        QCOMPARE(theme::zoom(), 1.0);
    }
};

QTEST_MAIN(TestApp)
#include "test_app.moc"
