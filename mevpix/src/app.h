// Fenêtre principale : menus, barre d'outils, page d'accueil, panneau latéral, zone image et
// graphique du profil, barre d'état.

#pragma once

#include "i18n.h"
#include "icons.h"
#include "session.h"
#include "settings.h"
#include "view.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QTranslator>

#include <optional>
#include <vector>

class ImageCanvas;
class ProfilePlot;
class QAction;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QToolBar;
class QToolButton;

/// Taille de la fenêtre au premier lancement (ramenée à l'écran s'il est plus petit).
inline constexpr QSize DEFAULT_WINDOW(1200, 700);
/// Taille minimale de la fenêtre.
inline constexpr QSize MIN_WINDOW(800, 500);

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(const Settings &settings, const SettingsFiles &files, const QString &startupPath);

    /// Charge l'image dans un fil de fond (`busy` pendant le chargement).
    void loadPath(const QString &path);
    bool busy() const;

    // Pour les essais de l'interface.
    ImageCanvas *canvas() const { return m_canvas; }
    ProfilePlot *plot() const { return m_plot; }
    const Session &session() const { return m_session; }
    Lang lang() const { return m_lang; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const Tr &tr() const { return texts(m_lang); }

    void buildActions();
    void buildMenus();
    void buildToolbar();
    /// Notice, thème et langue, en haut à droite de la barre de menus.
    void buildMenuCorner();
    /// Boutons du coin des menus : hauteur d'un titre de menu, icônes selon l'agrandissement.
    void sizeMenuCorner();
    void buildStatusBar();
    /// Page affichée tant qu'aucune image n'est ouverte : logo, nom, bouton Ouvrir.
    QWidget *buildWelcomePage();
    /// Bouton de la page d'accueil plus grand que la normale (après un changement de texte ou de police).
    void sizeWelcomeButton();
    QWidget *buildSidePanel();
    QWidget *buildCalibratePage();
    QWidget *buildMeasurePage();
    QWidget *buildProfilePage();

    /// Textes fixes, à reprendre après un changement de langue.
    void retranslate();
    /// Contenu qui dépend de l'état (outil, segments, étalonnage, profil…).
    void refresh();
    void refreshMeasureTable();
    void refreshCursors();
    /// Informations de droite de la barre d'état : image ouverte (nom raccourci si besoin) et zoom.
    void refreshStatusInfo();
    /// Élargit le panneau latéral si son contenu ne tient plus (agrandissement, langue, outil).
    void fitSidePanel();

    void setTool(Tool tool);
    void setLanguage(std::optional<Lang> choice);
    void setTheme(Qt::ColorScheme choice);

    /// Message de la barre d'état, avec sa nature (pictogramme et couleur).
    struct StatusMessage {
        QString text;
        icons::Status kind = icons::Status::Info;
    };
    void setStatus(const QString &text, icons::Status kind = icons::Status::Info);
    void setStatus(const StatusMessage &message) { setStatus(message.text, message.kind); }
    /// Hauteur de la barre d'état : une ligne, plus si le message ne tient pas.
    void fitStatusBar();
    /// Fond, filets et couleurs de la barre d'état (selon le thème).
    void styleStatusBar();
    void openHelp();
    void showAbout();

    // Chargement
    void openDialog();
    void onLoaded();
    void setImage(std::shared_ptr<const LoadedImage> img);

    // Segments
    void commit(const Segment &seg);
    void validateCalibration();
    void copyMeasures();
    void exportCsv();

    // Réglages
    void setUiZoom(double zoom);
    void applyUiZoom();
    double maxUiZoom() const;
    /// Polices des titres et des valeurs, proportionnelles à celle de l'interface.
    void styleFont(QWidget *widget, double factor, QFont::Weight weight = QFont::Normal, bool mono = false);
    void applyFonts();
    Settings currentSettings() const;
    /// Enregistre après une seconde sans changement : on n'écrit pas le fichier à chaque pas pendant
    /// qu'on redimensionne la fenêtre ou qu'on glisse un curseur.
    void settingsChanged();
    /// Enregistre dans le fichier de l'utilisateur, jamais dans le fichier imposé à côté du programme.
    void writeSettings();

    Session m_session;
    ImageCanvas *m_canvas = nullptr;
    ProfilePlot *m_plot = nullptr;
    /// Page d'accueil (0) ou image et panneau (1).
    QStackedWidget *m_pages = nullptr;
    QSplitter *m_hsplit = nullptr;
    QSplitter *m_vsplit = nullptr;
    QScrollArea *m_side = nullptr;
    /// Largeur minimale du panneau latéral au dernier ajustement.
    int m_sideMinWidth = 0;
    /// Largeur du panneau donnée à son premier affichage, après la page d'accueil.
    bool m_sideSized = false;
    bool m_plotSized = false;
    /// Voile affiché pendant qu'on glisse un fichier au-dessus de la fenêtre.
    QWidget *m_dropOverlay = nullptr;

    /// Langue choisie avec le bouton (vide : celle du système) et langue affichée.
    std::optional<Lang> m_langChoice;
    Lang m_lang;
    /// Thème choisi avec le bouton (`Unknown` : celui du bureau, avant le premier clic).
    Qt::ColorScheme m_theme;
    QTranslator m_qtTranslator;
    SettingsFiles m_files;
    Settings m_saved;
    QTimer m_saveTimer;
    /// L'échec d'enregistrement des réglages n'est signalé qu'une fois.
    bool m_settingsErrorShown = false;
    /// Dossier proposé par les boîtes de dialogue.
    QString m_lastDir;
    std::optional<QSize> m_windowSize;
    std::optional<QSize> m_previousWindowSize;
    bool m_maximized = false;
    /// Agrandissement affiché, et celui demandé (retenu dans les réglages ; l'affiché est borné par
    /// la largeur de la fenêtre).
    double m_uiZoom = 1.0;
    double m_uiZoomWanted = 1.0;
    QFont m_baseFont;

    struct ScaledFont {
        QPointer<QWidget> widget;
        double factor;
        QFont::Weight weight;
        bool mono;
    };
    std::vector<ScaledFont> m_fonts;

    QFutureWatcher<LoadResult> m_loader;
    QString m_loadingPath;
    /// Message à rétablir quand une boîte de dialogue est annulée.
    StatusMessage m_idleStatus;
    StatusMessage m_status;
    /// Zoom de l'image affiché dans la barre d'état (pixels écran par pixel image).
    double m_zoom = 0.0;

    // Actions et menus
    QToolBar *m_toolbar = nullptr;
    std::vector<QToolButton *> m_cornerButtons;
    QMenu *m_menuFile = nullptr;
    QMenu *m_menuHelp = nullptr;
    QAction *m_actOpen = nullptr;
    QAction *m_actQuit = nullptr;
    /// Boutons de droite de la barre d'outils : thème (soleil ou lune), langue (drapeau).
    QAction *m_actTheme = nullptr;
    QAction *m_actLanguage = nullptr;
    QAction *m_actManual = nullptr;
    QAction *m_actAbout = nullptr;
    QAction *m_actAboutQt = nullptr;
    QAction *m_actCalibrate = nullptr;
    QAction *m_actMeasure = nullptr;
    QAction *m_actProfile = nullptr;
    QAction *m_actFit = nullptr;
    /// Échelle de l'image, dans la barre d'outils (et les actions qui l'y montrent ou la cachent).
    QLabel *m_scaleLabel = nullptr;
    QAction *m_scaleAction = nullptr;
    QAction *m_scaleSeparator = nullptr;

    // Page d'accueil
    QLabel *m_welcomeTagline = nullptr;
    QPushButton *m_welcomeOpen = nullptr;
    QLabel *m_welcomeDropHint = nullptr;

    // Barre d'état : pictogramme et message à gauche ; image et zoom à droite.
    QLabel *m_statusIcon = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_busy = nullptr;
    QLabel *m_imageStatus = nullptr;
    QLabel *m_zoomStatus = nullptr;

    // Panneau latéral
    QWidget *m_calibratePage = nullptr;
    QWidget *m_measurePage = nullptr;
    QWidget *m_profilePage = nullptr;

    QLabel *m_calibHeading = nullptr;
    QLabel *m_calibStep1 = nullptr;
    QLabel *m_calibStep2 = nullptr;
    QLabel *m_calibSegment = nullptr;
    QLabel *m_realLength = nullptr;
    QLineEdit *m_calibInput = nullptr;
    QComboBox *m_unitCombo = nullptr;
    QLabel *m_invalidLength = nullptr;
    QPushButton *m_validate = nullptr;
    QLabel *m_currentScale = nullptr;
    QLabel *m_calibChanged = nullptr;

    QLabel *m_measuresHeading = nullptr;
    QLabel *m_uncalibrated = nullptr;
    QLabel *m_measureHint = nullptr;
    QTableWidget *m_measureTable = nullptr;
    QLabel *m_noMeasure = nullptr;
    QWidget *m_measureButtons = nullptr;
    QPushButton *m_copyMeasures = nullptr;
    QPushButton *m_clearAll = nullptr;

    QLabel *m_profileHeading = nullptr;
    QLabel *m_profileHint = nullptr;
    QLabel *m_channelLabel = nullptr;
    QComboBox *m_channelCombo = nullptr;
    QLabel *m_thicknessLabel = nullptr;
    QSlider *m_bandSlider = nullptr;
    QSpinBox *m_bandSpin = nullptr;
    QWidget *m_cursorsBox = nullptr;
    QLabel *m_cursorsTitle = nullptr;
    QLabel *m_cursorsHint = nullptr;
    QLabel *m_cursorName[2] = {};
    QLabel *m_cursorX[2] = {};
    QLabel *m_cursorI[2] = {};
    QLabel *m_deltaX = nullptr;
    QLabel *m_saturated = nullptr;
    QPushButton *m_exportCsv = nullptr;
};
