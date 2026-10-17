// La fenêtre principale : modes de travail, machine à états, menus, fichiers.
//
// Les états de la machine :
//
// - Debut : aucune vidéo
// - A : une vidéo est ouverte, on peut définir l'échelle ou démarrer
// - AB : préparation du suivi automatique (sélection des motifs)
// - B : suivi automatique en cours
// - C : sélection de l'étalon de l'échelle (entre A et A, ou D et D)
// - D : pointage manuel possible
// - E : pointage d'une série d'objets en cours
//
// Le mode Simplifié (guide.cpp) conduit l'élève pas à pas en s'appuyant sur ces mêmes états ; le mode
// Expert présente quatre onglets (Pointage, Coordonnées, Trajectoires, Grapheur). La fenêtre a le
// style commun aux logiciels de la suite (référence : mevsono/src), sans barre d'outils : la
// barre des menus donne tout (ouvrir une vidéo ou un exemple, changer de mode…) et porte à droite la
// notice, puis les boutons du thème (soleil ou lune) et de la langue (drapeau) ; la barre d'état,
// en bas, dit ce qu'il faut faire ou ce qui vient de se passer, et rappelle la vidéo ouverte, l'image
// affichée et le mode. Ctrl + et Ctrl − agrandissent ou réduisent toute l'interface.
//
// L'interface suit l'état : chaque action change l'état, puis `rafraichit()` met à jour tous les
// widgets visibles (textes dans la langue choisie, boutons actifs, valeurs). Les méthodes de la
// fenêtre sont réparties dans les fichiers de chaque partie, comme les parties de la version Rust.

#pragma once

#include "dialogues.h"
#include "format.h"
#include "guide.h"
#include "icones.h"
#include "onglet_graph.h"
#include "onglet_pointage.h"
#include "onglet_trajectoire.h"
#include "pointage.h"
#include "prefs.h"
#include "video.h"

#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QTranslator>

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

class EcranDemarrage;
class OngletCoord;
class PleinEcran;
class PageGuide;
class PageSansVideo;
class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabWidget;
class QToolButton;

/// taille de la fenêtre au premier lancement (réduite sur un petit écran), et taille minimale
inline constexpr QSize TAILLE_FENETRE(1200, 700);
inline constexpr QSize TAILLE_MIN_FENETRE(900, 600);
/// agrandissement de l'interface (police du système × agrandissement) : bornes
inline constexpr double AGRANDISSEMENT_MIN = 0.5;
inline constexpr double AGRANDISSEMENT_MAX = 3.0;

/// Les deux façons de travailler avec MeVCine
enum class Mode {
    /// procédure guidée en six étapes, un seul objet
    Simplifie,
    /// toutes les fonctions : plusieurs objets, suivi automatique, trajectoires, vitesses, grapheur
    Expert,
};

enum class Etat { Debut, A, AB, B, C, D, E };

/// les onglets du mode Expert, dans l'ordre
enum class Onglet { Pointage, Coordonnees, Trajectoires, Grapheur };

/// nom d'un état, pour les messages de débogage
QString nom_etat(Etat e);

class App : public QMainWindow {
    Q_OBJECT

public:
    /// `fichier` : vidéo ou projet à ouvrir (ligne de commande) ; `mode` : mode imposé
    App(const QString &fichier, std::optional<Mode> mode);
    ~App() override;

    // ------------------------------------------------------------------ l'état
    // (lu par les parties de l'interface et par les tests)

    prefs::Config config;
    /// mode de travail : celui de la dernière fois (réglage du poste), simplifié au premier lancement ;
    /// il se change dans le menu Mode
    Mode mode = Mode::Simplifie;
    Etat etat = Etat::Debut;
    /// état à restaurer après la sélection de l'étalon (A ou D)
    Etat etat_ancien = Etat::A;
    Onglet onglet = Onglet::Pointage;
    /// message de la barre d'état (vide : la consigne de l'état courant), et sa nature
    QString statut;
    icones::Statut type_statut = icones::Statut::Info;

    // la vidéo
    std::unique_ptr<VideoReader> video;
    /// chemin absolu de la vidéo (vide s'il n'y en a pas)
    QString filename;
    /// numéro de l'image courante, à partir de 1
    int index = 1;
    int image_max = 0;
    double fps = 25.0;
    /// la dernière image lue de la vidéo (l'image affichée), et sa clé (index, rotation)
    QImage image;
    std::optional<std::pair<int, int>> image_cle;

    // les pointages
    Pointage pointage;
    int objet_courant = 1;
    /// on refait la mesure de l'image courante, à la demande du tableau : l'image où revenir ensuite
    std::optional<int> refait;
    /// Mode Expert : le pointage est arrêté (« ⏹ Arrêter le pointage ») ; un clic sur l'image ne pointe
    /// plus, jusqu'à « ▶ Reprendre le pointage ». Les mesures, le tableau et les onglets restent.
    bool pointage_arrete = false;

    // l'échelle
    /// extrémités de l'étalon sélectionné, en attente de sa longueur
    std::optional<std::pair<Vecteur, Vecteur>> etalon_en_attente;
    /// sélection de l'étalon par deux clics : le premier est fait
    bool etalon_deux_clics = false;
    /// l'unité dans laquelle l'élève donne la longueur de l'étalon
    Unite unite_etalon = Unite::Metre;

    // le suivi automatique
    std::vector<QImage> motifs;
    std::vector<std::array<Vecteur, 2>> rects;
    std::vector<std::optional<QPoint>> points_probables;
    std::deque<int> pile_detections;

    // interactions avec l'image de la vidéo
    /// tracé en cours (étalon ou zone à suivre), en pixels d'image
    std::optional<std::array<Vecteur, 2>> drag;
    bool choix_origine = false;
    /// l'origine suit la souris (elle a été saisie pour la faire glisser)
    bool glisse_origine = false;
    /// position de la souris au-dessus de l'image, en pixels d'image
    std::optional<Vecteur> souris;
    /// pixels d'écran par pixel d'image de l'image affichée (pour la loupe)
    double vue_echelle = 1.0;

    // champs de saisie de l'onglet de pointage
    QString ips;
    int nb_obj = 1;
    bool suivi_auto = false;

    Guide guide;
    Trajectoire traj;
    Grapheur graph;
    /// dernier texte copié pour Python
    QString presse_papier;
    /// empreinte des mesures lors du dernier export, enregistrement ou ouverture : si elle n'a pas
    /// changé, rien ne sera perdu en quittant
    QString empreinte_sauvee;
    /// l'utilisateur a confirmé qu'il quittait sans enregistrer
    bool fermeture_confirmee = false;
    /// une vidéo est en cours d'ouverture (dans un autre fil) ; la fermeture de la fenêtre l'interrompt
    bool ouverture_en_cours = false;
    std::atomic_bool ouverture_annulee = false;
    /// la sauvegarde automatique : dernier texte écrit
    QString secours_texte;
    /// la dernière sauvegarde de secours a échoué (l'élève en a été averti)
    bool secours_en_echec = false;
    /// les dialogues à montrer ; le premier est affiché
    std::deque<Dialogue> dialogues;

    // ------------------------------------------------------------------ l'interface

    /// met à jour tous les widgets visibles d'après l'état
    void rafraichit();
    /// après un mouvement de la souris sur l'image : la loupe, les coordonnées, l'étalon
    void souris_bouge(std::optional<Vecteur> position, double echelle);
    /// un geste de la souris sur l'image de la vidéo
    void geste(const Geste &g, const dessin::Vue &vue);
    /// le curseur de la souris au-dessus de l'image : une croix quand un clic y fait quelque chose
    bool curseur_croix() const;
    /// Le curseur au-dessus du point `p` de l'image (rien : hors de l'image), vue à `echelle` points
    /// d'écran par pixel : une main sur l'origine qu'on peut faire glisser, sinon une croix quand un
    /// clic fait quelque chose
    Qt::CursorShape curseur(std::optional<Vecteur> p, double echelle) const;
    /// peut-on faire glisser l'origine ? (étapes 1 et 2 du mode Simplifié ; en mode Expert, avant le
    /// pointage, ou après le bouton « Placer l'origine »)
    bool origine_deplacable() const;
    /// le point `p` de l'image est-il sur l'origine, à `echelle` points d'écran par pixel ?
    bool pres_de_l_origine(Vecteur p, double echelle) const;
    /// le dialogue affiché, s'il y en a un
    QDialog *dialogue_affiche() const { return m_boite; }
    /// l'écran de démarrage, tant qu'il est là
    EcranDemarrage *ecran_demarrage() const { return m_demarrage; }

    bool simplifie() const { return mode == Mode::Simplifie; }
    /// dimensions des images de la vidéo, rotation comprise
    QSize dimensions() const;

    // modes et machine à états
    void change_mode(Mode m);
    void change_etat(Etat e);
    /// les onglets autres que celui du pointage sont-ils accessibles ?
    bool onglets_actifs() const;
    /// les contrôles de changement d'image sont-ils actifs ?
    bool controle_image_actif() const;
    /// peut-on changer l'origine ou le sens des axes ? (y compris après le pointage : les mesures
    /// sont alors recalculées)
    bool repere_modifiable() const;
    /// peut-on tourner la vidéo ? Seulement avant l'échelle et le pointage
    bool rotation_permise() const;
    /// la ligne de statut du mode Expert commence par une indication de l'état
    QString message_etat() const;
    /// la consigne pendant la sélection de l'étalon
    QString consigne_etalon() const;
    /// le message de la barre d'état quand aucun autre n'est affiché
    QString message_par_defaut() const;
    void affiche_statut(const QString &texte, icones::Statut type = icones::Statut::Info)
    {
        statut = texte;
        type_statut = type;
    }
    void message(const QString &titre, const QString &texte);

    // vidéo
    bool ouvre_video(const QString &chemin, int rotation);
    void tourne_image(int increment);
    void change_image(int i);
    /// l'image courante de la vidéo (lue si nécessaire) ; rien si elle est illisible
    std::optional<QImage> image_courante();

    // pointage
    bool pointage_ok() const;
    void clic_pointage(Vecteur p);
    bool peut_defaire() const;
    bool peut_refaire() const;
    void efface_point_precedent();
    void refait_point_suivant();
    /// refait la mesure d'une image (numéro à partir de 1), à la demande du tableau des mesures
    void refait_point_depuis_tableau(int index);
    /// renonce à refaire une mesure
    void annule_refait();
    /// Bouton « Démarrer » : passe à l'état D ou AB selon la case « suivi auto » ; après un arrêt du
    /// pointage, le reprend
    void debut_capture();
    /// Arrête le pointage : en mode Expert, un clic sur l'image ne pointe plus et les coordonnées
    /// s'affichent ; en mode Simplifié, passe à l'étape du tableau. Quitte le plein écran.
    void arrete_pointage();
    /// le pointage en plein écran est-il possible ? (pendant le pointage)
    bool plein_ecran_permis() const;
    bool plein_ecran() const;
    /// pointer en plein écran : l'image occupe tout l'écran, la loupe reste affichée
    void ouvre_plein_ecran();
    void quitte_plein_ecran();
    /// Raccourcis de la fenêtre repris par le plein écran : changer d'image, annuler et rétablir un
    /// point, agrandir l'interface
    QList<QAction *> raccourcis_plein_ecran() const;
    void redimensionne_data();
    /// Vérifie le nombre d'images par seconde saisi, et en déduit l'intervalle de temps entre deux
    /// images. Les mesures sont gardées : leurs dates sont recalculées.
    void verifie_ips();

    // échelle et origine
    void demande_echelle();
    void fin_echelle(Vecteur p1, Vecteur p2);
    void valide_longueur_etalon(double longueur);
    void annule_echelle();
    /// change l'unité de la longueur de l'étalon (retenue dans la configuration)
    void change_unite_etalon(Unite u);
    /// bouton « Placer l'origine » du mode Expert : le prochain clic sur l'image place l'origine (un
    /// second appui y renonce)
    void nouvelle_origine();
    /// texte de l'afficheur d'échelle, en pixels par mètre
    QString texte_echelle() const;
    /// Coordonnées d'un point dans le repère choisi : en pixels, puis en mètre si l'échelle est
    /// définie. Rien si le repère n'est pas encore choisi (mode Simplifié).
    struct Coordonnees {
        QString xpx, ypx;
        std::optional<std::pair<QString, QString>> metre;
    };
    std::optional<Coordonnees> coords(Vecteur p) const;
    /// le texte au-dessus de la loupe
    QString label_zoom() const;

    // suivi automatique
    void fin_selection_motif(Vecteur p1, Vecteur p2);
    void etape_detection();
    void stop_calculs();

    // fichiers
    void openfile(const QString &dossier);
    void openfile_dossier_prefere();
    void rouvre_ui();
    void enregistre_ui();
    void enregistre(const QString &chemin);
    void rouvre(const QString &fichier);
    bool traite_arg(const QString &fichier);
    void export_csv();
    void ecrit_csv(const QString &chemin);
    void copie_python();
    QString texte_python() const;
    /// vrai si les mesures actuelles viennent d'être copiées pour Python
    bool copie_a_jour() const;
    void tout_recommencer();
    /// le texte d'un fichier de projet avec les données courantes
    QString texte_projet() const;
    /// nom de base des fichiers proposés (celui de la vidéo)
    QString nom_de_base() const;
    std::optional<QString> nom_video() const;
    /// demande où enregistrer une image (trajectoire, graphique) et l'enregistre
    void enregistre_image(QWidget *zone, const QString &titre, const QString &filtre, const QString &extension);

    // ne rien perdre
    bool travail_en_securite() const;
    void note_travail_sauve();
    void confirme_si_mesures(const Apres &apres);
    void execute(const Apres &apres);
    /// Sauvegarde automatique du travail en cours, dans le dossier de configuration, dès qu'il a
    /// changé (au plus toutes les deux secondes) ; faux s'il n'y a rien à sauvegarder
    bool sauvegarde_de_secours();

    // aide, langue, thème, agrandissement
    void aide_page(const QString &page);
    QString page_d_aide() const;
    /// la notice, à la page de ce que l'on fait (F1)
    void aide();
    void change_langue(std::optional<Langue> l);
    /// le bouton du drapeau : passe à l'autre langue
    void langue_suivante();
    /// le bouton soleil ou lune : passe à l'autre thème, et le retient
    void theme_suivant();
    void propos();
    /// Affiche la fenêtre à sa taille normale, jamais agrandie d'office : la taille retenue, ou
    /// 1200 × 700, ramenée à l'écran disponible et centrée. Elle n'est agrandie que si l'utilisateur
    /// l'avait agrandie, ou si l'écran est plus petit que sa taille minimale.
    void montre();
    /// l'agrandissement de l'interface (1 : la police du système)
    double agrandissement() const { return m_agrandissement; }
    /// Change l'agrandissement de l'interface (Ctrl + / Ctrl − / Ctrl 0) et le retient ; il est
    /// appliqué dans la limite de la largeur de la fenêtre
    void change_agrandissement(double a);
    /// applique l'agrandissement demandé, borné par la largeur de la fenêtre
    void applique_agrandissement();
    /// le plus grand agrandissement permis par la largeur de la fenêtre
    double agrandissement_max() const;
    /// les vidéos d'exemple : menu du bouton de l'écran d'ouverture et du menu Fichier

    // modes Simplifié
    bool etape_faite(Etape e) const;
    bool etape_accessible(Etape e) const;
    void va_a_l_etape(Etape e);
    void valide_saisie_etalon();

    // dialogues
    void affiche_dialogues();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *objet, QEvent *event) override;

private:
    void construit_menus();
    /// à droite de la barre des menus : la notice, le thème, la langue
    void construit_coin_des_menus();
    /// Les boutons du coin, de la hauteur d'un titre de menu : la barre des menus garde sa hauteur
    /// standard. Icônes de 20 points × agrandissement, réduites s'il le faut pour y tenir.
    void dimensionne_coin_des_menus();
    void construit_barre_etat();
    void construit_raccourcis();
    void maj_menus();
    void maj_barre_etat();
    void style_barre_etat();
    /// Hauteur de la barre d'état : une ligne si le message tient dans sa largeur, plusieurs sinon
    /// (l'estimation de QLabel coupe le texte trop tôt)
    void ajuste_barre_etat();
    /// l'origine vient d'être placée ou déplacée sur l'image
    void origine_placee();
    /// passe en mode simplifié en ne gardant que le premier objet
    void mode_simplifie_un_objet();
    void maj_qt_traductions();
    void applique_preferences();
    void reinitialise_capture();
    void reinit_origine();
    void ecrit_dimensions_conf();
    void objet_suivant();
    void termine_pointage();
    void prepare_futur_clic();
    void affiche_point_attendu();
    QString empreinte_mesures() const;
    prefs::Config config_projet() const;
    void sauve_prefs();
    bool version_conf_ok(const prefs::Config &c);
    QString dossier_prefere() const;
    void echap();
    void touche_image(int pas);
    void ferme_dialogue(bool ok);

    // les pages
    QStackedWidget *m_pages = nullptr;
    PageSansVideo *m_sans_video = nullptr;
    PageGuide *m_guide = nullptr;
    QTabWidget *m_expert = nullptr;
    OngletPointage *m_onglet_pointage = nullptr;
    OngletTrajectoire *m_onglet_trajectoire = nullptr;
    OngletCoord *m_onglet_coord = nullptr;
    OngletGraph *m_onglet_graph = nullptr;
    /// le pointage en plein écran, une fenêtre à part (créée à la première demande)
    PleinEcran *m_plein_ecran = nullptr;
    /// les raccourcis de la fenêtre que le plein écran reprend
    QList<QAction *> m_raccourcis_image;
    /// les boutons de la notice, du thème et de la langue, à droite de la barre des menus
    std::array<QToolButton *, 2> m_boutons_coin{};
    QLabel *m_statut_icone = nullptr, *m_statut = nullptr, *m_info_video = nullptr, *m_info_image = nullptr,
           *m_info_mode = nullptr;
    EcranDemarrage *m_demarrage = nullptr;
    /// taille de la fenêtre hors agrandissement, retenue à la fermeture
    QSize m_taille_normale;
    /// Agrandissement de l'interface : celui qui est appliqué, et celui qui est demandé (retenu dans la
    /// configuration). Une fenêtre trop étroite borne le premier ; il revient au second quand elle
    /// s'élargit (au démarrage, la fenêtre n'a encore que sa largeur minimale).
    double m_agrandissement = 1.0;
    double m_agrandissement_voulu = 1.0;
    /// le thème affiché est sombre (pour ne refaire les couleurs qu'à un vrai changement)
    std::optional<bool> m_sombre;
    QWidget *m_depot = nullptr;
    QPointer<QDialog> m_boite;
    bool m_rafraichissement = false;

    QTimer m_detection;
    QTimer m_secours;
    QTranslator m_qt_traductions;
    bool m_qt_traductions_installees = false;

    // menus et actions
    QMenu *m_menu_fichier = nullptr, *m_menu_edition = nullptr, *m_menu_mode = nullptr,
          *m_menu_aide = nullptr;
    QAction *m_ouvrir = nullptr, *m_ouvrir_projet = nullptr, *m_recuperer = nullptr, *m_enregistrer = nullptr,
            *m_copier = nullptr, *m_csv = nullptr, *m_quitter = nullptr;
    QAction *m_defaire = nullptr, *m_refaire = nullptr, *m_recommencer = nullptr;
    QAction *m_mode_simplifie = nullptr, *m_mode_expert = nullptr;
    QAction *m_notice = nullptr, *m_a_propos = nullptr, *m_a_propos_qt = nullptr;
    QAction *m_theme = nullptr, *m_langue = nullptr;

    friend class PageGuide;
    friend class OngletCoord;
    friend class TestsInterface;
};
