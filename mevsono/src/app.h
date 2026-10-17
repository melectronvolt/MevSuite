// Fenêtre principale : menus (notice, thème et langue à leur droite), page d'accueil tant qu'aucun son
// n'est ouvert, puis signal (en haut), spectre de l'extrait (en bas), panneau des mesures et des
// actions courantes (à gauche) ; barre d'état, fenêtre des réglages.

#pragma once

#include "analyzer.h"
#include "audio.h"
#include "i18n.h"
#include "icons.h"
#include "player.h"
#include "session.h"
#include "settings.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QTranslator>

#include <array>
#include <atomic>
#include <optional>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTabBar;
class QToolButton;
class SettingsDialog;
class SignalPlot;
class SpectrumPlot;

/// Taille de la fenêtre au premier lancement non agrandi : tient dans un écran 1366 × 768.
inline constexpr QSize DEFAULT_WINDOW(1200, 700);
/// Taille minimale de la fenêtre.
inline constexpr QSize MIN_WINDOW(860, 560);

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(const Settings &settings, const SettingsFiles &files, const QString &startupPath);
    ~MainWindow() override;

    /// Charge un son d'exemple (menu Fichier › Exemples).
    /// Son d'exemple fabriqué par le programme (essais automatiques seulement : le logiciel n'en
    /// propose pas).
    void loadExample(audio::Example example);
    /// Lit un son dans un fil de fond (`busy` pendant la lecture).
    void loadPath(const QString &path);

    // Pour les essais de l'interface.
    const Session &session() const { return m_session; }
    const Analyzer &analyzer() const { return m_analyzer; }
    Player &player() { return m_player; }
    SignalPlot *signalPlot() const { return m_signal; }
    SpectrumPlot *spectrumPlot() const { return m_spectrum; }
    SettingsDialog *settingsDialog() const;
    /// Fondamentale utilisée : choisie à la main, sinon détectée.
    std::optional<double> f0() const;
    std::vector<dsp::Harmonic> harmonics() const;
    /// Position de lecture sur le signal (secondes), pendant la lecture ou la pause.
    std::optional<double> playhead() const;
    /// Bornes horizontales imposées au graphique du signal.
    void showSignal(double t0, double t1);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    const Tr &tr() const { return texts(m_lang); }

    void buildActions();
    void buildMenus();
    /// Notice, thème et langue, à droite de la barre de menus.
    void buildMenuCorner();
    void sizeMenuCorner();
    void buildStatusBar();
    /// Page affichée tant qu'aucun son n'est ouvert : ouvrir un son ou essayer un exemple.
    QWidget *buildWelcomePage();
    QWidget *buildSignalPane();
    QWidget *buildSpectrumPane();
    QWidget *buildSidePanel();

    /// Textes fixes, à reprendre après un changement de langue.
    void retranslate();
    /// Tout ce qui dépend de l'état (son, extrait, analyse, mode, lecture…).
    void refresh();
    void refreshSidePanel();
    void refreshHarmonics(const std::vector<dsp::Harmonic> &harmonics);
    void refreshTransport();
    /// Largeur minimale du panneau des mesures, selon son contenu.
    void fitSidePanel();
    /// Son et mode, à droite de la barre d'état.
    void refreshStatusInfo();

    void setLanguage(std::optional<Lang> choice);
    void setTheme(Qt::ColorScheme choice);
    void setMode(bool expert);
    /// Onglet choisi : mesure de la période ou étude des fréquences.
    void setTool(Tool tool);
    /// Part de la hauteur donnée au signal, au-dessus du spectre.
    void shareSignal();

    /// Message de la barre d'état, avec sa nature (pictogramme et couleur).
    struct StatusMessage {
        QString text;
        icons::Status kind = icons::Status::Info;
    };
    void setStatus(const QString &text, icons::Status kind = icons::Status::Info);
    void setStatus(const StatusMessage &message) { setStatus(message.text, message.kind); }
    /// Fond et bordure de la barre d'état, couleur du message (selon le thème).
    void styleStatusBar();
    /// Hauteur de la barre d'état : une ligne, ou plus pour un long message.
    void fitStatusBar();
    void openHelp();
    void showAbout();
    void showSettings();
    /// Un réglage a changé (menus, fenêtre des réglages, panneau) : vues à remettre à zéro, analyse.
    void paramsEdited();

    // Chargement
    void openDialog();
    void onLoaded();
    void setAudio(audio::Audio audio);
    bool busy() const;

    // Analyse
    void requestAnalysis();
    const dsp::Analysis *analysis() const;
    void analysisUpdated();

    // Écoute
    void play();
    void togglePlayback();
    /// Suit la lecture : tête de lecture, fin de lecture, erreurs de la sortie audio.
    void updatePlayback();
    void setListen(Listen listen);

    // Exports
    void exportSpectrum();
    void exportExtract();
    void saveCsv(const QString &csv, const QString &title, const QString &name);
    void copyTable();

    // Réglages
    void setUiZoom(double zoom);
    /// Applique l'agrandissement demandé, borné par la largeur de la fenêtre.
    void applyUiZoom();
    double maxUiZoom() const;
    /// Polices des titres et des valeurs, proportionnelles à celle de l'interface.
    void styleFont(QWidget *widget, double factor, QFont::Weight weight = QFont::Normal);
    void applyFonts();
    /// Boutons de la page d'accueil : de même taille, plus hauts que la normale.
    void sizeWelcomeButtons();
    Settings currentSettings() const;
    /// Enregistre après une seconde sans changement : on n'écrit pas le fichier à chaque pas pendant
    /// qu'on redimensionne la fenêtre ou qu'on glisse un curseur.
    void settingsChanged();
    /// Enregistre dans le fichier de l'utilisateur, jamais dans le fichier imposé à côté du programme.
    void writeSettings();

    Session m_session;
    /// Rang de l'exemple ouvert (son de synthèse) : son nom suit la langue.
    std::optional<size_t> m_example;
    Analyzer m_analyzer;
    Player m_player;
    /// Réglages déjà pris en compte, pour savoir ce qu'un changement modifie.
    Settings m_appliedParams;

    SignalPlot *m_signal = nullptr;
    SpectrumPlot *m_spectrum = nullptr;
    /// Page d'accueil (sans son), puis l'analyse.
    QStackedWidget *m_pages = nullptr;
    QSplitter *m_hsplit = nullptr;
    /// Panneau des mesures (défilant) ; sa largeur est donnée au premier affichage de l'analyse.
    QScrollArea *m_side = nullptr;
    bool m_sideSized = false;
    int m_sideMinWidth = 0;
    QSplitter *m_vsplit = nullptr;
    /// La séparation entre le signal et le spectre a été déplacée à la main ; sa place, pendant que
    /// le spectre est caché (mesure de la période).
    bool m_splitMoved = false;
    QList<int> m_splitSizes;
    /// Onglets des deux activités ; vue du signal gardée par chacune.
    QTabBar *m_tabs = nullptr;
    std::array<std::optional<std::pair<double, double>>, 2> m_signalViews;
    QPointer<SettingsDialog> m_settingsDialog;
    QWidget *m_dropOverlay = nullptr;

    /// Langue choisie (vide : celle du système) et langue affichée.
    Lang m_lang;
    QTranslator m_qtTranslator;
    SettingsFiles m_files;
    Settings m_saved;
    QTimer m_saveTimer;
    /// L'échec d'enregistrement des réglages n'est signalé qu'une fois.
    bool m_settingsErrorShown = false;
    std::optional<QSize> m_windowSize;
    std::optional<QSize> m_previousWindowSize;
    bool m_maximized = false;
    /// Agrandissement affiché (borné par la largeur de la fenêtre) et agrandissement demandé (retenu).
    double m_uiZoom = 1.0;
    double m_uiZoomWanted = 1.0;
    QFont m_baseFont;

    struct ScaledFont {
        QPointer<QWidget> widget;
        double factor;
        QFont::Weight weight;
    };
    std::vector<ScaledFont> m_fonts;

    QFutureWatcher<audio::LoadResult> m_loader;
    /// fermeture de la fenêtre : le chargement en cours s'arrête (ffmpeg compris)
    std::atomic_bool m_cancelLoad = false;
    QString m_loadingPath;
    /// Message à rétablir quand une boîte de dialogue est annulée.
    StatusMessage m_idleStatus;
    StatusMessage m_status;
    /// Suivi de la lecture (tête de lecture sur le signal).
    QTimer m_playTimer;

    // Menus ; à droite de leur barre, les boutons de la notice, du thème et de la langue
    QWidget *m_menuCorner = nullptr;
    std::vector<QToolButton *> m_cornerButtons;
    /// Boutons du haut du panneau de gauche qui ne suivent pas seuls une action.
    QToolButton *m_settingsButton = nullptr;
    QToolButton *m_zoomButton = nullptr;
    QMenu *m_menuFile = nullptr;
    QMenu *m_menuView = nullptr;
    QMenu *m_menuChannel = nullptr;
    QMenu *m_menuMode = nullptr;
    QMenu *m_menuHelp = nullptr;
    QAction *m_actOpen = nullptr;
    QAction *m_sepExports = nullptr;
    QAction *m_actExportSpectrum = nullptr;
    QAction *m_actExportExtract = nullptr;
    QAction *m_actQuit = nullptr;
    QAction *m_actFitAll = nullptr;
    QAction *m_actZoomSelection = nullptr;
    QAction *m_sepExpertView = nullptr;
    QAction *m_actDb = nullptr;
    QAction *m_actLog = nullptr;
    QAction *m_actMarks = nullptr;
    std::vector<QAction *> m_actChannels;
    QAction *m_sepSettings = nullptr;
    QAction *m_actSettings = nullptr;
    QAction *m_actSimple = nullptr;
    QAction *m_actExpert = nullptr;
    /// Boutons à droite de la barre de menus : thème (soleil ou lune), langue (drapeau).
    QAction *m_actTheme = nullptr;
    QAction *m_actLanguage = nullptr;
    /// Notice : entrée du menu Aide, bouton à droite de la barre de menus.
    QAction *m_actManual = nullptr;
    QAction *m_actAbout = nullptr;
    QAction *m_actAboutQt = nullptr;

    // Barre d'état : pictogramme et message à gauche ; son et mode à droite.
    QLabel *m_statusIcon = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_busy = nullptr;
    QLabel *m_soundStatus = nullptr;
    QLabel *m_modeStatus = nullptr;

    // Page d'accueil
    QLabel *m_welcomeTagline = nullptr;
    QPushButton *m_welcomeOpen = nullptr;
    QLabel *m_welcomeDropHint = nullptr;

    // Volets du signal et du spectre
    QLabel *m_signalTitle = nullptr;
    QLabel *m_signalHint = nullptr;
    QWidget *m_spectrumPane = nullptr;
    QLabel *m_spectrumTitle = nullptr;
    QLabel *m_spectrumHint = nullptr;

    // Panneau des mesures
    QWidget *m_soundBox = nullptr;
    QLabel *m_soundName = nullptr;
    QLabel *m_soundInfo = nullptr;
    QComboBox *m_listenCombo = nullptr;
    QLabel *m_listenLabel = nullptr;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_stopButton = nullptr;

    /// Une section par activité : mesure de la période, étude des fréquences.
    QWidget *m_periodSection = nullptr;
    QWidget *m_frequencySection = nullptr;
    QLabel *m_periodHeading = nullptr;
    QWidget *m_periodBox = nullptr;
    /// Noms des curseurs (t₁, t₂, A, B), de leur couleur, qui suit le thème.
    QLabel *m_t1Name = nullptr;
    QLabel *m_t2Name = nullptr;
    QLabel *m_aName = nullptr;
    QLabel *m_bName = nullptr;
    QLabel *m_t1Value = nullptr;
    QLabel *m_t2Value = nullptr;
    QLabel *m_deltaValue = nullptr;
    QLabel *m_periodHint = nullptr;
    QCheckBox *m_snapCheck = nullptr;
    QLabel *m_autocorrValue = nullptr;

    QLabel *m_extractHeading = nullptr;
    QLabel *m_aValue = nullptr;
    QLabel *m_bValue = nullptr;
    QLabel *m_extractInfo = nullptr;
    QWidget *m_computingBox = nullptr;
    QLabel *m_computing = nullptr;
    QLabel *m_tooShort = nullptr;
    QLabel *m_reticleHint = nullptr;

    QWidget *m_expertBox = nullptr;
    QLabel *m_dfLabel = nullptr;
    QLabel *m_dfValue = nullptr;
    QLabel *m_resolutionLabel = nullptr;
    QLabel *m_resolutionValue = nullptr;
    QLabel *m_fundamentalHeading = nullptr;
    QLabel *m_f0Value = nullptr;
    QWidget *m_f0ManualBox = nullptr;
    QLabel *m_f0Manual = nullptr;
    QPushButton *m_autoButton = nullptr;
    QLabel *m_f0Auto = nullptr;
    QLabel *m_f0None = nullptr;
    QTableWidget *m_harmonicsTable = nullptr;
    QPushButton *m_copyTable = nullptr;
    QLabel *m_f0NoPeak = nullptr;
};
