// Fenêtre principale : menus (thème et langue à leur droite), page d'accueil tant qu'aucun son
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
#include <functional>
#include <optional>
#include <vector>

class AudioDevices;
class PanneauMesures;
class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
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

    /// Lit un son dans un fil de fond (`busy` pendant la lecture).
    void charge_chemin(const QString &path);

    // Pour les essais de l'interface.
    const Session &session() const { return m_session; }
    const Analyzer &analyseur() const { return m_analyzer; }
    Player &lecteur() { return m_player; }
    SignalPlot *graphique_signal() const { return m_signal; }
    SpectrumPlot *graphique_spectre() const { return m_spectrum; }
    SettingsDialog *dialogue_reglages() const;
    /// Fondamentale utilisée : choisie à la main, sinon détectée.
    std::optional<double> fondamentale_courante() const;
    std::vector<dsp::Harmonic> harmoniques() const;
    /// Position de lecture sur le signal (secondes), pendant la lecture ou la pause.
    std::optional<double> tete_lecture() const;
    /// Bornes horizontales imposées au graphique du signal.
    void montre_signal(double t0, double t1);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    const Tr &tr() const { return textes(m_lang); }

    /// Assemble le panneau de gauche, les graphiques et les onglets au-dessus de l’accueil.
    void construit_espace_travail();
    /// Choisit l'accueil ou les graphiques selon la présence d'un son.
    void affiche_page(bool sonCharge);
    /// Raccorde les changements de sélection, les résultats d’analyse et les périphériques à la fenêtre.
    void relie_signaux();
    void installe_raccourcis();
    void construit_actions();
    void construit_menus();
    void retraduit_menus(const Tr &t);
    /// Thème et langue, à droite de la barre de menus.
    void construit_coin_des_menus();
    void dimensionne_coin_des_menus();
    void construit_barre_etat();
    /// Page où choisir un enregistrement au microphone ou ouvrir un fichier.
    QWidget *construit_page_accueil();
    QWidget *construit_volet_signal();
    QWidget *construit_volet_spectre();
    QWidget *construit_panneau_lateral();

    /// Textes fixes, à reprendre après un changement de langue.
    void retraduit();
    /// Tout ce qui dépend de l'état (son, extrait, analyse, mode, lecture…).
    void rafraichit();
    void rafraichit_panneau_lateral();
    void actualise_actions_spectre();
    void rafraichit_transport();
    /// Largeur minimale du panneau des mesures, selon son contenu.
    void ajuste_panneau_lateral();
    /// Son et mode, à droite de la barre d'état.
    void rafraichit_infos_etat();

    void change_langue(std::optional<Lang> choice);
    void change_theme(Qt::ColorScheme choice);
    void change_mode(bool expert);
    /// Onglet choisi : mesure de la période ou étude des fréquences.
    void choisit_outil(Tool tool);
    /// Part de la hauteur donnée au signal, au-dessus du spectre.
    void partage_hauteur_signal();

    /// Message de la barre d'état, avec sa nature (pictogramme et couleur).
    struct StatusMessage {
        QString text;
        icons::Status kind = icons::Status::Info;
    };
    void affiche_statut(const QString &text, icons::Status kind = icons::Status::Info);
    void affiche_statut(const StatusMessage &message) { affiche_statut(message.text, message.kind); }
    /// Fond et bordure de la barre d'état, couleur du message (selon le thème).
    void style_barre_etat();
    /// Hauteur de la barre d'état : une ligne, ou plus pour un long message.
    void ajuste_barre_etat();
    void ouvre_notice();
    void montre_a_propos();
    void montre_reglages();
    /// Un réglage a changé (menus, fenêtre des réglages, panneau) : vues à remettre à zéro, analyse.
    void parametres_modifies();

    // Chargement
    void ouvre_dialogue();
    void prepare_chargement(std::optional<audio::Audio> sonDejaLu = std::nullopt);
    void apres_chargement();
    void installe_son(audio::Audio audio, ApercuSon apercu);
    bool occupe() const;

    // Analyse
    void analyse_intervalle();
    void demande_analyse();
    const dsp::Analysis *analyse_courante() const;
    void analyse_mise_a_jour();

    // Écoute
    void sortie_changee();
    void montre_peripheriques();
    void enregistre_son();
    bool sauvegarde_prise();
    bool confirme_remplacement();
    void joue();
    void bascule_lecture();
    /// Suit la lecture : tête de lecture, fin de lecture, erreurs de la sortie audio.
    void met_a_jour_lecture();
    void choisit_ecoute(Listen listen);

    // Exports
    void exporte_spectre();
    void exporte_extrait();
    void enregistre_csv(const QString &title, const QString &name, size_t lignes,
                        const std::function<QString(size_t, size_t)> &bloc);
    void copie_tableau();

    // Réglages
    void change_agrandissement(double zoom);
    /// Applique l'agrandissement demandé, borné par la largeur de la fenêtre.
    void applique_agrandissement();
    double agrandissement_max() const;
    /// Polices des titres et des valeurs, proportionnelles à celle de l'interface.
    void police_du_style(QWidget *widget, double factor, QFont::Weight weight = QFont::Normal);
    void applique_polices();
    /// Boutons de la page d'accueil : de même taille, plus hauts que la normale.
    void dimensionne_boutons_accueil();
    Settings reglages_actuels() const;
    /// Enregistre après une seconde sans changement : on n'écrit pas le fichier à chaque pas pendant
    /// qu'on redimensionne la fenêtre ou qu'on glisse un curseur.
    void reglages_modifies();
    /// Enregistre dans le fichier de l'utilisateur, jamais dans le fichier imposé à côté du programme.
    void ecrit_reglages();

    AudioDevices *m_devices = nullptr;
    /// Une prise peut être écoutée avant sa sauvegarde. Demander confirmation si elle risque d’être perdue.
    bool m_prise = false;
    bool m_priseModifiee = false;
    Session m_session;
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

    struct Chargement {
        audio::LoadResult lecture;
        ApercuSon apercu;
    };
    QFutureWatcher<Chargement> m_loader;
    /// fermeture de la fenêtre : le chargement en cours s'arrête (ffmpeg compris)
    std::atomic_bool m_cancelLoad = false;
    /// Chemin demandé, conservé jusqu’à la réception du résultat par la fenêtre.
    QString m_loadingPath;
    /// Message à rétablir si l'utilisateur préfère garder sa prise après avoir ouvert un fichier.
    StatusMessage m_statutAvantChargement;
    StatusMessage m_status;
    /// Suivi de la lecture (tête de lecture sur le signal).
    QTimer m_playTimer;

    // Menus ; à droite de leur barre, les boutons du thème et de la langue
    QWidget *m_menuCorner = nullptr;
    std::vector<QToolButton *> m_cornerButtons;
    /// Boutons du haut du panneau de gauche qui ne suivent pas seuls une action.
    QToolButton *m_settingsButton = nullptr;
    QToolButton *m_zoomButton = nullptr;
    QMenu *m_menuAudio = nullptr;
    QMenu *m_menuFile = nullptr;
    QMenu *m_menuView = nullptr;
    QMenu *m_menuChannel = nullptr;
    QMenu *m_menuMode = nullptr;
    QMenu *m_menuHelp = nullptr;
    QAction *m_actDevices = nullptr;
    QAction *m_actRecord = nullptr;
    QAction *m_actSaveTake = nullptr;
    QAction *m_memorise = nullptr;
    QAction *m_oublie = nullptr;
    QAction *m_normalise = nullptr;
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
    /// Notice accessible depuis le menu Aide et par F1.
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
    QPushButton *m_welcomeRecord = nullptr;
    QPushButton *m_welcomeOpen = nullptr;
    QLabel *m_welcomeDropHint = nullptr;

    // Volets du signal et du spectre
    QLabel *m_signalTitle = nullptr;
    QLabel *m_signalHint = nullptr;
    QWidget *m_spectrumPane = nullptr;
    QLabel *m_spectrumTitle = nullptr;
    QLabel *m_spectrumHint = nullptr;

    // Panneau des mesures
    QToolButton *m_saveTakeButton = nullptr;
    QWidget *m_soundBox = nullptr;
    QLabel *m_soundName = nullptr;
    QLabel *m_soundInfo = nullptr;
    QComboBox *m_listenCombo = nullptr;
    QLabel *m_listenLabel = nullptr;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_stopButton = nullptr;

    PanneauMesures *m_mesures = nullptr;
};
