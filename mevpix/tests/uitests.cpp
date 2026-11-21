// Essais de mesure, de profil et d’interface sur une mire créée dans le dossier temporaire.

#include "about.h"
#include "app.h"
#include "canvas.h"
#include "crash.h"
#include "fichier_seance.h"
#include "plot.h"
#include "projet.h"
#include "style.h"
#include "tableau_mesures.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QClipboard>
#include <QCheckBox>
#include <QColorSpace>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
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

constexpr double REFERENCE_Y = 520.0;
constexpr double MESURE_Y = 350.0;

std::unique_ptr<FenetrePrincipale> banc_essai(Lang lang, Qt::ColorScheme theme = Qt::ColorScheme::Light)
{
    Reglages settings;
    settings.lang = lang;
    settings.theme = theme;
    auto w = std::make_unique<FenetrePrincipale>(settings, FichiersReglages{}, QString());
    w->resize(1280, 800);
    w->show();
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

/// Rendu de la fenêtre dans `captures/` (rien si `name` est vide).
void capture(QWidget *w, const QString &name)
{
    QTest::qWait(30);
    if (name.isEmpty())
        return;
    if (!w->grab().save(QDir(dossier_captures()).filePath(name + u".png"_s)))
        qWarning("capture impossible : %s", qPrintable(name));
}

bool charge(FenetrePrincipale &w, const QString &path)
{
    const auto avant = w.session().image;
    w.charge_chemin(path);
    for (int i = 0; i < 1000 && w.occupe(); ++i)
        QTest::qWait(10);
    QTest::qWait(30);
    // premier dessin : l'image s'ajuste à la zone d'affichage
    w.canevas()->grab();
    return !w.occupe() && w.session().image && w.session().image != avant;
}

void clique_corbeille(TableauMesures &tableau, int ligne)
{
    const QModelIndex cellule = tableau.model()->index(ligne, 2);
    QTest::mouseClick(tableau.viewport(), Qt::LeftButton, Qt::NoModifier, tableau.visualRect(cellule).center());
}

void souris(QWidget *widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

/// Glisser, clic gauche maintenu, entre deux points de l'image (coordonnées en pixels de l'image).
void glisse_sur_image(FenetrePrincipale &w, QPointF from, QPointF to)
{
    CanevasImage *canvas = w.canevas();
    // mise en page à jour (le graphique du profil réduit la zone image, qui se réajuste) : les
    // positions à l'écran sont calculées avec le zoom affiché
    QTest::qWait(20);
    canvas->grab();
    const QPointF a = canvas->vue().vers_ecran(from), b = canvas->vue().vers_ecran(to);
    souris(canvas, QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
    souris(canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 10; ++k)
        souris(canvas, QEvent::MouseMove, a + (b - a) * k / 10.0, Qt::NoButton, Qt::LeftButton);
    souris(canvas, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

QAction *action_barre_outils(FenetrePrincipale &w, const QString &text)
{
    for (QAction *action : w.findChildren<QAction *>()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

/// Pilote aussi les dialogues ouverts par le décodeur asynchrone ou depuis un autre dialogue.
template<class T, class F>
void prochain_dialogue(FenetrePrincipale &w, F repond, const QString &nom = {})
{
    auto *attente = new QTimer(&w);
    QObject::connect(attente, &QTimer::timeout, &w, [=] {
        auto *dialogue = qobject_cast<T *>(QApplication::activeModalWidget());
        if (!dialogue || (!nom.isEmpty() && dialogue->objectName() != nom))
            return;
        attente->stop();
        attente->deleteLater();
        repond(dialogue);
    });
    attente->start(10);
}

void choix_message(QMessageBox *boite, const QString &texte)
{
    for (auto *bouton : boite->buttons()) {
        if (bouton->text() == texte) {
            bouton->click();
            return;
        }
    }
    boite->reject();
    QFAIL(qPrintable(texte));
}

void chemin_sortie(FenetrePrincipale &w, const QString &path)
{
    prochain_dialogue<QFileDialog>(w, [path](QFileDialog *dialogue) {
        const QFileInfo cible(path);
        dialogue->setDirectory(cible.absolutePath());
        // « Enregistrer sous » peut proposer un fichier déjà sélectionné. Le test saisit
        // un nouveau nom, quel que soit le widget qui a reçu le clavier à l'ouverture.
        for (auto *vue : dialogue->findChildren<QAbstractItemView *>()) {
            if (vue->objectName() == u"listView" || vue->objectName() == u"treeView")
                vue->clearSelection();
        }
        auto *nom = dialogue->findChild<QLineEdit *>(u"fileNameEdit"_s);
        if (!nom) {
            dialogue->reject();
            QFAIL("Champ du nom de fichier introuvable");
        }
        nom->setText(cible.fileName());
        QMetaObject::invokeMethod(dialogue, "accept");
    });
}

/// Raccourci de la fenêtre (`Ctrl` +, `Ctrl` 0…), comme au clavier.
void raccourci(FenetrePrincipale &w, const QKeySequence &keys)
{
    for (QAction *action : w.actions()) {
        if (action->shortcut() == keys)
            action->trigger();
    }
}

/// Vrai si la page d'accueil est affichée (aucune image ouverte).
bool accueil_affiche(FenetrePrincipale &w)
{
    const auto *pages = qobject_cast<QStackedWidget *>(w.centralWidget());
    return pages && pages->currentWidget()->objectName() == u"welcome";
}

/// Couleur du nom du curseur A ou B dans le panneau.
QColor couleur_nom_curseur(FenetrePrincipale &w, const QString &name)
{
    for (const QToolButton *bouton : w.findChildren<QToolButton *>()) {
        if (bouton->text() == name)
            return bouton->palette().color(QPalette::ButtonText);
    }
    return {};
}

/// Police de l'interface (celle de l'application, agrandissement compris).
double taille_points_interface()
{
    return QApplication::font().pointSizeF();
}

/// Étalonne sur le repère noir de la mire, puis mesure le rectangle.
void etalonne_et_mesure(FenetrePrincipale &w, const QString &name)
{
    glisse_sur_image(w, {568.0, REFERENCE_Y}, {1538.0, REFERENCE_Y});
    QTest::keyClicks(w.findChild<QLineEdit *>(u"calibLength"_s), u"20"_s);
    capture(&w, name.isEmpty() ? name : name + u"_etalonnage"_s);
    w.findChild<QPushButton *>(u"validate"_s)->click();
    // barre d'état : étalonnage validé, en vert
    const QLabel *status = w.findChild<QLabel *>(u"statusMessage"_s);
    QCOMPARE(status->palette().color(QPalette::WindowText), couleur_reussite(w.palette()));
    glisse_sur_image(w, {563.0, MESURE_Y}, {1544.0, MESURE_Y});
    capture(&w, name.isEmpty() ? name : name + u"_mesure"_s);
}

/// Profil d'intensité le long de la mire, curseurs A et B aux bords du rectangle.
void profil(FenetrePrincipale &w, const QString &name)
{
    const Textes &t = textes(w.langue());
    action_barre_outils(w, t.toolProfile)->trigger();
    glisse_sur_image(w, {40.0, MESURE_Y}, {2100.0, MESURE_Y});
    // curseurs posés au quart et aux trois quarts de la coupe, glissés sur les bords du rectangle
    glisse_sur_image(w, {40.0 + 0.25 * 2060.0, MESURE_Y}, {563.0, MESURE_Y});
    glisse_sur_image(w, {40.0 + 0.75 * 2060.0, MESURE_Y}, {1544.0, MESURE_Y});
    capture(&w, name.isEmpty() ? name : name + u"_profil"_s);
}

/// Graphique du profil zoomé de 30 à 31,2 mm : la graduation 31,2 tombe sur le bord droit de la
/// courbe. Son étiquette reste entière, dans la marge de droite (rien ne touche le bord du widget).
void derniere_graduation_entiere(FenetrePrincipale &w)
{
    GraphiqueProfil *plot = w.graphique();
    const double len = w.session().coupe->longueur_px() * w.session().etalonnage->unites_par_pixel();
    plot->definit_vue(30.0 / len, 31.2 / len);
    const QImage image = plot->grab().toImage();
    capture(&w, u"22_fr_profil_bord"_s);
    plot->reinitialise_vue();
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

    QTemporaryDir dossier;
    QString mire;

private slots:
    void initTestCase()
    {
        QVERIFY(dossier.isValid());
        mire = dossier.filePath(u"mire.png"_s);
        QImage image(2148, 732, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        p.fillRect(563, 300, 981, 100, Qt::darkGray);
        p.fillRect(568, 500, 970, 40, Qt::black);
        p.end();
        QVERIFY(image.save(mire));
        // curseur de texte qui ne clignote pas : les captures sont les mêmes d'une
        // fois à l'autre
        QApplication::setCursorFlashTime(0);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    /// Fenêtre de départ avec les menus Fichier et Aide, page d'accueil sans image.
    void ouvre_fenetre()
    {
        QVERIFY(!Reglages().maximized);
        QCOMPARE(DEFAULT_WINDOW, QSize(1200, 700));
        auto w = banc_essai(Lang::Fr);
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
        QVERIFY(accueil_affiche(*w));
        QVERIFY(!action_barre_outils(*w, u"Étalonner"_s)->isEnabled());
        // le bouton d'ouverture de la barre d'outils garde ses points de suspension
        {
            auto *bar = w->findChild<QToolBar *>(u"toolbar"_s);
            auto *open = bar ? qobject_cast<QToolButton *>(bar->widgetForAction(action_barre_outils(*w, u"Ouvrir une image…"_s))) : nullptr;
            QVERIFY(open);
            QCOMPARE(open->text(), u"Ouvrir une image…"_s);
            QCOMPARE(open->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        }
        QVERIFY(!w->findChild<QLabel *>(u"scale"_s)->isVisibleTo(w.get()));
        QVERIFY(!w->findChild<QLabel *>(u"imageStatus"_s)->isVisibleTo(w.get()));
        capture(w.get(), u"01_fr_accueil"_s);
        // l'image ouverte remplace l'accueil
        QVERIFY(charge(*w, mire));
        QVERIFY(!accueil_affiche(*w));
        QVERIFY(action_barre_outils(*w, u"Étalonner"_s)->isEnabled());
        QVERIFY(w->findChild<QLabel *>(u"scale"_s)->isVisible());
        QVERIFY(w->findChild<QLabel *>(u"imageStatus"_s)->isVisible());
        // titres du panneau en demi-gras, à la taille du texte
        int headings = 0;
        for (const QLabel *label : w->findChildren<QLabel *>()) {
            const Textes &t = textes(Lang::Fr);
            if (label->text() == t.calibHeading || label->text() == t.cursors) {
                ++headings;
                QVERIFY2(label->font().weight() == QFont::DemiBold, qPrintable(label->text()));
                QVERIFY2(std::abs(label->font().pointSizeF() - taille_points_interface()) < 0.01, qPrintable(label->text()));
            }
        }
        QCOMPARE(headings, 2);
        // le panneau garde sa largeur de départ, même affiché après l'accueil
        QTest::qWait(30);
        const auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
        QVERIFY2(side->width() >= 290, qPrintable(QString::number(side->width())));
        // page d'accueil en anglais, au lancement
        auto en = banc_essai(Lang::En);
        QVERIFY(accueil_affiche(*en));
        capture(en.get(), u"11_en_accueil"_s);
    }

    /// Page d'accueil épurée : logo, nom, sous-titre, un bouton, une ligne ; tout tient sans
    /// chevauchement dans la plus petite fenêtre, en clair comme en sombre.
    void page_accueil_simple()
    {
        for (const Lang lang : LANGUES) {
            for (const auto theme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
                auto w = banc_essai(lang, theme);
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
                QCOMPARE(page->findChild<QPushButton *>(u"welcomeOpen"_s)->text(), textes(lang).openImage);
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
                QCOMPARE(est_sombre(w->palette()), dark);
                if (lang == Lang::Fr)
                    capture(w.get(), dark ? u"03_fr_accueil_petite_sombre"_s : u"03_fr_accueil_petite"_s);
                else if (!dark)
                    capture(w.get(), u"13_en_accueil_petite"_s);
            }
        }
    }
    void zoom_image_par_boutons()
    {
        for (const Lang lang : LANGUES) {
            auto w = banc_essai(lang);
            w->resize(MIN_WINDOW);
            auto *bar = w->findChild<QToolBar *>(u"toolbar"_s);
            QVERIFY(bar);
            const Textes &t = textes(lang);
            QVERIFY(accueil_affiche(*w) && !w->session().image);
            auto *canvas = w->canevas();
            auto *cadre = canvas->findChild<QFrame *>(u"imageZoomControls"_s);
            QVERIFY(cadre && !cadre->isVisible());

            QVERIFY(charge(*w, mire));
            const auto enBasADroite = [&] {
                return canvas->rect().contains(cadre->geometry())
                    && canvas->rect().bottomRight() - cadre->geometry().bottomRight() == QPoint(8, 8);
            };
            QVERIFY(cadre->isVisible() && enBasADroite());
            const auto boutons = cadre->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
            QCOMPARE(boutons.size(), 7);
            QCOMPARE(boutons[0]->text(), t.zoomInImage);
            QCOMPARE(boutons[1]->text(), t.zoomOutImage);
            QCOMPARE(boutons[2]->text(), t.fitWindow);
            for (size_t i = 0; i < t.panImage.size(); ++i) {
                auto *fleche = boutons[3 + i];
                QCOMPARE(fleche->toolTip(), t.panImage[i]);
                QVERIFY(!fleche->icon().isNull());
            }
            for (auto *bouton : boutons) {
                QVERIFY(bouton->isVisible() && bouton->isEnabled());
                QVERIFY(!bar->actions().contains(bouton->defaultAction()));
            }
            const VueImage &vue = canvas->vue();
            const double zoomInitial = vue.zoom();
            const QPointF centre = QRectF(canvas->rect()).center();
            const QPointF pointImage = vue.vers_image(centre);
            const auto deplacement = [&](QToolButton *bouton) {
                const QPointF avant = vue.vers_ecran(pointImage);
                QTest::mouseClick(bouton, Qt::LeftButton);
                return vue.vers_ecran(pointImage) - avant;
            };
            const QPointF gauche = deplacement(boutons[3]);
            QVERIFY(gauche.x() < 0 && gauche.y() == 0);
            QCOMPARE(deplacement(boutons[4]), -gauche);
            const QPointF haut = deplacement(boutons[5]);
            QVERIFY(haut.x() == 0 && haut.y() < 0);
            QCOMPARE(deplacement(boutons[6]), -haut);
            QCOMPARE(vue.zoom(), zoomInitial);
            QTest::mouseClick(boutons[0], Qt::LeftButton);
            QVERIFY(vue.zoom() > zoomInitial);
            QCOMPARE(vue.vers_image(centre), pointImage);
            const double zoomAgrandi = vue.zoom();
            const double avantAppui = vue.vers_ecran(pointImage).x();
            QTest::mousePress(boutons[4], Qt::LeftButton);
            QTRY_VERIFY(vue.vers_ecran(pointImage).x() > avantAppui + 1.5 * std::abs(gauche.x()));
            QTest::mouseRelease(boutons[4], Qt::LeftButton);
            QCOMPARE(vue.zoom(), zoomAgrandi);
            QVERIFY(!w->isWindowModified());
            QTest::mouseClick(boutons[1], Qt::LeftButton);
            QCOMPARE(vue.zoom(), zoomInitial);
            QTest::mouseClick(boutons[1], Qt::LeftButton);
            QVERIFY(vue.zoom() < zoomInitial);
            souris(canvas, QEvent::MouseButtonPress, centre, Qt::RightButton, Qt::RightButton);
            souris(canvas, QEvent::MouseMove, centre + QPointF(20, 30), Qt::NoButton, Qt::RightButton);
            souris(canvas, QEvent::MouseButtonRelease, centre + QPointF(20, 30), Qt::RightButton, Qt::NoButton);
            QTest::mouseClick(boutons[2], Qt::LeftButton);
            QTRY_COMPARE(vue.zoom(), zoomInitial);
            QCOMPARE(vue.vers_image(centre), pointImage);
            QVERIFY(!w->session().segmentEtalon);
            action_barre_outils(*w, t.toolProfile)->trigger();
            QTRY_VERIFY(w->graphique()->isVisible() && enBasADroite());
            w->resize(DEFAULT_WINDOW);
            QTRY_VERIFY(enBasADroite());

            // Réduire la zone image comme en tirant le séparateur vers la droite.
            auto *separateur = qobject_cast<QSplitter *>(canvas->parentWidget()->parentWidget());
            QVERIFY(separateur && separateur->orientation() == Qt::Horizontal);
            separateur->setSizes({separateur->width(), 1});
            QTRY_VERIFY(boutons[3]->geometry().top() > boutons[0]->geometry().bottom() && enBasADroite());
            separateur->setSizes({1, separateur->width()});
            QTRY_VERIFY(boutons[3]->geometry().center().y() == boutons[0]->geometry().center().y() && enBasADroite());
        }
    }

    void etalonnage_mesure_profil()
    {
        for (Lang lang : LANGUES) {
            auto w = banc_essai(lang);
            const QString name = u"%1_%2"_s.arg(lang == Lang::Fr ? u"02"_s : u"12"_s, code_langue(lang));
            QVERIFY(charge(*w, mire));
            QCOMPARE(w->session().image->rgb.size(), QSize(2148, 732));
            etalonne_et_mesure(*w, name);
            // 2 cm = 970 px ; le rectangle mesure environ 20 mm
            const auto &c = w->session().etalonnage;
            QVERIFY(c.has_value());
            QVERIFY2(std::abs(c->unites_par_pixel() - 20.0 / 970.0) < 0.002 * 20.0 / 970.0,
                     qPrintable(QString::number(c->unites_par_pixel())));
            QCOMPARE(w->session().mesures.size(), size_t(1));
            const double mm = w->session().mesures[0].longueur_px() * c->unites_par_pixel();
            QVERIFY2(std::abs(mm - 20.2) < 0.2, qPrintable(QString::number(mm)));
            profil(*w, name);
            QVERIFY(w->session().coupe.has_value());
            // thème clair : curseurs plus foncés dans le panneau, vifs sur l'image
            QCOMPARE(couleur_nom_curseur(*w, u"A"_s), QColor(215, 105, 0));
            QCOMPARE(couleur_nom_curseur(*w, u"B"_s), QColor(40, 150, 0));
            if (lang == Lang::Fr)
                derniere_graduation_entiere(*w);
            QVERIFY(std::abs(w->session().fractionsCurseurs[0] - 523.0 / 2060.0) < 0.01);
            QVERIFY(std::abs(w->session().fractionsCurseurs[1] - 1504.0 / 2060.0) < 0.01);
            // Δx = |xB − xA| écrit comme les positions : ni gras, ni plus gros
            const QLabel *delta = w->findChild<QLabel *>(u"deltaX"_s);
            QVERIFY(delta->isVisible() && delta->text().contains(u"Δx"_s));
            QVERIFY(!delta->font().bold());
            QVERIFY(std::abs(delta->font().pointSizeF() - QApplication::font().pointSizeF()) < 0.01);
        }
    }

    void commandes_illustrees()
    {
        for (Lang lang : LANGUES) {
            auto w = banc_essai(lang);
            const Textes &t = textes(lang);
            const QString suffixe = u"-"_s + code_langue(lang);
            QVERIFY(charge(*w, mire));
            etalonne_et_mesure(*w, {});
            profil(*w, {});
            QWidget *commandesZoom = w->findChild<QWidget *>(u"imageZoomControls"_s);
            QVERIFY(commandesZoom && commandesZoom->isVisible());
            capture(commandesZoom, u"zoom"_s + suffixe);
            for (QAction *titre : w->menuBar()->actions()) {
                QMenu *menu = titre->menu();
                QVERIFY(menu);
                for (QAction *action : menu->actions()) {
                    if (action->isSeparator())
                        continue;
                    QVERIFY2(action->isIconVisibleInMenu() && !action->icon().pixmap(16, 16).isNull(), qPrintable(action->text()));
                }
                menu->popup(w->menuBar()->mapToGlobal(w->menuBar()->actionGeometry(titre).bottomLeft()));
                capture(menu, (titre->text() == t.menuFile ? u"menu-fichier"_s : u"menu-aide"_s) + suffixe);
                menu->hide();
            }
            for (const QString &texte : {t.openImage, t.toolCalibrate, t.toolMeasure, t.toolProfile}) {
                const auto *action = action_barre_outils(*w, texte);
                QVERIFY2(action && !action->icon().pixmap(20, 20).isNull(), qPrintable(texte));
            }
            prochain_dialogue<QDialog>(*w, [&](QDialog *dialogue) {
                for (auto *caseACocher : dialogue->findChildren<QCheckBox *>())
                    if (caseACocher->text() == t.includeGraph)
                        caseACocher->setChecked(true);
                capture(dialogue, u"annotation"_s + suffixe);
                dialogue->reject();
            });
            action_barre_outils(*w, t.annotatedImage)->trigger();
            action_barre_outils(*w, t.toolCalibrate)->trigger();
            auto *longueur = w->findChild<QLineEdit *>(u"calibLength"_s);
            longueur->selectAll();
            QTest::keyClicks(longueur, u"25"_s);
            QVERIFY(w->session().etalonnage_en_attente());
            prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *boite) {
                capture(boite, u"echelle"_s + suffixe);
                choix_message(boite, t.cancel);
            }, u"confirmScale"_s);
            action_barre_outils(*w, t.annotatedImage)->trigger();
            prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *boite) {
                capture(boite, u"sauvegarde"_s + suffixe);
                choix_message(boite, t.cancel);
            }, u"confirmQuit"_s);
            QVERIFY(!w->close());
        }
    }

    void copie_image_annotee()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        etalonne_et_mesure(*w, QString());
        QAction *annoter = nullptr;
        for (auto *action : w->findChildren<QAction *>())
            if (action->text() == textes(Lang::Fr).annotatedImage)
                annoter = action;
        QVERIFY(annoter && annoter->isEnabled());
        QTimer::singleShot(0, w.get(), [&] {
            auto *boite = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!boite)
                return;
            if (auto *choix = boite->findChild<QComboBox *>())
                choix->setCurrentIndex(1);
            for (auto *bouton : boite->findChildren<QPushButton *>()) {
                if (bouton->text() == textes(Lang::Fr).copyAnnotation) {
                    bouton->click();
                    return;
                }
            }
            boite->reject();
        });
        annoter->trigger();
        const QImage image = QApplication::clipboard()->image();
        const auto &original = w->session().image->rgb;
        QCOMPARE(image.size(), original.size());
        QCOMPARE(image.pixelColor(0, 0), original.pixelColor(0, 0));
        QVERIFY(image.pixelColor(600, int(MESURE_Y)) != original.pixelColor(600, int(MESURE_Y)));
    }

    void annule_deplacement_et_suppressions()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        const Textes &t = textes(Lang::Fr);
        action_barre_outils(*w, t.toolMeasure)->trigger();
        glisse_sur_image(*w, {200, 200}, {500, 200});
        glisse_sur_image(*w, {250, 500}, {550, 500});
        const auto avant = w->session().mesures;
        QCOMPARE(avant.size(), 2);
        auto *canvas = w->canevas();
        canvas->setFocus();
        const QPointF a = canvas->vue().vers_ecran(avant[0].a);
        const QPointF b = a + QPointF(35, 20);
        souris(canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        souris(canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
        QVERIFY(w->session().mesures != avant);
        QTest::keyClick(canvas, Qt::Key_Escape);
        souris(canvas, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(w->session().mesures, avant);
        glisse_sur_image(*w, avant[0].a, {310, 350});
        QVERIFY(w->session().mesures != avant);
        QTest::keySequence(canvas, QKeySequence::Undo);
        QCOMPARE(w->session().mesures, avant);

        auto *table = w->findChild<TableauMesures *>();
        clique_corbeille(*table, 0);
        QCOMPARE(w->session().mesures, QList<Segment>{avant[1]});
        raccourci(*w, QKeySequence::Undo);
        QCOMPARE(w->session().mesures, avant);
        for (auto *bouton : w->findChildren<QPushButton *>())
            if (bouton->text() == t.clearAll)
                bouton->click();
        QVERIFY(w->session().mesures.empty());
        raccourci(*w, QKeySequence::Undo);
        QCOMPARE(w->session().mesures, avant);
        raccourci(*w, QKeySequence::Undo);
        QCOMPARE(w->session().mesures, QList<Segment>{avant[0]});
    }

    void double_clic_et_clavier_du_profil()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        const Textes &t = textes(Lang::Fr);
        action_barre_outils(*w, t.toolProfile)->trigger();
        glisse_sur_image(*w, {100, 350}, {1100, 350});
        const auto avant = w->session().fractionsCurseurs;
        GraphiqueProfil *plot = w->graphique();
        QCOMPARE(QApplication::focusWidget(), plot);
        const QPoint centre = plot->rect().center();
        const QImage ensemble = plot->grab().toImage();
        plot->definit_vue(0.05, 0.4);
        QVERIFY(plot->grab().toImage() != ensemble);
        QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseDClick(plot, Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseRelease(plot, Qt::LeftButton, Qt::NoModifier, centre);
        QTest::qWait(QApplication::doubleClickInterval() + 40);
        QCOMPARE(w->session().fractionsCurseurs, avant);
        QEvent quitte(QEvent::Leave);
        QApplication::sendEvent(plot, &quitte);
        QCOMPARE(plot->grab().toImage(), ensemble);
        QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier, centre);
        QTRY_VERIFY_WITH_TIMEOUT(w->session().fractionsCurseurs != avant, QApplication::doubleClickInterval() + 700);
        QTest::keySequence(plot, QKeySequence::Undo);
        QCOMPARE(w->session().fractionsCurseurs, avant);

        QToolButton *boutonB = nullptr;
        for (auto *b : w->findChildren<QToolButton *>())
            if (b->text() == u"B")
                boutonB = b;
        QVERIFY(boutonB);
        boutonB->click();
        QVERIFY(boutonB->isChecked() && plot->hasFocus());
        const double dernier = std::ceil(w->session().coupe->longueur_px());
        QTest::keyClick(plot, Qt::Key_Left);
        QCOMPARE(w->session().fractionsCurseurs[0], avant[0]);
        QCOMPARE(w->session().fractionsCurseurs[1], (std::round(avant[1] * dernier) - 1) / dernier);
        QTest::keySequence(plot, QKeySequence::Undo);
        QCOMPARE(w->session().fractionsCurseurs, avant);
        QTest::keyClick(plot, Qt::Key_A);
        QVERIFY(!boutonB->isChecked());
        QTest::keyClick(plot, Qt::Key_Right);
        QCOMPARE(w->session().fractionsCurseurs[0], (std::round(avant[0] * dernier) + 1) / dernier);
        const auto clavier = w->session().fractionsCurseurs;
        souris(plot, QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
        souris(plot, QEvent::MouseMove, centre + QPoint(64, 0), Qt::NoButton, Qt::LeftButton);
        QVERIFY(w->session().fractionsCurseurs != clavier);
        QTest::keyClick(plot, Qt::Key_Escape);
        souris(plot, QEvent::MouseButtonRelease, centre + QPoint(64, 0), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(w->session().fractionsCurseurs, clavier);
        QTest::keySequence(plot, QKeySequence::Undo);
        QTest::keySequence(plot, QKeySequence::Undo);
        QVERIFY(!w->session().coupe); // double-clic, sélection et Échap n'ont ajouté aucune édition
    }

    void graduations_aux_echelles_extremes()
    {
        Seance seance;
        auto image = std::make_shared<ImageChargee>();
        image->rgb = QImage(10, 1, QImage::Format_RGB888);
        image->rgb.fill(Qt::white);
        seance.definit_image(image);
        seance.coupe = Segment{{0, 0.5}, {10, 0.5}};
        GraphiqueProfil graphique(seance);
        graphique.resize(640, 200);
        graphique.definit_traductions(&textes(Lang::Fr));
        for (double longueur : {1.75e308, 1e-305}) {
            seance.etalonnage = Etalonnage::fabrique(10, longueur, Unite::Metre, image->diagonale_px());
            QVERIFY(seance.etalonnage);
            // La marge droite de 5 % peut dépasser le plus grand double.
            // Le même rendu sert à l'écran et à l'export du profil annoté.
            const QImage rendu = graphique.grab().toImage();
            QVERIFY(!rendu.isNull());
        }
    }

    void survol_et_extrema_par_colonne()
    {
        Seance seance;
        auto img = std::make_shared<ImageChargee>();
        img->rgb = QImage(16384, 1, QImage::Format_RGB888);
        img->rgb.fill(Qt::black);
        const Segment coupe{{0.5, 0.5}, {16383.5, 0.5}};
        seance.definit_image(img);
        seance.coupe = coupe;
        seance.largeurBande = 1;
        GraphiqueProfil plot(seance);
        plot.resize(800, 250);
        plot.definit_traductions(&textes(Lang::Fr));
        const QImage plat = plot.grab().toImage();
        img->rgb.setPixelColor(8192, 0, Qt::white);
        seance.definit_image(img);
        seance.coupe = coupe;
        const QImage pic = plot.grab().toImage();
        // Un seul pixel blanc, au milieu de plus de 20 échantillons par colonne.
        int lignesDuPic = 0;
        for (int y = 20; y < pic.height() - 50; ++y) {
            for (int x = pic.width() / 3; x < pic.width() * 2 / 3; ++x) {
                if (pic.pixel(x, y) != plat.pixel(x, y)) {
                    ++lignesDuPic;
                    break;
                }
            }
        }
        QVERIFY(lignesDuPic > 100);
        souris(&plot, QEvent::MouseMove, {320, 35}, Qt::NoButton, Qt::NoButton);
        const QImage haut = plot.grab().toImage();
        QVERIFY(haut != pic);
        souris(&plot, QEvent::MouseMove, {320, 145}, Qt::NoButton, Qt::NoButton);
        QCOMPARE(plot.grab().toImage(), haut); // même abscisse, même point et même intensité
    }

    void curseurs_superposes_en_unite_etalonnee()
    {
        QTemporaryDir dir;
        Seance seance;
        seance.definit_image(charge_image(mire).image);
        seance.outil = Outil::Profil;
        seance.coupe = Segment{{50, 30}, {150, 30}};
        seance.segmentEtalon = Segment{{50, 75}, {150, 75}};
        seance.saisieLongueur = u"1"_s;
        seance.etalonnage = seance.etalonnage_saisi();
        const QString fichier = dir.filePath(u"profil.mevpix"_s);
        QVERIFY(enregistre_seance(fichier, seance).reussie());
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, fichier));
        QTest::keyClick(w->graphique(), Qt::Key_B);
        for (int i = 0; i < 50; ++i)
            QTest::keyClick(w->graphique(), Qt::Key_Left);
        QCOMPARE(w->session().fractionsCurseurs[1], 0.25);
        for (const auto &nom : {u"positionA"_s, u"positionB"_s})
            QCOMPARE(w->findChild<QLabel *>(nom)->text(), u"x = 0,250 mm"_s);
        QVERIFY(w->findChild<QLabel *>(u"deltaX"_s)->text().endsWith(u"= 0 mm"_s));
    }

    void trace_dans_la_marge_et_coupe_trop_courte()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        const Textes &t = textes(Lang::Fr);
        action_barre_outils(*w, t.toolMeasure)->trigger();
        auto *canvas = w->canevas();
        canvas->grab();
        const double haut = canvas->vue().vers_ecran({0, 0}).y();
        QVERIFY(haut > 10);
        // Le geste entier se trouve dans la marge au-dessus de la mire.
        const QPointF debut(100, haut / 2), fin(260, haut / 2);
        souris(canvas, QEvent::MouseButtonPress, debut, Qt::LeftButton, Qt::LeftButton);
        souris(canvas, QEvent::MouseMove, fin, Qt::NoButton, Qt::LeftButton);
        souris(canvas, QEvent::MouseButtonRelease, fin, Qt::LeftButton, Qt::NoButton);
        QVERIFY(w->session().mesures.isEmpty());
        QVERIFY(!w->isWindowModified());

        // Un geste commencé sur la photo peut toujours atteindre son bord.
        glisse_sur_image(*w, {200, 50}, {200, -40});
        QCOMPARE(w->session().mesures.size(), 1);
        QCOMPARE(w->session().mesures[0].b.y(), 0.0);
        action_barre_outils(*w, t.toolProfile)->trigger();
        glisse_sur_image(*w, {300, 300}, {800, 300});
        const auto coupe = w->session().coupe;
        QVERIFY(coupe);
        glisse_sur_image(*w, coupe->b, coupe->a + QPointF(0.4, 0));
        QCOMPARE(w->session().coupe, coupe);
        raccourci(*w, QKeySequence::Undo);
        QVERIFY(!w->session().coupe);  // Le geste refusé n'a pas consommé une annulation.
    }

    void ancienne_coupe_reparable()
    {
        QTemporaryDir dir;
        Seance seance;
        seance.definit_image(charge_image(mire).image);
        seance.outil = Outil::Profil;
        seance.coupe = Segment{{300, 350}, {300, 350}};
        seance.mesures.append({{100, 100}, {200, 100}});
        const QString fichier = dir.filePath(u"ancienne.mevpix"_s);
        QVERIFY(enregistre_seance(fichier, seance).reussie());
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, fichier));
        QCOMPARE(w->session().mesures, seance.mesures);
        QCOMPARE(w->session().coupe, seance.coupe);
        const auto &t = textes(Lang::Fr);
        QPushButton *csv = nullptr;
        for (auto *bouton : w->findChildren<QPushButton *>())
            if (bouton->text() == t.exportCsv)
                csv = bouton;
        QVERIFY(csv && !csv->isVisible());
        // Un clic sur le graphique vide d'une ancienne coupe ne déplace pas A ou B.
        QTest::mouseClick(w->graphique(), Qt::LeftButton, Qt::NoModifier, w->graphique()->rect().center());
        w->graphique()->termine_clic();
        QCOMPARE(w->session().fractionsCurseurs, seance.fractionsCurseurs);
        QVERIFY(!w->isWindowModified());
        glisse_sur_image(*w, {300, 350}, {800, 350});
        QVERIFY(w->session().coupe_mesurable());
        QVERIFY(csv->isVisible() && csv->isEnabled());
    }

    void deplacement_actualise_une_seule_ligne()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        action_barre_outils(*w, textes(Lang::Fr).toolMeasure)->trigger();
        glisse_sur_image(*w, {200, 300}, {400, 300});
        glisse_sur_image(*w, {200, 500}, {400, 500});
        auto *tableau = w->findChild<TableauMesures *>();
        const QString autre = tableau->model()->index(1, 1).data().toString();
        // Une remise en page complète écraserait cette hauteur témoin.
        tableau->setRowHeight(1, 65);
        glisse_sur_image(*w, {400, 300}, {500, 300});
        QCOMPARE(tableau->model()->index(0, 1).data().toString(), u"300 px"_s);
        QCOMPARE(tableau->model()->index(1, 1).data().toString(), autre);
        QCOMPARE(tableau->rowHeight(1), 65);
        w->canevas()->zoome_au_centre(2.0);
        w->canevas()->grab();
        QCOMPARE(tableau->rowHeight(1), 65);
    }

    void tableau_long_et_corbeille_au_clavier()
    {
        Seance seance;
        for (int i = 1; i <= 10000; ++i)
            seance.mesures.append({{0, 0}, {double(i), 0}});
        TableauMesures tableau(nullptr);
        tableau.affiche(seance, textes(Lang::Fr));
        tableau.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tableau));
        const QModelIndex derniere = tableau.model()->index(9999, 2);
        tableau.setCurrentIndex(derniere);
        QTRY_VERIFY(tableau.verticalScrollBar()->value() > 0);
        QVERIFY(tableau.viewport()->rect().contains(tableau.visualRect(derniere).center()));
        QCOMPARE(derniere.siblingAtColumn(0).data().toString(), u"M10000"_s);
        QSignalSpy suppression(&tableau, &TableauMesures::suppression_demandee);
        QTest::keyClick(&tableau, Qt::Key_Space);
        QCOMPARE(suppression.count(), 1);
        QCOMPARE(suppression.first().first().toInt(), 9999);
    }

    void interruption_du_geste_sur_image()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        auto *canvas = w->canevas();
        const QPointF centre = canvas->rect().center();
        souris(canvas, QEvent::MouseButtonPress, centre, Qt::RightButton, Qt::RightButton);
        QEvent perteSouris(QEvent::UngrabMouse);
        QApplication::sendEvent(canvas, &perteSouris);
        const QPointF origine = canvas->vue().vers_ecran({});
        souris(canvas, QEvent::MouseMove, centre + QPoint(20, 30), Qt::NoButton, Qt::NoButton);
        QCOMPARE(canvas->vue().vers_ecran({}), origine);
        QVERIFY(!w->isWindowModified());
    }

    void ouverture_refusee_garde_la_mesure()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        // Le lecteur de fond a rendu son exemplaire de la photo à la séance.
        QCOMPARE(w->session().image.use_count(), 1L);
        const auto image = w->session().image;
        const Textes &t = textes(Lang::Fr);
        action_barre_outils(*w, t.toolMeasure)->trigger();
        glisse_sur_image(*w, {150, 300}, {230, 300});
        const auto mesures = w->session().mesures;
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { choix_message(b, t.cancel); }, u"confirmReplace"_s);
        QVERIFY(!charge(*w, mire));

        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"transfert.jpg"_s);
        QVERIFY(image->rgb.save(chemin, "JPEG"));
        QFile transfert(chemin);
        QVERIFY(transfert.resize(transfert.size() * 6 / 10));
        bool demandeAbandon = false;
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) {
            demandeAbandon = true;
            choix_message(b, t.cancel);
        }, u"confirmReplace"_s);
        QVERIFY(!charge(*w, chemin));
        QVERIFY(!demandeAbandon);
        QVERIFY(w->findChild<QLabel *>(u"statusMessage"_s)->text().contains(t.incompleteJpeg));
        QCOMPARE(w->session().image, image);
        QCOMPARE(w->session().mesures, mesures);
        QVERIFY(w->isWindowModified());
    }

    void edition_et_fermeture_pendant_sauvegarde()
    {
        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"travail.mevpix"_s);
        Seance seance;
        seance.definit_image(charge_image(mire).image);
        seance.outil = Outil::Mesurer;
        QVERIFY(enregistre_seance(chemin, seance).reussie());
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, chemin));
        glisse_sur_image(*w, {150, 300}, {230, 300});
        const Textes &t = textes(Lang::Fr);
        auto *sauver = action_barre_outils(*w, t.saveSession);
        sauver->trigger();
        QVERIFY(w->occupe());
        auto *tableau = w->findChild<TableauMesures *>();
        clique_corbeille(*tableau, 0);
        QVERIFY(w->session().mesures.isEmpty());

        // Après l'écriture, la suppression reste à enregistrer. Fermer doit la signaler.
        bool fermetureDemandee = false;
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) {
            fermetureDemandee = true;
            choix_message(b, t.cancel);
        }, u"confirmQuit"_s);
        QVERIFY(!w->close());
        QTRY_VERIFY(fermetureDemandee);
        QVERIFY(w->isVisible() && w->isWindowModified());
        QCOMPARE(charge_image(chemin).seance["mesures"].toArray().size(), 1);

        // Le 2e Ctrl+S prend la mesure rétablie. Sa suppression ultérieure reste à sauver.
        sauver->trigger();
        raccourci(*w, QKeySequence::Undo);
        QCOMPARE(w->session().mesures.size(), 1);
        sauver->trigger();
        clique_corbeille(*tableau, 0);
        QTRY_VERIFY(!w->occupe());
        QCOMPARE(charge_image(chemin).seance["mesures"].toArray().size(), 1);
        QVERIFY(w->isWindowModified());
        QVERIFY(w->session().mesures.isEmpty());

        sauver->trigger();
        QVERIFY(!w->close());
        QTRY_VERIFY(!w->isVisible());
        QCOMPARE(charge_image(chemin).seance["mesures"].toArray().size(), 0);
    }

    void ouvrir_pendant_sauvegarde_data()
    {
        QTest::addColumn<bool>("verrouille");
        QTest::newRow("écriture réussie") << false;
        QTest::newRow("fichier occupé") << true;
    }

    void ouvrir_pendant_sauvegarde()
    {
        QFETCH(bool, verrouille);
        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"travail.mevpix"_s);
        Seance seance;
        seance.definit_image(charge_image(mire).image);
        seance.outil = Outil::Mesurer;
        QVERIFY(enregistre_seance(chemin, seance).reussie());
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, chemin));
        const auto ancienneImage = w->session().image;
        glisse_sur_image(*w, {150, 300}, {230, 300});
        const Textes &t = textes(Lang::Fr);
        QLockFile verrou(chemin + u".lock"_s);
        if (verrouille) {
            QVERIFY(verrou.tryLock());
            prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) {
                w->charge_chemin(mire);
                choix_message(b, t.cancel);
            }, u"conflitSeance"_s);
        }
        action_barre_outils(*w, t.saveSession)->trigger();
        if (!verrouille)
            w->charge_chemin(mire);
        QTRY_VERIFY(!w->occupe());
        QCOMPARE(w->session().image == ancienneImage, verrouille);
        QCOMPARE(w->isWindowModified(), verrouille);
        QCOMPARE(w->session().mesures.size(), verrouille ? 1 : 0);
        QCOMPARE(charge_image(chemin).seance["mesures"].toArray().size(), verrouille ? 0 : 1);
    }

    void deux_fenetres_conservent_leurs_mesures()
    {
        QTemporaryDir dir;
        const QString original = dir.filePath(u"classe.mevpix"_s);
        const QString copie = dir.filePath(u"autre.mevpix"_s);
        Seance seance;
        seance.definit_image(charge_image(mire).image);
        seance.outil = Outil::Mesurer;
        QVERIFY(enregistre_seance(original, seance).reussie());
        auto premier = banc_essai(Lang::Fr);
        auto second = banc_essai(Lang::Fr);
        QVERIFY(charge(*premier, original));
        QVERIFY(charge(*second, original));
        glisse_sur_image(*premier, {150, 300}, {230, 300});
        glisse_sur_image(*second, {150, 300}, {270, 300});
        const auto &t = textes(Lang::Fr);
        action_barre_outils(*premier, t.saveSession)->trigger();
        QTRY_VERIFY(!premier->occupe());
        const auto version = charge_image(original).empreinteSeance;
        QVERIFY(!version.isEmpty());
        prochain_dialogue<QMessageBox>(*second, [&](QMessageBox *b) { choix_message(b, t.cancel); }, u"conflitSeance"_s);
        action_barre_outils(*second, t.saveSession)->trigger();
        QTRY_VERIFY(!second->occupe());
        QVERIFY(second->isWindowModified());
        QCOMPARE(charge_image(original).empreinteSeance, version);
        QCOMPARE(qRound(second->session().mesures[0].longueur_px()), 120);

        // Choisir de nouveau le nom du fichier ouvert garde la même protection.
        chemin_sortie(*second, original);
        prochain_dialogue<QMessageBox>(*second, [&](QMessageBox *b) { choix_message(b, t.cancel); }, u"conflitSeance"_s);
        action_barre_outils(*second, t.saveSessionAs)->trigger();
        QTRY_VERIFY(!second->occupe());
        QVERIFY(second->isWindowModified());
        QCOMPARE(charge_image(original).empreinteSeance, version);

        prochain_dialogue<QMessageBox>(*second, [&](QMessageBox *b) { choix_message(b, t.saveSession); }, u"confirmQuit"_s);
        prochain_dialogue<QMessageBox>(*second, [&](QMessageBox *b) { choix_message(b, t.saveCopy); }, u"conflitSeance"_s);
        chemin_sortie(*second, copie);
        second->close();
        QTRY_VERIFY(!second->isVisible());
        QCOMPARE(charge_image(original).empreinteSeance, version);
        auto relit = banc_essai(Lang::Fr);
        QVERIFY(charge(*relit, original));
        QCOMPARE(qRound(relit->session().mesures[0].longueur_px()), 80);
        QVERIFY(charge(*relit, copie));
        QCOMPARE(qRound(relit->session().mesures[0].longueur_px()), 120);
    }

    void remplacement_modifie_pendant_la_confirmation()
    {
        QTemporaryDir dir;
        const QString destination = dir.filePath(u"mesures.mevpix"_s);
        Seance autreSeance;
        autreSeance.definit_image(charge_image(mire).image);
        const auto premiere = enregistre_seance(destination, autreSeance);
        QVERIFY(premiere.reussie());

        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        const Textes &t = textes(Lang::Fr);
        action_barre_outils(*w, t.toolMeasure)->trigger();
        glisse_sur_image(*w, {150, 300}, {230, 300});
        chemin_sortie(*w, destination);
        QByteArray versionExterne;
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *boite) {
            // L'autre fenêtre a le temps d'enregistrer pendant que l'on réfléchit.
            autreSeance.mesures.append({{10, 50}, {130, 50}});
            versionExterne = enregistre_seance(destination, autreSeance, premiere.empreinte).empreinte;
            choix_message(boite, t.replaceButton);
        }, u"confirmeRemplacementFichier"_s);
        bool conflitSignale = false;
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *boite) {
            conflitSignale = true;
            choix_message(boite, t.cancel);
        }, u"conflitSeance"_s);
        action_barre_outils(*w, t.saveSessionAs)->trigger();

        QTRY_VERIFY(!w->occupe());
        QVERIFY(!versionExterne.isEmpty());
        QVERIFY(conflitSignale);
        QCOMPARE(charge_image(destination).empreinteSeance, versionExterne);
        QVERIFY(w->isWindowModified());
        QCOMPARE(qRound(w->session().mesures[0].longueur_px()), 80);
    }

    void deux_fenetres_partagent_les_reglages_data()
    {
        QTest::addColumn<bool>("ordreInverse");
        QTest::newRow("langue puis taille") << false;
        QTest::newRow("taille puis langue") << true;
    }

    void deux_fenetres_partagent_les_reglages()
    {
        QFETCH(bool, ordreInverse);
        QTemporaryDir dir;
        const FichiersReglages fichiers{dir.filePath(u"mevpix.ini"_s), {}};
        Reglages initial;
        initial.lang = Lang::Fr;
        initial.windowSize = QSize(1000, 640);
        QCOMPARE(initial.enregistre(fichiers.user), QString());
        FenetrePrincipale langue(initial, fichiers, {}), taille(initial, fichiers, {});
        for (auto *fenetre : {&langue, &taille}) {
            fenetre->resize(*initial.windowSize);
            fenetre->show();
        }
        langue.findChild<QAction *>(u"language"_s)->trigger();
        taille.resize(1050, 680);
        if (ordreInverse)
            taille.close();
        langue.close();
        if (!ordreInverse)
            taille.close();
        const auto resultat = Reglages::charge(fichiers);
        QCOMPARE(resultat.lang, std::optional(Lang::En));
        QCOMPARE(resultat.windowSize, std::optional(QSize(1050, 680)));
    }

    void reglages_reessayes_a_la_fermeture()
    {
        QTemporaryDir dir;
        const QString dossierConfig = dir.filePath(u"reglages"_s);
        QFile obstacle(dossierConfig);
        QVERIFY(obstacle.open(QIODevice::WriteOnly));
        obstacle.close();
        const FichiersReglages fichiers{dossierConfig + u"/mevpix.ini"_s, {}};
        Reglages initial;
        initial.lang = Lang::En;
        FenetrePrincipale w(initial, fichiers, {});
        w.show();
        w.findChild<QAction *>(u"language"_s)->trigger();
        QCOMPARE(w.langue(), Lang::Fr);
        const auto *statut = w.findChild<QLabel *>(u"statusMessage"_s);
        QTRY_VERIFY_WITH_TIMEOUT(statut->text().contains(u"réglages"_s), 4000);
        QVERIFY(obstacle.remove());
        QVERIFY(QDir().mkdir(dossierConfig));
        QVERIFY(w.close());
        QVERIFY(QFileInfo::exists(fichiers.user));
        QCOMPARE(Reglages::charge(fichiers).lang, std::optional(Lang::Fr));
    }

    void chemins_de_seance_et_protection_du_travail()
    {
        QTemporaryDir dir;
        const QString original = dir.filePath(u"Binome-A.mevpix"_s);
        Seance s;
        s.definit_image(charge_image(mire).image);
        QVERIFY(enregistre_seance(original, s).reussie());
        auto w = banc_essai(Lang::Fr);
        const Textes &t = textes(Lang::Fr);
        QVERIFY(charge(*w, original));
        QVERIFY(w->windowTitle().contains(u"Binome-A.mevpix"_s));
        action_barre_outils(*w, t.toolMeasure)->trigger();
        QVERIFY(!w->isWindowModified());
        glisse_sur_image(*w, {400, 300}, {800, 300});
        QVERIFY(w->isWindowModified());
        action_barre_outils(*w, t.saveSession)->trigger();
        QTRY_VERIFY(!w->occupe());
        QVERIFY(!w->isWindowModified());
        QCOMPARE(charge_image(original).seance["mesures"].toArray().size(), 1);
        chemin_sortie(*w, dir.filePath(u"Copie"_s));
        action_barre_outils(*w, t.saveSessionAs)->trigger();
        QTRY_VERIFY(!w->occupe());
        const QString copie = dir.filePath(u"Copie.mevpix"_s);
        const auto *statut = w->findChild<QLabel *>(u"statusMessage"_s);
        QVERIFY2(w->windowTitle().contains(u"Copie.mevpix"_s), qPrintable(w->windowTitle() + u" — "_s + statut->text()));
        QVERIFY(QFileInfo::exists(copie));
        glisse_sur_image(*w, {400, 450}, {800, 450});
        prochain_dialogue<QFileDialog>(*w, [](QFileDialog *d) { d->reject(); });
        action_barre_outils(*w, t.saveSessionAs)->trigger();
        QVERIFY(w->windowTitle().contains(u"Copie.mevpix"_s));
        // Échec d’un enregistrement direct : l’ancienne copie reste disponible ailleurs.
        const QString sauvegarde = dir.filePath(u"sauvegarde.mevpix"_s);
        QVERIFY(QFile::rename(copie, sauvegarde));
        QVERIFY(QDir().mkdir(copie));
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { choix_message(b, t.saveSession); }, u"confirmQuit"_s);
        QVERIFY(!w->close());
        QTRY_VERIFY(!w->occupe());
        QVERIFY(w->isVisible());
        QCOMPARE(w->session().mesures.size(), 2);
        QVERIFY(w->findChild<QLabel *>(u"statusMessage"_s)->text().contains(u"Copie.mevpix"_s));
        QCOMPARE(charge_image(sauvegarde).seance["mesures"].toArray().size(), 1);
        QVERIFY(QDir().rmdir(copie));
        QVERIFY(QFile::rename(sauvegarde, copie));
        // Réouverture du même fichier : la sauvegarde choisie doit précéder la relecture.
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { choix_message(b, t.saveSession); }, u"confirmReplace"_s);
        QVERIFY(charge(*w, copie));
        QCOMPARE(w->session().mesures.size(), 2);
        QCOMPARE(charge_image(copie).seance["mesures"].toArray().size(), 2);
        QCOMPARE(charge_image(original).seance["mesures"].toArray().size(), 1);
        action_barre_outils(*w, t.toolProfile)->trigger();
        w->canevas()->zoome_au_centre(2);
        QVERIFY(!w->isWindowModified());
        bool demande = false;
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { demande = true; choix_message(b, t.discard); });
        QVERIFY(w->close());
        QVERIFY(!demande);
    }

    void premiere_sauvegarde_a_la_fermeture()
    {
        auto w = banc_essai(Lang::Fr);
        const Textes &t = textes(Lang::Fr);
        QVERIFY(charge(*w, mire));
        action_barre_outils(*w, t.toolMeasure)->trigger();
        glisse_sur_image(*w, {400, 350}, {1200, 350});
        const auto sauver = [&](QMessageBox *boite) { choix_message(boite, t.saveSession); };
        prochain_dialogue<QMessageBox>(*w, sauver, u"confirmQuit"_s);
        prochain_dialogue<QFileDialog>(*w, [](QFileDialog *d) { d->reject(); });
        QVERIFY(!w->close());
        QCOMPARE(w->session().mesures.size(), 1);
        QVERIFY(w->isVisible());
        QTemporaryDir dir;
        const QString path = dir.filePath(u"Premiere.mevpix"_s);
        prochain_dialogue<QMessageBox>(*w, sauver, u"confirmQuit"_s);
        chemin_sortie(*w, path);
        w->close();
        QTRY_VERIFY(!w->isVisible());
        QVERIFY(w->windowTitle().contains(u"Premiere.mevpix"_s));
        const auto lecture = charge_image(path);
        QVERIFY(lecture.image);
        QCOMPARE(lecture.seance["mesures"].toArray().size(), 1);
    }

    void export_si_etalonnage_en_attente()
    {
        auto w = banc_essai(Lang::Fr);
        const Textes &t = textes(Lang::Fr);
        QVERIFY(charge(*w, mire));
        etalonne_et_mesure(*w, {});
        profil(*w, {});
        const auto modifieLongueur = [&](const QString &texte) {
            action_barre_outils(*w, t.toolCalibrate)->trigger();
            auto *champ = w->findChild<QLineEdit *>(u"calibLength"_s);
            champ->selectAll();
            QTest::keyClicks(champ, texte);
        };
        const auto bouton = [&](const QString &texte) -> QPushButton * {
            for (auto *b : w->findChildren<QPushButton *>())
                if (b->text() == texte)
                    return b;
            return nullptr;
        };
        modifieLongueur(u"40"_s);
        action_barre_outils(*w, t.toolMeasure)->trigger();
        QVERIFY(w->session().etalonnage_en_attente());
        QApplication::clipboard()->setText(u"témoin"_s);
        auto *copier = bouton(t.copyMeasures);
        QVERIFY(copier);
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { choix_message(b, t.cancel); }, u"confirmScale"_s);
        copier->click();
        QCOMPARE(QApplication::clipboard()->text(), u"témoin"_s);
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { choix_message(b, t.useValidatedScale); }, u"confirmScale"_s);
        copier->click();
        const QString mesures = QApplication::clipboard()->text();
        QVERIFY(mesures.contains(u"20,2"_s));
        QVERIFY(w->session().etalonnage_en_attente());

        QTemporaryDir dir;
        const QString csv = dir.filePath(u"coupe.csv"_s);
        action_barre_outils(*w, t.toolProfile)->trigger();
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { choix_message(b, t.useNewScale); }, u"confirmScale"_s);
        chemin_sortie(*w, csv);
        bouton(t.exportCsv)->click();
        QVERIFY(!w->session().etalonnage_en_attente());
        QCOMPARE(w->session().etalonnage->longueurReelle, 40.0);
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly));
        Seance donnees = w->session();
        QCOMPARE(QString::fromUtf8(f.readAll()), en_csv(*donnees.profil(), donnees.etalonnage, t.decimal));

        modifieLongueur(u"x"_s);
        bool verifie = false;
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) {
            for (auto *option : b->buttons())
                if (option->text() == t.useNewScale)
                    verifie = !option->isEnabled();
            choix_message(b, t.cancel);
        }, u"confirmScale"_s);
        action_barre_outils(*w, t.annotatedImage)->trigger();
        QVERIFY(verifie);
        QCOMPARE(w->session().etalonnage->longueurReelle, 40.0);
        prochain_dialogue<QMessageBox>(*w, [&](QMessageBox *b) { choix_message(b, t.discard); }, u"confirmReplace"_s);
        QVERIFY(charge(*w, mire));
        QVERIFY(w->session().saisieLongueur.isEmpty());
        QVERIFY(!w->session().etalonnage);
    }

    void annotation_coupe_graphique_png_et_copie_data()
    {
        QTest::addColumn<QString>("nom");
        QTest::addColumn<QColorSpace>("espace");
        QTest::newRow("photo sans profil") << u"photo.png"_s << QColorSpace();
        QTest::newRow("photo haute densité") << u"photo@2x.png"_s << QColorSpace(QColorSpace::SRgb);
        QTest::newRow("photo Display P3") << u"photo.png"_s << QColorSpace(QColorSpace::DisplayP3);
    }

    void annotation_coupe_graphique_png_et_copie()
    {
        QFETCH(QString, nom);
        QFETCH(QColorSpace, espace);
        QTemporaryDir source;
        const QString image = source.filePath(nom);
        QImage reference = QImage(mire).convertToFormat(QImage::Format_RGB32);
        reference.setColorSpace(espace);
        const QPoint temoin(50, 40); // loin des tracés et du graphique
        reference.setPixelColor(temoin, QColor(200, 100, 50));
        QVERIFY(reference.save(image));
        auto w = banc_essai(Lang::Fr);
        const Textes &t = textes(Lang::Fr);
        QVERIFY(charge(*w, image));
        action_barre_outils(*w, t.toolProfile)->trigger();
        glisse_sur_image(*w, {100, 350}, {1800, 350});
        const auto exporter = [&](bool graphe, bool png) {
            prochain_dialogue<QDialog>(*w, [&, graphe, png](QDialog *d) {
                auto *boutons = d->findChild<QDialogButtonBox *>();
                for (auto *caseACocher : d->findChildren<QCheckBox *>())
                    if (caseACocher->text() == t.includeGraph)
                        caseACocher->setChecked(graphe);
                if (png)
                    boutons->button(QDialogButtonBox::Save)->click();
                else
                    for (auto *b : boutons->buttons())
                        if (b->text() == t.copyAnnotation)
                            b->click();
            });
            action_barre_outils(*w, t.annotatedImage)->trigger();
        };
        exporter(false, false);
        const QImage photo = QApplication::clipboard()->image();
        QCOMPARE(photo.size(), w->session().image->rgb.size());
        // Repère noir dans la moitié inférieure droite, loin des tracés.
        QCOMPARE(photo.pixelColor(1400, 520), QColor(Qt::black));
        const auto milieu = w->session().coupe->point_en(0.5).toPoint();
        QCOMPARE(photo.pixelColor(milieu), COLOR_CUT);
        exporter(true, false);
        const QImage figure = QApplication::clipboard()->image();
        QVERIFY(figure.height() > photo.height());
        QCOMPARE(figure.width(), photo.width());
        QCOMPARE(figure.pixelColor(milieu), COLOR_CUT);
        QTemporaryDir dir;
        const QString png = dir.filePath(u"figure.png"_s);
        chemin_sortie(*w, dir.filePath(u"figure"_s));
        exporter(true, true);
        const QImage fichier(png);
        QVERIFY(!fichier.isNull());
        QCOMPARE(fichier.convertToFormat(figure.format()), figure);
        for (const QImage &rendu : {photo, figure, fichier}) {
            QCOMPARE(rendu.colorSpace(), espace);
            QCOMPARE(rendu.pixelColor(temoin), reference.pixelColor(temoin));
            if (espace.isValid())
                QCOMPARE(rendu.convertedToColorSpace(QColorSpace::SRgb).pixelColor(temoin),
                         reference.convertedToColorSpace(QColorSpace::SRgb).pixelColor(temoin));
        }
    }

    /// Fermer la fenêtre avec des mesures (rien n'est enregistré) : on demande ; « Annuler » garde la
    /// fenêtre ouverte, et c'est le bouton par défaut.
    void fermeture_avec_mesures_demande()
    {
        auto w = banc_essai(Lang::Fr);
        QVERIFY(charge(*w, mire));
        etalonne_et_mesure(*w, u"99_fr_fermeture"_s);
        QCOMPARE(w->session().mesures.size(), size_t(1));
        auto answer = [](const QString &button, bool *asked) {
            QTimer::singleShot(150, [button, asked] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box || box->objectName() != u"confirmQuit"_s)
                    return;
                *asked = true;
                QVERIFY(box->defaultButton() && box->defaultButton()->text() == u"Annuler"_s);
                for (QAbstractButton *b : box->buttons())
                    if (b->text() == button)
                        b->click();
            });
        };
        bool asked = false;
        answer(u"Annuler"_s, &asked);
        QVERIFY(!w->close());
        QVERIFY(asked);
        QVERIFY(w->isVisible());
        asked = false;
        answer(textes(Lang::Fr).discard, &asked);
        QVERIFY(w->close());
        QVERIFY(asked);
    }

    /// En haut à droite de la barre de menus, dans cet ordre : thème (soleil ou lune), langue
    /// (drapeau) ; pas de bouton de notice (menu Aide, F1) ; boutons plats, de la hauteur d'un titre
    /// de menu : la barre de menus garde sa hauteur standard, même agrandie.
    void boutons_coin_des_menus()
    {
        auto w = banc_essai(Lang::Fr);
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
            raccourci(*w, QKeySequence(QKeySequence::ZoomIn));
        QTest::qWait(30);
        QVERIFY2(standard(), qPrintable(QString::number(w->menuBar()->height())));
        QVERIFY(theme->iconSize().height() > normal.height());
        raccourci(*w, QKeySequence(Qt::CTRL | Qt::Key_0));
        QTest::qWait(30);
        QCOMPARE(theme->iconSize(), normal);
    }

    /// L'agrandissement enregistré revient au démarrage ; une fenêtre trop étroite le baisse, et il
    /// revient quand elle s'élargit de nouveau.
    void agrandissement_retrouve()
    {
        const double base = taille_points_interface();
        Reglages settings;
        settings.lang = Lang::Fr;
        settings.uiZoom = 1.5;
        auto w = std::make_unique<FenetrePrincipale>(settings, FichiersReglages{}, QString());
        w->resize(1280, 800);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::qWait(50);
        QVERIFY2(std::abs(taille_points_interface() - 1.5 * base) < 0.01, qPrintable(QString::number(taille_points_interface() / base)));
        // 1000 points de large : au plus 1000 / 800 = 125 %
        w->resize(1000, 800);
        QTest::qWait(50);
        QVERIFY2(std::abs(taille_points_interface() - 1.25 * base) < 0.01, qPrintable(QString::number(taille_points_interface() / base)));
        w->resize(1280, 800);
        QTest::qWait(50);
        QVERIFY2(std::abs(taille_points_interface() - 1.5 * base) < 0.01, qPrintable(QString::number(taille_points_interface() / base)));
        // Ctrl 0 : la police de l'application revient à sa taille, pour les essais suivants
        raccourci(*w, QKeySequence(Qt::CTRL | Qt::Key_0));
        QTest::qWait(30);
        QVERIFY(std::abs(taille_points_interface() - base) < 0.01);
    }

    /// Le bouton de thème passe du soleil (clair) à la lune (sombre), et inversement.
    void bouton_theme()
    {
        auto w = banc_essai(Lang::Fr, Qt::ColorScheme::Light);
        QVERIFY(!est_sombre(w->palette()));
        auto *button = w->findChild<QToolButton *>(u"theme"_s);
        QCOMPARE(button->toolTip(), u"Thème clair. Cliquer pour passer au thème sombre."_s);
        button->click();
        QTest::qWait(50);
        QVERIFY(est_sombre(w->palette()));
        QCOMPARE(button->toolTip(), u"Thème sombre. Cliquer pour passer au thème clair."_s);
        // thème sombre de la suite : fond de fenêtre presque noir
        QCOMPARE(w->palette().color(QPalette::Window), QColor(32, 33, 36));
        QCOMPARE(w->palette().color(QPalette::Base), QColor(22, 23, 25));
        capture(w.get(), u"21_fr_sombre_accueil"_s);
        QVERIFY(charge(*w, mire));
        etalonne_et_mesure(*w, u"21_fr_sombre"_s);
        profil(*w, u"21_fr_sombre"_s);
        // thème sombre : curseurs aux couleurs vives
        QCOMPARE(couleur_nom_curseur(*w, u"A"_s), COLOR_A);
        QCOMPARE(couleur_nom_curseur(*w, u"B"_s), COLOR_B);
        button->click();
        QTest::qWait(50);
        QCOMPARE(couleur_nom_curseur(*w, u"A"_s), QColor(215, 105, 0));
        button->click();
        QTest::qWait(50);
        QVERIFY(est_sombre(w->palette()));
        button->click();
        QTest::qWait(50);
        QVERIFY(!est_sombre(w->palette()));
    }

    /// Le drapeau passe du français à l'anglais, et inversement.
    void bouton_langue()
    {
        auto w = banc_essai(Lang::Fr);
        auto *button = w->findChild<QToolButton *>(u"language"_s);
        QCOMPARE(button->toolTip(), u"Langue : français. Cliquer pour passer en anglais (English)."_s);
        button->click();
        QCOMPARE(w->langue(), Lang::En);
        QCOMPARE(w->menuBar()->actions().first()->text(), u"File"_s);
        QVERIFY(action_barre_outils(*w, textes(Lang::En).toolCalibrate));
        QCOMPARE(button->toolTip(), u"Language: English. Click to switch to French (français)."_s);
        QCOMPARE(w->findChild<QPushButton *>(u"welcomeOpen"_s)->text(), u"Open an image…"_s);
        capture(w.get(), u"15_en_langue"_s);
        button->click();
        QCOMPARE(w->langue(), Lang::Fr);
    }

    /// Barre d'état : une ligne, de même hauteur d'un message à l'autre ; un échec s'y lit en rouge.
    void barre_etat_une_ligne()
    {
        auto w = banc_essai(Lang::Fr);
        const int line = w->statusBar()->height();
        QVERIFY2(line < 2 * w->fontMetrics().height() + 4, qPrintable(QString::number(line)));
        QVERIFY(charge(*w, mire));
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
        w = banc_essai(Lang::Fr);
        message = w->findChild<QLabel *>(u"statusMessage"_s);
        w->charge_chemin(u"/nulle/part/image.png"_s);
        for (int i = 0; i < 200 && w->occupe(); ++i)
            QTest::qWait(10);
        QTest::qWait(30);
        QVERIFY2(message->text().startsWith(u"Impossible d'ouvrir"_s), qPrintable(message->text()));
        QCOMPARE(message->palette().color(QPalette::WindowText), couleur_erreur(w->palette()));
        QVERIFY(message->palette().color(QPalette::WindowText) != normal);
        QVERIFY(accueil_affiche(*w));
        capture(w.get(), u"04_fr_erreur"_s);
    }

    /// Le panneau latéral n'est jamais plus étroit que son contenu : aucun texte rogné à droite, pour
    /// chaque outil, en anglais, et avec l'interface agrandie.
    void panneau_lateral_jamais_rogne()
    {
        for (const Lang lang : LANGUES) {
            auto w = banc_essai(lang);
            QVERIFY(charge(*w, mire));
            auto *side = w->findChild<QScrollArea *>(u"sidePanel"_s);
            const auto fits = [&] {
                QTest::qWait(30);
                return side->widget()->width() <= side->viewport()->width();
            };
            const auto widths = [&] {
                return u"%1 > %2"_s.arg(side->widget()->width()).arg(side->viewport()->width());
            };
            const Textes &t = textes(lang);
            etalonne_et_mesure(*w, QString());
            for (const QString &outil : {t.toolCalibrate, t.toolMeasure}) {
                action_barre_outils(*w, outil)->trigger();
                QVERIFY2(fits(), qPrintable(widths()));
            }
            profil(*w, QString());
            QCOMPARE(w->session().outil, Outil::Profil);
            QVERIFY2(fits(), qPrintable(widths()));
            // agrandissement de l'interface (Ctrl +) jusqu'à 160 % : le contenu dépasse la largeur initiale
            for (int i = 0; i < 6; ++i)
                raccourci(*w, QKeySequence(QKeySequence::ZoomIn));
            bool fitsZoomed = true;
            QString zoomedWidths;
            for (const QString &outil : {t.toolCalibrate, t.toolMeasure, t.toolProfile}) {
                action_barre_outils(*w, outil)->trigger();
                if (!fits()) {
                    fitsZoomed = false;
                    zoomedWidths = widths();
                }
            }
            QCOMPARE(w->session().outil, Outil::Profil);
            // barre d'état agrandie elle aussi, toujours d'une ligne
            const QLabel *message = w->findChild<QLabel *>(u"statusMessage"_s);
            const bool statusZoomed = std::abs(message->font().pointSizeF() - taille_points_interface()) < 0.01;
            // une ligne de texte, ses marges (4 + 4) et celles de la barre (6) : deux lignes en
            // ajouteraient une de plus
            const int lineHeight = message->fontMetrics().height();
            // (sauf si le message ne tient pas dans la largeur : il passe alors à la ligne, selon la
            // police du système)
            const bool messageFits = message->fontMetrics().horizontalAdvance(message->text()) <= message->contentsRect().width();
            const bool statusOneLine = w->statusBar()->height() < 2 * lineHeight + 14 || !messageFits;
            const QString statusHeights = u"%1 px pour une ligne de %2 px : « %3 »"_s.arg(w->statusBar()->height()).arg(lineHeight).arg(message->text());
            if (lang == Lang::En)
                capture(w.get(), u"14_en_agrandi"_s);
            // Ctrl 0 : la police de l'application revient à sa taille, pour les essais suivants
            raccourci(*w, QKeySequence(Qt::CTRL | Qt::Key_0));
            QVERIFY2(fitsZoomed, qPrintable(zoomedWidths));
            QVERIFY(statusZoomed);
            QVERIFY2(statusOneLine, qPrintable(statusHeights));
        }
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
        const QString log = dir.filePath(u"mevpix.log"_s);
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
        QVERIFY(lines[0].startsWith(u"MeVPix "_s + about::VERSION));
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
        const QString log = dir.filePath(u"mevpix.log"_s);
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
        QVERIFY(lines[0].startsWith(u"MeVPix "_s + about::VERSION));
        // journal écrit en mode texte : \r\n sous Windows
        QCOMPARE(lines[1].trimmed(), u"essai d'exception"_s);
    }
};

QTEST_MAIN(UiTests)
#include "uitests.moc"
