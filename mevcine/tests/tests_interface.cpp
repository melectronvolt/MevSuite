// Tests de bout en bout : on pilote l'interface comme le ferait un élève ou un professeur, dans les
// deux modes, comme les tests de l'interface de la version Rust.
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
#include "i18n.h"
#include "outils.h"
#include "prefs.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QMenu>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOptionMenuItem>
#include <QToolBar>
#include <QStatusBar>
#include <QScrollBar>
#include <QScrollArea>
#include <QToolButton>

#include <memory>

namespace {

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
    void configuration(std::optional<Langue> langue)
    {
        m_conf = std::make_unique<QTemporaryDir>();
        qputenv("MEVCINE_CONF", m_conf->path().toLocal8Bit());
        ecrit_texte(prefs::fichier_conf(),
                    u"[DEFAULT]\nversion = mevcine 1.0\nlangue = %1\n"_s.arg(i18n::code(langue.value_or(Langue::Francais))));
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
    void rendu(App &app, const QString &nom)
    {
        attend();
        const QDir dossier(QString::fromUtf8(MEVCINE_RENDUS_TESTS));
        QDir().mkpath(dossier.path());
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

    /// déclenche l'entrée d'un menu
    bool menu(App &app, const QString &intitule)
    {
        for (QAction *a : app.menuBar()->actions()) {
            if (!a->menu())
                continue;
            for (QAction *b : a->menu()->actions()) {
                if (b->text() == intitule && b->isEnabled()) {
                    b->trigger();
                    attend();
                    return true;
                }
            }
        }
        return false;
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
        return app.simplifie() ? app.m_guide->tableau() : app.m_onglet_coord->tableau();
    }

    /// le bouton ⌖ d'une ligne du tableau des mesures
    QAbstractButton *refaire(App &app, int ligne)
    {
        QTableWidget *t = tableau(app)->table();
        return qobject_cast<QAbstractButton *>(t->cellWidget(ligne, t->columnCount() - 1));
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
    void init()
    {
        i18n::choisit(Langue::Francais);
    }

    void accueil_choix_du_mode()
    {
        auto app = fenetre({}, std::nullopt);
        QCOMPARE(app->mode, std::nullopt);
        QCOMPARE(app->windowTitle(), u"MeVCine — Étude cinématique de vidéos"_s);
        QVERIFY(bouton(*app, u"Mode simplifié"_s));
        QVERIFY(bouton(*app, u"Mode expert"_s));
        // la notice est dans le menu Aide (F1), sans bouton à elle ; l'accueil n'a pas d'autre bouton
        QVERIFY(!bouton(*app, u"Notice"_s));
        QVERIFY(!bouton(*app, u"📖 Notice complète…"_s));
        QCOMPARE(app->page_d_aide(), u"index.html"_s);
        QVERIFY(QFileInfo(QDir(chemins::help_dir()).filePath(u"fr/index.html"_s)).isFile());
        rendu(*app, u"00_accueil"_s);
        QVERIFY(clique(*app, u"Mode simplifié"_s));
        QCOMPARE(app->mode, Mode::Simplifie);
        // pas de vidéo rouverte d'office : l'écran d'ouverture, avec un seul bouton (pas de vidéos
        // d'exemple : chacun ouvre les siennes)
        auto *ouvrir = app->m_sans_video->findChild<QPushButton *>(u"ouvrir"_s);
        QVERIFY(ouvrir && ouvrir->isVisible());
        QCOMPARE(ouvrir->text(), u"📂 Ouvrir une vidéo…"_s);
        QCOMPARE(app->m_sans_video->findChildren<QPushButton *>().size(), 1);
        for (QAction *x : app->menuBar()->actions().front()->menu()->actions())
            QVERIFY2(!x->text().contains(u"exemple"_s, Qt::CaseInsensitive), qPrintable(x->text()));
        QCOMPARE(app->page_d_aide(), u"demarrer.html"_s);
        rendu(*app, u"01_sans_video"_s);
        // une vidéo, glissée sur la fenêtre comme un élève le ferait
        QVERIFY(app->traite_arg(outils::video(u"Principe_inertie.avi"_s)));
        app->rafraichit();
        attend();
        QVERIFY(app->video);
        QCOMPARE(app->windowTitle(), u"MeVCine — Principe_inertie.avi"_s);
    }

    /// Les deux pages sans vidéo (le choix du mode, puis l'ouverture d'une vidéo) sont épurées et
    /// tiennent sans chevauchement dans la fenêtre à sa taille minimale, dans chaque langue et dans
    /// chaque thème
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
                a.mode.reset();
                a.rafraichit();
                attend();
                verifie_page_epuree(a.m_accueil, nom + u" accueil"_s);
                rendu(a, u"03_%1_accueil_minimale"_s.arg(nom));
                a.change_mode(Mode::Simplifie);
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
        auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Expert);
        App &a = *app;
        QStatusBar *barre = a.statusBar();
        const int ligne = a.m_statut->fontMetrics().height();
        // une ligne de texte, ses marges (4 + 4) et celles de la barre (6) : deux lignes en ajouteraient
        // une de plus
        QVERIFY2(barre->height() < 2 * ligne + 14, qUtf8Printable(u"%1 pour une ligne de %2"_s.arg(barre->height()).arg(ligne)));
        for (QLabel *l : {a.m_info_video, a.m_info_image, a.m_info_mode})
            QVERIFY(l->isVisible());
        QVERIFY(a.m_info_video->text().startsWith(u"Principe_inertie.avi · 400 × 300 px"_s));
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
        auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Simplifie);
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
        const QDir dossier(chemins::help_dir());
        for (const QString &page : {u"index.html"_s, u"demarrer.html"_s, u"simplifie.html"_s, u"expert.html"_s}) {
            for (Langue l : TOUTES_LES_LANGUES)
                QVERIFY2(QFileInfo(dossier.filePath(i18n::code(l) + u'/' + page)).isFile(), qUtf8Printable(page));
        }
    }

    /// Le mode Simplifié, étape par étape, avec la vidéo d'un palet sur une table (une règle jaune de
    /// 0,85 m sert d'étalon)
    void guide_complet()
    {
        auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Simplifie);
        App &a = *app;
        QCOMPARE(a.etat, Etat::A);
        QCOMPARE(a.guide.etape, Etape::Origine);
        QVERIFY(a.choix_origine);
        QCOMPARE(a.page_d_aide(), u"simplifie.html"_s);
        sans_energie(a);
        rendu(a, u"10_guide_origine"_s);

        // 1. l'origine : un glisser la place là où on lâche le bouton, puis un clic au bout de la règle
        glisse_image(a, {120, 120}, {150, 160});
        QVERIFY(a.guide.origine_choisie);
        QVERIFY2((a.pointage.origine - Vecteur(150, 160)).norme() < 1.0, qUtf8Printable(a.pointage.origine.texte()));
        clic_image(a, {45, 188});
        QVERIFY((a.pointage.origine - Vecteur(45, 188)).norme() < 1.0);
        // on ne peut pas sauter l'étape des axes
        QVERIFY(!a.etape_accessible(Etape::Echelle));
        QVERIFY(clique(a, u"Suivant ▶"_s));
        QCOMPARE(a.guide.etape, Etape::Axes);
        QVERIFY(!a.choix_origine);

        // 2. les axes : aucun choix d'office ; l'origine se déplace encore en la saisissant, mais un
        // glisser ailleurs sur l'image ne fait rien
        QVERIFY(!a.guide.axes_choisis);
        QVERIFY(!a.etape_faite(Etape::Axes));
        glisse_image(a, {46, 189}, {60, 200});
        QVERIFY2((a.pointage.origine - Vecteur(60, 200)).norme() < 1.0, qUtf8Printable(a.pointage.origine.texte()));
        glisse_image(a, {200, 100}, {250, 120});
        QVERIFY((a.pointage.origine - Vecteur(60, 200)).norme() < 1.0);
        glisse_image(a, {60, 200}, {45, 188});
        QVERIFY((a.pointage.origine - Vecteur(45, 188)).norme() < 1.0);
        rendu(a, u"11_guide_axes"_s);
        QVERIFY(clique(a, u"x vers la droite, y vers le haut"_s));
        QVERIFY(a.guide.axes_choisis);
        QCOMPARE(std::pair(a.pointage.sens_x, a.pointage.sens_y), std::pair(1, 1));
        QVERIFY(clique(a, u"Suivant ▶"_s));
        QCOMPARE(a.guide.etape, Etape::Echelle);

        // 3. l'échelle : on sélectionne l'étalon (deux clics), puis on donne sa longueur en mètre, en
        // notation scientifique
        QVERIFY(clique(a, u"Sélectionner l'étalon"_s));
        QCOMPARE(a.etat, Etat::C);
        clic_image(a, {45, 188});
        QVERIFY(a.etalon_deux_clics);
        ZoneVideo *z = zone(a);
        souris(z, QEvent::MouseMove, z->vue().ecran({250, 188}), Qt::NoButton, Qt::NoButton);
        attend();
        rendu(a, u"12_guide_etalon_en_cours"_s);
        clic_image(a, {398, 188});
        QCOMPARE(a.etat, Etat::A);
        QVERIFY(a.etalon_en_attente.has_value());
        const auto [p1, p2] = *a.etalon_en_attente;
        QVERIFY(std::abs((p2 - p1).norme() - 353.0) < 1.5);
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
        QVERIFY(affiche(a, u"✔ Échelle : 85 cm pour 353 pixels."_s));
        rendu(a, u"14_guide_echelle"_s);
        // une unité écrite après le nombre remplace celle de la liste
        efface(champ);
        tape(champ, u"8,5E-1 m"_s);
        QVERIFY(clique(a, u"Valider"_s));
        e = a.pointage.echelle;
        QVERIFY(std::abs(e.longueur_reelle_etalon - 0.85) < 1e-12);
        QCOMPARE(a.unite_etalon, Unite::Metre);
        QCOMPARE(unite->currentText(), u"m"_s);
        QVERIFY(clique(a, u"Suivant ▶"_s));
        QCOMPARE(a.guide.etape, Etape::Pointage);
        QCOMPARE(a.etat, Etat::D);

        // 4. le pointage de dix images ; le tableau se remplit à côté
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(a.index, i + 1);
            clic_image(a, {365.0 - 16.0 * i, 110.0 + 0.5 * i});
        }
        QCOMPARE(a.pointage.nb_images_pointees(), 10);
        QCOMPARE(a.index, 11);
        // 5. le tableau : t, x, y, et une colonne pour refaire une mesure
        const Tableau t = Tableau::construit(a.pointage);
        QCOMPARE(t.en_tetes, QStringList({u"t (s)"_s, u"x (m)"_s, u"y (m)"_s}));
        QCOMPARE(t.lignes.size(), size_t(10));
        QCOMPARE(t.lignes[0][0], u"0,000"_s);
        QCOMPARE(t.lignes[1][0], u"0,040"_s);
        QVERIFY(affiche(a, u"Refaire"_s));
        QCOMPARE(tableau(a)->table()->rowCount(), 10);
        rendu(a, u"15_guide_pointage"_s);
        QVERIFY(clique(a, u"Terminer le pointage ▶"_s));
        QCOMPARE(a.guide.etape, Etape::Tableau);
        // en dehors du pointage, un clic sur l'image ne pointe rien
        clic_image(a, {100, 100});
        QCOMPARE(a.pointage.nb_images_pointees(), 10);

        // refaire la mesure de l'image 5
        const Vecteur ancienne = *a.pointage.position(1, 4);
        refaire(a, 4)->click();
        attend();
        QCOMPARE(a.refait, 11);
        QCOMPARE(a.index, 5);
        rendu(a, u"16_guide_refaire"_s);
        clic_image(a, ancienne + Vecteur(6, -3));
        QCOMPARE(a.refait, std::nullopt);
        QCOMPARE(a.index, 11); // retour à l'image où l'on en était
        const Vecteur nouvelle = *a.pointage.position(1, 4);
        QVERIFY((nouvelle - (ancienne + Vecteur(6, -3))).norme() < 1.0);
        QCOMPARE(a.pointage.nb_images_pointees(), 10);

        // l'origine et les axes changent après le pointage : les mesures sont recalculées, et gardées
        const double x_avant = a.pointage.point_en_metre(nouvelle).x;
        a.va_a_l_etape(Etape::Origine);
        a.rafraichit();
        QVERIFY(a.choix_origine);
        clic_image(a, {100, 250});
        QCOMPARE(a.pointage.nb_images_pointees(), 10);
        const double x_apres = a.pointage.point_en_metre(nouvelle).x;
        QVERIFY(std::abs(x_avant - x_apres - 55.0 * 0.85 / 353.0) < 0.01);
        a.va_a_l_etape(Etape::Axes);
        a.rafraichit();
        QVERIFY(clique(a, u"x vers la gauche, y vers le bas"_s));
        QVERIFY(std::abs(a.pointage.point_en_metre(nouvelle).x + x_apres) < 1e-9);
        rendu(a, u"17_guide_axes_changes"_s);
        a.va_a_l_etape(Etape::Axes);
        a.rafraichit();
        QVERIFY(clique(a, u"x vers la droite, y vers le haut"_s));

        // 6. l'export
        a.va_a_l_etape(Etape::Export);
        a.rafraichit();
        QCOMPARE(a.guide.etape, Etape::Export);
        QVERIFY(clique(a, u"📋 Copier pour Python"_s));
        const QString texte = a.presse_papier;
        QVERIFY2(texte.startsWith(u"import numpy as np\n"_s), qUtf8Printable(texte));
        QVERIFY(texte.contains(u"vidéo : Principe_inertie.avi"_s));
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
        QVERIFY(bouton(a, u"Suivant ▶"_s));
        QAbstractButton *precedent = bouton(a, u"◀ Précédent"_s);
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
                       u"t, x, y = np.loadtxt(%1, delimiter=',', skiprows=1, unpack=True)\nassert len(t) == 10"_s.arg(
                           u"'"_s + csv + u"'"_s));
        sans_energie(a);
    }

    /// Le mode Expert : toutes les fonctions, sans aucune énergie
    void expert_complet()
    {
        auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Expert);
        App &a = *app;
        QCOMPARE(a.etat, Etat::A);
        QCOMPARE(a.image_max, 28);
        QCOMPARE(a.dimensions(), QSize(400, 300));
        QCOMPARE(a.page_d_aide(), u"expert.html"_s);
        // un pictogramme par onglet, à la hauteur du texte
        for (int i = 0; i < a.m_expert->count(); ++i)
            QVERIFY(!a.m_expert->tabIcon(i).isNull());
        QCOMPARE(a.m_expert->iconSize().height(), QFontMetrics(QApplication::font()).height());
        rendu(a, u"30_expert"_s);

        // l'échelle : l'étalon d'abord (tirer-glisser), sa longueur ensuite
        QVERIFY(clique(a, u"Définir l'échelle"_s));
        QCOMPARE(a.etat, Etat::C);
        glisse_image(a, {45, 188}, {398, 188});
        QCOMPARE(a.etat, Etat::A);
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
        QVERIFY2(std::abs(e.longueur_pixel_etalon() - 353.0) < 1.5, qUtf8Printable(QString::number(e.longueur_pixel_etalon())));
        QVERIFY(affiche(a, u"0,85 m pour 353 px"_s));

        // avant le pointage, l'origine se fait glisser
        const Vecteur centre = a.pointage.origine;
        glisse_image(a, centre, {210, 160});
        QVERIFY2((a.pointage.origine - Vecteur(210, 160)).norme() < 1.0, qUtf8Printable(a.pointage.origine.texte()));
        glisse_image(a, {210, 160}, centre);

        // démarrage du pointage manuel, sur dix images
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::D);
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(a.index, i + 1);
            clic_image(a, {365.0 - 16.0 * i, 110.0 + 0.5 * i});
        }
        QCOMPARE(a.pointage.premiere_image(), 1);
        QCOMPARE(a.pointage.derniere_image(), 10);
        const Vecteur p5 = *a.pointage.position(1, 4);
        QVERIFY2(std::abs(p5.x - 301.0) < 1.0 && std::abs(p5.y - 112.0) < 1.0, qUtf8Printable(p5.texte()));
        rendu(a, u"31_expert_pointage"_s);

        // défaire, refaire
        QVERIFY(clique(a, u"↶"_s));
        QCOMPARE(a.pointage.derniere_image(), 9);
        QCOMPARE(a.index, 10);
        QVERIFY(clique(a, u"↷"_s));
        QCOMPARE(a.pointage.derniere_image(), 10);
        QCOMPARE(a.index, 11);

        // un pointage hors des images voisines est refusé
        a.change_image(20);
        a.rafraichit();
        clic_image(a, {100, 100});
        QCOMPARE(a.pointage.derniere_image(), 10);
        a.change_image(11);
        a.rafraichit();

        // les trajectoires, avec les vecteurs vitesse
        onglet(a, u"Trajectoires"_s);
        QCOMPARE(a.onglet, Onglet::Trajectoires);
        QVERIFY(clique(a, u"Montrer les vecteurs vitesse"_s));
        QVERIFY(a.traj.vecteurs);
        // l'échelle automatique : le plus long vecteur mesure environ le septième de l'image
        QVERIFY(a.traj.echelle_vitesse.isEmpty());
        const double echelle = echelle_vitesse_auto(a.pointage, a.dimensions());
        double plus_long = 0.0;
        for (const auto &[o, b] : a.pointage.vecteurs_vitesse(echelle)[0])
            plus_long = std::max(plus_long, (b - o).norme());
        QVERIFY2(plus_long > 25.0 && plus_long < 80.0, qUtf8Printable(QString::number(plus_long)));
        QCOMPARE(arrondi_rond(47.0), 50.0);
        QCOMPARE(arrondi_rond(0.3), 0.2);
        rendu(a, u"32_expert_vitesses"_s);
        // la trajectoire sur un fond clair, comme une feuille
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
        QVERIFY(bouton(a, u"📋 Copier pour Python"_s));
        QVERIFY(bouton(a, u"💾 Enregistrer en CSV…"_s));
        sans_energie(a);
        const Tableau t = Tableau::construit(a.pointage);
        QCOMPARE(t.en_tetes, QStringList({u"t (s)"_s, u"x (m)"_s, u"y (m)"_s}));
        QCOMPARE(t.lignes.size(), size_t(10));
        rendu(a, u"36_expert_coordonnees"_s);
        // refaire une mesure depuis le tableau : retour au tableau ensuite
        refaire(a, 2)->click();
        attend();
        QCOMPARE(a.onglet, Onglet::Pointage);
        QCOMPARE(a.index, 3);
        const Vecteur p3 = *a.pointage.position(1, 2);
        clic_image(a, p3 + Vecteur(2, 0));
        QCOMPARE(a.onglet, Onglet::Coordonnees);
        QCOMPARE(a.index, 11);

        // le grapheur : positions, vitesses et accélérations seulement
        onglet(a, u"Grapheur"_s);
        QStringList noms;
        for (const Serie &s : grandeurs(a.pointage))
            noms << s.nom;
        QCOMPARE(noms, QStringList({u"x"_s, u"y"_s, u"v"_s, u"vx"_s, u"vy"_s, u"a"_s, u"ax"_s, u"ay"_s}));
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
        const QString projet = temporaire(u"palet.mevcine"_s);
        a.enregistre(projet);
        QVERIFY(lit_texte(projet).startsWith(u"# version = mevcine"_s));
        const Pointage avant = a.pointage;
        auto app2 = fenetre(projet, Mode::Expert);
        const App &b = *app2;
        QCOMPARE(b.etat, Etat::D);
        QCOMPARE(b.pointage.premiere_image(), 1);
        QCOMPARE(b.pointage.derniere_image(), 10);
        QCOMPARE(b.index, 11);
        for (int i = 0; i < 10; ++i)
            QVERIFY2((*avant.position(1, i) - *b.pointage.position(1, i)).norme() < 0.01, qUtf8Printable(u"image %1"_s.arg(i + 1)));
        QVERIFY(std::abs(b.pointage.echelle.px_par_m() - avant.echelle.px_par_m()) < 1e-6);
        QVERIFY(std::abs(b.pointage.delta_t - avant.delta_t) < 1e-12);
        rendu(*app2, u"39_expert_projet_rouvert"_s);

        // le même projet, rouvert en mode simplifié : on reprend au tableau
        auto app3 = fenetre(projet, Mode::Simplifie);
        QCOMPARE(app3->guide.etape, Etape::Tableau);
        QVERIFY(app3->guide.origine_choisie && app3->guide.axes_choisis);
    }

    /// Coordonnées : le tableau prend la place, ses colonnes entières (« Refaire » comprise), l'export
    /// tient sur une ligne au-dessus. Pointage : on l'arrête et on le reprend ; en plein écran, la
    /// loupe reste là et s'écarte de la souris.
    void plein_ecran_et_arret_du_pointage()
    {
        auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Expert);
        App &a = *app;
        QVERIFY(!bouton(a, u"Plein écran"_s));
        // l'échelle, pour que le mode simplifié aille ensuite jusqu'au tableau
        QVERIFY(clique(a, u"Définir l'échelle"_s));
        glisse_image(a, {45, 188}, {398, 188});
        auto *dialogue = qobject_cast<DialogueEchelle *>(a.dialogue_affiche());
        QVERIFY(dialogue);
        tape(dialogue->champ(), u"0,85"_s);
        QVERIFY(clique(a, u"OK"_s));
        QVERIFY(a.pointage.echelle.est_definie());
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::D);
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
        QVERIFY(bouton(a, u"✕ Quitter le plein écran"_s));
        QVERIFY(bouton(a, u"⏹ Arrêter le pointage"_s));
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
        QVERIFY(clique(a, u"⏹ Arrêter le pointage"_s));
        QVERIFY(!a.plein_ecran());
        QVERIFY(a.pointage_arrete);
        QCOMPARE(a.onglet, Onglet::Coordonnees);
        QCOMPARE(a.etat, Etat::D);

        // le tableau prend presque toute la largeur ; rien n'y est caché, « Refaire » compris
        TableauMesures *t = tableau(a);
        QTableWidget *table = t->table();
        QVERIFY2(table->width() > a.m_onglet_coord->width() * 9 / 10, qUtf8Printable(QString::number(table->width())));
        QVERIFY(!table->horizontalScrollBar()->isVisible());
        const int derniere = table->columnCount() - 1;
        QCOMPARE(table->horizontalHeaderItem(derniere)->text(), u"Refaire"_s);
        QVERIFY(table->horizontalHeader()->sectionViewportPosition(derniere) + table->columnWidth(derniere)
                <= table->viewport()->width());
        QVERIFY(table->columnWidth(1) > 2 * table->fontMetrics().horizontalAdvance(u"-0,00000"_s));
        // l'export, sur une ligne au-dessus du tableau
        QAbstractButton *copier = bouton(a, u"📋 Copier pour Python"_s);
        QAbstractButton *csv = bouton(a, u"💾 Enregistrer en CSV…"_s);
        QVERIFY(copier && csv && bouton(a, u"Enregistrer le projet…"_s));
        QCOMPARE(copier->mapTo(&a, QPoint()).y(), csv->mapTo(&a, QPoint()).y());
        QVERIFY(copier->mapTo(&a, QPoint(0, copier->height())).y() < table->mapTo(&a, QPoint()).y());
        rendu(a, u"42_coordonnees_pointage_arrete"_s);

        // pointage arrêté : un clic sur l'image ne pointe plus ; la mesure demandée par le tableau, si
        onglet(a, u"Pointage"_s);
        QVERIFY(affiche(a, u"Pointage arrêté : « ▶ Reprendre le pointage » pour continuer"_s));
        QVERIFY(!bouton(a, u"⏹ Arrêter le pointage"_s));
        clic_image(a, {250.0, 110.0});
        QCOMPARE(a.pointage.nb_images_pointees(), 6);
        rendu(a, u"41_expert_pointage_arrete"_s);
        onglet(a, u"Coordonnées"_s);
        refaire(a, 1)->click();
        attend();
        QCOMPARE(a.onglet, Onglet::Pointage);
        clic_image(a, *a.pointage.position(1, 1) + Vecteur(1, 0));
        QCOMPARE(a.onglet, Onglet::Coordonnees);
        QCOMPARE(a.pointage.nb_images_pointees(), 6);
        // la reprise : on pointe de nouveau, à la suite
        onglet(a, u"Pointage"_s);
        QVERIFY(clique(a, u"▶ Reprendre le pointage"_s));
        QVERIFY(!a.pointage_arrete);
        a.change_image(7);
        a.rafraichit();
        clic_image(a, {253.0, 110.0});
        QCOMPARE(a.pointage.nb_images_pointees(), 7);

        // en mode simplifié, le plein écran à l'étape du pointage ; « Terminer le pointage ▶ » y mène
        // au tableau
        QVERIFY(menu(a, u"Mode simplifié (pas à pas)"_s));
        QCOMPARE(a.mode, Mode::Simplifie);
        a.va_a_l_etape(Etape::Pointage);
        a.rafraichit();
        attend();
        QVERIFY(clique(a, u"Plein écran"_s));
        QVERIFY(a.plein_ecran());
        QVERIFY(clique(a, u"Terminer le pointage ▶"_s));
        QVERIFY(!a.plein_ecran());
        QCOMPARE(a.guide.etape, Etape::Tableau);
        QVERIFY(!bouton(a, u"Plein écran"_s));
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

    void suivi_automatique()
    {
        auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Expert);
        App &a = *app;
        QVERIFY(clique(a, u"Suivi automatique"_s));
        QVERIFY(a.suivi_auto);
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::AB);
        // le message d'explication
        QVERIFY(clique(a, u"OK"_s));
        // les deux premières images montrent la règle d'étalonnage ; le mouvement commence à
        // l'image 3, où le palet est à gauche
        a.change_image(3);
        a.rafraichit();
        glisse_image(a, {40, 83}, {104, 147});
        // la détection se poursuit jusqu'à la dernière image
        QVERIFY(QTest::qWaitFor([&] { return a.etat == Etat::D; }, 30000));
        QCOMPARE(a.pointage.premiere_image(), 3);
        QCOMPARE(a.pointage.derniere_image(), 28);
        std::vector<double> xs, ys;
        for (int i = 2; i < 28; ++i) {
            xs.push_back(a.pointage.position(1, i)->x);
            ys.push_back(a.pointage.position(1, i)->y);
        }
        // sur la première image, le motif est retrouvé là où on l'a pris
        QCOMPARE(std::pair(xs[0], ys[0]), std::pair(72.0, 115.0));
        // ensuite le palet part vers la droite, sans retour en arrière
        for (size_t k = 1; k < xs.size(); ++k)
            QVERIFY(xs[k] > xs[k - 1]);
        for (double y : ys)
            QVERIFY(std::abs(y - 115.0) < 15.0);
        rendu(a, u"40_suivi_auto"_s);
    }

    void origine_axes_rotation_et_deux_objets()
    {
        auto app = fenetre(outils::video(u"g1.avi"_s), Mode::Expert);
        App &a = *app;
        QCOMPARE(a.dimensions(), QSize(400, 300));
        QVERIFY(a.pointage.origine == Vecteur(200, 150));

        // rotation d'un quart de tour : l'origine revient au centre
        QVERIFY(clique(a, u"⟳"_s));
        QCOMPARE(a.dimensions(), QSize(300, 400));
        QVERIFY(a.pointage.origine == Vecteur(150, 200));
        QCOMPARE(a.image.size(), QSize(300, 400));
        QVERIFY(clique(a, u"⟲"_s));
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
        QVERIFY((a.pointage.origine - Vecteur(100, 250)).norme() < 0.5);

        // ordonnées vers le bas
        QVERIFY(clique(a, u"x vers la droite, y vers le bas"_s));
        QCOMPARE(a.pointage.sens_y, -1);
        QCOMPARE(a.pointage.sens_x, 1);

        // deux objets : on passe par l'état E entre les deux clics
        a.nb_obj = 2;
        a.redimensionne_data();
        a.rafraichit();
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        QCOMPARE(a.etat, Etat::D);
        clic_image(a, {150, 200});
        QCOMPARE(a.etat, Etat::E);
        QCOMPARE(a.objet_courant, 2);
        QCOMPARE(a.index, 1);
        clic_image(a, {300, 100});
        QCOMPARE(a.etat, Etat::D);
        QCOMPARE(a.index, 2);
        const Vecteur q1 = *a.pointage.position(1, 0), q2 = *a.pointage.position(2, 0);
        // sans échelle, 1 px = 1 m : ordonnées vers le bas, donc négatives au-dessus de l'origine
        const Vecteur m1 = a.pointage.point_en_metre(q1);
        QVERIFY2(std::abs(m1.x - 50.0) < 0.5 && std::abs(m1.y + 50.0) < 0.5, qUtf8Printable(m1.texte()));
        QVERIFY((q2 - Vecteur(300, 100)).norme() < 0.5);
        // les exports nomment les objets : lst_x1, lst_x2...
        const QString texte = a.texte_python();
        QVERIFY2(texte.contains(u"lst_x1 = "_s) && texte.contains(u"lst_y2 = "_s), qUtf8Printable(texte));
        QVERIFY(texte.contains(u"en pixels"_s));
        rendu(a, u"50_deux_objets"_s);

        // le mode simplifié ne suit qu'un objet : on demande avant d'oublier le second
        a.change_mode(Mode::Simplifie);
        a.rafraichit();
        attend();
        QCOMPARE(a.mode, Mode::Expert);
        QVERIFY(affiche(a, u"Passer en mode simplifié ?"_s));
        rendu(a, u"52_mode_simplifie_un_objet"_s);
        QVERIFY(clique(a, u"Annuler"_s));
        QCOMPARE(a.mode, Mode::Expert);
        QCOMPARE(a.pointage.nb_obj(), 2);
        QVERIFY(menu(a, u"Mode simplifié (pas à pas)"_s));
        QVERIFY(clique(a, u"Garder l'objet 1"_s));
        QCOMPARE(a.mode, Mode::Simplifie);
        QCOMPARE(a.pointage.nb_obj(), 1);
        QCOMPARE(a.nb_obj, 1);
        QVERIFY((*a.pointage.position(1, 0) - q1).norme() < 1e-9);
        QVERIFY(menu(a, u"Mode expert (toutes les fonctions)"_s));
        QCOMPARE(a.mode, Mode::Expert);

        // tout recommencer : on confirme d'abord, car des mesures seraient perdues
        QVERIFY(clique(a, u"Tout recommencer"_s));
        QVERIFY(affiche(a, u"Tout recommencer ?"_s));
        QVERIFY(a.pointage.a_des_pointages());
        QVERIFY(clique(a, u"Tout effacer"_s));
        QCOMPARE(a.etat, Etat::A);
        QVERIFY(!a.pointage.a_des_pointages());
        QCOMPARE(a.pointage.sens_y, 1);
        a.change_mode(Mode::Simplifie);
        a.rafraichit();
        QCOMPARE(a.mode, Mode::Simplifie);
        QCOMPARE(a.pointage.nb_obj(), 1);
    }

    /// Une vidéo au ralenti d'un téléphone Android est reconnue ; « À propos » nomme l'auteur.
    void ralenti_et_a_propos()
    {
        const auto ralenti = outils::video_test(
            u"ralenti.mp4"_s, {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc2=s=320x240:r=30:d=1"_s, u"-c:v"_s, u"libx264"_s,
                               u"-metadata"_s, u"com.android.capture.fps=240.000000"_s, u"-movflags"_s, u"use_metadata_tags"_s});
        if (!ralenti)
            QSKIP("libx264 absent");
        auto app = fenetre(*ralenti, Mode::Expert);
        App &a = *app;
        QCOMPARE(a.etat, Etat::A);
        // les calculs utilisent la cadence de prise de vue
        QCOMPARE(a.fps, 240.0);
        QCOMPARE(a.ips, u"240"_s);
        QVERIFY(std::abs(a.pointage.delta_t - 1.0 / 240.0) < 1e-12);
        QVERIFY(affiche(a, u"Vidéo au ralenti"_s));
        rendu(a, u"60_ralenti"_s);
        QVERIFY(clique(a, u"OK"_s));
        QVERIFY(bouton(a, u"Démarrer le pointage"_s));
        QStringList onglets;
        for (int i = 0; i < a.m_expert->count(); ++i)
            onglets << a.m_expert->tabText(i);
        QCOMPARE(onglets, QStringList({u"Pointage"_s, u"Coordonnées"_s, u"Trajectoires"_s, u"Grapheur"_s}));
        a.propos();
        attend();
        auto *propos = a.findChild<FenetreAPropos *>();
        QVERIFY(propos && propos->isVisible());
        QStringList textes;
        for (QLabel *l : propos->findChildren<QLabel *>())
            textes << l->text();
        const QString tout = textes.join(u'\n');
        QVERIFY(tout.contains(u"Rémi MEVAERE"_s));
        QVERIFY(tout.contains(u"mailto:suite@sciences-physiques.net"_s));
        QVERIFY(textes.contains(u"version %1"_s.arg(a_propos::VERSION)));
        // les composants libres utilisés, avec leur licence
        QVERIFY(textes.contains(u"Composants libres utilisés"_s));
        for (const QString &ressource : {u"pymecavideo"_s, u"Qt "_s, u"FFmpeg"_s, u"GCC"_s, u"mingw-w64"_s, u"LGPL-3.0-only"_s})
            QVERIFY2(tout.contains(ressource), qUtf8Printable(ressource));
        // la notice de licence de pymecavideo, gardée
        QVERIFY(tout.contains(u"Jean-Baptiste Butet"_s) && tout.contains(u"Georges Khaznadar"_s));
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
        QCOMPARE(a.image_max, 119);
        // l'image 21 suit une image perdue
        QVERIFY(std::abs(a.pointage.date(20) - 21.0 / 30.0) < 1e-3);
        QVERIFY(affiche(a, u"Cadence variable"_s));
        rendu(a, u"63_cadence_variable"_s);
    }

    /// un ancien projet .mecavideo (positions en pixels d'écran) se rouvre
    void ancien_projet_mecavideo()
    {
        const QString projet = temporaire(u"ancien.mecavideo"_s);
        ecrit_texte(projet, u"# version = pymecavideo 8.0\n# lastvideo = %1\n# taille_image = (800,600)\n# origine = (400, 300)\n"
                            "# sens_x = 1\n# sens_y = 1\n# index_depart = 2\n# etalon_m = 1.0\n# etalon_org = (0, 0)\n"
                            "# etalon_ext = (200, 0)\n# deltat = 0.04\n# nb_obj = 1\n# \n# temps en seconde, positions en mètre\n"
                            "t\tx1\ty1\n0,000\t0,5\t0,25\n0,040\t0,6\t0,25\n"_s.arg(outils::video(u"g1.avi"_s)));
        auto app = fenetre(projet, Mode::Simplifie);
        const App &a = *app;
        QCOMPARE(a.etat, Etat::D);
        QCOMPARE(a.pointage.premiere_image(), 2);
        QCOMPARE(a.pointage.nb_images_pointees(), 2);
        // l'image fait 400 x 300 : l'échelle de l'écran est divisée par deux
        QVERIFY(std::abs(a.pointage.echelle.px_par_m() - 100.0) < 1e-9);
        const Vecteur m = a.pointage.point_en_metre(*a.pointage.position(1, 1));
        QVERIFY2(std::abs(m.x - 0.5) < 1e-9 && std::abs(m.y - 0.25) < 1e-9, qUtf8Printable(m.texte()));
        QCOMPARE(a.guide.etape, Etape::Tableau);
    }

    /// Rien ne se perd sans prévenir : quitter, tout recommencer ou ouvrir un autre fichier avec des
    /// mesures ni copiées ni enregistrées demande confirmation ; après une copie pour Python, plus
    /// rien n'est demandé. Et le travail en cours est sauvegardé automatiquement, puis se récupère.
    void rien_ne_se_perd_sans_prevenir()
    {
        auto app = fenetre(outils::video(u"g1.avi"_s), Mode::Expert);
        App &a = *app;
        const QString secours = prefs::fichier_secours();
        QFile::remove(secours);
        // sans mesure, aucune question
        QVERIFY(a.travail_en_securite());
        QVERIFY(clique(a, u"Tout recommencer"_s));
        QVERIFY(!affiche(a, u"Tout recommencer ?"_s));
        QVERIFY(clique(a, u"Démarrer le pointage"_s));
        for (int i = 0; i < 3; ++i)
            clic_image(a, {100.0 + 10.0 * i, 150.0});
        QVERIFY(!a.travail_en_securite());
        // la sauvegarde automatique est faite dès qu'il y a des mesures
        QVERIFY2(QFileInfo(secours).isFile(), "sauvegarde de secours absente");
        const QString texte = lit_texte(secours);
        QVERIFY2(texte.startsWith(u"# version = mevcine"_s), qUtf8Printable(texte));
        QVERIFY(texte.contains(u"g1.avi"_s));
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
        a.confirme_si_mesures({Apres::Type::OuvrirFichier, outils::video(u"Principe_inertie.avi"_s)});
        attend();
        QVERIFY(affiche(a, u"Abandonner les mesures ?"_s));
        QVERIFY(clique(a, u"Annuler"_s));
        QCOMPARE(a.pointage.nb_images_pointees(), 3);
        QCOMPARE(a.image_max, 16); // la vidéo n'a pas changé
        // la copie pour Python met les mesures en sécurité : plus de question
        onglet(a, u"Coordonnées"_s);
        QVERIFY(clique(a, u"📋 Copier pour Python"_s));
        QVERIFY(a.travail_en_securite());
        // une mesure de plus : de nouveau en danger, et la sauvegarde suit
        onglet(a, u"Pointage"_s);
        clic_image(a, {140, 150});
        QVERIFY(!a.travail_en_securite());
        a.sauvegarde_de_secours();
        QVERIFY2(lit_texte(secours) != texte, "la sauvegarde doit avoir suivi");
        // la récupération, dans une nouvelle session
        auto app2 = fenetre(secours, Mode::Expert, std::nullopt, true);
        QCOMPARE(app2->pointage.nb_images_pointees(), 4);
        QCOMPARE(app2->etat, Etat::D);
        QVERIFY2(app2->travail_en_securite(), "ce qui vient d'un fichier ne risque rien");
        // le menu Fichier propose la récupération
        QVERIFY(menu(*app2, u"Récupérer le travail précédent…"_s));
        // après une copie, on quitte sans question
        a.copie_python();
        a.close();
        attend();
        QVERIFY(!a.isVisible());
        QVERIFY(a.dialogues.empty());
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
        // un clic sur une carte de l'accueil, cachée par l'écran de démarrage, ne choisit pas de mode :
        // il fait seulement disparaître l'écran
        QAbstractButton *carte = bouton(*app, u"Mode simplifié"_s);
        QVERIFY(carte);
        QTest::mouseClick(carte, Qt::LeftButton);
        attend();
        QVERIFY(!app->ecran_demarrage());
        QCOMPARE(app->mode, std::nullopt);
        // sans clic, il disparaît en moins de trois secondes
        auto app2 = std::make_unique<App>(QString(), std::nullopt);
        app2->show();
        QVERIFY(app2->ecran_demarrage());
        QTest::qWait(1500);
        QVERIFY(app2->ecran_demarrage());
        QVERIFY(QTest::qWaitFor([&] { return !app2->ecran_demarrage(); }, 2500));
    }

    /// Le bouton du drapeau change la langue de l'interface aussitôt, et la retient dans la
    /// configuration ; le bouton soleil ou lune change le thème, retenu lui aussi
    void boutons_langue_et_theme()
    {
        auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Expert);
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
        for (QAction *m : a.menuBar()->actions())
            menus_fr << m->text();
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
            // quelques points, pour les onglets des mesures
            QVERIFY(clique(a, u"Démarrer le pointage"_s));
            for (int i = 0; i < 5; ++i)
                clic_image(a, {100.0 + 30.0 * i, 150.0});
            onglet(a, u"Trajectoires"_s);
            QCOMPARE(a.onglet, Onglet::Trajectoires);
            a.traj.vecteurs = true;
            a.rafraichit();
            rendu(a, u"67_trajectoires_sombre"_s);
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
        // les tests suivants, et les captures de la notice, restent en thème clair (et non dans le
        // thème du bureau)
        theme::choisit(Qt::ColorScheme::Light);
        attend();
    }

    /// Le mode simplifié dans chaque langue : les étapes se suivent avec les intitulés traduits, et le
    /// texte copié pour Python a ses commentaires dans la langue. En anglais, aucun texte affiché
    /// n'est resté en français.
    void guide_dans_chaque_langue()
    {
        // seuls noms propres accentués permis en anglais
        const QStringList permis = {u"Rémi MEVAERE"_s, u"Français"_s,
                                    u"Language: English. Click to switch to French (français)."_s};
        for (Langue langue : TOUTES_LES_LANGUES) {
            const QString code = i18n::code(langue);
            {
                auto app = fenetre({}, std::nullopt, langue);
                QCOMPARE(i18n::langue(), langue);
                rendu(*app, u"80_%1_accueil"_s.arg(code));
                QVERIFY(clique(*app, trad("Mode simplifié")));
                QVERIFY(bouton(*app, trad("📂 Ouvrir une vidéo…")));
                rendu(*app, u"91_%1_sans_video"_s.arg(code));
            }
            auto app = fenetre(outils::video(u"Principe_inertie.avi"_s), Mode::Simplifie, langue);
            App &a = *app;
            QStringList textes = textes_affiches(a);
            rendu(a, u"84_%1_origine"_s.arg(code));
            clic_image(a, {45, 188});
            QVERIFY(clique(a, trad("Suivant ▶")));
            textes << textes_affiches(a);
            QVERIFY(clique(a, trad("x vers la droite") + u", "_s + trad("y vers le haut")));
            rendu(a, u"81_%1_axes"_s.arg(code));
            QVERIFY(clique(a, trad("Suivant ▶")));
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
            QVERIFY(clique(a, trad("Suivant ▶")));
            for (int i = 0; i < 4; ++i)
                clic_image(a, {365.0 - 16.0 * i, 110.0});
            textes << textes_affiches(a);
            rendu(a, u"85_%1_pointage"_s.arg(code));
            // les nombres du tableau suivent la langue : 0.040 en anglais, 0,040 ailleurs
            const Tableau t = Tableau::construit(a.pointage);
            QCOMPARE(t.lignes[1][0], langue == Langue::Anglais ? u"0.040"_s : u"0,040"_s);
            QVERIFY(clique(a, trad("Terminer le pointage ▶")));
            QVERIFY(clique(a, trad("Suivant ▶")));
            QCOMPARE(a.guide.etape, Etape::Export);
            QVERIFY(clique(a, trad("📋 Copier pour Python")));
            const QString texte = a.presse_papier;
            QVERIFY2(texte.contains(trad("Mesures MeVCine — vidéo : {}", "Principe_inertie.avi")), qUtf8Printable(texte));
            QVERIFY2(texte.contains(u"lst_t = np.array([0.0, 0.04, 0.08, 0.12])"_s), qUtf8Printable(texte));
            verifie_python(texte, u"assert len(lst_x) == 4"_s);
            textes << textes_affiches(a);
            rendu(a, u"83_%1_export"_s.arg(code));
            // le mode expert, avec les mêmes mesures : ses quatre onglets
            a.change_mode(Mode::Expert);
            a.rafraichit();
            textes << textes_affiches(a);
            rendu(a, u"86_%1_expert_pointage"_s.arg(code));
            onglet(a, trad("Trajectoires"));
            a.traj.vecteurs = true;
            a.rafraichit();
            textes << textes_affiches(a);
            rendu(a, u"87_%1_expert_trajectoires"_s.arg(code));
            onglet(a, trad("Coordonnées"));
            textes << textes_affiches(a);
            rendu(a, u"88_%1_expert_coordonnees"_s.arg(code));
            onglet(a, trad("Grapheur"));
            a.graph.x = u"t"_s;
            a.graph.y = u"x"_s;
            a.graph.style = 1;
            a.rafraichit();
            textes << textes_affiches(a);
            rendu(a, u"89_%1_expert_grapheur"_s.arg(code));
            // une mesure de plus, puis la question avant de quitter
            onglet(a, trad("Pointage"));
            clic_image(a, {301, 112});
            QVERIFY2(!a.travail_en_securite(), qUtf8Printable(code));
            a.confirme_si_mesures({Apres::Type::Quitter});
            attend();
            QVERIFY2(affiche(a, trad("Quitter MeVCine ?")), qUtf8Printable(code));
            textes << textes_affiches(a);
            rendu(a, u"90_%1_confirmation"_s.arg(code));
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
        }
        i18n::choisit(Langue::Francais);
    }
};

QTEST_MAIN(TestsInterface)
#include "tests_interface.moc"
