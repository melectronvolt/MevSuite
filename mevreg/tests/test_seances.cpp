// Parcours élève. Les mesures entrent par les mêmes cellules, presse-papiers et fichiers
// qu’en TP. Le document est consulté après les gestes pour vérifier le résultat physique.
#include "app/graphwidget.h"
#include "app/help.h"
#include "app/mainwindow.h"
#include "app/modelingpanel.h"
#include "app/scriptpanel.h"
#include "app/statspanel.h"
#include "app/tableview.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/resources.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextBrowser>
#include <QToolButton>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

void exige(bool condition, const QString &message)
{
    if (!condition)
        throw std::runtime_error(message.toStdString());
}

void actualise()
{
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

QAbstractButton *bouton(QWidget &racine, const QString &texte)
{
    for (auto *b : racine.findChildren<QAbstractButton *>())
        if (b->isVisible() && (b->text().remove(u'&') == texte || b->accessibleName() == texte))
            return b;
    throw std::runtime_error((u"Bouton introuvable : "_s + texte).toStdString());
}

void clique(QWidget &racine, const QString &texte)
{
    auto *b = bouton(racine, texte);
    exige(b->isEnabled(), u"Commande indisponible : "_s + texte);
    QTest::mouseClick(b, Qt::LeftButton);
    actualise();
}

template<class T> T *champ(QWidget &racine, const QString &nom)
{
    auto *w = racine.findChild<T *>(nom);
    exige(w, u"Champ introuvable : "_s + nom);
    return w;
}

QDialog *dialogue()
{
    for (auto *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QDialog *>(w); d && d->isVisible())
            return d;
    throw std::runtime_error("Aucun dialogue visible");
}

QString texte_visible(QWidget &racine)
{
    QStringList texte;
    for (auto *l : racine.findChildren<QLabel *>())
        if (l->isVisible()) texte << l->text();
    return texte.join(u'\n');
}

void choisir(QComboBox *combo, const QString &prefixe)
{
    const int i = combo->findText(prefixe, Qt::MatchStartsWith);
    exige(i >= 0, u"Choix introuvable : "_s + prefixe);
    combo->showPopup();
    auto *vue = combo->view();
    vue->setCurrentIndex(combo->model()->index(i, combo->modelColumn(), combo->rootModelIndex()));
    QTest::keyClick(vue, Qt::Key_Return);
    actualise();
    exige(combo->currentIndex() == i, u"Sélection de "_s + prefixe);
}

class DepotEleve : public Store {
public:
    DepotEleve() { prefs = Prefs{}; prefs.modeSimple = true; }
};

class Poste {
public:
    DepotEleve store;
    MainWindow win{store};
    QTemporaryDir dossier;

    Poste()
    {
        exige(dossier.isValid(), u"Création du dossier de TP"_s);
        win.resize(1100, 700);
        win.show();
        actualise();
    }

    void menu(const QString &rubrique, const QString &libelle)
    {
        for (auto *a : win.menuBar()->actions()) {
            if (!a->isVisible() || a->text().remove(u'&') != rubrique) continue;
            QMenu *m = a->menu();
            Q_EMIT m->aboutToShow();
            for (auto *item : m->actions())
                if (item->isEnabled() && item->text().remove(u'&') == libelle) {
                    item->trigger();
                    actualise();
                    return;
                }
        }
        throw std::runtime_error((rubrique + u" › "_s + libelle).toStdString());
    }

    void deux_colonnes(const QString &x, const QString &ux, const QString &y, const QString &uy)
    {
        clique(win, u"Nouveau tableur"_s);
        auto *d = dialogue();
        const auto edits = d->findChildren<QLineEdit *>();
        exige(edits.size() == 4, u"Noms et unités du premier tableur"_s);
        const QStringList valeurs{x, ux, y, uy};
        for (int i = 0; i < 4; ++i) edits[i]->setText(valeurs[i]);
        clique(*d, u"Créer le tableur"_s);
    }

    TableGrid *grille()
    {
        for (auto *g : win.findChildren<TableGrid *>())
            if (g->isVisible()) return g;
        throw std::runtime_error("Aucun tableur visible");
    }

    void colle(const QString &texte, int ligne = 0, int colonne = 0)
    {
        auto *g = grille();
        const QModelIndex index = g->model()->index(ligne, colonne);
        g->scrollTo(index);
        QTest::mouseClick(g->viewport(), Qt::LeftButton, {}, g->visualRect(index).center());
        QApplication::clipboard()->setText(texte);
        QTest::keyClick(g, Qt::Key_V, Qt::ControlModifier);
        actualise();
    }

    void etape(int i)
    {
        auto *tabs = champ<QTabWidget>(win, u"etapesTP"_s);
        QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, {}, tabs->tabBar()->tabRect(i).center());
        exige(tabs->currentIndex() == i, u"Changement d’étape"_s);
        actualise();
    }

    void trace()
    {
        etape(1);
        clique(win, u"Tracer les mesures"_s);
        exige(!store.ws.graphs.empty(), u"Graphe attendu"_s);
    }

    const Grandeur &q(const QString &nom) const
    {
        const auto *res = store.ws.doc.par_nom(nom);
        exige(res, u"Grandeur absente : "_s + nom);
        return *res;
    }

    QString sortie(const QString &nom) const
    {
        return QDir(qEnvironmentVariable("MEVREG_TP_SORTIE", dossier.path())).filePath(nom);
    }

    void fichier(const QString &path, const std::function<void()> &ouvre)
    {
        bool choisi = false;
        QTimer::singleShot(0, &win, [&] {
            if (auto *f = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                f->setDirectory(QFileInfo(path).absolutePath());
                f->selectFile(QFileInfo(path).fileName());
                choisi = QMetaObject::invokeMethod(f, "accept", Qt::DirectConnection);
                if (f->isVisible()) f->reject();
            }
        });
        ouvre();
        actualise();
        exige(choisi, u"Choix du fichier : "_s + path);
    }
};

template<class F> void seance(F f)
{
    try {
        Poste poste;
        f(poste);
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }
}

} // namespace

class TestSeances : public QObject {
    Q_OBJECT
private:
    Prefs avant;
private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        assure_ressources_coeur();
        theme::initialise();
        avant = Prefs::charge();
        help::openUrl = [](const QUrl &) { return true; };
    }

    void cleanupTestCase() { avant.enregistre(); }

    void collage_mesure_absente_et_unites()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"I"_s, u"A"_s, u"U"_s, u"V"_s);
            p.colle(u"0\tabsent\n0,1\t4,7\n0,2\t9,4"_s);
            const auto &xs = p.q(u"I"_s).values, &ys = p.q(u"U"_s).values;
            QCOMPARE(xs, (std::vector<double>{0, 0.1, 0.2}));
            QCOMPARE(ys.size(), size_t(3));
            QVERIFY(std::isnan(ys[0]));
            QCOMPARE(ys[2], 9.4);
            p.colle(u"U (mV)\n4700\n9400"_s, 1, 1);
            QCOMPARE(p.q(u"U"_s).valeur(1), 4.7);
            QVERIFY(texte_visible(p.win).contains(u"mV"_s));
        });
    }

    void etalonnage_et_compte_rendu()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"C"_s, u"µmol/L"_s, u"A"_s, {});
            p.colle(u"0\t0,01\n10\t0,11\n20\t0,21\n30\t0,31\n40\t0,41"_s);
            p.trace();
            p.etape(2);
            choisir(champ<QComboBox>(p.win, u"modeleGuide"_s), u"Affine"_s);
            clique(p.win, u"Ajuster aux mesures"_s);
            const auto &r = p.store.ws.graphs.front().plots.front().fits.front().result;
            QVERIFY(r && r->n == 5);
            QVERIFY(std::abs((0.26 - r->params[1]) / r->params[0] - 25) < 1e-9);
            p.etape(3);
            const QString conclusion = u"Une absorbance de 0,26 correspond à 25 µmol/L."_s;
            champ<QPlainTextEdit>(p.win, u"conclusionTP"_s)->setPlainText(conclusion);
            const QString path = p.sortie(u"Étalonnage.phy"_s);
            p.fichier(path, [&] { clique(p.win, u"Enregistrer la séance…"_s); });
            QVERIFY(QFileInfo::exists(path));
            const QString pdf = p.sortie(u"Étalonnage.pdf"_s);
            p.fichier(pdf, [&] { clique(p.win, u"Compte rendu court…"_s); });
            QFile rapport(pdf);
            QVERIFY(rapport.open(QIODevice::ReadOnly) && rapport.read(5) == "%PDF-");
            p.fichier(path, [&] { p.menu(u"Fichier"_s, u"Ouvrir…"_s); });
            QCOMPARE(p.store.ws.meta.notes, conclusion);
            QCOMPARE(p.q(u"C"_s).unit, u"µmol/L"_s);
            QCOMPARE(p.store.ws.graphs.front().plots.front().fits.size(), size_t(1));
        });
    }

    void loi_ohm_depuis_fichier()
    {
        seance([](Poste &p) {
            const QString csv = p.dossier.filePath(u"Résistance.csv"_s);
            QVERIFY(QFile::copy(u":/help/resistance.csv"_s, csv));
            p.fichier(csv, [&] { p.menu(u"Fichier"_s, u"Importer un fichier CSV…"_s); });
            clique(*dialogue(), u"Importer"_s);
            QCOMPARE(p.q(u"I"_s).values, (std::vector<double>{0, 0.1, 0.2, 0.3}));
            QCOMPARE(p.q(u"U"_s).unit, u"V"_s);
            p.etape(3);
            QVERIFY(!bouton(p.win, u"Copier le graphe"_s)->isEnabled());
            QCOMPARE(champ<QComboBox>(p.win, u"tableRapport"_s)->currentText(), u"Résistance"_s);
            p.trace();
            p.etape(0);
            auto *g = p.grille();
            QTest::mouseClick(g->viewport(), Qt::LeftButton, {}, g->visualRect(g->model()->index(0, 1)).center());
            clique(p.win, u"Incertitude…"_s);
            auto *d = dialogue();
            choisir(d->findChild<QComboBox *>(), u"Lecture sur une graduation"_s);
            d->findChild<QLineEdit *>()->setText(u"0,1"_s);
            clique(*d, u"Créer"_s);
            const auto u = p.q(u"U"_s).uncertainty;
            QVERIFY(u);
            QVERIFY(std::abs(p.store.ws.doc.obtient(*u)->valeur(3) - 0.1 / std::sqrt(12.)) < 1e-10);
            p.etape(2);
            clique(p.win, u"Ajuster aux mesures"_s);
            const auto &fit = p.store.ws.graphs.front().plots.front().fits.front();
            QVERIFY(fit.result && std::abs(fit.result->params[0] - 47) < 1e-9);
            QVERIFY(texte_visible(p.win).contains(u"Ω"_s));
            p.etape(3);
            clique(p.win, u"Copier le graphe"_s);
            QVERIFY(!QApplication::clipboard()->image().isNull());
            champ<QPlainTextEdit>(p.win, u"conclusionTP"_s)->setPlainText(u"La résistance vaut 47 Ω."_s);
            const QString path = p.sortie(u"resistance.phy"_s);
            p.fichier(path, [&] { clique(p.win, u"Enregistrer la séance…"_s); });
            QVERIFY(QFileInfo(path).size() > 0);
        });
    }

    void choisir_les_sources_du_rapport()
    {
        seance([](Poste &p) {
            p.win.resize(1024, 640);
            p.deux_colonnes(u"t"_s, u"s"_s, u"T"_s, u"°C"_s);
            p.colle(u"0\t20\n1\t22\n2\t24"_s);
            p.trace();
            p.etape(0);
            clique(p.win, u"Nouveau tableur…"_s);
            auto *d = dialogue();
            const auto noms = d->findChildren<QLineEdit *>();
            QCOMPARE(noms.size(), 4);
            noms[0]->setText(u"I"_s); noms[1]->setText(u"A"_s);
            noms[2]->setText(u"U"_s); noms[3]->setText(u"V"_s);
            clique(*d, u"Créer le tableur"_s);
            p.colle(u"0\t0\n0,1\t4,7"_s);
            p.trace();
            p.etape(3);
            auto *tables = champ<QComboBox>(p.win, u"tableRapport"_s);
            auto *graphes = champ<QComboBox>(p.win, u"grapheRapport"_s);
            QCOMPARE(tables->count(), 2);
            QCOMPARE(graphes->count(), 2);
            QCOMPARE(tables->currentIndex(), 1);
            QCOMPARE(graphes->currentIndex(), 1);
            choisir(tables, tables->itemText(0));
            choisir(graphes, graphes->itemText(0));
            champ<QPlainTextEdit>(p.win, u"conclusionTP"_s)->setPlainText(u"Échauffement de 4 °C en 2 s."_s);
            const QString pdf = p.sortie(u"Échauffement.pdf"_s);
            p.fichier(pdf, [&] { clique(p.win, u"Compte rendu court…"_s); });
            QVERIFY(QFileInfo(pdf).size() > 1000);
            p.etape(0);
            QCOMPARE(champ<QComboBox>(p.win, u"tableGuide"_s)->currentIndex(), 0);
            p.etape(2);
            QCOMPARE(champ<QComboBox>(p.win, u"grapheGuide"_s)->currentIndex(), 0);
            auto *page = champ<QTabWidget>(p.win, u"etapesTP"_s)->currentWidget();
            auto *zone = qobject_cast<QScrollArea *>(page);
            QVERIFY(zone);
            auto *bas = champ<QCheckBox>(*page, u"residusGuide"_s);
            zone->ensureWidgetVisible(bas);
            QVERIFY(zone->viewport()->rect().contains(bas->mapTo(zone->viewport(), bas->rect().center())));
        });
    }

    void charge_condensateur_au_clavier()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"t"_s, u"ms"_s, u"U"_s, u"V"_s);
            QStringList lignes;
            for (int i = 0; i <= 40; ++i) {
                const double t = i / 2.;
                lignes << QString::number(t) + u'\t' + QString::number(5 * (1 - std::exp(-t / 3.)), 'g', 12);
            }
            p.colle(lignes.join(u'\n'));
            p.trace();
            p.menu(u"Mode"_s, u"Expert"_s);
            p.menu(u"Analyse"_s, u"Modélisation"_s);
            auto *panneau = ModelingPanel::instance();
            QVERIFY(panneau);
            QTRY_VERIFY(panneau->findChild<QWidget *>(u"choixModele"_s));
            auto *choix = champ<QWidget>(*panneau, u"choixModele"_s);
            QVERIFY(choix->focusPolicy() & Qt::TabFocus);
            choix->setFocus(Qt::TabFocusReason);
            QTest::keyClick(choix, Qt::Key_Home);
            for (const auto &modele : fit::ModelKind::catalogue()) {
                if (modele.type == fit::ModelKind::Charge) break;
                QTest::keyClick(choix, Qt::Key_Right);
            }
            QVERIFY(choix->accessibleDescription().contains(u"Montée exponentielle"_s));
            QTest::keyClick(choix, Qt::Key_Return);
            QTRY_VERIFY(!p.store.ws.graphs.front().plots.front().fits.empty());
            const auto &fit = p.store.ws.graphs.front().plots.front().fits.front();
            QCOMPARE(fit.kind.type, fit::ModelKind::Charge);
            QVERIFY(fit.result);
            QVERIFY(std::abs(fit.result->params.at(2) - 3) < 1e-5);
            QTRY_VERIFY(texte_visible(*panneau).contains(u"Paramètres"_s));
            clique(*panneau, u"Copier les résultats"_s);
            const auto texte = QApplication::clipboard()->text();
            QVERIFY2(texte.section(u"τ = "_s, 1).section(u'\n', 0, 0).contains(u"ms"_s), qPrintable(texte));
            QVERIFY2(texte.section(u"A = "_s, 1).section(u'\n', 0, 0).contains(u"V"_s), qPrintable(texte));
            clique(*panneau, u"Créer la grandeur modèle"_s);
            QCOMPARE(p.q(u"U_mod"_s).unit, u"V"_s);
            QVERIFY(std::abs(p.q(u"U_mod"_s).valeur(6) - 5 * (1 - std::exp(-1.))) < 1e-6);
        });
    }

    void mouvement_vertical()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"t"_s, u"s"_s, u"z"_s, u"m"_s);
            QStringList lignes;
            for (int i = 0; i <= 10; ++i) {
                const double t = i / 10.;
                lignes << QString::number(t) + u'\t' + QString::number(4.9*t*t);
            }
            p.colle(lignes.join(u'\n'));
            p.trace();
            p.etape(2);
            choisir(champ<QComboBox>(p.win, u"modeleGuide"_s), u"Parabole"_s);
            clique(p.win, u"Ajuster aux mesures"_s);
            const auto &r = p.store.ws.graphs.front().plots.front().fits.front().result;
            QVERIFY(r && std::abs(2*r->params[0] - 9.8) < 1e-9);
            p.menu(u"Mode"_s, u"Expert"_s);
            for (const QString &action : {u"Dérivée…"_s, u"Dérivée seconde…"_s}) {
                p.menu(u"Analyse"_s, action);
                clique(*dialogue(), u"Calculer"_s);
            }
            const auto &v = p.q(u"dz_dt"_s), &a = p.q(u"d2z_dt2"_s);
            QVERIFY(std::abs(v.valeur(5) - 4.9) < 1e-9);
            QVERIFY(std::abs(a.valeur(5) - 9.8) < 1e-9);
            QCOMPARE(v.unit, u"m/s"_s);
            p.menu(u"Analyse"_s, u"Intégrale…"_s);
            auto *d = dialogue();
            choisir(d->findChildren<QuantityCombo *>().front(), u"dz_dt"_s);
            clique(*d, u"Créer la primitive"_s);
            QVERIFY(std::abs(p.q(u"int_dz_dt"_s).valeur(10) - 4.9) < 1e-9);
        });
    }

    void energie_cinetique_et_python()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"t"_s, u"s"_s, u"v"_s, u"m/s"_s);
            p.colle(u"0\t0\n1\t2\n2\t4"_s);
            p.menu(u"Mode"_s, u"Expert"_s);
            p.menu(u"Données"_s, u"Nouveau paramètre…"_s);
            auto *d = dialogue();
            auto editions = d->findChildren<QLineEdit *>();
            QCOMPARE(editions.size(), 3);
            editions[0]->setText(u"m"_s);
            editions[1]->setText(u"kg"_s);
            QTest::mouseClick(editions[2], Qt::LeftButton);
            QTest::keyClick(editions[2], Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(editions[2], "0,5");
            clique(*d, u"Créer"_s);
            p.menu(u"Mode"_s, u"Simplifié"_s);
            p.etape(0);
            clique(p.win, u"Formule"_s);
            d = dialogue();
            editions = d->findChildren<QLineEdit *>();
            QCOMPARE(editions.size(), 3);
            editions[0]->setText(u"Ec"_s);
            editions[1]->setText(u"J"_s);
            editions[2]->setText(u"0,5*m*v^2"_s);
            clique(*d, u"Créer"_s);
            QCOMPARE(p.q(u"Ec"_s).values, (std::vector<double>{0, 1, 4}));
            p.menu(u"Mode"_s, u"Expert"_s);
            p.menu(u"Python"_s, u"Nouveau script"_s);
            auto *script = p.win.findChild<ScriptPanel *>();
            QVERIFY(script);
            auto *ed = script->editeur();
            auto *console = script->findChild<QTextBrowser *>();
            QVERIFY(console);
            ed->setFocus();
            QTest::keyClick(ed, Qt::Key_A, Qt::ControlModifier);
            QApplication::clipboard()->setText(u"import numpy as np\nimport mevreg as mr\n"
                "val = np.max(mr.valeurs('Ec'))\nprint(f'Énergie maximale = {val:g} J')"_s);
            QTest::keyClick(ed, Qt::Key_V, Qt::ControlModifier);
            QTest::keyClick(ed, Qt::Key_F5);
            QTRY_VERIFY_WITH_TIMEOUT(console->toPlainText().contains(u"Énergie maximale = 4 J"_s), 20000);
            // Une erreur de saisie dans le script affiche la ligne en cause et conserve le tableur.
            ed->appendPlainText(u"1 / 0"_s);
            QTest::keyClick(ed, Qt::Key_Return, Qt::ControlModifier);
            QTRY_VERIFY_WITH_TIMEOUT(console->toPlainText().contains(u"ZeroDivisionError"_s), 20000);
            QCOMPARE(p.q(u"Ec"_s).values, (std::vector<double>{0, 1, 4}));
        });
    }

    void periode_et_incertitude_type_a()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"essai"_s, {}, u"T"_s, u"s"_s);
            p.colle(u"1\t1,98\n2\t2,02\n3\t\n4\t2,00\n5\t2,00"_s);
            p.etape(2);
            clique(p.win, u"Statistiques…"_s);
            auto *d = dialogue();
            const QString lecture = texte_visible(*d);
            QVERIFY2(lecture.contains(u"0,0082"_s) && lecture.contains(u"2,000"_s), qPrintable(lecture));
            clique(*d, u"Fermer"_s);
            p.menu(u"Mode"_s, u"Expert"_s);
            p.menu(u"Analyse"_s, u"Statistiques"_s);
            auto *stats = p.win.findChild<StatsPanel *>();
            QVERIFY(stats);
            choisir(stats->findChild<QComboBox *>(), u"T"_s);
            QVERIFY2(texte_visible(*stats).contains(u"0,0082"_s), qPrintable(texte_visible(*stats)));
            clique(*stats, u"classes auto"_s);
            stats->findChild<QSpinBox *>()->setValue(2);
            QCOMPARE(stats->classes(), std::optional<size_t>(2));
            const auto h = trace_histogramme(p.store.ws.doc, p.q(u"T"_s).id, 2, p.store.style());
            QVERIFY(h && !h->data.empty());
            auto *exp = qobject_cast<QToolButton *>(bouton(*stats, u"Exporter"_s));
            QVERIFY(exp && exp->menu());
            const QString svg = p.sortie(u"Périodes.svg"_s);
            auto *action = exp->menu()->actions().at(1);
            QVERIFY(action->text().contains(u"SVG"_s));
            p.fichier(svg, [&] { action->trigger(); });
            QFile histogramme(svg);
            QVERIFY(histogramme.open(QIODevice::ReadOnly));
            const auto image = histogramme.readAll();
            QVERIFY(image.contains("<svg") && image.contains("Histogramme"));
        });
    }

    void pictogrammes_du_parcours()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"I"_s, u"A"_s, u"U"_s, u"V"_s);
            auto *tabs = champ<QTabWidget>(p.win, u"etapesTP"_s);
            for (int i = 0; i < tabs->count(); ++i)
                QVERIFY2(!tabs->tabIcon(i).pixmap(24, 24).isNull(), qPrintable(tabs->tabText(i)));
            for (auto *b : tabs->findChildren<QPushButton *>())
                QVERIFY2(!b->icon().pixmap(24, 24).isNull(), qPrintable(b->text()));
            clique(p.win, u"Formule"_s);
            auto *d = dialogue();
            for (auto *b : d->findChildren<QPushButton *>())
                QVERIFY2(!b->icon().pixmap(24, 24).isNull(), qPrintable(b->text()));
            clique(*d, u"Annuler"_s);
        });
    }

    void circuit_rc_simplifie_data()
    {
        QTest::addColumn<bool>("charge");
        QTest::newRow("charge") << true;
        QTest::newRow("decharge") << false;
    }

    void circuit_rc_simplifie()
    {
        QFETCH(bool, charge);
        seance([charge](Poste &p) {
            p.deux_colonnes(u"t"_s, u"ms"_s, u"Uc"_s, u"V"_s);
            QStringList mesures;
            for (int t = 0; t <= 20; ++t) {
                const double reste = std::exp(-t / 3.);
                mesures << QString::number(t) + u'\t' + QString::number(5 * (charge ? 1 - reste : reste), 'g', 12);
            }
            p.colle(mesures.join(u'\n'));
            p.trace();
            p.etape(2);
            auto *modele = champ<QComboBox>(p.win, u"modeleGuide"_s);
            QCOMPARE(modele->currentData().toInt(), fit::ModelKind::Linear);
            choisir(modele, charge ? u"Montée exponentielle"_s : u"Décroissance exponentielle"_s);
            QVERIFY(bouton(p.win, u"Tangentes parallèles…"_s)->isEnabled());
            clique(p.win, u"Ajuster aux mesures"_s);
            const auto &ajustement = p.store.ws.graphs.front().plots.front().fits.front();
            QVERIFY(ajustement.result);
            QVERIFY(std::abs(ajustement.result->params.at(2) - 3) < 1e-5);
            QVERIFY(texte_visible(p.win).section(u"τ = "_s, 1).section(u'\n', 0, 0).contains(u"ms"_s));
            const QString fichier = p.dossier.filePath(u"circuit.phy"_s);
            QVERIFY(p.win.enregistre(false, fichier));
            // Les anciens documents peuvent contenir un profil de TP qui ne correspond pas aux mesures.
            QFile ancien(fichier);
            QVERIFY(ancien.open(QIODevice::ReadOnly));
            auto racine = QJsonDocument::fromJson(ancien.readAll()).object();
            ancien.close();
            auto espace = racine.value(u"workspace"_s).toObject();
            QVERIFY(!espace.contains(u"guide_activity"_s));
            espace.insert(u"guide_activity"_s, u"dosage"_s);
            racine.insert(u"workspace"_s, espace);
            QVERIFY(ancien.open(QIODevice::WriteOnly | QIODevice::Truncate));
            ancien.write(QJsonDocument(racine).toJson());
            ancien.close();
            QVERIFY(p.win.ouvre_chemin(fichier));
            p.etape(2);
            QVERIFY(!p.win.findChild<QComboBox *>(u"activiteGuide"_s));
            QCOMPARE(p.store.ws.graphs.front().plots.front().fits.size(), size_t(1));
            QCOMPARE(modele->currentData().toInt(), charge ? fit::ModelKind::Charge : fit::ModelKind::Discharge);
        });
    }

    void autres_modeles_usuels_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("constante") << int(fit::ModelKind::Constant);
        QTest::newRow("puissance") << int(fit::ModelKind::Power);
        QTest::newRow("exponentielle") << int(fit::ModelKind::ExpX);
        QTest::newRow("logarithme") << int(fit::ModelKind::Ln);
        QTest::newRow("sinusoide") << int(fit::ModelKind::Sine);
    }

    void autres_modeles_usuels()
    {
        QFETCH(int, type);
        seance([type](Poste &p) {
            p.deux_colonnes(u"x"_s, {}, u"y"_s, {});
            QStringList mesures;
            for (int n = 1; n <= 40; ++n) {
                const double x = n / 4.;
                double y = 3;
                switch (type) {
                case fit::ModelKind::Power: y = 2 * std::pow(x, 1.7); break;
                case fit::ModelKind::ExpX: y = 3 * std::exp(-0.25 * x); break;
                case fit::ModelKind::Ln: y = 2 * std::log(x) + 3; break;
                case fit::ModelKind::Sine: y = 2 * std::sin(2 * std::numbers::pi * x / 4) + 3; break;
                }
                mesures << QString::number(x) + u'\t' + QString::number(y, 'g', 13);
            }
            p.colle(mesures.join(u'\n'));
            p.trace();
            p.etape(2);
            auto *modeles = champ<QComboBox>(p.win, u"modeleGuide"_s);
            const int indice = modeles->findData(type);
            QVERIFY(indice >= 0);
            choisir(modeles, modeles->itemText(indice));
            clique(p.win, u"Ajuster aux mesures"_s);
            const auto &f = p.store.ws.graphs.front().plots[0].fits.at(0);
            QVERIFY2(f.result, qPrintable(f.resultError));
            const auto modele = fit::Model::fabrique(f.kind, u"x"_s, {}, nullptr);
            QVERIFY(modele);
            for (size_t n = 0; n < 40; ++n)
                QVERIFY(std::abs(modele->evalue1(f.result->params, p.q(u"x"_s).valeur(n)) - p.q(u"y"_s).valeur(n)) < 1e-6);
        });
    }

    void analyses_sur_la_courbe_choisie()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"a"_s, u"mm"_s, u"b"_s, u"V"_s);
            p.colle(u"0\t3\n1\t5\n2\t7\n3\t9\n4\t11"_s);
            p.trace();
            const ViewId gid = p.store.ws.graphs.front().id;
            const QId x = p.q(u"a"_s).id, y = p.q(u"b"_s).id;
            QId carre = 0;
            p.store.edite([&](Seance &ws) {
                carre = *ws.doc.ajoute_calculee(u"surface"_s, u"mm^2"_s, u"a^2"_s);
                ws.tables.front().columns.push_back(carre);
                auto *g = ws.graphe_modifiable(gid);
                g->rows = 2;
                g->plots.resize(2);
                g->plots[1].ajoute_courbe(x, carre);
            });
            p.etape(2);
            auto *courbes = champ<QComboBox>(p.win, u"courbeGuide"_s);
            auto *modeles = champ<QComboBox>(p.win, u"modeleGuide"_s);
            QCOMPARE(courbes->count(), 2);
            QVERIFY(modeles->findData(fit::ModelKind::Linear) >= 0 && modeles->findData(fit::ModelKind::Charge) >= 0);
            clique(p.win, u"Tangentes parallèles…"_s);
            auto *tangentes = dialogue();
            QVERIFY(!bouton(*tangentes, u"Tracer sur le graphe"_s)->isEnabled());
            QVERIFY(texte_visible(*tangentes).contains(u"pente"_s));
            clique(*tangentes, u"Annuler"_s);
            QVERIFY(p.store.ws.graphe(gid)->plots.front().tangents.empty());

            clique(p.win, u"Dérivée…"_s);
            clique(*dialogue(), u"Calculer"_s);
            QCOMPARE(p.q(u"db_da"_s).values, (std::vector<double>{2, 2, 2, 2, 2}));
            p.win.ouvre_onglet({TabKey::Graph, gid});
            p.etape(2);
            choisir(courbes, u"Panneau 2"_s);
            choisir(modeles, u"Parabole"_s);
            clique(p.win, u"Ajuster aux mesures"_s);
            const auto *g = p.store.ws.graphe(gid);
            QVERIFY(g->plots[0].fits.empty());
            const auto &f = g->plots[1].fits.at(0);
            QVERIFY(f.result && f.y == carre && std::abs(f.result->params[0] - 1) < 1e-9);
            clique(p.win, u"Statistiques…"_s);
            auto *stats = dialogue()->findChild<StatsPanel *>();
            QVERIFY(stats && stats->grandeur_choisie() == carre);
            clique(*dialogue(), u"Fermer"_s);
            clique(p.win, u"Intégrale…"_s);
            clique(*dialogue(), u"Créer la primitive"_s);
            const auto &primitive = p.q(u"int_surface"_s);
            QVERIFY(primitive.formula && primitive.formula->contains(u"surface"_s));
            clique(p.win, u"Dérivée…"_s);
            auto *d = dialogue();
            choisir(champ<QComboBox>(*d, u"ordreDerivee"_s), u"Dérivée seconde"_s);
            clique(*d, u"Calculer"_s);
            const auto &derivee = p.q(u"d2surface_da2"_s);
            QVERIFY(std::abs(derivee.valeur(2) - 2) < 1e-9);
            QCOMPARE(p.store.ws.graphs.back().plots[0].curves.front().x, x);
            QCOMPARE(p.store.ws.graphs.back().plots[0].curves.front().y, derivee.id);
            QCOMPARE(p.store.ws.graphe(gid)->plots[0].curves.front().y, y);
        });
    }

    void courbe_en_s_sur_deux_axes()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"position"_s, u"mm"_s, u"signal"_s, u"V"_s);
            QStringList mesures;
            for (int i = 0; i <= 20; ++i)
                mesures << QString::number(i) + u'\t' + QString::number(3 + 2 * std::tanh((i - 10) / 1.5), 'g', 12);
            p.colle(mesures.join(u'\n'));
            p.trace();
            const ViewId gid = p.store.ws.graphs.front().id;
            const QId x = p.q(u"position"_s).id, y = p.q(u"signal"_s).id;
            p.store.edite([&](Seance &ws) {
                auto *g = ws.graphe_modifiable(gid);
                g->rows = 2;
                g->plots.resize(2);
                g->plots[1].ajoute_courbe(x, y);
                g->plots[1].ajoute_courbe(x, y).side = YSide::Right;
            });
            p.etape(2);
            auto *courbes = champ<QComboBox>(p.win, u"courbeGuide"_s);
            auto *modeles = champ<QComboBox>(p.win, u"modeleGuide"_s);
            QCOMPARE(courbes->count(), 3);
            const QString gauche = courbes->itemText(1), droite = courbes->itemText(2);
            QVERIFY(droite.contains(u"axe droit"_s));
            for (const auto &nom : {gauche, droite}) {
                choisir(courbes, nom);
                choisir(modeles, u"Affine"_s);
                clique(p.win, u"Ajuster aux mesures"_s);
                clique(p.win, u"Tangentes parallèles…"_s);
                auto *d = dialogue();
                QVERIFY2(texte_visible(*d).contains(u"position = 10 mm"_s), qPrintable(texte_visible(*d)));
                clique(*d, u"Tracer sur le graphe"_s);
                p.etape(2);
            }
            const auto &panneau = p.store.ws.graphe(gid)->plots[1];
            QCOMPARE(panneau.fits.size(), size_t(2));
            QCOMPARE(panneau.tangents.size(), size_t(2));
            QCOMPARE(panneau.fits[0].side, YSide::Left);
            QCOMPARE(panneau.fits[1].side, YSide::Right);
            QCOMPARE(panneau.tangents[0].side, YSide::Left);
            QCOMPARE(panneau.tangents[1].side, YSide::Right);
            choisir(courbes, droite);
            clique(p.win, u"Maximum de pente…"_s);
            auto *d = dialogue();
            QVERIFY2(texte_visible(*d).contains(u"position = 10 mm"_s), qPrintable(texte_visible(*d)));
            clique(*d, u"Appliquer"_s);
            const auto *g = p.store.ws.graphe(gid);
            QVERIFY(g->plots[0].tangents.empty() && g->plots[0].fits.empty());
            QCOMPARE(g->plots[0].curves.size(), size_t(1));
            const auto &derivee = g->plots[1].curves.back();
            QCOMPARE(derivee.y, p.q(u"dsignal_dposition"_s).id);
            QCOMPARE(derivee.side, YSide::Left);
            QCOMPARE(derivee.dosage.value_or(0), y);
        });
    }

    void statistiques_depuis_la_colonne()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"essai"_s, {}, u"T"_s, u"s"_s);
            p.colle(u"1\t1,8\n2\t2,0\n3\t2,2"_s);
            auto *g = p.grille();
            QTest::mouseClick(g->viewport(), Qt::LeftButton, {}, g->visualRect(g->model()->index(0, 1)).center());
            auto verifie = [&] {
                auto *d = dialogue();
                auto *stats = d->findChild<StatsPanel *>();
                QVERIFY(stats && stats->grandeur_choisie() == p.q(u"T"_s).id);
                QVERIFY(texte_visible(*stats).contains(u"0,1155"_s));
                clique(*d, u"Fermer"_s);
                QVERIFY(p.store.prefs.modeSimple);
            };
            clique(p.win, u"Statistiques…"_s);
            verifie();
            p.etape(2);
            clique(p.win, u"Statistiques…"_s);
            verifie();
            QCOMPARE(champ<QTabWidget>(p.win, u"etapesTP"_s)->currentIndex(), 2);
        });
    }

    void conversion_de_colonne()
    {
        seance([](Poste &p) {
            p.deux_colonnes(u"t"_s, u"s"_s, u"z"_s, u"m"_s);
            p.colle(u"0\t12\n1\t15\n2\t27"_s);
            p.trace();
            p.etape(0);
            const QId z = p.q(u"z"_s).id;
            auto *entete = p.grille()->horizontalHeader();
            const QPoint pos(entete->sectionViewportPosition(1) + 15, entete->height() / 2);
            bool trouve = false;
            QTimer::singleShot(0, entete, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu) return;
                for (auto *a : menu->actions())
                    if (a->text().startsWith(u"Convertir en grandeur calculée")) {
                        trouve = !a->icon().isNull();
                        menu->close();
                        a->trigger();
                        return;
                    }
                menu->close();
            });
            QContextMenuEvent clic(QContextMenuEvent::Mouse, pos, entete->viewport()->mapToGlobal(pos));
            QApplication::sendEvent(entete->viewport(), &clic);
            QVERIFY(trouve);
            auto *d = dialogue();
            auto *formule = champ<QLineEdit>(*d, u"formuleGrandeur"_s);
            for (const auto &invalide : {QString(), u"inconnue+1"_s, u"z+1"_s}) {
                formule->setText(invalide);
                clique(*d, u"Convertir"_s);
                QVERIFY(d->isVisible());
                QVERIFY(!p.q(u"z"_s).formula);
                QCOMPARE(p.q(u"z"_s).valeur(2), 27.);
            }
            formule->setText(u"2*t+1"_s);
            clique(*d, u"Convertir"_s);
            QCOMPARE(p.q(u"z"_s).id, z);
            QCOMPARE(p.q(u"z"_s).values, (std::vector<double>{1, 3, 5}));
            QCOMPARE(p.store.ws.graphs.front().plots.front().curves.front().y, z);
            p.menu(u"Édition"_s, u"Annuler"_s);
            QVERIFY(!p.q(u"z"_s).formula);
            QCOMPARE(p.q(u"z"_s).values, (std::vector<double>{12, 15, 27}));
            p.menu(u"Édition"_s, u"Rétablir"_s);
            p.colle(u"4"_s, 2, 0);
            QCOMPARE(p.q(u"z"_s).valeur(2), 9.);
            const QString path = p.dossier.filePath(u"formule.phy"_s);
            QVERIFY(p.win.enregistre(false, path) && p.win.ouvre_chemin(path));
            QCOMPARE(p.q(u"z"_s).formula.value_or(QString()), u"2*t+1"_s);
            QCOMPARE(p.q(u"z"_s).valeur(2), 9.);
        });
    }

    void lecture_et_poignee_data()
    {
        QTest::addColumn<bool>("sombre");
        QTest::newRow("clair") << false;
        QTest::newRow("sombre") << true;
    }

    void lecture_et_poignee()
    {
        QFETCH(bool, sombre);
        seance([sombre](Poste &p) {
            p.deux_colonnes(u"t"_s, u"s"_s, u"z"_s, u"m"_s);
            p.colle(u"1\t3\n2\t6"_s);
            theme::choisit(sombre ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            auto *g = p.grille();
            const int anciennePolice = g->fontMetrics().height();
            p.menu(u"Édition"_s, u"Préférences…"_s);
            auto *d = dialogue();
            auto *texte = champ<QSpinBox>(*d, u"texteTableur"_s);
            auto *ligne = champ<QSpinBox>(*d, u"hauteurLigne"_s);
            texte->setValue(180);
            ligne->setValue(64);
            QCOMPARE(g->rowHeight(0), theme::px(64));
            QVERIFY(g->fontMetrics().height() > anciennePolice);
            ligne->setValue(18);
            QVERIFY(g->rowHeight(0) > g->fontMetrics().height());
            QCOMPARE(Prefs::charge().hauteurLigne, 18);
            QCOMPARE(Prefs::charge().texteTableur, 180);
            clique(*d, u"Fermer"_s);
            p.win.activateWindow();
            g->setFocus();
            QTRY_VERIFY(g->hasFocus());
            const QModelIndex mesure = g->model()->index(1, 1);
            QCOMPARE(mesure.data(Qt::TextAlignmentRole).value<Qt::Alignment>(), Qt::Alignment(Qt::AlignCenter));
            g->setCurrentIndex(mesure);
            QTest::keyClick(g, Qt::Key_F2);
            auto *saisie = g->viewport()->findChild<QLineEdit *>();
            QVERIFY(saisie && saisie->isVisible());
            QCOMPARE(saisie->alignment(), Qt::Alignment(Qt::AlignCenter));
            saisie->selectAll();
            QTest::keyClicks(saisie, "-10,5");
            QTest::keyClick(saisie, Qt::Key_Return);
            QCOMPARE(p.q(u"z"_s).valeur(1), -10.5);
            QTest::mouseClick(g->viewport(), Qt::LeftButton, {}, g->visualRect(g->model()->index(0, 0)).center());
            QTest::keyClick(g, Qt::Key_Down, Qt::ShiftModifier);
            QVERIFY(g->hasFocus());
            const int taille = theme::px(7);
            auto rect = [g](int r) { return g->visualRect(g->model()->index(r, 0)); };
            const QPoint poignee(rect(1).right() - 2 * taille + taille / 2, rect(1).bottom() - taille / 2 - 1);
            const QPoint dessous(poignee.x(), rect(1).bottom() + 2);
            auto couleur = [g](QPoint point) {
                const QPixmap vue = g->viewport()->grab();
                return vue.toImage().pixelColor(point * vue.devicePixelRatio());
            };
            const QColor bleu = theme::couleurs_tableur().cursor;
            QCOMPARE(couleur(poignee), bleu);
            QVERIFY(couleur(dessous) != bleu);
            QTest::mousePress(g->viewport(), Qt::LeftButton, {}, poignee);
            QTest::mouseMove(g->viewport(), rect(4).center());
            QTest::mouseRelease(g->viewport(), Qt::LeftButton, {}, rect(4).center());
            QCOMPARE(p.q(u"t"_s).valeur(4), 5.);
            QTest::mouseClick(g->viewport(), Qt::LeftButton, {}, rect(3).center());
            QVERIFY(couleur(poignee) != bleu && couleur(dessous) != bleu);
            g->scrollTo(g->model()->index(35, 0));
            g->scrollTo(g->model()->index(0, 0));
            QVERIFY(couleur(dessous) != bleu);
            const int hauteur = g->rowHeight(0);
            p.menu(u"Mode"_s, u"Expert"_s);
            p.win.ouvre_onglet({TabKey::Table, p.store.ws.tables.front().id});
            actualise();
            QCOMPARE(p.grille()->rowHeight(0), hauteur);
        });
    }

    void dosage_data()
    {
        QTest::addColumn<int>("sens");
        QTest::newRow("saut-croissant") << 1;
        QTest::newRow("saut-decroissant") << -1;
    }

    void dosage()
    {
        QFETCH(int, sens);
        seance([sens](Poste &p) {
            p.deux_colonnes(u"V"_s, u"mL"_s, u"pH"_s, {});
            QStringList lignes;
            for (int i = 0; i <= 20; ++i)
                lignes << QString::number(i) + u'\t' + QString::number(7 + sens * 3 * std::tanh((i-10) / 1.5), 'g', 10);
            p.colle(lignes.join(u'\n'));
            p.trace();
            p.etape(2);
            clique(p.win, u"Tangentes parallèles…"_s);
            clique(*dialogue(), u"Tracer sur le graphe"_s);
            p.etape(2);
            clique(p.win, u"Maximum de pente…"_s);
            auto *d = dialogue();
            QVERIFY2(texte_visible(*d).contains(u"10"_s), qPrintable(texte_visible(*d)));
            clique(*d, u"Appliquer"_s);
            const auto &trace = p.store.ws.graphs.front().plots.front();
            QCOMPARE(trace.tangents.size(), size_t(1));
            QCOMPARE(trace.curves.size(), size_t(2));
            QCOMPARE(trace.curves.back().side, YSide::Right);
            QVERIFY(sens * p.q(u"dpH_dV"_s).valeur(10) > 1.5);
        });
    }

    void signal_du_diapason_data()
    {
        QTest::addColumn<QString>("unite");
        QTest::addColumn<double>("unitesParSeconde");
        QTest::newRow("secondes") << u"s"_s << 1.;
        QTest::newRow("millisecondes") << u"ms"_s << 1000.;
        QTest::newRow("microsecondes") << u"µs"_s << 1e6;
    }

    void signal_du_diapason()
    {
        QFETCH(QString, unite);
        QFETCH(double, unitesParSeconde);
        seance([&](Poste &p) {
            p.deux_colonnes(u"t"_s, unite, u"son"_s, u"V"_s);
            QStringList lignes;
            for (int i = 0; i < 200; ++i) {
                const double t = i / 8000.;
                lignes << QString::number(t * unitesParSeconde, 'g', 12) + u'\t'
                       + QString::number(std::sin(2 * std::numbers::pi * 440 * t), 'g', 12);
            }
            p.colle(lignes.join(u'\n'));
            p.menu(u"Mode"_s, u"Expert"_s);
            p.menu(u"Analyse"_s, u"Spectre (analyse de Fourier)…"_s);
            clique(*dialogue(), u"Calculer le spectre"_s);
            const auto &amp = p.q(u"A_son"_s).values;
            const auto k = std::max_element(amp.begin(), amp.end()) - amp.begin();
            QCOMPARE(p.q(u"f"_s).unit, u"Hz"_s);
            QVERIFY(std::abs(p.q(u"f"_s).valeur(k) - 440) < 1e-8);
            QVERIFY(std::abs(amp[k] - 1) < 1e-8);
        });
    }
};
QTEST_MAIN(TestSeances)
#include "test_seances.moc"
