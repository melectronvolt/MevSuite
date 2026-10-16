// Essais de l'interface : MeVPix piloté comme par un élève sur la photo d'exemple (figure de
// diffraction sur papier millimétré) : page d'accueil, ouvrir, étalonner, mesurer, profil
// d'intensité, thème et langue, panneau et barre d'état, sans fenêtre visible (plateforme
// « offscreen »). Chaque étape est rendue dans `captures/` (dossier de compilation), d'où viennent
// les images de la notice (tools/images_notice.py).

#include "app.h"
#include "canvas.h"
#include "plot.h"
#include "style.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

/// Photo d'exemple : 2148 × 732 px. Les lignes épaisses du papier millimétré (tous les centimètres)
/// passent par x = 568 et x = 1538 px ; la tache centrale va d'un minimum sombre à l'autre, de
/// x ≈ 563 à x ≈ 1544 px, sur la ligne y = 350 px.
const QString EXAMPLE = QStringLiteral(MEVPIX_SOURCES "/tests/data/diffraction1.png");
constexpr double GRID_Y = 520.0;
constexpr double FRINGE_Y = 350.0;

std::unique_ptr<MainWindow> harness(Lang lang, Qt::ColorScheme theme = Qt::ColorScheme::Light)
{
    Settings settings;
    settings.lang = lang;
    settings.theme = theme;
    auto w = std::make_unique<MainWindow>(settings, SettingsFiles{}, QString());
    w->resize(1280, 800);
    w->show();
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

/// Rendu de la fenêtre dans `captures/` (rien si `name` est vide).
void capture(QWidget *w, const QString &name)
{
    QTest::qWait(30);
    if (name.isEmpty())
        return;
    if (!w->grab().save(QDir(captures()).filePath(name + u".png"_s)))
        qWarning("capture impossible : %s", qPrintable(name));
}

bool load(MainWindow &w, const QString &path)
{
    w.loadPath(path);
    for (int i = 0; i < 1000 && w.busy(); ++i)
        QTest::qWait(10);
    QTest::qWait(30);
    // premier dessin : l'image s'ajuste à la zone d'affichage
    w.canvas()->grab();
    return w.session().image != nullptr;
}

void mouse(QWidget *widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

/// Glisser, clic gauche maintenu, entre deux points de l'image (coordonnées en pixels de l'image).
void dragOnImage(MainWindow &w, QPointF from, QPointF to)
{
    ImageCanvas *canvas = w.canvas();
    // mise en page à jour (le graphique du profil réduit la zone image, qui se réajuste) : les
    // positions à l'écran sont calculées avec le zoom affiché
    QTest::qWait(20);
    canvas->grab();
    const QPointF a = canvas->view().toScreen(from), b = canvas->view().toScreen(to);
    mouse(canvas, QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
    mouse(canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 10; ++k)
        mouse(canvas, QEvent::MouseMove, a + (b - a) * k / 10.0, Qt::NoButton, Qt::LeftButton);
    mouse(canvas, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

QAction *toolbarAction(MainWindow &w, const QString &text)
{
    for (QAction *action : w.findChildren<QAction *>()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

/// Raccourci de la fenêtre (`Ctrl` +, `Ctrl` 0…), comme au clavier.
void shortcut(MainWindow &w, const QKeySequence &keys)
{
    for (QAction *action : w.actions()) {
        if (action->shortcut() == keys)
            action->trigger();
    }
}

/// Vrai si la page d'accueil est affichée (aucune image ouverte).
bool welcomeShown(MainWindow &w)
{
    const auto *pages = qobject_cast<QStackedWidget *>(w.centralWidget());
    return pages && pages->currentWidget()->objectName() == u"welcome";
}

/// Couleur du nom du curseur A ou B dans le panneau.
QColor cursorNameColor(MainWindow &w, const QString &name)
{
    for (const QLabel *label : w.findChildren<QLabel *>()) {
        if (label->text() == name)
            return label->palette().color(QPalette::WindowText);
    }
    return {};
}

/// Police de l'interface (celle de l'application, agrandissement compris).
double uiPointSize()
{
    return QApplication::font().pointSizeF();
}

/// Étalonne sur 2 cm du papier millimétré, puis mesure la tache centrale.
void calibrateAndMeasure(MainWindow &w, const QString &name)
{
    dragOnImage(w, {568.0, GRID_Y}, {1538.0, GRID_Y});
    QTest::keyClicks(w.findChild<QLineEdit *>(u"calibLength"_s), u"20"_s);
    capture(&w, name.isEmpty() ? name : name + u"_etalonnage"_s);
    w.findChild<QPushButton *>(u"validate"_s)->click();
    // barre d'état : étalonnage validé, en vert
    const QLabel *status = w.findChild<QLabel *>(u"statusMessage"_s);
    QCOMPARE(status->palette().color(QPalette::WindowText), successColor(w.palette()));
    dragOnImage(w, {563.0, FRINGE_Y}, {1544.0, FRINGE_Y});
    capture(&w, name.isEmpty() ? name : name + u"_mesure"_s);
}

/// Profil d'intensité le long de la figure, curseurs A et B sur les minima de la tache centrale.
void profile(MainWindow &w, const QString &name)
{
    const Tr &t = texts(w.lang());
    toolbarAction(w, t.toolProfile)->trigger();
    dragOnImage(w, {40.0, FRINGE_Y}, {2100.0, FRINGE_Y});
    // curseurs posés au quart et aux trois quarts de la coupe, glissés sur les minima
    dragOnImage(w, {40.0 + 0.25 * 2060.0, FRINGE_Y}, {563.0, FRINGE_Y});
    dragOnImage(w, {40.0 + 0.75 * 2060.0, FRINGE_Y}, {1544.0, FRINGE_Y});
    capture(&w, name.isEmpty() ? name : name + u"_profil"_s);
}

/// Graphique du profil zoomé de 30 à 31,2 mm : la graduation 31,2 tombe sur le bord droit de la
/// courbe. Son étiquette reste entière, dans la marge de droite (rien ne touche le bord du widget).
void lastTickIsWhole(MainWindow &w)
{
    ProfilePlot *plot = w.plot();
    const double len = w.session().cut->lenPx() * w.session().calibration->unitsPerPx();
    plot->setView(30.0 / len, 31.2 / len);
    const QImage image = plot->grab().toImage();
    capture(&w, u"22_fr_profil_bord"_s);
    plot->resetView();
    const QRgb background = plot->palette().color(QPalette::Window).rgb();
    const qreal ratio = image.devicePixelRatio();
    for (int x = image.width() - int(std::ceil(3 * ratio)); x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y)
            QVERIFY2(image.pixel(x, y) == background, qPrintable(u"%1, %2"_s.arg(x).arg(y)));
    }
}

} // namespace

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY2(QFileInfo::exists(EXAMPLE), qPrintable(EXAMPLE));
    }

    /// Fenêtre de départ : pas agrandie, menus Fichier et Aide seulement (la langue et le thème sont
    /// des boutons de la barre d'outils), page d'accueil tant qu'aucune image n'est ouverte.
    void startWindow()
    {
        QVERIFY(!Settings().maximized);
        QCOMPARE(DEFAULT_WINDOW, QSize(1200, 700));
        auto w = harness(Lang::Fr);
        QVERIFY(!w->isMaximized() && !w->isFullScreen());
        QVERIFY(w->windowTitle() == u"MeVPix — Mesurer des longueurs"_s);
        QStringList titles;
        for (QAction *a : w->menuBar()->actions())
            titles << a->text();
        QCOMPARE(titles, (QStringList{u"Fichier"_s, u"Aide"_s}));
        // menu Aide : notice (F1), « À propos » ; pas d'émoji dans les menus
        QStringList help;
        for (QAction *a : w->menuBar()->actions().last()->menu()->actions())
            help << a->text();
        QCOMPARE(help, (QStringList{u"Notice d'utilisation…"_s, u"À propos de MeVPix…"_s, u"À propos de Qt…"_s}));
        QCOMPARE(w->menuBar()->actions().last()->menu()->actions().first()->shortcut(), QKeySequence(Qt::Key_F1));
        // barre de menus standard, sans feuille de style
        QVERIFY(w->menuBar()->styleSheet().isEmpty());
        QCOMPARE(w->findChildren<QToolBar *>().size(), 1);
        // sans image : la page d'accueil, outils grisés, pas d'échelle ni d'image dans la barre d'état
        QVERIFY(welcomeShown(*w));
        QVERIFY(!toolbarAction(*w, u"📏 Étalonner"_s)->isEnabled());
        QVERIFY(!w->findChild<QLabel *>(u"scale"_s)->isVisibleTo(w.get()));
        QVERIFY(!w->findChild<QLabel *>(u"imageStatus"_s)->isVisibleTo(w.get()));
        capture(w.get(), u"01_fr_accueil"_s);
        // l'image ouverte remplace l'accueil
        QVERIFY(load(*w, EXAMPLE));
        QVERIFY(!welcomeShown(*w));
        QVERIFY(toolbarAction(*w, u"📏 Étalonner"_s)->isEnabled());
        QVERIFY(w->findChild<QLabel *>(u"scale"_s)->isVisible());
        QVERIFY(w->findChild<QLabel *>(u"imageStatus"_s)->isVisible());
        // titres du panneau en demi-gras, à la taille du texte
        int headings = 0;
        for (const QLabel *label : w->findChildren<QLabel *>()) {
            const Tr &t = texts(Lang::Fr);
            if (label->text() == t.calibHeading || label->text() == t.cursors) {
                ++headings;
                QVERIFY2(label->font().weight() == QFont::DemiBold, qPrintable(label->text()));
                QVERIFY2(std::abs(label->font().pointSizeF() - uiPointSize()) < 0.01, qPrintable(label->text()));
            }
        }
        QCOMPARE(headings, 2);
        // le panneau garde sa largeur de départ, même affiché après l'accueil
        QTest::qWait(30);
        const auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
        QVERIFY2(side->width() >= 290, qPrintable(QString::number(side->width())));
    }

    /// Page d'accueil épurée : logo, nom, sous-titre, un bouton, une ligne ; tout tient sans
    /// chevauchement dans la plus petite fenêtre, en clair comme en sombre.
    void welcomePageIsSimpleAndFits()
    {
        for (const Lang lang : ALL_LANGS) {
            for (const auto theme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
                auto w = harness(lang, theme);
                w->resize(MIN_WINDOW);
                QTest::qWait(30);
                QCOMPARE(w->size(), MIN_WINDOW);
                QWidget *page = w->findChild<QWidget *>(u"welcome"_s);
                QVERIFY(page->isVisible());
                QList<QWidget *> shown;
                for (QWidget *child : page->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
                    if (child->isVisibleTo(page))
                        shown << child;
                }
                QCOMPARE(shown.size(), 5); // logo, nom, sous-titre, bouton, ligne
                QCOMPARE(page->findChildren<QPushButton *>().size(), 1);
                QCOMPARE(page->findChild<QPushButton *>(u"welcomeOpen"_s)->text(), texts(lang).openImage);
                for (QWidget *a : shown) {
                    QVERIFY2(page->rect().contains(a->geometry()), qPrintable(a->metaObject()->className()));
                    for (QWidget *b : shown) {
                        if (a != b)
                            QVERIFY2(!a->geometry().intersects(b->geometry()),
                                     qPrintable(u"%1 / %2"_s.arg(QString::fromLatin1(a->metaObject()->className()),
                                                                 QString::fromLatin1(b->metaObject()->className()))));
                    }
                }
                const bool dark = theme == Qt::ColorScheme::Dark;
                QCOMPARE(isDark(w->palette()), dark);
                if (lang == Lang::Fr)
                    capture(w.get(), dark ? u"03_fr_accueil_petite_sombre"_s : u"03_fr_accueil_petite"_s);
                else if (!dark)
                    capture(w.get(), u"13_en_accueil_petite"_s);
            }
        }
    }
    void calibrationMeasureAndProfile()
    {
        for (Lang lang : ALL_LANGS) {
            auto w = harness(lang);
            const QString name = u"%1_%2"_s.arg(lang == Lang::Fr ? u"02"_s : u"12"_s, langCode(lang));
            QVERIFY(load(*w, EXAMPLE));
            QCOMPARE(w->session().image->rgb.size(), QSize(2148, 732));
            calibrateAndMeasure(*w, name);
            // 2 cm = 970 px ; la tache centrale mesure environ 20 mm
            const auto &c = w->session().calibration;
            QVERIFY(c.has_value());
            QVERIFY2(std::abs(c->unitsPerPx() - 20.0 / 970.0) < 0.002 * 20.0 / 970.0,
                     qPrintable(QString::number(c->unitsPerPx())));
            QCOMPARE(w->session().measures.size(), size_t(1));
            const double mm = w->session().measures[0].lenPx() * c->unitsPerPx();
            QVERIFY2(std::abs(mm - 20.2) < 0.2, qPrintable(QString::number(mm)));
            profile(*w, name);
            QVERIFY(w->session().cut.has_value());
            // thème clair : curseurs plus foncés dans le panneau, vifs sur l'image
            QCOMPARE(cursorNameColor(*w, u"A"_s), QColor(215, 105, 0));
            QCOMPARE(cursorNameColor(*w, u"B"_s), QColor(40, 150, 0));
            if (lang == Lang::Fr)
                lastTickIsWhole(*w);
            QVERIFY(std::abs(w->session().cursorT[0] - 523.0 / 2060.0) < 0.01);
            QVERIFY(std::abs(w->session().cursorT[1] - 1504.0 / 2060.0) < 0.01);
            // Δx = |xB − xA| écrit comme les positions : ni gras, ni plus gros
            const QLabel *delta = w->findChild<QLabel *>(u"deltaX"_s);
            QVERIFY(delta->isVisible() && delta->text().contains(u"Δx"_s));
            QVERIFY(!delta->font().bold());
            QVERIFY(std::abs(delta->font().pointSizeF() - QApplication::font().pointSizeF()) < 0.01);
        }
    }

    /// En haut à droite de la barre de menus, dans cet ordre : thème (soleil ou lune), langue
    /// (drapeau) ; pas de bouton de notice (menu Aide, F1) ; boutons plats, de la hauteur d'un titre
    /// de menu : la barre de menus garde sa hauteur standard, même agrandie.
    void menuCornerButtons()
    {
        auto w = harness(Lang::Fr);
        QWidget *corner = w->menuBar()->cornerWidget(Qt::TopRightCorner);
        QVERIFY(corner && corner->isVisible());
        auto *theme = corner->findChild<QToolButton *>(u"theme"_s);
        auto *language = corner->findChild<QToolButton *>(u"language"_s);
        QVERIFY(theme && language);
        QCOMPARE(corner->findChildren<QToolButton *>().size(), 2);
        QCOMPARE(theme->defaultAction(), w->findChild<QAction *>(u"theme"_s));
        QCOMPARE(language->defaultAction(), w->findChild<QAction *>(u"language"_s));
        for (QToolButton *button : {theme, language}) {
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(button->autoRaise());
            QCOMPARE(button->focusPolicy(), Qt::NoFocus);
        }
        QVERIFY(theme->geometry().right() < language->geometry().left());
        // plus dans la barre d'outils
        auto *bar = w->findChild<QToolBar *>(u"toolbar"_s);
        QVERIFY(!bar->findChild<QToolButton *>(u"theme"_s));
        // hauteur standard de la barre de menus, à 100 % comme à 150 %
        const auto standard = [&] {
            const int title = w->menuBar()->actionGeometry(w->menuBar()->actions().first()).height();
            return w->menuBar()->height() <= title + 2;
        };
        QVERIFY2(standard(), qPrintable(QString::number(w->menuBar()->height())));
        const QSize normal = theme->iconSize();
        for (int i = 0; i < 5; ++i)
            shortcut(*w, QKeySequence(QKeySequence::ZoomIn));
        QTest::qWait(30);
        QVERIFY2(standard(), qPrintable(QString::number(w->menuBar()->height())));
        QVERIFY(theme->iconSize().height() > normal.height());
        shortcut(*w, QKeySequence(Qt::CTRL | Qt::Key_0));
        QTest::qWait(30);
        QCOMPARE(theme->iconSize(), normal);
    }

    /// L'agrandissement enregistré revient au démarrage ; une fenêtre trop étroite le baisse, et il
    /// revient quand elle s'élargit de nouveau.
    void savedZoomComesBack()
    {
        const double base = uiPointSize();
        Settings settings;
        settings.lang = Lang::Fr;
        settings.uiZoom = 1.5;
        auto w = std::make_unique<MainWindow>(settings, SettingsFiles{}, QString());
        w->resize(1280, 800);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::qWait(50);
        QVERIFY2(std::abs(uiPointSize() - 1.5 * base) < 0.01, qPrintable(QString::number(uiPointSize() / base)));
        // 1000 points de large : au plus 1000 / 800 = 125 %
        w->resize(1000, 800);
        QTest::qWait(50);
        QVERIFY2(std::abs(uiPointSize() - 1.25 * base) < 0.01, qPrintable(QString::number(uiPointSize() / base)));
        w->resize(1280, 800);
        QTest::qWait(50);
        QVERIFY2(std::abs(uiPointSize() - 1.5 * base) < 0.01, qPrintable(QString::number(uiPointSize() / base)));
        // Ctrl 0 : la police de l'application revient à sa taille, pour les essais suivants
        shortcut(*w, QKeySequence(Qt::CTRL | Qt::Key_0));
        QTest::qWait(30);
        QVERIFY(std::abs(uiPointSize() - base) < 0.01);
    }

    /// Le bouton de thème passe du soleil (clair) à la lune (sombre), et inversement.
    void themeButton()
    {
        auto w = harness(Lang::Fr, Qt::ColorScheme::Light);
        QVERIFY(!isDark(w->palette()));
        auto *button = w->findChild<QToolButton *>(u"theme"_s);
        QCOMPARE(button->toolTip(), u"Thème clair. Cliquer pour passer au thème sombre."_s);
        button->click();
        QTest::qWait(50);
        QVERIFY(isDark(w->palette()));
        QCOMPARE(button->toolTip(), u"Thème sombre. Cliquer pour passer au thème clair."_s);
        // thème sombre de la suite : fond de fenêtre presque noir
        QCOMPARE(w->palette().color(QPalette::Window), QColor(32, 33, 36));
        QCOMPARE(w->palette().color(QPalette::Base), QColor(22, 23, 25));
        capture(w.get(), u"21_fr_sombre_accueil"_s);
        QVERIFY(load(*w, EXAMPLE));
        calibrateAndMeasure(*w, u"21_fr_sombre"_s);
        profile(*w, u"21_fr_sombre"_s);
        // thème sombre : curseurs aux couleurs vives
        QCOMPARE(cursorNameColor(*w, u"A"_s), COLOR_A);
        QCOMPARE(cursorNameColor(*w, u"B"_s), COLOR_B);
        button->click();
        QTest::qWait(50);
        QCOMPARE(cursorNameColor(*w, u"A"_s), QColor(215, 105, 0));
        button->click();
        QTest::qWait(50);
        QVERIFY(isDark(w->palette()));
        button->click();
        QTest::qWait(50);
        QVERIFY(!isDark(w->palette()));
    }

    /// Le drapeau passe du français à l'anglais, et inversement.
    void languageButton()
    {
        auto w = harness(Lang::Fr);
        auto *button = w->findChild<QToolButton *>(u"language"_s);
        QCOMPARE(button->toolTip(), u"Langue : français. Cliquer pour passer en anglais (English)."_s);
        button->click();
        QCOMPARE(w->lang(), Lang::En);
        QCOMPARE(w->menuBar()->actions().first()->text(), u"File"_s);
        QVERIFY(toolbarAction(*w, texts(Lang::En).toolCalibrate));
        QCOMPARE(button->toolTip(), u"Language: English. Click to switch to French (français)."_s);
        QCOMPARE(w->findChild<QPushButton *>(u"welcomeOpen"_s)->text(), u"📂 Open an image…"_s);
        capture(w.get(), u"11_en_accueil"_s);
        button->click();
        QCOMPARE(w->lang(), Lang::Fr);
    }

    /// Barre d'état : une ligne, de même hauteur d'un message à l'autre ; un échec s'y lit en rouge.
    void statusBarIsOneLine()
    {
        auto w = harness(Lang::Fr);
        const int line = w->statusBar()->height();
        QVERIFY2(line < 2 * w->fontMetrics().height() + 4, qPrintable(QString::number(line)));
        QVERIFY(load(*w, EXAMPLE));
        QTest::qWait(30);
        QCOMPARE(w->statusBar()->height(), line);
        auto *message = w->findChild<QLabel *>(u"statusMessage"_s);
        const QColor normal = message->palette().color(QPalette::WindowText);
        // informations de droite : couleur discrète donnée par la feuille de style
        const QString style = w->statusBar()->styleSheet();
        const QColor weak = QApplication::palette().color(QPalette::PlaceholderText);
        QVERIFY2(style.contains(u"QLabel#imageStatus { color: rgba(%1, %2, %3, %4); }"_s.arg(weak.red())
                                    .arg(weak.green()).arg(weak.blue()).arg(weak.alpha())),
                 qPrintable(style));
        QLabel *image = w->findChild<QLabel *>(u"imageStatus"_s);
        const QImage shot = image->grab().toImage();
        // texte de l'image plus clair que le noir du texte normal (thème clair)
        int darkest = 255;
        for (int y = 0; y < shot.height(); ++y) {
            for (int x = 0; x < shot.width(); ++x)
                darkest = std::min(darkest, qGray(shot.pixel(x, y)));
        }
        QVERIFY2(darkest > 60, qPrintable(QString::number(darkest)));
        w = harness(Lang::Fr);
        message = w->findChild<QLabel *>(u"statusMessage"_s);
        w->loadPath(u"/nulle/part/image.png"_s);
        for (int i = 0; i < 200 && w->busy(); ++i)
            QTest::qWait(10);
        QTest::qWait(30);
        QVERIFY2(message->text().startsWith(u"Impossible d'ouvrir"_s), qPrintable(message->text()));
        QCOMPARE(message->palette().color(QPalette::WindowText), errorColor(w->palette()));
        QVERIFY(message->palette().color(QPalette::WindowText) != normal);
        QVERIFY(welcomeShown(*w));
        capture(w.get(), u"04_fr_erreur"_s);
    }

    /// Le panneau latéral n'est jamais plus étroit que son contenu : aucun texte rogné à droite, pour
    /// chaque outil, en anglais, et avec l'interface agrandie.
    void sidePanelIsNeverClipped()
    {
        for (const Lang lang : ALL_LANGS) {
            auto w = harness(lang);
            QVERIFY(load(*w, EXAMPLE));
            auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
            const auto fits = [&] {
                QTest::qWait(30);
                return side->widget()->width() <= side->viewport()->width();
            };
            const auto widths = [&] {
                return u"%1 > %2"_s.arg(side->widget()->width()).arg(side->viewport()->width());
            };
            const Tr &t = texts(lang);
            calibrateAndMeasure(*w, QString());
            for (const QString &tool : {t.toolCalibrate, t.toolMeasure}) {
                toolbarAction(*w, tool)->trigger();
                QVERIFY2(fits(), qPrintable(widths()));
            }
            profile(*w, QString());
            QCOMPARE(w->session().tool, Tool::Profile);
            QVERIFY2(fits(), qPrintable(widths()));
            // agrandissement de l'interface (Ctrl +) jusqu'à 160 % : le contenu dépasse la largeur initiale
            for (int i = 0; i < 6; ++i)
                shortcut(*w, QKeySequence(QKeySequence::ZoomIn));
            bool fitsZoomed = true;
            QString zoomedWidths;
            for (const QString &tool : {t.toolCalibrate, t.toolMeasure, t.toolProfile}) {
                toolbarAction(*w, tool)->trigger();
                if (!fits()) {
                    fitsZoomed = false;
                    zoomedWidths = widths();
                }
            }
            QCOMPARE(w->session().tool, Tool::Profile);
            // barre d'état agrandie elle aussi, toujours d'une ligne
            const QLabel *message = w->findChild<QLabel *>(u"statusMessage"_s);
            const bool statusZoomed = std::abs(message->font().pointSizeF() - uiPointSize()) < 0.01;
            const bool statusOneLine = w->statusBar()->height() < 2 * message->fontMetrics().height() + 4;
            if (lang == Lang::En)
                capture(w.get(), u"14_en_agrandi"_s);
            // Ctrl 0 : la police de l'application revient à sa taille, pour les essais suivants
            shortcut(*w, QKeySequence(Qt::CTRL | Qt::Key_0));
            QVERIFY2(fitsZoomed, qPrintable(zoomedWidths));
            QVERIFY(statusZoomed);
            QVERIFY(statusOneLine);
        }
    }
};

QTEST_MAIN(UiTests)
#include "uitests.moc"
