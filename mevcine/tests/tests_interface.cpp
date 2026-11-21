// Tests de bout en bout : on pilote l'interface comme le ferait un élève ou un professeur, dans les
// deux modes.
//
// Les rendus de chaque étape sont écrits dans le dossier de compilation (rendus-tests/), pour
// pouvoir les regarder.

#include "app/a_propos.h"
#include "app/accueil.h"
#include "app/app.h"
#include "app/demarrage.h"
#include "app/dessin.h"
#include "app/dialogues.h"
#include "app/onglet_coord.h"
#include "app/plein_ecran.h"
#include "app/tableau.h"
#include "app/theme.h"
#include "calculs.h"
#include "detect.h"
#include "ecriture_image.h"
#include "erreurs.h"
#include "i18n.h"
#include "outils.h"
#include "chemins.h"
#include "prefs.h"

#include <algorithm>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>
#include <QMenu>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOptionMenuItem>
#include <QToolBar>
#include <QStatusBar>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalSpy>
#include <QToolButton>
#include <QWheelEvent>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace {

/// plantage pour l'essai du journal : accès mémoire invalide, sans fichier core (Linux)
[[noreturn]] void plante()
{
#ifdef Q_OS_LINUX
    prctl(PR_SET_DUMPABLE, 0);
#endif
    volatile std::uintptr_t adresse = 0;
    *reinterpret_cast<volatile int *>(adresse) = 0;
    std::abort();
}

/// exception que rien ne rattrape, dans un fil de calcul (essai du journal), sans fichier core
[[noreturn]] void lance_dans_un_fil()
{
#ifdef Q_OS_LINUX
    prctl(PR_SET_DUMPABLE, 0);
#endif
    std::thread([] { throw std::runtime_error("essai d'exception"); }).join();
    std::abort();
}

QString lit_texte(const QString &chemin)
{
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

void ecrit_texte(const QString &chemin, const QString &texte)
{
    QFile f(chemin);
    if (f.open(QIODevice::WriteOnly))
        f.write(texte.toUtf8());
}

/// laisse passer les événements en attente (dialogues ouverts au tour suivant, destructions)
void attend()
{
    for (int k = 0; k < 4; ++k) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/// un événement de la souris, à une position précise (en virgule flottante)
void souris(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton bouton, Qt::MouseButtons boutons)
{
    QMouseEvent e(type, pos, w->mapToGlobal(pos), bouton, boutons, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

void clic(QWidget *w, QPointF pos, Qt::MouseButton bouton = Qt::LeftButton)
{
    souris(w, QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton);
    souris(w, QEvent::MouseButtonPress, pos, bouton, bouton);
    souris(w, QEvent::MouseButtonRelease, pos, bouton, Qt::NoButton);
    attend();
}

void glisse(QWidget *w, QPointF a, QPointF b)
{
    souris(w, QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
    souris(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 5; ++k)
        souris(w, QEvent::MouseMove, a + (b - a) * (k / 5.0), Qt::NoButton, Qt::LeftButton);
    souris(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
    attend();
}

// Bille et règle dans le même plan. Le film des captures est fabriqué dans le dossier de tests.
Vecteur centre_bille(int numero)
{
    return {45.0 + 11 * numero, 52.0 + 0.55 * (numero - 15) * (numero - 15)};
}

QString film_notice()
{
    QTemporaryDir images;
    if (!images.isValid())
        return {};
    for (int numero = 1; numero <= 28; ++numero) {
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(QColor(239, 238, 228));
        QPainter p(&image);
        p.setPen(QColor(218, 219, 209));
        for (int x = 5; x < 400; x += 24)
            p.drawLine(x, 0, x, 299);
        for (int y = 20; y < 300; y += 24)
            p.drawLine(0, y, 399, y);
        // 17 divisions de 5 cm, du trait x=45 au trait x=398.
        p.fillRect(QRect(43, 187, 357, 27), QColor(251, 250, 244));
        for (int k = 0; k <= 17; ++k) {
            const int x = 45 + qRound(353.0 * k / 17);
            p.fillRect(QRect(x, 188, 2, k % 2 == 0 || k == 17 ? 10 : 6), QColor(46, 61, 65));
        }
        p.setPen(QColor(46, 61, 65));
        p.drawLine(45, 188, 398, 188);
        QFont chiffres = p.font();
        chiffres.setPixelSize(12);
        p.setFont(chiffres);
        p.drawText(44, 211, u"0"_s);
        p.drawText(QRect(320, 197, 78, 17), Qt::AlignRight | Qt::AlignVCenter, u"85 cm"_s);
        const Vecteur centre = centre_bille(numero);
        const QPointF c(centre.x, centre.y);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(24, 61, 94), 1.5));
        p.setBrush(QColor(48, 123, 173));
        p.drawEllipse(c, 11.0, 11.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(239, 247, 249));
        p.drawEllipse(c, 2.0, 2.0);
        p.end();
        if (!image.save(images.filePath(u"%1.png"_s.arg(numero, 2, 10, u'0'))))
            return {};
    }
    return outils::video_test(u"bille-85cm.mp4"_s,
                             {u"-framerate"_s, u"25"_s, u"-start_number"_s, u"1"_s,
                              u"-i"_s, images.filePath(u"%02d.png"_s), u"-c:v"_s, u"mpeg4"_s,
                              u"-q:v"_s, u"2"_s, u"-pix_fmt"_s, u"yuv420p"_s}).value_or(QString());
}

/// tous les widgets visibles de la fenêtre et de ses dialogues
QList<QWidget *> widgets_visibles(App &app)
{
    QList<QWidget *> res;
    for (QWidget *w : app.findChildren<QWidget *>()) {
        if (w->isVisible())
            res << w;
    }
    return res;
}

/// les textes affichés : intitulés, valeurs, bulles d'aide, menus
QStringList textes_affiches(App &app)
{
    QStringList t{app.windowTitle()};
    for (QWidget *w : widgets_visibles(app)) {
        t << w->toolTip() << w->accessibleName() << w->windowTitle();
        if (auto *l = qobject_cast<QLabel *>(w))
            t << l->text();
        else if (auto *b = qobject_cast<QAbstractButton *>(w))
            t << b->text();
        else if (auto *e = qobject_cast<QLineEdit *>(w))
            t << e->text() << e->placeholderText();
        else if (auto *c = qobject_cast<QComboBox *>(w)) {
            for (int i = 0; i < c->count(); ++i)
                t << c->itemText(i);
        } else if (auto *g = qobject_cast<QGroupBox *>(w))
            t << g->title();
        else if (auto *p = qobject_cast<QPlainTextEdit *>(w))
            t << p->toPlainText().split(u'\n');
        else if (auto *tb = qobject_cast<QTabBar *>(w)) {
            for (int i = 0; i < tb->count(); ++i)
                t << tb->tabText(i);
        } else if (auto *tw = qobject_cast<QTableWidget *>(w)) {
            for (int j = 0; j < tw->columnCount(); ++j) {
                if (auto *h = tw->horizontalHeaderItem(j))
                    t << h->text();
            }
        }
        if (auto *m = qobject_cast<QMessageBox *>(w))
            t << m->text() << m->informativeText();
    }
    for (QAction *a : app.menuBar()->actions()) {
        t << a->text() << a->toolTip();
        if (a->menu()) {
            for (QAction *b : a->menu()->actions())
                t << b->text() << b->toolTip();
        }
    }
    t.removeAll(QString());
    return t;
}

/// le bouton visible et actif qui porte cet intitulé
QAbstractButton *bouton(App &app, const QString &intitule)
{
    for (QWidget *w : widgets_visibles(app)) {
        auto *b = qobject_cast<QAbstractButton *>(w);
        if (b && b->isEnabled() && (b->text() == intitule || b->accessibleName() == intitule))
            return b;
    }
    return nullptr;
}

/// un texte affiché (étiquette, bouton, message)
bool affiche(App &app, const QString &texte)
{
    return textes_affiches(app).contains(texte);
}

} // namespace

class TestsInterface : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_conf;
    std::unique_ptr<QTemporaryDir> m_temp;

    /// Une configuration jetable, propre au test, pour ne pas toucher à celle de l'utilisateur.
    /// L'interface est en français, sauf demande contraire.
    /// une configuration neuve, avec le mode déjà choisi (simplifié), sauf pour un premier démarrage
    void configuration(std::optional<Langue> langue, bool premier_demarrage = false)
    {
        m_conf = std::make_unique<QTemporaryDir>();
        qputenv("MEVCINE_CONF", m_conf->path().toLocal8Bit());
        ecrit_texte(prefs::fichier_conf(),
                    u"[DEFAULT]\nversion = mevcine 1.0\nlangue = %1\n%2"_s.arg(i18n::code(langue.value_or(Langue::Francais)),
                                                                                premier_demarrage ? QString() : u"mode = simplifie\n"_s));
    }

    QString temporaire(const QString &nom)
    {
        if (!m_temp)
            m_temp = std::make_unique<QTemporaryDir>();
        return m_temp->filePath(nom);
    }

    /// l'application, sans son écran de démarrage
    std::unique_ptr<App> fenetre(const QString &fichier, std::optional<Mode> mode, std::optional<Langue> langue = {},
                                 bool garde_la_configuration = false)
    {
        if (!garde_la_configuration)
            configuration(langue);
        auto app = std::make_unique<App>(fichier, mode);
        app->resize(1280, 800);
        app->show();
        if (!QTest::qWaitForWindowExposed(app.get()))
            qWarning("fenêtre non exposée");
        if (app->ecran_demarrage())
            app->ecran_demarrage()->termine();
        attend();
        return app;
    }

    /// enregistre l'image de la fenêtre ; un dialogue ouvert y est posé au centre, comme à l'écran
    void verifie_icones(QWidget &fenetre)
    {
        for (auto *b : fenetre.findChildren<QAbstractButton *>()) {
            if (!b->isVisible() || qobject_cast<BoutonAxes *>(b))
                continue; // le bouton d’orientation dessine le repère entier
            const auto *outil = qobject_cast<QToolButton *>(b);
            if (outil && outil->arrowType() != Qt::NoArrow)
                continue; // flèche d’ouverture ou de défilement dessinée par Qt
            const QString nom = b->text() + u" "_s + b->accessibleName() + u" "_s + b->objectName();
            QVERIFY2(!b->icon().isNull(), qPrintable(nom));
            QVERIFY2(!outil || outil->toolButtonStyle() != Qt::ToolButtonTextOnly, qPrintable(nom));
        }
    }

    void rendu(App &app, const QString &nom)
    {
        attend();
        const QDir dossier(QString::fromUtf8(MEVCINE_RENDUS_TESTS));
        QDir().mkpath(dossier.path());
        verifie_icones(app);
        QPixmap image = app.grab();
        if (QDialog *d = app.dialogue_affiche()) {
            const QPixmap dialogue = d->grab();
            QPainter p(&image);
            p.fillRect(image.rect(), QColor(0, 0, 0, 60));
            const QRect r(QPoint((image.width() - dialogue.width()) / 2, (image.height() - dialogue.height()) / 3),
                          dialogue.size());
            p.fillRect(r.adjusted(3, 3, 6, 6), QColor(0, 0, 0, 70));
            p.drawPixmap(r.topLeft(), dialogue);
            p.setPen(app.palette().color(QPalette::Mid));
            p.drawRect(r.adjusted(0, 0, -1, -1));
        }
        image.save(dossier.filePath(nom + u".png"_s));
    }

    /// enregistre l'image d'une fenêtre à elle (le plein écran)
    void rendu_fenetre(QWidget *w, const QString &nom, const QRect &cadre = QRect(0, 0, -1, -1))
    {
        attend();
        const QDir dossier(QString::fromUtf8(MEVCINE_RENDUS_TESTS));
        QDir().mkpath(dossier.path());
        verifie_icones(*w);
        w->grab(cadre).save(dossier.filePath(nom + u".png"_s));
    }

    /// enregistre le haut de la fenêtre avec un menu déroulé sous son titre, comme à l'écran ; `actif`
    /// : l'entrée mise en évidence, comme sous la souris
    void rendu_menu(App &app, const QString &titre, const QString &nom, const QString &actif = {})
    {
        attend();
        QAction *entree = nullptr;
        for (QAction *a : app.menuBar()->actions()) {
            if (a->menu() && a->text() == titre)
                entree = a;
        }
        QVERIFY2(entree, qUtf8Printable(titre));
        QMenu *menu = entree->menu();
        app.menuBar()->setActiveAction(entree);
        attend();
        for (QAction *a : menu->actions()) {
            if (!actif.isEmpty() && a->text() == actif)
                menu->setActiveAction(a);
        }
        attend();
        QPixmap image = app.grab();
        const QPixmap deroule = menu->grab();
        const QPoint pos = app.menuBar()->mapTo(&app, app.menuBar()->actionGeometry(entree).bottomLeft() + QPoint(0, 1));
        const QRect r(pos, deroule.size());
        {
            QPainter p(&image);
            p.fillRect(r.adjusted(3, 3, 4, 4), QColor(0, 0, 0, 50));
            p.drawPixmap(pos, deroule);
        }
        menu->hide();
        app.menuBar()->setActiveAction(nullptr);
        attend();
        // le haut à gauche de la fenêtre : la barre des menus et le menu déroulé, avec un peu de marge
        const QRect garde(0, 0, std::min(image.width(), r.right() + 1 + 80), std::min(image.height(), r.bottom() + 1 + 30));
        const QDir dossier(QString::fromUtf8(MEVCINE_RENDUS_TESTS));
        QDir().mkpath(dossier.path());
        image.copy(garde).save(dossier.filePath(nom + u".png"_s));
    }

    /// la zone de l'image de la vidéo affichée
    ZoneVideo *zone(App &app)
    {
        return app.simplifie() ? app.m_guide->zone() : app.m_onglet_pointage->zone();
    }

    /// un clic sur un point de l'image de la vidéo
    void clic_image(App &app, Vecteur p)
    {
        ZoneVideo *z = zone(app);
        clic(z, z->vue().ecran(p));
    }

    void glisse_image(App &app, Vecteur a, Vecteur b)
    {
        ZoneVideo *z = zone(app);
        glisse(z, z->vue().ecran(a), z->vue().ecran(b));
    }

    bool clique(App &app, const QString &intitule)
    {
        QAbstractButton *b = bouton(app, intitule);
        if (!b) {
            qWarning("bouton « %s » introuvable", qUtf8Printable(intitule));
            return false;
        }
        b->click();
        attend();
        return true;
    }

    /// choisit un onglet du mode Expert
    void onglet(App &app, const QString &nom)
    {
        QTabBar *barre = app.m_expert->tabBar();
        for (int i = 0; i < barre->count(); ++i) {
            if (barre->tabText(i) == nom) {
                QTest::mouseClick(barre, Qt::LeftButton, {}, barre->tabRect(i).center());
                attend();
                return;
            }
        }
        qWarning("onglet « %s » introuvable", qUtf8Printable(nom));
    }

    QAction *action_menu(App &app, const QString &intitule)
    {
        for (QAction *a : app.menuBar()->actions()) {
            if (!a->menu())
                continue;
            for (QAction *b : a->menu()->actions()) {
                if (b->text() == intitule)
                    return b;
            }
        }
        return nullptr;
    }

    /// déclenche l'entrée d'un menu
    bool menu(App &app, const QString &intitule)
    {
        auto *action = action_menu(app, intitule);
        if (!action || !action->isEnabled())
            return false;
        action->trigger();
        attend();
        return true;
    }

    /// tape un texte dans un champ de saisie
    void tape(QLineEdit *champ, const QString &texte)
    {
        champ->setFocus();
        QTest::keyClicks(champ, texte);
        attend();
    }

    /// efface un champ de saisie au clavier, comme un élève
    void efface(QLineEdit *champ)
    {
        champ->setFocus();
        champ->selectAll();
        QTest::keyClick(champ, Qt::Key_Backspace);
        attend();
    }

    /// le tableau des mesures affiché
    TableauMesures *tableau(App &app)
    {
        if (app.simplifie())
            return app.m_guide->tableau();
        return app.onglet == Onglet::Pointage ? app.m_onglet_pointage->tableau() : app.m_onglet_coord->tableau();
    }

    /// Recherche une croix ou son cercle de survol dans le rendu autour du point vidéo.
    bool couleur_au_point(ZoneVideo *zone, Vecteur point, QColor couleur)
    {
        const QPoint centre = zone->vue().ecran(point).toPoint();
        const QImage image = zone->grab(QRect(centre - QPoint(11, 11), QSize(23, 23))).toImage();
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == couleur)
                    return true;
            }
        }
        return false;
    }

    /// le bouton ⌖ d'une ligne du tableau des mesures
    QAbstractButton *bouton_reprise(App &app, int ligne)
    {
        QTableWidget *t = tableau(app)->table();
        return qobject_cast<QAbstractButton *>(t->cellWidget(ligne, t->columnCount() - 2));
    }

    /// aucun intitulé de l'interface ne parle d'énergie, de masse ou de g
    void sans_energie(App &app)
    {
        for (const QString &t : textes_affiches(app)) {
            for (const QString &mot : {u"nergie"_s, u"masse"_s, u"Masse"_s, u"pesanteur"_s, u"Cinétique"_s, u"Ec1"_s, u"Epp"_s})
                QVERIFY2(!t.contains(mot), qUtf8Printable(u"intitulé contenant « %1 » : %2"_s.arg(mot, t)));
        }
    }

    /// Une page sans vidéo, épurée : le logo, le nom, le sous-titre, deux boutons de même taille côte à
    /// côte, une ligne discrète ; rien ne dépasse de la page, rien ne se chevauche, aucun texte n'est
    /// coupé
    static void verifie_page_epuree(QWidget *page, const QString &nom, int nb_boutons = 2)
    {
        QVERIFY2(page->isVisible(), qUtf8Printable(nom));
        QList<QWidget *> elements;
        QList<QAbstractButton *> boutons;
        for (QWidget *w : page->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
            if (!w->isVisible())
                continue;
            elements << w;
            if (auto *b = qobject_cast<QAbstractButton *>(w))
                boutons << b;
        }
        QVERIFY2(elements.size() == 4 + nb_boutons, qUtf8Printable(u"%1 : %2 éléments"_s.arg(nom).arg(elements.size())));
        QVERIFY2(boutons.size() == nb_boutons, qUtf8Printable(nom));
        if (nb_boutons == 2) {
            QCOMPARE(boutons[0]->size(), boutons[1]->size());
            QCOMPARE(boutons[0]->y(), boutons[1]->y());
        }
        // le logo (96 points), le nom (2,2 fois le texte), le sous-titre (1,15 fois)
        const auto taille = [](QWidget *w) { return QFontInfo(w->font()).pointSizeF() / QFontInfo(QApplication::font()).pointSizeF(); };
        QList<QLabel *> etiquettes;
        for (QWidget *w : elements) {
            if (auto *l = qobject_cast<QLabel *>(w))
                etiquettes << l;
        }
        QCOMPARE(etiquettes.size(), 4);
        QCOMPARE(etiquettes[0]->pixmap().size(), QSize(96, 96));
        QCOMPARE(etiquettes[1]->text(), u"MeVCine"_s);
        QVERIFY2(std::abs(taille(etiquettes[1]) - 2.2) < 0.15, qUtf8Printable(QString::number(taille(etiquettes[1]))));
        QVERIFY2(std::abs(taille(etiquettes[2]) - 1.15) < 0.1, qUtf8Printable(QString::number(taille(etiquettes[2]))));
        QCOMPARE(etiquettes[2]->palette().color(QPalette::WindowText), QApplication::palette().color(QPalette::PlaceholderText));
        QCOMPARE(etiquettes[3]->palette().color(QPalette::WindowText), QApplication::palette().color(QPalette::PlaceholderText));
        for (qsizetype i = 0; i < elements.size(); ++i) {
            const QRect r = elements[i]->geometry();
            const QString quoi = u"%1 : %2"_s.arg(nom, elements[i]->metaObject()->className());
            QVERIFY2(page->rect().contains(r), qUtf8Printable(quoi + u" dépasse de la page"_s));
            if (auto *l = qobject_cast<QLabel *>(elements[i]); l && l->wordWrap())
                QVERIFY2(r.height() >= l->heightForWidth(r.width()), qUtf8Printable(quoi + u" : texte coupé"_s));
            QVERIFY2(r.width() >= elements[i]->minimumSizeHint().width(), qUtf8Printable(quoi + u" : trop étroit"_s));
            for (qsizetype j = i + 1; j < elements.size(); ++j)
                QVERIFY2(!r.intersects(elements[j]->geometry()), qUtf8Printable(quoi + u" chevauche "_s + elements[j]->metaObject()->className()));
        }
    }

    /// Le panneau latéral affiché n'est pas rogné : il est au moins aussi large que son contenu, sans
    /// barre de défilement horizontale ; à sa première apparition, il a au moins 300 points de large
    static void verifie_panneau(App &a, const QString &nom, bool depart = false)
    {
        attend();
        QWidget *panneau = nullptr;
        for (QWidget *w : a.findChildren<QWidget *>(u"panneau_lateral"_s)) {
            if (w->isVisible())
                panneau = w;
        }
        QVERIFY2(panneau, qUtf8Printable(nom));
        const QString largeurs = u"%1 : %2 pour %3 au moins"_s.arg(nom).arg(panneau->width()).arg(panneau->minimumSizeHint().width());
        if (depart)
            QVERIFY2(panneau->width() >= 300, qUtf8Printable(largeurs));
        QVERIFY2(panneau->width() >= panneau->minimumSizeHint().width(), qUtf8Printable(largeurs));
        const auto defilements = panneau->findChildren<QScrollArea *>(u"defilement_lateral"_s);
        QVERIFY(!defilements.isEmpty());
        for (QScrollArea *d : defilements) {
            QVERIFY2(!d->horizontalScrollBar()->isVisible(), qUtf8Printable(nom));
            QVERIFY2(d->widget()->width() >= d->widget()->minimumSizeHint().width(),
                     qUtf8Printable(u"%1 : contenu de %2 pour %3"_s.arg(nom).arg(d->widget()->width()).arg(d->widget()->minimumSizeHint().width())));
        }
        // aucun texte ni bouton ne dépasse à droite
        for (QWidget *w : panneau->findChildren<QWidget *>()) {
            if (!w->isVisible() || !(qobject_cast<QLabel *>(w) || qobject_cast<QAbstractButton *>(w)))
                continue;
            const int droite = w->mapTo(panneau, QPoint(w->width(), 0)).x();
            QVERIFY2(droite <= panneau->width(), qUtf8Printable(u"%1 : %2 « %3 » dépasse"_s.arg(nom, w->metaObject()->className(),
                                                                                              qobject_cast<QLabel *>(w) ? qobject_cast<QLabel *>(w)->text() : QString())));
        }
    }

    static void verifie_python(const QString &texte, const QString &controle)
    {
        const QString erreur = outils::execute_python(texte, controle);
        QVERIFY2(erreur.isEmpty(), qUtf8Printable(erreur + u"\n"_s + texte));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(!outils::mire(16).isEmpty());
        QVERIFY(!outils::mire(28).isEmpty());
        // curseur de texte qui ne clignote pas : les rendus sont les mêmes d'une
        // fois à l'autre, au lieu de montrer le curseur ou non selon l'instant
        QApplication::setCursorFlashTime(0);
    }

    void init()
    {
        i18n::choisit(Langue::Francais);
    }

    void export_image_ecrit_par_fragments_et_preserve_original()
    {
        class Destination : public QIODevice {
        public:
            QByteArray donnees;
            qint64 limite = 0;
            qint64 readData(char *, qint64) override { return -1; }
            qint64 writeData(const char *s, qint64 n) override
            {
                const qint64 taille = std::min({n, limite - donnees.size(), qint64(7)});
                if (taille <= 0) {
                    setErrorString(u"disque plein"_s);
                    return -1;
                }
                donnees.append(s, taille);
                return taille;
            }
        } sortie;
        sortie.open(QIODevice::WriteOnly | QIODevice::Unbuffered);
        QImage image(200, 100, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        QString erreur;
        sortie.limite = 128;
        QVERIFY(!ecriture_image::ecrit(sortie, image, "PNG", &erreur));
        QCOMPARE(erreur, u"disque plein"_s);
        QCOMPARE(sortie.donnees.size(), 128);
        sortie.donnees.clear();
        sortie.limite = 100000;
        QVERIFY(ecriture_image::ecrit(sortie, image, "PNG", &erreur));
        QCOMPARE(QImage::fromData(sortie.donnees), image);

        QTemporaryDir dir;
        const QString nom = dir.filePath(u"export.jpg"_s);
        QVERIFY(ecriture_image::enregistre(nom, image, "JPEG", erreur));
        QFile fichier(nom);
        QVERIFY(fichier.open(QIODevice::ReadOnly));
        const auto original = fichier.readAll();
        fichier.close();
        QCOMPARE(QImage::fromData(original, "JPEG").size(), image.size());
        QVERIFY(!ecriture_image::enregistre(nom, QImage(), "JPEG", erreur));
        QVERIFY(fichier.open(QIODevice::ReadOnly));
        QCOMPARE(fichier.readAll(), original);
    }

    void accueil_ouvrir_une_video()
    {
        // premier lancement : mode simplifié (il se change dans le menu Mode), et l'accueil propose
        // d'ouvrir une vidéo, sans choix du mode
        auto app = fenetre({}, std::nullopt);
        QCOMPARE(app->mode, Mode::Simplifie);
        QCOMPARE(app->windowTitle(), u"MeVCine — Étude cinématique de vidéos"_s);
        QVERIFY(!bouton(*app, u"Mode simplifié"_s));
        QVERIFY(!bouton(*app, u"Mode expert"_s));
        // la notice est dans le menu Aide (F1), sans bouton à elle ; l'accueil n'a pas d'autre bouton
        QVERIFY(!bouton(*app, u"Notice"_s));
        QVERIFY(!bouton(*app, u"📖 Notice complète…"_s));
        QVERIFY(QFileInfo(QDir(chemins::dossier_notice()).filePath(u"fr/index.html"_s)).isFile());
        // La copie de la notice appartient au compte et reste disponible entre deux consultations.
        const QString copie = chemins::copie_aide();
        QVERIFY(copie.startsWith(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
        QCOMPARE(chemins::copie_aide(), copie);
        QVERIFY(QFileInfo(QDir(copie).filePath(u"fr/index.html"_s)).isFile());
        QVERIFY(QFileInfo(QDir(copie).filePath(u"en/index.html"_s)).isFile());
        QVERIFY(QFileInfo(QDir(copie).filePath(u"help.css"_s)).isFile());
        // la version de la notice est celle du programme
        {
            QFile page(QDir(copie).filePath(u"fr/index.html"_s));
            QVERIFY(page.open(QIODevice::ReadOnly));
            const QByteArray texte = page.readAll();
            QVERIFY(texte.contains("version " MEVCINE_VERSION));
            QVERIFY(!texte.contains("{{version}}"));
        }
        rendu(*app, u"00_accueil"_s);
        // pas de vidéo rouverte d'office : l'écran d'ouverture, avec un seul bouton (pas de vidéos
        // d'exemple : chacun ouvre les siennes)
        auto *ouvrir = app->m_sans_video->findChild<QPushButton *>(u"ouvrir"_s);
        QVERIFY(ouvrir && ouvrir->isVisible());
        QCOMPARE(ouvrir->text(), u"Ouvrir une vidéo…"_s);
        QCOMPARE(std::ranges::count_if(app->m_sans_video->findChildren<QPushButton *>(), [](QPushButton *b) { return b->isVisible(); }), 1);
        for (QAction *x : app->menuBar()->actions().front()->menu()->actions())
            QVERIFY2(!x->text().contains(u"exemple"_s, Qt::CaseInsensitive), qPrintable(x->text()));
        QCOMPARE(app->page_d_aide(), u"demarrer.html"_s);
        rendu(*app, u"01_sans_video"_s);
        // une vidéo, glissée sur la fenêtre comme un élève le ferait
        QVERIFY(app->ouvre_argument(outils::mire(28)));
        app->rafraichit();
        attend();
        QVERIFY(app->video);
        QCOMPARE(app->windowTitle(), u"MeVCine — mire-28.avi"_s);
        // le mode choisi dans le menu est retenu pour la prochaine fois
        app->change_mode(Mode::Expert);
        app->sauve_prefs();
        app.reset();
        auto suivante = fenetre({}, std::nullopt, std::nullopt, true);
        QCOMPARE(suivante->mode, Mode::Expert);
    }

    /// La page sans vidéo (ouvrir une vidéo) est épurée et tient sans chevauchement dans la fenêtre à
    /// sa taille minimale, dans chaque langue et dans chaque thème
    void pages_d_accueil_a_la_taille_minimale()
    {
        for (Langue langue : TOUTES_LES_LANGUES) {
            const QString code = i18n::code(langue);
            auto app = fenetre({}, std::nullopt, langue);
            App &a = *app;
            a.resize(a.minimumSize());
            attend();
            QCOMPARE(a.size(), a.minimumSize());
            for (const bool sombre : {false, true}) {
                theme::choisit(sombre ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
                attend();
                QCOMPARE(dessin::sombre(QApplication::palette()), sombre);
                const QString nom = u"%1_%2"_s.arg(code, sombre ? u"sombre"_s : u"clair"_s);
                a.rafraichit();
                attend();
                verifie_page_epuree(a.m_sans_video, nom + u" sans vidéo"_s, 1);
                rendu(a, u"04_%1_sans_video_minimale"_s.arg(nom));
            }
        }
        theme::choisit(Qt::ColorScheme::Light);
        attend();
        i18n::choisit(Langue::Francais);
    }

    /// Au premier démarrage (aucun mode retenu ni imposé), la page d'accueil fait choisir le mode :
    /// deux boutons de même taille à la place de celui d'ouverture, dans une page qui reste épurée et
    /// tient dans la fenêtre à sa taille minimale ; le choix est retenu pour la prochaine fois
    void choix_du_mode_au_premier_demarrage()
    {
        for (Langue langue : TOUTES_LES_LANGUES) {
            const QString code = i18n::code(langue);
            configuration(langue, true);
            auto app = fenetre({}, std::nullopt, langue, true);
            App &a = *app;
            QVERIFY(a.mode_a_choisir);
            a.rafraichit();
            attend();
            rendu(a, u"00_%1_premier_demarrage"_s.arg(code));
            a.resize(a.minimumSize());
            attend();
            for (const bool sombre : {false, true}) {
                theme::choisit(sombre ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
                attend();
                const QString nom = u"%1_%2"_s.arg(code, sombre ? u"sombre"_s : u"clair"_s);
                a.rafraichit();
                attend();
                verifie_page_epuree(a.m_sans_video, nom + u" choix du mode"_s, 2);
                rendu(a, u"04_%1_choix_du_mode_minimale"_s.arg(nom));
            }
            theme::choisit(Qt::ColorScheme::Light);
            attend();
            auto *ouvrir = a.m_sans_video->findChild<QPushButton *>(u"ouvrir"_s);
            auto *expert = a.m_sans_video->findChild<QPushButton *>(u"mode_expert"_s);
            QVERIFY(ouvrir && !ouvrir->isVisible());
            QVERIFY(expert && expert->isVisible());
            expert->click();
            attend();
            QCOMPARE(a.mode, Mode::Expert);
            QVERIFY(!a.mode_a_choisir);
            QVERIFY(ouvrir->isVisible() && !expert->isVisible());
            verifie_page_epuree(a.m_sans_video, code + u" après le choix du mode"_s, 1);
            a.sauve_prefs();
            app.reset();
            auto suivante = fenetre({}, std::nullopt, langue, true);
            QCOMPARE(suivante->mode, Mode::Expert);
            QVERIFY(!suivante->mode_a_choisir);
        }
        // le mode imposé (--simple, --expert) : rien à choisir
        configuration(Langue::Francais, true);
        auto impose = fenetre({}, Mode::Simplifie, Langue::Francais, true);
        QVERIFY(!impose->mode_a_choisir);
        QVERIFY(impose->m_sans_video->findChild<QPushButton *>(u"ouvrir"_s)->isVisible());
        impose.reset();
        i18n::choisit(Langue::Francais);
    }

    /// La fenêtre s'ouvre à sa taille normale, jamais agrandie d'office (sauf sur un écran plus petit
    /// que sa taille minimale), et le titre nomme le logiciel
    void fenetre_a_sa_taille_normale()
    {
        configuration(std::nullopt);
        App a(QString(), std::nullopt);
        a.montre();
        QVERIFY(QTest::qWaitForWindowExposed(&a));
        if (a.ecran_demarrage())
            a.ecran_demarrage()->termine();
        attend();
        const QRect dispo = a.screen()->availableGeometry();
        const bool petit_ecran = dispo.width() - 16 < TAILLE_MIN_FENETRE.width() || dispo.height() - 48 < TAILLE_MIN_FENETRE.height();
        QCOMPARE(a.isMaximized(), petit_ecran);
        if (!petit_ecran) {
            QVERIFY(a.width() <= TAILLE_FENETRE.width() && a.height() <= TAILLE_FENETRE.height());
            QVERIFY(a.width() >= TAILLE_MIN_FENETRE.width() && a.height() >= TAILLE_MIN_FENETRE.height());
        }
        QCOMPARE(a.windowTitle(), u"MeVCine — Étude cinématique de vidéos"_s);
        a.close();
    }

    /// La barre d'état : une ligne de haut, plusieurs seulement si le message ne tient pas ; à droite,
    /// la vidéo, l'image et le mode, séparés par un filet
    void barre_d_etat_d_une_ligne()
    {
        auto app = fenetre(outils::mire(28), Mode::Expert);
        App &a = *app;
        QStatusBar *barre = a.statusBar();
        const int ligne = a.m_statut->fontMetrics().height();
        // une ligne de texte, ses marges (4 + 4) et celles de la barre (6) : deux lignes en ajouteraient
        // une de plus
        QVERIFY2(barre->height() < 2 * ligne + 14, qUtf8Printable(u"%1 pour une ligne de %2"_s.arg(barre->height()).arg(ligne)));
        for (QLabel *l : {a.m_info_video, a.m_info_image, a.m_info_mode})
            QVERIFY(l->isVisible());
        QVERIFY(a.m_info_video->text().startsWith(u"mire-28.avi · 400 × 300 px"_s));
        QCOMPARE(a.m_info_image->text(), u"Image 1 sur 28"_s);
        QCOMPARE(a.m_info_mode->text(), u"Mode expert"_s);
        // le filet à gauche de chaque information, de la couleur Mid, se voit sur l'image de la barre
        const QImage image = barre->grab().toImage();
        const QColor mid = QApplication::palette().color(QPalette::Mid);
        for (QLabel *l : {a.m_info_video, a.m_info_image, a.m_info_mode}) {
            const QPoint p = l->mapTo(barre, QPoint(0, l->height() / 2)) * image.devicePixelRatio();
            QCOMPARE(image.pixelColor(p).rgb(), mid.rgb());
            QVERIFY(image.pixelColor(p + QPoint(3, 0)).rgb() != mid.rgb());
        }
        // un long message dans une petite fenêtre : plusieurs lignes, et rien n'est coupé
        a.resize(a.minimumSize());
        a.affiche_statut(QStringList(8, u"Un message très long, qui ne tient pas sur une seule ligne."_s).join(u' '),
                         icones::Statut::Erreur);
        a.rafraichit();
        attend();
        QVERIFY2(barre->height() >= 2 * ligne + 14, qUtf8Printable(QString::number(barre->height())));
        QVERIFY(a.m_statut->height() >= a.m_statut->heightForWidth(a.m_statut->width()));
        QCOMPARE(a.m_statut->palette().color(QPalette::WindowText), dessin::couleur_erreur(QApplication::palette()));
        rendu(a, u"64_barre_d_etat_longue"_s);
        // un message court : de nouveau une ligne
        a.affiche_statut(u"Prêt."_s, icones::Statut::Reussite);
        a.rafraichit();
        attend();
        QVERIFY2(barre->height() < 2 * ligne + 14, qUtf8Printable(QString::number(barre->height())));
        QCOMPARE(a.m_statut->palette().color(QPalette::WindowText), dessin::couleur_reussite(QApplication::palette()));
    }

    /// Le panneau latéral n'est jamais rogné : ni à la taille minimale de la fenêtre, ni avec
    /// l'interface agrandie ; il part d'au moins 300 points de large
    void panneau_lateral_jamais_rogne()
    {
        auto app = fenetre(outils::mire(28), Mode::Simplifie);
        App &a = *app;
        verifie_panneau(a, u"guide"_s, true);
        a.change_mode(Mode::Expert);
        a.rafraichit();
        attend();
        verifie_panneau(a, u"pointage"_s, true);
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        for (int i = 0; i < 3; ++i)
            clic_image(a, {100.0 + 10.0 * i, 150.0});
        onglet(a, u"Trajectoires"_s);
        verifie_panneau(a, u"trajectoires"_s, true);
        // à la taille minimale de la fenêtre
        a.resize(a.minimumSize());
        attend();
        verifie_panneau(a, u"trajectoires, fenêtre minimale"_s);
        onglet(a, u"Pointage"_s);
        verifie_panneau(a, u"pointage, fenêtre minimale"_s);
        a.change_mode(Mode::Simplifie);
        a.rafraichit();
        attend();
        verifie_panneau(a, u"guide, fenêtre minimale"_s);
        // l'interface agrandie (Ctrl +) : le panneau s'élargit ; puis de nouveau à 100 % (Ctrl 0)
        a.resize(1500, 900);
        attend();
        const int avant = QFontInfo(QApplication::font()).pixelSize();
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(&a, Qt::Key_Plus, Qt::ControlModifier);
        attend();
        QVERIFY2(std::abs(a.agrandissement() - 1.4) < 1e-9, qUtf8Printable(QString::number(a.agrandissement())));
        QVERIFY(QFontInfo(QApplication::font()).pixelSize() > avant);
        // tout suit, la barre d'état comprise
        QCOMPARE(QFontInfo(a.m_statut->font()).pixelSize(), QFontInfo(QApplication::font()).pixelSize());
        QCOMPARE(QFontInfo(a.m_info_mode->font()).pixelSize(), QFontInfo(QApplication::font()).pixelSize());
        // les icônes du coin des menus suivent l'agrandissement, sans dépasser la hauteur d'un titre de
        // menu
        const int titre = a.menuBar()->actionGeometry(a.menuBar()->actions().first()).height();
        QCOMPARE(a.findChild<QToolButton *>(u"theme"_s)->iconSize().height(), std::min(28, titre - 4));
        QCOMPARE(a.findChild<QToolButton *>(u"theme"_s)->height(), titre);
        // et le coin a la place de ses boutons agrandis
        QWidget *coin = a.menuBar()->cornerWidget(Qt::TopRightCorner);
        QVERIFY2(coin->width() >= coin->sizeHint().width() && coin->height() >= titre,
                 qUtf8Printable(u"%1 × %2 pour %3 × %4"_s.arg(coin->width()).arg(coin->height()).arg(coin->sizeHint().width()).arg(titre)));
        verifie_panneau(a, u"guide agrandi"_s);
        rendu(a, u"68_guide_agrandi"_s);
        a.change_mode(Mode::Expert);
        a.rafraichit();
        attend();
        verifie_panneau(a, u"pointage agrandi"_s);
        QTest::keyClick(&a, Qt::Key_0, Qt::ControlModifier);
        attend();
        QCOMPARE(a.agrandissement(), 1.0);
        QCOMPARE(QFontInfo(QApplication::font()).pixelSize(), avant);
        QCOMPARE(QFontInfo(a.m_statut->font()).pixelSize(), avant);
    }

    /// chaque page d'aide contextuelle existe dans chaque langue
    void pages_d_aide()
    {
        const QDir dossier(chemins::dossier_notice());
        for (const QString &page : {u"index.html"_s, u"demarrer.html"_s, u"simplifie.html"_s, u"expert.html"_s}) {
            for (Langue l : TOUTES_LES_LANGUES)
                QVERIFY2(QFileInfo(dossier.filePath(i18n::code(l) + u'/' + page)).isFile(), qUtf8Printable(page));
        }
    }

    /// Le mode Simplifié, étape par étape.
    void guide_complet()
    {
        auto app = fenetre(outils::mire(28), Mode::Simplifie);
        App &a = *app;
        QCOMPARE(a.etat, Etat::Preparation);
        QCOMPARE(a.guide.etape, Etape::Origine);
        QVERIFY(a.choix_origine);
        QCOMPARE(a.page_d_aide(), u"simplifie.html#origine"_s);
        sans_energie(a);
        rendu(a, u"10_guide_origine"_s);

        // 1. l'origine : un glisser la place là où on lâche le bouton, puis un clic la déplace
        glisse_image(a, {120, 120}, {150, 160});
        QVERIFY(a.guide.origine_choisie);
        QVERIFY2((a.pointage.origine - Vecteur(150, 160)).module() < 1.0, qUtf8Printable(a.pointage.origine.texte()));
        clic_image(a, {45, 188});
        QVERIFY((a.pointage.origine - Vecteur(45, 188)).module() < 1.0);
        // on ne peut pas sauter l'étape des axes
        QVERIFY(!a.etape_accessible(Etape::Echelle));
        QVERIFY(clique(a, u"Suivant"_s));
        QCOMPARE(a.guide.etape, Etape::Axes);
        QVERIFY(!a.choix_origine);

        // Aller voir le mode expert avant même la première mesure, puis reprendre à la même étape.
        const Vecteur origine_guide = a.pointage.origine;
        QVERIFY(menu(a, u"Mode expert (toutes les fonctions)"_s));
        QCOMPARE(a.pointage.origine, origine_guide);
        QVERIFY(menu(a, u"Mode simplifié (pas à pas)"_s));
        QCOMPARE(a.guide.etape, Etape::Axes);
        QVERIFY(a.guide.origine_choisie);

        // 2. les axes : aucun choix d'office ; l'origine se déplace encore en la saisissant, mais un
        // glisser ailleurs sur l'image ne fait rien
        QVERIFY(!a.guide.axes_choisis);
        QVERIFY(!a.etape_faite(Etape::Axes));
        glisse_image(a, {46, 189}, {60, 200});
        QVERIFY2((a.pointage.origine - Vecteur(60, 200)).module() < 1.0, qUtf8Printable(a.pointage.origine.texte()));
        glisse_image(a, {200, 100}, {250, 120});
        QVERIFY((a.pointage.origine - Vecteur(60, 200)).module() < 1.0);
        glisse_image(a, {60, 200}, {45, 188});
        QVERIFY((a.pointage.origine - Vecteur(45, 188)).module() < 1.0);
        rendu(a, u"11_guide_axes"_s);
        QVERIFY(clique(a, u"x vers la droite, y vers le haut"_s));
        QVERIFY(a.guide.axes_choisis);
        QCOMPARE(std::pair(a.pointage.sens_x, a.pointage.sens_y), std::pair(1, 1));
        QVERIFY(clique(a, u"Suivant"_s));
        QCOMPARE(a.guide.etape, Etape::Echelle);

        // 3. l'échelle : on sélectionne l'étalon (deux clics), puis on donne sa longueur en mètre, en
        // notation scientifique
        QVERIFY(clique(a, u"Sélectionner l'étalon"_s));
        QCOMPARE(a.etat, Etat::Etalonnage);
        clic_image(a, {45, 188});
        QVERIFY(a.etalon_deux_clics);
        ZoneVideo *z = zone(a);
        souris(z, QEvent::MouseMove, z->vue().ecran({250, 188}), Qt::NoButton, Qt::NoButton);
        attend();
        rendu(a, u"12_guide_etalon_en_cours"_s);
        clic_image(a, {398, 188});
        QCOMPARE(a.etat, Etat::Preparation);
        QVERIFY(a.etalon_en_attente.has_value());
        const auto [p1, p2] = *a.etalon_en_attente;
        QVERIFY(std::abs((p2 - p1).module() - 353.0) < 1.5);
        QVERIFY(!a.pointage.echelle.est_definie());
        // le champ de la longueur a le focus
        QLineEdit *champ = a.m_guide->saisie_etalon();
        QVERIFY(champ->hasFocus() || QApplication::focusWidget() == champ);
        // une unité autre que mm, cm ou m est refusée
        QCOMPARE(a.unite_etalon, Unite::Metre);
        tape(champ, u"1 km"_s);
        QVERIFY(clique(a, u"Valider"_s));
        QVERIFY(a.guide.erreur_etalon && a.guide.erreur_etalon->contains(u"(cm)"_s));
        QVERIFY(!a.pointage.echelle.est_definie());
        rendu(a, u"13_guide_echelle_refusee"_s);
        // la longueur en centimètre, l'unité choisie dans la liste
        efface(champ);
        QComboBox *unite = a.m_guide->unite_etalon();
        unite->setCurrentIndex(unite->findText(u"cm"_s));
        attend();
        QCOMPARE(a.unite_etalon, Unite::Centimetre);
        tape(champ, u"85"_s);
        QVERIFY(clique(a, u"Valider"_s));
        Echelle e = a.pointage.echelle;
        QVERIFY2(e.est_definie(), qUtf8Printable(a.guide.saisie_etalon + u" : "_s + a.guide.erreur_etalon.value_or(QString())));
        QVERIFY(std::abs(e.longueur_reelle_etalon - 0.85) < 1e-12);
        QVERIFY(affiche(a, u"Échelle : 85 cm pour 353 pixels.\n1 pixel sur cet étalon représente 0,28 % de sa longueur."_s));
        rendu(a, u"14_guide_echelle"_s);
        // une unité écrite après le nombre remplace celle de la liste
        efface(champ);
        tape(champ, u"8,5E-1 m"_s);
        QVERIFY(clique(a, u"Valider"_s));
        e = a.pointage.echelle;
        QVERIFY(std::abs(e.longueur_reelle_etalon - 0.85) < 1e-12);
        QCOMPARE(a.unite_etalon, Unite::Metre);
        QCOMPARE(unite->currentText(), u"m"_s);
        QVERIFY(clique(a, u"Suivant"_s));
        QCOMPARE(a.guide.etape, Etape::Pointage);
        QCOMPARE(a.etat, Etat::Pointage);
        QVERIFY(!bouton(a, u"Terminer le pointage"_s));
        QVERIFY(!a.etape_accessible(Etape::Export));
        // Un projet étalonné peut reprendre avant son tout premier point.
        const QString etalonne = temporaire(u"etalonne.mevcine"_s);
        a.ecrit_projet(etalonne);
        {
            auto reprise = fenetre(etalonne, Mode::Expert, {}, true);
            QVERIFY(menu(*reprise, u"Mode simplifié (pas à pas)"_s));
            QVERIFY(reprise->pointage_ok());
            reprise->charge_projet(etalonne);
            reprise->rafraichit();
            QCOMPARE(reprise->guide.etape, Etape::Pointage);
            clic_image(*reprise, {365, 110});
            QCOMPARE(tableau(*reprise)->table()->rowCount(), 1);
            QVERIFY(clique(*reprise, u"Terminer le pointage"_s));
            QCOMPARE(reprise->guide.etape, Etape::Export);
        }

        // 4. le pointage de dix images ; le tableau se remplit à côté
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(a.index, i + 1);
            clic_image(a, {365.0 - 16.0 * i, 110.0 + 0.5 * i});
        }
        QCOMPARE(a.pointage.nb_images_pointees(), 10);
        QCOMPARE(a.index, 11);
        // le tableau de la vue de pointage permet de lire et de reprendre les mesures
        const Tableau t = Tableau::construit(a.pointage);
        QCOMPARE(t.en_tetes, QStringList({u"t (s)"_s, u"x (m)"_s, u"y (m)"_s}));
        QCOMPARE(t.lignes.size(), size_t(10));
        QCOMPARE(t.lignes[0][0], u"0,000"_s);
        QCOMPARE(t.lignes[1][0], u"0,040"_s);
        QVERIFY(affiche(a, u"Rec"_s));
        QCOMPARE(tableau(a)->table()->rowCount(), 10);
        rendu(a, u"15_guide_pointage"_s);
        QVERIFY(clique(a, u"Terminer le pointage"_s));
        QCOMPARE(a.guide.etape, Etape::Export);
        QCOMPARE(a.message_par_defaut(), u"Étape 5 sur 5 : Export"_s);
        for (Etape e : TOUTES_LES_ETAPES) {
            const QString titre = u"%1. %2"_s.arg(numero(e)).arg(nom(e));
            QAbstractButton *etape = bouton(a, titre);
            QVERIFY2(etape, qUtf8Printable(titre));
            QCOMPARE(etape->text(), titre);
            QCOMPARE(etape->isChecked(), e == Etape::Export);
        }
        QVERIFY(!affiche(a, u"Suivant"_s));
        QVERIFY(clique(a, u"Précédent"_s));
        QCOMPARE(a.guide.etape, Etape::Pointage);
        QVERIFY(tableau(a)->isVisible());
        QCOMPARE(tableau(a)->table()->rowCount(), 10);
        QVERIFY(clique(a, u"5. Export"_s));
        // en dehors du pointage, un clic sur l'image ne pointe rien
        clic_image(a, {100, 100});
        QCOMPARE(a.pointage.nb_images_pointees(), 10);

        // refaire la mesure de l'image 5
        const Vecteur ancienne = *a.pointage.point_de_l_objet(1, 4);
        bouton_reprise(a, 4)->click();
        attend();
        QCOMPARE(a.refait, 11);
        QCOMPARE(a.index, 5);
        rendu(a, u"16_guide_refaire"_s);
        clic_image(a, ancienne + Vecteur(6, -3));
        QCOMPARE(a.refait, std::nullopt);
        QCOMPARE(a.index, 11); // retour à l'image où l'on en était
        const Vecteur nouvelle = *a.pointage.point_de_l_objet(1, 4);
        QVERIFY((nouvelle - (ancienne + Vecteur(6, -3))).module() < 1.0);
        QCOMPARE(a.pointage.nb_images_pointees(), 10);

        // l'origine et les axes changent après le pointage : les mesures sont recalculées, et gardées
        const double x_avant = a.pointage.position_dans_repere(nouvelle).x;
        a.va_a_l_etape(Etape::Origine);
        a.rafraichit();
        QVERIFY(a.choix_origine);
        clic_image(a, {100, 250});
        QCOMPARE(a.pointage.nb_images_pointees(), 10);
        const double x_apres = a.pointage.position_dans_repere(nouvelle).x;
        QVERIFY(std::abs(x_avant - x_apres - 55.0 * 0.85 / 353.0) < 0.01);
        a.va_a_l_etape(Etape::Axes);
        a.rafraichit();
        QVERIFY(clique(a, u"x vers la gauche, y vers le bas"_s));
        QVERIFY(std::abs(a.pointage.position_dans_repere(nouvelle).x + x_apres) < 1e-9);
        rendu(a, u"17_guide_axes_changes"_s);
        a.va_a_l_etape(Etape::Axes);
        a.rafraichit();
        QVERIFY(clique(a, u"x vers la droite, y vers le haut"_s));

        // 5. l'export
        a.va_a_l_etape(Etape::Export);
        a.rafraichit();
        QCOMPARE(a.guide.etape, Etape::Export);
        QVERIFY(clique(a, u"Copier pour Python"_s));
        const QString texte = a.presse_papier;
        QVERIFY2(texte.startsWith(u"import numpy as np\n"_s), qUtf8Printable(texte));
        QVERIFY(texte.contains(u"vidéo : mire-28.avi"_s));
        QVERIFY(texte.contains(u"lst_t = np.array([0.0, 0.04, 0.08,"_s));
        QVERIFY(texte.contains(u"lst_x = np.array(["_s));
        QVERIFY(texte.contains(u"lst_y = np.array(["_s));
        QVERIFY(a.copie_a_jour());
        QCOMPARE(QGuiApplication::clipboard()->text(), texte);
        verifie_python(texte, u"assert len(lst_t) == len(lst_x) == len(lst_y) == 10\nassert abs(lst_t[9] - 0.36) < 1e-9"_s);
        rendu(a, u"18_guide_export"_s);
        // dans une petite fenêtre, rien n'est coupé : Précédent et Suivant restent visibles
        a.resize(a.minimumSize());
        attend();
        a.va_a_l_etape(Etape::Echelle);
        a.rafraichit();
        rendu(a, u"19_guide_petite_fenetre"_s);
        QVERIFY(bouton(a, u"Suivant"_s));
        QAbstractButton *precedent = bouton(a, u"Précédent"_s);
        QVERIFY(precedent);
        QVERIFY(a.rect().contains(QRect(precedent->mapTo(&a, QPoint(0, 0)), precedent->size())));
        a.resize(1280, 800);
        a.va_a_l_etape(Etape::Export);
        a.rafraichit();
        const QString csv = temporaire(u"mesures.csv"_s);
        a.ecrit_csv(csv);
        const QStringList lignes = lit_texte(csv).split(u"\r\n"_s, Qt::SkipEmptyParts);
        QCOMPARE(lignes[0], u"t (s),x (m),y (m)"_s);
        QCOMPARE(lignes.size(), 11);
        QVERIFY(lignes[1].startsWith(u"0,"_s));
        verifie_python(u"import numpy as np"_s,
                       u"t, x, y = np.genfromtxt(%1, delimiter=',', skip_header=1, unpack=True)\nassert len(t) == 10"_s.arg(
                           u"'"_s + csv + u"'"_s));
        sans_energie(a);
    }

    /// Le mode Expert : toutes les fonctions, sans aucune énergie
    void expert_complet()
    {
        auto app = fenetre(outils::mire(28), Mode::Expert);
        App &a = *app;
        QCOMPARE(a.etat, Etat::Preparation);
        QCOMPARE(a.nombre_images_video, 28);
        QCOMPARE(a.dimensions(), QSize(400, 300));
        QCOMPARE(a.page_d_aide(), u"expert.html#pointage"_s);
        // un pictogramme par onglet, à la hauteur du texte
        for (int i = 0; i < a.m_expert->count(); ++i)
            QVERIFY(!a.m_expert->tabIcon(i).isNull());
        QCOMPARE(a.m_expert->iconSize().height(), QFontMetrics(QApplication::font()).height());
        rendu(a, u"30_expert"_s);

        // l'échelle : l'étalon d'abord (tirer-glisser), sa longueur ensuite
        QVERIFY(clique(a, u"Définir l'échelle"_s));
        QCOMPARE(a.etat, Etat::Etalonnage);
        glisse_image(a, {45, 188}, {398, 188});
        QCOMPARE(a.etat, Etat::Preparation);
        QVERIFY2(!a.dialogues.empty(), "le dialogue de la longueur doit s'ouvrir");
        auto *dialogue = qobject_cast<DialogueEchelle *>(a.dialogue_affiche());
        QVERIFY(dialogue);
        rendu(a, u"30_expert_longueur"_s);
        QCOMPARE(dialogue->unite()->currentText(), u"m"_s);
        tape(dialogue->champ(), u"0,85"_s);
        QVERIFY(clique(a, u"OK"_s));
        const Echelle e = a.pointage.echelle;
        QVERIFY(e.est_definie());
        QVERIFY(std::abs(e.longueur_reelle_etalon - 0.85) < 1e-12);
        QVERIFY2(std::abs(e.etalon_pixels() - 353.0) < 1.5, qUtf8Printable(QString::number(e.etalon_pixels())));
        QVERIFY(affiche(a, u"0,85 m pour 353 px\n1 pixel sur cet étalon représente 0,28 % de sa longueur."_s));

        // avant le pointage, l'origine se fait glisser
        const Vecteur centre = a.pointage.origine;
        glisse_image(a, centre, {210, 160});
        QVERIFY2((a.pointage.origine - Vecteur(210, 160)).module() < 1.0, qUtf8Printable(a.pointage.origine.texte()));
        glisse_image(a, {210, 160}, centre);

        // démarrage du pointage manuel, sur dix images
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::Pointage);
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(a.index, i + 1);
            clic_image(a, {365.0 - 16.0 * i, 110.0 + 0.5 * i});
        }
        QCOMPARE(a.pointage.image_initiale(), 1);
        QCOMPARE(a.pointage.image_finale(), 10);
        const Vecteur p5 = *a.pointage.point_de_l_objet(1, 4);
        QVERIFY2(std::abs(p5.x - 301.0) < 1.0 && std::abs(p5.y - 112.0) < 1.0, qUtf8Printable(p5.texte()));
        rendu(a, u"31_expert_pointage"_s);

        // défaire, refaire
        QVERIFY(clique(a, u"Annuler le dernier point (Ctrl+Z)"_s));
        QCOMPARE(a.pointage.image_finale(), 9);
        QCOMPARE(a.index, 10);
        QVERIFY(clique(a, u"Rétablir le point (Ctrl+Y)"_s));
        QCOMPARE(a.pointage.image_finale(), 10);
        QCOMPARE(a.index, 11);

        // un pointage hors des images voisines est refusé
        a.change_image(20);
        a.rafraichit();
        clic_image(a, {100, 100});
        QCOMPARE(a.pointage.image_finale(), 10);
        a.change_image(11);
        a.rafraichit();

        // les trajectoires, avec les vecteurs vitesse
        onglet(a, u"Trajectoires"_s);
        QCOMPARE(a.onglet, Onglet::Trajectoires);
        QVERIFY(clique(a, u"Montrer les vecteurs vitesse"_s));
        QVERIFY(a.traj.vecteurs);
        // l'échelle automatique : le plus long vecteur mesure environ le septième de l'image
        QVERIFY(a.traj.echelle_vitesse.isEmpty());
        const double echelle = echelle_vitesse_auto(a.pointage.fleches_vitesse(1.0), a.dimensions());
        double plus_long = 0.0;
        for (const auto &[o, b] : a.pointage.fleches_vitesse(echelle)[0])
            plus_long = std::max(plus_long, (b - o).module());
        QVERIFY2(plus_long > 25.0 && plus_long < 80.0, qUtf8Printable(QString::number(plus_long)));
        QCOMPARE(arrondi_rond(47.0), 50.0);
        QCOMPARE(arrondi_rond(0.3), 0.2);
        rendu(a, u"32_expert_vitesses"_s);
        // la trajectoire sur le fond des graphiques : blanc en thème clair
        {
            auto *dessin = a.m_onglet_trajectoire->findChild<DessinTrajectoire *>();
            QVERIFY(dessin);
            const QImage image = dessin->grab().toImage();
            const dessin::Vue vue = dessin::Vue::ajuste(QRectF(dessin->rect()), a.dimensions());
            const QPoint coin = (vue.rect.topLeft() + QPointF(3, 3)).toPoint() * image.devicePixelRatio();
            QVERIFY2(QColor(image.pixel(coin)).lightness() > 240, qUtf8Printable(QColor(image.pixel(coin)).name()));
        }
        a.traj.mode = ModeTrajectoire::Chronophotographie;
        a.rafraichit();
        rendu(a, u"33_expert_chronophotographie"_s);
        a.traj.mode = ModeTrajectoire::Chronogramme;
        a.rafraichit();
        rendu(a, u"34_expert_chronogramme"_s);
        a.traj.mode = ModeTrajectoire::Trajectoire;
        a.traj.referentiel = 1;
        a.rafraichit();
        // la vidéo recadrée autour de l'objet 1
        QVERIFY(clique(a, u"Voir la vidéo"_s));
        QTest::qWait(200);
        auto *ralenti = a.findChild<FenetreRalenti *>();
        QVERIFY(ralenti && ralenti->isVisible());
        ralenti->grab().save(QDir(QString::fromUtf8(MEVCINE_RENDUS_TESTS)).filePath(u"35_expert_video_recadree.png"_s));
        QVERIFY(clique(a, u"Fermer"_s));
        QVERIFY(!a.findChild<FenetreRalenti *>());

        // les coordonnées : le tableau et les deux exports, sans énergie
        onglet(a, u"Coordonnées"_s);
        QVERIFY(bouton(a, u"Copier pour Python"_s));
        QVERIFY(bouton(a, u"Enregistrer en CSV…"_s));
        sans_energie(a);
        const Tableau t = Tableau::construit(a.pointage);
        QCOMPARE(t.en_tetes, QStringList({u"t (s)"_s, u"x (m)"_s, u"y (m)"_s}));
        QCOMPARE(t.lignes.size(), size_t(10));
        rendu(a, u"36_expert_coordonnees"_s);
        // refaire une mesure depuis le tableau : retour au tableau ensuite
        bouton_reprise(a, 2)->click();
        attend();
        QCOMPARE(a.onglet, Onglet::Pointage);
        QCOMPARE(a.index, 3);
        const Vecteur p3 = *a.pointage.point_de_l_objet(1, 2);
        clic_image(a, p3 + Vecteur(2, 0));
        QCOMPARE(a.onglet, Onglet::Coordonnees);
        QCOMPARE(a.index, 11);

        // le grapheur : positions, vitesses et accélérations seulement
        onglet(a, u"Grapheur"_s);
        QCOMPARE(a.page_d_aide(), u"expert.html#grapheur"_s);
        QStringList noms;
        for (const auto &s : grandeurs_disponibles(a.pointage.nombre_objets()))
            noms << s.nom;
        QCOMPARE(noms, QStringList({u"t"_s, u"x"_s, u"y"_s, u"v"_s, u"vx"_s, u"vy"_s, u"a"_s, u"ax"_s, u"ay"_s}));
        // au départ, le graphique est là, vide : t en abscisse, la grandeur à tracer reste à choisir
        Graphe *graphe = a.m_onglet_graph->graphe();
        QVERIFY(graphe->isVisible());
        QCOMPARE(a.graph.x, std::optional(u"t"_s));
        QVERIFY(!a.graph.y);
        QCOMPARE(graphe->message(), u"Choisissez en haut la grandeur à tracer, et en fonction de quoi."_s);
        QVERIFY(graphe->titre().isEmpty());
        QVERIFY(!bouton(a, u"Enregistrer"_s));
        rendu(a, u"37_expert_grapheur_vide"_s);
        // le choix de la grandeur, dans sa liste, trace aussitôt le graphique, avec son titre
        auto *tracer = a.m_onglet_graph->findChildren<QComboBox *>().first();
        tracer->setCurrentIndex(tracer->findData(u"x"_s));
        attend();
        QCOMPARE(a.graph.y, std::optional(u"x"_s));
        QCOMPARE(graphe->titre(), u"x en fonction de t"_s);
        QVERIFY(bouton(a, u"Enregistrer"_s));
        sans_energie(a);
        rendu(a, u"37_expert_grapheur_x_t"_s);
        a.graph.y = u"vx"_s;
        a.graph.style = 1;
        a.rafraichit();
        rendu(a, u"38_expert_grapheur_vx_t"_s);

        // l'origine se change encore après le pointage
        onglet(a, u"Pointage"_s);
        QVERIFY(clique(a, u"Placer l'origine"_s));
        QVERIFY(a.choix_origine);
        clic_image(a, {45, 188});
        QVERIFY(!a.choix_origine);
        QCOMPARE(a.pointage.nb_images_pointees(), 10);

        // enregistrement du projet, puis réouverture
        const QString projet = temporaire(u"mesure.mevcine"_s);
        a.ecrit_projet(projet);
        QVERIFY(lit_texte(projet).startsWith(u"# version = mevcine"_s));
        const Pointage avant = a.pointage;
        auto app2 = fenetre(projet, Mode::Expert);
        const App &b = *app2;
        QCOMPARE(b.etat, Etat::Pointage);
        QCOMPARE(b.pointage.image_initiale(), 1);
        QCOMPARE(b.pointage.image_finale(), 10);
        QCOMPARE(b.index, 11);
        for (int i = 0; i < 10; ++i)
            QVERIFY2((*avant.point_de_l_objet(1, i) - *b.pointage.point_de_l_objet(1, i)).module() < 0.01, qUtf8Printable(u"image %1"_s.arg(i + 1)));
        QVERIFY(std::abs(b.pointage.echelle.pixels_par_metre() - avant.echelle.pixels_par_metre()) < 1e-6);
        QVERIFY(std::abs(b.pointage.delta_t - avant.delta_t) < 1e-12);
        rendu(*app2, u"39_expert_projet_rouvert"_s);

        // le même projet reprend au pointage avec ses mesures dans le tableau
        auto app3 = fenetre(projet, Mode::Simplifie);
        QCOMPARE(app3->guide.etape, Etape::Pointage);
        QVERIFY(tableau(*app3)->isVisible());
        QCOMPARE(tableau(*app3)->table()->rowCount(), 10);
        QVERIFY(app3->guide.origine_choisie && app3->guide.axes_choisis);
    }

    /// Coordonnées : le tableau prend la place, ses colonnes entières (« Refaire » comprise), l'export
    /// tient sur une ligne au-dessus. Pointage : on l'arrête et on le reprend ; en plein écran, la
    /// loupe reste là et s'écarte de la souris.
    void grapheur_trace_et_survol()
    {
        Graphe g(nullptr);
        g.resize(900, 560);
        g.show();
        QVERIFY(QTest::qWaitForWindowExposed(&g));
        // Une montée, un plateau, une verticale, un retour en arrière et un point isolé.
        const std::vector<Graphe::Point> points{
            {{0, 0}, 0, 0}, {{1, 1}, 1, 1}, {{2, 1}, 2, 2},
            {{2, -1}, 3, 3}, {{1, -1}, 4, 4}, {{4, 0}, 6, 6}
        };
        const auto survole = [&](QPointF coordonnees) {
            souris(&g, QEvent::MouseMove, g.ecran(coordonnees), Qt::NoButton, Qt::NoButton);
        };
        const auto montre = [&](int style) { g.montre(points, {}, u"x (m)"_s, u"y (m)"_s, style, true); };
        montre(0);
        const QImage points_seuls = g.grab().toImage();
        for (int style : {0, 1, 2}) {
            montre(style);
            for (const auto &p : points) {
                survole(p.coordonnees);
                QVERIFY(g.lecture());
                QCOMPARE(g.lecture()->image, p.image);
                QCOMPARE(g.lecture()->coordonnees, p.coordonnees);
            }
            survole({2.5, -0.5}); // le milieu du segment qui franchirait l'image manquante
            QVERIFY(!g.lecture());
            const QImage trace = g.grab().toImage();
            const auto trace_visible = [&](QPointF position) {
                const QPoint centre = (g.ecran(position) * trace.devicePixelRatio()).toPoint();
                const QRect voisinage(centre - QPoint(2, 2), QSize(5, 5));
                return trace.copy(voisinage) != points_seuls.copy(voisinage);
            };
            QVERIFY(!trace_visible({2.5, -0.5}));
            QVERIFY(!trace_visible({0.5, 0.5})); // milieu de la corde, sous la courbe
            // Sur le premier intervalle y = x + x² − x³. Les autres suivent les deux axes.
            const std::vector<std::pair<QPointF, int>> lectures{
                {{0.25, 0.296875}, 0}, {{0.5, 0.625}, 1}, {{0.75, 0.890625}, 1},
                {{1.6, 1}, 2}, {{2, -0.25}, 3}, {{1.4, -1}, 4}
            };
            for (const auto &[position, numero] : lectures) {
                QCOMPARE(trace_visible(position), style != 0);
                survole(position);
                QCOMPARE(g.lecture().has_value(), style != 0);
                if (style != 0) {
                    QCOMPARE(g.lecture()->image, points[numero].image);
                    QCOMPARE(g.lecture()->coordonnees, points[numero].coordonnees);
                }
            }
            QCOMPARE(g.image_du_trace().toImage(), trace);
        }
        const QPointF milieu(0.5, 0.625);
        const QPointF curseur = g.ecran(milieu);
        const QRectF vue = g.vue();
        QWheelEvent zoom(curseur, g.mapToGlobal(curseur), {}, {0, 120}, Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false);
        QApplication::sendEvent(&g, &zoom);
        QVERIFY(g.vue().width() < vue.width());
        QVERIFY(g.lecture() && *g.lecture() == points[1]);
        glisse(&g, curseur, curseur + QPointF(60, 30));
        QVERIFY(QLineF(g.ecran(milieu), curseur + QPointF(60, 30)).length() < 1e-5);
        survole(milieu);
        QVERIFY(g.lecture() && *g.lecture() == points[1]);
        QEvent sortie(QEvent::Leave);
        QApplication::sendEvent(&g, &sortie);
        QVERIFY(!g.lecture());
        souris(&g, QEvent::MouseButtonDblClick, curseur, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(g.vue(), vue);
        QWheelEvent zoom_fort(curseur, g.mapToGlobal(curseur), {}, {0, 2400}, Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
        QApplication::sendEvent(&g, &zoom_fort);
        QVERIFY(!g.lecture()); // seules des portions de trait sont encore visibles
    }

    void grapheur_lecture_dans_la_fenetre()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        App &a = *app;
        a.etat = Etat::Pointage;
        Pointage &p = a.pointage;
        p.origine = {40, 100};
        p.delta_t = 0.25;
        p.instants.emplace(p.nombre_images());
        for (int i = 1; i < p.nombre_images(); ++i)
            (*p.instants)[i] = 2 * i - 1;
        p.memorise_point(1, {40, 100}, 0);
        p.memorise_point(1, {42, 100}, 1);
        p.memorise_point(1, {42, 100}, 2);
        const QString mesures = a.texte_python();
        a.graph.y = u"x"_s;
        a.graph.style = 1;
        a.onglet = Onglet::Grapheur;
        a.rafraichit();
        attend();
        Graphe *g = a.m_onglet_graph->graphe();
        const QRect avant = g->geometry();
        const auto lis = [&](QPointF coordonnees) {
            souris(g, QEvent::MouseMove, g->ecran(coordonnees), Qt::NoButton, Qt::NoButton);
            attend();
            for (auto *label : a.m_onglet_graph->findChildren<QLabel *>()) {
                if (label->isVisible() && label->text().contains(u" = "))
                    return label->text().simplified();
            }
            return QString();
        };
        QCOMPARE(lis({0.25, 2}), u"t = 0,25 s · x = 2 px · Point de l'image 2"_s);
        const QPointF milieu(0.125, 1.25);
        QCOMPARE(lis(milieu), u"t = 0,25 s · x = 2 px · Point de l'image 2"_s);
        QCOMPARE(g->geometry(), avant);
        rendu(a, u"38_expert_lecture_courbe"_s);
        QVERIFY(g->lecture() && g->lecture()->image == 1);
        QCOMPARE(g->geometry(), avant);
        QSignalSpy images(g, &Graphe::pointClique);
        clic(g, g->ecran(milieu));
        QTest::qWait(QApplication::doubleClickInterval() + 50);
        QCOMPARE(images.count(), 0);
        clic(g, g->ecran({0.25, 2}));
        QTRY_COMPARE(images.count(), 1);
        QCOMPARE(images.first().first().toInt(), 1);
        auto *details = a.m_onglet_graph->findChild<QDialog *>();
        QVERIFY(details && details->isVisible());
        details->close();
        QCOMPARE(a.texte_python(), mesures);
        a.findChild<QToolButton *>(u"langue"_s)->click();
        QCOMPARE(lis(milieu), u"t = 0.25 s · x = 2 px · Point from frame 2"_s);
        p.memorise_point(1, {46, 100}, 4);
        a.graph.y = u"t"_s;
        a.rafraichit();
        QVERIFY(lis({p.date_relative(3), p.date_relative(3)}).isEmpty());
        QVERIFY(!lis({p.date_relative(4), p.date_relative(4)}).isEmpty());
    }

    void plein_ecran_et_arret_du_pointage()
    {
        auto app = fenetre(outils::mire(28), Mode::Expert);
        App &a = *app;
        QVERIFY(!bouton(a, u"Plein écran"_s));
        // l'échelle, pour que le mode simplifié aille ensuite jusqu'à l'export
        QVERIFY(clique(a, u"Définir l'échelle"_s));
        glisse_image(a, {45, 188}, {398, 188});
        auto *dialogue = qobject_cast<DialogueEchelle *>(a.dialogue_affiche());
        QVERIFY(dialogue);
        tape(dialogue->champ(), u"0,85"_s);
        QVERIFY(clique(a, u"OK"_s));
        QVERIFY(a.pointage.echelle.est_definie());
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::Pointage);
        QVERIFY(!bouton(a, u"Démarrer le pointage"_s));
        for (int i = 0; i < 4; ++i)
            clic_image(a, {365.0 - 16.0 * i, 110.0});
        QCOMPARE(a.pointage.nb_images_pointees(), 4);

        // le plein écran : l'image, la loupe dans son cadre, la barre des images, l'arrêt
        QVERIFY(clique(a, u"Plein écran"_s));
        QVERIFY(a.plein_ecran());
        PleinEcran *pe = a.m_plein_ecran;
        QVERIFY(pe->isFullScreen() || pe->windowState() & Qt::WindowFullScreen);
        QVERIFY(pe->cadre_loupe()->isVisible());
        QVERIFY(pe->zone()->geometry().contains(pe->cadre_loupe()->geometry().topLeft()));
        QVERIFY(bouton(a, u"Quitter le plein écran"_s));
        QVERIFY(bouton(a, u"Arrêter le pointage"_s));
        // la consigne, que la barre d'état cachée ne montre plus
        QVERIFY(affiche(a, u"Cliquez sur l'objet"_s));
        ZoneVideo *z = pe->zone();
        clic(z, z->vue().ecran({301.0, 110.0}));
        QCOMPARE(a.pointage.nb_images_pointees(), 5);
        // la souris près du cadre : il passe de l'autre côté
        QVERIFY(!pe->cadre_a_droite());
        souris(z, QEvent::MouseMove, z->mapFrom(pe, pe->cadre_loupe()->geometry().center()), Qt::NoButton, Qt::NoButton);
        QVERIFY(pe->cadre_a_droite());
        QVERIFY(pe->cadre_loupe()->geometry().left() > pe->width() / 2);
        attend();
        pe->grab().save(QDir(QString::fromUtf8(MEVCINE_RENDUS_TESTS)).filePath(u"40_plein_ecran.png"_s));
        // Échap : retour à la fenêtre, le pointage continue
        QTest::keyClick(pe, Qt::Key_Escape);
        attend();
        QVERIFY(!a.plein_ecran());
        QVERIFY(!a.pointage_arrete);
        clic_image(a, {285.0, 110.0});
        QCOMPARE(a.pointage.nb_images_pointees(), 6);

        // arrêter le pointage, depuis le plein écran : les coordonnées s'affichent
        QVERIFY(clique(a, u"Plein écran"_s));
        QVERIFY(clique(a, u"Arrêter le pointage"_s));
        QVERIFY(!a.plein_ecran());
        QVERIFY(a.pointage_arrete);
        QCOMPARE(a.onglet, Onglet::Coordonnees);
        QCOMPARE(a.etat, Etat::Pointage);

        // le tableau prend presque toute la largeur ; rien n'y est caché, « Refaire » compris
        TableauMesures *t = tableau(a);
        QTableWidget *table = t->table();
        QVERIFY2(table->width() > a.m_onglet_coord->width() * 9 / 10, qUtf8Printable(QString::number(table->width())));
        QVERIFY(!table->horizontalScrollBar()->isVisible());
        const int derniere = table->columnCount() - 1;
        QCOMPARE(table->horizontalHeaderItem(derniere - 1)->text(), u"Rec"_s);
        QCOMPARE(table->horizontalHeaderItem(derniere)->text(), u"Sup."_s);
        QVERIFY(table->horizontalHeader()->sectionViewportPosition(derniere) + table->columnWidth(derniere)
                <= table->viewport()->width());
        QVERIFY(table->columnWidth(1) > 2 * table->fontMetrics().horizontalAdvance(u"-0,00000"_s));
        // l'export, sur une ligne au-dessus du tableau
        QAbstractButton *copier = bouton(a, u"Copier pour Python"_s);
        QAbstractButton *csv = bouton(a, u"Enregistrer en CSV…"_s);
        QVERIFY(copier && csv && bouton(a, u"Enregistrer le projet…"_s));
        QCOMPARE(copier->mapTo(&a, QPoint()).y(), csv->mapTo(&a, QPoint()).y());
        QVERIFY(copier->mapTo(&a, QPoint(0, copier->height())).y() < table->mapTo(&a, QPoint()).y());
        rendu(a, u"42_coordonnees_pointage_arrete"_s);

        // pointage arrêté : un clic sur l'image ne pointe plus ; la mesure demandée par le tableau, si
        onglet(a, u"Pointage"_s);
        QVERIFY(affiche(a, u"Pointage arrêté : « Reprendre le pointage » pour continuer"_s));
        QVERIFY(!bouton(a, u"Arrêter le pointage"_s));
        clic_image(a, {250.0, 110.0});
        QCOMPARE(a.pointage.nb_images_pointees(), 6);
        rendu(a, u"41_expert_pointage_arrete"_s);
        onglet(a, u"Coordonnées"_s);
        bouton_reprise(a, 1)->click();
        attend();
        QCOMPARE(a.onglet, Onglet::Pointage);
        clic_image(a, *a.pointage.point_de_l_objet(1, 1) + Vecteur(1, 0));
        QCOMPARE(a.onglet, Onglet::Coordonnees);
        QCOMPARE(a.pointage.nb_images_pointees(), 6);
        // la reprise : on pointe de nouveau, à la suite
        onglet(a, u"Pointage"_s);
        QVERIFY(clique(a, u"Reprendre le pointage"_s));
        QVERIFY(!a.pointage_arrete);
        a.change_image(7);
        a.rafraichit();
        clic_image(a, {253.0, 110.0});
        QCOMPARE(a.pointage.nb_images_pointees(), 7);

        // en mode simplifié, terminer le pointage en plein écran ouvre directement l'export
        QVERIFY(menu(a, u"Mode simplifié (pas à pas)"_s));
        QCOMPARE(a.mode, Mode::Simplifie);
        a.va_a_l_etape(Etape::Pointage);
        a.rafraichit();
        attend();
        QVERIFY(clique(a, u"Plein écran"_s));
        QVERIFY(a.plein_ecran());
        QVERIFY(clique(a, u"Terminer le pointage"_s));
        QVERIFY(!a.plein_ecran());
        QCOMPARE(a.guide.etape, Etape::Export);
        QVERIFY(!bouton(a, u"Plein écran"_s));
        QCOMPARE(tableau(a)->table()->rowCount(), 7);
    }

    /// L'agrandissement retenu revient au démarrage, borné par la largeur de la fenêtre : une fenêtre
    /// rétrécie le réduit, élargie elle le rend ; la configuration garde la valeur demandée
    void agrandissement_retenu()
    {
        configuration(std::nullopt);
        ecrit_texte(prefs::fichier_conf(), lit_texte(prefs::fichier_conf()) + u"agrandissement = 1.5\n"_s);
        auto app = fenetre({}, Mode::Expert, std::nullopt, true);
        App &a = *app;
        a.resize(1400, 800);
        attend();
        QVERIFY2(std::abs(a.agrandissement() - 1.5) < 1e-9, qUtf8Printable(QString::number(a.agrandissement())));
        a.resize(1000, 800);
        attend();
        QVERIFY2(std::abs(a.agrandissement() - 1000.0 / 900.0) < 1e-9, qUtf8Printable(QString::number(a.agrandissement())));
        a.resize(1400, 800);
        attend();
        QVERIFY2(std::abs(a.agrandissement() - 1.5) < 1e-9, qUtf8Printable(QString::number(a.agrandissement())));
        QVERIFY(lit_texte(prefs::fichier_conf()).contains(u"agrandissement = 1.5"_s));
        // les essais suivants, à 100 %
        QTest::keyClick(&a, Qt::Key_0, Qt::ControlModifier);
        attend();
        QCOMPARE(a.agrandissement(), 1.0);
    }

    void visee_assistee_et_validation()
    {
        auto app = fenetre(outils::mire(16), Mode::Simplifie);
        App &a = *app;
        a.guide.origine_choisie = a.guide.axes_choisis = true;
        a.pointage.echelle = {{10, 10}, {110, 10}, 1.0};
        a.va_a_l_etape(Etape::Pointage);
        a.rafraichit();
        ZoneVideo *z = zone(a);
        auto *loupe = a.m_guide->findChild<Loupe *>();
        const QPointF centre = QRectF(loupe->rect()).center();
        QVERIFY(clique(a, u"Visée assistée"_s));
        clic(loupe, centre);
        QVERIFY(!a.visee_proposee());
        QCOMPARE(a.pointage.nb_images_pointees(), 0);

        const Vecteur premier(56.25, 115.5);
        a.clic_pointage(premier);
        a.rafraichit();
        const auto deuxieme = a.visee_proposee();
        QVERIFY(deuxieme);
        QVERIFY((*deuxieme - (premier + Vecteur(8, 0))).module() <= 1);
        const QImage vue_fixe = loupe->grab().toImage();
        souris(z, QEvent::MouseMove, z->vue().ecran({210, 30}), Qt::NoButton, Qt::NoButton);
        QEvent sortie(QEvent::Leave);
        QApplication::sendEvent(z, &sortie);
        QCOMPARE(loupe->centre(), deuxieme);
        QCOMPARE(loupe->grab().toImage(), vue_fixe);
        QCOMPARE(a.pointage.nb_images_pointees(), 1);
        clic(loupe, centre - QPointF(1, 1)); // dans le même pixel vidéo
        QCOMPARE(a.pointage.point_de_l_objet(1, 1), deuxieme);
        QCOMPARE(a.pointage.saisie(1, 1), Pointage::Saisie::Assistee);
        QCOMPARE(a.pointage.exemples_visee(1, 2).size(), 1u);

        const auto troisieme = a.visee_proposee();
        QVERIFY(troisieme);
        const double zoom = std::max(3.0, 2 * z->vue().echelle);
        clic(loupe, centre + QPointF(2 * zoom, -zoom));
        const Vecteur corrige = *troisieme + Vecteur(2, -1);
        QCOMPARE(a.pointage.point_de_l_objet(1, 2), corrige);
        QCOMPARE(tableau(a)->table()->item(2, 1)->toolTip(), u"Visée corrigée par l’élève · référence"_s);
        QCOMPARE(a.pointage.exemples_visee(1, 3).back().second, corrige);
        a.retire_mesure_avant();
        QCOMPARE(a.pointage.exemples_visee(1, 3).size(), 1u);
        a.retablit_point();
        a.rafraichit();
        QCOMPARE(a.pointage.saisie(1, 2), Pointage::Saisie::AssisteeCorrigee);
        QCOMPARE(a.index, 4);
        const auto quatrieme = a.visee_proposee();
        QVERIFY(quatrieme);
        QTest::keyClick(z, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(loupe->centre(), *quatrieme + Vecteur(0, 1));
        QVERIFY(clique(a, u"Plein écran"_s));
        auto *pe = a.m_plein_ecran;
        auto *loupe_pe = pe->findChild<Loupe *>();
        QCOMPARE(loupe_pe->centre(), *quatrieme + Vecteur(0, 1));
        const QRect cadre = pe->cadre_loupe()->geometry();
        QVERIFY(!cadre.intersects(pe->zone()->geometry()));
        souris(pe->zone(), QEvent::MouseMove, pe->zone()->mapFrom(pe, cadre.center()), Qt::NoButton, Qt::NoButton);
        QCOMPARE(pe->cadre_loupe()->geometry(), cadre);
        QTest::keyClick(pe->zone(), Qt::Key_Space);
        QCOMPARE(a.pointage.point_de_l_objet(1, 3), *quatrieme + Vecteur(0, 1));
        QCOMPARE(a.pointage.saisie(1, 3), Pointage::Saisie::AssisteeCorrigee);
        const auto cinquieme = a.visee_proposee();
        QVERIFY(cinquieme);
        clic(loupe_pe, centre);
        QCOMPARE(a.pointage.point_de_l_objet(1, 4), cinquieme);
        QCOMPARE(a.pointage.saisie(1, 4), Pointage::Saisie::Assistee);
        QCOMPARE(a.pointage.exemples_visee(1, 5).size(), 3u);
        QTest::keyClick(pe, Qt::Key_Escape);
        attend();
        QVERIFY(!a.plein_ecran());
        QKeyEvent repetee(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QString(), true);
        QApplication::sendEvent(z, &repetee);
        QCOMPARE(a.index, 6);

        // Un clic libre a priorité. Sans détail identifiable, la loupe et Espace ne créent aucune mesure.
        clic_image(a, {240, 40});
        QCOMPARE(a.index, 7);
        QCOMPARE(a.pointage.saisie(1, 5), Pointage::Saisie::Manuelle);
        QVERIFY(!a.visee_proposee());
        QVERIFY(affiche(a, u"Détail incertain. Pointez-le dans la vidéo."_s));
        clic(loupe, centre);
        QTest::keyClick(z, Qt::Key_Space);
        QCOMPARE(a.pointage.nb_images_pointees(), 6);
        clic_image(a, {104, 115});
        QVERIFY(a.visee_proposee());

        // Une navigation entre l’appui et le relâchement annule le clic en cours.
        souris(loupe, QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
        a.change_image(7);
        a.rafraichit();
        souris(loupe, QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(a.pointage.nb_images_pointees(), 7);
        a.reprend_mesure(2);
        a.rafraichit();
        QVERIFY(!a.visee_proposee());
        a.annule_refait();
        a.change_mode(Mode::Expert);
        a.rafraichit();
        QVERIFY(!a.visee_proposee());
        a.change_mode(Mode::Simplifie);
        a.rafraichit();
        QVERIFY(a.guide.visee_assistee);
        const auto exemples = a.pointage.exemples_visee(1, 7);
        const QString fichier = temporaire(u"visee.mevcine"_s);
        a.ecrit_projet(fichier);
        a.charge_projet(fichier);
        QVERIFY(a.guide.visee_assistee);
        QCOMPARE(a.pointage.saisie(1, 1), Pointage::Saisie::Assistee);
        QCOMPARE(a.pointage.saisie(1, 2), Pointage::Saisie::AssisteeCorrigee);
        QCOMPARE(a.pointage.saisie(1, 3), Pointage::Saisie::AssisteeCorrigee);
        QCOMPARE(a.pointage.saisie(1, 5), Pointage::Saisie::Manuelle);
        const auto relus = a.pointage.exemples_visee(1, 7);
        QCOMPARE(relus.size(), exemples.size());
        for (size_t i = 0; i < relus.size(); ++i) {
            QCOMPARE(relus[i].first, exemples[i].first);
            QVERIFY((relus[i].second - exemples[i].second).module() < 1e-9);
        }
        // Les versions précédentes ne distinguaient pas une correction dans la loupe d'une validation au centre.
        auto lignes = lit_texte(fichier).split(u'\n');
        lignes.removeIf([](const QString &l) { return l.startsWith(u"# assistes_corriges ="); });
        const QString ancien = temporaire(u"ancienne-visee.mevcine"_s);
        ecrit_texte(ancien, lignes.join(u'\n'));
        a.charge_projet(ancien);
        QCOMPARE(a.pointage.saisie(1, 2), Pointage::Saisie::Assistee);
        QCOMPARE(a.pointage.exemples_visee(1, 7).size(), 3u);
    }

    void visee_apres_correction_et_suppression()
    {
        auto app = fenetre(outils::mire(16), Mode::Simplifie);
        App &a = *app;
        a.guide.origine_choisie = a.guide.axes_choisis = a.guide.visee_assistee = true;
        a.pointage.echelle = {{10, 10}, {110, 10}, 1.0};
        a.va_a_l_etape(Etape::Pointage);
        a.rafraichit();
        a.clic_pointage({56.25, 115.5});
        a.rafraichit();
        QVERIFY(a.visee_proposee());
        a.clic_pointage(*a.visee_proposee() + Vecteur(6, 0), Pointage::Saisie::Assistee);
        a.rafraichit();
        QVERIFY(!a.visee_proposee());
        QTest::keyClick(zone(a), Qt::Key_Space);
        QCOMPARE(a.pointage.nb_images_pointees(), 2);
        QCOMPARE(a.index, 3);
        a.clic_pointage({72.25, 115.5});
        a.rafraichit();
        QCOMPARE(a.index, 4);
        QVERIFY(!a.visee_proposee());
        // Même image affichée et même dernière mesure. Retirer la référence en conflit libère la visée.
        a.pointage.supprime_points({{1, 1}});
        a.rafraichit();
        QVERIFY(a.visee_proposee());
        QVERIFY2((*a.visee_proposee() - Vecteur(80.25, 115.5)).module() < 1, qPrintable(a.visee_proposee()->texte()));
        a.pointage.annule_mesure();
        a.rafraichit();
        QVERIFY(!a.visee_proposee());
        // Refaire cette mesure en expert doit aussi remplacer l'ancien exemple en mode simplifié.
        a.change_mode(Mode::Expert);
        a.reprend_mesure(2);
        a.rafraichit();
        a.clic_pointage({64.25, 115.5});
        a.change_mode(Mode::Simplifie);
        a.change_image(4);
        a.rafraichit();
        QVERIFY(a.visee_proposee());
        QVERIFY((*a.visee_proposee() - Vecteur(80.25, 115.5)).module() < 1);
    }

    void visee_sur_deux_objets_en_fin_de_video()
    {
        auto app = fenetre(outils::mire(16), Mode::Simplifie);
        App &a = *app;
        a.nombre_objets_demandes = 2;
        a.guide.origine_choisie = a.guide.axes_choisis = a.guide.visee_assistee = true;
        a.pointage.echelle = {{10, 10}, {110, 10}, 1.0};
        a.va_a_l_etape(Etape::Pointage);
        a.change_image(15);
        a.rafraichit();
        // Deux détails distincts sur le même carré mobile.
        a.clic_pointage({168, 115});
        a.clic_pointage({174, 112});
        a.rafraichit();
        QCOMPARE(a.index, 16);
        auto *loupe = a.m_guide->findChild<Loupe *>();
        const QPointF centre = QRectF(loupe->rect()).center();
        const auto premier = a.visee_proposee();
        QVERIFY(premier);
        souris(loupe, QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
        QTest::keyClick(zone(a), Qt::Key_Space);
        QCOMPARE(a.objet_courant, 2);
        const auto second = a.visee_proposee();
        QVERIFY(second && (*second - *premier).module() > 4);
        // Le relâchement de l'ancien clic ne valide pas le second objet de la même image.
        souris(loupe, QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
        QVERIFY(!a.pointage.point_de_l_objet(2, 15));
        clic(loupe, centre);
        QCOMPARE(a.pointage.point_de_l_objet(1, 15), premier);
        QCOMPARE(a.pointage.point_de_l_objet(2, 15), second);
        QVERIFY(!a.visee_proposee());
        QVERIFY(affiche(a, u"Cette mesure figure déjà dans le tableau."_s));
        clic(loupe, centre);
        QCOMPARE(a.index, 16);
        QCOMPARE(a.objet_courant, 1);
    }

    void suivi_automatique()
    {
        auto app = fenetre(outils::mire(28), Mode::Expert);
        App &a = *app;
        QVERIFY(clique(a, u"Suivi automatique"_s));
        QVERIFY(a.suivi_auto);
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::SelectionMotifs);
        QVERIFY(a.dialogues.empty());
        // Le carré se trouve dans le rectangle sélectionné à la troisième image.
        a.change_image(3);
        a.rafraichit();
        glisse_image(a, {40, 83}, {104, 147});
        // la détection se poursuit jusqu'à la dernière image
        QVERIFY(QTest::qWaitFor([&] { return a.etat == Etat::Pointage; }, 30000));
        QCOMPARE(a.pointage.image_initiale(), 3);
        QCOMPARE(a.pointage.image_finale(), 28);
        QCOMPARE(a.index, 28);
        QVERIFY(a.pointage_arrete);
        QCOMPARE(a.onglet, Onglet::Coordonnees);
        onglet(a, u"Pointage"_s);
        const auto dernier = a.pointage.point_de_l_objet(1, 27);
        clic_image(a, {60, 90});
        QCOMPARE(a.pointage.point_de_l_objet(1, 27), dernier);
        QVERIFY(!bouton(a, u"Arrêter le pointage"_s));
        std::vector<double> xs, ys;
        for (int i = 2; i < 28; ++i) {
            QVERIFY(a.pointage.saisie(1, i) != Pointage::Saisie::Manuelle);
            QVERIFY(!tableau(a)->table()->item(i - 2, 0)->icon().isNull());
            xs.push_back(a.pointage.point_de_l_objet(1, i)->x);
            ys.push_back(a.pointage.point_de_l_objet(1, i)->y);
        }
        // sur la première image, le motif est retrouvé là où on l'a pris
        QCOMPARE(std::pair(xs[0], ys[0]), std::pair(72.0, 115.0));
        // ensuite le carré part vers la droite, sans retour en arrière
        for (size_t k = 1; k < xs.size(); ++k)
            QVERIFY(xs[k] > xs[k - 1]);
        for (double y : ys)
            QVERIFY(std::abs(y - 115.0) < 15.0);
        rendu(a, u"40_suivi_auto"_s);
        const auto sources = a.pointage.provenances();
        const auto premier = a.pointage.point_de_l_objet(1, 2);
        QVERIFY(premier);
        QVERIFY(clique(a, u"Reprendre le pointage"_s));
        a.change_image(3);
        a.rafraichit();
        clic_image(a, *premier);
        QCOMPARE(a.pointage.saisie(1, 2), Pointage::Saisie::Manuelle);
        QVERIFY(tableau(a)->table()->item(0, 0)->icon().isNull());
        a.retire_mesure_avant();
        QCOMPARE(a.pointage.provenances(), sources);
        a.retablit_point();
        QCOMPARE(a.pointage.saisie(1, 2), Pointage::Saisie::Manuelle);
        const auto apres = a.pointage.provenances();
        const QString fichier = temporaire(u"suivi.mevcine"_s);
        a.ecrit_projet(fichier);
        a.charge_projet(fichier);
        QCOMPARE(a.pointage.provenances(), apres);
        onglet(a, u"Coordonnées"_s);
        auto *table = tableau(a)->table();
        QVERIFY(table->item(0, 1)->icon().isNull());
        for (int i = 1; i < table->rowCount(); ++i)
            QVERIFY(!table->item(i, 1)->icon().isNull() && !table->item(i, 2)->icon().isNull());
        a.pointage.supprime_points({{3, 1}});
        QCOMPARE(a.pointage.saisie(1, 3), Pointage::Saisie::Manuelle);
        a.retire_mesure_avant();
        QCOMPARE(a.pointage.provenances(), apres);

        // Un projet ancien ne signalait que les détections douteuses.
        QStringList lignes = prefs::lignes(lit_texte(fichier));
        lignes.removeIf([](const QString &ligne) { return ligne.startsWith(u"# automatiques ="); });
        for (QString &ligne : lignes) {
            if (ligne.startsWith(u"# a_verifier ="))
                ligne = u"# a_verifier = 3:1"_s;
        }
        const QString ancien = temporaire(u"ancien.mevcine"_s);
        ecrit_texte(ancien, lignes.join(u'\n'));
        a.charge_projet(ancien);
        QCOMPARE(a.pointage.provenances().size(), size_t(1));
        QCOMPARE(a.pointage.saisie(1, 3), Pointage::Saisie::AVerifier);
    }

    void interruption_du_suivi_avant_la_derniere_image()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        App &a = *app;
        a.change_image(15);
        a.suivi_auto = true;
        a.lance_pointage();
        a.fin_selection_motif({155, 100}, {185, 130});
        a.etape_detection();
        QCOMPARE(a.index, 16);
        a.arrete_suivi();
        QTRY_COMPARE(a.etat, Etat::Pointage);
        QCOMPARE(a.pointage.image_finale(), 15);
        QCOMPARE(a.onglet, Onglet::Pointage);
        QVERIFY(a.pointage_ok());
        clic_image(a, {176, 115});
        QCOMPARE(a.pointage.saisie(1, 15), Pointage::Saisie::Manuelle);
        QCOMPARE(a.pointage.image_finale(), 16);
    }

    void fin_du_suivi_apres_changement_de_mode()
    {
        for (bool avec_echelle : {false, true}) {
            auto app = fenetre(outils::mire(16), Mode::Expert);
            App &a = *app;
            a.guide.origine_choisie = a.guide.axes_choisis = true;
            if (avec_echelle)
                a.pointage.echelle = {{10, 10}, {110, 10}, 1.0};
            a.change_image(16);
            a.suivi_auto = true;
            a.lance_pointage();
            a.fin_selection_motif({155, 100}, {185, 130});
            a.change_mode(Mode::Simplifie);
            QTRY_COMPARE(a.etat, Etat::Pointage);
            QCOMPARE(a.pointage.image_finale(), 16);
            QCOMPARE(a.guide.etape, avec_echelle ? Etape::Export : Etape::Echelle);
            QVERIFY(!a.pointage_ok());
            a.change_mode(Mode::Expert);
            QVERIFY(a.pointage_arrete);
            QCOMPARE(a.onglet, Onglet::Coordonnees);
        }
    }

    void croisement_de_deux_mobiles_identiques()
    {
        const auto film = outils::video_test(u"croisement-identique.mkv"_s,
                                             {u"-f"_s, u"lavfi"_s, u"-i"_s,
                                              u"color=gray:s=128x72:r=25[bg];color=white:s=12x12:r=25,split[a][b];"
                                              "[bg][a]overlay=x=12+10*(n-1):y=32[g];[g][b]overlay=x=92-10*(n-1):y=32"_s,
                                              u"-frames:v"_s, u"9"_s, u"-c:v"_s, u"ffv1"_s});
        QVERIFY(film);
        auto app = fenetre(*film, Mode::Expert);
        App &a = *app;
        a.nombre_objets_demandes = 2;
        a.suivi_auto = true;
        a.ajuste_nombre_objets();
        a.lance_pointage();
        a.fin_selection_motif({10, 30}, {26, 46});
        a.fin_selection_motif({90, 30}, {106, 46});
        while (a.etat == Etat::SuiviAutomatique)
            a.etape_detection();
        for (int i = 0; i < 9; ++i) {
            QCOMPARE(a.pointage.point_de_l_objet(1, i), Vecteur(18 + 10 * i, 38));
            QCOMPARE(a.pointage.point_de_l_objet(2, i), Vecteur(98 - 10 * i, 38));
            for (int obj : {1, 2})
                QCOMPARE(a.pointage.saisie(obj, i), i == 4 ? Pointage::Saisie::AVerifier : Pointage::Saisie::Automatique);
        }
    }

    /// Une détection qui échoue sur un objet ne décale pas les objets suivants et n'empêche pas
    /// l'avancement d'image : l'objet est désigné par sa place dans la sélection.
    void suivi_automatique_objet_manquant()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        App &a = *app;
        a.nombre_objets_demandes = 2;
        a.ajuste_nombre_objets();
        a.index = 1;
        a.etat = Etat::SuiviAutomatique;
        a.pistes.clear();
        const auto img = a.image_courante();
        QVERIFY(img);
        a.pistes.emplace_back(QImage(), std::nullopt, 0); // motif impossible : la détection de l'objet 1 échoue
        a.pistes.emplace_back(*detect::decoupe(*img, 37, 95, 40, 40), std::nullopt, 0);
        a.prochaine_image_du_suivi = 1;
        a.etape_detection();
        QVERIFY(!a.pointage.point_de_l_objet(1, 0).has_value());
        QVERIFY(a.pointage.point_de_l_objet(2, 0).has_value());
        QCOMPARE(a.index, 2);
        a.arrete_suivi();
    }

    void interruption_du_suivi_par_une_image_illisible()
    {
        const QString film = temporaire(u"suivi.avi"_s), deplace = temporaire(u"suivi-deplace.avi"_s);
        QVERIFY(QFile::copy(outils::mire(16), film));
        auto app = fenetre(film, Mode::Expert);
        app->suivi_auto = true;
        app->lance_pointage();
        app->fin_selection_motif({35, 95}, {75, 135});
        app->etape_detection();
        QCOMPARE(app->index, 2);
        app->video->oublie_cache_et_flux();
        QVERIFY(QFile::rename(film, deplace));
        app->etape_detection();
        app->rafraichit();
        QCOMPARE(app->etat, Etat::Pointage);
        QCOMPARE(app->pointage.image_finale(), 1);
        QVERIFY(!app->prochaine_image_du_suivi);
        QVERIFY(affiche(*app, u"Image 2 illisible"_s));
        QVERIFY(QFile::rename(deplace, film));
        QVERIFY(clique(*app, u"Réessayer"_s));
        QVERIFY(app->pointage_ok());
    }

    void origine_au_centre_d_une_image_impaire()
    {
        const auto film = outils::video_test(u"dimensions-impaires.mkv"_s,
            {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=size=161x91:rate=5"_s,
             u"-frames:v"_s, u"2"_s, u"-c:v"_s, u"ffv1"_s, u"-pix_fmt"_s, u"bgr0"_s});
        QVERIFY(film);
        auto app = fenetre(*film, Mode::Expert);
        QCOMPARE(app->dimensions(), QSize(161, 91));
        QCOMPARE(app->pointage.origine, Vecteur(80.5, 45.5));
    }

    void projet_deplace_et_pointage_termine()
    {
        QTemporaryDir origine, destination;
        const QString premiere = origine.filePath(u"film.avi"_s), copie = destination.filePath(u"film.avi"_s);
        const QString projet = destination.filePath(u"seance.mevcine"_s);
        QVERIFY(QFile::copy(outils::mire(16), premiere));
        QVERIFY(QFile::copy(premiere, copie));
        auto app = fenetre(premiere, Mode::Expert);
        app->lance_pointage();
        clic_image(*app, {60, 115});
        app->arrete_pointage();
        app->ecrit_projet(projet);
        app.reset(); // permet aussi de remplacer la vidéo sous Windows
        ecrit_texte(premiere, u"Un autre fichier portant le même nom."_s);
        // Si la copie voisine correspond, la boîte de recherche de fichier reste fermée.
        auto reprise = fenetre(projet, Mode::Expert);
        QCOMPARE(reprise->fichier_video, copie);
        QCOMPARE(reprise->pointage.image_finale(), 1);
        QVERIFY(reprise->pointage_arrete);
        QCOMPARE(reprise->onglet, Onglet::Coordonnees);
        reprise->lance_pointage();
        QVERIFY(reprise->pointage_ok());
        // Une table décalée au-delà du film ne peut pas écraser les points de cette séance.
        const QString hors_video = destination.filePath(u"hors-video.mevcine"_s);
        ecrit_texte(hors_video, lit_texte(projet).replace(u"# index_depart = 1\n"_s, u"# index_depart = 17\n"_s));
        reprise->charge_projet(hors_video);
        QCOMPARE(reprise->pointage.image_finale(), 1);
        QVERIFY(!reprise->dialogues.empty());
        QVERIFY(reprise->dialogues.front().texte.contains(u"17"));
        reprise->ferme_dialogue(true);
        const QString invalide = destination.filePath(u"tableau-tronque.mevcine"_s);
        ecrit_texte(invalide, u"# version = mevcine 1.0\nt x1 y1\n0 42"_s);
        reprise->charge_projet(invalide);
        QCOMPARE(reprise->pointage.image_finale(), 1);
        QCOMPARE(reprise->fichier_video, copie);
        QVERIFY(!reprise->dialogues.empty());
        QVERIFY(reprise->dialogues.front().texte.contains(u"3"));
    }

    void image_illisible_et_reessai()
    {
        const QString copie = temporaire(u"video-mobile.avi"_s), deplacee = temporaire(u"video-absente.avi"_s);
        QVERIFY(QFile::copy(outils::mire(16), copie));
        auto app = fenetre(copie, Mode::Expert);
        App &a = *app;
        a.lance_pointage();
        a.rafraichit();
        QVERIFY(a.image_affichee());
        a.video->oublie_cache_et_flux();
        QVERIFY(QFile::rename(copie, deplacee));
        a.change_image(2);
        QVERIFY(!a.pointage_ok());
        a.rafraichit();
        QVERIFY(a.image.isNull());
        QVERIFY(!a.pointage_ok());
        QVERIFY(affiche(a, u"Image 2 illisible"_s));
        clic_image(a, {120, 100});
        QVERIFY(!a.pointage.a_des_pointages());
        QVERIFY(QFile::rename(deplacee, copie));
        QVERIFY(clique(a, u"Réessayer"_s));
        QVERIFY(a.image_affichee());
        QCOMPARE(a.image_cle->first, 2);
        clic_image(a, {120, 100});
        QVERIFY(a.pointage.point_de_l_objet(1, 1));
        QVERIFY(!a.pointage.point_de_l_objet(1, 0));
    }

    void deux_objets_occultation_et_reprise()
    {
        const auto film = outils::video_test(u"deux-objets-occultation.mkv"_s, {u"-f"_s, u"lavfi"_s, u"-i"_s,
            u"color=black:s=160x120:r=10:d=0.7"_s, u"-vf"_s,
            u"drawbox=x=20:y=30:w=14:h=14:c=white:t=fill:enable='lt(n,2)+between(n,4,5)',drawbox=x=94:y=62:w=16:h=16:c=red:t=fill"_s,
            u"-c:v"_s, u"ffv1"_s});
        QVERIFY(film);
        auto app = fenetre(*film, Mode::Simplifie);
        App &a = *app;
        clic_image(a, {15, 95});
        QVERIFY(a.guide.origine_choisie);
        QVERIFY(a.sauvegarde_de_secours());
        auto reglages = prefs::lit_projet(a.fichier_secours());
        QVERIFY(reglages && reglages->config.valeur_entiere(u"origine_choisie"_s) == 1);
        a.saisit_echelle();
        a.fin_echelle({10, 75}, {110, 75});
        a.valide_longueur_etalon(0.5);
        QVERIFY(a.sauvegarde_de_secours());
        reglages = prefs::lit_projet(a.fichier_secours());
        QVERIFY(reglages && reglages->config.valeur_reelle(u"etalon_m"_s) == 0.5);
        a.change_mode(Mode::Expert);
        a.nombre_objets_demandes = 2;
        a.suivi_auto = true;
        a.lance_pointage();
        a.fin_selection_motif({18,28}, {36,46});
        a.fin_selection_motif({92,60}, {112,80});
        QCOMPARE(a.etat, Etat::SuiviAutomatique);
        for (int i = 0; i < 3; ++i)
            a.etape_detection();
        QVERIFY(a.sauvegarde_de_secours());
        const auto suivi = prefs::lit_projet(a.fichier_secours());
        QVERIFY(suivi && suivi->config.valeur_texte(u"perdus"_s) == u"2:1"_s);
        while (a.etat == Etat::SuiviAutomatique)
            a.etape_detection();
        a.rafraichit();
        QCOMPARE(a.pointage.saisie(1,2), Pointage::Saisie::Perdue);
        QVERIFY(!a.pointage.point_de_l_objet(1,2));
        QCOMPARE(a.pointage.saisie(1,4), Pointage::Saisie::Automatique);
        QCOMPARE(a.pointage.saisie(2,2), Pointage::Saisie::Automatique);

        const QString python = a.texte_python();
        const auto sources = a.pointage.provenances();
        const auto q1 = a.pointage.point_de_l_objet(1,0), q2 = a.pointage.point_de_l_objet(2,0);
        a.pointage.supprime_points({{4,2}});
        a.retire_mesure_avant();
        QVERIFY(a.retablissement_possible());
        a.reprend_mesure(1);
        a.rafraichit();
        clic_image(a, {24,34});
        QCOMPARE(a.objet_courant, 2);
        QCOMPARE(a.pointage.point_de_l_objet(1,0), q1);
        a.change_mode(Mode::Simplifie);
        a.rafraichit();
        QTest::keyClick(&a, Qt::Key_Escape);
        attend();
        QVERIFY(!a.refait && a.retablissement_possible());
        a.change_mode(Mode::Expert);
        a.rafraichit();
        QCOMPARE(a.texte_python(), python);
        QCOMPARE(a.pointage.provenances(), sources);
        a.reprend_mesure(1);
        a.rafraichit();
        clic_image(a, {24,34});
        clic_image(a, {102,71});
        QCOMPARE(a.pointage.saisie(1,0), Pointage::Saisie::Manuelle);
        QCOMPARE(a.pointage.saisie(2,0), Pointage::Saisie::Manuelle);
        a.retire_mesure_avant();
        QCOMPARE(a.pointage.point_de_l_objet(1,0), q1);
        QCOMPARE(a.pointage.point_de_l_objet(2,0), q2);
        QCOMPARE(a.pointage.provenances(), sources);
        a.rafraichit();
        QVERIFY(clique(a, u"À vérifier"_s));
        QCOMPARE(a.index, 3);
        QCOMPARE(a.objet_courant, 1);
        a.annule_refait();
        a.rafraichit();
        QVERIFY(clique(a, u"À vérifier"_s));
        QCOMPARE(a.index, 4);
        QVERIFY(clique(a, u"Confirmer"_s));
        QCOMPARE(a.pointage.saisie(1,3), Pointage::Saisie::AbsenceConfirmee);
        QVERIFY(!a.pointage.point_de_l_objet(1,3));
        a.retire_mesure_avant();
        QCOMPARE(a.pointage.saisie(1,3), Pointage::Saisie::Perdue);
        a.retablit_point();
        const auto sources_validees = a.pointage.provenances();

        const QString projet = temporaire(u"deux-objets.mevcine"_s);
        a.ecrit_projet(projet);
        a.charge_projet(projet);
        QCOMPARE(a.pointage.provenances(), sources_validees);
        QCOMPARE(a.texte_python(), python);
        // Fond, mobile et règle suivent la même translation, pour chacun des deux référentiels.
        a.traj.mode = ModeTrajectoire::Chronophotographie;
        a.traj.chrono_img = 1;
        DessinTrajectoire dessin(a, nullptr);
        dessin.resize(640, 480);
        for (int ref : {1, 2}) {
            a.traj.referentiel = ref;
            const QImage vue = dessin.grab().toImage();
            const auto pixel = [&](QPointF pos) { return vue.pixelColor((pos * vue.devicePixelRatio()).toPoint()); };
            const QColor mobile = pixel({332, 252});
            QVERIFY(mobile.red() > 220);
            QVERIFY(ref == 1 ? mobile.green() > 220 : mobile.green() < 40);
            const Vecteur decalage = Vecteur(80,60) - *a.pointage.point_de_l_objet(ref, 0);
            const QColor etalon = pixel({(55 + decalage.x) * 4, (75 + decalage.y) * 4});
            QVERIFY(etalon.blue() > 225 && etalon.red() < 40);
        }

        a.graph.y = u"vx2"_s;
        a.onglet = Onglet::Grapheur;
        a.rafraichit();
        a.findChild<Graphe *>()->pointClique(1);
        auto *details = a.findChild<Graphe *>()->parentWidget()->findChild<QDialog *>();
        QVERIFY(details);
        QStringList explication;
        for (auto *label : details->findChildren<QLabel *>())
            explication << label->text();
        const QString lignes = explication.join(u'\n');
        QVERIFY(lignes.contains(u"objet 2 · intervalle de 0,2 s"));
        QVERIFY(lignes.contains(u"Image 1 · t = 0 s") && lignes.contains(u"Image 3 · t = 0,2 s"));
        details->close();

        const QString csv = temporaire(u"deux-objets.csv"_s);
        a.ecrit_csv(csv);
        QVERIFY(a.csv_a_jour() && a.projet_a_jour());
        verifie_python(a.texte_python(), u"t,x1,y1,x2,y2 = np.genfromtxt(%1, delimiter=',', skip_header=1, unpack=True)\n"
                       "assert len(t) == 7 and np.allclose(t, lst_t)\n"
                       "assert np.array_equal(np.isnan(x1), [False,False,True,True,False,False,True])\n"
                       "assert np.allclose(x1,lst_x1,equal_nan=True) and np.allclose(y1,lst_y1,equal_nan=True)\n"
                       "assert np.allclose(x2,lst_x2) and np.allclose(y2,lst_y2)"_s.arg(u"'"_s + QDir::fromNativeSeparators(csv) + u"'"_s));
    }

    void tableau_de_pointage_data()
    {
        QTest::addColumn<bool>("expert");
        QTest::newRow("guide") << false;
        QTest::newRow("expert") << true;
    }

    void tableau_de_pointage()
    {
        QFETCH(bool, expert);
        auto app = fenetre(outils::mire(16), expert ? Mode::Expert : Mode::Simplifie);
        App &a = *app;
        a.guide.etape = Etape::Pointage;
        a.nombre_objets_demandes = expert ? 2 : 1;
        a.ajuste_nombre_objets();
        a.etat = Etat::Pointage;
        // Un trou entre les deux images pointées distingue le numéro d'image du rang de la ligne.
        for (int i : {2, 4}) {
            a.pointage.memorise_point(1, {30.0 * i, 50}, i);
            if (expert)
                a.pointage.memorise_point(2, {30.0 * i, 220}, i);
        }
        a.change_image(6);
        a.rafraichit();
        attend();
        auto *t = tableau(a)->table();
        auto *z = zone(a);
        QCOMPARE(t->rowCount(), 2);
        QCOMPARE(t->columnCount(), expert ? 3 : 5);
        if (expert) {
            QCOMPARE(t->horizontalHeaderItem(0)->text(), u"n°"_s);
            QCOMPARE(t->item(0, 0)->text(), u"3"_s);
            QCOMPARE(t->item(1, 0)->text(), u"5"_s);
            QVERIFY(t->mapTo(&a, QPoint()).x() >= z->mapTo(&a, QPoint(z->width(), 0)).x());
        }
        souris(t->viewport(), QEvent::MouseMove, t->visualItemRect(t->item(0, expert ? 0 : 1)).center(),
               Qt::NoButton, Qt::NoButton);
        QTRY_VERIFY(couleur_au_point(z, {60, 50}, Qt::yellow));
        if (expert)
            QVERIFY(couleur_au_point(z, {60, 220}, Qt::yellow));
        QVERIFY(couleur_au_point(z, {120, 50}, dessin::couleur(1)));
        QVERIFY(!couleur_au_point(z, {120, 50}, Qt::yellow));
        QCOMPARE(a.index, 6);
        // Pendant la lecture de la nouvelle image, l'aperçu précédent reste affiché.
        souris(t->viewport(), QEvent::MouseMove, t->visualItemRect(t->item(1, expert ? 0 : 1)).center(),
               Qt::NoButton, Qt::NoButton);
        QVERIFY(couleur_au_point(z, {60, 50}, Qt::yellow));
        QTRY_VERIFY(couleur_au_point(z, {120, 50}, Qt::yellow));
        QTest::mouseMove(z, QPoint(4, 4));
        QEvent sortie(QEvent::Leave);
        QApplication::sendEvent(t->viewport(), &sortie);
        QVERIFY(!couleur_au_point(z, {120, 50}, Qt::yellow));

        // Les flèches parcourent le tableau en laissant la tête de lecture à l'image 6.
        t->setFocus();
        t->setCurrentCell(0, 0);
        QTest::keyClick(t, Qt::Key_Down);
        QTRY_COMPARE(z->accessibleDescription(), u"Aperçu · image 5"_s);
        QTest::keyClick(t, Qt::Key_Up);
        QTRY_COMPARE(z->accessibleDescription(), u"Aperçu · image 3"_s);
        if (!expert) {
            QTest::keyClick(t, Qt::Key_Right);
            QTRY_COMPARE(z->accessibleDescription(), u"Aperçu · image 3 · objet 1"_s);
        }
        QCOMPARE(a.index, 6);

        bouton_reprise(a, 0)->click();
        QCOMPARE(a.index, 3);
        clic_image(a, {66, 52});
        if (expert)
            clic_image(a, {65, 222});
        QCOMPARE(a.index, 6);
        QCOMPARE(a.onglet, Onglet::Pointage);
        auto *poubelle = qobject_cast<QToolButton *>(t->cellWidget(0, t->columnCount() - 1));
        QVERIFY(poubelle && !poubelle->icon().isNull());
        if (expert)
            QCOMPARE(poubelle->toolTip(), u"Supprimer les 2 objets de l'image 3"_s);
        poubelle->click();
        QCOMPARE(t->rowCount(), 1);
        QVERIFY(!a.pointage.point_de_l_objet(1, 2));
        if (expert) {
            QVERIFY(!a.pointage.point_de_l_objet(2, 2));
            QCOMPARE(t->item(0, 0)->text(), u"5"_s);
        }
        QVERIFY(a.pointage.point_de_l_objet(1, 4));
        if (expert)
            QVERIFY(clique(a, u"Annuler"_s));
        else
            QTest::keyClick(&a, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(t->rowCount(), 2);
        for (int obj = 1; obj <= a.nombre_objets_demandes; ++obj)
            QVERIFY(a.pointage.point_de_l_objet(obj, 2));
    }

    void supprimer_et_survoler_une_plage()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        App &a = *app;
        a.nombre_objets_demandes = 2;
        a.ajuste_nombre_objets();
        a.etat = Etat::Pointage;
        for (int i = 0; i < 4; ++i) {
            a.pointage.memorise_point(1, {30.0 + 40 * i, 50}, i);
            a.pointage.memorise_point(2, {60.0 + 40 * i, 180}, i);
        }
        a.onglet = Onglet::Coordonnees;
        a.rafraichit();
        auto *vue = tableau(a);
        auto *t = vue->table();
        const int depart = a.index;
        QTest::mouseMove(t->viewport(), t->visualItemRect(t->item(1, 3)).center());
        auto *apercu = vue->findChild<ZoneVideo *>(u"apercuMesure"_s);
        QVERIFY(apercu);
        QTRY_VERIFY(apercu->isVisible());
        QVERIFY(couleur_au_point(apercu, {100, 180}, Qt::yellow));
        QVERIFY(!couleur_au_point(apercu, {70, 50}, Qt::yellow));
        QVERIFY(couleur_au_point(apercu, {30, 50}, dessin::couleur(1)));
        QCOMPARE(a.index, depart);
        t->setRangeSelected(QTableWidgetSelectionRange(1, 3, 2, 4), true);
        t->setFocus();
        QTest::keyClick(t, Qt::Key_Delete);
        QVERIFY(!a.pointage.point_de_l_objet(2, 1));
        QVERIFY(!a.pointage.point_de_l_objet(2, 2));
        QVERIFY(a.pointage.point_de_l_objet(1, 1));
        QTest::keyClick(t, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(a.pointage.point_de_l_objet(2, 1));
        QVERIFY(a.pointage.point_de_l_objet(2, 2));
    }

    void affichage_et_actions_du_tableau_data()
    {
        QTest::addColumn<Mode>("mode");
        QTest::addColumn<Onglet>("onglet");
        QTest::newRow("guide") << Mode::Simplifie << Onglet::Pointage;
        QTest::newRow("pointage-expert") << Mode::Expert << Onglet::Pointage;
        QTest::newRow("coordonnees") << Mode::Expert << Onglet::Coordonnees;
    }

    void affichage_et_actions_du_tableau()
    {
        QFETCH(Mode, mode);
        QFETCH(Onglet, onglet);
        auto app = fenetre(outils::mire(28), mode);
        App &a = *app;
        theme::choisit(Qt::ColorScheme::Dark);
        a.etat = Etat::Pointage;
        a.guide.etape = Etape::Pointage;
        a.onglet = onglet;
        for (int i = 0; i < 26; ++i)
            a.pointage.memorise_point(1, {20.125 + i * 2.3, 101.123 + i}, i, Pointage::Saisie::Automatique);
        a.rafraichit();
        a.change_agrandissement(1.5);
        attend();
        auto *t = tableau(a)->table();
        t->scrollToTop();
        const int rec = t->columnCount() - 2, sup = rec + 1;
        QVERIFY(t->verticalScrollBar()->isVisible());
        QVERIFY(t->horizontalHeader()->sectionViewportPosition(sup) + t->columnWidth(sup) <= t->viewport()->width());
        QVERIFY(!t->horizontalScrollBar()->isVisible());
        for (int i = 0; i < t->rowCount(); ++i)
            for (int j = 0; j < rec; ++j)
                QVERIFY2(t->columnWidth(j) >= t->sizeHintForIndex(t->model()->index(i, j)).width(),
                         qPrintable(u"ligne %1 colonne %2 : largeur %3, contenu %4"_s.arg(i).arg(j).arg(t->columnWidth(j))
                                        .arg(t->sizeHintForIndex(t->model()->index(i, j)).width())));

        const auto donnee = t->model()->index(0, rec - 1);
        t->setCurrentIndex(donnee);
        QTest::keyClick(t, Qt::Key_Down);
        t->setCurrentCell(1, sup, QItemSelectionModel::SelectCurrent);
        auto *poubelle = t->cellWidget(2, sup);
        QTest::mouseMove(poubelle, poubelle->rect().center());
        auto *z = onglet == Onglet::Coordonnees ? tableau(a)->findChild<ZoneVideo *>() : zone(a);
        QTRY_COMPARE(z->accessibleDescription(), u"Aperçu · image 3"_s);
        QVERIFY(!t->selectionModel()->isSelected(t->model()->index(1, sup)));
        QVERIFY(!t->model()->index(1, rec).flags().testFlag(Qt::ItemIsSelectable));
        t->selectionModel()->select(donnee, QItemSelectionModel::ClearAndSelect);
        QCOMPARE(t->selectionModel()->selectedIndexes(), QModelIndexList{donnee});
        QTest::mouseMove(t->cellWidget(3, rec));
        QTRY_COMPARE(z->accessibleDescription(), u"Aperçu · image 4"_s);
        QCOMPARE(t->selectionModel()->selectedIndexes(), QModelIndexList{donnee});
        QTest::mouseMove(t->verticalScrollBar(), t->verticalScrollBar()->rect().center());
        QTRY_VERIFY(z->accessibleDescription().isEmpty());
        QCOMPARE(t->contextMenuPolicy(), Qt::PreventContextMenu);
        QTest::mouseClick(poubelle, Qt::RightButton);
        QCOMPARE(t->rowCount(), 26);
        QTest::mouseClick(poubelle, Qt::LeftButton);
        QCOMPARE(t->rowCount(), 25);
        QVERIFY(!a.pointage.point_de_l_objet(1, 2));
        theme::choisit(Qt::ColorScheme::Light);
    }

    void origine_axes_rotation_et_deux_objets()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        App &a = *app;
        QCOMPARE(a.dimensions(), QSize(400, 300));
        QVERIFY(a.pointage.origine == Vecteur(200, 150));

        // rotation d'un quart de tour : l'origine revient au centre
        QVERIFY(clique(a, u"Tourner la vidéo d'un quart de tour vers la droite"_s));
        QCOMPARE(a.dimensions(), QSize(300, 400));
        QVERIFY(a.pointage.origine == Vecteur(150, 200));
        QCOMPARE(a.image.size(), QSize(300, 400));
        QVERIFY(clique(a, u"Tourner la vidéo d'un quart de tour vers la gauche"_s));
        QCOMPARE(a.dimensions(), QSize(400, 300));

        // nouvelle origine, choisie d'un clic sur l'image ; un second appui sur le bouton y renonce
        QVERIFY(clique(a, u"Placer l'origine"_s));
        QVERIFY(a.choix_origine);
        rendu(a, u"51_expert_placer_origine"_s);
        QVERIFY(clique(a, u"Placer l'origine"_s));
        QVERIFY(!a.choix_origine);
        QVERIFY(clique(a, u"Placer l'origine"_s));
        clic_image(a, {100, 250});
        QVERIFY(!a.choix_origine);
        QVERIFY((a.pointage.origine - Vecteur(100, 250)).module() < 0.5);

        // ordonnées vers le bas
        QVERIFY(clique(a, u"x vers la droite, y vers le bas"_s));
        QCOMPARE(a.pointage.sens_y, -1);
        QCOMPARE(a.pointage.sens_x, 1);

        // deux objets : la série reste ouverte entre les deux clics
        a.nombre_objets_demandes = 2;
        a.ajuste_nombre_objets();
        a.rafraichit();
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::Pointage);
        clic_image(a, {150, 200});
        QCOMPARE(a.etat, Etat::SerieEnCours);
        QCOMPARE(a.objet_courant, 2);
        QCOMPARE(a.index, 1);
        clic_image(a, {300, 100});
        QCOMPARE(a.etat, Etat::Pointage);
        QCOMPARE(a.index, 2);
        const Vecteur q1 = *a.pointage.point_de_l_objet(1, 0), q2 = *a.pointage.point_de_l_objet(2, 0);
        // sans échelle, 1 px = 1 m : ordonnées vers le bas, donc négatives au-dessus de l'origine
        const Vecteur m1 = a.pointage.position_dans_repere(q1);
        QVERIFY2(std::abs(m1.x - 50.0) < 0.5 && std::abs(m1.y + 50.0) < 0.5, qUtf8Printable(m1.texte()));
        QVERIFY((q2 - Vecteur(300, 100)).module() < 0.5);
        // les exports nomment les objets : lst_x1, lst_x2...
        const QString texte = a.texte_python();
        QVERIFY2(texte.contains(u"lst_x1 = "_s) && texte.contains(u"lst_y2 = "_s), qUtf8Printable(texte));
        QVERIFY(texte.contains(u"en pixels"_s));
        rendu(a, u"50_deux_objets"_s);

        const int image = a.index;
        const auto etalon = a.pointage.echelle;
        QVERIFY(menu(a, u"Mode simplifié (pas à pas)"_s));
        QCOMPARE(a.mode, Mode::Simplifie);
        QVERIFY(a.dialogues.empty());
        QCOMPARE(a.nombre_objets_demandes, 2);
        QCOMPARE(a.texte_python(), texte);
        QVERIFY(menu(a, u"Mode expert (toutes les fonctions)"_s));
        QCOMPARE(a.mode, Mode::Expert);
        QCOMPARE(a.index, image);
        QCOMPARE(a.pointage.echelle.p1, etalon.p1);
        QCOMPARE(a.pointage.echelle.p2, etalon.p2);
        QCOMPARE(a.pointage.point_de_l_objet(1, 0), Position(q1));
        QCOMPARE(a.pointage.point_de_l_objet(2, 0), Position(q2));
        QCOMPARE(a.pointage.sens_y, -1);
        QCOMPARE(a.texte_python(), texte);

        // tout recommencer : on confirme d'abord, car des mesures seraient perdues
        QVERIFY(clique(a, u"Tout recommencer"_s));
        QVERIFY(affiche(a, u"Tout recommencer ?"_s));
        QVERIFY(a.pointage.a_des_pointages());
        QVERIFY(clique(a, u"Tout effacer"_s));
        QCOMPARE(a.etat, Etat::Preparation);
        QVERIFY(!a.pointage.a_des_pointages());
        QCOMPARE(a.pointage.sens_y, 1);
        a.change_mode(Mode::Simplifie);
        a.rafraichit();
        QCOMPARE(a.mode, Mode::Simplifie);
        QCOMPARE(a.pointage.nombre_objets(), 2);
    }

    /// Le référentiel mobile conserve les intervalles réels entre les images disponibles.
    void ralenti_cadre()
    {
        Pointage p;
        p.alloue_mesures(1, 0.04, 3);
        p.instants = std::vector<double>{0, 1, 3};
        p.memorise_point(1, {100, 50}, 0);
        p.memorise_point(1, {700, 150}, 2);
        const auto r = Ralenti::nouveau(p, 1, QSize(800, 600));
        QVERIFY(r.has_value());
        QCOMPARE(r->images.size(), size_t(2));
        QCOMPARE(r->images.front().index, 0);
        QCOMPARE(r->images.front().duree, 0.12);
        QCOMPARE(r->images.back().duree, 0.04);
        // le cadre garde l'objet à la même place pendant tout son déplacement
        QCOMPARE(r->taille, QSize(199, 499));
        QCOMPARE(r->decalage, Vecteur(100, 50));
    }

    void ralenti_et_a_propos()
    {
        const auto ralenti = outils::video_test(
            u"ralenti.mp4"_s, {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc2=s=320x240:r=30:d=1"_s, u"-c:v"_s, u"libx264"_s,
                               u"-metadata"_s, u"com.android.capture.fps=240.000000"_s, u"-movflags"_s, u"use_metadata_tags"_s});
        if (!ralenti)
            QSKIP("libx264 absent");
        auto app = fenetre(*ralenti, Mode::Expert);
        App &a = *app;
        QCOMPARE(a.etat, Etat::Preparation);
        // les calculs utilisent la cadence de prise de vue
        QCOMPARE(a.cadence_prise_de_vue, 240.0);
        QCOMPARE(a.saisie_cadence, u"240"_s);
        QVERIFY(std::abs(a.pointage.delta_t - 1.0 / 240.0) < 1e-12);
        QVERIFY(affiche(a, u"Vidéo au ralenti"_s));
        rendu(a, u"60_ralenti"_s);
        QVERIFY(clique(a, u"OK"_s));
        QVERIFY(bouton(a, u"Démarrer le pointage"_s));
        QStringList onglets;
        for (int i = 0; i < a.m_expert->count(); ++i)
            onglets << a.m_expert->tabText(i);
        QCOMPARE(onglets, QStringList({u"Pointage"_s, u"Coordonnées"_s, u"Trajectoires"_s, u"Grapheur"_s}));
        a.ouvre_a_propos();
        attend();
        auto *propos = a.findChild<FenetreAPropos *>();
        QVERIFY(propos && propos->isVisible());
        QStringList textes;
        QLabel *nom = nullptr, *courriel = nullptr, *site = nullptr;
        for (QLabel *l : propos->findChildren<QLabel *>()) {
            const QString texte = l->text();
            textes << texte;
            if (texte == u"Rémi MEVAERE")
                nom = l;
            else if (texte.contains(u"mailto:suite@sciences-physiques.net"))
                courriel = l;
            else if (texte.contains(u"href=\"https://sciences-physiques.net/\""))
                site = l;
        }
        QVERIFY(nom && courriel && site);
        QVERIFY(nom->alignment().testFlag(Qt::AlignLeft));
        for (QLabel *lien : {courriel, site}) {
            QVERIFY(lien->alignment().testFlag(Qt::AlignLeft));
            QCOMPARE(lien->mapTo(propos, QPoint()).x(), nom->mapTo(propos, QPoint()).x());
            QVERIFY(lien->height() >= lien->heightForWidth(lien->width()));
        }
        QVERIFY(site->mapTo(propos, QPoint()).y() > courriel->mapTo(propos, QPoint()).y());
        const QString tout = textes.join(u'\n');
        QVERIFY(textes.contains(u"version %1"_s.arg(a_propos::VERSION)));
        // les logiciels libres utilisés, avec leur licence, puis le fichier de réglages (comme MeVSono)
        QVERIFY(textes.contains(u"Logiciels libres utilisés"_s));
        QVERIFY(tout.contains(QDir::toNativeSeparators(prefs::fichier_conf()).toHtmlEscaped()));
        for (const QString &ressource : {u"Qt "_s, u"FFmpeg"_s, u"GCC"_s, u"mingw-w64"_s, u"LGPL-3.0-only"_s})
            QVERIFY2(tout.contains(ressource), qUtf8Printable(ressource));
        QVERIFY(tout.contains(u"NOTICE"_s));
        QVERIFY(tout.contains(u"github.com/melectronvolt/MevSuite"_s));
        QVERIFY(tout.contains(u"GPL-3.0-or-later"_s));
        verifie_icones(*propos);
        propos->grab().save(QDir(QString::fromUtf8(MEVCINE_RENDUS_TESTS)).filePath(u"61_a_propos.png"_s));
        QVERIFY(clique(a, u"Fermer"_s));
        QVERIFY(!a.findChild<FenetreAPropos *>());
    }

    /// vidéo à cadence variable : l'interface le signale, et les dates des images sont les dates
    /// réelles
    void cadence_variable()
    {
        const auto f = outils::video_test(
            u"cadence-variable-4.mp4"_s,
            {u"-filter_complex"_s,
             u"color=c=gray:s=320x240:r=30:d=4,settb=1/90000,"
             "setpts='if(lt(N,80),N/30,80/30+(N-80)/24)/TB',select='not(eq(n,20))'[fond];"
             "color=c=red:s=16x16[carre];[fond][carre]overlay=x='20+60*t':y=100:eval=frame:shortest=1:format=yuv444[v]"_s,
             u"-map"_s, u"[v]"_s, u"-fps_mode"_s, u"passthrough"_s, u"-video_track_timescale"_s, u"90000"_s, u"-c:v"_s,
             u"libx264"_s, u"-qp"_s, u"0"_s, u"-pix_fmt"_s, u"yuv444p"_s});
        if (!f)
            QSKIP("libx264 absent");
        auto app = fenetre(*f, Mode::Expert);
        App &a = *app;
        QVERIFY(a.pointage.cadence_variable());
        QCOMPARE(a.nombre_images_video, 119);
        // l'image 21 suit une image perdue
        QVERIFY(std::abs(a.pointage.date(20) - 21.0 / 30.0) < 1e-3);
        QVERIFY(affiche(a, u"Cadence variable"_s));
        QVERIFY(std::abs(a.pointage.date(118) - 4.30) < 0.001);
        QVERIFY(affiche(a, u"320 × 240 px · 119 images · 4,33 s"_s));
        rendu(a, u"63_cadence_variable"_s);
    }

    /// Une image pointée à moitié compte comme travail à protéger. Une sauvegarde de secours
    /// impossible est signalée une seule fois, puis reprend lorsque le dossier redevient accessible.
    void ligne_incomplete_et_secours()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        App &a = *app;
        a.nombre_objets_demandes = 2;
        a.ajuste_nombre_objets();
        a.rafraichit();
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        clic_image(a, {100, 150});
        clic_image(a, {200, 150});
        QCOMPARE(a.pointage.nb_images_pointees(), 1);
        a.note_travail_sauve();
        QVERIFY(a.travail_en_securite());
        // l'objet 1 seul sur l'image suivante : en danger
        clic_image(a, {110, 150});
        QVERIFY(a.pointage.point_de_l_objet(1, a.index).has_value() || a.pointage.point_de_l_objet(1, a.index - 1).has_value());
        QVERIFY(!a.travail_en_securite());
        // secours impossible (un dossier à la place du fichier) : un avertissement
        const QString secours = a.fichier_secours();
        QFile::remove(secours);
        QVERIFY(QDir().mkpath(secours));
        clic_image(a, {210, 150}); // un pointage de plus : le secours a changé
        a.sauvegarde_de_secours();
        attend();
        QVERIFY(affiche(a, u"Sauvegarde automatique impossible"_s));
        QVERIFY(clique(a, u"OK"_s) || clique(a, u"Fermer"_s));
        attend();
        // pas une seconde fois
        clic_image(a, {120, 150});
        a.sauvegarde_de_secours();
        attend();
        QVERIFY(!affiche(a, u"Sauvegarde automatique impossible"_s));
        QVERIFY(QDir(secours).removeRecursively());
        a.sauvegarde_de_secours();
        QVERIFY(QFileInfo(secours).isFile());
    }

    /// Les exports, la sauvegarde du projet et le secours ont des rôles distincts.
    void rien_ne_se_perd_sans_prevenir()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        App &a = *app;
        // sans mesure, aucune question
        QVERIFY(a.travail_en_securite());
        QVERIFY(clique(a, u"Tout recommencer"_s));
        QVERIFY(!affiche(a, u"Tout recommencer ?"_s));
        const QString secours = a.fichier_secours();
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        for (int i = 0; i < 3; ++i)
            clic_image(a, {100.0 + 10.0 * i, 150.0});
        QVERIFY(!a.travail_en_securite());
        // la sauvegarde automatique est faite dès qu'il y a des mesures
        QVERIFY2(QFileInfo(secours).isFile(), "sauvegarde de secours absente");
        const QString texte = lit_texte(secours);
        QVERIFY2(texte.startsWith(u"# version = mevcine"_s), qUtf8Printable(texte));
        QVERIFY(texte.contains(u"mire-16.avi"_s));
        // quitter : la question, puis on annule
        a.close();
        attend();
        QVERIFY(a.isVisible());
        QVERIFY(affiche(a, u"Quitter MeVCine ?"_s));
        rendu(a, u"70_confirmation_quitter"_s);
        // la touche Entrée ne confirme pas
        QTest::keyClick(a.dialogue_affiche(), Qt::Key_Return);
        attend();
        QVERIFY(!a.fermeture_confirmee);
        QVERIFY(a.isVisible());
        QVERIFY(a.dialogues.empty());
        // tout recommencer, ouvrir un autre fichier : la question, puis on annule
        QVERIFY(clique(a, u"Tout recommencer"_s));
        QVERIFY(affiche(a, u"Tout recommencer ?"_s));
        QVERIFY(clique(a, u"Annuler"_s));
        QCOMPARE(a.pointage.nb_images_pointees(), 3);
        a.confirme_si_mesures({Apres::Type::OuvrirFichier, outils::mire(28)});
        attend();
        QVERIFY(affiche(a, u"Abandonner les mesures ?"_s));
        QVERIFY(clique(a, u"Annuler"_s));
        QCOMPARE(a.pointage.nb_images_pointees(), 3);
        QCOMPARE(a.nombre_images_video, 16); // la vidéo n'a pas changé
        // La copie pour Python fournit les mesures, mais le projet reste modifié.
        onglet(a, u"Coordonnées"_s);
        QVERIFY(clique(a, u"Copier pour Python"_s));
        QVERIFY(a.copie_a_jour());
        QVERIFY(!a.travail_en_securite());
        QGuiApplication::clipboard()->setText(u"autre texte"_s);
        attend();
        QVERIFY(!a.copie_a_jour());
        QVERIFY(!affiche(a, u"Mesures dans le presse-papiers"_s));
        a.ecrit_projet(temporaire(u"enregistre.mevcine"_s));
        QVERIFY(a.projet_a_jour());
        // une mesure de plus : de nouveau en danger, et la sauvegarde suit
        onglet(a, u"Pointage"_s);
        clic_image(a, {140, 150});
        QVERIFY(!a.travail_en_securite());
        a.sauvegarde_de_secours();
        QVERIFY2(lit_texte(secours) != texte, "la sauvegarde doit avoir suivi");
        // la récupération, dans une nouvelle session
        auto app2 = fenetre(secours, Mode::Expert, std::nullopt, true);
        QCOMPARE(app2->pointage.nb_images_pointees(), 4);
        QCOMPARE(app2->etat, Etat::Pointage);
        QVERIFY(!app2->projet_a_jour());
        app2->ecrit_projet(temporaire(u"recupere.mevcine"_s));
        QVERIFY(app2->travail_en_securite());
        auto *recuperer = action_menu(*app2, u"Récupérer le travail précédent…"_s);
        QVERIFY(recuperer && recuperer->isEnabled());
        a.ecrit_projet(temporaire(u"fin.mevcine"_s));
        a.close();
        attend();
        QVERIFY(!a.isVisible());
        QVERIFY(a.dialogues.empty());
    }

    void enregistrer_entre_deux_objets_data()
    {
        QTest::addColumn<int>("image_en_cours");
        QTest::addColumn<bool>("retour_arriere");
        QTest::newRow("image suivante") << 2 << false;
        QTest::newRow("fin du film") << 16 << false;
        QTest::newRow("retour sur une image anterieure") << 2 << true;
    }

    void enregistrer_entre_deux_objets()
    {
        QFETCH(int, image_en_cours);
        QFETCH(bool, retour_arriere);
        auto app = fenetre(outils::mire(16), Mode::Expert);
        app->nombre_objets_demandes = 2;
        app->ajuste_nombre_objets();
        app->rafraichit();
        QVERIFY(clique(*app, u"Démarrer le pointage"_s));
        // La plage de pointage accepte l'image voisine de la première ou de la dernière
        // mesure. Commencer à côté de l'image voulue reproduit un déplacement autorisé.
        const int premiere = retour_arriere ? image_en_cours + 1 : image_en_cours - 1;
        app->change_image(premiere);
        app->rafraichit();
        clic_image(*app, {50, 100});
        clic_image(*app, {100, 150});
        app->change_image(image_en_cours);
        app->rafraichit();
        clic_image(*app, {55, 102});
        const int index = image_en_cours - 1;
        const auto premier = app->pointage.point_de_l_objet(1, index);
        QVERIFY(premier);
        auto *enregistrer = action_menu(*app, u"Enregistrer le projet…"_s);
        QVERIFY(enregistrer && enregistrer->isEnabled());
        QCOMPARE(enregistrer->shortcut(), QKeySequence(QKeySequence::Save));
        QVERIFY(app->sauvegarde_de_secours());
        const QString secours = app->fichier_secours();
        const QString projet = temporaire(u"deux-objets.mevcine"_s);
        app->ecrit_projet(projet);
        QVERIFY(app->projet_a_jour());
        // Le projet et le secours doivent demander exactement le même clic.
        for (const auto &source : {projet, secours}) {
            auto reprise = fenetre(source, Mode::Expert, {}, true);
            QCOMPARE(reprise->etat, Etat::SerieEnCours);
            QCOMPARE(reprise->index, image_en_cours);
            QCOMPARE(reprise->objet_courant, 2);
            QCOMPARE(reprise->pointage.point_de_l_objet(1, index), premier);
            reprise->retire_mesure_avant();
            QVERIFY(!reprise->pointage.point_de_l_objet(1, index));
            if (retour_arriere)
                QVERIFY(reprise->pointage.point_de_l_objet(2, premiere - 1));
            reprise->retablit_point();
            QCOMPARE(reprise->index, image_en_cours);
            QCOMPARE(reprise->objet_courant, 2);
            reprise->rafraichit();
            clic_image(*reprise, {105, 152});
            QCOMPARE(reprise->pointage.point_de_l_objet(1, index), premier);
            QVERIFY(reprise->pointage.point_de_l_objet(2, index));
            QCOMPARE(reprise->index, std::min(image_en_cours + 1, 16));
            QCOMPARE(reprise->objet_courant, 1);
        }
    }

    void recuperer_deux_seances_du_meme_film()
    {
        const QString film = outils::mire(16);
        auto matin = fenetre(film, Mode::Expert);
        auto soir = fenetre(film, Mode::Expert, {}, true);
        QVERIFY(clique(*matin, u"Démarrer le pointage"_s));
        clic_image(*matin, {70, 80});
        QVERIFY(matin->sauvegarde_de_secours());
        const QString copie_matin = matin->fichier_secours();
        const QString texte_matin = lit_texte(copie_matin);
        const QString projet_partage = temporaire(u"chariots.mevcine"_s);
        matin->ecrit_projet(projet_partage);
        QVERIFY(clique(*soir, u"Démarrer le pointage"_s));
        clic_image(*soir, {120, 80});
        clic_image(*soir, {130, 80});
        QVERIFY(soir->sauvegarde_de_secours());
        const QString copie_soir = soir->fichier_secours();
        QVERIFY(copie_matin != copie_soir);
        QCOMPARE(lit_texte(copie_matin), texte_matin);
        QCOMPARE(SecoursSeance::disponibles().size(), 2);
        // La boîte de récupération permet de choisir la séance du matin, toujours ouverte.
        auto choix = fenetre({}, Mode::Expert, {}, true);
        const bool dialogues_qt = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto retablit = qScopeGuard([=] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, dialogues_qt); });
        QTimer::singleShot(0, choix.get(), [&] {
            if (auto *dialogue = choix->findChild<QFileDialog *>()) {
                QTimer::singleShot(5000, dialogue, &QDialog::reject);
                dialogue->selectFile(copie_matin);
                QMetaObject::invokeMethod(dialogue, "accept");
            }
        });
        QVERIFY(menu(*choix, u"Récupérer le travail précédent…"_s));
        QCOMPARE(choix->pointage.nb_images_pointees(), 1);
        QVERIFY(choix->fichier_secours() != copie_matin);
        clic_image(*choix, {80, 90});
        choix->ecrit_projet(temporaire(u"copie-du-matin.mevcine"_s));
        QVERIFY(choix->close());
        QCOMPARE(lit_texte(copie_matin), texte_matin);
        // L'ancien nom reste lisible. Une séance récupérée conserve l'original historique.
        const QString ancien = QDir(prefs::dossier_conf()).filePath(u"mevcine-secours.mevcine"_s);
        QVERIFY(QFile::copy(copie_matin, ancien));
        auto historique = fenetre(ancien, Mode::Expert, {}, true);
        QVERIFY(!historique->projet_a_jour());
        QVERIFY(historique->fichier_secours() != ancien);
        QCOMPARE(historique->pointage.nb_images_pointees(), 1);
        // La fermeture sans enregistrement doit prendre le dernier clic avant la minuterie.
        clic_image(*soir, {140, 80});
        soir->close();
        attend();
        QVERIFY(clique(*soir, u"Quitter sans enregistrer"_s));
        QVERIFY(!soir->isVisible());
        auto reprise = fenetre(copie_soir, Mode::Expert, {}, true);
        QCOMPARE(reprise->fichier_secours(), copie_soir);
        QCOMPARE(reprise->pointage.nb_images_pointees(), 3);
        // Les deux fenêtres enregistrent volontairement au même emplacement. La première
        // doit conserver son secours lorsque le fichier nommé appartient désormais à l'autre.
        reprise->ecrit_projet(projet_partage);
        QVERIFY(reprise->close());
        QVERIFY(!QFileInfo::exists(copie_soir));
        QVERIFY(matin->close());
        QCOMPARE(lit_texte(copie_matin), texte_matin);
        QCOMPARE(lit_texte(ancien), texte_matin);
    }

    void rotation_de_projet_invalide()
    {
        auto app = fenetre(outils::mire(16), Mode::Expert);
        QVERIFY(clique(*app, u"Démarrer le pointage"_s));
        clic_image(*app, {50, 100});
        const auto position = app->pointage.point_de_l_objet(1, 0);
        const QString projet = temporaire(u"rotation.mevcine"_s);
        app->ecrit_projet(projet);
        QString contenu = lit_texte(projet);
        QVERIFY(contenu.contains(u"# rotation = 0\n"_s));
        ecrit_texte(projet, contenu.replace(u"# rotation = 0\n"_s, u"# rotation = 45\n"_s));
        app->charge_projet(projet);
        attend();
        QVERIFY(affiche(*app, u"Projet illisible"_s));
        QCOMPARE(app->video->rotation(), 0);
        QCOMPARE(app->pointage.point_de_l_objet(1, 0), position);
        QCOMPARE(app->index, 2);
    }

    /// L'écran de démarrage : MeVCine et son auteur, puis il s'efface tout seul (ou au premier clic),
    /// et l'accueil apparaît
    void ecran_de_demarrage()
    {
        configuration(std::nullopt);
        auto app = std::make_unique<App>(QString(), std::nullopt);
        app->resize(1280, 800);
        app->show();
        QVERIFY(QTest::qWaitForWindowExposed(app.get()));
        QVERIFY(app->ecran_demarrage() && app->ecran_demarrage()->isVisible());
        QTest::qWait(100);
        app->grab().save(QDir(QString::fromUtf8(MEVCINE_RENDUS_TESTS)).filePath(u"02_demarrage.png"_s));
        // un clic sur l'accueil, caché par l'écran de démarrage, fait seulement disparaître l'écran
        QTest::mouseClick(app->m_sans_video, Qt::LeftButton, {}, QPoint(10, 10));
        attend();
        QVERIFY(!app->ecran_demarrage());
        // sans clic, il disparaît en moins de trois secondes
        auto app2 = std::make_unique<App>(QString(), std::nullopt);
        app2->show();
        QVERIFY(app2->ecran_demarrage());
        QTest::qWait(1500);
        QVERIFY(app2->ecran_demarrage());
        QVERIFY(QTest::qWaitFor([&] { return !app2->ecran_demarrage(); }, 2500));
    }

    void coche_distincte_du_pictogramme()
    {
        theme::choisit(Qt::ColorScheme::Dark);
        auto app = fenetre({}, Mode::Expert);
        QMenu *menu = app->m_menu_mode;
        QAction *expert = app->m_mode_expert;
        expert->setShortcut(QKeySequence(u"Ctrl+E"_s));
        menu->popup(app->menuBar()->mapToGlobal(QPoint(0, app->menuBar()->height())));
        menu->setActiveAction(expert);
        attend();
        const QRect ligne = menu->actionGeometry(expert);
        const QImage cochee = menu->grab(ligne).toImage();
        app->m_mode_simplifie->setChecked(true);
        QVERIFY(!expert->isChecked());
        const QImage libre = menu->grab(ligne).toImage();
        QCOMPARE(cochee.size(), libre.size());
        QVERIFY(cochee != libre);
        // Le pictogramme et le libellé restent lisibles à gauche, le raccourci à droite.
        const auto echelle = libre.devicePixelRatio();
        const int icone = menu->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, menu);
        const int gauche = qRound((icone + menu->fontMetrics().horizontalAdvance(expert->text())) * echelle);
        const int droite = qRound(menu->fontMetrics().horizontalAdvance(expert->shortcut().toString(QKeySequence::NativeText)) * echelle);
        QCOMPARE(cochee.copy(0, 0, gauche, libre.height()), libre.copy(0, 0, gauche, libre.height()));
        const QRect clavier(libre.width() - droite, 0, droite, libre.height());
        QCOMPARE(cochee.copy(clavier), libre.copy(clavier));
    }

    /// Le bouton du drapeau change la langue de l'interface aussitôt, et la retient dans la
    /// configuration ; le bouton soleil ou lune change le thème, retenu lui aussi
    void boutons_langue_et_theme()
    {
        theme::choisit(Qt::ColorScheme::Light);
        auto app = fenetre(outils::mire(28), Mode::Expert);
        App &a = *app;
        const QString conf = prefs::fichier_conf();
        auto *drapeau = a.findChild<QToolButton *>(u"langue"_s);
        QVERIFY(drapeau && drapeau->isVisible());
        auto *soleil = a.findChild<QToolButton *>(u"theme"_s);
        QVERIFY(soleil && soleil->isVisible());
        // pas de barre d'outils : tout est dans les menus ; à droite de la barre des menus, le thème
        // puis la langue (icônes seules) ; la notice est dans le menu Aide (F1)
        QVERIFY(a.findChildren<QToolBar *>().isEmpty());
        QWidget *coin = a.menuBar()->cornerWidget(Qt::TopRightCorner);
        QVERIFY(coin && coin->isVisible());
        QCOMPARE(coin->findChildren<QToolButton *>().size(), 2);
        for (QToolButton *b : {soleil, drapeau}) {
            QCOMPARE(b->parentWidget(), coin);
            QCOMPARE(b->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(b->autoRaise());
            QCOMPARE(b->focusPolicy(), Qt::NoFocus);
        }
        QVERIFY(soleil->geometry().right() < drapeau->geometry().left());
        QVERIFY(coin->mapTo(a.menuBar(), drapeau->geometry().topRight()).x() >= a.menuBar()->width() - 12);
        // la barre des menus : ses titres 10 % plus hauts que ceux du style du poste, et les boutons du
        // coin ne l'agrandissent pas
        auto *style = qobject_cast<QProxyStyle *>(QApplication::style());
        QVERIFY(style);
        QStyleOptionMenuItem option;
        option.initFrom(a.menuBar());
        const QSize titre_base = style->baseStyle()->sizeFromContents(QStyle::CT_MenuBarItem, &option, QSize(40, 18), a.menuBar());
        const QSize titre_suite = style->sizeFromContents(QStyle::CT_MenuBarItem, &option, QSize(40, 18), a.menuBar());
        QCOMPARE(titre_suite.height(), qRound(titre_base.height() * 1.1));
        QVERIFY(titre_suite.width() > titre_base.width());
        QMenuBar nue;
        nue.setNativeMenuBar(false);
        nue.addMenu(u"Fichier"_s);
        QCOMPARE(a.menuBar()->height(), nue.sizeHint().height());
        QCOMPARE(soleil->toolTip(), u"Thème clair. Cliquer pour passer au thème sombre."_s);
        QCOMPARE(drapeau->toolTip(), u"Langue : français. Cliquer pour passer en anglais (English)."_s);
        // les menus : pas de menu Langue ni Thème, pas d'émoji dans leurs titres, police de l'interface
        QStringList menus_fr, aide;
        for (QAction *m : a.menuBar()->actions()) {
            menus_fr << m->text();
            for (QAction *commande : m->menu()->actions()) {
                if (!commande->isSeparator()) {
                    QVERIFY2(commande->isIconVisibleInMenu(), qUtf8Printable(commande->text()));
                    QVERIFY2(!commande->icon().pixmap(16, 16).isNull(), qUtf8Printable(commande->text()));
                }
            }
        }
        QCOMPARE(menus_fr, QStringList({u"Fichier"_s, u"Édition"_s, u"Mode"_s, u"Aide"_s}));
        QCOMPARE(a.menuBar()->font(), QApplication::font());
        for (QAction *x : a.menuBar()->actions().last()->menu()->actions()) {
            if (!x->isSeparator())
                aide << x->text();
        }
        QCOMPARE(aide, QStringList({u"Notice d'utilisation…"_s, u"À propos de MeVCine…"_s, u"À propos de Qt…"_s}));
        drapeau->click();
        attend();
        QCOMPARE(i18n::langue(), Langue::Anglais);
        QVERIFY(lit_texte(conf).contains(u"langue = en"_s));
        QStringList menus;
        for (QAction *m : a.menuBar()->actions())
            menus << m->text();
        QCOMPARE(menus, QStringList({u"File"_s, u"Edit"_s, u"Mode"_s, u"Help"_s}));
        QCOMPARE(drapeau->toolTip(), u"Language: English. Click to switch to French (français)."_s);
        QCOMPARE(soleil->toolTip(), u"Light theme. Click to switch to the dark theme."_s);
        QVERIFY(!bouton(a, u"Guide"_s));
        QVERIFY(bouton(a, u"Start marking"_s));
        QVERIFY(bouton(a, u"Set the scale"_s));
        rendu(a, u"65_expert_anglais"_s);
        drapeau->click();
        attend();
        QCOMPARE(i18n::langue(), Langue::Francais);
        QVERIFY(lit_texte(conf).contains(u"langue = fr"_s));

        // le thème sombre, puis le thème clair
        const bool sombre = dessin::sombre(QApplication::palette());
        soleil->click();
        attend();
        QCOMPARE(dessin::sombre(QApplication::palette()), !sombre);
        QVERIFY(lit_texte(conf).contains(sombre ? u"theme = light"_s : u"theme = dark"_s));
        if (!sombre) {
            // les textes colorés suivent le thème
            QCOMPARE(a.m_statut->palette().color(QPalette::WindowText), QApplication::palette().color(QPalette::WindowText));
            QCOMPARE(soleil->toolTip(), u"Thème sombre. Cliquer pour passer au thème clair."_s);
            // la palette sombre de la suite
            const QPalette p = QApplication::palette();
            QCOMPARE(p.color(QPalette::Window), QColor(32, 33, 36));
            QCOMPARE(p.color(QPalette::Base), QColor(22, 23, 25));
            QCOMPARE(p.color(QPalette::Highlight), QColor(53, 132, 228));
            QCOMPARE(dessin::couleur_attention(p), QColor(255, 143, 0));
            rendu(a, u"66_expert_sombre"_s);
            rendu_menu(a, trad("Mode"), u"66_menu_mode_sombre"_s, trad("Mode simplifié (pas à pas)"));
            // quelques points, pour les onglets des mesures
            QVERIFY(clique(a, u"Démarrer le pointage"_s));
            for (int i = 0; i < 5; ++i)
                clic_image(a, {100.0 + 30.0 * i, 150.0});
            onglet(a, u"Trajectoires"_s);
            QCOMPARE(a.onglet, Onglet::Trajectoires);
            a.traj.vecteurs = true;
            a.rafraichit();
            rendu(a, u"67_trajectoires_sombre"_s);
            // la trajectoire et le chronogramme sur le fond sombre des graphiques (Base)
            for (const ModeTrajectoire mode : {ModeTrajectoire::Trajectoire, ModeTrajectoire::Chronogramme}) {
                a.traj.mode = mode;
                a.rafraichit();
                auto *dessin = a.m_onglet_trajectoire->findChild<DessinTrajectoire *>();
                QVERIFY(dessin);
                const QImage image = dessin->grab().toImage();
                const dessin::Vue vue = dessin::Vue::ajuste(QRectF(dessin->rect()), a.dimensions());
                const QPoint coin = (vue.rect.topLeft() + QPointF(3, 3)).toPoint() * image.devicePixelRatio();
                QCOMPARE(QColor(image.pixel(coin)), dessin->palette().color(QPalette::Base));
                QVERIFY2(QColor(image.pixel(coin)).lightness() < 60, qUtf8Printable(QColor(image.pixel(coin)).name()));
            }
            a.traj.mode = ModeTrajectoire::Trajectoire;
            a.rafraichit();
            onglet(a, u"Grapheur"_s);
            a.graph.x = u"t"_s;
            a.graph.y = u"x"_s;
            a.graph.style = 1;
            a.rafraichit();
            rendu(a, u"69_grapheur_sombre"_s);
            onglet(a, u"Pointage"_s);
        }
        soleil->click();
        attend();
        QCOMPARE(dessin::sombre(QApplication::palette()), sombre);
        // les tests suivants restent en thème clair (et non dans le
        // thème du bureau)
        theme::choisit(Qt::ColorScheme::Light);
        attend();
    }

    /// Le mode simplifié dans chaque langue : les étapes se suivent avec les intitulés traduits, et le
    /// texte copié pour Python a ses commentaires dans la langue. En anglais, aucun texte affiché
    /// n'est resté en français.
    void guide_dans_chaque_langue()
    {
        theme::choisit(Qt::ColorScheme::Light);
        const QString film = film_notice();
        QVERIFY(!film.isEmpty());
        // seuls noms propres accentués permis en anglais
        const QStringList permis = {u"Rémi MEVAERE"_s, u"Français"_s,
                                    u"Language: English. Click to switch to French (français)."_s};
        for (Langue langue : TOUTES_LES_LANGUES) {
            const QString code = i18n::code(langue);
            {
                // au premier lancement : le choix du mode
                configuration(langue, true);
                auto app = fenetre({}, std::nullopt, langue, true);
                QVERIFY(bouton(*app, trad("Mode simplifié\npas à pas")));
                rendu(*app, u"95_%1_premier_demarrage"_s.arg(code));
            }
            {
                // au lancement, sans choix du mode : l'écran qui propose d'ouvrir une vidéo
                auto app = fenetre({}, std::nullopt, langue);
                QCOMPARE(i18n::langue(), langue);
                QVERIFY(bouton(*app, trad("Ouvrir une vidéo…")));
                rendu(*app, u"91_%1_sans_video"_s.arg(code));
            }
            auto app = fenetre(film, Mode::Simplifie, langue);
            App &a = *app;
            QStringList textes = textes_affiches(a);
            rendu(a, u"84_%1_origine"_s.arg(code));
            clic_image(a, {45, 188});
            QVERIFY(clique(a, trad("Suivant")));
            textes << textes_affiches(a);
            QVERIFY(clique(a, trad("x vers la droite") + u", "_s + trad("y vers le haut")));
            rendu(a, u"81_%1_axes"_s.arg(code));
            QVERIFY(clique(a, trad("Suivant")));
            QVERIFY(clique(a, trad("Sélectionner l'étalon")));
            clic_image(a, {45, 188});
            clic_image(a, {398, 188});
            QComboBox *unite = a.m_guide->unite_etalon();
            unite->setCurrentIndex(unite->findText(u"cm"_s));
            attend();
            tape(a.m_guide->saisie_etalon(), u"85"_s);
            QVERIFY(clique(a, trad("Valider")));
            QVERIFY2(a.pointage.echelle.est_definie(), qUtf8Printable(code + u" : "_s + a.guide.erreur_etalon.value_or(QString())));
            QVERIFY(std::abs(a.pointage.echelle.longueur_reelle_etalon - 0.85) < 1e-12);
            textes << textes_affiches(a);
            rendu(a, u"82_%1_echelle"_s.arg(code));
            QVERIFY(clique(a, trad("Suivant")));
            a.change_image(9);
            a.rafraichit();
            attend();
            for (int i = 0; i < 4; ++i)
                clic_image(a, centre_bille(a.index));
            QCOMPARE(a.index, 13);
            ZoneVideo *z = zone(a);
            souris(z, QEvent::MouseMove, z->vue().ecran(centre_bille(a.index)), Qt::NoButton, Qt::NoButton);
            textes << textes_affiches(a);
            rendu(a, u"85_%1_pointage"_s.arg(code));
            rendu_fenetre(z, u"85_%1_image"_s.arg(code), z->vue().rect.toAlignedRect());
            QVERIFY(clique(a, trad("Visée assistée")));
            QVERIFY(a.visee_proposee());
            Loupe *loupe = a.m_guide->findChild<Loupe *>();
            clic(loupe, QRectF(loupe->rect()).center());
            QCOMPARE(a.pointage.nb_images_pointees(), 5);
            rendu(a, u"85_%1_visee"_s.arg(code));
            a.retire_mesure_avant();
            a.rafraichit();
            // le plein écran, pour pointer : la loupe et la consigne dans un cadre posé sur l'image
            QVERIFY(clique(a, trad("Plein écran")));
            QVERIFY(a.plein_ecran());
            textes << textes_affiches(a);
            {
                PleinEcran *pe = a.m_plein_ecran;
                // l'écran de la plateforme sans écran (offscreen) est carré  : on donne à la fenêtre un format rectangulaire
                pe->setWindowState(Qt::WindowNoState);
                pe->resize(1280, 800);
                attend();
                ZoneVideo *zp = pe->zone();
                souris(zp, QEvent::MouseMove, zp->vue().ecran(centre_bille(a.index)), Qt::NoButton, Qt::NoButton);
                QVERIFY(!pe->cadre_a_droite());
                rendu_fenetre(pe, u"92_%1_plein_ecran"_s.arg(code));
                QTest::keyClick(pe, Qt::Key_Escape);
                attend();
            }
            QVERIFY(!a.plein_ecran());
            QCOMPARE(a.pointage.nb_images_pointees(), 4);
            // les nombres du tableau suivent la langue : 0.040 en anglais, 0,040 ailleurs
            const Tableau t = Tableau::construit(a.pointage);
            QCOMPARE(t.lignes[1][0], langue == Langue::Anglais ? u"0.040"_s : u"0,040"_s);
            QVERIFY(clique(a, trad("Terminer le pointage")));
            QCOMPARE(a.guide.etape, Etape::Export);
            QVERIFY(affiche(a, u"5. Export"_s));
            QVERIFY(clique(a, trad("Copier pour Python")));
            const QString texte = a.presse_papier;
            QVERIFY2(texte.contains(trad("Mesures MeVCine — vidéo : {}", QFileInfo(film).fileName())), qUtf8Printable(texte));
            QVERIFY2(texte.contains(u"lst_t = np.array([0.0, 0.04, 0.08, 0.12])"_s), qUtf8Printable(texte));
            verifie_python(texte, u"assert len(lst_x) == 4"_s);
            textes << textes_affiches(a);
            rendu(a, u"83_%1_export"_s.arg(code));
            // les menus Fichier (la sauvegarde automatique est là : « Récupérer le travail
            // précédent… » est actif) et Mode, ouverts comme sous la souris
            QVERIFY(QFileInfo(a.fichier_secours()).isFile());
            rendu_menu(a, trad("Fichier"), u"93_%1_menu_fichier"_s.arg(code));
            rendu_menu(a, trad("Mode"), u"94_%1_menu_mode"_s.arg(code), trad("Mode expert (toutes les fonctions)"));
            rendu_menu(a, trad("Édition"), u"96_%1_menu_edition"_s.arg(code));
            rendu_menu(a, trad("Aide"), u"97_%1_menu_aide"_s.arg(code));
            QCOMPARE(a.mode, Mode::Simplifie);
            // Continuer le même pointage dans les quatre onglets du mode expert.
            a.change_mode(Mode::Expert);
            a.rafraichit();
            for (int k = 0; k < 8; ++k)
                clic_image(a, centre_bille(a.index));
            QCOMPARE(a.pointage.nb_images_pointees(), 12);
            {
                ZoneVideo *ze = zone(a);
                souris(ze, QEvent::MouseMove, ze->vue().ecran(centre_bille(a.index)), Qt::NoButton, Qt::NoButton);
            }
            textes << textes_affiches(a);
            rendu(a, u"86_%1_expert_pointage"_s.arg(code));
            onglet(a, trad("Trajectoires"));
            a.traj.vecteurs = true;
            a.rafraichit();
            textes << textes_affiches(a);
            rendu(a, u"87_%1_expert_trajectoires"_s.arg(code));
            onglet(a, trad("Coordonnées"));
            auto *table = tableau(a)->table();
            const QPoint cellule = table->visualItemRect(table->item(2, 1)).center();
            souris(table->viewport(), QEvent::MouseMove, cellule, Qt::NoButton, Qt::NoButton);
            auto *apercu = a.m_onglet_coord->findChild<ZoneVideo *>(u"apercuMesure"_s);
            QVERIFY(apercu);
            QTRY_VERIFY(apercu->isVisible());
            textes << textes_affiches(a);
            rendu(a, u"88_%1_expert_coordonnees"_s.arg(code));
            onglet(a, trad("Grapheur"));
            a.graph.x = u"t"_s;
            a.graph.y = u"y"_s;
            a.graph.style = 1;
            a.rafraichit();
            attend();
            Graphe *graphe = a.m_onglet_graph->graphe();
            const auto lecture = [&](int i) {
                return QPointF(a.pointage.date_relative(i),
                               a.pointage.position_dans_repere(*a.pointage.point_de_l_objet(1, i)).y);
            };
            souris(graphe, QEvent::MouseMove, graphe->ecran(lecture(10)), Qt::NoButton, Qt::NoButton);
            QVERIFY(graphe->lecture() && graphe->lecture()->image == 10);
            textes << textes_affiches(a);
            rendu(a, u"89_%1_expert_grapheur"_s.arg(code));
            a.graph.y = u"vy"_s;
            a.rafraichit();
            graphe->pointClique(14);
            auto *calcul = a.m_onglet_graph->findChild<QDialog *>();
            QVERIFY(calcul && calcul->isVisible());
            rendu_fenetre(calcul, u"99_%1_calcul"_s.arg(code));
            calcul->close();
            attend();
            // une mesure de plus, puis la question avant de quitter
            onglet(a, trad("Pointage"));
            clic_image(a, centre_bille(a.index));
            QVERIFY2(!a.travail_en_securite(), qUtf8Printable(code));
            a.confirme_si_mesures({Apres::Type::Quitter});
            attend();
            QVERIFY2(affiche(a, trad("Quitter MeVCine ?")), qUtf8Printable(code));
            textes << textes_affiches(a);
            rendu(a, u"90_%1_confirmation"_s.arg(code));
            rendu_fenetre(a.dialogue_affiche(), u"90_%1_dialogue"_s.arg(code));
            QVERIFY(clique(a, trad("Annuler")));
            QVERIFY2(a.dialogues.empty(), qUtf8Printable(code));
            if (langue == Langue::Anglais) {
                QStringList restes;
                for (const QString &t : textes) {
                    const bool accentue = std::any_of(t.begin(), t.end(), [](QChar c) { return u"éèêàâçôûùîï«»"_s.contains(c); });
                    if (accentue && !permis.contains(t) && !restes.contains(t))
                        restes << t;
                }
                QVERIFY2(restes.isEmpty(), qUtf8Printable(u"textes restés en français :\n"_s + restes.join(u'\n')));
            }
            auto suivi = fenetre(film, Mode::Expert, langue);
            QVERIFY(clique(*suivi, trad("Suivi automatique")));
            QVERIFY(clique(*suivi, trad("Démarrer le pointage")));
            suivi->change_image(9);
            suivi->rafraichit();
            const Vecteur c = centre_bille(9);
            glisse_image(*suivi, c - Vecteur(17, 17), c + Vecteur(17, 17));
            QTRY_VERIFY_WITH_TIMEOUT(suivi->etat == Etat::Pointage, 30000);
            QVERIFY(!suivi->pointage.provenances().empty());
            onglet(*suivi, trad("Pointage"));
            rendu(*suivi, u"98_%1_suivi"_s.arg(code));
            // Le même film et le même étalon servent à illustrer les colonnes des deux notices.
            suivi->pointage.echelle = a.pointage.echelle;
            suivi->pointage.origine = a.pointage.origine;
            suivi->change_mode(Mode::Simplifie);
            theme::choisit(Qt::ColorScheme::Dark);
            suivi->rafraichit();
            attend();
            QTableWidget *mesures = tableau(*suivi)->table();
            QVERIFY(mesures->isVisible() && mesures->rowCount() >= 6);
            mesures->scrollToTop();
            QTest::mouseMove(zone(*suivi), QPoint(10, 10));
            const int hauteur = mesures->rowViewportPosition(5) + mesures->rowHeight(5) + mesures->horizontalHeader()->height();
            const QRect cadre(0, 0, mesures->horizontalHeader()->length() + 2 * mesures->frameWidth(), hauteur + 2 * mesures->frameWidth());
            rendu_fenetre(mesures, u"100_%1_tableau"_s.arg(code), cadre);
            theme::choisit(Qt::ColorScheme::Light);
        }
        i18n::choisit(Langue::Francais);
    }

    void journal_des_plantages()
    {
        // plantage (accès mémoire invalide) : le journal reçoit la date, la cause et la pile
        // d'appels ; l'essai se relance lui-même, et c'est ce second programme qui plante
        if (qEnvironmentVariableIsSet("MEVSUITE_ESSAI_PLANTAGE")) {
            erreurs::installe(false);
            plante();
        }
        QTemporaryDir conf;
        QVERIFY(conf.isValid());
        QProcess enfant;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"MEVSUITE_ESSAI_PLANTAGE"_s, u"1"_s);
        env.insert(u"MEVCINE_CONF"_s, conf.path());
        enfant.setProcessEnvironment(env);
        enfant.start(QCoreApplication::applicationFilePath(), {u"journal_des_plantages"_s});
        QVERIFY(enfant.waitForFinished(60000));
        QCOMPARE(enfant.exitStatus(), QProcess::CrashExit);
        const QStringList lignes = lit_texte(conf.filePath(u"mevcine.log"_s)).split(u'\n', Qt::SkipEmptyParts);
        QVERIFY2(lignes.size() >= 4, qUtf8Printable(lignes.join(u'\n')));
        static const QRegularExpression entete(uR"(^\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d UTC\] MeVCine (\S+) \(C\+\+, Qt\)$)"_s);
        const QRegularExpressionMatch m = entete.match(lignes[0]);
        QVERIFY2(m.hasMatch(), qUtf8Printable(lignes[0]));
        QCOMPARE(m.captured(1), a_propos::VERSION);
#ifdef Q_OS_WIN
        QCOMPARE(lignes[1], u"Exception 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)"_s);
#else
        QCOMPARE(lignes[1], u"Signal 11 (SIGSEGV)"_s);
#endif
    }

    void journal_d_une_exception()
    {
        // exception que rien ne rattrape, dans un fil de calcul : le journal reçoit son message
        // (sous Windows, le gestionnaire de plantage la laisse repartir vers std::terminate)
        if (qEnvironmentVariableIsSet("MEVSUITE_ESSAI_EXCEPTION")) {
            erreurs::installe(false);
            lance_dans_un_fil();
        }
        QTemporaryDir conf;
        QVERIFY(conf.isValid());
        QProcess enfant;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"MEVSUITE_ESSAI_EXCEPTION"_s, u"1"_s);
        env.insert(u"MEVCINE_CONF"_s, conf.path());
        enfant.setProcessEnvironment(env);
        enfant.start(QCoreApplication::applicationFilePath(), {u"journal_d_une_exception"_s});
        QVERIFY(enfant.waitForFinished(60000));
        QVERIFY(enfant.exitStatus() == QProcess::CrashExit || enfant.exitCode() != 0);
        const QStringList lignes = lit_texte(conf.filePath(u"mevcine.log"_s)).split(u'\n', Qt::SkipEmptyParts);
        QVERIFY2(lignes.size() >= 2, qUtf8Printable(lignes.join(u'\n')));
        QVERIFY2(lignes[0].contains(u"] MeVCine "_s + a_propos::VERSION), qUtf8Printable(lignes[0]));
        // journal écrit en mode texte : \r\n sous Windows
        QCOMPARE(lignes[1].trimmed(), u"essai d'exception"_s);
    }
};

QTEST_MAIN(TestsInterface)
#include "tests_interface.moc"
