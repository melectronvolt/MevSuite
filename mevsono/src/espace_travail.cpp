// Construction de la page d'accueil et de l'espace de mesure. Les deux onglets
// partagent le signal, les commandes d'écoute et le panneau de résultats.
#include "app.h"

#include "about.h"
#include "audiodevices.h"
#include "libelles.h"
#include "panneaumesures.h"
#include "signalplot.h"
#include "spectrumplot.h"
#include "style.h"

#include <QAction>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

/// Largeur par défaut et minimale du panneau des mesures ; hauteur minimale des graphiques.
constexpr int LARGEUR_PANNEAU = 300;
constexpr int LARGEUR_PANNEAU_MIN = 240;
constexpr int HAUTEUR_SIGNAL_MIN = 120;
/// Part de la hauteur donnée au signal au premier affichage.
constexpr double PART_SIGNAL = 0.42;

QToolButton *bouton_transport(const QString &symbole, const QString &nom, QWidget *parent)
{
    auto *bouton = new QToolButton(parent);
    bouton->setText(symbole);
    bouton->setObjectName(nom);
    // l'Espace reste à la lecture et à la pause, même après un clic sur un bouton
    bouton->setFocusPolicy(Qt::NoFocus);
    bouton->setMinimumWidth(34);
    return bouton;
}

/// Panneau des mesures : il défile en hauteur, et n'est jamais plus étroit que son contenu (sinon les
/// textes seraient rognés à droite, sans barre de défilement horizontale).
class DefilementMesures : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

    QSize minimumSizeHint() const override
    {
        const int largeurContenu = widget() ? widget()->minimumSizeHint().width() : 0;
        return {std::max(LARGEUR_PANNEAU_MIN, largeurContenu + verticalScrollBar()->sizeHint().width() + 2 * frameWidth()),
                QScrollArea::minimumSizeHint().height()};
    }
};

/// Fond du panneau des mesures, un peu plus sombre que celui des graphiques, et filet à sa droite : la
/// séparation des deux volets se voit.
class FondPanneau : public QWidget {
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

} // namespace

void MainWindow::construit_espace_travail()
{
    m_signal = new SignalPlot(m_session, this);
    m_signal->setMinimumHeight(HAUTEUR_SIGNAL_MIN);
    m_spectrum = new SpectrumPlot(m_session, m_analyzer, this);

    auto *panneau = new FondPanneau(this);
    auto *dispositionPanneau = new QHBoxLayout(panneau);
    dispositionPanneau->setContentsMargins(0, 0, 1, 0); // filet de droite
    m_side = new DefilementMesures(panneau);
    m_side->setObjectName(u"sidePanel"_s);
    m_side->setWidget(construit_panneau_lateral());
    m_side->setWidgetResizable(true);
    m_side->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_side->setFrameShape(QFrame::NoFrame);
    // fond transparent : celui de `FondPanneau`
    m_side->viewport()->setAutoFillBackground(false);
    m_side->widget()->setAutoFillBackground(false);
    m_side->widget()->installEventFilter(this);
    dispositionPanneau->addWidget(m_side);

    m_vsplit = new QSplitter(Qt::Vertical, this);
    m_vsplit->addWidget(construit_volet_signal());
    m_spectrumPane = construit_volet_spectre();
    m_vsplit->addWidget(m_spectrumPane);
    m_vsplit->setCollapsible(0, false);
    m_vsplit->setCollapsible(1, false);
    m_vsplit->setStretchFactor(1, 1);
    m_vsplit->installEventFilter(this);
    connect(m_vsplit, &QSplitter::splitterMoved, this, [this] { m_splitMoved = true; });

    m_hsplit = new QSplitter(Qt::Horizontal, this);
    m_hsplit->addWidget(panneau);
    m_hsplit->addWidget(m_vsplit);
    m_hsplit->setCollapsible(0, false);
    m_hsplit->setCollapsible(1, false);
    m_hsplit->setStretchFactor(1, 1);
    m_hsplit->setSizes({LARGEUR_PANNEAU, DEFAULT_WINDOW.width() - LARGEUR_PANNEAU});
    m_pages = new QStackedWidget(this);
    // Les deux activités, en onglets sur toute la largeur, au-dessus du panneau et des graphiques
    auto *espace = new QWidget(this);
    auto *dispositionTravail = new QVBoxLayout(espace);
    dispositionTravail->setContentsMargins(0, 0, 0, 0);
    dispositionTravail->setSpacing(0);
    m_tabs = new QTabBar(espace);
    m_tabs->setObjectName(u"tabs"_s);
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    // Tab donne le focus aux onglets. Les flèches permettent alors de changer d’activité.
    m_tabs->setFocusPolicy(Qt::TabFocus);
    m_tabs->addTab(icons::onglet_periode(), QString());
    m_tabs->addTab(icons::onglet_frequences(), QString());
    m_tabs->setCurrentIndex(m_session.tool == Tool::Period ? 0 : 1);
    m_spectrumPane->setVisible(m_session.tool == Tool::Extract);
    connect(m_tabs, &QTabBar::currentChanged, this,
            [this](int index) { choisit_outil(index == 0 ? Tool::Period : Tool::Extract); });
    dispositionTravail->addWidget(m_tabs);
    dispositionTravail->addWidget(m_hsplit, 1);
    m_pages->addWidget(construit_page_accueil());
    m_pages->addWidget(espace);
    setCentralWidget(m_pages);
}

QWidget *MainWindow::construit_page_accueil()
{
    auto *page = new QWidget(this);
    page->setObjectName(u"welcome"_s);
    auto *disposition = new QVBoxLayout(page);
    disposition->setContentsMargins(24, 24, 24, 24);
    const auto centre = [&](QLabel *label) {
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignHCenter);
        disposition->addWidget(label);
        return label;
    };
    disposition->addStretch(1);
    auto *logo = new QLabel(page);
    logo->setPixmap(about::icone().scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    disposition->addWidget(logo, 0, Qt::AlignHCenter);
    disposition->addSpacing(8);
    police_du_style(centre(new QLabel(about::NAME, page)), 2.2, QFont::Bold);
    m_welcomeTagline = centre(new QLabel(page));
    police_du_style(m_welcomeTagline, 1.15);

    disposition->addSpacing(32);
    m_welcomeOpen = new QPushButton(page);
    m_welcomeOpen->setIcon(m_actOpen->icon());
    m_welcomeOpen->setObjectName(u"welcomeOpen"_s);
    connect(m_welcomeOpen, &QPushButton::clicked, this, &MainWindow::ouvre_dialogue);
    auto *boutons = new QHBoxLayout;
    boutons->setSpacing(12);
    boutons->addStretch();
    police_du_style(m_welcomeOpen, 1.1);
    boutons->addWidget(m_welcomeOpen);
    m_welcomeRecord = new QPushButton(page);
    m_welcomeRecord->setIcon(m_actRecord->icon());
    m_welcomeRecord->setObjectName(u"welcomeRecord"_s);
    police_du_style(m_welcomeRecord, 1.1);
    connect(m_welcomeRecord, &QPushButton::clicked, m_actRecord, &QAction::trigger);
    boutons->addWidget(m_welcomeRecord);
    boutons->addStretch();
    disposition->addLayout(boutons);
    disposition->addSpacing(10);
    m_welcomeDropHint = centre(new QLabel(page));
    m_devices = new AudioDevices(page);
    disposition->addWidget(m_devices, 0, Qt::AlignHCenter);
    disposition->addStretch(1);
    return page;
}

void MainWindow::dimensionne_boutons_accueil()
{
    // Ouvrir et enregistrer ont la même taille dans les deux langues.
    m_welcomeOpen->setMinimumSize(0, 0);
    m_welcomeRecord->setMinimumSize(0, 0);
    const QSize tailleConseillee = m_welcomeOpen->sizeHint().expandedTo(m_welcomeRecord->sizeHint());
    for (auto *bouton : {m_welcomeOpen, m_welcomeRecord})
        bouton->setMinimumSize(tailleConseillee.width() + tailleConseillee.height(), int(tailleConseillee.height() * 1.4));
}

QWidget *MainWindow::construit_volet_signal()
{
    auto *volet = new QWidget(this);
    auto *disposition = new QVBoxLayout(volet);
    disposition->setContentsMargins(4, 4, 4, 0);
    auto *entete = new QHBoxLayout;
    m_signalTitle = new QLabel(volet);
    police_du_style(m_signalTitle, 1.0, TITLE_WEIGHT);
    entete->addWidget(m_signalTitle);
    m_signalHint = etiquette_discrete(volet);
    entete->addWidget(m_signalHint, 1);
    disposition->addLayout(entete);
    disposition->addWidget(m_signal, 1);
    return volet;
}

QWidget *MainWindow::construit_volet_spectre()
{
    auto *volet = new QWidget(this);
    auto *disposition = new QVBoxLayout(volet);
    disposition->setContentsMargins(4, 4, 4, 4);
    auto *entete = new QHBoxLayout;
    m_spectrumTitle = new QLabel(volet);
    police_du_style(m_spectrumTitle, 1.0, TITLE_WEIGHT);
    entete->addWidget(m_spectrumTitle);
    m_spectrumHint = etiquette_discrete(volet);
    entete->addWidget(m_spectrumHint, 1);
    disposition->addLayout(entete);
    disposition->addWidget(m_spectrum, 1);
    return volet;
}

QWidget *MainWindow::construit_panneau_lateral()
{
    auto *panneau = new QWidget;
    auto *disposition = new QVBoxLayout(panneau);

    // ouvrir un son, les vues, les réglages (mode expert) : deux boutons par ligne
    auto *actions = new QGridLayout;
    actions->setSpacing(6);
    const auto bouton_action = [](QToolButton *bouton, QAction *action) {
        bouton->setDefaultAction(action);
        bouton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        bouton->setFocusPolicy(Qt::NoFocus);
        bouton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return bouton;
    };
    actions->addWidget(bouton_action(new QToolButton(panneau), m_actOpen), 0, 0);
    actions->addWidget(bouton_action(new QToolButton(panneau), m_actFitAll), 0, 1);
    m_zoomButton = bouton_action(new QToolButton(panneau), m_actZoomSelection);
    actions->addWidget(m_zoomButton, 1, 0);
    m_settingsButton = bouton_action(new QToolButton(panneau), m_actSettings);
    actions->addWidget(m_settingsButton, 1, 1);
    actions->setColumnStretch(0, 1);
    actions->setColumnStretch(1, 1);
    disposition->addLayout(actions);
    disposition->addSpacing(4);
    disposition->addWidget(separateur(panneau));

    m_soundBox = new QWidget(panneau);
    auto *dispositionSon = new QVBoxLayout(m_soundBox);
    dispositionSon->setContentsMargins(0, 0, 0, 0);
    m_soundName = etiquette_a_la_ligne(m_soundBox);
    police_du_style(m_soundName, 1.0, TITLE_WEIGHT);
    m_soundInfo = etiquette_discrete(m_soundBox);
    dispositionSon->addWidget(m_soundName);
    dispositionSon->addWidget(m_soundInfo);
    m_saveTakeButton = bouton_action(new QToolButton(m_soundBox), m_actSaveTake);
    m_saveTakeButton->setFocusPolicy(Qt::StrongFocus);
    dispositionSon->addWidget(m_saveTakeButton);

    // écoute : ce qu'on écoute (mode expert), lecture, pause, arrêt
    auto *transport = new QHBoxLayout;
    transport->addWidget(new QLabel(u"🔊"_s, m_soundBox));
    m_listenCombo = new QComboBox(m_soundBox);
    m_listenCombo->setObjectName(u"listen"_s);
    m_listenCombo->addItems({QString(), QString()});
    m_listenLabel = new QLabel(m_soundBox);
    m_playButton = bouton_transport(u"▶"_s, u"play"_s, m_soundBox);
    m_pauseButton = bouton_transport(u"⏸"_s, u"pause"_s, m_soundBox);
    m_stopButton = bouton_transport(u"⏹"_s, u"stop"_s, m_soundBox);
    transport->addWidget(m_listenCombo);
    transport->addWidget(m_listenLabel);
    transport->addWidget(m_playButton);
    transport->addWidget(m_pauseButton);
    transport->addWidget(m_stopButton);
    transport->addStretch();
    dispositionSon->addSpacing(4);
    dispositionSon->addLayout(transport);
    dispositionSon->addSpacing(4);
    dispositionSon->addWidget(separateur(m_soundBox));

    m_mesures = new PanneauMesures(m_soundBox);
    dispositionSon->addWidget(m_mesures);

    disposition->addWidget(m_soundBox);
    disposition->addStretch();

    connect(m_listenCombo, &QComboBox::activated, this,
            [this](int index) { choisit_ecoute(index == 0 ? Listen::Sound : Listen::Extract); });
    connect(m_playButton, &QToolButton::clicked, this, &MainWindow::joue);
    connect(m_pauseButton, &QToolButton::clicked, this, [this] {
        m_player.pause();
        met_a_jour_lecture();
    });
    connect(m_stopButton, &QToolButton::clicked, this, [this] {
        m_player.arrete();
        met_a_jour_lecture();
    });
    connect(m_mesures, &PanneauMesures::intervalle_demande, this, &MainWindow::analyse_intervalle);
    connect(m_mesures, &PanneauMesures::aimantation_changee, this, [this](bool checked) {
        m_session.params.snapZero = checked;
        parametres_modifies();
    });
    connect(m_mesures, &PanneauMesures::detection_automatique_demandee, this, [this] {
        m_session.manualF0.reset();
        rafraichit();
    });
    connect(m_mesures, &PanneauMesures::copie_demandee, this, &MainWindow::copie_tableau);
    return panneau;
}

void MainWindow::affiche_page(bool sonCharge)
{
    m_pages->setCurrentIndex(sonCharge ? 1 : 0);
    if (!sonCharge || m_sideSized)
        return;
    // Le panneau acquiert sa largeur après le premier affichage de la page de travail.
    m_sideSized = true;
    QTimer::singleShot(0, this, [this] {
        const int largeur = std::max({LARGEUR_PANNEAU, fontMetrics().averageCharWidth() * 38,
                                      m_side->parentWidget()->minimumSizeHint().width()});
        m_hsplit->setSizes({largeur, std::max(m_hsplit->width() - largeur, 1)});
    });
}

void MainWindow::partage_hauteur_signal()
{
    const int total = m_vsplit->height();
    if (m_splitMoved || total <= 0 || m_spectrumPane->isHidden())
        return;
    const int signal = std::max(int(total * PART_SIGNAL), HAUTEUR_SIGNAL_MIN);
    m_vsplit->setSizes({signal, std::max(total - signal, 1)});
}

void MainWindow::ajuste_panneau_lateral()
{
    // Contenu plus large (mode expert, langue, agrandissement) : le volet s'élargit d'autant.
    const int largeur = m_side->minimumSizeHint().width();
    if (largeur != m_sideMinWidth) {
        m_sideMinWidth = largeur;
        m_side->updateGeometry();
    }
}
