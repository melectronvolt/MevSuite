// Essais de l'interface : le programme est piloté comme par un élève (clics, glisser, survol), sans
// fenêtre visible (plateforme « offscreen »). Chaque étape est rendue dans `captures/` (dossier de
// compilation), ce qui sert aussi aux images de la notice.

#include "about.h"
#include "app.h"
#include "settingsdialog.h"
#include "signalplot.h"
#include "spectrumplot.h"
#include "style.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>

using namespace Qt::StringLiterals;

namespace {

std::unique_ptr<MainWindow> harness(Lang lang, bool expert)
{
    Settings settings;
    settings.lang = lang;
    settings.expert = expert;
    settings.maximized = false;
    auto w = std::make_unique<MainWindow>(settings, SettingsFiles{}, QString());
    w->resize(1280, 800);
    w->show();
    w->activateWindow();
    if (!QTest::qWaitForWindowExposed(w.get()))
        qWarning("fenêtre non affichée");
    QTest::qWait(50);
    return w;
}

QString captures()
{
    const QString dir = QDir(QCoreApplication::applicationDirPath()).filePath(u"captures"_s);
    QDir().mkpath(dir);
    return dir;
}

void capture(QWidget *w, const QString &name, QWidget *overlay = nullptr)
{
    QTest::qWait(30);
    QImage image = w->grab().toImage();
    // fenêtre des réglages : dessinée au milieu de la fenêtre principale
    if (overlay) {
        const QImage dialog = overlay->grab().toImage();
        const QRect place((image.width() - dialog.width()) / 2, (image.height() - dialog.height()) / 2, dialog.width(),
                          dialog.height());
        QPainter painter(&image);
        // ombre et bord, comme une fenêtre posée sur la fenêtre principale
        painter.fillRect(place.translated(4, 4), QColor(0, 0, 0, 60));
        painter.drawImage(place.topLeft(), dialog);
        painter.setPen(overlay->palette().color(QPalette::Mid));
        painter.drawRect(place.adjusted(0, 0, -1, -1));
    }
    if (!image.save(QDir(captures()).filePath(name + u".png"_s)))
        qWarning("capture impossible : %s", qPrintable(name));
}

/// Attend la fin de l'analyse (calculée dans un fil de fond).
bool waitAnalysis(MainWindow &w)
{
    for (int i = 0; i < 500; ++i) {
        QCoreApplication::processEvents();
        if (!w.analyzer().pending()) {
            QTest::qWait(20);
            return true;
        }
        QTest::qWait(10);
    }
    return false;
}

void load(MainWindow &w, audio::Example example)
{
    w.loadExample(example);
    QVERIFY(waitAnalysis(w));
}

void mouse(QWidget *widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void hover(QWidget *widget, QPointF pos)
{
    mouse(widget, QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

void drag(QWidget *widget, QPointF start, QPointF end)
{
    hover(widget, start);
    mouse(widget, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 10; ++k)
        mouse(widget, QEvent::MouseMove, start + (end - start) * k / 10.0, Qt::NoButton, Qt::LeftButton);
    mouse(widget, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

/// Point du signal à l'instant `t`, à mi-hauteur.
QPointF signalAt(MainWindow &w, double t)
{
    SignalPlot *plot = w.signalPlot();
    return {plot->screenX(t), plot->plotRect().center().y()};
}

/// Choisit un onglet, comme un clic : mesure de la période ou étude des fréquences.
void showTab(MainWindow &w, Tool tool)
{
    w.findChild<QTabBar *>(u"tabs"_s)->setCurrentIndex(tool == Tool::Period ? 0 : 1);
    QTest::qWait(30);
}

/// Survol du spectre du son de flûte, un peu à droite du pic de 440 Hz : le réticule désigne ce pic.
void hoverBesideTheFirstPeak(MainWindow &w)
{
    SpectrumPlot *plot = w.spectrumPlot();
    hover(plot, QPointF(plot->screenX(445.0), plot->plotRect().center().y()));
    const auto peak = plot->hoverPeak();
    QVERIFY2(peak, "pic le plus proche du réticule");
    QVERIFY2(std::abs(peak->freq - 440.0) < 0.1, qPrintable(QString::number(peak->freq)));
}

/// Vrai si un texte visible (étiquette, bouton, case à cocher) porte exactement `text`.
bool shows(QWidget *root, const QString &text)
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
bool showsPart(QWidget *root, const QString &part)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->text().contains(part) && label->isVisibleTo(root))
            return true;
    }
    return false;
}

/// Page affichée : l'accueil (sans son) ou l'analyse.
bool welcomeShown(MainWindow &w)
{
    return w.findChild<QWidget *>(u"welcome"_s)->isVisibleTo(&w);
}

/// La barre de menus, une fois mise en page, a la hauteur d'une barre sans les boutons de droite.
bool standardMenuBar(MainWindow &w)
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

std::vector<size_t> harmonicsFound(const MainWindow &w)
{
    std::vector<size_t> ranks;
    for (const dsp::Harmonic &h : w.harmonics()) {
        if (h.peak)
            ranks.push_back(h.rank);
    }
    return ranks;
}

/// Mesure sur le signal carré (200 Hz), dans l'onglet de la mesure de la période : le logiciel donne t₁, t₂ et Δt,
/// jamais la période ni la fréquence. `snap` : aimantation laissée active (sinon décochée).
Cursors measureOnSquareWave(MainWindow &w, bool snap)
{
    w.loadExample(audio::Example::Square);
    waitAnalysis(w);
    const Cursors extract = w.session().extract;
    showTab(w, Tool::Period);
    // aimantation : active par défaut ; décochée en mode expert pour placer les curseurs librement
    if (!snap)
        w.findChild<QCheckBox *>(u"snap"_s)->click();
    w.showSignal(0.5, 0.52);
    QCoreApplication::processEvents();
    // glisser d'environ 0,5051 s à 0,5151 s, près de deux passages par zéro
    drag(w.signalPlot(), signalAt(w, 0.5051), signalAt(w, 0.5151));
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

PlayState playback(MainWindow &w)
{
    return {w.player().playing(), w.player().paused(), w.playhead()};
}

/// Attend (en temps réel) que l'état de la lecture vérifie `condition`.
bool waitPlayback(MainWindow &w, const std::function<bool(const PlayState &)> &condition)
{
    for (int i = 0; i < 500; ++i) {
        QTest::qWait(10);
        if (condition(playback(w)))
            return true;
    }
    return false;
}

void pressSpace(MainWindow &w)
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

private slots:
    void startScreen()
    {
        auto w = harness(Lang::Fr, false);
        // menus ; à leur droite, sur la même ligne : notice, thème, langue ; pas de barre d'outils
        QStringList titles;
        for (QAction *a : w->menuBar()->actions())
            titles << a->text();
        QCOMPARE(titles, (QStringList{u"Fichier"_s, u"Affichage"_s, u"Mode"_s, u"Aide"_s}));
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
        QVERIFY(standardMenuBar(*w));
        QVERIFY(!action(*w, u"🔍 Tout voir"_s)->isEnabled());
        QCOMPARE(about::icon().size(), QSize(256, 256));
        // sans son : la page d'accueil, un seul bouton (pas de sons d'exemple) ; mode à droite de la
        // barre d'état
        QVERIFY(welcomeShown(*w));
        QVERIFY(!w->findChild<QPushButton *>(u"welcomeExamples"_s));
        for (QAction *a : w->menuBar()->actions().front()->menu()->actions())
            QVERIFY2(!a->text().contains(u"Exemple"_s), qPrintable(a->text()));
        QVERIFY(shows(w.get(), u"📂 Ouvrir un son…"_s));
        QVERIFY(shows(w.get(), u"Mode simplifié"_s));
        QVERIFY(!w->findChild<QLabel *>(u"soundStatus"_s)->isVisibleTo(w.get()));
        capture(w.get(), u"00_accueil"_s);
        // un son (fabriqué pour l'essai) : l'analyse remplace l'accueil
        w->loadExample(audio::Example::Flute);
        QVERIFY(waitAnalysis(*w));
        QVERIFY(!welcomeShown(*w));
        QVERIFY(action(*w, u"🔍 Tout voir"_s)->isEnabled());
        QVERIFY(showsPart(w.get(), u"Flûte (La 440 Hz) · 44,1 kHz"_s));
        // deux onglets sur toute la largeur ; d'abord la mesure de la période, sans spectre
        auto *tabs = w->findChild<QTabBar *>(u"tabs"_s);
        QVERIFY(tabs && tabs->isVisible());
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabText(0), u"Mesure de la période"_s);
        QCOMPARE(tabs->tabText(1), u"Étude des fréquences"_s);
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(!w->spectrumPlot()->isVisibleTo(w.get()));
        QVERIFY(shows(w.get(), u"Mesure d'une période"_s) && !shows(w.get(), u"Extrait analysé"_s));
        // en haut du panneau de gauche : ouvrir, exemples, tout voir ; zoom sur l'extrait pour les fréquences
        auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
        const auto panelButtons = [&] {
            QStringList texts;
            for (QToolButton *b : side->findChildren<QToolButton *>()) {
                if (b->isVisibleTo(w.get()))
                    texts << b->text();
            }
            return texts;
        };
        for (const QString &text : {u"📂 Ouvrir un son…"_s, u"🔍 Tout voir"_s})
            QVERIFY2(panelButtons().contains(text), qPrintable(text));
        QVERIFY(!panelButtons().contains(u"🎵 Exemples ▾"_s));
        QVERIFY(!panelButtons().contains(u"Zoom sur l'extrait"_s));
        QVERIFY(!panelButtons().contains(u"⚙ Réglages…"_s));
        showTab(*w, Tool::Extract);
        QVERIFY(w->spectrumPlot()->isVisibleTo(w.get()));
        QVERIFY(shows(w.get(), u"Extrait analysé"_s) && !shows(w.get(), u"Mesure d'une période"_s));
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
    void sidePanelIsNeverClipped()
    {
        for (const bool expert : {false, true}) {
            auto w = harness(Lang::En, expert);
            w->resize(1400, 800);
            load(*w, audio::Example::Flute);
            measureOnSquareWave(*w, true);
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
            const bool menuBarZoomed = standardMenuBar(*w);
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

    /// Page d'accueil épurée : logo, nom, sous-titre, un bouton (pas de sons d'exemple), une ligne ;
    /// tout tient sans chevauchement dans la plus petite fenêtre.
    void welcomePageIsSimpleAndFits()
    {
        for (const Lang lang : {Lang::Fr, Lang::En}) {
            auto w = harness(lang, false);
            w->resize(MIN_WINDOW);
            QTest::qWait(30);
            QWidget *page = w->findChild<QWidget *>(u"welcome"_s);
            QList<QWidget *> shown;
            for (QWidget *child : page->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
                if (child->isVisibleTo(page))
                    shown << child;
            }
            QCOMPARE(page->findChildren<QPushButton *>().size(), 1);
            QCOMPARE(shown.size(), 5); // logo, nom, sous-titre, bouton, ligne
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
    void themeAndLanguageButtons()
    {
        auto w = harness(Lang::Fr, false);
        load(*w, audio::Example::Flute);
        showTab(*w, Tool::Extract);
        QAction *themeButton = w->findChild<QAction *>(u"theme"_s);
        QAction *languageButton = w->findChild<QAction *>(u"language"_s);
        QVERIFY(!isDark(QApplication::palette()));
        QCOMPARE(themeButton->toolTip(), u"Thème clair. Cliquer pour passer au thème sombre."_s);
        themeButton->trigger();
        QTest::qWait(30);
        QVERIFY(isDark(QApplication::palette()));
        QCOMPARE(w->session().params.theme, Qt::ColorScheme::Dark);
        QCOMPARE(themeButton->toolTip(), u"Thème sombre. Cliquer pour passer au thème clair."_s);
        hoverBesideTheFirstPeak(*w);
        capture(w.get(), u"08_theme_sombre"_s);
        // mode expert en sombre : champs du panneau des mesures (écoute, case, tableau)
        menu(*w, u"Expert"_s);
        QVERIFY(waitAnalysis(*w));
        capture(w.get(), u"08_theme_sombre_expert"_s);
        menu(*w, u"Simplifié"_s);
        // mesure de la période en sombre : t₁ et t₂ se détachent du signal
        showTab(*w, Tool::Period);
        w->showSignal(1.0, 1.012);
        drag(w->signalPlot(), signalAt(*w, 1.0021), signalAt(*w, 1.0089));
        QVERIFY(w->session().period);
        capture(w.get(), u"08_theme_sombre_mesure"_s);

        QCOMPARE(languageButton->toolTip(), u"Langue : français. Cliquer pour passer en anglais (English)."_s);
        languageButton->trigger();
        QCOMPARE(w->session().params.lang, Lang::En);
        QCOMPARE(w->menuBar()->actions().first()->text(), u"File"_s);
        QVERIFY(shows(w.get(), u"Interface in English."_s));
        QVERIFY(shows(w.get(), u"Simple mode"_s));
        QCOMPARE(languageButton->toolTip(), u"Language: English. Click to switch to French (français)."_s);
        languageButton->trigger();
        QCOMPARE(w->session().params.lang, Lang::Fr);

        themeButton->trigger();
        QTest::qWait(30);
        QVERIFY(!isDark(QApplication::palette()));
        QCOMPARE(w->session().params.theme, Qt::ColorScheme::Light);
    }

    /// Barre d'état : une ligne, de même hauteur d'un message à l'autre ; un échec s'y lit en rouge,
    /// avec son pictogramme.
    void statusBarShowsFailures()
    {
        auto w = harness(Lang::En, false);
        const int line = w->statusBar()->height();
        QVERIFY2(line < 2 * w->fontMetrics().height() + 4, qPrintable(QString::number(line)));
        load(*w, audio::Example::Flute);
        QCOMPARE(w->statusBar()->height(), line);
        w = harness(Lang::Fr, false);
        auto *message = w->findChild<QLabel *>(u"statusMessage"_s);
        const QColor normal = message->palette().color(QPalette::WindowText);
        w->loadPath(u"/nulle/part/son.wav"_s);
        for (int i = 0; i < 200 && !message->text().startsWith(u"Impossible"_s); ++i)
            QTest::qWait(10);
        QVERIFY2(message->text().startsWith(u"Impossible de lire"_s), qPrintable(message->text()));
        QVERIFY(message->palette().color(QPalette::WindowText) != normal);
        QVERIFY(welcomeShown(*w));
        capture(w.get(), u"09_erreur"_s);
    }

    void simpleModeGivesOnlyMeasurements()
    {
        auto w = harness(Lang::Fr, false);
        load(*w, audio::Example::Flute);
        showTab(*w, Tool::Extract);
        QVERIFY(!w->session().params.expert);
        // ni fondamentale ni tableau des harmoniques : l'élève mesure
        QVERIFY(!shows(w.get(), u"Fondamentale et harmoniques"_s));
        QVERIFY(shows(w.get(), u"Extrait analysé"_s));
        hoverBesideTheFirstPeak(*w);
        capture(w.get(), u"01_flute_simplifie"_s);
        // pas de réglages dans le menu Affichage
        QVERIFY(!action(*w, u"⚙ Réglages…"_s)->isVisible());
        QVERIFY(action(*w, u"🔍 Tout voir"_s)->isVisible());
        // passage en mode expert par le menu Mode
        menu(*w, u"Expert"_s);
        QVERIFY(w->session().params.expert);
        QVERIFY(waitAnalysis(*w));
        QVERIFY(shows(w.get(), u"Fondamentale et harmoniques"_s));
    }

    void expertModeFundamentalAndHarmonics()
    {
        auto w = harness(Lang::Fr, true);
        load(*w, audio::Example::Flute);
        showTab(*w, Tool::Extract);
        const auto f0 = w->f0();
        QVERIFY(f0 && std::abs(*f0 - 440.0) < 0.1);
        const auto [a, b] = w->session().extract;
        QVERIFY2(std::abs(b - a - 0.5) < 1e-9, "extrait de 0,5 s");
        QVERIFY(shows(w.get(), u"f₁ = 440,0 Hz"_s));
        QVERIFY(shows(w.get(), u"(automatique)"_s));
        QVERIFY(!shows(w.get(), u"(à la main)"_s));
        // panneau sans consignes ni explications : la ligne d'aide du spectre et les bulles les donnent
        QVERIFY(!showsPart(w.get(), u"Survolez le spectre"_s));
        QVERIFY(!showsPart(w.get(), u"Zoomez sur le signal"_s));
        QVERIFY(showsPart(w.get(), u"Autocorrélation : T\u00a0≈"_s));
        QCOMPARE(harmonicsFound(*w), (std::vector<size_t>{1, 2, 3, 4, 5, 6}));
        // repères des harmoniques : désactivés par défaut
        QVERIFY(!Settings().harmonicMarks);
        hoverBesideTheFirstPeak(*w);
        capture(w.get(), u"02_flute_expert"_s);
        // décibels et axe logarithmique (menu Affichage), repères des harmoniques (réglages)
        menu(*w, u"en décibels (dB)"_s);
        menu(*w, u"logarithmique"_s);
        menu(*w, u"⚙ Réglages…"_s);
        SettingsDialog *dialog = w->settingsDialog();
        QVERIFY(dialog && dialog->isVisible());
        QCheckBox *marks = nullptr;
        for (QCheckBox *box : dialog->findChildren<QCheckBox *>()) {
            if (box->text() == u"Afficher les repères n × f₁")
                marks = box;
        }
        QVERIFY(marks);
        marks->click();
        QVERIFY(w->session().params.harmonicMarks);
        QVERIFY(w->session().params.db && w->session().params.logFreq);
        hover(w->spectrumPlot(), QPointF(w->spectrumPlot()->screenX(std::log10(445.0)),
                                         w->spectrumPlot()->plotRect().center().y()));
        capture(w.get(), u"03_flute_db_log_reperes"_s);
        for (QAbstractButton *button : dialog->findChildren<QAbstractButton *>()) {
            if (button->text() == u"Fermer")
                button->click();
        }
        QVERIFY(!dialog->isVisible());
    }

    void stoppedOrganPipeHasOddHarmonics()
    {
        auto w = harness(Lang::Fr, true);
        load(*w, audio::Example::OrganPipe);
        showTab(*w, Tool::Extract);
        // tuyau fermé à une extrémité : les harmoniques impairs seulement
        QCOMPARE(harmonicsFound(*w), (std::vector<size_t>{1, 3, 5, 7, 9}));
        capture(w.get(), u"04_tuyau_bouche"_s);
    }

    void draggingOnTheSignalChoosesTheExtract()
    {
        auto w = harness(Lang::Fr, false);
        load(*w, audio::Example::String);
        showTab(*w, Tool::Extract);
        const Cursors before = w->session().extract;
        drag(w->signalPlot(), signalAt(*w, 1.2), signalAt(*w, 1.85));
        const Cursors after = w->session().extract;
        QVERIFY(before != after);
        QVERIFY(after[1] > after[0]);
        QVERIFY(std::abs(after[0] - 1.2) < 0.01 && std::abs(after[1] - 1.85) < 0.01);
        QVERIFY(waitAnalysis(*w));
        const auto f0 = w->f0();
        QVERIFY(f0 && std::abs(*f0 - 110.0) < 0.5);
        // simple clic : le curseur le plus proche (B) vient sous la souris
        drag(w->signalPlot(), signalAt(*w, 2.0), signalAt(*w, 2.0));
        QVERIFY(std::abs(w->session().extract[1] - 2.0) < 0.01);
        QCOMPARE(w->session().extract[0], after[0]);
        // A ne passe pas B : glissé au-delà, il s'arrête sur lui
        drag(w->signalPlot(), signalAt(*w, after[0]), signalAt(*w, 2.5));
        Cursors e = w->session().extract;
        QVERIFY2(e[0] <= e[1] && std::abs(e[0] - 2.0) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
        // A et B l'un sur l'autre : glisser vers la droite écarte B, vers la gauche écarte A
        drag(w->signalPlot(), signalAt(*w, e[1]), signalAt(*w, 2.4));
        e = w->session().extract;
        QVERIFY2(std::abs(e[0] - 2.0) < 0.01 && std::abs(e[1] - 2.4) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
        // B ne passe pas A non plus
        drag(w->signalPlot(), signalAt(*w, e[1]), signalAt(*w, 1.0));
        e = w->session().extract;
        QVERIFY2(e[0] <= e[1] && std::abs(e[1] - 2.0) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
        // nouvel extrait glissé de droite à gauche : A est à gauche
        drag(w->signalPlot(), signalAt(*w, 1.6), signalAt(*w, 0.6));
        e = w->session().extract;
        QVERIFY2(std::abs(e[0] - 0.6) < 0.01 && std::abs(e[1] - 1.6) < 0.01, qPrintable(u"%1 %2"_s.arg(e[0]).arg(e[1])));
    }

    void measuringADuration()
    {
        {
            auto w = harness(Lang::Fr, false);
            const auto [t1, t2] = measureOnSquareWave(*w, true);
            // aimantés par défaut aux passages par zéro montants (0,505 s et 0,515 s) : deux périodes de 5 ms
            QVERIFY2(std::abs(t1 - 0.505) < 2e-5 && std::abs(t2 - 0.515) < 2e-5, qPrintable(u"%1 %2"_s.arg(t1).arg(t2)));
            QVERIFY(shows(w.get(), u"Δt = t₂ − t₁"_s));
            QVERIFY(shows(w.get(), u"10,00 ms"_s));
            // aucune période ni fréquence calculée à la place de l'élève, aucune option
            QVERIFY(!shows(w.get(), u"Aimanter t₁ et t₂ aux passages par zéro"_s));
            QVERIFY(!showsPart(w.get(), u"T ≈"_s) && !showsPart(w.get(), u"f₁ ="_s));
            hover(w->signalPlot(), signalAt(*w, 0.511));
            capture(w.get(), u"05_mesure_simplifie"_s);
        }
        // mode expert, aimantation décochée : les curseurs restent où on les lâche
        auto w = harness(Lang::Fr, true);
        const auto [t1, t2] = measureOnSquareWave(*w, false);
        QVERIFY2(std::abs(t1 - 0.505) > 5e-5, qPrintable(QString::number(t1)));
        QVERIFY(std::abs(t1 - 0.5051) < 1e-4 && std::abs(t2 - 0.5151) < 1e-4);
        capture(w.get(), u"06_mesure_expert"_s);
    }

    /// Lecture, pause et arrêt, sur une sortie audio simulée : l'extrait en mode simplifié ; tout le son
    /// ou l'extrait, au choix, en mode expert.
    void listeningToTheExtractAndTheSound()
    {
        {
            auto w = harness(Lang::Fr, false);
            w->player().setSimulated(true);
            load(*w, audio::Example::Flute);
            // mesure de la période : on écoute tout le son
            QVERIFY(shows(w.get(), u"Écouter le son"_s));
            w->findChild<QToolButton *>(u"play"_s)->click();
            QVERIFY(playback(*w).playing && playback(*w).t && *playback(*w).t < 0.2);
            w->findChild<QToolButton *>(u"stop"_s)->click();
            // étude des fréquences : l'extrait couvre les 80 % du milieu du son (3 s), et c'est lui qu'on
            // écoute
            showTab(*w, Tool::Extract);
            QVERIFY(shows(w.get(), u"Écouter l'extrait A–B"_s));
            const auto [a, b] = w->session().extract;
            QVERIFY(std::abs(a - 0.3) < 1e-9 && std::abs(b - 2.7) < 1e-9);
            QVERIFY(!w->findChild<QComboBox *>(u"listen"_s)->isVisibleTo(w.get()));
            PlayState p = playback(*w);
            QVERIFY(!p.playing && !p.paused && !p.t);
            auto *play = w->findChild<QToolButton *>(u"play"_s);
            auto *pause = w->findChild<QToolButton *>(u"pause"_s);
            auto *stop = w->findChild<QToolButton *>(u"stop"_s);
            // ▶ : depuis A ; la tête de lecture avance sur le signal
            play->click();
            p = playback(*w);
            QVERIFY(p.playing && p.t && *p.t >= a && *p.t < a + 0.2);
            QVERIFY(waitPlayback(*w, [&](const PlayState &q) { return q.t && *q.t > a + 0.3; }));
            hover(w->spectrumPlot(), QPointF(w->spectrumPlot()->screenX(1500.0), w->spectrumPlot()->plotRect().center().y()));
            capture(w.get(), u"07_ecoute"_s);
            // ⏸ : la tête de lecture s'arrête, ▶ reprend au même endroit
            pause->click();
            p = playback(*w);
            QVERIFY(!p.playing && p.paused);
            QTest::qWait(100);
            QCOMPARE(playback(*w).t, p.t);
            play->click();
            QVERIFY(playback(*w).t >= p.t);
            // ⏹ : plus de tête de lecture
            stop->click();
            p = playback(*w);
            QVERIFY(!p.playing && !p.paused && !p.t);
            // Espace : lecture, puis pause
            pressSpace(*w);
            QVERIFY(playback(*w).playing);
            pressSpace(*w);
            QVERIFY(playback(*w).paused);
        }

        // mode expert : tout le son depuis le début, puis l'extrait d'une demi-seconde, qui s'arrête seul
        auto w = harness(Lang::Fr, true);
        w->player().setSimulated(true);
        load(*w, audio::Example::Flute);
        const auto [a, b] = w->session().extract;
        w->findChild<QToolButton *>(u"play"_s)->click();
        PlayState p = playback(*w);
        QVERIFY(p.playing && p.t && *p.t < 0.2);
        // le choix de l'extrait pendant la lecture s'applique aussitôt
        auto *listen = w->findChild<QComboBox *>(u"listen"_s);
        QVERIFY(listen->isVisibleTo(w.get()));
        QCOMPARE(listen->currentText(), u"tout le son"_s);
        listen->setCurrentIndex(1);
        emit listen->activated(1);
        p = playback(*w);
        QVERIFY(p.playing && p.t && *p.t >= a - 1e-6 && *p.t < a + 0.2);
        QVERIFY(waitPlayback(*w, [&](const PlayState &q) { return q.t && *q.t > a + 0.2; }));
        QVERIFY(*playback(*w).t <= b + 1e-6);
        QVERIFY(waitPlayback(*w, [](const PlayState &q) { return !q.playing; }));
        p = playback(*w);
        QVERIFY(!p.playing && !p.paused && !p.t);
    }

    void englishInterface()
    {
        auto w = harness(Lang::En, false);
        QVERIFY(shows(w.get(), u"or drag a sound or video file onto the window"_s));
        capture(w.get(), u"09_en_accueil"_s);
        load(*w, audio::Example::Flute);
        showTab(*w, Tool::Extract);
        QVERIFY(shows(w.get(), u"Analysed extract"_s));
        hoverBesideTheFirstPeak(*w);
        capture(w.get(), u"10_en_flute"_s);
        measureOnSquareWave(*w, true);
        hover(w->signalPlot(), signalAt(*w, 0.511));
        capture(w.get(), u"11_en_measure"_s);
        showTab(*w, Tool::Extract);
        menu(*w, u"Expert"_s);
        menu(*w, u"⚙ Settings…"_s);
        QVERIFY(w->settingsDialog() && shows(w->settingsDialog(), u"Window function"_s));
        QVERIFY(waitAnalysis(*w));
        capture(w.get(), u"12_en_settings"_s, w->settingsDialog());
    }
};

QTEST_MAIN(UiTests)
#include "uitests.moc"
