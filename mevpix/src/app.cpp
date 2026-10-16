#include "app.h"

#include "about.h"
#include "canvas.h"
#include "help.h"
#include "plot.h"
#include "style.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
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
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Extensions proposées par la boîte de dialogue Ouvrir (en minuscules et en majuscules : sous Linux,
/// le filtre distingue `photo.jpg` de `PHOTO.JPG`).
const QStringList IMAGE_EXTENSIONS = {u"png"_s, u"jpg"_s, u"jpeg"_s, u"bmp"_s, u"tif"_s, u"tiff"_s, u"webp"_s};
/// Largeur par défaut et minimale du panneau latéral ; hauteur par défaut et minimale du graphique
/// du profil.
constexpr int SIDE_WIDTH = 300;
constexpr int SIDE_MIN_WIDTH = 240;
constexpr int PLOT_HEIGHT = 260;
constexpr int PLOT_MIN_HEIGHT = 140;
/// Titres du panneau latéral : taille de la police de l'interface (en demi-gras, `TITLE_WEIGHT`).
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
    // Dans la barre d'outils, le texte d'une étiquette prend la couleur des boutons.
    palette.setColor(QPalette::ButtonText, color);
    widget->setPalette(palette);
}

/// Couleur pour une feuille de style, transparence comprise (la couleur des textes discrets du thème
/// clair est souvent celle du texte, à demi transparente).
QString cssColor(const QColor &color)
{
    return u"rgba(%1, %2, %3, %4)"_s.arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

/// Texte secondaire, plus discret.
void setWeak(QWidget *widget)
{
    setTextColor(widget, QApplication::palette().color(QPalette::PlaceholderText));
}

QFrame *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

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

/// Logo du programme (icône 256 px), à la taille `side` en pixels logiques.
QPixmap logo(int side, qreal ratio)
{
    QPixmap pixmap = QPixmap(u":/icons/mevpix-256.png"_s)
                         .scaled(QSize(side, side) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

} // namespace

MainWindow::MainWindow(const Settings &settings, const SettingsFiles &files, const QString &startupPath)
    : m_langChoice(settings.lang)
    , m_lang(resolveLang(settings.lang))
    , m_theme(settings.theme)
    , m_files(files)
    , m_saved(settings)
    , m_lastDir(settings.lastDir)
    , m_windowSize(settings.windowSize)
    , m_maximized(settings.maximized)
    , m_baseFont(QApplication::font())
{
    m_session.calibUnit = settings.unit;
    m_session.channel = settings.channel;
    m_session.band = settings.band;

    setWindowIcon(QApplication::windowIcon());
    setMinimumSize(MIN_WINDOW);
    setAcceptDrops(true);

    m_canvas = new ImageCanvas(m_session, this);
    m_plot = new ProfilePlot(m_session, this);
    m_plot->setMinimumHeight(PLOT_MIN_HEIGHT);

    buildActions();
    buildMenus();
    buildToolbar();
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
    sideLayout->addWidget(m_side);

    m_vsplit = new QSplitter(Qt::Vertical, this);
    m_vsplit->addWidget(m_canvas);
    m_vsplit->addWidget(m_plot);
    m_vsplit->setCollapsible(0, false);
    m_vsplit->setCollapsible(1, false);
    m_vsplit->setStretchFactor(0, 1);
    m_plot->hide();

    m_hsplit = new QSplitter(Qt::Horizontal, this);
    m_hsplit->addWidget(side);
    m_hsplit->addWidget(m_vsplit);
    m_hsplit->setCollapsible(0, false);
    m_hsplit->setCollapsible(1, false);
    m_hsplit->setStretchFactor(1, 1);
    m_hsplit->setSizes({SIDE_WIDTH, DEFAULT_WINDOW.width() - SIDE_WIDTH});
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildWelcomePage());
    m_pages->addWidget(m_hsplit);
    setCentralWidget(m_pages);

    buildStatusBar();

    m_dropOverlay = new DropOverlay(this);

    connect(m_canvas, &ImageCanvas::segmentDrawn, this, &MainWindow::commit);
    connect(m_canvas, &ImageCanvas::edited, this, &MainWindow::refresh);
    connect(m_canvas, &ImageCanvas::zoomChanged, this, [this](double zoom) {
        m_zoom = zoom;
        refresh();
    });
    connect(m_plot, &ProfilePlot::cursorsMoved, this, [this] {
        m_canvas->update();
        refreshCursors();
    });
    connect(&m_loader, &QFutureWatcher<LoadResult>::finished, this, &MainWindow::onLoaded);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &MainWindow::writeSettings);

    // Échap : abandonne le tracé en cours, où que soit le focus.
    connect(new QShortcut(Qt::Key_Escape, this), &QShortcut::activated, m_canvas, &ImageCanvas::cancelDrag);
    // Ctrl+Z hors d'un champ de saisie (où il annule la frappe) : supprime la dernière mesure.
    auto *undo = new QAction(this);
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, this, [this] {
        if (m_session.tool == Tool::Measure && !m_session.measures.empty()) {
            m_session.measures.pop_back();
            refresh();
        }
    });
    addAction(undo);
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

    m_status = {tr().statusStart};
    setLanguage(m_langChoice);
    setTheme(m_theme);
    setStatus(tr().statusStart);
    setUiZoom(settings.uiZoom);
    refresh();

    if (!startupPath.isEmpty())
        QTimer::singleShot(0, this, [this, startupPath] { loadPath(startupPath); });
}

// ---------------------------------------------------------------- construction

void MainWindow::buildActions()
{
    m_actOpen = new QAction(this);
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openDialog);

    m_actQuit = new QAction(this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    m_actQuit->setMenuRole(QAction::QuitRole);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    // Thème : un clic passe à l'autre thème (celui du bureau est suivi jusqu'au premier clic).
    m_actTheme = new QAction(this);
    m_actTheme->setObjectName(u"theme"_s);
    connect(m_actTheme, &QAction::triggered, this, [this] {
        setTheme(isDark(QApplication::palette()) ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    });
    // Langue : un clic passe à la langue suivante.
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

    m_actCalibrate = new QAction(this);
    m_actCalibrate->setCheckable(true);
    connect(m_actCalibrate, &QAction::triggered, this, [this] { setTool(Tool::Calibrate); });
    m_actMeasure = new QAction(this);
    m_actMeasure->setCheckable(true);
    connect(m_actMeasure, &QAction::triggered, this, [this] { setTool(Tool::Measure); });
    m_actProfile = new QAction(this);
    m_actProfile->setCheckable(true);
    connect(m_actProfile, &QAction::triggered, this,
            [this] { setTool(m_session.tool == Tool::Profile ? Tool::Measure : Tool::Profile); });
    m_actFit = new QAction(this);
    connect(m_actFit, &QAction::triggered, m_canvas, &ImageCanvas::requestFit);
}

void MainWindow::buildMenus()
{
    m_menuFile = menuBar()->addMenu(QString());
    m_menuFile->addAction(m_actOpen);
    m_menuFile->addSeparator();
    m_menuFile->addAction(m_actQuit);

    m_menuHelp = menuBar()->addMenu(QString());
    m_menuHelp->addAction(m_actManual);
    m_menuHelp->addAction(m_actAbout);
    m_menuHelp->addAction(m_actAboutQt);
}

void MainWindow::buildToolbar()
{
    m_toolbar = addToolBar(u"toolbar"_s);
    m_toolbar->setObjectName(u"toolbar"_s);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolbar->addAction(m_actOpen);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actCalibrate);
    m_toolbar->addAction(m_actMeasure);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actProfile);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actFit);
    // échelle de l'image, à côté des outils qui la donnent et s'en servent (cachée sans image)
    m_scaleSeparator = m_toolbar->addSeparator();
    m_scaleLabel = new QLabel(m_toolbar);
    m_scaleLabel->setObjectName(u"scale"_s);
    m_scaleLabel->setContentsMargins(8, 0, 8, 0);
    m_scaleAction = m_toolbar->addWidget(m_scaleLabel);
}

void MainWindow::buildMenuCorner()
{
    // À droite de la barre de menus, sur sa ligne : la notice, le thème et la langue.
    // Barre de menus dans la fenêtre, même avec un menu global du bureau, qui cacherait ces boutons.
    menuBar()->setNativeMenuBar(false);
    auto *corner = new QWidget(menuBar());
    corner->setObjectName(u"menuCorner"_s);
    auto *layout = new QHBoxLayout(corner);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(2);
    // thème et langue ; la notice est dans le menu Aide (F1), un bouton de plus faisait doublon
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
    menuBar()->updateGeometry();
}

void MainWindow::buildStatusBar()
{
    // À gauche, ce qu'il faut faire ensuite (ou ce qui vient de se passer), avec un pictogramme ; à
    // droite, l'image ouverte et son zoom.
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);
    m_statusIcon = new QLabel(bar);
    m_statusIcon->setContentsMargins(6, 0, 0, 0);
    // Un message long (chemin du fichier, formats acceptés…) passe à la ligne au lieu d'être coupé.
    m_statusLabel = wrapped(bar);
    m_statusLabel->setObjectName(u"statusMessage"_s);
    m_statusLabel->setContentsMargins(0, 4, 0, 4);
    m_statusLabel->installEventFilter(this);
    m_busy = new QProgressBar(bar);
    m_busy->setRange(0, 0);
    m_busy->setMaximumWidth(80);
    m_busy->setTextVisible(false);
    m_busy->hide();
    m_imageStatus = new StatusInfo(bar);
    m_imageStatus->setObjectName(u"imageStatus"_s);
    m_zoomStatus = new StatusInfo(bar);
    m_zoomStatus->setObjectName(u"zoomStatus"_s);
    bar->addWidget(m_statusIcon);
    bar->addWidget(m_busy);
    bar->addWidget(m_statusLabel, 1);
    bar->addPermanentWidget(m_imageStatus);
    bar->addPermanentWidget(m_zoomStatus);
}

QWidget *MainWindow::buildWelcomePage()
{
    // Épurée : le logiciel, puis ouvrir une image ; la notice, la langue et le thème sont dans la
    // barre d'outils.
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
    styleFont(centered(new QLabel(about::NAME, page)), 2.2, QFont::Bold);
    m_welcomeTagline = centered(new QLabel(page));
    styleFont(m_welcomeTagline, 1.15);

    layout->addSpacing(32);
    m_welcomeOpen = new QPushButton(page);
    m_welcomeOpen->setObjectName(u"welcomeOpen"_s);
    styleFont(m_welcomeOpen, 1.1);
    connect(m_welcomeOpen, &QPushButton::clicked, this, &MainWindow::openDialog);
    layout->addWidget(m_welcomeOpen, 0, Qt::AlignHCenter);
    layout->addSpacing(10);
    m_welcomeDropHint = centered(new QLabel(page));
    layout->addStretch(1);
    return page;
}

void MainWindow::sizeWelcomeButton()
{
    // plus large et plus haut que la normale
    m_welcomeOpen->setMinimumSize(0, 0);
    const QSize hint = m_welcomeOpen->sizeHint();
    m_welcomeOpen->setMinimumSize(hint.width() + hint.height(), int(hint.height() * 1.4));
}

QWidget *MainWindow::buildSidePanel()
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    m_calibratePage = buildCalibratePage();
    m_measurePage = buildMeasurePage();
    m_profilePage = buildProfilePage();
    layout->addWidget(m_calibratePage);
    layout->addWidget(m_measurePage);
    layout->addWidget(m_profilePage);
    layout->addStretch();
    return panel;
}

QWidget *MainWindow::buildCalibratePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_calibHeading = wrapped(page);
    styleFont(m_calibHeading, HEADING_SIZE, TITLE_WEIGHT);
    m_calibStep1 = wrapped(page);
    m_calibStep2 = wrapped(page);
    m_calibSegment = wrapped(page);
    layout->addWidget(m_calibHeading);
    layout->addWidget(m_calibStep1);
    layout->addWidget(m_calibStep2);
    layout->addSpacing(8);
    layout->addWidget(m_calibSegment);

    auto *row = new QHBoxLayout;
    m_realLength = new QLabel(page);
    m_calibInput = new QLineEdit(page);
    m_calibInput->setObjectName(u"calibLength"_s);
    m_calibInput->setMaximumWidth(90);
    m_unitCombo = new QComboBox(page);
    for (Unit unit : ALL_UNITS)
        m_unitCombo->addItem(unitSymbol(unit));
    row->addWidget(m_realLength);
    row->addWidget(m_calibInput);
    row->addWidget(m_unitCombo);
    row->addStretch();
    layout->addLayout(row);

    m_invalidLength = wrapped(page);
    m_validate = new QPushButton(page);
    m_validate->setObjectName(u"validate"_s);
    m_currentScale = wrapped(page);
    m_calibChanged = wrapped(page);
    layout->addWidget(m_invalidLength);
    layout->addWidget(m_validate);
    layout->addSpacing(8);
    layout->addWidget(m_currentScale);
    layout->addWidget(m_calibChanged);

    connect(m_calibInput, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_session.calibInput = text;
        refresh();
    });
    // Touche Entrée dans le champ : valide comme le bouton.
    connect(m_calibInput, &QLineEdit::returnPressed, this, &MainWindow::validateCalibration);
    connect(m_unitCombo, &QComboBox::activated, this, [this](int index) {
        m_session.calibUnit = ALL_UNITS[index];
        refresh();
        settingsChanged();
    });
    connect(m_validate, &QPushButton::clicked, this, &MainWindow::validateCalibration);
    return page;
}

QWidget *MainWindow::buildMeasurePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_measuresHeading = wrapped(page);
    styleFont(m_measuresHeading, HEADING_SIZE, TITLE_WEIGHT);
    m_uncalibrated = wrapped(page);
    m_measureHint = wrapped(page);
    layout->addWidget(m_measuresHeading);
    layout->addWidget(m_uncalibrated);
    layout->addWidget(m_measureHint);
    layout->addSpacing(8);

    // Tout le panneau latéral défile : le tableau prend la hauteur de ses lignes, sans défilement propre.
    m_measureTable = new QTableWidget(0, 3, page);
    m_measureTable->horizontalHeader()->hide();
    m_measureTable->verticalHeader()->hide();
    m_measureTable->setAlternatingRowColors(true);
    m_measureTable->setShowGrid(false);
    m_measureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_measureTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_measureTable->setFocusPolicy(Qt::NoFocus);
    m_measureTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_measureTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_measureTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // nom réglable à la main (largeur du contenu au départ) ; la longueur prend la place qui
    // reste ; le bouton de suppression, à sa largeur
    m_measureTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_measureTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_measureTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(m_measureTable);
    m_noMeasure = wrapped(page);
    layout->addWidget(m_noMeasure);

    m_measureButtons = new QWidget(page);
    auto *buttons = new QHBoxLayout(m_measureButtons);
    buttons->setContentsMargins(0, 4, 0, 0);
    m_copyMeasures = new QPushButton(m_measureButtons);
    m_clearAll = new QPushButton(m_measureButtons);
    buttons->addWidget(m_copyMeasures);
    buttons->addWidget(m_clearAll);
    buttons->addStretch();
    layout->addWidget(m_measureButtons);

    connect(m_copyMeasures, &QPushButton::clicked, this, &MainWindow::copyMeasures);
    connect(m_clearAll, &QPushButton::clicked, this, [this] {
        m_session.measures.clear();
        refresh();
    });
    return page;
}

QWidget *MainWindow::buildProfilePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_profileHeading = wrapped(page);
    styleFont(m_profileHeading, HEADING_SIZE, TITLE_WEIGHT);
    m_profileHint = wrapped(page);
    layout->addWidget(m_profileHeading);
    layout->addWidget(m_profileHint);
    layout->addSpacing(8);

    auto *grid = new QGridLayout;
    m_channelLabel = new QLabel(page);
    m_channelCombo = new QComboBox(page);
    for (size_t i = 0; i < ALL_CHANNELS.size(); ++i)
        m_channelCombo->addItem(QString());
    m_thicknessLabel = new QLabel(page);
    auto *band = new QHBoxLayout;
    m_bandSlider = new QSlider(Qt::Horizontal, page);
    m_bandSlider->setRange(1, 50);
    m_bandSpin = new QSpinBox(page);
    m_bandSpin->setRange(1, 50);
    m_bandSpin->setSuffix(u" px"_s);
    band->addWidget(m_bandSlider, 1);
    band->addWidget(m_bandSpin);
    grid->addWidget(m_channelLabel, 0, 0);
    grid->addWidget(m_channelCombo, 0, 1);
    grid->addWidget(m_thicknessLabel, 1, 0);
    grid->addLayout(band, 1, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    m_cursorsBox = new QWidget(page);
    auto *box = new QVBoxLayout(m_cursorsBox);
    box->setContentsMargins(0, 8, 0, 0);
    box->addWidget(separator(m_cursorsBox));
    m_cursorsTitle = wrapped(m_cursorsBox);
    styleFont(m_cursorsTitle, HEADING_SIZE, TITLE_WEIGHT);
    m_cursorsHint = wrapped(m_cursorsBox);
    box->addWidget(m_cursorsTitle);
    box->addWidget(m_cursorsHint);
    auto *cursors = new QGridLayout;
    for (int i : {0, 1}) {
        // couleur selon le thème (`retranslate`)
        auto *name = new QLabel(i == 0 ? u"A"_s : u"B"_s, m_cursorsBox);
        styleFont(name, 1.0, TITLE_WEIGHT);
        m_cursorName[i] = name;
        m_cursorX[i] = new QLabel(m_cursorsBox);
        m_cursorI[i] = new QLabel(m_cursorsBox);
        styleFont(m_cursorX[i], 1.0, QFont::Normal, true);
        styleFont(m_cursorI[i], 1.0, QFont::Normal, true);
        cursors->addWidget(name, i, 0);
        cursors->addWidget(m_cursorX[i], i, 1);
        cursors->addWidget(m_cursorI[i], i, 2);
    }
    cursors->setColumnStretch(2, 1);
    box->addLayout(cursors);
    m_deltaX = wrapped(m_cursorsBox);
    m_deltaX->setObjectName(u"deltaX"_s);
    styleFont(m_deltaX, 1.0, QFont::Normal, true);
    box->addWidget(m_deltaX);
    m_saturated = wrapped(m_cursorsBox);
    box->addWidget(m_saturated);
    m_exportCsv = new QPushButton(m_cursorsBox);
    box->addSpacing(8);
    box->addWidget(m_exportCsv, 0, Qt::AlignLeft);
    layout->addWidget(m_cursorsBox);

    connect(m_channelCombo, &QComboBox::activated, this, [this](int index) {
        m_session.channel = ALL_CHANNELS[index];
        refresh();
        settingsChanged();
    });
    const auto setBand = [this](int value) {
        if (value == m_session.band)
            return;
        m_session.band = value;
        refresh();
        settingsChanged();
    };
    connect(m_bandSlider, &QSlider::valueChanged, this, setBand);
    connect(m_bandSpin, &QSpinBox::valueChanged, this, setBand);
    connect(m_exportCsv, &QPushButton::clicked, this, &MainWindow::exportCsv);
    return page;
}

// ---------------------------------------------------------------- textes et état

void MainWindow::retranslate()
{
    const Tr &t = tr();
    m_menuFile->setTitle(t.menuFile);
    m_actOpen->setText(t.openImage);
    m_actQuit->setText(t.menuQuit);
    // Le soleil ou la lune et le drapeau montrent le thème et la langue affichés.
    const bool dark = isDark(QApplication::palette());
    m_actTheme->setIcon(icons::theme(dark));
    m_actTheme->setText(dark ? t.themeToLight : t.themeToDark);
    m_actLanguage->setIcon(icons::flag(m_lang));
    m_actLanguage->setText(t.switchLanguage);
    m_zoomStatus->setToolTip(t.zoomTip);
    m_menuHelp->setTitle(t.menuHelp);
    m_actManual->setText(t.menuManual);
    m_actAbout->setText(t.menuAbout);
    m_actAboutQt->setText(t.menuAboutQt);
    m_actCalibrate->setText(t.toolCalibrate);
    m_actMeasure->setText(t.toolMeasure);
    m_actProfile->setText(t.toolProfile);
    m_actFit->setText(t.fitWindow);

    m_welcomeTagline->setText(t.tagline);
    m_welcomeOpen->setText(t.openImage);
    m_welcomeDropHint->setText(t.welcomeDropHint);
    sizeWelcomeButton();
    static_cast<DropOverlay *>(m_dropOverlay)->text = t.dropHere;

    m_calibHeading->setText(t.calibHeading);
    m_calibStep1->setText(t.calibStep1);
    m_calibStep2->setText(t.calibStep2);
    m_realLength->setText(t.realLength);
    m_calibInput->setPlaceholderText(t.example);
    m_invalidLength->setText(t.invalidLength);
    setTextColor(m_invalidLength, errorColor(palette()));
    m_validate->setText(t.validate);
    m_calibChanged->setText(t.calibChanged);
    setTextColor(m_calibChanged, warnColor(palette()));

    m_measuresHeading->setText(t.measuresHeading);
    m_uncalibrated->setText(t.uncalibratedPx);
    setTextColor(m_uncalibrated, warnColor(palette()));
    m_measureHint->setText(t.measureHint);
    m_noMeasure->setText(t.noMeasure);
    m_copyMeasures->setText(t.copyMeasures);
    m_clearAll->setText(t.clearAll);

    m_profileHeading->setText(t.profileHeading);
    m_profileHint->setText(t.profileHint);
    m_channelLabel->setText(t.channelLabel);
    for (size_t i = 0; i < ALL_CHANNELS.size(); ++i)
        m_channelCombo->setItemText(static_cast<int>(i), t.channelName(ALL_CHANNELS[i]));
    m_thicknessLabel->setText(t.thickness);
    m_bandSlider->setToolTip(t.thicknessTip);
    m_bandSpin->setToolTip(t.thicknessTip);
    m_cursorsTitle->setText(t.cursors);
    m_cursorsHint->setText(t.cursorsHint);
    setTextColor(m_cursorName[0], cursorColorA(QApplication::palette()));
    setTextColor(m_cursorName[1], cursorColorB(QApplication::palette()));
    setTextColor(m_saturated, warnColor(palette()));
    m_exportCsv->setText(t.exportCsv);


    // couleurs des textes discrets (le thème a pu changer)
    for (QWidget *label : {m_welcomeTagline, m_welcomeDropHint, m_noMeasure, m_cursorsHint})
        setWeak(label);
    // En sombre, le style dessine le bord des champs (longueur, épaisseur, tableau) un peu plus foncé
    // que le fond de la fenêtre : sur le fond du panneau, il ne se voyait plus. Fond de référence plus
    // clair pour ce dessin, dans le panneau seulement (le panneau garde le fond de `SideBackground`).
    QPalette fields;
    if (dark)
        fields.setColor(QPalette::Window, QApplication::palette().color(QPalette::Light));
    m_side->widget()->setPalette(fields);
    styleStatusBar();

    m_canvas->setTr(&t);
    m_plot->setTr(&t);
}

void MainWindow::refresh()
{
    const Tr &t = tr();
    const QChar d = t.decimal;
    const Session &s = m_session;

    setWindowTitle(u"%1 — %2"_s.arg(about::NAME, s.image ? s.image->name : t.subtitle));
    const bool loaded = s.image != nullptr;
    if (loaded && m_pages->currentIndex() == 0) {
        m_pages->setCurrentIndex(1);
        // Largeur du panneau latéral, donnée à son premier affichage : avant, caché derrière la page
        // d'accueil, il n'a pas sa taille, et QSplitter répartirait en proportion.
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
    for (QAction *action : {m_actCalibrate, m_actMeasure, m_actProfile, m_actFit})
        action->setEnabled(loaded);
    m_scaleAction->setVisible(loaded);
    m_scaleSeparator->setVisible(loaded);
    // sans image, aucun outil n'est montré enfoncé
    m_actCalibrate->setChecked(loaded && s.tool == Tool::Calibrate);
    m_actMeasure->setChecked(loaded && s.tool == Tool::Measure);
    m_actProfile->setChecked(loaded && s.tool == Tool::Profile);
    if (const auto &c = s.calibration) {
        const QString text = t.scale(fmtNum(c->unitsPerPx(), 3, d) + u' ' + unitSymbol(c->unit));
        const bool pending = s.calibPending();
        m_scaleLabel->setText(pending ? u"⚠ "_s + text : text);
        m_scaleLabel->setToolTip(pending ? t.calibChanged : QString());
        setTextColor(m_scaleLabel, pending ? warnColor(palette()) : palette().color(QPalette::WindowText));
    } else {
        m_scaleLabel->setText(t.notCalibrated);
        m_scaleLabel->setToolTip(QString());
        setTextColor(m_scaleLabel, warnColor(palette()));
    }

    m_calibratePage->setVisible(s.tool == Tool::Calibrate);
    m_measurePage->setVisible(s.tool == Tool::Measure);
    m_profilePage->setVisible(s.tool == Tool::Profile);

    // Étalonnage
    if (s.calibSeg) {
        m_calibSegment->setText(t.segmentDrawn(fmtNum(s.calibSeg->lenPx(), 4, d)));
        setTextColor(m_calibSegment, palette().color(QPalette::WindowText));
    } else {
        m_calibSegment->setText(t.noSegment);
        setWeak(m_calibSegment);
    }
    if (m_calibInput->text() != s.calibInput)
        m_calibInput->setText(s.calibInput);
    m_unitCombo->setCurrentIndex(static_cast<int>(std::ranges::find(ALL_UNITS, s.calibUnit) - ALL_UNITS.begin()));
    const auto value = parseNum(s.calibInput);
    m_invalidLength->setVisible(!s.calibInput.trimmed().isEmpty() && (!value || *value <= 0.0));
    m_validate->setEnabled(s.calibFromInputs().has_value());
    m_currentScale->setVisible(s.calibration.has_value());
    if (const auto &c = s.calibration) {
        m_currentScale->setText(t.currentScale(fmtNum(c->unitsPerPx(), 3, d), fmtNum(1.0 / c->unitsPerPx(), 3, d),
                                               unitSymbol(c->unit)));
    }
    m_calibChanged->setVisible(s.calibPending());

    // Mesures
    m_uncalibrated->setVisible(!s.calibration);
    refreshMeasureTable();

    // Profil
    m_channelCombo->setCurrentIndex(
        static_cast<int>(std::ranges::find(ALL_CHANNELS, s.channel) - ALL_CHANNELS.begin()));
    {
        const QSignalBlocker blockSlider(m_bandSlider), blockSpin(m_bandSpin);
        m_bandSlider->setValue(s.band);
        m_bandSpin->setValue(s.band);
    }
    m_exportCsv->setEnabled(!busy());
    refreshCursors();

    if (m_plot->isHidden() == (s.tool == Tool::Profile)) {
        m_plot->setVisible(s.tool == Tool::Profile);
        if (!m_plot->isHidden() && !m_plotSized) {
            const int total = m_vsplit->height();
            m_vsplit->setSizes({std::max(total - PLOT_HEIGHT, 1), PLOT_HEIGHT});
            m_plotSized = true;
        }
    }
    m_busy->setVisible(busy());
    refreshStatusInfo();
    fitSidePanel();
    m_canvas->update();
    m_plot->update();
}

void MainWindow::refreshStatusInfo()
{
    const Tr &t = tr();
    const LoadedImage *image = m_session.image.get();
    m_imageStatus->setVisible(image != nullptr);
    m_zoomStatus->setVisible(image != nullptr && m_zoom > 0.0);
    if (!image)
        return;
    // nom de l'image (raccourci au milieu s'il est long) et ses dimensions
    const QString full = u"%1 · %2 × %3 px"_s.arg(image->name, QString::number(image->rgb.width()),
                                                 QString::number(image->rgb.height()));
    m_imageStatus->setText(
        m_imageStatus->fontMetrics().elidedText(full, Qt::ElideMiddle, std::max(width() / 3, 120)));
    m_imageStatus->setToolTip(full);
    const double pct = m_zoom * 100.0;
    m_zoomStatus->setText(t.zoomInfo(pct >= 10.0 ? QString::number(std::lround(pct)) : fmtDec(pct, 1, t.decimal)));
}

void MainWindow::fitSidePanel()
{
    // Contenu plus large (outil, langue, agrandissement) : le panneau s'élargit d'autant.
    const int width = m_side->minimumSizeHint().width();
    if (width != m_sideMinWidth) {
        m_sideMinWidth = width;
        m_side->updateGeometry();
    }
}

void MainWindow::refreshMeasureTable()
{
    const Tr &t = tr();
    const auto &measures = m_session.measures;
    const int n = static_cast<int>(measures.size());
    const int previous = m_measureTable->rowCount();
    m_measureTable->setRowCount(n);
    for (int i = previous; i < n; ++i) {
        auto *name = new QTableWidgetItem(u"M%1"_s.arg(i + 1));
        QFont bold = m_measureTable->font();
        bold.setBold(true);
        name->setFont(bold);
        m_measureTable->setItem(i, 0, name);
        auto *length = new QTableWidgetItem;
        length->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_measureTable->setItem(i, 1, length);
        auto *remove = new QToolButton(m_measureTable);
        remove->setText(u"🗑"_s);
        remove->setAutoRaise(true);
        // Les lignes suivent l'ordre des mesures : la ligne i supprime toujours la mesure i.
        connect(remove, &QToolButton::clicked, this, [this, i] {
            if (size_t(i) < m_session.measures.size()) {
                m_session.measures.erase(m_session.measures.begin() + i);
                refresh();
            }
        });
        m_measureTable->setCellWidget(i, 2, remove);
    }
    // largeur de départ du nom : celle du contenu ; ensuite, celle choisie à la main
    if (previous == 0 && n > 0)
        m_measureTable->resizeColumnToContents(0);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(QApplication::font().pointSizeF());
    for (int i = 0; i < n; ++i) {
        QTableWidgetItem *length = m_measureTable->item(i, 1);
        length->setText(fmtLen(measures[i].lenPx(), m_session.calibration, t.decimal));
        length->setFont(mono);
        m_measureTable->cellWidget(i, 2)->setToolTip(t.deleteMeasure);
    }
    m_measureTable->setVisible(n > 0);
    m_measureTable->resizeRowsToContents();
    m_measureTable->setFixedHeight(m_measureTable->verticalHeader()->length() + 2 * m_measureTable->frameWidth());
    m_noMeasure->setVisible(n == 0);
    m_measureButtons->setVisible(n > 0);
}

void MainWindow::refreshCursors()
{
    const Tr &t = tr();
    const QChar d = t.decimal;
    const Profile *profile = m_session.profile();
    m_cursorsBox->setVisible(profile != nullptr);
    if (!profile)
        return;
    const auto [unit, perPx] = m_session.unitScale();
    const double len = profile->lenPx() * perPx;
    const double x[2] = {m_session.cursorT[0] * len, m_session.cursorT[1] * len};
    // Positions et écart avec les mêmes décimales (3 chiffres significatifs pour Δx).
    const double delta = std::abs(x[1] - x[0]);
    const int dec = sigDecimals(delta, 3);
    for (int i : {0, 1}) {
        m_cursorX[i]->setText(u"x = %1 %2"_s.arg(fmtDec(x[i], dec, d), unit));
        m_cursorI[i]->setText(u"I = %1"_s.arg(fmtDec(profile->valueAt(m_session.cursorT[i]), 0, d)));
    }
    m_deltaX->setText(u"Δx = |xB − xA| = %1 %2"_s.arg(fmtDec(delta, dec, d), unit));
    m_saturated->setVisible(profile->saturated > 0);
    if (profile->saturated > 0) {
        const double pct = 100.0 * profile->saturated / double(profile->values.size());
        m_saturated->setText(t.saturated(fmtNum(pct, 2, d)));
    }
}

void MainWindow::setTool(Tool tool)
{
    if (tool != m_session.tool)
        m_canvas->cancelDrag();
    m_session.tool = tool;
    refresh();
}

void MainWindow::setLanguage(std::optional<Lang> choice)
{
    m_langChoice = choice;
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
    m_theme = choice;
    // Le soleil ou la lune changent avec la palette (`changeEvent`).
    theme::choose(choice);
    settingsChanged();
}

void MainWindow::setStatus(const QString &text, icons::Status kind)
{
    m_status = {text, kind};
    m_statusLabel->setText(text);
    fitStatusBar();
    // pictogramme de la hauteur d'une ligne
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
    const int line = std::max(m_statusLabel->heightForWidth(QWIDGETSIZE_MAX), m_zoomStatus->sizeHint().height());
    statusBar()->setFixedHeight(std::max(line, m_statusLabel->heightForWidth(m_statusLabel->width())) + 6);
}

void MainWindow::styleStatusBar()
{
    // Bande distincte du reste de la fenêtre : fond un peu plus foncé, filet au-dessus ; image et
    // zoom séparés du message par un filet vertical. L'image, plus discrète : couleur donnée ici, la
    // feuille de style l'emportant sur la palette des étiquettes qu'elle décrit.
    const QPalette palette = QApplication::palette();
    const QColor background = palette.color(QPalette::Window).darker(isDark(palette) ? 135 : 107);
    statusBar()->setStyleSheet(u"QStatusBar { background: %1; border-top: 1px solid %2; }"
                               u"QStatusBar::item { border: none; }"
                               u"QLabel#imageStatus { color: %3; }"_s.arg(
                                   cssColor(background), cssColor(palette.color(QPalette::Mid)),
                                   cssColor(palette.color(QPalette::PlaceholderText))));
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
    for (const QString &ext : IMAGE_EXTENSIONS)
        patterns << u"*."_s + ext << u"*."_s + ext.toUpper();
    const QString filters = u"%1 (%2);;%3 (*)"_s.arg(t.images, patterns.join(u' '), t.allFiles);
    m_idleStatus = m_status;
    setStatus(t.chooseImage);
    const QString path = QFileDialog::getOpenFileName(this, t.dialogOpen, m_lastDir, filters);
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
    // Décodage dans un fil de fond : la fenêtre reste réactive pendant le chargement d'une grande photo.
    m_loader.setFuture(QtConcurrent::run(loadImage, path));
    refresh();
}

void MainWindow::onLoaded()
{
    const Tr &t = tr();
    LoadResult result;
    try {
        result = m_loader.result();
    } catch (...) {
        setStatus(t.unexpectedError, icons::Status::Error);
        refresh();
        return;
    }
    if (!result.image) {
        const QString reason = result.unsupported ? t.unsupportedFormat : result.error;
        setStatus(t.openFailed(QDir::toNativeSeparators(m_loadingPath), reason), icons::Status::Error);
        refresh();
        return;
    }
    m_lastDir = QFileInfo(m_loadingPath).absolutePath();
    settingsChanged();
    // Image chargée en attente de confirmation : l'actuelle porte un étalonnage ou des mesures.
    if (m_session.hasWork()) {
        QMessageBox box(QMessageBox::Question, about::NAME, t.replaceImage(result.image->name), QMessageBox::NoButton,
                        this);
        QPushButton *open = box.addButton(t.openAnyway, QMessageBox::AcceptRole);
        QPushButton *cancel = box.addButton(t.cancel, QMessageBox::RejectRole);
        box.setDefaultButton(open);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() != open) {
            setStatus(m_idleStatus);
            refresh();
            return;
        }
    }
    setImage(result.image);
}

void MainWindow::setImage(std::shared_ptr<const LoadedImage> img)
{
    setStatus(tr().imageLoaded(img->name, img->rgb.width(), img->rgb.height()));
    m_session.setImage(std::move(img));
    m_canvas->cancelDrag();
    m_canvas->requestFit();
    m_plot->resetView();
    refresh();
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
            m_idleStatus = m_status;
            loadPath(url.toLocalFile());
            return;
        }
    }
}

// ---------------------------------------------------------------- segments

void MainWindow::commit(const Segment &seg)
{
    const Tr &t = tr();
    const QChar d = t.decimal;
    switch (m_session.tool) {
    case Tool::Calibrate:
        setStatus(t.calibDrawn(fmtNum(seg.lenPx(), 4, d)));
        m_session.calibSeg = seg;
        break;
    case Tool::Measure:
        setStatus(t.measureTag(static_cast<int>(m_session.measures.size()) + 1,
                               fmtLen(seg.lenPx(), m_session.calibration, d)));
        m_session.measures.push_back(seg);
        break;
    case Tool::Profile:
        setStatus(t.cutDrawn);
        m_session.cut = seg;
        m_session.cursorT = {0.25, 0.75};
        m_plot->resetView();
        break;
    }
    refresh();
}

void MainWindow::validateCalibration()
{
    const auto c = m_session.calibFromInputs();
    if (!c)
        return;
    m_session.calibration = c;
    setStatus(tr().calibValidated(fmtNum(c->unitsPerPx(), 3, tr().decimal) + u' ' + unitSymbol(c->unit)), icons::Status::Success);
    setTool(Tool::Measure);
}

void MainWindow::copyMeasures()
{
    const Tr &t = tr();
    const auto &calib = m_session.calibration;
    const QString unit = calib ? unitSymbol(calib->unit) : u"px"_s;
    std::vector<double> lengths;
    for (const Segment &seg : m_session.measures)
        lengths.push_back(seg.lenPx());
    QApplication::clipboard()->setText(measuresTable(lengths, calib, t.decimal, {t.colMeasure, t.colLength(unit)}));
    setStatus(t.measuresCopied, icons::Status::Success);
}

void MainWindow::exportCsv()
{
    const Profile *profile = m_session.profile();
    if (!profile || busy())
        return;
    const Tr &t = tr();
    const QString csv = toCsv(*profile, m_session.calibration, t.decimal);
    m_idleStatus = m_status;
    setStatus(t.chooseSave);
    const QString start = m_lastDir.isEmpty() ? t.csvName : QDir(m_lastDir).filePath(t.csvName);
    QString path = QFileDialog::getSaveFileName(this, t.dialogExport, start, u"CSV (*.csv)"_s);
    if (path.isEmpty()) {
        setStatus(m_idleStatus);
        return;
    }
    path = withCsvExtension(path);
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(csv.toUtf8()) >= 0 && file.commit())
        setStatus(t.exported(QDir::toNativeSeparators(path)), icons::Status::Success);
    else
        setStatus(t.exportFailed(file.errorString()), icons::Status::Error);
}

// ---------------------------------------------------------------- réglages

double MainWindow::maxUiZoom() const
{
    // Le maximum dépend de la largeur de la fenêtre : il reste toujours au moins la largeur minimale
    // (800 points) pour les panneaux et l'image, même sur un écran 1366 × 768.
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
    m_canvas->setUiScale(m_uiZoom);
    const int icon = static_cast<int>(std::lround(20 * m_uiZoom));
    m_toolbar->setIconSize(QSize(icon, icon));
    // après que la barre de menus a pris la nouvelle police (hauteur de ses titres)
    QTimer::singleShot(0, this, &MainWindow::sizeMenuCorner);
    sizeWelcomeButton();
    // feuille de style de la barre d'état appliquée à nouveau : ses étiquettes prennent la nouvelle
    // police (et le pictogramme, la hauteur d'une ligne)
    styleStatusBar();
    refreshMeasureTable();
    refreshStatusInfo();
    fitSidePanel();
    settingsChanged();
}

void MainWindow::styleFont(QWidget *widget, double factor, QFont::Weight weight, bool mono)
{
    m_fonts.push_back({widget, factor, weight, mono});
}

void MainWindow::applyFonts()
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

Settings MainWindow::currentSettings() const
{
    Settings s;
    s.lang = m_langChoice;
    s.theme = m_theme;
    s.unit = m_session.calibUnit;
    s.channel = m_session.channel;
    s.band = m_session.band;
    s.uiZoom = m_uiZoomWanted;
    s.lastDir = m_lastDir;
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
            setStatus(tr().settingsFailed(error), icons::Status::Error);
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
    if (m_imageStatus)
        refreshStatusInfo();
    if (m_dropOverlay && m_dropOverlay->isVisible())
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
    } else if (event->type() == QEvent::PaletteChange && m_measureTable) {
        // Thème clair ou sombre : couleurs des avertissements et des textes discrets, une fois la
        // nouvelle palette transmise à tous les widgets.
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
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    writeSettings();
    event->accept();
}
