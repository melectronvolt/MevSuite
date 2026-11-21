// Menus, raccourcis et boutons de la barre de menus.
#include "app.h"

#include "about.h"
#include "signalplot.h"
#include "spectrumplot.h"
#include "style.h"

#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QShortcut>
#include <QTabBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

void MainWindow::installe_raccourcis()
{
    for (const QKeySequence &keys :
         {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab),
          QKeySequence(Qt::CTRL | Qt::Key_PageDown), QKeySequence(Qt::CTRL | Qt::Key_PageUp)}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, [this] {
            if (m_tabs->isVisible())
                m_tabs->setCurrentIndex(1 - m_tabs->currentIndex());
        });
    }

    // Espace : écouter, ou mettre en pause (sauf pendant une saisie, qui garde la touche).
    connect(new QShortcut(Qt::Key_Space, this), &QShortcut::activated, this, [this] {
        if (m_session.loaded)
            bascule_lecture();
    });
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
}

void MainWindow::construit_actions()
{
    m_actOpen = new QAction(this);
    m_actOpen->setIcon(icons::dossier_ouvert());
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::ouvre_dialogue);
    m_actExportSpectrum = new QAction(this);
    connect(m_actExportSpectrum, &QAction::triggered, this, &MainWindow::exporte_spectre);
    m_actExportExtract = new QAction(this);
    connect(m_actExportExtract, &QAction::triggered, this, &MainWindow::exporte_extrait);
    m_actQuit = new QAction(this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    m_actQuit->setMenuRole(QAction::QuitRole);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    m_actFitAll = new QAction(icons::commande(icons::Commande::ToutVoir), QString(), this);
    connect(m_actFitAll, &QAction::triggered, this, [this] {
        m_signal->reinitialise_vue();
        m_spectrum->reinitialise_vue();
    });
    m_actZoomSelection = new QAction(icons::commande(icons::Commande::ZoomExtrait), QString(), this);
    connect(m_actZoomSelection, &QAction::triggered, this, [this] {
        const auto [a, b] = trie(m_session.extract);
        const double margin = std::max((b - a) * 0.1, 1e-3);
        m_signal->definit_vue_x(a - margin, b + margin);
    });
    const auto toggle = [this](bool Settings::*field) {
        auto *action = new QAction(this);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, field](bool checked) {
            m_session.params.*field = checked;
            parametres_modifies();
        });
        return action;
    };
    m_actDb = toggle(&Settings::db);
    m_actLog = toggle(&Settings::logFreq);
    m_actMarks = toggle(&Settings::harmonicMarks);
    auto *channels = new QActionGroup(this);
    for (audio::Channel channel : audio::ALL_CHANNELS) {
        QAction *action = channels->addAction(QString());
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, channel] {
            m_session.params.channel = channel;
            parametres_modifies();
        });
        m_actChannels.push_back(action);
    }
    m_actSettings = new QAction(icons::commande(icons::Commande::Reglages), QString(), this);
    connect(m_actSettings, &QAction::triggered, this, &MainWindow::montre_reglages);

    auto *modes = new QActionGroup(this);
    m_actSimple = modes->addAction(icons::commande(icons::Commande::ModeSimple), QString());
    m_actSimple->setCheckable(true);
    connect(m_actSimple, &QAction::triggered, this, [this] { change_mode(false); });
    m_actExpert = modes->addAction(icons::commande(icons::Commande::ModeExpert), QString());
    m_actExpert->setCheckable(true);
    connect(m_actExpert, &QAction::triggered, this, [this] { change_mode(true); });

    // thème et langue : un clic passe à l'autre
    m_actTheme = new QAction(this);
    m_actTheme->setObjectName(u"theme"_s);
    connect(m_actTheme, &QAction::triggered, this, [this] {
        change_theme(est_sombre(QApplication::palette()) ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    });
    m_actLanguage = new QAction(this);
    m_actLanguage->setObjectName(u"language"_s);
    connect(m_actLanguage, &QAction::triggered, this, [this] {
        const auto at = std::ranges::find(ALL_LANGS, m_lang) - ALL_LANGS.begin();
        change_langue(ALL_LANGS[size_t(at + 1) % ALL_LANGS.size()]);
    });

    m_actManual = new QAction(icons::commande(icons::Commande::Notice), QString(), this);
    m_actManual->setShortcut(QKeySequence::HelpContents);
    connect(m_actManual, &QAction::triggered, this, &MainWindow::ouvre_notice);
    m_actAbout = new QAction(QIcon(about::icone()), QString(), this);
    m_actAbout->setMenuRole(QAction::AboutRole);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::montre_a_propos);
    m_actAboutQt = new QAction(icons::etat(icons::Status::Info), QString(), this);
    m_actAboutQt->setMenuRole(QAction::AboutQtRole);
    connect(m_actAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::construit_menus()
{
    // Les menus donnent tout ; le panneau de gauche, ce qui sert le plus.
    m_menuFile = menuBar()->addMenu(QString());
    m_menuFile->addAction(m_actOpen);
    m_actRecord = m_menuFile->addAction(icons::enregistrer(), QString(), this, &MainWindow::enregistre_son);
    m_actSaveTake = m_menuFile->addAction(icons::disquette(), QString(), this, [this] { sauvegarde_prise(); });
    m_actExportSpectrum->setIcon(m_actSaveTake->icon());
    m_actExportExtract->setIcon(m_actSaveTake->icon());
    m_menuAudio = menuBar()->addMenu(u"Audio"_s);
    m_actDevices = m_menuAudio->addAction(icons::commande(icons::Commande::Peripheriques), QString(),
                                          this, &MainWindow::montre_peripheriques);
    m_sepExports = m_menuFile->addSeparator();
    m_menuFile->addAction(m_actExportSpectrum);
    m_menuFile->addAction(m_actExportExtract);
    m_menuFile->addSeparator();
    m_menuFile->addAction(m_actQuit);

    m_menuView = menuBar()->addMenu(QString());
    m_menuView->setToolTipsVisible(true);
    m_menuView->addAction(m_actFitAll);
    m_menuView->addAction(m_actZoomSelection);
    m_memorise = m_menuView->addAction(icons::commande(icons::Commande::MemoriserSpectre), QString(), this,
                                       [this] { m_spectrum->memorise(); rafraichit(); });
    m_oublie = m_menuView->addAction(icons::commande(icons::Commande::RetirerSpectre), QString(), this,
                                     [this] { m_spectrum->oublie(); rafraichit(); });
    m_normalise = m_menuView->addAction(QString());
    m_normalise->setCheckable(true);
    connect(m_normalise, &QAction::toggled, this, [this](bool oui) { m_spectrum->normalise(oui); rafraichit(); });
    m_sepExpertView = m_menuView->addSeparator();
    m_menuView->addAction(m_actDb);
    m_menuView->addAction(m_actLog);
    m_menuView->addAction(m_actMarks);
    m_menuChannel = m_menuView->addMenu(icons::commande(icons::Commande::Voie), QString());
    for (QAction *action : m_actChannels)
        m_menuChannel->addAction(action);
    m_sepSettings = m_menuView->addSeparator();
    m_menuView->addAction(m_actSettings);

    m_menuMode = menuBar()->addMenu(QString());
    m_menuMode->setToolTipsVisible(true);
    m_menuMode->addAction(m_actSimple);
    m_menuMode->addAction(m_actExpert);

    m_menuHelp = menuBar()->addMenu(QString());
    m_menuHelp->addAction(m_actManual);
    m_menuHelp->addAction(m_actAbout);
    m_menuHelp->addAction(m_actAboutQt);

    for (QMenu *menu : {m_menuFile, m_menuAudio, m_menuView, m_menuMode, m_menuHelp}) {
        for (QAction *action : menu->actions()) {
            if (!action->icon().isNull())
                action->setIconVisibleInMenu(true);
        }
    }
}

void MainWindow::construit_coin_des_menus()
{
    // À droite de la barre de menus, sur sa ligne : le thème et la langue. Pas de barre d'outils ;
    // ouvrir et les vues sont en haut du panneau de gauche.
    // Barre de menus dans la fenêtre, même avec un menu global du bureau, qui cacherait ces boutons.
    menuBar()->setNativeMenuBar(false);
    m_menuCorner = new QWidget(menuBar());
    m_menuCorner->setObjectName(u"menuCorner"_s);
    auto *layout = new QHBoxLayout(m_menuCorner);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(2);
    // thème et langue ; la notice est dans le menu Aide (F1), un bouton de plus faisait doublon
    for (QAction *action : {m_actTheme, m_actLanguage}) {
        auto *button = new QToolButton(m_menuCorner);
        button->setObjectName(action->objectName());
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        m_cornerButtons.push_back(button);
    }
    menuBar()->setCornerWidget(m_menuCorner, Qt::TopRightCorner);
}

void MainWindow::dimensionne_coin_des_menus()
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

void MainWindow::retraduit_menus(const Tr &t)
{
    m_menuFile->setTitle(t.menuFile);
    m_actOpen->setText(t.openSound);
    // les boutons du panneau gardent les points de suspension, comme l'accueil et les menus
    m_actOpen->setIconText(t.openSound);
    m_actExportSpectrum->setText(t.exportSpectrum);
    m_actExportExtract->setText(t.exportExtract);
    m_actQuit->setText(t.menuQuit);
    m_menuView->setTitle(t.menuView);

    m_memorise->setText(t.keepSpectrum);
    m_oublie->setText(t.removeSpectrum);
    m_normalise->setText(t.matchPeaks);
    m_actFitAll->setText(t.fitAll);
    m_actZoomSelection->setText(t.zoomSelection);
    m_actDb->setText(t.ampDb);
    m_actLog->setText(t.axisLog);
    m_actMarks->setText(t.harmonicMarks);
    m_actMarks->setToolTip(t.harmonicMarksTip);
    m_menuChannel->setTitle(t.channelMenu);
    for (size_t i = 0; i < m_actChannels.size(); ++i)
        m_actChannels[i]->setText(t.channelNames[i]);
    m_actSettings->setText(t.settingsButton);
    m_actSettings->setIconText(t.settingsButton);
    m_menuMode->setTitle(t.menuMode);
    m_actSimple->setText(t.modeSimple);
    m_actSimple->setToolTip(t.modeSimpleTip);
    m_actExpert->setText(t.modeExpert);
    m_actExpert->setToolTip(t.modeExpertTip);
    m_menuHelp->setTitle(t.menuHelp);
    m_actManual->setText(t.menuManual);
    const bool dark = est_sombre(QApplication::palette());
    m_actTheme->setIcon(icons::theme(dark));
    m_actTheme->setText(dark ? t.themeToLight : t.themeToDark);
    m_actLanguage->setIcon(icons::drapeau(m_lang));
    m_actLanguage->setText(t.switchLanguage);

    m_actRecord->setText(t.recordSound);
    m_actSaveTake->setText(t.saveTake);
    m_actSaveTake->setIconText(t.saveTake);
    m_actDevices->setText(t.audioDevices);
    m_actAbout->setText(t.menuAbout);
    m_actAboutQt->setText(t.menuAboutQt);
}
