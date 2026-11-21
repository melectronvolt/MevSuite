// Fenêtre principale : menus, barre d'outils, page d'accueil, panneau latéral, zone image et
// graphique du profil, barre d'état.

#pragma once

#include "fichier_seance.h"
#include "i18n.h"
#include "icons.h"
#include "session.h"
#include "settings.h"
#include "view.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QTranslator>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class CanevasImage;
class GraphiqueProfil;
class QAction;
class QBoxLayout;
class QComboBox;
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
class TableauMesures;
class QToolBar;
class QToolButton;

/// Taille de la fenêtre au premier lancement (ramenée à l'écran s'il est plus petit).
inline constexpr QSize DEFAULT_WINDOW(1200, 700);
/// Taille minimale de la fenêtre.
inline constexpr QSize MIN_WINDOW(800, 500);

class FenetrePrincipale : public QMainWindow {
    Q_OBJECT

public:
    FenetrePrincipale(const Reglages &settings, const FichiersReglages &files, const QString &startupPath);

    /// L'ouverture attend la fin d'une éventuelle sauvegarde.
    void charge_chemin(const QString &path);
    /// Vrai pendant la lecture d'une photo ou l'écriture d'une séance.
    bool occupe() const;

    // Pour les essais de l'interface.
    CanevasImage *canevas() const { return m_canevas; }
    GraphiqueProfil *graphique() const { return m_graphique; }
    const Seance &session() const { return m_seance; }
    Lang langue() const { return m_lang; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static constexpr int LARGEUR_PANNEAU = 300;
    static constexpr std::array SENS_FLECHES{QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)};

    const Textes &tr() const { return textes(m_lang); }

    void construit_actions();
    void construit_menus();
    void construit_barre_outils();
    void construit_zoom_sur_image();
    void dispose_commandes_image();
    /// Boutons de thème et de langue placés à droite des menus.
    void construit_coin_des_menus();
    /// Boutons du coin des menus : hauteur d'un titre de menu, icônes selon l'agrandissement.
    void dimensionne_coin_des_menus();
    void construit_barre_etat();
    /// Page affichée tant qu'aucune image n'est ouverte : logo, nom, bouton Ouvrir.
    QWidget *construit_page_accueil();
    /// Bouton de la page d'accueil plus grand que la normale (après un changement de texte ou de police).
    void dimensionne_bouton_accueil();
    QWidget *construit_panneau_lateral();
    QWidget *construit_page_etalonnage();
    QWidget *construit_page_mesures();
    QWidget *construit_page_profil();

    /// Textes fixes, à reprendre après un changement de langue.
    void retraduit();
    /// Contenu qui dépend de l'état (outil, segments, étalonnage, profil…).
    void rafraichit();
    void rafraichit_etalonnage();
    void trace_modifie(CibleTrace cible);
    void rafraichit_tableau_mesures();
    void rafraichit_curseurs();
    /// Informations de droite de la barre d'état : image ouverte (nom raccourci si besoin) et zoom.
    void rafraichit_infos_etat();
    /// Élargit le panneau latéral si son contenu ne tient plus (agrandissement, langue, outil).
    void ajuste_panneau_lateral();

    void choisit_outil(Outil outil);
    void change_langue(std::optional<Lang> choice);
    void change_theme(Qt::ColorScheme choice);

    /// Message de la barre d'état, avec sa nature (pictogramme et couleur).
    struct StatusMessage {
        QString text;
        icons::Status kind = icons::Status::Info;
    };
    void affiche_statut(const QString &text, icons::Status kind = icons::Status::Info);
    void affiche_statut(const StatusMessage &message) { affiche_statut(message.text, message.kind); }
    /// Hauteur de la barre d'état : une ligne, plus si le message ne tient pas.
    void ajuste_barre_etat();
    /// Fond, filets et couleurs de la barre d'état (selon le thème).
    void style_barre_etat();
    void ouvre_notice();
    void montre_a_propos();

    // Ouverture, sauvegarde et export (app_fichiers.cpp).
    void ouvre_dialogue();
    void enregistre_projet(bool choisirNom = false);
    void ecrit_projet(QString chemin, bool choisirNom);
    void lance_ecriture(const QByteArray &empreinte);
    void apres_sauvegarde(const ResultatSauvegarde &resultat);
    void termine_sauvegarde(bool reussie);
    enum class ChoixAbandon { Annuler, Abandonner, Enregistrer };
    ChoixAbandon confirme_abandon(const QString &message, const QString &nom);
    bool confirme_echelle_export();
    void exporte_annotation();
    QAction *m_sauver = nullptr, *m_sauverSous = nullptr, *m_annoter = nullptr;
    QString m_cheminSeance;
    QByteArray m_empreinteSeance;  ///< Contenu du .mevpix à la dernière ouverture ou sauvegarde réussie.
    Travail m_derniereSauvegarde;
    bool seance_modifiee() const;
    bool est_seance_courante(const QString &chemin) const;
    void apres_chargement();
    void propose_ouverture(const QString &chemin, const ResultatChargement &result);
    std::optional<ContenuSeance> m_travailAEcrire;
    QString m_destinationEcriture;
    /// Une ouverture ou une fermeture demandée pendant l'écriture attend sa réussite.
    std::function<void()> m_apresSauvegarde;
    /// Dernier Ctrl+S reçu pendant l'écriture, avec les mesures qui existaient à ce clic.
    std::optional<ContenuSeance> m_prochaineEcriture;

    // Segments
    void valide(const Segment &seg);
    void termine_saisie();
    void valide_etalonnage();
    void copie_mesures();
    void exporte_csv();

    // Réglages
    void change_agrandissement(double zoom);
    void applique_agrandissement();
    double agrandissement_max() const;
    /// Polices des titres et des valeurs, proportionnelles à celle de l'interface.
    void police_du_style(QWidget *widget, double factor, QFont::Weight weight = QFont::Normal, bool mono = false);
    void applique_polices();
    Reglages reglages_actuels() const;
    /// Enregistre après une seconde sans changement : on n'écrit pas le fichier à chaque pas pendant
    /// qu'on redimensionne la fenêtre ou qu'on glisse un curseur.
    void reglages_modifies();
    /// Enregistre dans le fichier de l'utilisateur, jamais dans le fichier imposé à côté du programme.
    void ecrit_reglages();

    Seance m_seance;
    CanevasImage *m_canevas = nullptr;
    GraphiqueProfil *m_graphique = nullptr;
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
    QLabel *m_voileDepot = nullptr;

    /// Langue choisie avec le bouton (vide : celle du système) et langue affichée.
    std::optional<Lang> m_langChoice;
    Lang m_lang;
    /// Thème choisi avec le bouton (`Unknown` : celui du bureau, avant le premier clic).
    Qt::ColorScheme m_theme;
    QTranslator m_qtTranslator;
    FichiersReglages m_fichiersReglages;
    Reglages m_reglagesEnregistres;
    QTimer m_delaiReglages;
    /// Une erreur d'écriture des réglages est signalée une fois, jusqu'au prochain succès.
    bool m_settingsErrorShown = false;
    /// Dossier proposé par les boîtes de dialogue.
    QString m_dernierDossier;
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

    QFutureWatcher<ResultatChargement> m_lecture;
    QString m_cheminChargement;
    bool m_fermetureEnCours = false;
    /// Message à rétablir quand une boîte de dialogue est annulée.
    StatusMessage m_idleStatus;
    StatusMessage m_statut;
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
    QAction *m_actionEtalonner = nullptr;
    QAction *m_actionMesurer = nullptr;
    QAction *m_actionProfil = nullptr;
    QAction *m_agrandirImage = nullptr;
    QAction *m_reduireImage = nullptr;
    QAction *m_ajusterImage = nullptr;
    std::array<QAction *, 4> m_actionsDeplacement{};
    QBoxLayout *m_groupesNavigation = nullptr;
    /// Échelle de l'image, dans la barre d'outils (et les actions qui l'y montrent ou la cachent).
    QLabel *m_scaleLabel = nullptr;
    QAction *m_scaleAction = nullptr;
    QAction *m_scaleSeparator = nullptr;

    // Page d'accueil
    QLabel *m_welcomeTagline = nullptr;
    QPushButton *m_welcomeOpen = nullptr;
    QLabel *m_welcomeDropHint = nullptr;

    // Barre d'état : pictogramme et message à gauche ; image et zoom à droite.
    QLabel *m_iconeEtat = nullptr;
    std::array<QPixmap, 4> m_pictosStatut;
    QLabel *m_messageEtat = nullptr;
    QProgressBar *m_indicateurActivite = nullptr;
    QLabel *m_infoImage = nullptr;
    QLabel *m_infoZoom = nullptr;

    // Panneau latéral
    QWidget *m_calibratePage = nullptr;
    QWidget *m_measurePage = nullptr;
    QWidget *m_profilePage = nullptr;

    QLabel *m_calibHeading = nullptr;
    QLabel *m_calibStep1 = nullptr;
    QLabel *m_calibStep2 = nullptr;
    QLabel *m_longueurSegment = nullptr;
    QLabel *m_realLength = nullptr;
    QLineEdit *m_champLongueur = nullptr;
    std::optional<Travail> m_avantSaisie;
    QComboBox *m_unitCombo = nullptr;
    QLabel *m_invalidLength = nullptr;
    QPushButton *m_validate = nullptr;
    QLabel *m_currentScale = nullptr;
    QLabel *m_alerteEtalonnage = nullptr;

    QLabel *m_measuresHeading = nullptr;
    QLabel *m_uncalibrated = nullptr;
    QLabel *m_measureHint = nullptr;
    TableauMesures *m_tableauMesures = nullptr;
    QLabel *m_noMeasure = nullptr;
    QWidget *m_measureButtons = nullptr;
    QPushButton *m_copyMeasures = nullptr;
    QPushButton *m_clearAll = nullptr;

    QLabel *m_profileHeading = nullptr;
    QLabel *m_profileHint = nullptr;
    QLabel *m_channelLabel = nullptr;
    QComboBox *m_channelCombo = nullptr;
    QLabel *m_thicknessLabel = nullptr;
    QSlider *m_glissiereEpaisseur = nullptr;
    QSpinBox *m_champEpaisseur = nullptr;
    QWidget *m_cursorsBox = nullptr;
    QLabel *m_cursorsTitle = nullptr;
    QLabel *m_cursorsHint = nullptr;
    QToolButton *m_boutonsCurseurs[2] = {};
    QLabel *m_positionsCurseurs[2] = {};
    QLabel *m_intensitesCurseurs[2] = {};
    QLabel *m_deltaX = nullptr;
    QLabel *m_alerteProfil = nullptr;
    QPushButton *m_exportCsv = nullptr;
};
