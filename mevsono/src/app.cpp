#include "app.h"

#include "about.h"
#include "format.h"
#include "help.h"
#include "savefile.h"
#include "settingsdialog.h"
#include "signalplot.h"
#include "spectrumplot.h"
#include "style.h"
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLibraryInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleOption>
#include <QStylePainter>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Largeur par défaut et minimale du panneau des mesures ; hauteur minimale des graphiques.
constexpr int SIDE_WIDTH = 300;
constexpr int SIDE_MIN_WIDTH = 240;
constexpr int SIGNAL_MIN_HEIGHT = 120;
/// Part de la hauteur donnée au signal au premier affichage.
constexpr double SIGNAL_SHARE = 0.42;
/// Titres du panneau des mesures : taille de la police de l'interface, en demi-gras (`TITLE_WEIGHT`).
constexpr double HEADING_SIZE = 1.0;

QLabel *wrapped(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    return label;
}

void setTextColor(QWidget *widget, const QColor &color)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText, color);
    palette.setColor(QPalette::ButtonText, color);
    widget->setPalette(palette);
}

/// Texte secondaire, plus discret.
QLabel *weak(QWidget *parent)
{
    QLabel *label = wrapped(parent);
    setTextColor(label, QApplication::palette().color(QPalette::PlaceholderText));
    return label;
}

QFrame *separator(QWidget *parent, Qt::Orientation orientation = Qt::Horizontal)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

QToolButton *transportButton(const QString &symbol, const QString &name, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(symbol);
    button->setObjectName(name);
    // l'Espace reste à la lecture et à la pause, même après un clic sur un bouton
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumWidth(34);
    return button;
}

/// Panneau des mesures : il défile en hauteur, et n'est jamais plus étroit que son contenu (sinon les
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

/// Fond du panneau des mesures, un peu plus sombre que celui des graphiques, et filet à sa droite : la
/// séparation des deux volets se voit.
class SideBackground : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QPalette palette = this->palette();
        painter.fillRect(rect(), palette.color(QPalette::Window).darker(isDark(palette) ? 125 : 104));
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

/// Voile affiché pendant qu'on glisse un fichier au-dessus de la fenêtre.
class DropOverlay : public QWidget {
public:
    explicit DropOverlay(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

    QString text;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 160));
        QFont big = font();
        big.setPixelSize(24);
        painter.setFont(big);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, text);
    }
};

} // namespace

MainWindow::MainWindow(const Settings &settings, const SettingsFiles &files, const QString &startupPath)
    : m_lang(resolveLang(settings.lang))
    , m_files(files)
    , m_saved(settings)
    , m_windowSize(settings.windowSize)
    , m_maximized(settings.maximized)
    , m_baseFont(QApplication::font())
{
    m_session.params = settings;
    m_appliedParams = settings;

    setWindowIcon(QApplication::windowIcon());
    setMinimumSize(MIN_WINDOW);
    setAcceptDrops(true);

    m_signal = new SignalPlot(m_session, this);
    m_signal->setMinimumHeight(SIGNAL_MIN_HEIGHT);
    m_spectrum = new SpectrumPlot(m_session, m_analyzer, this);

    buildActions();
    buildMenus();
    buildMenuCorner();

    auto *side = new SideBackground(this);
    auto *sideLayout = new QHBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 1, 0); // filet de droite
    m_side = new SideScroll(side);
    m_side->setObjectName(u"sidePanel"_s);
    m_side->setWidget(buildSidePanel());
    m_side->setWidgetResizable(true);
    m_side->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_side->setFrameShape(QFrame::NoFrame);
    // fond transparent : celui de `SideBackground`
    m_side->viewport()->setAutoFillBackground(false);
    m_side->widget()->setAutoFillBackground(false);
    m_side->widget()->installEventFilter(this);
    sideLayout->addWidget(m_side);

    m_vsplit = new QSplitter(Qt::Vertical, this);
    m_vsplit->addWidget(buildSignalPane());
    m_spectrumPane = buildSpectrumPane();
    m_vsplit->addWidget(m_spectrumPane);
    m_vsplit->setCollapsible(0, false);
    m_vsplit->setCollapsible(1, false);
    m_vsplit->setStretchFactor(1, 1);
    m_vsplit->installEventFilter(this);
    connect(m_vsplit, &QSplitter::splitterMoved, this, [this] { m_splitMoved = true; });

    m_hsplit = new QSplitter(Qt::Horizontal, this);
    m_hsplit->addWidget(side);
    m_hsplit->addWidget(m_vsplit);
    m_hsplit->setCollapsible(0, false);
    m_hsplit->setCollapsible(1, false);
    m_hsplit->setStretchFactor(1, 1);
    m_hsplit->setSizes({SIDE_WIDTH, DEFAULT_WINDOW.width() - SIDE_WIDTH});
    m_pages = new QStackedWidget(this);
    // Les deux activités, en onglets sur toute la largeur, au-dessus du panneau et des graphiques
    auto *work = new QWidget(this);
    auto *workLayout = new QVBoxLayout(work);
    workLayout->setContentsMargins(0, 0, 0, 0);
    workLayout->setSpacing(0);
    m_tabs = new QTabBar(work);
    m_tabs->setObjectName(u"tabs"_s);
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    // au clavier : Tab mène aux onglets (flèches pour changer), et Ctrl + Tab passe à l'autre activité
    m_tabs->setFocusPolicy(Qt::TabFocus);
    for (const QKeySequence &keys :
         {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab),
          QKeySequence(Qt::CTRL | Qt::Key_PageDown), QKeySequence(Qt::CTRL | Qt::Key_PageUp)}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, [this] {
            if (m_tabs->isVisible())
                m_tabs->setCurrentIndex(1 - m_tabs->currentIndex());
        });
    }
    m_tabs->addTab(icons::periodTab(), QString());
    m_tabs->addTab(icons::frequenciesTab(), QString());
    m_tabs->setCurrentIndex(m_session.tool == Tool::Period ? 0 : 1);
    m_spectrumPane->setVisible(m_session.tool == Tool::Extract);
    connect(m_tabs, &QTabBar::currentChanged, this,
            [this](int index) { setTool(index == 0 ? Tool::Period : Tool::Extract); });
    workLayout->addWidget(m_tabs);
    workLayout->addWidget(m_hsplit, 1);
    m_pages->addWidget(buildWelcomePage());
    m_pages->addWidget(work);
    setCentralWidget(m_pages);

    buildStatusBar();

    m_dropOverlay = new DropOverlay(this);

    connect(m_signal, &SignalPlot::extractChanged, this, &MainWindow::refresh);
    connect(m_signal, &SignalPlot::periodChanged, this, [this] {
        refreshSidePanel();
        fitSidePanel();
    });
    connect(m_spectrum, &SpectrumPlot::fundamentalChosen, this, [this](std::optional<double> f0) {
        m_session.manualF0 = f0;
        refresh();
    });
    connect(&m_analyzer, &Analyzer::updated, this, &MainWindow::analysisUpdated);
    connect(&m_loader, &QFutureWatcher<audio::LoadResult>::finished, this, &MainWindow::onLoaded);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &MainWindow::writeSettings);
    m_playTimer.setInterval(16);
    connect(&m_playTimer, &QTimer::timeout, this, &MainWindow::updatePlayback);

    // Espace : écouter, ou mettre en pause (sauf pendant une saisie, qui garde la touche).
    connect(new QShortcut(Qt::Key_Space, this), &QShortcut::activated, this, [this] {
        if (m_session.loaded)
            togglePlayback();
    });
    // Ctrl + / Ctrl − / Ctrl 0 : agrandissement de l'interface, borné pour ne pas la rendre inutilisable.
    const auto zoomShortcut = [this](const QKeySequence &keys, auto step) {
        auto *action = new QAction(this);
        action->setShortcut(keys);
        connect(action, &QAction::triggered, this, [this, step] { setUiZoom(step(m_uiZoom)); });
        addAction(action);
    };
    const auto zoomIn = [](double z) { return std::round(z * 10.0 + 1.0) / 10.0; };
    zoomShortcut(QKeySequence::ZoomIn, zoomIn);
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), zoomIn);
    zoomShortcut(QKeySequence::ZoomOut, [](double z) { return std::round(z * 10.0 - 1.0) / 10.0; });
    zoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), [](double) { return 1.0; });

    setLanguage(settings.lang);
    setTheme(settings.theme);
    setStatus(tr().statusStart);
    setUiZoom(settings.uiZoom);
    refresh();

    if (!startupPath.isEmpty())
        QTimer::singleShot(0, this, [this, startupPath] { loadPath(startupPath); });
}

MainWindow::~MainWindow()
{
    // le son se coupe avant que les échantillons et la fenêtre disparaissent
    m_player.stop();
}

// ---------------------------------------------------------------- construction

void MainWindow::buildActions()
{
    m_actOpen = new QAction(this);
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openDialog);
    m_actExportSpectrum = new QAction(this);
    connect(m_actExportSpectrum, &QAction::triggered, this, &MainWindow::exportSpectrum);
    m_actExportExtract = new QAction(this);
    connect(m_actExportExtract, &QAction::triggered, this, &MainWindow::exportExtract);
    m_actQuit = new QAction(this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    m_actQuit->setMenuRole(QAction::QuitRole);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    m_actFitAll = new QAction(this);
    connect(m_actFitAll, &QAction::triggered, this, [this] {
        m_signal->resetView();
        m_spectrum->resetView();
    });
    m_actZoomSelection = new QAction(this);
    connect(m_actZoomSelection, &QAction::triggered, this, [this] {
        const auto [a, b] = sorted(m_session.extract);
        const double margin = std::max((b - a) * 0.1, 1e-3);
        m_signal->setViewX(a - margin, b + margin);
    });
    const auto toggle = [this](bool Settings::*field) {
        auto *action = new QAction(this);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, field](bool checked) {
            m_session.params.*field = checked;
            paramsEdited();
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
            paramsEdited();
        });
        m_actChannels.push_back(action);
    }
    m_actSettings = new QAction(this);
    connect(m_actSettings, &QAction::triggered, this, &MainWindow::showSettings);

    auto *modes = new QActionGroup(this);
    m_actSimple = modes->addAction(QString());
    m_actSimple->setCheckable(true);
    connect(m_actSimple, &QAction::triggered, this, [this] { setMode(false); });
    m_actExpert = modes->addAction(QString());
    m_actExpert->setCheckable(true);
    connect(m_actExpert, &QAction::triggered, this, [this] { setMode(true); });

    // thème et langue : un clic passe à l'autre
    m_actTheme = new QAction(this);
    m_actTheme->setObjectName(u"theme"_s);
    connect(m_actTheme, &QAction::triggered, this, [this] {
        setTheme(isDark(QApplication::palette()) ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    });
    m_actLanguage = new QAction(this);
    m_actLanguage->setObjectName(u"language"_s);
    connect(m_actLanguage, &QAction::triggered, this, [this] {
        const auto at = std::ranges::find(ALL_LANGS, m_lang) - ALL_LANGS.begin();
        setLanguage(ALL_LANGS[size_t(at + 1) % ALL_LANGS.size()]);
    });

    m_actManual = new QAction(this);
    m_actManual->setShortcut(QKeySequence::HelpContents);
    connect(m_actManual, &QAction::triggered, this, &MainWindow::openHelp);
    m_actAbout = new QAction(this);
    m_actAbout->setMenuRole(QAction::AboutRole);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);
    m_actAboutQt = new QAction(this);
    m_actAboutQt->setMenuRole(QAction::AboutQtRole);
    connect(m_actAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::buildMenus()
{
    // Les menus donnent tout ; le panneau de gauche, ce qui sert le plus.
    m_menuFile = menuBar()->addMenu(QString());
    m_menuFile->addAction(m_actOpen);
    m_sepExports = m_menuFile->addSeparator();
    m_menuFile->addAction(m_actExportSpectrum);
    m_menuFile->addAction(m_actExportExtract);
    m_menuFile->addSeparator();
    m_menuFile->addAction(m_actQuit);

    m_menuView = menuBar()->addMenu(QString());
    m_menuView->setToolTipsVisible(true);
    m_menuView->addAction(m_actFitAll);
    m_menuView->addAction(m_actZoomSelection);
    m_sepExpertView = m_menuView->addSeparator();
    m_menuView->addAction(m_actDb);
    m_menuView->addAction(m_actLog);
    m_menuView->addAction(m_actMarks);
    m_menuChannel = m_menuView->addMenu(QString());
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
}

void MainWindow::buildMenuCorner()
{
    // À droite de la barre de menus, sur sa ligne : la notice, le thème et la langue. Pas de barre
    // d'outils ; ouvrir, les exemples et les vues sont en haut du panneau de gauche.
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

void MainWindow::sizeMenuCorner()
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

void MainWindow::buildStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);
    m_statusIcon = new QLabel(bar);
    m_statusIcon->setContentsMargins(6, 0, 0, 0);
    // Un message long (chemin du fichier, formats acceptés…) passe à la ligne.
    m_statusLabel = wrapped(bar);
    m_statusLabel->setObjectName(u"statusMessage"_s);
    m_statusLabel->setContentsMargins(0, 4, 0, 4);
    m_statusLabel->installEventFilter(this);
    m_busy = new QProgressBar(bar);
    m_busy->setRange(0, 0);
    m_busy->setMaximumWidth(80);
    m_busy->setTextVisible(false);
    m_busy->hide();
    m_soundStatus = new StatusInfo(bar);
    m_soundStatus->setObjectName(u"soundStatus"_s);
    m_modeStatus = new StatusInfo(bar);
    m_modeStatus->setObjectName(u"modeStatus"_s);
    bar->addWidget(m_statusIcon);
    bar->addWidget(m_busy);
    bar->addWidget(m_statusLabel, 1);
    bar->addPermanentWidget(m_soundStatus);
    bar->addPermanentWidget(m_modeStatus);
}

QWidget *MainWindow::buildWelcomePage()
{
    // Épurée : le logiciel, puis ouvrir un son ou essayer un exemple ; la notice, la langue et le
    // thème sont à droite de la barre de menus.
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
    auto *logo = new QLabel(page);
    logo->setPixmap(about::icon().scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(8);
    styleFont(centered(new QLabel(about::NAME, page)), 2.2, QFont::Bold);
    m_welcomeTagline = centered(new QLabel(page));
    styleFont(m_welcomeTagline, 1.15);

    layout->addSpacing(32);
    m_welcomeOpen = new QPushButton(page);
    m_welcomeOpen->setObjectName(u"welcomeOpen"_s);
    connect(m_welcomeOpen, &QPushButton::clicked, this, &MainWindow::openDialog);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    buttons->addStretch();
    // pas de sons d'exemple : on ouvre ses propres fichiers
    styleFont(m_welcomeOpen, 1.1);
    buttons->addWidget(m_welcomeOpen);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addSpacing(10);
    m_welcomeDropHint = centered(new QLabel(page));
    layout->addStretch(1);
    return page;
}

void MainWindow::sizeWelcomeButtons()
{
    // le bouton plus large et plus haut que la normale
    m_welcomeOpen->setMinimumSize(0, 0);
    const QSize hint = m_welcomeOpen->sizeHint();
    m_welcomeOpen->setMinimumSize(hint.width() + hint.height(), int(hint.height() * 1.4));
}

QWidget *MainWindow::buildSignalPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(4, 4, 4, 0);
    auto *header = new QHBoxLayout;
    m_signalTitle = new QLabel(pane);
    styleFont(m_signalTitle, 1.0, TITLE_WEIGHT);
    header->addWidget(m_signalTitle);
    m_signalHint = weak(pane);
    header->addWidget(m_signalHint, 1);
    layout->addLayout(header);
    layout->addWidget(m_signal, 1);
    return pane;
}

QWidget *MainWindow::buildSpectrumPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *header = new QHBoxLayout;
    m_spectrumTitle = new QLabel(pane);
    styleFont(m_spectrumTitle, 1.0, TITLE_WEIGHT);
    header->addWidget(m_spectrumTitle);
    m_spectrumHint = weak(pane);
    header->addWidget(m_spectrumHint, 1);
    layout->addLayout(header);
    layout->addWidget(m_spectrum, 1);
    return pane;
}

QWidget *MainWindow::buildSidePanel()
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);

    // ouvrir un son, les vues, les réglages (mode expert) : deux boutons par ligne
    auto *actions = new QGridLayout;
    actions->setSpacing(6);
    const auto actionButton = [panel](QToolButton *button, QAction *action) {
        if (action)
            button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFocusPolicy(Qt::NoFocus);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return button;
    };
    actions->addWidget(actionButton(new QToolButton(panel), m_actOpen), 0, 0);
    actions->addWidget(actionButton(new QToolButton(panel), m_actFitAll), 0, 1);
    m_zoomButton = actionButton(new QToolButton(panel), m_actZoomSelection);
    actions->addWidget(m_zoomButton, 1, 0);
    m_settingsButton = actionButton(new QToolButton(panel), m_actSettings);
    actions->addWidget(m_settingsButton, 1, 1);
    actions->setColumnStretch(0, 1);
    actions->setColumnStretch(1, 1);
    layout->addLayout(actions);
    layout->addSpacing(4);
    layout->addWidget(separator(panel));

    m_soundBox = new QWidget(panel);
    auto *box = new QVBoxLayout(m_soundBox);
    box->setContentsMargins(0, 0, 0, 0);
    m_soundName = wrapped(m_soundBox);
    styleFont(m_soundName, 1.0, TITLE_WEIGHT);
    m_soundInfo = weak(m_soundBox);
    box->addWidget(m_soundName);
    box->addWidget(m_soundInfo);

    // écoute : ce qu'on écoute (mode expert), lecture, pause, arrêt
    auto *transport = new QHBoxLayout;
    transport->addWidget(new QLabel(u"🔊"_s, m_soundBox));
    m_listenCombo = new QComboBox(m_soundBox);
    m_listenCombo->setObjectName(u"listen"_s);
    m_listenCombo->addItems({QString(), QString()});
    m_listenLabel = new QLabel(m_soundBox);
    m_playButton = transportButton(u"▶"_s, u"play"_s, m_soundBox);
    m_pauseButton = transportButton(u"⏸"_s, u"pause"_s, m_soundBox);
    m_stopButton = transportButton(u"⏹"_s, u"stop"_s, m_soundBox);
    transport->addWidget(m_listenCombo);
    transport->addWidget(m_listenLabel);
    transport->addWidget(m_playButton);
    transport->addWidget(m_pauseButton);
    transport->addWidget(m_stopButton);
    transport->addStretch();
    box->addSpacing(4);
    box->addLayout(transport);
    box->addSpacing(4);
    box->addWidget(separator(m_soundBox));

    // Mesure de la période : les instants et leur écart ; le calcul de la période revient à l'élève
    m_periodSection = new QWidget(m_periodSection);
    auto *periodSection = new QVBoxLayout(m_periodSection);
    periodSection->setContentsMargins(0, 0, 0, 0);
    m_periodHeading = wrapped(m_periodSection);
    styleFont(m_periodHeading, HEADING_SIZE, TITLE_WEIGHT);
    periodSection->addWidget(m_periodHeading);
    m_periodBox = new QWidget(m_periodSection);
    auto *period = new QGridLayout(m_periodBox);
    period->setContentsMargins(0, 0, 0, 0);
    period->setHorizontalSpacing(12);
    // leur couleur est donnée par `retranslate`, qui suit le thème
    const auto cursorName = [&](const QString &name, QWidget *parent) {
        auto *label = new QLabel(name, parent);
        styleFont(label, 1.0, TITLE_WEIGHT);
        return label;
    };
    m_t1Value = new QLabel(m_periodBox);
    m_t2Value = new QLabel(m_periodBox);
    m_deltaValue = new QLabel(m_periodBox);
    styleFont(m_deltaValue, 1.0);
    m_t1Name = cursorName(u"t₁"_s, m_periodBox);
    m_t2Name = cursorName(u"t₂"_s, m_periodBox);
    period->addWidget(m_t1Name, 0, 0);
    period->addWidget(m_t1Value, 0, 1);
    period->addWidget(m_t2Name, 1, 0);
    period->addWidget(m_t2Value, 1, 1);
    period->addWidget(new QLabel(u"Δt = t₂ − t₁"_s, m_periodBox), 2, 0);
    period->addWidget(m_deltaValue, 2, 1);
    period->setColumnStretch(1, 1);
    periodSection->addWidget(m_periodBox);
    m_periodHint = weak(m_periodSection);
    periodSection->addWidget(m_periodHint);
    m_snapCheck = new QCheckBox(m_periodSection);
    m_snapCheck->setObjectName(u"snap"_s);
    periodSection->addWidget(m_snapCheck);
    box->addWidget(m_periodSection);

    // Étude des fréquences : l'extrait analysé et son spectre ; en mode expert, la finesse de l'analyse,
    // la fondamentale et les harmoniques
    m_frequencySection = new QWidget(m_frequencySection);
    auto *frequencies = new QVBoxLayout(m_frequencySection);
    frequencies->setContentsMargins(0, 0, 0, 0);
    m_extractHeading = wrapped(m_frequencySection);
    styleFont(m_extractHeading, HEADING_SIZE, TITLE_WEIGHT);
    frequencies->addWidget(m_extractHeading);
    auto *extract = new QGridLayout;
    extract->setHorizontalSpacing(12);
    m_aValue = new QLabel(m_frequencySection);
    m_bValue = new QLabel(m_frequencySection);
    m_aName = cursorName(u"A"_s, m_frequencySection);
    m_bName = cursorName(u"B"_s, m_frequencySection);
    extract->addWidget(m_aName, 0, 0);
    extract->addWidget(m_aValue, 0, 1);
    extract->addWidget(m_bName, 1, 0);
    extract->addWidget(m_bValue, 1, 1);
    extract->setColumnStretch(1, 1);
    frequencies->addLayout(extract);
    m_extractInfo = wrapped(m_frequencySection);
    frequencies->addWidget(m_extractInfo);
    // mode expert : la période trouvée par le logiciel dans l'extrait, en une ligne (l'explication dans
    // la bulle)
    m_autocorrValue = wrapped(m_frequencySection);
    frequencies->addWidget(m_autocorrValue);
    m_computingBox = new QWidget(m_frequencySection);
    auto *computing = new QHBoxLayout(m_computingBox);
    computing->setContentsMargins(0, 0, 0, 0);
    auto *spinner = new QProgressBar(m_computingBox);
    spinner->setRange(0, 0);
    spinner->setTextVisible(false);
    spinner->setMaximumWidth(40);
    m_computing = new QLabel(m_computingBox);
    computing->addWidget(spinner);
    computing->addWidget(m_computing, 1);
    frequencies->addWidget(m_computingBox);
    m_tooShort = weak(m_frequencySection);
    frequencies->addWidget(m_tooShort);
    m_reticleHint = weak(m_frequencySection);
    frequencies->addSpacing(4);
    frequencies->addWidget(m_reticleHint);

    // mode expert : finesse de l'analyse (le détail de la FFT dans les bulles), fondamentale et
    // harmoniques ; ni consignes ni explications, que donnent les bulles et la ligne d'aide du spectre
    m_expertBox = new QWidget(m_frequencySection);
    auto *expert = new QVBoxLayout(m_expertBox);
    expert->setContentsMargins(0, 0, 0, 0);
    auto *resolution = new QGridLayout;
    resolution->setHorizontalSpacing(12);
    m_dfLabel = new QLabel(m_expertBox);
    m_dfValue = new QLabel(m_expertBox);
    m_resolutionLabel = new QLabel(m_expertBox);
    m_resolutionValue = new QLabel(m_expertBox);
    resolution->addWidget(m_dfLabel, 0, 0);
    resolution->addWidget(m_dfValue, 0, 1);
    resolution->addWidget(m_resolutionLabel, 1, 0);
    resolution->addWidget(m_resolutionValue, 1, 1);
    resolution->setColumnStretch(1, 1);
    expert->addLayout(resolution);
    expert->addSpacing(6);
    expert->addWidget(separator(m_expertBox));
    m_fundamentalHeading = wrapped(m_expertBox);
    styleFont(m_fundamentalHeading, HEADING_SIZE, TITLE_WEIGHT);
    expert->addWidget(m_fundamentalHeading);
    // f₁, et sur la même ligne, trouvée automatiquement ou choisie à la main
    auto *f0Line = new QHBoxLayout;
    m_f0Value = new QLabel(m_expertBox);
    m_f0Auto = new QLabel(m_expertBox);
    m_f0Manual = new QLabel(m_expertBox);
    f0Line->addWidget(m_f0Value);
    f0Line->addWidget(m_f0Auto);
    f0Line->addWidget(m_f0Manual);
    f0Line->addStretch();
    expert->addLayout(f0Line);
    m_f0ManualBox = new QWidget(m_expertBox);
    auto *manual = new QHBoxLayout(m_f0ManualBox);
    manual->setContentsMargins(0, 0, 0, 0);
    m_autoButton = new QPushButton(m_f0ManualBox);
    manual->addWidget(m_autoButton);
    manual->addStretch();
    expert->addWidget(m_f0ManualBox);
    m_f0None = weak(m_expertBox);
    expert->addWidget(m_f0None);
    // Tout le panneau défile : le tableau prend la hauteur de ses lignes, sans défilement propre.
    m_harmonicsTable = new QTableWidget(0, 5, m_expertBox);
    m_harmonicsTable->setObjectName(u"harmonics"_s);
    m_harmonicsTable->verticalHeader()->hide();
    m_harmonicsTable->setAlternatingRowColors(true);
    m_harmonicsTable->setShowGrid(false);
    m_harmonicsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_harmonicsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_harmonicsTable->setFocusPolicy(Qt::NoFocus);
    m_harmonicsTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_harmonicsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_harmonicsTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // colonnes réglables à la main ; la dernière prend la place qui reste
    m_harmonicsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_harmonicsTable->horizontalHeader()->setStretchLastSection(true);
    m_harmonicsTable->horizontalHeader()->setHighlightSections(false);
    expert->addWidget(m_harmonicsTable);
    m_copyTable = new QPushButton(m_expertBox);
    expert->addWidget(m_copyTable, 0, Qt::AlignLeft);
    m_f0NoPeak = weak(m_expertBox);
    expert->addWidget(m_f0NoPeak);
    frequencies->addWidget(m_expertBox);
    box->addWidget(m_frequencySection);

    layout->addWidget(m_soundBox);
    layout->addStretch();

    connect(m_listenCombo, &QComboBox::activated, this,
            [this](int index) { setListen(index == 0 ? Listen::Sound : Listen::Extract); });
    connect(m_playButton, &QToolButton::clicked, this, &MainWindow::play);
    connect(m_pauseButton, &QToolButton::clicked, this, [this] {
        m_player.pause();
        updatePlayback();
    });
    connect(m_stopButton, &QToolButton::clicked, this, [this] {
        m_player.stop();
        updatePlayback();
    });
    connect(m_snapCheck, &QCheckBox::clicked, this, [this](bool checked) {
        m_session.params.snapZero = checked;
        paramsEdited();
    });
    connect(m_autoButton, &QPushButton::clicked, this, [this] {
        m_session.manualF0.reset();
        refresh();
    });
    connect(m_copyTable, &QPushButton::clicked, this, &MainWindow::copyTable);
    return panel;
}

// ---------------------------------------------------------------- textes et état

void MainWindow::retranslate()
{
    const Tr &t = tr();
    m_menuFile->setTitle(t.menuFile);
    m_actOpen->setText(t.openSound);
    // les boutons du panneau gardent les points de suspension, comme l'accueil et les menus
    m_actOpen->setIconText(t.openSound);
    if (m_example && m_session.loaded)
        m_session.loaded->audio.name = t.exampleNames[*m_example];
    m_actExportSpectrum->setText(t.exportSpectrum);
    m_actExportExtract->setText(t.exportExtract);
    m_actQuit->setText(t.menuQuit);
    m_menuView->setTitle(t.menuView);
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
    const bool dark = isDark(QApplication::palette());
    m_actTheme->setIcon(icons::theme(dark));
    m_actTheme->setText(dark ? t.themeToLight : t.themeToDark);
    m_actLanguage->setIcon(icons::flag(m_lang));
    m_actLanguage->setText(t.switchLanguage);

    m_welcomeTagline->setText(t.tagline);
    m_welcomeOpen->setText(t.openSound);
    m_welcomeDropHint->setText(t.welcomeDropHint);
    sizeWelcomeButtons();
    m_actAbout->setText(t.menuAbout);
    m_actAboutQt->setText(t.menuAboutQt);

    m_signalTitle->setText(t.signalTitle);
    m_tabs->setTabText(0, t.tabPeriod);
    m_tabs->setTabToolTip(0, t.tabPeriodTip);
    m_tabs->setTabText(1, t.tabFrequencies);
    m_tabs->setTabToolTip(1, t.tabFrequenciesTip);
    m_spectrumTitle->setText(t.spectrumTitle);

    m_listenCombo->setItemText(0, t.listenSound);
    m_listenCombo->setItemText(1, t.listenExtract);
    m_listenCombo->setToolTip(t.listenTip);
    m_playButton->setToolTip(t.playTip);
    m_pauseButton->setToolTip(t.pauseTip);
    m_stopButton->setToolTip(t.stopTip);
    m_periodHeading->setText(t.periodHeading);
    m_periodHint->setText(t.periodHint);
    m_snapCheck->setText(t.snapZero);
    m_snapCheck->setToolTip(t.snapZeroTip);
    m_autocorrValue->setToolTip(t.autocorrelationTip);
    m_extractHeading->setText(t.extractHeading);
    m_computing->setText(t.computing);
    m_tooShort->setText(t.extractTooShort);
    m_reticleHint->setText(t.reticleHint);
    m_dfLabel->setText(t.dfLabel);
    m_resolutionLabel->setText(t.resolutionLabel);
    m_resolutionLabel->setToolTip(t.resolutionTip);
    m_fundamentalHeading->setText(t.fundamentalHeading);
    m_f0Manual->setText(t.f0Manual);
    m_autoButton->setText(t.autoButton);
    m_f0Auto->setText(t.f0Auto);
    m_f0None->setText(t.f0None);
    for (QLabel *label : {m_f0Value, m_f0Auto, m_f0Manual})
        label->setToolTip(t.f0RightClick);
    m_harmonicsTable->setHorizontalHeaderLabels({t.colRank, t.colFreq, t.colRatio, t.colAmp, t.colRel});
    m_copyTable->setText(t.copyTable);
    m_f0NoPeak->setText(t.f0NoPeak);
    static_cast<DropOverlay *>(m_dropOverlay)->text = t.dropHint;

    // couleurs des textes discrets (le thème a pu changer)
    const QColor placeholder = QApplication::palette().color(QPalette::PlaceholderText);
    for (QLabel *label : {m_signalHint, m_spectrumHint, m_soundInfo, m_periodHint, m_tooShort, m_reticleHint,
                          m_f0Manual, m_f0Auto, m_f0None, m_f0NoPeak, m_welcomeTagline, m_welcomeDropHint})
        setTextColor(label, placeholder);
    // couleurs des curseurs, plus foncées en thème clair
    const QPalette pal = QApplication::palette();
    setTextColor(m_t1Name, cursorColorT(pal));
    setTextColor(m_t2Name, cursorColorT(pal));
    setTextColor(m_aName, cursorColorA(pal));
    setTextColor(m_bName, cursorColorB(pal));
    // En sombre, le style dessine le bord des champs (liste d'écoute, case, tableau) un peu plus foncé
    // que le fond de la fenêtre : sur le fond du panneau, il ne se voyait plus. Fond de référence plus
    // clair pour ce dessin, dans le panneau seulement (le panneau garde le fond de `SideBackground`).
    QPalette fields;
    if (isDark(pal))
        fields.setColor(QPalette::Window, pal.color(QPalette::Light));
    m_side->widget()->setPalette(fields);
    styleStatusBar();

    m_signal->setTr(&t);
    m_spectrum->setTr(&t);
    if (m_settingsDialog)
        m_settingsDialog->retranslate(t);
}

void MainWindow::refresh()
{
    requestAnalysis();
    const Tr &t = tr();
    const Settings &p = m_session.params;
    const bool expert = p.expert;
    const bool loaded = m_session.loaded.has_value();
    const bool stereo = loaded && m_session.loaded->audio.channels.size() >= 2;

    setWindowTitle(u"%1 — %2"_s.arg(about::NAME, loaded ? m_session.loaded->audio.name : t.subtitle));
    if (loaded && m_pages->currentIndex() == 0) {
        m_pages->setCurrentIndex(1);
        // Largeur du panneau des mesures, donnée au premier affichage de l'analyse : avant, le volet,
        // caché derrière l'accueil, n'a pas sa taille, et QSplitter répartirait en proportion.
        if (!m_sideSized) {
            m_sideSized = true;
            QTimer::singleShot(0, this, [this] {
                const int side = std::max({SIDE_WIDTH, fontMetrics().averageCharWidth() * 38,
                                           m_side->parentWidget()->minimumSizeHint().width()});
                m_hsplit->setSizes({side, std::max(m_hsplit->width() - side, 1)});
            });
        }
    } else if (!loaded) {
        m_pages->setCurrentIndex(0);
    }
    m_actOpen->setEnabled(!busy());
    m_welcomeOpen->setEnabled(!busy());
    m_sepExports->setVisible(expert);
    m_actExportSpectrum->setVisible(expert);
    m_actExportExtract->setVisible(expert);
    // pendant le calcul d'un nouvel extrait, le spectre affiché est encore l'ancien : pas d'export
    m_actExportSpectrum->setEnabled(analysis() && !busy() && !m_analyzer.pending());
    m_actExportExtract->setEnabled(loaded && !busy());
    m_actFitAll->setEnabled(loaded);
    const bool frequencies = m_session.tool == Tool::Extract;
    m_actZoomSelection->setEnabled(loaded && frequencies);
    m_zoomButton->setVisible(frequencies);
    for (QAction *action :
         {m_sepExpertView, m_actDb, m_actLog, m_actMarks, m_sepSettings, m_actSettings})
        action->setVisible(expert);
    m_settingsButton->setVisible(expert);
    m_menuChannel->menuAction()->setVisible(expert && stereo);
    m_actDb->setChecked(p.db);
    m_actLog->setChecked(p.logFreq);
    m_actMarks->setChecked(p.harmonicMarks);
    for (size_t i = 0; i < m_actChannels.size(); ++i)
        m_actChannels[i]->setChecked(audio::ALL_CHANNELS[i] == p.channel);
    m_actSimple->setChecked(!expert);
    m_actExpert->setChecked(expert);

    m_tabs->setCurrentIndex(frequencies ? 1 : 0);
    m_signalHint->setText(!loaded ? QString() : frequencies ? t.signalHint : t.signalHintPeriod);
    m_spectrumHint->setText(expert ? t.spectrumHintExpert : t.spectrumHint);
    m_busy->setVisible(busy());

    refreshSidePanel();
    refreshTransport();
    refreshStatusInfo();
    fitSidePanel();
    m_signal->update();
    m_spectrum->analysisChanged();
    if (m_settingsDialog)
        m_settingsDialog->load();
}

void MainWindow::refreshSidePanel()
{
    const Tr &t = tr();
    const QChar d = t.decimal;
    const bool expert = m_session.params.expert;
    const bool loaded = m_session.loaded.has_value();
    m_soundBox->setVisible(loaded);
    if (!loaded)
        return;
    const audio::Audio &sound = m_session.loaded->audio;
    m_soundName->setText(sound.name);
    m_soundInfo->setText(soundInfo(t, sound));
    m_periodSection->setVisible(m_session.tool == Tool::Period);
    m_frequencySection->setVisible(m_session.tool == Tool::Extract);

    if (const auto &c = m_session.period) {
        m_t1Value->setText(fmtPosition((*c)[0], d));
        m_t2Value->setText(fmtPosition((*c)[1], d));
        m_deltaValue->setText(fmtTime((*c)[1] - (*c)[0], d));
    }
    m_periodBox->setVisible(m_session.period.has_value());
    // la consigne est pour l'élève (mode simplifié) ; l'expert la retrouve dans la bulle de l'onglet
    m_periodHint->setVisible(!m_session.period && !expert);
    const dsp::Analysis *a = analysis();
    m_snapCheck->setVisible(expert);
    m_snapCheck->setChecked(m_session.params.snapZero);
    m_autocorrValue->setVisible(expert && a);
    if (a) {
        if (const auto &period = a->period) {
            // espaces insécables : la ligne ne se coupe qu'entre T et f
            const auto glued = [](QString s) { return s.replace(u' ', QChar(0x00a0)); };
            m_autocorrValue->setText(u"%1 T\u00a0≈\u00a0%2 · f\u00a0≈\u00a0%3"_s.arg(
                t.autocorrelation, glued(fmtTime(*period, d)), glued(fmtFreq(1.0 / *period, d))));
            setTextColor(m_autocorrValue, palette().color(QPalette::WindowText));
        } else {
            m_autocorrValue->setText(u"%1 %2"_s.arg(t.autocorrelation, t.noPeriod));
            setTextColor(m_autocorrValue, palette().color(QPalette::PlaceholderText));
        }
    }

    const auto range = m_session.extractRange();
    m_aValue->setText(fmtPosition(m_session.extract[0], d));
    m_bValue->setText(fmtPosition(m_session.extract[1], d));
    const size_t count = range ? range->second - range->first : 0;
    m_extractInfo->setText(t.extractInfo(fmtTime(double(count) / sound.rate, d), count));
    m_computingBox->setVisible(!a && m_analyzer.pending());
    m_tooShort->setVisible(!a && !m_analyzer.pending());
    // le mode expert l'a dans la ligne d'aide du spectre
    m_reticleHint->setVisible(a && !expert);
    m_expertBox->setVisible(expert && a);
    if (!expert || !a)
        return;

    const dsp::Spectrum &sp = a->spectrum;
    const size_t window = size_t(std::ranges::find(dsp::ALL_WINDOWS, m_session.params.window) - dsp::ALL_WINDOWS.begin());
    // détail de la FFT dans les bulles de l'écart entre raies
    QString fft = t.fftInfo(sp.fftLen, t.windowNames[window]);
    if (sp.segments > 1)
        fft += u"\n"_s + t.segmentsInfo(sp.segments, sp.segmentLen);
    for (QLabel *label : {m_dfLabel, m_dfValue})
        label->setToolTip(u"%1\n\n%2"_s.arg(fft, t.dfTip));
    m_dfValue->setText(fmtFreq(sp.df(), d));
    m_resolutionValue->setText(fmtFreq(sp.resolution(), d));

    const auto f0 = this->f0();
    m_f0Value->setVisible(f0.has_value());
    if (f0)
        m_f0Value->setText(u"f₁ = %1"_s.arg(fmtFreq(*f0, d)));
    m_f0Manual->setVisible(f0 && m_session.manualF0);
    m_f0ManualBox->setVisible(f0 && m_session.manualF0);
    m_f0Auto->setVisible(f0 && !m_session.manualF0);
    m_f0None->setVisible(!f0);
    const std::vector<dsp::Harmonic> h = harmonics();
    // le tableau dès qu'un rang a un pic : sans pic à la fondamentale (fondamentale absente), un tiret
    // au rang 1 et les autres rangs mesurés
    const bool table = std::any_of(h.begin(), h.end(), [](const dsp::Harmonic &x) { return x.peak.has_value(); });
    m_harmonicsTable->setVisible(table);
    m_copyTable->setVisible(table);
    m_copyTable->setEnabled(!m_analyzer.pending());
    m_f0NoPeak->setVisible(f0 && (h.empty() || !h.front().peak));
    if (table)
        refreshHarmonics(h);
}

void MainWindow::setTool(Tool tool)
{
    if (tool == m_session.tool)
        return;
    // Chaque activité garde sa vue du signal : la période se mesure de près, l'extrait se choisit de
    // plus loin.
    if (m_session.loaded)
        m_signalViews[size_t(m_session.tool)] = std::pair(m_signal->viewX0(), m_signal->viewX1());
    m_session.tool = tool;
    if (const auto &view = m_signalViews[size_t(tool)])
        m_signal->setViewX(view->first, view->second);
    else
        m_signal->resetView();
    // le spectre n'a sa place que dans l'étude des fréquences ; le signal prend alors toute la hauteur
    if (tool == Tool::Period) {
        m_splitSizes = m_vsplit->sizes();
        m_spectrumPane->hide();
    } else {
        m_spectrumPane->show();
        if (m_splitMoved && m_splitSizes.size() == 2)
            m_vsplit->setSizes(m_splitSizes);
        else
            shareSignal();
    }
    refresh();
}

void MainWindow::shareSignal()
{
    const int total = m_vsplit->height();
    if (m_splitMoved || total <= 0 || m_spectrumPane->isHidden())
        return;
    const int signal = std::max(int(total * SIGNAL_SHARE), SIGNAL_MIN_HEIGHT);
    m_vsplit->setSizes({signal, std::max(total - signal, 1)});
}

void MainWindow::fitSidePanel()
{
    // Contenu plus large (mode expert, langue, agrandissement) : le volet s'élargit d'autant.
    const int width = m_side->minimumSizeHint().width();
    if (width != m_sideMinWidth) {
        m_sideMinWidth = width;
        m_side->updateGeometry();
    }
}

void MainWindow::refreshHarmonics(const std::vector<dsp::Harmonic> &harmonics)
{
    const QChar d = tr().decimal;
    // rapports à la fondamentale : son pic, ou sa fréquence attendue s'il n'y en a pas
    const std::optional<dsp::Peak> &first = harmonics.front().peak;
    const double f1 = first ? first->freq : harmonics.front().expected;
    const int rows = int(harmonics.size());
    m_harmonicsTable->setRowCount(rows);
    const QColor placeholder = palette().color(QPalette::PlaceholderText);
    const auto cell = [&](int row, int column, const QString &text, std::optional<QColor> color = std::nullopt) {
        QTableWidgetItem *item = m_harmonicsTable->item(row, column);
        if (!item) {
            item = new QTableWidgetItem;
            m_harmonicsTable->setItem(row, column, item);
        }
        item->setText(text);
        item->setTextAlignment(column == 0 ? Qt::AlignCenter : Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(color ? QBrush(*color) : QBrush());
    };
    for (int r = 0; r < rows; ++r) {
        const dsp::Harmonic &h = harmonics[size_t(r)];
        cell(r, 0, QString::number(h.rank), harmonicColor(palette()));
        if (const auto &p = h.peak) {
            cell(r, 1, fmtDec(p->freq, 1, d));
            cell(r, 2, fmtDec(p->freq / f1, 3, d));
            cell(r, 3, fmtDec(toDb(p->amplitude), 1, d));
            if (first)
                cell(r, 4, fmtDec(100.0 * p->amplitude / first->amplitude, 1, d) + u" %"_s);
            else
                cell(r, 4, u"—"_s, placeholder);
        } else {
            for (int c = 1; c < 5; ++c)
                cell(r, c, u"—"_s, placeholder);
        }
    }
    m_harmonicsTable->resizeRowsToContents();
    // Colonnes à la largeur de leur contenu (la dernière prend la place qui reste), et le tableau assez
    // large pour toutes : le panneau s'élargit d'autant, au lieu de cacher les dernières colonnes.
    QHeaderView *header = m_harmonicsTable->horizontalHeader();
    header->setStretchLastSection(false);
    m_harmonicsTable->resizeColumnsToContents();
    int width = 2 * m_harmonicsTable->frameWidth();
    for (int c = 0; c < header->count(); ++c)
        width += header->sectionSize(c);
    header->setStretchLastSection(true);
    m_harmonicsTable->setMinimumWidth(width);
    m_harmonicsTable->setFixedHeight(m_harmonicsTable->horizontalHeader()->height()
                                     + m_harmonicsTable->verticalHeader()->length()
                                     + 2 * m_harmonicsTable->frameWidth());
}

void MainWindow::refreshTransport()
{
    const bool expert = m_session.params.expert;
    m_listenCombo->setVisible(expert);
    m_listenLabel->setVisible(!expert);
    m_listenLabel->setText(m_session.listenChoice() == Listen::Sound ? tr().listenSoundOnly : tr().listenExtractOnly);
    m_listenCombo->setCurrentIndex(m_session.listen == Listen::Sound ? 0 : 1);
    const bool playing = m_player.playing();
    const auto clip = m_session.clip();
    m_playButton->setEnabled(!playing && clip && !clip->empty());
    m_pauseButton->setEnabled(playing);
    m_stopButton->setEnabled(playing || m_player.paused());
}

void MainWindow::refreshStatusInfo()
{
    const Tr &t = tr();
    m_modeStatus->setText(m_session.params.expert ? t.modeExpertName : t.modeSimpleName);
    m_modeStatus->setToolTip(t.modeStatusTip);
    m_soundStatus->setVisible(m_session.loaded.has_value());
    if (!m_session.loaded)
        return;
    // nom du son (raccourci au milieu s'il est long) et ses caractéristiques
    const audio::Audio &sound = m_session.loaded->audio;
    const QString full = u"%1 · %2"_s.arg(sound.name, soundInfo(t, sound));
    m_soundStatus->setText(m_soundStatus->fontMetrics().elidedText(full, Qt::ElideMiddle, std::max(width() / 3, 120)));
    m_soundStatus->setToolTip(full);
}

void MainWindow::setLanguage(std::optional<Lang> choice)
{
    m_session.params.lang = choice;
    m_appliedParams.lang = choice;
    m_lang = resolveLang(choice);
    // Textes de Qt lui-même (boutons des boîtes de dialogue, menu contextuel des champs de saisie).
    QApplication::removeTranslator(&m_qtTranslator);
    if (m_qtTranslator.load(u"qtbase_"_s + langCode(m_lang), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&m_qtTranslator);
    retranslate();
    setStatus(tr().languageChanged);
    refresh();
    settingsChanged();
}

void MainWindow::setTheme(Qt::ColorScheme choice)
{
    m_session.params.theme = choice;
    m_appliedParams.theme = choice;
    // la nouvelle palette (changeEvent) met à jour le bouton, les couleurs des textes et des graphiques
    theme::choose(choice);
    settingsChanged();
}

void MainWindow::setMode(bool expert)
{
    if (expert == m_session.params.expert)
        return;
    const auto listened = m_session.clip();
    m_session.params.expert = expert;
    m_appliedParams.expert = expert;
    // le mode simplifié n'écoute que l'extrait
    if (m_session.clip() != listened)
        m_player.stop();
    if (!expert && m_settingsDialog)
        m_settingsDialog->close();
    m_spectrum->resetView();
    setStatus(expert ? tr().modeExpertOn : tr().modeSimpleOn);
    refresh();
    updatePlayback();
    settingsChanged();
}

void MainWindow::paramsEdited()
{
    const Settings &p = m_session.params;
    const Settings &old = m_appliedParams;
    if (p.db != old.db || p.logFreq != old.logFreq || p.fmax != old.fmax)
        m_spectrum->resetView();
    m_appliedParams = p;
    refresh();
    settingsChanged();
}

void MainWindow::setStatus(const QString &text, icons::Status kind)
{
    m_status = {text, kind};
    m_statusLabel->setText(text);
    fitStatusBar();
    const int side = m_statusLabel->fontMetrics().height();
    m_statusIcon->setPixmap(icons::status(kind).pixmap(QSize(side, side), devicePixelRatioF()));
    m_statusIcon->setVisible(!text.isEmpty());
    const QPalette palette = QApplication::palette();
    QColor color = palette.color(QPalette::WindowText);
    switch (kind) {
    case icons::Status::Info: break;
    case icons::Status::Success: color = successColor(palette); break;
    case icons::Status::Warning: color = warnColor(palette); break;
    case icons::Status::Error: color = errorColor(palette); break;
    }
    setTextColor(m_statusLabel, color);
}

void MainWindow::fitStatusBar()
{
    // Hauteur donnée à la barre : celle que QLabel estime coupe le texte dès une vingtaine de
    // caractères (et la barre changeait de hauteur d'un message à l'autre). Ici, une ligne si le
    // message tient dans la largeur qu'il occupe, plusieurs sinon.
    const int line = std::max(m_statusLabel->heightForWidth(QWIDGETSIZE_MAX), m_modeStatus->sizeHint().height());
    statusBar()->setFixedHeight(std::max(line, m_statusLabel->heightForWidth(m_statusLabel->width())) + 6);
}

void MainWindow::styleStatusBar()
{
    // Bande distincte du reste de la fenêtre : fond un peu plus foncé, filet au-dessus ; son et mode
    // séparés du message par un filet vertical. Le son, plus discret : couleur donnée ici, la feuille
    // de style l'emportant sur la palette des étiquettes qu'elle décrit.
    // Couleurs avec leur transparence : en thème clair, PlaceholderText est un noir translucide, que
    // `QColor::name()` donnerait noir.
    const auto css = [](const QColor &c) {
        return u"rgba(%1, %2, %3, %4%)"_s.arg(c.red()).arg(c.green()).arg(c.blue()).arg(std::lround(c.alphaF() * 100));
    };
    const QPalette palette = QApplication::palette();
    const QColor background = palette.color(QPalette::Window).darker(isDark(palette) ? 135 : 107);
    statusBar()->setStyleSheet(u"QStatusBar { background: %1; border-top: 1px solid %2; }"
                               u"QStatusBar::item { border: none; }"
                               u"QLabel#soundStatus { color: %3; }"_s.arg(css(background),
                                                                          css(palette.color(QPalette::Mid)),
                                                                          css(palette.color(QPalette::PlaceholderText))));
    setStatus(m_status);
}

void MainWindow::openHelp()
{
    QString error;
    const QString page = help::write(m_lang, m_files, &error);
    if (page.isEmpty()) {
        setStatus(tr().helpFailed(error), icons::Status::Error);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(page));
    setStatus(tr().helpOpened(QDir::toNativeSeparators(page)), icons::Status::Success);
}

void MainWindow::showAbout()
{
    AboutDialog dialog(tr(), m_files, this);
    dialog.exec();
}

void MainWindow::showSettings()
{
    if (!m_session.params.expert)
        return;
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(m_session.params, this);
        m_settingsDialog->setObjectName(u"settings"_s);
        connect(m_settingsDialog, &SettingsDialog::edited, this, &MainWindow::paramsEdited);
        m_settingsDialog->retranslate(tr());
    }
    m_settingsDialog->load();
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

// ---------------------------------------------------------------- chargement

bool MainWindow::busy() const
{
    return m_loader.isRunning();
}

void MainWindow::openDialog()
{
    if (busy())
        return;
    const Tr &t = tr();
    QStringList patterns;
    for (const QString &ext : audio::extensions())
        patterns << u"*."_s + ext << u"*."_s + ext.toUpper();
    const QString filters = u"%1 (%2);;%3 (*)"_s.arg(t.sounds, patterns.join(u' '), t.allFiles);
    m_idleStatus = m_status;
    setStatus(t.chooseSound);
    const QString path = QFileDialog::getOpenFileName(this, t.dialogOpen, m_session.params.lastDir, filters);
    if (path.isEmpty())
        setStatus(m_idleStatus);
    else
        loadPath(path);
}

void MainWindow::loadPath(const QString &path)
{
    if (busy())
        return;
    m_loadingPath = path;
    setStatus(tr().loading(QDir::toNativeSeparators(path)));
    // Décodage dans un fil de fond : la fenêtre reste réactive pendant le chargement d'un long son.
    m_cancelLoad = false;
    m_loader.setFuture(QtConcurrent::run(audio::load, path, &m_cancelLoad));
    refresh();
}

void MainWindow::onLoaded()
{
    // chargement interrompu par la fermeture de la fenêtre
    if (m_cancelLoad)
        return;
    const Tr &t = tr();
    audio::LoadResult result;
    try {
        result = m_loader.result();
    } catch (...) {
        setStatus(t.unexpectedError, icons::Status::Error);
        refresh();
        return;
    }
    if (!result.audio) {
        const QString reason = result.error == audio::LoadError::Unsupported ? t.unsupportedFormat
                             : result.error == audio::LoadError::NoAudio     ? t.noAudio
                                                                             : result.message;
        setStatus(t.openFailed(QDir::toNativeSeparators(m_loadingPath), reason), icons::Status::Error);
        refresh();
        return;
    }
    m_session.params.lastDir = QFileInfo(m_loadingPath).absolutePath();
    m_appliedParams.lastDir = m_session.params.lastDir;
    settingsChanged();
    QString status = t.loaded(result.audio->name, soundInfo(t, *result.audio));
    const bool truncated = result.audio->truncated, damaged = result.audio->damaged,
               invalid = result.audio->invalidSamples;
    if (truncated)
        status += u" — "_s + t.truncated;
    if (damaged)
        status += u" — "_s + t.damaged;
    if (invalid)
        status += u" — "_s + t.invalidSamples;
    setAudio(std::move(*result.audio));
    setStatus(status, truncated || damaged || invalid ? icons::Status::Warning : icons::Status::Success);
}

void MainWindow::loadExample(audio::Example example)
{
    const Tr &t = tr();
    const size_t index = size_t(std::ranges::find(audio::ALL_EXAMPLES, example) - audio::ALL_EXAMPLES.begin());
    const QString &name = t.exampleNames[index];
    setAudio(audio::generate(example, name));
    m_example = index;
    // le même message qu'à l'ouverture d'un fichier (sons de synthèse des essais et des captures)
    setStatus(t.loaded(name, soundInfo(t, m_session.loaded->audio)), icons::Status::Success);
}

void MainWindow::setAudio(audio::Audio sound)
{
    m_example.reset();
    m_player.stop();
    m_session.setAudio(std::move(sound));
    m_analyzer.clear();
    m_signalViews = {};
    m_signal->resetView();
    m_spectrum->resetView();
    refresh();
    updatePlayback();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        m_dropOverlay->setGeometry(rect());
        m_dropOverlay->raise();
        m_dropOverlay->show();
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropOverlay->hide();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    m_dropOverlay->hide();
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            loadPath(url.toLocalFile());
            return;
        }
    }
}

// ---------------------------------------------------------------- analyse

void MainWindow::requestAnalysis()
{
    const auto key = m_session.analysisKey();
    if (!key)
        return;
    m_analyzer.request(*key, m_session.loaded->audio.signal(key->channel), m_session.loaded->audio.rate);
}

const dsp::Analysis *MainWindow::analysis() const
{
    return m_session.loaded ? m_analyzer.result(m_session.loaded->id) : nullptr;
}

void MainWindow::analysisUpdated()
{
    // pendant le calcul d'un nouvel extrait, le spectre affiché est encore l'ancien : pas d'export
    m_actExportSpectrum->setEnabled(analysis() && !busy() && !m_analyzer.pending());
    refreshSidePanel();
    fitSidePanel();
    m_spectrum->analysisChanged();
}

std::optional<double> MainWindow::f0() const
{
    if (m_session.manualF0)
        return m_session.manualF0;
    const dsp::Analysis *a = analysis();
    return a ? a->fundamental : std::nullopt;
}

std::vector<dsp::Harmonic> MainWindow::harmonics() const
{
    const dsp::Analysis *a = analysis();
    const auto f = f0();
    if (!a || !f)
        return {};
    return dsp::harmonics(a->peaks, *f, a->spectrum.df(), size_t(m_session.active().harmonics));
}

SettingsDialog *MainWindow::settingsDialog() const
{
    return m_settingsDialog;
}

void MainWindow::showSignal(double t0, double t1)
{
    m_signal->setViewX(t0, t1);
}

// ---------------------------------------------------------------- écoute

void MainWindow::play()
{
    const auto clip = m_session.clip();
    if (!clip || !m_session.loaded)
        return;
    const audio::Audio &sound = m_session.loaded->audio;
    if (const auto error = m_player.play(*clip, sound.signal(clip->channel), sound.rate)) {
        const Tr &t = tr();
        setStatus(t.playFailed(error->noDevice ? t.noAudioOutput : t.audioFailures[size_t(error->failure)]),
                  icons::Status::Error);
    }
    updatePlayback();
}

void MainWindow::togglePlayback()
{
    if (m_player.playing())
        m_player.pause();
    else
        play();
    updatePlayback();
}

void MainWindow::setListen(Listen listen)
{
    if (listen == m_session.listen)
        return;
    m_session.listen = listen;
    // pendant la lecture, on passe aussitôt à l'autre choix
    if (m_player.playing())
        play();
    else
        m_player.stop();
    updatePlayback();
}

std::optional<double> MainWindow::playhead() const
{
    const Clip *clip = m_player.clip();
    if (!m_session.loaded || !clip || clip->audio != m_session.loaded->id)
        return std::nullopt;
    const auto position = m_player.position();
    if (!position)
        return std::nullopt;
    return *position / m_session.loaded->audio.rate;
}

void MainWindow::updatePlayback()
{
    if (const auto error = m_player.update())
        setStatus(tr().playFailed(tr().audioFailures[size_t(*error)]), icons::Status::Error);
    m_signal->setPlayhead(playhead());
    refreshTransport();
    // la tête de lecture avance
    if (m_player.busy() && !m_playTimer.isActive())
        m_playTimer.start();
    else if (!m_player.busy())
        m_playTimer.stop();
}

// ---------------------------------------------------------------- exports

void MainWindow::exportSpectrum()
{
    const dsp::Analysis *a = analysis();
    if (!a || m_analyzer.pending())
        return;
    saveCsv(spectrumCsv(tr(), a->spectrum), tr().dialogExportSpectrum, tr().csvSpectrumName);
}

void MainWindow::exportExtract()
{
    const auto range = m_session.extractRange();
    if (!m_session.loaded || !range)
        return;
    const audio::Audio &sound = m_session.loaded->audio;
    const std::vector<float> &signal = *sound.signal(m_session.channel());
    const std::span<const float> samples = std::span(signal).subspan(range->first, range->second - range->first);
    saveCsv(extractCsv(tr(), samples, range->first, sound.rate), tr().dialogExportExtract, tr().csvExtractName);
}

void MainWindow::saveCsv(const QString &csv, const QString &title, const QString &name)
{
    if (busy())
        return;
    const Tr &t = tr();
    m_idleStatus = m_status;
    setStatus(t.chooseSave);
    const QString &dir = m_session.params.lastDir;
    const QString start = dir.isEmpty() ? name : QDir(dir).filePath(name);
    QString path = QFileDialog::getSaveFileName(this, title, start, u"CSV (*.csv)"_s);
    if (path.isEmpty()) {
        setStatus(m_idleStatus);
        return;
    }
    const bool completed = QFileInfo(path).suffix().isEmpty();
    path = withCsvExtension(path);
    // nom tapé sans extension : le dialogue n'a pas vu que le fichier .csv existait déjà
    if (completed && QFileInfo::exists(path)) {
        QMessageBox box(QMessageBox::Question, about::NAME, t.replaceFile(QFileInfo(path).fileName()), QMessageBox::NoButton, this);
        QPushButton *replace = box.addButton(t.replaceButton, QMessageBox::AcceptRole);
        QPushButton *cancel = box.addButton(t.cancel, QMessageBox::RejectRole);
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() != replace) {
            setStatus(m_idleStatus);
            return;
        }
    }
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && writeAll(file, csv.toUtf8()) && file.commit())
        setStatus(t.exported(QDir::toNativeSeparators(path)), icons::Status::Success);
    else
        setStatus(t.exportFailed(file.errorString()), icons::Status::Error);
}

void MainWindow::copyTable()
{
    const std::vector<dsp::Harmonic> h = harmonics();
    if (std::none_of(h.begin(), h.end(), [](const dsp::Harmonic &x) { return x.peak.has_value(); }))
        return;
    QApplication::clipboard()->setText(harmonicsTable(tr(), h));
    setStatus(tr().tableCopied, icons::Status::Success);
}

// ---------------------------------------------------------------- réglages

double MainWindow::maxUiZoom() const
{
    // Le maximum dépend de la largeur de la fenêtre : il reste toujours au moins la largeur minimale
    // pour les panneaux et les graphiques, même sur un écran 1366 × 768.
    if (width() < MIN_WINDOW.width())
        return UI_ZOOM_MAX; // fenêtre pas encore mesurée
    return std::clamp(double(width()) / MIN_WINDOW.width(), 1.0, UI_ZOOM_MAX);
}

void MainWindow::setUiZoom(double zoom)
{
    // L'agrandissement demandé est retenu ; celui qui s'affiche est borné par la largeur de la
    // fenêtre, et revient à la demande quand la fenêtre s'élargit (au démarrage, la fenêtre n'a pas
    // encore sa taille : l'agrandissement enregistré se retrouve au premier redimensionnement).
    m_uiZoomWanted = std::clamp(zoom, UI_ZOOM_MIN, UI_ZOOM_MAX);
    applyUiZoom();
}

void MainWindow::applyUiZoom()
{
    m_uiZoom = std::clamp(m_uiZoomWanted, UI_ZOOM_MIN, maxUiZoom());
    QFont font = m_baseFont;
    if (m_baseFont.pointSizeF() > 0.0)
        font.setPointSizeF(m_baseFont.pointSizeF() * m_uiZoom);
    else
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(m_baseFont.pixelSize() * m_uiZoom))));
    QApplication::setFont(font);
    applyFonts();
    m_signal->setUiScale(m_uiZoom);
    m_spectrum->setUiScale(m_uiZoom);
    sizeMenuCorner();
    // pictogrammes des onglets : de la hauteur d'une ligne de texte
    const int tabIcon = QFontMetrics(QApplication::font()).height();
    m_tabs->setIconSize(QSize(tabIcon, tabIcon));
    sizeWelcomeButtons();
    // les étiquettes décrites par la feuille de style de la barre d'état ne prennent la nouvelle police
    // qu'avec elle (`styleStatusBar` réaffiche aussi le message, à sa hauteur)
    styleStatusBar();
    refreshSidePanel();
    refreshStatusInfo();
    fitSidePanel();
    settingsChanged();
}

void MainWindow::styleFont(QWidget *widget, double factor, QFont::Weight weight)
{
    m_fonts.push_back({widget, factor, weight});
}

void MainWindow::applyFonts()
{
    const QFont base = QApplication::font();
    for (const ScaledFont &f : m_fonts) {
        if (!f.widget)
            continue;
        QFont font = base;
        if (base.pointSizeF() > 0.0)
            font.setPointSizeF(base.pointSizeF() * f.factor);
        else
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(base.pixelSize() * f.factor))));
        font.setWeight(f.weight);
        f.widget->setFont(font);
    }
}

Settings MainWindow::currentSettings() const
{
    Settings s = m_session.params;
    s.uiZoom = m_uiZoomWanted;
    s.windowSize = m_windowSize;
    s.maximized = m_maximized;
    return s;
}

void MainWindow::settingsChanged()
{
    if (currentSettings() != m_saved)
        m_saveTimer.start();
}

void MainWindow::writeSettings()
{
    m_saveTimer.stop();
    const Settings current = currentSettings();
    if (current == m_saved)
        return;
    if (!m_files.user.isEmpty()) {
        const QString error = current.save(m_files.user);
        if (!error.isEmpty() && !m_settingsErrorShown) {
            setStatus(tr().settingsFailed(error), icons::Status::Warning);
            m_settingsErrorShown = true;
        }
    }
    m_saved = current;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) && width() >= 400
        && height() >= 300) {
        m_previousWindowSize = m_windowSize;
        m_windowSize = size();
    }
    refreshStatusInfo();
    if (m_dropOverlay->isVisible())
        m_dropOverlay->setGeometry(rect());
    if (std::clamp(m_uiZoomWanted, UI_ZOOM_MIN, maxUiZoom()) != m_uiZoom)
        applyUiZoom();
    settingsChanged();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        m_maximized = isMaximized();
        // Agrandissement signalé après le redimensionnement : la taille notée est celle de l'écran.
        if ((isMaximized() || isFullScreen()) && m_windowSize == size())
            m_windowSize = m_previousWindowSize;
        settingsChanged();
    } else if (event->type() == QEvent::PaletteChange && m_soundBox) {
        // Thème clair ou sombre : couleurs des textes discrets, une fois la nouvelle palette transmise
        // à tous les widgets.
        QTimer::singleShot(0, this, [this] {
            retranslate();
            refresh();
        });
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // le message a changé de largeur : il tient peut-être sur une ligne de plus ou de moins
    if (watched == m_statusLabel && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &MainWindow::fitStatusBar);
    // Part du signal gardée tant que l'utilisateur n'a pas déplacé la séparation : quand la fenêtre
    // change de taille, et au premier affichage de l'analyse (après la page d'accueil). Après la mise
    // en page, qui donne la nouvelle hauteur des volets.
    if (watched == m_vsplit && event->type() == QEvent::Resize && !m_splitMoved)
        QTimer::singleShot(0, this, &MainWindow::shareSignal);
    // Nouvelle mise en page du panneau (police agrandie, mode, textes) : sa largeur minimale n'est
    // connue qu'à ce moment, après les événements qui suivent le changement.
    if (m_side && watched == m_side->widget() && event->type() == QEvent::LayoutRequest)
        fitSidePanel();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // un chargement en cours (ffmpeg peut-être bloqué) est interrompu : le programme se termine
    m_cancelLoad = true;
    m_loader.waitForFinished();
    m_player.stop();
    writeSettings();
    event->accept();
}
