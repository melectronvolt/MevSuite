// Essais de l'interface : le programme est piloté comme par un élève (clics, glisser, survol), sans
// fenêtre visible (plateforme « offscreen »). Chaque étape est rendue dans `captures/` (dossier de
// compilation).

#include "about.h"
#include "audiodevices.h"
#include "app.h"
#include "crash.h"
#include "settingsdialog.h"
#include "signalplot.h"
#include "signaux.h"
#include "spectrumplot.h"
#include "style.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProgressBar>
#include <QProgressDialog>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScopeGuard>
#include <QShortcut>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <thread>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

using namespace Qt::StringLiterals;

namespace {

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

std::unique_ptr<MainWindow> banc_essai(Lang lang, bool expert)
{
    Settings settings;
    settings.lang = lang;
    settings.expert = expert;
    settings.maximized = false;
    // thème clair imposé : sinon celui du bureau (portail D-Bus), sombre sur certains postes
    settings.theme = Qt::ColorScheme::Light;
    auto w = std::make_unique<MainWindow>(settings, SettingsFiles{}, QString());
    w->resize(1280, 800);
    w->show();
    w->activateWindow();
    if (!QTest::qWaitForWindowExposed(w.get()))
        qWarning("fenêtre non affichée");
    QTest::qWait(50);
    return w;
}

QString dossier_captures()
{
    const QString dir = QDir(QCoreApplication::applicationDirPath()).filePath(u"captures"_s);
    QDir().mkpath(dir);
    return dir;
}

void langues_de_la_notice()
{
    QTest::addColumn<Lang>("lang");
    QTest::newRow("fr") << Lang::Fr;
    QTest::newRow("en") << Lang::En;
}

/// Rendu d'une fenêtre (fenêtre principale, ou fenêtre des réglages seule) dans `captures/`.
void capture(QWidget *w, const QString &name)
{
    QTest::qWait(30);
    const QImage image = w->grab().toImage();
    const QString chemin = QDir(dossier_captures()).filePath(name + u".png"_s);
    QVERIFY2(image.save(chemin), qPrintable(chemin));
}

/// Attend la fin de l'analyse (calculée dans un fil de fond).
bool attend_analyse(MainWindow &w)
{
    for (int i = 0; i < 500; ++i) {
        QCoreApplication::processEvents();
        if (!w.analyseur().en_attente()) {
            QTest::qWait(20);
            return true;
        }
        QTest::qWait(10);
    }
    return false;
}

void charge(MainWindow &w, double frequence, std::initializer_list<int> rangs = {1, 2, 3, 4, 5, 6},
            int cadence = int(essais::RATE))
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(u"signal-%1.wav"_s.arg(frequence));
    std::vector<std::pair<double, double>> composantes;
    for (int rang : rangs)
        composantes.emplace_back(rang * frequence, 0.4 / rang);
    QVERIFY(audio::ecrit_wav(path, cadence, {essais::sons_purs(composantes, 3.0, cadence)}));
    const quint64 precedent = w.session().loaded ? w.session().loaded->id : 0;
    w.charge_chemin(path);
    QTRY_VERIFY(w.session().loaded && w.session().loaded->id != precedent);
    QVERIFY(attend_analyse(w));
}

void souris(QWidget *widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void survol(QWidget *widget, QPointF pos)
{
    souris(widget, QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

/// La souris quitte le graphique : plus de réticule.
void quitte(QWidget *widget)
{
    QEvent event(QEvent::Leave);
    QApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}

/// Attend que le panneau des mesures montre tout son contenu (il s'élargit en quelques mises en page).
bool panneau_lateral_tient(MainWindow &w)
{
    auto *side = w.findChild<QScrollArea *>(u"sidePanel"_s);
    return QTest::qWaitFor([&] { return side->widget()->width() <= side->viewport()->width(); }, 1000);
}

void glisse(QWidget *widget, QPointF start, QPointF end)
{
    survol(widget, start);
    souris(widget, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 10; ++k)
        souris(widget, QEvent::MouseMove, start + (end - start) * k / 10.0, Qt::NoButton, Qt::LeftButton);
    souris(widget, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

/// Point du signal à l'instant `t`, à mi-hauteur.
QPointF point_du_signal(MainWindow &w, double t)
{
    SignalPlot *plot = w.graphique_signal();
    return {plot->ecran_x(t), plot->cadre_graphique().center().y()};
}

/// Choisit un onglet, comme un clic : mesure de la période ou étude des fréquences.
void montre_onglet(MainWindow &w, Tool tool)
{
    w.findChild<QTabBar *>(u"tabs"_s)->setCurrentIndex(tool == Tool::Period ? 0 : 1);
    QTest::qWait(30);
}

/// Survol du spectre du son de flûte, un peu à droite du pic de 440 Hz : le réticule désigne ce pic.
void survole_pres_du_premier_pic(MainWindow &w)
{
    SpectrumPlot *plot = w.graphique_spectre();
    // L'onglet des fréquences vient parfois de lancer sa première analyse.
    QVERIFY(attend_analyse(w));
    QVERIFY(panneau_lateral_tient(w));
    plot->repaint();
    survol(plot, QPointF(plot->ecran_x(445.0), plot->cadre_graphique().center().y()));
    const auto peak = plot->pic_survole();
    QVERIFY2(peak, "pic le plus proche du réticule");
    QVERIFY2(std::abs(peak->freq - 440.0) < 0.1, qPrintable(QString::number(peak->freq)));
}

/// Vrai si un texte visible (étiquette, bouton, case à cocher) porte exactement `text`.
bool affiche_texte(QWidget *root, const QString &text)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->text() == text && label->isVisibleTo(root))
            return true;
    }
    for (QAbstractButton *button : root->findChildren<QAbstractButton *>()) {
        if (button->text() == text && button->isVisibleTo(root))
            return true;
    }
    return false;
}

/// Vrai si une étiquette visible contient `part`.
bool affiche_partie(QWidget *root, const QString &part)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->text().contains(part) && label->isVisibleTo(root))
            return true;
    }
    return false;
}

/// Page affichée : l'accueil (sans son) ou l'analyse.
bool accueil_affiche(MainWindow &w)
{
    return w.findChild<QWidget *>(u"welcome"_s)->isVisibleTo(&w);
}

/// La barre de menus, une fois mise en page, a la hauteur d'une barre sans les boutons de droite.
bool barre_menus_standard(MainWindow &w)
{
    QMenuBar plain;
    plain.setNativeMenuBar(false);
    plain.addMenu(u"Fichier"_s);
    const int height = plain.sizeHint().height();
    return QTest::qWaitFor([&] { return w.menuBar()->height() == height; }, 1000);
}

QAction *action(MainWindow &w, const QString &text)
{
    for (QAction *a : w.findChildren<QAction *>()) {
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

/// Déclenche une entrée de menu, comme un clic.
void menu(MainWindow &w, const QString &text)
{
    QAction *a = action(w, text);
    QVERIFY2(a && a->isVisible() && a->isEnabled(), qPrintable(text));
    a->trigger();
    QCoreApplication::processEvents();
}

std::vector<size_t> harmoniques_trouvees(const MainWindow &w)
{
    std::vector<size_t> ranks;
    for (const dsp::Harmonic &h : w.harmoniques()) {
        if (h.peak)
            ranks.push_back(h.rank);
    }
    return ranks;
}

/// Mesure sur le signal carré (200 Hz), dans l'onglet de la mesure de la période : le logiciel donne t₁, t₂ et Δt,
/// jamais la période ni la fréquence. `snap` : aimantation laissée active (sinon décochée).
Cursors mesure_sur_signal_carre(MainWindow &w, bool snap)
{
    charge(w, 200, {1, 3, 5, 7, 9});
    const Cursors extract = w.session().extract;
    montre_onglet(w, Tool::Period);
    // aimantation : active par défaut ; décochée en mode expert pour placer les curseurs librement
    if (!snap)
        w.findChild<QCheckBox *>(u"snap"_s)->click();
    w.montre_signal(0.5, 0.52);
    QCoreApplication::processEvents();
    // glisser d'environ 0,5051 s à 0,5151 s, près de deux passages par zéro
    glisse(w.graphique_signal(), point_du_signal(w, 0.5051), point_du_signal(w, 0.5151));
    const auto period = w.session().period;
    if (!period || (*period)[1] <= (*period)[0]) {
        qWarning("curseurs de mesure absents");
        return {0.0, 0.0};
    }
    // l'extrait analysé n'a pas bougé
    if (w.session().extract != extract)
        qWarning("l'extrait a bougé");
    return *period;
}

struct PlayState {
    bool playing;
    bool paused;
    std::optional<double> t;
};

PlayState lecture(MainWindow &w)
{
    return {w.lecteur().en_lecture(), w.lecteur().en_pause(), w.tete_lecture()};
}

/// Attend (en temps réel) que l'état de la lecture vérifie `condition`.
bool attend_lecture(MainWindow &w, const std::function<bool(const PlayState &)> &condition)
{
    for (int i = 0; i < 500; ++i) {
        QTest::qWait(10);
        if (condition(lecture(w)))
            return true;
    }
    return false;
}

void appuie_espace(MainWindow &w)
{
    // raccourci de la fenêtre, comme la touche Espace hors d'un champ de saisie
    for (QShortcut *shortcut : w.findChildren<QShortcut *>()) {
        if (shortcut->key() == QKeySequence(Qt::Key_Space))
            emit shortcut->activated();
    }
    QCoreApplication::processEvents();
}

} // namespace

class UiTests : public QObject {
    Q_OBJECT

signals:
    void pageDemandee(const QUrl &url);

private slots:
    void initTestCase()
    {
        // curseur de texte qui ne clignote pas : les captures sont les mêmes d'une
        // fois à l'autre
        QApplication::setCursorFlashTime(0);
    }

    void notice_dans_la_langue_active_data() { langues_de_la_notice(); }

    void notice_dans_la_langue_active()
    {
        QFETCH(Lang, lang);
        auto w = banc_essai(lang, false);
        QSignalSpy pages(this, &UiTests::pageDemandee);
        // Recevoir l'URL envoyée au navigateur, sans ouvrir de fenêtre extérieure pendant l'essai.
        QDesktopServices::setUrlHandler(u"file"_s, this, "pageDemandee");
        const auto retablit = qScopeGuard([] { QDesktopServices::unsetUrlHandler(u"file"_s); });
        menu(*w, textes(lang).menuManual);
        QTest::keyClick(w.get(), Qt::Key_F1);
        QTRY_COMPARE(pages.count(), 2);
        for (const auto &args : pages) {
            const QFileInfo fichier(args.first().toUrl().toLocalFile());
            QCOMPARE(fichier.fileName(), code_langue(lang) + u".html"_s);
            QVERIFY(fichier.isFile());
            QVERIFY(fichier.dir().exists(u"accueil-%1.png"_s.arg(code_langue(lang))));
        }
    }

    void icones_des_menus_data() { langues_de_la_notice(); }

    void icones_des_menus()
    {
        QFETCH(Lang, lang);
        auto w = banc_essai(lang, true);
        charge(*w, 440);
        montre_onglet(*w, Tool::Extract);
        const Tr &t = textes(lang);
        menu(*w, t.keepSpectrum);
        for (const QString &texte : {t.audioDevices, t.fitAll, t.zoomSelection, t.keepSpectrum, t.removeSpectrum,
                                     t.channelMenu, t.settingsButton, t.modeSimple, t.modeExpert, t.menuManual,
                                     t.menuAbout, t.menuAboutQt}) {
            const QAction *a = action(*w, texte);
            QVERIFY2(a && a->isIconVisibleInMenu() && !a->icon().pixmap(16, 16).isNull(), qPrintable(texte));
        }
        for (const QString &theme : {u"clair"_s, u"sombre"_s}) {
            if (theme == u"sombre"_s)
                w->findChild<QAction *>(u"theme"_s)->trigger();
            for (const QString &titre : {u"Audio"_s, t.menuView, t.menuMode, t.menuHelp}) {
                QMenu *popup = action(*w, titre)->menu();
                popup->popup(w->menuBar()->mapToGlobal(QPoint(0, w->menuBar()->height())));
                capture(popup, u"menu_%1_%2"_s.arg(titre.toLower(), theme) + (lang == Lang::En ? u"_en"_s : QString()));
                popup->hide();
            }
        }
    }

    void ecran_demarrage()
    {
        auto w = banc_essai(Lang::Fr, false);
        // menus ; à leur droite, sur la même ligne : thème et langue ; pas de barre d'outils
        QStringList titles;
        for (QAction *a : w->menuBar()->actions())
            titles << a->text();
        QCOMPARE(titles, (QStringList{u"Fichier"_s, u"Audio"_s, u"Affichage"_s, u"Mode"_s, u"Aide"_s}));
        QVERIFY(w->findChildren<QToolBar *>().isEmpty());
        QWidget *corner = w->menuBar()->cornerWidget(Qt::TopRightCorner);
        QVERIFY(corner && corner->isVisible());
        QList<QToolButton *> cornerButtons = corner->findChildren<QToolButton *>();
        std::ranges::sort(cornerButtons, {}, [](QToolButton *b) { return b->x(); });
        QList<QAction *> cornerActions;
        for (QToolButton *b : cornerButtons) {
            QVERIFY(b->isVisible());
            cornerActions << b->defaultAction();
        }
        // thème et langue ; la notice est dans le menu Aide (F1)
        QCOMPARE(cornerActions, (QList<QAction *>{w->findChild<QAction *>(u"theme"_s), w->findChild<QAction *>(u"language"_s)}));
        QVERIFY(corner->geometry().right() >= w->menuBar()->width() - 8);
        QVERIFY(barre_menus_standard(*w));
        QVERIFY(!action(*w, u"Tout voir"_s)->isEnabled());
        QCOMPARE(about::icone().size(), QSize(256, 256));
        // sans son : la page d'accueil et ses commandes audio ; mode à droite de la
        // barre d'état
        QVERIFY(accueil_affiche(*w));
        QVERIFY(!w->findChild<QPushButton *>(u"welcomeExamples"_s));
        for (QAction *a : w->menuBar()->actions().front()->menu()->actions())
            QVERIFY2(!a->text().contains(u"Exemple"_s), qPrintable(a->text()));
        QVERIFY(affiche_texte(w.get(), u"\u2002Ouvrir un son…"_s));
        QVERIFY(affiche_texte(w.get(), u"Mode simplifié"_s));
        QVERIFY(!w->findChild<QLabel *>(u"soundStatus"_s)->isVisibleTo(w.get()));
        capture(w.get(), u"00_accueil"_s);
        // un son (fabriqué pour l'essai) : l'analyse remplace l'accueil
        charge(*w, 440);
        QVERIFY(!accueil_affiche(*w));
        QVERIFY(action(*w, u"Tout voir"_s)->isEnabled());
        QVERIFY(affiche_partie(w.get(), u"signal-440.wav · 44,1 kHz"_s));
        // deux onglets sur toute la largeur ; d'abord la mesure de la période, sans spectre
        auto *tabs = w->findChild<QTabBar *>(u"tabs"_s);
        QVERIFY(tabs && tabs->isVisible());
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabText(0), u"Mesure de la période"_s);
        QCOMPARE(tabs->tabText(1), u"Étude des fréquences"_s);
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(!w->graphique_spectre()->isVisibleTo(w.get()));
        QVERIFY(affiche_texte(w.get(), u"Mesure d'une période"_s) && !affiche_texte(w.get(), u"Extrait analysé"_s));
        // en haut du panneau de gauche : ouvrir, tout voir ; zoom sur l'extrait pour les fréquences
        auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
        const auto panelButtons = [&] {
            QStringList texts;
            for (QToolButton *b : side->findChildren<QToolButton *>()) {
                if (b->isVisibleTo(w.get()))
                    texts << b->text();
            }
            return texts;
        };
        for (const QString &text : {u"Ouvrir un son…"_s, u"Tout voir"_s})
            QVERIFY2(panelButtons().contains(text), qPrintable(text));
        QVERIFY(!panelButtons().contains(u"🎵 Exemples ▾"_s));
        QVERIFY(!panelButtons().contains(u"Zoom sur l'extrait"_s));
        QVERIFY(!panelButtons().contains(u"Réglages…"_s));
        // au clavier : Ctrl + Tab passe d'une activité à l'autre, et l'onglet reçoit le focus par Tab
        QCOMPARE(tabs->focusPolicy(), Qt::TabFocus);
        QTest::keyClick(w.get(), Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(tabs->currentIndex(), 1);
        QTest::keyClick(w.get(), Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(tabs->currentIndex(), 0);
        montre_onglet(*w, Tool::Extract);
        QVERIFY(w->graphique_spectre()->isVisibleTo(w.get()));
        QVERIFY(affiche_texte(w.get(), u"Extrait analysé"_s) && !affiche_texte(w.get(), u"Mesure d'une période"_s));
        QVERIFY(panelButtons().contains(u"Zoom sur l'extrait"_s));
        // la séparation donne au signal sa part de la hauteur, même affiché après l'accueil
        QList<int> sizes;
        for (QSplitter *split : w->findChildren<QSplitter *>()) {
            if (split->orientation() == Qt::Vertical)
                sizes = split->sizes();
        }
        QCOMPARE(sizes.size(), 2);
        QVERIFY2(std::abs(double(sizes[0]) / (sizes[0] + sizes[1]) - 0.42) < 0.03, qPrintable(u"%1 %2"_s.arg(sizes[0]).arg(sizes[1])));
        // le panneau des mesures garde sa largeur, même affiché après l'accueil
        QVERIFY2(w->findChild<QScrollArea *>(u"sidePanel"_s)->width() >= 290,
                 qPrintable(QString::number(w->findChild<QScrollArea *>(u"sidePanel"_s)->width())));
    }

    /// Le panneau des mesures n'est jamais plus étroit que son contenu : aucun texte rogné à droite,
    /// dans les deux modes, en anglais (textes plus longs), et avec l'interface agrandie.
    void panneau_lateral_jamais_rogne()
    {
        for (const bool expert : {false, true}) {
            auto w = banc_essai(Lang::En, expert);
            w->resize(1400, 800);
            charge(*w, 440);
            mesure_sur_signal_carre(*w, true);
            auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
            // le volet s'élargit au bout de quelques mises en page successives
            const auto fits = [&] {
                return QTest::qWaitFor([&] { return side->widget()->width() <= side->viewport()->width(); }, 1000);
            };
            QVERIFY2(fits(), qPrintable(u"%1 > %2"_s.arg(side->widget()->width()).arg(side->viewport()->width())));
            // agrandissement de l'interface (Ctrl +) jusqu'à 160 % : le contenu dépasse la largeur initiale
            for (int i = 0; i < 6; ++i) {
                for (QAction *a : w->actions()) {
                    if (a->shortcut() == QKeySequence(QKeySequence::ZoomIn))
                        a->trigger();
                }
            }
            const bool fitsZoomed = fits();
            const bool menuBarZoomed = barre_menus_standard(*w);
            const QString widths = u"%1 > %2"_s.arg(side->widget()->width()).arg(side->viewport()->width());
            if (expert)
                capture(w.get(), u"13_en_agrandi"_s);
            // Ctrl 0 : la police de l'application revient à sa taille, pour les essais suivants
            for (QAction *a : w->actions()) {
                if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_0))
                    a->trigger();
            }
            QVERIFY2(fitsZoomed, qPrintable(widths));
            QVERIFY(menuBarZoomed);
        }
    }

    void enregistre_sur_micro_virtuel_data() { langues_de_la_notice(); }

    void enregistre_sur_micro_virtuel()
    {
        if (!qEnvironmentVariableIsSet("MEVSONO_AUDIO_VIRTUEL"))
            QSKIP("tests/audio-virtuel.sh fournit le serveur audio privé");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QFETCH(Lang, lang);
        auto w = banc_essai(lang, false);
        const Tr &t = textes(lang);
        auto *entrees = w->findChild<QComboBox *>(u"audioInput"_s);
        auto *sorties = w->findChild<QComboBox *>(u"audioOutput"_s);
        auto *devices = w->findChild<AudioDevices *>();
        QVERIFY(entrees && sorties && devices);
        QTRY_VERIFY(entrees->findText(u"USB-Mic"_s) >= 0);
        entrees->setCurrentIndex(entrees->findText(u"USB-Mic"_s));
        const auto entree = devices->entree().id();
        const auto sortie = devices->sortie().id();

        auto pactl = [](const QStringList &args) {
            QProcess p;
            p.start(u"pactl"_s, args);
            if (!p.waitForFinished(5000) || p.exitCode() != 0)
                return QString();
            return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        };
        auto brancheSortie = [&] {
            return pactl({u"load-module"_s, u"module-null-sink"_s, u"sink_name=sono_usb"_s,
                          u"sink_properties=device.description=USB-Headset"_s});
        };
        QString casque = brancheSortie();
        QVERIFY(!casque.isEmpty());
        QTRY_VERIFY(sorties->findText(u"USB-Headset"_s) >= 0);
        QCOMPARE(devices->sortie().id(), sortie);
        sorties->setCurrentIndex(sorties->findText(u"USB-Headset"_s));
        devices->actualise();
        QCOMPARE(devices->entree().id(), entree);
        QCOMPARE(sorties->currentText(), u"USB-Headset"_s);
        w->resize(MIN_WINDOW);
        capture(w.get(), u"accueil_audio_"_s + code_langue(lang));

        QTemporaryDir dir;
        const QString signal = dir.filePath(u"440.wav"_s);
        QVERIFY(audio::ecrit_wav(signal, int(essais::RATE), {essais::sons_purs({{440, 0.5}}, 4)}));
        QProcess tonalite;
        tonalite.start(u"paplay"_s, {u"--device=sono_tp"_s, signal});
        QVERIFY(tonalite.waitForStarted());
        bool niveauRecu = false;
        QTimer mesure;
        connect(&mesure, &QTimer::timeout, w.get(), [&] {
            if (auto *d = QApplication::activeModalWidget()) {
                const auto *bar = d->findChild<QProgressBar *>();
                if (!niveauRecu && bar && bar->value() > -40 && affiche_partie(d, u"dBFS"_s)) {
                    niveauRecu = true;
                    capture(d, u"prise_micro_"_s + code_langue(lang));
                }
            }
        });
        mesure.start(50);
        // Arrêter après une seconde et vérifier la prise par le chemin normal de l'interface.
        QTimer::singleShot(200, devices, &AudioDevices::actualise);
        QTimer::singleShot(1200, w.get(), [] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                d->accept();
        });
        menu(*w, t.recordSound);
        mesure.stop();
        QVERIFY(niveauRecu);
        QVERIFY(w->session().loaded);
        // En mode simplifié, ouvrir l'onglet des fréquences déclenche l'analyse de la prise.
        montre_onglet(*w, Tool::Extract);
        QVERIFY(attend_analyse(*w));
        const auto f0 = w->fondamentale_courante();
        QVERIFY(f0 && std::abs(*f0 - 440) < 0.5);
        montre_onglet(*w, Tool::Period);
        QVERIFY(action(*w, t.saveTake)->isEnabled());
        const auto boutons = w->findChildren<QToolButton *>();
        const auto enregistrer = std::ranges::find_if(boutons, [&](auto *b) {
            return b->isVisible() && b->defaultAction() == action(*w, t.saveTake);
        });
        QVERIFY(enregistrer != boutons.end());
        QVERIFY(!(*enregistrer)->icon().isNull());
        QCOMPARE((*enregistrer)->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);

        w->findChild<QAction *>(u"theme"_s)->trigger();
        auto *fichierMenu = w->menuBar()->actions().front()->menu();
        fichierMenu->popup(w->menuBar()->mapToGlobal(QPoint(0, w->menuBar()->height())));
        capture(fichierMenu, lang == Lang::Fr ? u"menu_fichier_prise"_s : u"menu_fichier_prise_en"_s);
        fichierMenu->hide();

        const QString fichier = dir.filePath(u"prise.wav"_s);
        QTimer::singleShot(50, w.get(), [&] {
            if (auto *d = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                d->setDirectory(dir.path());
                d->findChild<QLineEdit *>(u"fileNameEdit"_s)->setText(QFileInfo(fichier).fileName());
                QMetaObject::invokeMethod(d, "accept");
            }
        });
        (*enregistrer)->click();
        QVERIFY(QFileInfo::exists(fichier));
        const auto id = w->session().loaded->id;
        bool selecteursVisibles = false;
        QTimer::singleShot(50, w.get(), [&] {
            auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (d) {
                selecteursVisibles = d->isAncestorOf(devices) && devices->isVisible();
                devices->actualise();
                d->accept();
            }
        });
        menu(*w, t.audioDevices);
        QVERIFY(selecteursVisibles);
        QTimer::singleShot(150, w.get(), [] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                d->reject();
        });
        menu(*w, t.recordSound);
        QCOMPARE(w->session().loaded->id, id);

        // Lecture sur le casque choisi. Son retrait arrête la lecture.
        auto *play = w->findChild<QToolButton *>(u"play"_s);
        play->click();
        QTRY_VERIFY(w->lecteur().en_lecture());
        const auto sortiesServeur = pactl({u"list"_s, u"short"_s, u"sinks"_s}).split(u'\n');
        QString indexCasque;
        for (const auto &ligne : sortiesServeur)
            if (ligne.section(u'\t', 1, 1) == u"sono_usb")
                indexCasque = ligne.section(u'\t', 0, 0);
        QVERIFY(!indexCasque.isEmpty());
        const auto utiliseCasque = [&] {
            const auto flux = pactl({u"list"_s, u"short"_s, u"sink-inputs"_s}).split(u'\n');
            return std::ranges::any_of(flux, [&](const QString &ligne) { return ligne.section(u'\t', 1, 1) == indexCasque; });
        };
        QTRY_VERIFY(utiliseCasque());
        pactl({u"unload-module"_s, casque});
        QTRY_VERIFY(sorties->findText(u"USB-Headset"_s) < 0);
        QVERIFY(!w->lecteur().en_lecture());

        const auto micro = pactl({u"load-module"_s, u"module-remap-source"_s, u"master=sono_tp.monitor"_s,
                                  u"source_name=sono_usb_mic"_s, u"source_properties=device.description=MicroUSB"_s});
        QVERIFY(!micro.isEmpty());
        QTRY_VERIFY(entrees->findText(u"MicroUSB"_s) >= 0);
        entrees->setCurrentIndex(entrees->findText(u"MicroUSB"_s));
        QTimer::singleShot(500, w.get(), [&] { pactl({u"unload-module"_s, micro}); });
        QTimer secours;
        secours.setSingleShot(true);
        connect(&secours, &QTimer::timeout, w.get(), [] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                d->reject();
        });
        secours.start(5000);
        menu(*w, t.recordSound);
        QVERIFY(secours.isActive());
        QVERIFY(w->session().loaded->id != id);
        QVERIFY(affiche_partie(w.get(), t.inputFailure));
        const auto prisePartielle = w->session().loaded->id;
        QTimer::singleShot(50, w.get(), [] {
            if (auto *m = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                m->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!w->close());
        QCOMPARE(w->session().loaded->id, prisePartielle);
        tonalite.waitForFinished(5000);
    }

    /// Page d'accueil épurée : ouverture, enregistrement et choix des périphériques ;
    /// tout tient sans chevauchement dans la plus petite fenêtre.
    void page_accueil_simple()
    {
        for (const Lang lang : {Lang::Fr, Lang::En}) {
            auto w = banc_essai(lang, false);
            w->resize(MIN_WINDOW);
            QTest::qWait(30);
            QWidget *page = w->findChild<QWidget *>(u"welcome"_s);
            QList<QWidget *> shown;
            for (QWidget *child : page->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
                if (child->isVisibleTo(page))
                    shown << child;
            }
            const Tr &t = textes(lang);
            auto *enregistrer = page->findChild<QPushButton *>(u"welcomeRecord"_s);
            QVERIFY(enregistrer && enregistrer->isVisibleTo(page));
            QCOMPARE(enregistrer->text(), u"\u2002"_s + t.recordSound);
            QCOMPARE(enregistrer->accessibleName(), t.recordSound);
            QVERIFY(affiche_texte(page, t.refreshDevices));
            QVERIFY(page->findChild<QComboBox *>(u"audioInput"_s));
            QVERIFY(page->findChild<QComboBox *>(u"audioOutput"_s));
            for (const auto &label : {t.openSound, t.recordSound, t.saveTake, t.exportSpectrum, t.exportExtract}) {
                const auto *commande = action(*w, label);
                QVERIFY2(commande && commande->isIconVisibleInMenu() && !commande->icon().isNull(), qPrintable(label));
            }
            QVERIFY(!action(*w, t.saveTake)->isEnabled());
            for (QWidget *a : shown) {
                QVERIFY2(page->rect().contains(a->geometry()), qPrintable(a->metaObject()->className()));
                for (QWidget *b : shown) {
                    if (a != b)
                        QVERIFY2(!a->geometry().intersects(b->geometry()), qPrintable(u"%1 / %2"_s.arg(
                            QString::fromLatin1(a->metaObject()->className()), QString::fromLatin1(b->metaObject()->className()))));
                }
            }
            capture(w.get(), lang == Lang::Fr ? u"00_accueil_petite"_s : u"09_en_accueil_petite"_s);
        }
    }

    /// Bouton soleil / lune et bouton drapeau : un clic passe à l'autre thème, à l'autre langue.
    void boutons_theme_et_langue()
    {
        auto w = banc_essai(Lang::Fr, false);
        charge(*w, 440);
        montre_onglet(*w, Tool::Extract);
        QAction *themeButton = w->findChild<QAction *>(u"theme"_s);
        QAction *languageButton = w->findChild<QAction *>(u"language"_s);
        QVERIFY(!est_sombre(QApplication::palette()));
        QCOMPARE(themeButton->toolTip(), u"Thème clair. Cliquer pour passer au thème sombre."_s);
        themeButton->trigger();
        QTest::qWait(30);
        QVERIFY(est_sombre(QApplication::palette()));
        QCOMPARE(w->session().params.theme, Qt::ColorScheme::Dark);
        QCOMPARE(themeButton->toolTip(), u"Thème sombre. Cliquer pour passer au thème clair."_s);
        survole_pres_du_premier_pic(*w);
        capture(w.get(), u"08_theme_sombre"_s);
        // mode expert en sombre : champs du panneau des mesures (écoute, case, tableau)
        menu(*w, u"Expert"_s);
        QVERIFY(attend_analyse(*w));
        capture(w.get(), u"08_theme_sombre_expert"_s);
        menu(*w, u"Simplifié"_s);
        // mesure de la période en sombre : t₁ et t₂ se détachent du signal
        montre_onglet(*w, Tool::Period);
        w->montre_signal(1.0, 1.012);
        glisse(w->graphique_signal(), point_du_signal(*w, 1.0021), point_du_signal(*w, 1.0089));
        QVERIFY(w->session().period);
        capture(w.get(), u"08_theme_sombre_mesure"_s);

        QCOMPARE(languageButton->toolTip(), u"Langue : français. Cliquer pour passer en anglais (English)."_s);
        languageButton->trigger();
        QCOMPARE(w->session().params.lang, Lang::En);
        QCOMPARE(w->menuBar()->actions().first()->text(), u"File"_s);
        QVERIFY(affiche_texte(w.get(), u"Interface in English."_s));
        QVERIFY(affiche_texte(w.get(), u"Simple mode"_s));
        QCOMPARE(languageButton->toolTip(), u"Language: English. Click to switch to French (français)."_s);
        languageButton->trigger();
        QCOMPARE(w->session().params.lang, Lang::Fr);

        themeButton->trigger();
        QTest::qWait(30);
        QVERIFY(!est_sombre(QApplication::palette()));
        QCOMPARE(w->session().params.theme, Qt::ColorScheme::Light);
    }

    /// Barre d'état : une ligne, de même hauteur d'un message à l'autre ; un échec s'y lit en rouge,
    /// avec son pictogramme.
    void barre_etat_montre_echecs()
    {
        auto w = banc_essai(Lang::En, false);
        const int line = w->statusBar()->height();
        QVERIFY2(line < 2 * w->fontMetrics().height() + 4, qPrintable(QString::number(line)));
        charge(*w, 440);
        QCOMPARE(w->statusBar()->height(), line);
        w = banc_essai(Lang::Fr, false);
        auto *message = w->findChild<QLabel *>(u"statusMessage"_s);
        const QColor normal = message->palette().color(QPalette::WindowText);
        w->charge_chemin(u"/nulle/part/son.wav"_s);
        for (int i = 0; i < 200 && !message->text().startsWith(u"Impossible"_s); ++i)
            QTest::qWait(10);
        QVERIFY2(message->text().startsWith(u"Impossible de lire"_s), qPrintable(message->text()));
        QVERIFY(message->palette().color(QPalette::WindowText) != normal);
        QVERIFY(accueil_affiche(*w));
        capture(w.get(), u"09_erreur"_s);

        // Une prise déjà ouverte reste à l'écran si le WAV suivant se termine
        // avant son premier échantillon. La barre d'état indique la cause.
        charge(*w, 440);
        const auto precedent = w->session().loaded->id;
        QTemporaryDir dir;
        const auto path = dir.filePath(u"incomplet.wav"_s);
        QVERIFY(audio::ecrit_wav(path, 8000, {{0.5f, -0.5f}}));
        QVERIFY(QFile::resize(path, 45));
        w->charge_chemin(path);
        QTRY_VERIFY(message->text().contains(textes(Lang::Fr).incompleteAudio));
        QCOMPARE(w->session().loaded->id, precedent);
    }

    void mode_simplifie_donne_mesures()
    {
        auto w = banc_essai(Lang::Fr, false);
        QSignalSpy calculs(&w->analyseur(), &Analyzer::mis_a_jour);
        charge(*w, 440);
        glisse(w->graphique_signal(), point_du_signal(*w, 0.2), point_du_signal(*w, 0.3));
        QVERIFY(w->session().period);
        QCOMPARE(calculs.count(), 0);
        QVERIFY(!w->analyseur().en_attente());
        QVERIFY(!w->analyseur().resultat(w->session().loaded->id));
        montre_onglet(*w, Tool::Extract);
        QTRY_COMPARE(calculs.count(), 1);
        QVERIFY(w->analyseur().resultat(w->session().loaded->id));
        montre_onglet(*w, Tool::Period);
        montre_onglet(*w, Tool::Extract);
        QVERIFY(!w->analyseur().en_attente());
        QCOMPARE(calculs.count(), 1);
        QVERIFY(!w->session().params.expert);
        // ni fondamentale ni tableau des harmoniques : l'élève mesure
        QVERIFY(!affiche_texte(w.get(), u"Fondamentale et harmoniques"_s));
        QVERIFY(affiche_texte(w.get(), u"Extrait analysé"_s));
        survole_pres_du_premier_pic(*w);
        capture(w.get(), u"01_flute_simplifie"_s);
        // pas de réglages dans le menu Affichage
        QVERIFY(!action(*w, u"Réglages…"_s)->isVisible());
        QVERIFY(action(*w, u"Tout voir"_s)->isVisible());
        // passage en mode expert par le menu Mode
        menu(*w, u"Expert"_s);
        QVERIFY(w->session().params.expert);
        QVERIFY(attend_analyse(*w));
        QVERIFY(affiche_texte(w.get(), u"Fondamentale et harmoniques"_s));
    }

    void change_de_mode_pendant_ouverture()
    {
        Settings reglages;
        reglages.lang = Lang::Fr;
        reglages.channel = audio::Channel::Right;
        MainWindow w(reglages, SettingsFiles{}, {});
        w.show();
        constexpr int cadence = 8000;
        std::vector<float> gauche(3 * cadence), droite(3 * cadence);
        const auto note = essais::sons_purs({{440, 0.5}}, 0.5, cadence);
        std::ranges::copy(note, gauche.begin());
        std::ranges::copy(note, droite.begin() + 2 * cadence);
        QTemporaryDir dir;
        const auto chemin = dir.filePath(u"deux-voies.wav"_s);
        QVERIFY(audio::ecrit_wav(chemin, cadence, {gauche, droite}));
        w.charge_chemin(chemin);
        // Changer de mode avant que la fenêtre traite la fin de la lecture.
        auto *expert = action(w, textes(Lang::Fr).modeExpert);
        QVERIFY(expert);
        expert->trigger();
        QTRY_VERIFY(w.session().loaded);
        QCOMPARE(w.session().canal(), audio::Channel::Right);
        QCOMPARE(w.session().extract, (Cursors{2.0, 2.5}));
        QCOMPARE(w.session().loaded->id, quint64(1));
    }

    void mesure_ouverture_fichier()
    {
        const QString chemin = qEnvironmentVariable("MEVSONO_MESURE_OUVERTURE");
        if (chemin.isEmpty())
            QSKIP("Indiquer le fichier à mesurer dans MEVSONO_MESURE_OUVERTURE");
        QVERIFY2(QFileInfo(chemin).isReadable(), qPrintable(chemin));
        MainWindow fenetre(Settings{}, SettingsFiles{}, {});
        fenetre.resize(1000, 680);
        fenetre.show();
        QTest::qWait(250);

        // Une minuterie de la fenêtre compte aussi le temps où un calcul ou le
        // premier dessin empêche Qt de traiter les événements.
        QEventLoop boucle;
        QElapsedTimer chrono;
        QTimer battement;
        battement.setTimerType(Qt::PreciseTimer);
        qint64 precedent = 0, plusLongIntervalle = 0, ouverture = -1;
        connect(&battement, &QTimer::timeout, &boucle, [&] {
            const qint64 maintenant = chrono.elapsed();
            plusLongIntervalle = std::max(plusLongIntervalle, maintenant - precedent);
            precedent = maintenant;
            if (fenetre.session().loaded && ouverture < 0)
                ouverture = maintenant;
            if (ouverture >= 0 && maintenant - ouverture >= 200)
                boucle.quit();
        });
        QTimer::singleShot(60'000, &boucle, &QEventLoop::quit);
        chrono.start();
        battement.start(10);
        fenetre.charge_chemin(chemin);
        boucle.exec();
        QVERIFY2(ouverture >= 0, "Le fichier n’a pas pu être ouvert en une minute");
        qInfo("Ouverture %lld ms, plus long intervalle entre deux événements %lld ms",
              static_cast<long long>(ouverture), static_cast<long long>(plusLongIntervalle));
    }

    void reglages_reessayes_apres_un_echec()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Un fichier ordinaire occupe provisoirement la place du dossier de réglages.
        QFile obstacle(dir.filePath(u"preferences"_s));
        QVERIFY(obstacle.open(QIODevice::WriteOnly));
        obstacle.close();
        const SettingsFiles files{obstacle.fileName() + u"/mevsono.ini"_s, {}};
        Settings settings;
        settings.lang = Lang::Fr;
        MainWindow w(settings, files, {});
        w.show();
        menu(w, textes(Lang::Fr).modeExpert);
        QTRY_VERIFY(affiche_partie(&w, textes(Lang::Fr).settingsFailed(QString())));
        QVERIFY(obstacle.remove());
        QVERIFY(w.close());
        QVERIFY(QFileInfo::exists(files.user));
        QVERIFY(Settings::charge(files).expert);
    }

    void mesures_visibles_sur_un_sinus_a_48_khz()
    {
        Settings settings;
        settings.lang = Lang::Fr;
        settings.expert = true;
        settings.fftSize = 1024;
        MainWindow w(settings, {}, {});
        w.show();
        charge(w, 6000, {1}, 48000);
        // Dans l'onglet période, la fréquence affichée vient uniquement de l'autocorrélation.
        QVERIFY(affiche_partie(&w, u"f\u00a0≈\u00a06000,0\u00a0Hz"_s));
        montre_onglet(w, Tool::Extract);
        QVERIFY(affiche_texte(&w, u"f₁ = 6000,0 Hz"_s));

        charge(w, 46.875, {1}, 48000);
        const auto *table = w.findChild<QTableWidget *>(u"harmonics"_s);
        QVERIFY(table && table->isVisible() && table->rowCount() >= 2);
        QCOMPARE(table->item(0, 1)->text(), u"46,9"_s);
        QCOMPARE(table->item(1, 1)->text(), u"—"_s);
    }

    void mode_expert_fondamentale_et_harmoniques()
    {
        auto w = banc_essai(Lang::Fr, true);
        charge(*w, 440);
        QCOMPARE(w->session().tool, Tool::Period);
        QVERIFY(affiche_partie(w.get(), u"Autocorrélation : T\u00a0≈"_s));
        montre_onglet(*w, Tool::Extract);
        const auto f0 = w->fondamentale_courante();
        QVERIFY(f0 && std::abs(*f0 - 440.0) < 0.1);
        const auto [a, b] = w->session().extract;
        QVERIFY2(std::abs(b - a - 0.5) < 1e-9, "extrait de 0,5 s");
        QVERIFY(affiche_texte(w.get(), u"f₁ = 440,0 Hz"_s));
        QVERIFY(affiche_texte(w.get(), u"(automatique)"_s));
        QVERIFY(!affiche_texte(w.get(), u"(à la main)"_s));
        // panneau sans consignes ni explications : la ligne d'aide du spectre et les bulles les donnent
        QVERIFY(!affiche_partie(w.get(), u"Survolez le spectre"_s));
        QVERIFY(!affiche_partie(w.get(), u"Zoomez sur le signal"_s));
        QVERIFY(affiche_partie(w.get(), u"Autocorrélation : T\u00a0≈"_s));
        QCOMPARE(harmoniques_trouvees(*w), (std::vector<size_t>{1, 2, 3, 4, 5, 6}));
        // tableau des harmoniques : chaque colonne a la largeur de son contenu, et le panneau s'élargit
        // pour le montrer en entier
        auto *table = w->findChild<QTableWidget *>(u"harmonics"_s);
        auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
        QVERIFY(table && table->isVisible() && side);
        const auto tableFits = [&] {
            const QHeaderView *h = table->horizontalHeader();
            for (int c = 0; c < h->count(); ++c) {
                int need = h->sectionSizeHint(c);
                for (int r = 0; r < table->rowCount(); ++r)
                    need = std::max(need, table->fontMetrics().horizontalAdvance(table->item(r, c)->text()));
                if (h->sectionSize(c) < need || h->sectionViewportPosition(c) + h->sectionSize(c) > table->viewport()->width())
                    return false;
            }
            return side->widget()->width() <= side->viewport()->width();
        };
        QVERIFY(QTest::qWaitFor(tableFits, 1000));
        // repères des harmoniques : désactivés par défaut
        QVERIFY(!Settings().harmonicMarks);
        survole_pres_du_premier_pic(*w);
        capture(w.get(), u"02_flute_expert"_s);
        // fenêtre des réglages, aux valeurs par défaut
        quitte(w->graphique_spectre());
        menu(*w, u"Réglages…"_s);
        SettingsDialog *dialog = w->dialogue_reglages();
        QVERIFY(dialog && dialog->isVisible());
        // seuil des pics : le signe moins « − », comme ailleurs dans MeVSono ; le trait d'union se tape
        {
            auto *threshold = dialog->findChild<QDoubleSpinBox *>();
            QVERIFY(threshold);
            QCOMPARE(threshold->text(), u"−40,0 dB"_s);
            threshold->findChild<QLineEdit *>()->selectAll();
            QTest::keyClicks(threshold, u"-55,5"_s);
            QCOMPARE(threshold->value(), -55.5);
            threshold->setValue(-40.0);
        }
        capture(dialog, u"02_reglages"_s);
        // décibels et axe logarithmique (menu Affichage), repères des harmoniques (réglages)
        menu(*w, u"en décibels (dB)"_s);
        menu(*w, u"logarithmique"_s);
        QCheckBox *marks = nullptr;
        for (QCheckBox *box : dialog->findChildren<QCheckBox *>()) {
            if (box->text() == u"Afficher les repères n × f₁")
                marks = box;
        }
        QVERIFY(marks);
        marks->click();
        QVERIFY(w->session().params.harmonicMarks);
        QVERIFY(w->session().params.db && w->session().params.logFreq);
        survol(w->graphique_spectre(), QPointF(w->graphique_spectre()->ecran_x(std::log10(445.0)),
                                         w->graphique_spectre()->cadre_graphique().center().y()));
        capture(w.get(), u"03_flute_db_log_reperes"_s);
        for (QAbstractButton *button : dialog->findChildren<QAbstractButton *>()) {
            if (button->text() == u"Fermer")
                button->click();
        }
        QVERIFY(!dialog->isVisible());
    }

    void tuyau_ferme_harmoniques_impaires()
    {
        auto w = banc_essai(Lang::Fr, true);
        charge(*w, 261.63, {1, 3, 5, 7, 9});
        montre_onglet(*w, Tool::Extract);
        // tuyau fermé à une extrémité : les harmoniques impairs seulement
        QCOMPARE(harmoniques_trouvees(*w), (std::vector<size_t>{1, 3, 5, 7, 9}));
        capture(w.get(), u"04_tuyau_bouche"_s);
    }

    /// L'agrandissement demandé est retenu : borné quand la fenêtre se rétrécit, il revient quand elle
    /// s'élargit (et c'est lui qui est enregistré).
    void agrandissement_retrouve()
    {
        const double base = QApplication::font().pointSizeF();
        Settings settings;
        settings.lang = Lang::Fr;
        settings.maximized = false;
        settings.uiZoom = 1.5;
        auto w = std::make_unique<MainWindow>(settings, SettingsFiles{}, QString());
        w->resize(1400, 800);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::qWait(50);
        const auto ratio = [&] { return QApplication::font().pointSizeF() / base; };
        QVERIFY2(std::abs(ratio() - 1.5) < 0.01, qPrintable(QString::number(ratio())));
        // 1000 points de large : au plus 1000 / 860
        w->resize(1000, 800);
        QTest::qWait(50);
        QVERIFY2(std::abs(ratio() - 1000.0 / 860.0) < 0.01, qPrintable(QString::number(ratio())));
        // Ctrl + dans la fenêtre étroite : rien ne change, et l'agrandissement retenu ne baisse pas
        for (QAction *a : w->actions())
            if (a->shortcut() == QKeySequence(QKeySequence::ZoomIn))
                a->trigger();
        QTest::qWait(30);
        QVERIFY2(std::abs(ratio() - 1000.0 / 860.0) < 0.01, qPrintable(QString::number(ratio())));
        w->resize(1400, 800);
        QTest::qWait(50);
        QVERIFY2(std::abs(ratio() - 1.5) < 0.01, qPrintable(QString::number(ratio())));
        // Ctrl 0 : la police de l'application revient à sa taille, pour les essais suivants
        for (QAction *a : w->actions())
            if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_0))
                a->trigger();
        QTest::qWait(30);
        QVERIFY(std::abs(QApplication::font().pointSizeF() - base) < 0.01);
    }

    void export_spectre_attend_nouvel_extrait()
    {
        // pendant le calcul du spectre d'un nouvel extrait, l'export (qui donnerait l'ancien
        // spectre) est désactivé ; il revient une fois le calcul fini
        auto w = banc_essai(Lang::Fr, true);
        charge(*w, 110);
        montre_onglet(*w, Tool::Extract);
        QVERIFY(attend_analyse(*w));
        QAction *exportAction = action(*w, u"Exporter le spectre (CSV)…"_s);
        QVERIFY(exportAction && exportAction->isEnabled());
        glisse(w->graphique_signal(), point_du_signal(*w, 0.4), point_du_signal(*w, 2.6));
        QVERIFY(!w->analyseur().en_attente() || !exportAction->isEnabled());
        QVERIFY(attend_analyse(*w));
        QVERIFY(exportAction->isEnabled());
    }

    void export_csv_par_blocs()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        auto w = banc_essai(Lang::Fr, true);
        QTemporaryDir dir;
        const auto &t = textes(Lang::Fr);
        charge(*w, 440);
        const QString path = dir.filePath(u"extrait.csv"_s);
        auto choisir = [&] {
            auto *d = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!d)
                return;
            d->setOption(QFileDialog::DontConfirmOverwrite);
            d->setDirectory(dir.path());
            d->findChild<QLineEdit *>(u"fileNameEdit"_s)->setText(QFileInfo(path).fileName());
            QMetaObject::invokeMethod(d, "accept");
        };
        QTimer::singleShot(0, w.get(), choisir);
        menu(*w, t.exportExtract);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto &s = w->session();
        const auto [a, b] = *s.intervalle_extrait();
        const auto &son = s.loaded->audio;
        QCOMPARE(file.readAll(), csv_extrait(t, std::span(*son.mix).subspan(a, b - a), a, son.rate).toUtf8());
        file.close();

        // Un export annulé conserve le fichier déjà présent à destination.
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("original");
        file.close();
        montre_onglet(*w, Tool::Extract);
        glisse(w->graphique_signal(), point_du_signal(*w, 0.1), point_du_signal(*w, 2.9));
        bool annule = false;
        QTimer::singleShot(0, w.get(), [&] {
            choisir();
            QTimer::singleShot(0, w.get(), [&] {
                if (auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget())) {
                    annule = true;
                    progress->reject();
                }
            });
        });
        menu(*w, t.exportExtract);
        QVERIFY(annule);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("original"));
    }

    void glisser_sur_signal_choisit_extrait()
    {
        auto w = banc_essai(Lang::Fr, false);
        charge(*w, 110);
        montre_onglet(*w, Tool::Extract);
        const Cursors before = w->session().extract;
        glisse(w->graphique_signal(), point_du_signal(*w, 1.2), point_du_signal(*w, 1.85));
        const Cursors after = w->session().extract;
        QVERIFY(before != after);
        QVERIFY(after[1] > after[0]);
        QVERIFY(std::abs(after[0] - 1.2) < 0.01 && std::abs(after[1] - 1.85) < 0.01);
        QVERIFY(attend_analyse(*w));
        const auto f0 = w->fondamentale_courante();
        QVERIFY(f0 && std::abs(*f0 - 110.0) < 0.5);
        // simple clic : le curseur le plus proche (B) vient sous la souris
        glisse(w->graphique_signal(), point_du_signal(*w, 2.0), point_du_signal(*w, 2.0));
        QTRY_VERIFY(std::abs(w->session().extract[1] - 2.0) < 0.01);
        QCOMPARE(w->session().extract[0], after[0]);
        // A ne passe pas B : glissé au-delà, il s'arrête sur lui
        glisse(w->graphique_signal(), point_du_signal(*w, after[0]), point_du_signal(*w, 2.5));
        Cursors e = w->session().extract;
        QVERIFY2(e[0] <= e[1] && std::abs(e[0] - 2.0) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
        // A et B l'un sur l'autre : glisser vers la droite écarte B, vers la gauche écarte A
        glisse(w->graphique_signal(), point_du_signal(*w, e[1]), point_du_signal(*w, 2.4));
        e = w->session().extract;
        QVERIFY2(std::abs(e[0] - 2.0) < 0.01 && std::abs(e[1] - 2.4) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
        // B ne passe pas A non plus
        glisse(w->graphique_signal(), point_du_signal(*w, e[1]), point_du_signal(*w, 1.0));
        e = w->session().extract;
        QVERIFY2(e[0] <= e[1] && std::abs(e[1] - 2.0) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
        // nouvel extrait glissé de droite à gauche : A est à gauche
        glisse(w->graphique_signal(), point_du_signal(*w, 1.6), point_du_signal(*w, 0.6));
        e = w->session().extract;
        QVERIFY2(std::abs(e[0] - 0.6) < 0.01 && std::abs(e[1] - 1.6) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
    }

    void double_clic_et_clavier()
    {
        auto w = banc_essai(Lang::Fr, false);
        charge(*w, 440);
        auto *signal = w->graphique_signal();
        for (Tool tool : {Tool::Period, Tool::Extract}) {
            montre_onglet(*w, tool);
            signal->setFocus();
            QTest::keyClick(signal, Qt::Key_1);
            const auto lire = [&] { return tool == Tool::Period ? *w->session().period : w->session().extract; };
            auto initial = lire();
            QTest::keyClick(signal, Qt::Key_Right);
            QVERIFY(std::abs(lire()[0] - initial[0] - 1 / essais::RATE) < 1e-9);
            QTest::keyClick(signal, Qt::Key_2);
            QTest::keyClick(signal, Qt::Key_Left);
            QVERIFY(std::abs(lire()[1] - initial[1] + 1 / essais::RATE) < 1e-9);
            initial = lire();
            QTest::keyClick(signal, Qt::Key_Plus);
            QVERIFY(signal->vue_x1() - signal->vue_x0() < 2);
            const auto pos = signal->cadre_graphique().center();
            souris(signal, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
            souris(signal, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
            souris(signal, QEvent::MouseButtonDblClick, pos, Qt::LeftButton, Qt::LeftButton);
            souris(signal, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
            QTest::qWait(QApplication::doubleClickInterval() + 50);
            QCOMPARE(lire(), initial);
            QCOMPARE(signal->vue_x1(), 3.0);
        }
        QVERIFY(attend_analyse(*w));
        auto *sp = w->graphique_spectre();
        sp->setFocus();
        QTest::keyClick(sp, Qt::Key_Right);
        QVERIFY(sp->pic_selectionne());
        const auto premier = *sp->pic_selectionne();
        QTest::keyClick(sp, Qt::Key_Right);
        QVERIFY(sp->pic_selectionne()->freq > premier.freq);
        QTest::keyClick(sp, Qt::Key_Left);
        QCOMPARE(*sp->pic_selectionne(), premier);
        quitte(sp);
        QVERIFY(affiche_partie(w.get(), textes(Lang::Fr).selectedPeak));
        QTest::keyClick(sp, Qt::Key_Escape);
        QVERIFY(!sp->pic_selectionne());
    }

    void zoom_spectral_et_pic_sous_la_souris()
    {
        auto fenetre = banc_essai(Lang::Fr, true);
        charge(*fenetre, 440);
        montre_onglet(*fenetre, Tool::Extract);
        auto *spectre = fenetre->graphique_spectre();
        survole_pres_du_premier_pic(*fenetre);

        // Le pointeur reste immobile alors que le graphique sous le réticule change.
        spectre->definit_vue_x(850.0, 950.0);
        QVERIFY(spectre->pic_survole());
        QVERIFY(std::abs(spectre->pic_survole()->freq - 880.0) < 0.5);

        menu(*fenetre, textes(Lang::Fr).axisLog);
        spectre->repaint();
        const QPointF centre = spectre->cadre_graphique().center();
        QWheelEvent dezoom(centre, spectre->mapToGlobal(centre), {}, QPoint(0, -12000),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(spectre, &dezoom);
        // La vue peut dépasser les 5 kHz initiaux et s'arrête à la limite de Nyquist.
        QCOMPARE(spectre->vue_x1(), std::log10(essais::RATE / 2));
        QTest::keyClick(spectre, Qt::Key_Home);
        spectre->repaint();
        QCOMPARE(spectre->vue_x1(), std::log10(5000.0));
        menu(*fenetre, textes(Lang::Fr).keepSpectrum);

        // À 24 échantillons/s, tout le spectre est sous les 20 Hz de la vue habituelle.
        charge(*fenetre, 2, {1}, 24);
        spectre->repaint();
        QVERIFY(spectre->vue_x0() < spectre->vue_x1());
        QCOMPARE(spectre->vue_x1(), std::log10(12.0));
        // On peut toujours explorer les 440 Hz de la référence, puis retrouver le son seul.
        spectre->definit_vue_x(std::log10(400.0), std::log10(500.0));
        QCOMPARE(spectre->vue_x1(), std::log10(500.0));
        menu(*fenetre, textes(Lang::Fr).removeSpectrum);
        spectre->repaint();
        QCOMPARE(spectre->vue_x1(), std::log10(12.0));
    }

    void curseur_de_periode_apres_croisement()
    {
        auto fenetre = banc_essai(Lang::Fr, true);
        const auto [debut, fin] = mesure_sur_signal_carre(*fenetre, false);
        auto *signal = fenetre->graphique_signal();
        // Le système peut livrer le relâchement avant le prochain mouvement de la souris.
        souris(signal, QEvent::MouseButtonPress, point_du_signal(*fenetre, debut), Qt::LeftButton, Qt::LeftButton);
        souris(signal, QEvent::MouseButtonRelease, point_du_signal(*fenetre, 0.518), Qt::LeftButton, Qt::NoButton);
        const Cursors apres = *fenetre->session().period;
        QCOMPARE(apres[0], fin);
        QVERIFY(std::abs(apres[1] - 0.518) < 1e-6);
        QTest::keyClick(signal, Qt::Key_Right);
        QCOMPARE((*fenetre->session().period)[0], fin);
        QCOMPARE((*fenetre->session().period)[1], apres[1] + 1 / essais::RATE);
    }

    void seuil_du_fichier_conserve_par_le_dialogue()
    {
        Settings reglages = Settings::interprete(u"peak_threshold_db = -42.36\n"_s);
        SettingsDialog dialogue(reglages, nullptr);
        dialogue.retraduit(textes(Lang::Fr));
        dialogue.charge();
        auto *option = dialogue.findChild<QCheckBox *>();
        auto *seuil = dialogue.findChild<QDoubleSpinBox *>();
        QVERIFY(option && seuil);
        option->click();
        QCOMPARE(reglages.thresholdDb, -42.36);
        seuil->setValue(-38.5);
        QCOMPARE(reglages.thresholdDb, -38.5);
    }

    void comparaison_de_spectres_data() { langues_de_la_notice(); }

    void comparaison_de_spectres()
    {
        QFETCH(Lang, lang);
        auto w = banc_essai(lang, false);
        const Tr &t = textes(lang);
        charge(*w, 440);
        montre_onglet(*w, Tool::Extract);
        QVERIFY(QTest::qWaitFor([&] { return action(*w, t.keepSpectrum)->isEnabled(); }));
        menu(*w, t.keepSpectrum);
        auto *spectre = w->findChild<SpectrumPlot *>();
        QVERIFY(spectre && spectre->reference_compatible());
        const auto nom = spectre->nom_reference();
        charge(*w, 200, {1, 3, 5, 7, 9});
        QVERIFY(spectre->reference_compatible());
        QCOMPARE(spectre->nom_reference(), nom);
        menu(*w, t.matchPeaks);
        QVERIFY(affiche_partie(w.get(), t.matchedPeaks));
        capture(w.get(), u"comparaison_"_s + code_langue(lang));
        menu(*w, t.removeSpectrum);
        QVERIFY(!spectre->a_reference());
    }

    void ordonnees_du_signal_suivent_la_vue_data() { langues_de_la_notice(); }

    void ordonnees_du_signal_suivent_la_vue()
    {
        QFETCH(Lang, lang);
        auto w = banc_essai(lang, false);
        w->resize(MIN_WINDOW);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"amplitude-variable.wav"_s);
        auto son = essais::sons_purs({{100, 0.8}}, 1.0);
        for (size_t i = son.size() / 2; i < son.size(); ++i)
            son[i] = i < 3 * son.size() / 4 ? 0.25f + son[i] * 0.01f : 0.0f;
        QVERIFY(audio::ecrit_wav(path, int(essais::RATE), {son}));
        w->charge_chemin(path);
        QTRY_VERIFY(w->session().loaded);
        auto *signal = w->graphique_signal();
        const auto bornes = [&] {
            signal->repaint();
            const QRectF cadre = signal->cadre_graphique();
            return std::pair(signal->donnee_y(cadre.bottom()), signal->donnee_y(cadre.top()));
        };
        QVERIFY(bornes().second > 0.8);
        w->montre_signal(0.55, 0.65);
        const auto [lo, hi] = bornes();
        QVERIFY(lo > 0.24 && lo < 0.2421);
        QVERIFY(hi > 0.2579 && hi < 0.26);
        w->montre_signal(0.8, 0.9);
        QVERIFY(bornes().first < 0 && bornes().second > 0);
        w->montre_signal(0.55, 0.65);

        const Tr &t = textes(lang);
        menu(*w, t.modeExpert);
        menu(*w, t.settingsButton);
        auto *dialog = w->dialogue_reglages();
        QVERIFY(dialog && dialog->isVisible());
        QCheckBox *option = nullptr;
        for (auto *box : dialog->findChildren<QCheckBox *>())
            if (box->text() == t.autoScaleY)
                option = box;
        QVERIFY(option && option->isChecked());
        capture(signal, u"axe_ajuste_"_s + code_langue(lang));
        option->click();
        QVERIFY(bornes().second > 0.8);
        QCOMPARE(signal->vue_x0(), 0.55);
        capture(signal, u"axe_fixe_"_s + code_langue(lang));
        menu(*w, t.modeSimple);
        QCOMPARE(bornes(), std::pair(lo, hi));
        menu(*w, t.modeExpert);
        QVERIFY(bornes().second > 0.8);
    }

    void echelle_fixe_et_normalisation_entre_deux_raies()
    {
        auto w = banc_essai(Lang::Fr, true);
        const auto &t = textes(Lang::Fr);
        QTemporaryDir dir;
        const auto ouvre = [&](double bin) {
            constexpr int n = 8192;
            const QString path = dir.filePath(QString::number(bin) + u".wav"_s);
            std::vector<float> samples(n);
            for (int i = 0; i < n; ++i)
                samples[i] = float(0.5 * std::sin(2 * std::numbers::pi * bin * i / n));
            QVERIFY(audio::ecrit_wav(path, 48000, {samples}));
            w->charge_chemin(path);
            QTRY_VERIFY(w->session().loaded && w->session().loaded->audio.name == QFileInfo(path).fileName());
            QVERIFY(attend_analyse(*w));
        };
        ouvre(75.5);
        montre_onglet(*w, Tool::Extract);
        menu(*w, t.settingsButton);
        auto *dialog = w->dialogue_reglages();
        QCheckBox *autoY = nullptr;
        for (auto *option : dialog->findChildren<QCheckBox *>())
            if (option->text() == t.autoScaleY)
                autoY = option;
        QVERIFY(autoY && autoY->isChecked());
        autoY->click();
        dialog->close();
        auto *plot = w->graphique_spectre();
        const auto maximum = [&] {
            plot->repaint();
            return plot->donnee_y(plot->cadre_graphique().top());
        };
        QVERIFY(std::abs(maximum() - 0.575) < 0.002);
        menu(*w, t.keepSpectrum);
        ouvre(75);
        menu(*w, t.matchPeaks);
        menu(*w, t.settingsButton);
        autoY->click();
        dialog->close();
        QVERIFY(std::abs(maximum() - 0.575) < 0.002);
    }

    void ordonnees_du_spectre_suivent_les_pics_visibles()
    {
        auto w = banc_essai(Lang::Fr, true);
        const Tr &t = textes(Lang::Fr);
        charge(*w, 440);
        montre_onglet(*w, Tool::Extract);
        auto *spectre = w->graphique_spectre();
        const auto sommet = [&] {
            spectre->repaint();
            return spectre->donnee_y(spectre->cadre_graphique().top());
        };
        QVERIFY(sommet() > 0.4);
        spectre->definit_vue_x(1700.0, 1800.0); // harmonique 4, amplitude 0,1
        QVERIFY(sommet() > 0.1 && sommet() < 0.12);
        menu(*w, t.keepSpectrum);
        charge(*w, 440, {2}); // pic maximal moitié moins haut que celui de la référence
        spectre->definit_vue_x(1700.0, 1800.0);
        QVERIFY(sommet() > 0.1 && sommet() < 0.12); // le pic de référence reste visible
        menu(*w, t.matchPeaks);
        QVERIFY(sommet() > 0.05 && sommet() < 0.06);
        menu(*w, t.ampDb);
        menu(*w, t.axisLog);
        spectre->definit_vue_x(std::log10(1700.0), std::log10(1800.0));
        QVERIFY(sommet() > -26.1 && sommet() < -22.0);
        menu(*w, t.removeSpectrum);
        QVERIFY(sommet() < -60.0);
        menu(*w, t.fitAll);
        QVERIFY(sommet() > -14.0);
    }

    void mesure_d_une_duree()
    {
        {
            auto w = banc_essai(Lang::Fr, false);
            const auto [t1, t2] = mesure_sur_signal_carre(*w, true);
            // aimantés par défaut aux passages par zéro montants (0,505 s et 0,515 s) : deux périodes de 5 ms
            QVERIFY2(std::abs(t1 - 0.505) < 2e-5 && std::abs(t2 - 0.515) < 2e-5, qPrintable(u"%1 %2"_s.arg(t1).arg(t2)));
            QVERIFY(affiche_texte(w.get(), u"Δt = t₂ − t₁"_s));
            QVERIFY(affiche_texte(w.get(), u"10,00 ms"_s));
            // aucune période ni fréquence calculée à la place de l'élève, aucune option
            QVERIFY(!affiche_texte(w.get(), u"Aimanter t₁ et t₂ aux passages par zéro"_s));
            QVERIFY(!affiche_partie(w.get(), textes(Lang::Fr).autocorrelation));
            QVERIFY(!affiche_partie(w.get(), u"f₁ ="_s));
            survol(w->graphique_signal(), point_du_signal(*w, 0.511));
            capture(w.get(), u"05_mesure_simplifie"_s);
            for (auto *button : w->findChildren<QPushButton *>())
                if (button->text() == textes(Lang::Fr).analyseInterval)
                    QVERIFY(!button->isVisible());
        }
        // mode expert, aimantation décochée : les curseurs restent où on les lâche
        auto w = banc_essai(Lang::Fr, true);
        const auto [t1, t2] = mesure_sur_signal_carre(*w, false);
        QVERIFY2(std::abs(t1 - 0.505) > 5e-5, qPrintable(QString::number(t1)));
        QVERIFY(std::abs(t1 - 0.5051) < 1e-4 && std::abs(t2 - 0.5151) < 1e-4);
        capture(w.get(), u"06_mesure_expert"_s);
        for (auto *button : w->findChildren<QPushButton *>())
            if (button->text() == textes(Lang::Fr).analyseInterval)
                button->click();
        QCOMPARE(w->session().tool, Tool::Extract);
        QCOMPARE(w->session().extract, (Cursors{t1, t2}));
        QVERIFY(attend_analyse(*w));
    }

    /// Lecture, pause et arrêt, sur une sortie audio simulée : l'extrait en mode simplifié ; tout le son
    /// ou l'extrait, au choix, en mode expert.
    void ecoute_extrait_et_son()
    {
        {
            auto w = banc_essai(Lang::Fr, false);
            w->lecteur().simule(true);
            charge(*w, 440);
            // mesure de la période : on écoute tout le son
            QVERIFY(affiche_texte(w.get(), u"Écouter le son"_s));
            w->findChild<QToolButton *>(u"play"_s)->click();
            QVERIFY(lecture(*w).playing && lecture(*w).t && *lecture(*w).t < 0.2);
            // Le changement de plage arrête aussi une lecture en cours.
            // étude des fréquences : l'extrait couvre les 80 % du milieu du son (3 s), et c'est lui qu'on
            // écoute
            montre_onglet(*w, Tool::Extract);
            QVERIFY(affiche_texte(w.get(), u"Écouter l'extrait A–B"_s));
            const auto [a, b] = w->session().extract;
            QVERIFY(std::abs(a - 0.3) < 1e-9 && std::abs(b - 2.7) < 1e-9);
            QVERIFY(!w->findChild<QComboBox *>(u"listen"_s)->isVisibleTo(w.get()));
            PlayState p = lecture(*w);
            QVERIFY(!p.playing && !p.paused && !p.t);
            auto *play = w->findChild<QToolButton *>(u"play"_s);
            auto *pause = w->findChild<QToolButton *>(u"pause"_s);
            auto *stop = w->findChild<QToolButton *>(u"stop"_s);
            // ▶ : depuis A ; la tête de lecture avance sur le signal
            play->click();
            p = lecture(*w);
            QVERIFY(p.playing && p.t && *p.t >= a && *p.t < a + 0.2);
            QVERIFY(attend_lecture(*w, [&](const PlayState &q) { return q.t && *q.t > a + 0.3; }));
            survol(w->graphique_spectre(), QPointF(w->graphique_spectre()->ecran_x(1500.0), w->graphique_spectre()->cadre_graphique().center().y()));
            capture(w.get(), u"07_ecoute"_s);
            const auto passage = w->session().extract;
            w->findChild<AudioDevices *>()->sortie_changee();
            QVERIFY(!lecture(*w).playing && !lecture(*w).paused);
            play->click();
            QVERIFY(lecture(*w).playing);
            QCOMPARE(w->session().extract, passage);
            // ⏸ : la tête de lecture s'arrête, ▶ reprend au même endroit
            pause->click();
            p = lecture(*w);
            QVERIFY(!p.playing && p.paused);
            QTest::qWait(100);
            QCOMPARE(lecture(*w).t, p.t);
            play->click();
            QVERIFY(lecture(*w).t >= p.t);
            // ⏹ : plus de tête de lecture
            stop->click();
            p = lecture(*w);
            QVERIFY(!p.playing && !p.paused && !p.t);
            // Espace : lecture, puis pause
            appuie_espace(*w);
            QVERIFY(lecture(*w).playing);
            appuie_espace(*w);
            QVERIFY(lecture(*w).paused);
        }

        // mode expert : tout le son depuis le début, puis l'extrait d'une demi-seconde, qui s'arrête seul
        auto w = banc_essai(Lang::Fr, true);
        w->lecteur().simule(true);
        charge(*w, 440);
        const auto [a, b] = w->session().extract;
        w->findChild<QToolButton *>(u"play"_s)->click();
        PlayState p = lecture(*w);
        QVERIFY(p.playing && p.t && *p.t < 0.2);
        // le choix de l'extrait pendant la lecture s'applique aussitôt
        auto *listen = w->findChild<QComboBox *>(u"listen"_s);
        QVERIFY(listen->isVisibleTo(w.get()));
        QCOMPARE(listen->currentText(), u"tout le son"_s);
        listen->setCurrentIndex(1);
        emit listen->activated(1);
        p = lecture(*w);
        QVERIFY(p.playing && p.t && *p.t >= a - 1e-6 && *p.t < a + 0.2);
        QVERIFY(attend_lecture(*w, [&](const PlayState &q) { return q.t && *q.t > a + 0.2; }));
        QVERIFY(*lecture(*w).t <= b + 1e-6);
        QVERIFY(attend_lecture(*w, [](const PlayState &q) { return !q.playing; }));
        p = lecture(*w);
        QVERIFY(!p.playing && !p.paused && !p.t);
    }

    void interface_en_anglais()
    {
        auto w = banc_essai(Lang::En, false);
        QVERIFY(affiche_texte(w.get(), u"or drag a sound or video file onto the window"_s));
        capture(w.get(), u"09_en_accueil"_s);
        charge(*w, 440);
        montre_onglet(*w, Tool::Extract);
        QVERIFY(affiche_texte(w.get(), u"Analysed extract"_s));
        survole_pres_du_premier_pic(*w);
        capture(w.get(), u"10_en_flute"_s);
        mesure_sur_signal_carre(*w, true);
        survol(w->graphique_signal(), point_du_signal(*w, 0.511));
        capture(w.get(), u"11_en_measure"_s);
    }

    /// Mode expert en anglais, comme en français : fondamentale, tableau des
    /// harmoniques, fenêtre des réglages.
    void mode_expert_en_anglais()
    {
        auto w = banc_essai(Lang::En, true);
        charge(*w, 440);
        montre_onglet(*w, Tool::Extract);
        QVERIFY(affiche_texte(w.get(), u"Fundamental and harmonics"_s));
        QVERIFY(affiche_texte(w.get(), u"f₁ = 440.0 Hz"_s));
        QCOMPARE(harmoniques_trouvees(*w), (std::vector<size_t>{1, 2, 3, 4, 5, 6}));
        QVERIFY(panneau_lateral_tient(*w));
        survole_pres_du_premier_pic(*w);
        capture(w.get(), u"12_en_expert"_s);
        quitte(w->graphique_spectre());
        menu(*w, u"Settings…"_s);
        QVERIFY(w->dialogue_reglages() && affiche_texte(w->dialogue_reglages(), u"Window function"_s));
        capture(w->dialogue_reglages(), u"12_en_settings"_s);
    }

    // Plantage (accès mémoire invalide) : le journal reçoit la cause et la pile d'appels. L'essai se
    // relance lui-même, et c'est ce second programme qui plante.
    void journal_plantage()
    {
        if (const QString log = qEnvironmentVariable("MEVSUITE_ESSAI_PLANTAGE"); !log.isEmpty()) {
            crash::installe(log, Lang::Fr, false);
            plante_maintenant();
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(u"mevsono.log"_s);
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
        QVERIFY(lines[0].startsWith(u"MeVSono "_s + about::VERSION));
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
            crash::installe(log, Lang::Fr, false);
            exception_dans_un_fil();
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(u"mevsono.log"_s);
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
        QVERIFY(lines[0].startsWith(u"MeVSono "_s + about::VERSION));
        // journal écrit en mode texte : \r\n sous Windows
        QCOMPARE(lines[1].trimmed(), u"essai d'exception"_s);
    }
};

QTEST_MAIN(UiTests)
#include "uitests.moc"
