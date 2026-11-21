// Construction de la fenêtre principale, préférences et événements du bureau.
#include "app.h"

#include "about.h"
#include "canvas.h"
#include "help.h"
#include "plot.h"
#include "style.h"
#include "theme.h"
#include "tableau_mesures.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibraryInfo>
#include <QMenuBar>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Dimensions minimales du panneau de mesures et du graphique.
constexpr int SIDE_MIN_WIDTH = 240;
constexpr int PLOT_MIN_HEIGHT = 140;


/// Panneau latéral : il défile en hauteur, et n'est jamais plus étroit que son contenu (sinon les
/// textes seraient rognés à droite, sans barre de défilement horizontale).
class SideScroll : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

    QSize minimumSizeHint() const override
    {
        const int content = widget() ? widget()->minimumSizeHint().width() : 0;
        return {std::max(SIDE_MIN_WIDTH, content + verticalScrollBar()->sizeHint().width() + 2 * frameWidth()),
                QScrollArea::minimumSizeHint().height()};
    }
};

/// Fond du panneau latéral, un peu plus sombre que celui de la fenêtre, et filet à sa droite : la
/// séparation avec l'image se voit.
class SideBackground : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QPalette palette = this->palette();
        painter.fillRect(rect(), palette.color(QPalette::Window).darker(est_sombre(palette) ? 125 : 104));
        painter.fillRect(QRect(width() - 1, 0, 1, height()), palette.color(QPalette::Mid));
    }
};

/// Information permanente de la barre d'état, séparée du message par un filet vertical de 1 px
/// couleur Mid et 10 px de marge (une bordure de feuille de style n'y est pas dessinée).
class StatusInfo : public QLabel {
public:
    explicit StatusInfo(QWidget *parent)
        : QLabel(parent)
    {
        setContentsMargins(11, 0, 10, 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QLabel::paintEvent(event);
        QPainter(this).fillRect(QRect(0, 0, 1, height()), QApplication::palette().color(QPalette::Mid));
    }
};

/// Logo du programme (icône 256 px), à la taille `side` en pixels logiques.
QPixmap logo(int side, qreal ratio)
{
    QPixmap pixmap = QPixmap(u":/icons/mevpix-256.png"_s)
                         .scaled(QSize(side, side) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

} // namespace

FenetrePrincipale::FenetrePrincipale(const Reglages &settings, const FichiersReglages &files, const QString &startupPath)
    : m_langChoice(settings.lang)
    , m_lang(resout_langue(settings.lang))
    , m_theme(settings.theme)
    , m_fichiersReglages(files)
    , m_reglagesEnregistres(settings)
    , m_dernierDossier(settings.lastDir)
    , m_windowSize(settings.windowSize)
    , m_maximized(settings.maximized)
    , m_baseFont(QApplication::font())
{
    m_seance.uniteSaisie = settings.unite;
    m_seance.canal = settings.canal;
    m_seance.largeurBande = settings.largeurBande;

    setWindowIcon(QApplication::windowIcon());
    setMinimumSize(MIN_WINDOW);
    setAcceptDrops(true);

    m_canevas = new CanevasImage(m_seance, this);
    m_graphique = new GraphiqueProfil(m_seance, this);
    m_graphique->setMinimumHeight(PLOT_MIN_HEIGHT);

    construit_actions();
    construit_menus();
    construit_barre_outils();
    construit_zoom_sur_image();
    construit_coin_des_menus();
    // Ces actions n'ont pas de menu. Le mode explicite évite que Qt 6.11 raccorde
    // à nouveau le bouton à son action à chaque changement d'état.
    for (auto *bouton : findChildren<QToolButton *>()) {
        if (bouton->defaultAction() && !bouton->defaultAction()->menu())
            bouton->setPopupMode(QToolButton::DelayedPopup);
    }

    auto *side = new SideBackground(this);
    auto *sideLayout = new QHBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 1, 0); // filet de droite
    m_side = new SideScroll(side);
    m_side->setObjectName(u"sidePanel"_s);
    m_side->setWidget(construit_panneau_lateral());
    m_side->setWidgetResizable(true);
    m_side->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_side->setFrameShape(QFrame::NoFrame);
    // fond transparent : celui de `SideBackground`
    m_side->viewport()->setAutoFillBackground(false);
    m_side->widget()->setAutoFillBackground(false);
    m_side->widget()->installEventFilter(this);
    sideLayout->addWidget(m_side);

    m_vsplit = new QSplitter(Qt::Vertical, this);
    m_vsplit->addWidget(m_canevas);
    m_vsplit->addWidget(m_graphique);
    m_vsplit->setCollapsible(0, false);
    m_vsplit->setCollapsible(1, false);
    m_vsplit->setStretchFactor(0, 1);
    m_graphique->hide();

    m_hsplit = new QSplitter(Qt::Horizontal, this);
    m_hsplit->addWidget(side);
    m_hsplit->addWidget(m_vsplit);
    m_hsplit->setCollapsible(0, false);
    m_hsplit->setCollapsible(1, false);
    m_hsplit->setStretchFactor(1, 1);
    m_hsplit->setSizes({LARGEUR_PANNEAU, DEFAULT_WINDOW.width() - LARGEUR_PANNEAU});
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(construit_page_accueil());
    m_pages->addWidget(m_hsplit);
    setCentralWidget(m_pages);

    construit_barre_etat();

    m_voileDepot = new QLabel(this);
    m_voileDepot->setTextFormat(Qt::PlainText);
    m_voileDepot->setAlignment(Qt::AlignCenter);
    m_voileDepot->setWordWrap(true);
    m_voileDepot->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_voileDepot->setStyleSheet(u"background-color: rgba(0,0,0,160); color: white; font-size: 24px;"_s);
    m_voileDepot->hide();

    connect(m_canevas, &CanevasImage::segment_trace, this, &FenetrePrincipale::valide);
    connect(m_canevas, &CanevasImage::profil_selectionne, this, [this] { m_graphique->setFocus(Qt::MouseFocusReason); });
    connect(m_canevas, &CanevasImage::modifie, this, &FenetrePrincipale::trace_modifie);
    connect(m_canevas, &CanevasImage::zoom_change, this, [this](double zoom) {
        m_zoom = zoom;
        rafraichit_infos_etat();
    });
    connect(m_graphique, &GraphiqueProfil::curseurs_deplaces, this, [this] {
        setWindowModified(seance_modifiee());
        m_canevas->update();
        rafraichit_curseurs();
    });
    connect(&m_lecture, &QFutureWatcher<ResultatChargement>::finished, this, &FenetrePrincipale::apres_chargement);
    m_delaiReglages.setSingleShot(true);
    m_delaiReglages.setInterval(1000);
    connect(&m_delaiReglages, &QTimer::timeout, this, &FenetrePrincipale::ecrit_reglages);

    // Échap : abandonne le tracé en cours, où que soit le focus.
    connect(new QShortcut(Qt::Key_Escape, this), &QShortcut::activated, this, [this] {
        m_canevas->annule_glisser();
        m_graphique->annule_glisser();
    });
    auto *undo = new QAction(this);
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, this, [this] {
        termine_saisie();
        m_graphique->termine_clic();
        if (m_canevas->annule_glisser() || m_graphique->annule_glisser())
            return;
        if (m_seance.annule()) {
            rafraichit_tableau_mesures();
            rafraichit();
            reglages_modifies();
        }
    });
    addAction(undo);
    // Ctrl + / Ctrl − / Ctrl 0 : agrandissement de l'interface, borné pour ne pas la rendre inutilisable.
    const auto zoomShortcut = [this](const QKeySequence &keys, auto step) {
        auto *action = new QAction(this);
        action->setShortcut(keys);
        connect(action, &QAction::triggered, this, [this, step] { change_agrandissement(step(m_uiZoom)); });
        addAction(action);
    };
    // agrandir : pas sous l'agrandissement demandé (celui affiché, borné par une fenêtre étroite,
    // peut être plus petit ; le retenu ne doit pas baisser)
    const auto zoomIn = [this](double z) { return std::max(m_uiZoomWanted, std::round(z * 10.0 + 1.0) / 10.0); };
    zoomShortcut(QKeySequence::ZoomIn, zoomIn);
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), zoomIn);
    zoomShortcut(QKeySequence::ZoomOut, [](double z) { return std::round(z * 10.0 - 1.0) / 10.0; });
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), [](double) { return 1.0; });

    m_statut = {tr().statusStart};
    change_langue(m_langChoice);
    change_theme(m_theme);
    affiche_statut(tr().statusStart);
    change_agrandissement(settings.uiZoom);
    rafraichit();

    if (!startupPath.isEmpty())
        QTimer::singleShot(0, this, [this, startupPath] { charge_chemin(startupPath); });
}

// ---------------------------------------------------------------- construction

void FenetrePrincipale::construit_actions()
{
    m_actOpen = new QAction(icons::commande(icons::Commande::Ouvrir), QString(), this);
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, &QAction::triggered, this, &FenetrePrincipale::ouvre_dialogue);

    m_actQuit = new QAction(icons::commande(icons::Commande::Quitter), QString(), this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    m_actQuit->setMenuRole(QAction::QuitRole);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    // Thème : un clic passe à l'autre thème (celui du bureau est suivi jusqu'au premier clic).
    m_actTheme = new QAction(this);
    m_actTheme->setObjectName(u"theme"_s);
    connect(m_actTheme, &QAction::triggered, this, [this] {
        change_theme(est_sombre(QApplication::palette()) ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    });
    // Langue : un clic passe à la langue suivante.
    m_actLanguage = new QAction(this);
    m_actLanguage->setObjectName(u"language"_s);
    connect(m_actLanguage, &QAction::triggered, this, [this] {
        const auto at = std::ranges::find(LANGUES, m_lang) - LANGUES.begin();
        change_langue(LANGUES[size_t(at + 1) % LANGUES.size()]);
    });

    m_actManual = new QAction(icons::commande(icons::Commande::Notice), QString(), this);
    m_actManual->setShortcut(QKeySequence::HelpContents);
    connect(m_actManual, &QAction::triggered, this, &FenetrePrincipale::ouvre_notice);
    m_actAbout = new QAction(QIcon(u":/icons/mevpix-32.png"_s), QString(), this);
    m_actAbout->setMenuRole(QAction::AboutRole);
    connect(m_actAbout, &QAction::triggered, this, &FenetrePrincipale::montre_a_propos);
    m_actAboutQt = new QAction(icons::etat(icons::Status::Info), QString(), this);
    m_actAboutQt->setMenuRole(QAction::AboutQtRole);
    connect(m_actAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    m_actionEtalonner = new QAction(icons::commande(icons::Commande::Etalonner), QString(), this);
    m_actionEtalonner->setCheckable(true);
    connect(m_actionEtalonner, &QAction::triggered, this, [this] { choisit_outil(Outil::Etalonner); });
    m_actionMesurer = new QAction(icons::commande(icons::Commande::Mesurer), QString(), this);
    m_actionMesurer->setCheckable(true);
    connect(m_actionMesurer, &QAction::triggered, this, [this] { choisit_outil(Outil::Mesurer); });
    m_actionProfil = new QAction(icons::commande(icons::Commande::Profil), QString(), this);
    m_actionProfil->setCheckable(true);
    connect(m_actionProfil, &QAction::triggered, this,
            [this] { choisit_outil(m_seance.outil == Outil::Profil ? Outil::Mesurer : Outil::Profil); });
    m_agrandirImage = new QAction(this);
    m_reduireImage = new QAction(this);
    connect(m_agrandirImage, &QAction::triggered, m_canevas, [this] { m_canevas->zoome_au_centre(1.25); });
    connect(m_reduireImage, &QAction::triggered, m_canevas, [this] { m_canevas->zoome_au_centre(1.0 / 1.25); });
    m_ajusterImage = new QAction(this);
    connect(m_ajusterImage, &QAction::triggered, m_canevas, &CanevasImage::demande_ajustement);
}

void FenetrePrincipale::construit_menus()
{
    m_menuFile = menuBar()->addMenu(QString());
    m_menuFile->addAction(m_actOpen);
    m_sauver = m_menuFile->addAction(icons::commande(icons::Commande::Enregistrer), QString(), this, [this] { enregistre_projet(); });
    m_sauver->setShortcut(QKeySequence::Save);
    m_sauverSous = m_menuFile->addAction(icons::commande(icons::Commande::EnregistrerSous), QString(), this, [this] { enregistre_projet(true); });
    m_sauverSous->setShortcut(QKeySequence::SaveAs);
    m_annoter = m_menuFile->addAction(icons::commande(icons::Commande::ImageAnnotee), QString(), this, &FenetrePrincipale::exporte_annotation);
    m_menuFile->addSeparator();
    m_menuFile->addAction(m_actQuit);

    m_menuHelp = menuBar()->addMenu(QString());
    m_menuHelp->addAction(m_actManual);
    m_menuHelp->addAction(m_actAbout);
    m_menuHelp->addAction(m_actAboutQt);
    for (QMenu *menu : {m_menuFile, m_menuHelp}) {
        for (QAction *action : menu->actions())
            action->setIconVisibleInMenu(true);
    }
}

void FenetrePrincipale::construit_barre_outils()
{
    m_toolbar = addToolBar(u"toolbar"_s);
    m_toolbar->setObjectName(u"toolbar"_s);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolbar->addAction(m_actOpen);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actionEtalonner);
    m_toolbar->addAction(m_actionMesurer);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actionProfil);
    // échelle de l'image, à côté des outils qui la donnent et s'en servent (cachée sans image)
    m_scaleSeparator = m_toolbar->addSeparator();
    m_scaleLabel = new QLabel(m_toolbar);
    m_scaleLabel->setObjectName(u"scale"_s);
    m_scaleLabel->setContentsMargins(8, 0, 8, 0);
    m_scaleAction = m_toolbar->addWidget(m_scaleLabel);
}

void FenetrePrincipale::construit_zoom_sur_image()
{
    auto *cadre = new QFrame(m_canevas);
    cadre->setObjectName(u"imageZoomControls"_s);
    cadre->setFrameShape(QFrame::StyledPanel);
    cadre->setBackgroundRole(QPalette::Button);
    cadre->setAutoFillBackground(true);
    cadre->setCursor(Qt::ArrowCursor);
    cadre->setAttribute(Qt::WA_NoMousePropagation);

    m_groupesNavigation = new QBoxLayout(QBoxLayout::LeftToRight, cadre);
    m_groupesNavigation->setContentsMargins(3, 2, 3, 2);
    m_groupesNavigation->setSpacing(5);
    auto *loupes = new QHBoxLayout;
    auto *fleches = new QHBoxLayout;
    for (auto *groupe : {loupes, fleches}) {
        groupe->setSpacing(1);
        m_groupesNavigation->addLayout(groupe);
    }
    const auto ajouteBouton = [&](QHBoxLayout *groupe, QAction *action) {
        auto *bouton = new QToolButton(cadre);
        bouton->setDefaultAction(action);
        bouton->setToolButtonStyle(action == m_ajusterImage ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
        bouton->setAutoRaise(true);
        bouton->setFocusPolicy(Qt::StrongFocus);
        bouton->setIconSize(m_toolbar->iconSize());
        connect(m_toolbar, &QToolBar::iconSizeChanged, bouton, &QToolButton::setIconSize);
        groupe->addWidget(bouton);
        return bouton;
    };
    for (QAction *action : {m_agrandirImage, m_reduireImage, m_ajusterImage})
        ajouteBouton(loupes, action);
    for (size_t i = 0; i < SENS_FLECHES.size(); ++i) {
        auto *action = new QAction(cadre);
        m_actionsDeplacement[i] = action;
        connect(action, &QAction::triggered, m_canevas, [this, sens = SENS_FLECHES[i]] {
            m_canevas->decale_image(sens);
        });
        ajouteBouton(fleches, action)->setAutoRepeat(true);
    }
    auto *position = new QVBoxLayout(m_canevas);
    position->setContentsMargins(8, 8, 8, 8);
    position->addWidget(cadre, 0, Qt::AlignRight | Qt::AlignBottom);
    m_canevas->installEventFilter(this);
}

void FenetrePrincipale::dispose_commandes_image()
{
    const QMargins marges = m_canevas->layout()->contentsMargins() + m_groupesNavigation->contentsMargins()
                            + m_groupesNavigation->parentWidget()->contentsMargins();
    const int bords = marges.left() + marges.right();
    const int loupes = m_groupesNavigation->itemAt(0)->minimumSize().width();
    const int fleches = m_groupesNavigation->itemAt(1)->minimumSize().width();
    // Le séparateur peut réduire la zone image à la largeur d'un seul groupe. Les deux groupes
    // se placent alors sur deux lignes, ce qui laisse sa place au tableau des mesures.
    m_canevas->setMinimumWidth(bords + std::max(loupes, fleches));
    const int largeurEnLigne = bords + loupes + fleches + m_groupesNavigation->spacing();
    const auto sens = m_canevas->width() >= largeurEnLigne ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (m_groupesNavigation->direction() != sens)
        m_groupesNavigation->setDirection(sens);
}

void FenetrePrincipale::construit_coin_des_menus()
{
    // Le thème et la langue restent accessibles à droite des menus.
    // Barre de menus dans la fenêtre, même avec un menu global du bureau, qui cacherait ces boutons.
    menuBar()->setNativeMenuBar(false);
    auto *corner = new QWidget(menuBar());
    corner->setObjectName(u"menuCorner"_s);
    auto *layout = new QHBoxLayout(corner);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(2);
    for (QAction *action : {m_actTheme, m_actLanguage}) {
        auto *button = new QToolButton(corner);
        button->setObjectName(action->objectName());
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        m_cornerButtons.push_back(button);
    }
    menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
}

void FenetrePrincipale::dimensionne_coin_des_menus()
{
    // Boutons de la hauteur des titres des menus : la barre garde sa hauteur standard. Icônes de 20 px
    // × agrandissement, réduites s'il le faut pour tenir dans cette hauteur. (Barre pas encore mise en
    // page : hauteur d'une ligne et marges.)
    int height = menuBar()->actionGeometry(m_menuFile->menuAction()).height();
    if (height <= 0)
        height = QFontMetrics(QApplication::font()).height() + 8;
    const int icon = std::min(static_cast<int>(std::lround(20 * m_uiZoom)), height - 4);
    for (QToolButton *button : m_cornerButtons) {
        button->setIconSize(QSize(icon, icon));
        button->setFixedHeight(height);
    }
    // La barre de menus ne revoit la place du coin que quand on le lui redonne : il garderait sinon sa
    // taille d'avant, trop petite pour les boutons agrandis (comme MeVReg).
    if (QWidget *corner = menuBar()->cornerWidget(Qt::TopRightCorner)) {
        corner->adjustSize();
        menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
    }
    menuBar()->updateGeometry();
}

void FenetrePrincipale::construit_barre_etat()
{
    // À gauche, ce qu'il faut faire ensuite (ou ce qui vient de se passer), avec un pictogramme ; à
    // droite, l'image ouverte et son zoom.
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);
    m_iconeEtat = new QLabel(bar);
    m_iconeEtat->setContentsMargins(6, 0, 0, 0);
    // Un message long (chemin du fichier, formats acceptés…) passe à la ligne au lieu d'être coupé.
    m_messageEtat = new QLabel(bar);
    m_messageEtat->setTextFormat(Qt::PlainText);
    m_messageEtat->setWordWrap(true);
    m_messageEtat->setObjectName(u"statusMessage"_s);
    m_messageEtat->setContentsMargins(0, 4, 0, 4);
    m_messageEtat->installEventFilter(this);
    m_indicateurActivite = new QProgressBar(bar);
    m_indicateurActivite->setRange(0, 0);
    m_indicateurActivite->setMaximumWidth(80);
    m_indicateurActivite->setTextVisible(false);
    m_indicateurActivite->hide();
    m_infoImage = new StatusInfo(bar);
    m_infoImage->setTextFormat(Qt::PlainText);
    m_infoImage->setObjectName(u"imageStatus"_s);
    m_infoZoom = new StatusInfo(bar);
    m_infoZoom->setObjectName(u"zoomStatus"_s);
    bar->addWidget(m_iconeEtat);
    bar->addWidget(m_indicateurActivite);
    bar->addWidget(m_messageEtat, 1);
    bar->addPermanentWidget(m_infoImage);
    bar->addPermanentWidget(m_infoZoom);
}

QWidget *FenetrePrincipale::construit_page_accueil()
{
    // À l’accueil, le bouton Ouvrir permet de commencer une mesure.
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
    layout->addStretch(1);
    auto *icon = new QLabel(page);
    icon->setPixmap(logo(96, devicePixelRatio()));
    layout->addWidget(icon, 0, Qt::AlignHCenter);
    layout->addSpacing(8);
    police_du_style(centered(new QLabel(about::NAME, page)), 2.2, QFont::Bold);
    m_welcomeTagline = centered(new QLabel(page));
    police_du_style(m_welcomeTagline, 1.15);

    layout->addSpacing(32);
    m_welcomeOpen = new QPushButton(page);
    m_welcomeOpen->setIcon(m_actOpen->icon());
    m_welcomeOpen->setObjectName(u"welcomeOpen"_s);
    police_du_style(m_welcomeOpen, 1.1);
    connect(m_welcomeOpen, &QPushButton::clicked, this, &FenetrePrincipale::ouvre_dialogue);
    layout->addWidget(m_welcomeOpen, 0, Qt::AlignHCenter);
    layout->addSpacing(10);
    m_welcomeDropHint = centered(new QLabel(page));
    layout->addStretch(1);
    return page;
}

void FenetrePrincipale::dimensionne_bouton_accueil()
{
    // plus large et plus haut que la normale
    m_welcomeOpen->setMinimumSize(0, 0);
    const QSize hint = m_welcomeOpen->sizeHint();
    m_welcomeOpen->setMinimumSize(hint.width() + hint.height(), int(hint.height() * 1.4));
}

void FenetrePrincipale::change_langue(std::optional<Lang> choice)
{
    m_langChoice = choice;
    m_lang = resout_langue(choice);
    // Textes de Qt lui-même (boutons des boîtes de dialogue, menu contextuel des champs de saisie).
    QApplication::removeTranslator(&m_qtTranslator);
    if (m_qtTranslator.load(u"qtbase_"_s + code_langue(m_lang), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&m_qtTranslator);
    retraduit();
    affiche_statut(tr().languageChanged);
    rafraichit();
    reglages_modifies();
}

void FenetrePrincipale::change_theme(Qt::ColorScheme choice)
{
    m_theme = choice;
    // Le soleil ou la lune changent avec la palette (`changeEvent`).
    theme::choisit(choice);
    reglages_modifies();
}

void FenetrePrincipale::ouvre_notice()
{
    QString error;
    const QString page = help::ecrit(m_lang, &error);
    if (page.isEmpty()) {
        affiche_statut(tr().helpFailed(error), icons::Status::Error);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(page))) {
        affiche_statut(tr().helpFailed(QDir::toNativeSeparators(page)), icons::Status::Error);
        return;
    }
    affiche_statut(tr().helpOpened(QDir::toNativeSeparators(page)), icons::Status::Success);
}

void FenetrePrincipale::montre_a_propos()
{
    DialogueAPropos dialog(tr(), m_fichiersReglages, this);
    dialog.exec();
}

// ---------------------------------------------------------------- réglages

double FenetrePrincipale::agrandissement_max() const
{
    // Le maximum dépend de la largeur de la fenêtre : il reste toujours au moins la largeur minimale
    // (800 points) pour les panneaux et l'image, même sur un écran 1366 × 768.
    if (width() < MIN_WINDOW.width())
        return UI_ZOOM_MAX; // fenêtre pas encore mesurée
    return std::clamp(double(width()) / MIN_WINDOW.width(), 1.0, UI_ZOOM_MAX);
}

void FenetrePrincipale::change_agrandissement(double zoom)
{
    // L'agrandissement demandé est retenu ; celui qui s'affiche est borné par la largeur de la
    // fenêtre, et revient à la demande quand la fenêtre s'élargit (au démarrage, la fenêtre n'a pas
    // encore sa taille : l'agrandissement enregistré se retrouve au premier redimensionnement).
    m_uiZoomWanted = std::clamp(zoom, UI_ZOOM_MIN, UI_ZOOM_MAX);
    applique_agrandissement();
}

void FenetrePrincipale::applique_agrandissement()
{
    m_uiZoom = std::clamp(m_uiZoomWanted, UI_ZOOM_MIN, agrandissement_max());
    QFont font = m_baseFont;
    if (m_baseFont.pointSizeF() > 0.0)
        font.setPointSizeF(m_baseFont.pointSizeF() * m_uiZoom);
    else
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(m_baseFont.pixelSize() * m_uiZoom))));
    QApplication::setFont(font);
    applique_polices();
    m_canevas->change_echelle_interface(m_uiZoom);
    const int icon = static_cast<int>(std::lround(20 * m_uiZoom));
    m_toolbar->setIconSize(QSize(icon, icon));
    m_welcomeOpen->setIconSize(QSize(icon, icon));
    // après que la barre de menus a pris la nouvelle police (hauteur de ses titres)
    QTimer::singleShot(0, this, &FenetrePrincipale::dimensionne_coin_des_menus);
    dimensionne_bouton_accueil();
    // feuille de style de la barre d'état appliquée à nouveau : ses étiquettes prennent la nouvelle
    // police (et le pictogramme, la hauteur d'une ligne)
    style_barre_etat();
    rafraichit_tableau_mesures();
    rafraichit_infos_etat();
    ajuste_panneau_lateral();
    reglages_modifies();
}

void FenetrePrincipale::police_du_style(QWidget *widget, double factor, QFont::Weight weight, bool mono)
{
    m_fonts.push_back({widget, factor, weight, mono});
}

void FenetrePrincipale::applique_polices()
{
    const QFont base = QApplication::font();
    for (const ScaledFont &f : m_fonts) {
        if (!f.widget)
            continue;
        QFont font = f.mono ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : base;
        if (base.pointSizeF() > 0.0)
            font.setPointSizeF(base.pointSizeF() * f.factor);
        else
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(base.pixelSize() * f.factor))));
        font.setWeight(f.weight);
        f.widget->setFont(font);
    }
}

Reglages FenetrePrincipale::reglages_actuels() const
{
    Reglages s;
    s.lang = m_langChoice;
    s.theme = m_theme;
    s.unite = m_seance.uniteSaisie;
    s.canal = m_seance.canal;
    s.largeurBande = m_seance.largeurBande;
    s.uiZoom = m_uiZoomWanted;
    s.lastDir = m_dernierDossier;
    s.windowSize = m_windowSize;
    s.maximized = m_maximized;
    return s;
}

void FenetrePrincipale::reglages_modifies()
{
    if (!m_fichiersReglages.user.isEmpty() && reglages_actuels() != m_reglagesEnregistres)
        m_delaiReglages.start();
}

void FenetrePrincipale::ecrit_reglages()
{
    m_delaiReglages.stop();
    const Reglages current = reglages_actuels();
    if (m_fichiersReglages.user.isEmpty() || current == m_reglagesEnregistres)
        return;
    const QString erreur = current.enregistre(m_fichiersReglages.user, &m_reglagesEnregistres);
    if (!erreur.isEmpty()) {
        if (!m_settingsErrorShown)
            affiche_statut(tr().settingsFailed(erreur), icons::Status::Error);
        m_settingsErrorShown = true;
        // La prochaine modification ou la fermeture réessaiera, par exemple après le retour du réseau.
        return;
    }
    m_reglagesEnregistres = current;
    m_settingsErrorShown = false;
}

void FenetrePrincipale::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) && width() >= 400
        && height() >= 300) {
        m_previousWindowSize = m_windowSize;
        m_windowSize = size();
    }
    if (m_infoImage)
        rafraichit_infos_etat();
    if (m_voileDepot && m_voileDepot->isVisible())
        m_voileDepot->setGeometry(rect());
    if (std::clamp(m_uiZoomWanted, UI_ZOOM_MIN, agrandissement_max()) != m_uiZoom)
        applique_agrandissement();
    reglages_modifies();
}

void FenetrePrincipale::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        m_maximized = isMaximized();
        // Agrandissement signalé après le redimensionnement : la taille notée est celle de l'écran.
        if ((isMaximized() || isFullScreen()) && m_windowSize == size())
            m_windowSize = m_previousWindowSize;
        reglages_modifies();
    } else if (event->type() == QEvent::PaletteChange && m_tableauMesures) {
        // Thème clair ou sombre : couleurs des avertissements et des textes discrets, une fois la
        // nouvelle palette transmise à tous les widgets.
        QTimer::singleShot(0, this, [this] {
            retraduit();
            rafraichit();
        });
    }
}

bool FenetrePrincipale::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canevas && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest))
        dispose_commandes_image();
    // le message a changé de largeur : il tient peut-être sur une ligne de plus ou de moins
    if (watched == m_messageEtat && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &FenetrePrincipale::ajuste_barre_etat);
    // Nouvelle mise en page du panneau (police agrandie, outil, textes) : sa largeur minimale n'est
    // connue qu'à ce moment, après les événements qui suivent le changement.
    if (m_side && watched == m_side->widget() && event->type() == QEvent::LayoutRequest)
        ajuste_panneau_lateral();
    return QMainWindow::eventFilter(watched, event);
}
