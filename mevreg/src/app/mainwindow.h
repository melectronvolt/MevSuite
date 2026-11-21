// Fenêtre principale : menus, barre d'outils, fenêtres ancrables (grandeurs, tableurs, graphes,
// scripts…), page d'accueil, barre d'état, fichiers, annuler/rétablir, exports, résultats des
// scripts Python.

#pragma once

#include "app/icons.h"
#include "app/store.h"
#include "core/csv.h"
#include "plot/export.h"

#include <QDockWidget>
#include <QFont>
#include <QLockFile>
#include <memory>
#include <QMainWindow>
#include <QPointer>
#include <QSize>

#include <map>
#include <optional>

namespace report {
class PageWriter;
}

namespace docking {
struct Target;
}

class GraphPanel;
class Guide;
class ModelingPanel;
class StatsPanel;
class QLabel;
class QPushButton;
class QTabBar;
class QToolBar;
class QToolButton;

/// Taille de la fenêtre au premier lancement, et taille minimale (toute la barre d'outils, trois
/// colonnes de fenêtres, la page d'accueil sans chevauchement).
inline constexpr QSize DEFAULT_WINDOW(1200, 700);
inline constexpr QSize MIN_WINDOW(1000, 600);

/// Identifiant d'une fenêtre ancrable : « quantities », « table:3 », « graph:5 », « drawing:Fente »…
struct TabKey {
    enum Kind { Quantities, Calculations, Table, Graph, Script, Drawing, Modeling, Statistics } kind = Quantities;
    ViewId id = 0;
    QString name;

    const char *icone() const;
    QString texte() const;
    static std::optional<TabKey> analyse(const QString &s);
    bool operator==(const TabKey &) const = default;
};

class DockTitle;

/// Fenêtre ancrable qui se détruit quand on la ferme. Glissée par sa barre de titre, elle ne se
/// place pas où Qt le décide : la fenêtre principale affiche des repères de dépôt. Détachée, c'est
/// une fenêtre à part, avec le cadre du système : elle se déplace, s'agrandit et se ferme comme
/// les autres (Qt dessine lui-même le cadre d'une fenêtre ancrable flottante et la déplace d'une
/// façon que Wayland refuse : elle restait figée au milieu de l'écran). Les fenêtres de réglages et
/// de résultats (Grandeurs, Calculs, Modélisation, Statistiques, Notice) ont le fond un peu plus
/// sombre des panneaux latéraux de la suite ; les tableurs, graphes et scripts gardent celui de la
/// fenêtre.
class Dock : public QDockWidget {
    Q_OBJECT

public:
    Dock(const TabKey &key, QWidget *content, QWidget *parent);
    TabKey key;

    bool detache() const { return m_detached; }
    /// Fenêtre à part (vrai) ou fenêtre ancrable de la fenêtre principale (faux) ; la fenêtre
    /// principale la retire de sa disposition avant, et l'y replace après.
    void definit_detache(bool on);
    DockTitle *barre_titre() const { return m_title; }

Q_SIGNALS:
    /// Fermeture demandée (bouton Fermer, cadre de la fenêtre détachée) : la fenêtre principale
    /// demande confirmation s'il le faut, puis ferme la fenêtre.
    void fermeture_demandee(Dock *self);
    /// La barre de titre est glissée (position du pointeur à l'écran).
    void glisser_demande(Dock *self, QPoint globalPos);
    /// Bouton Détacher (vrai) ou Rattacher (faux).
    void detachement_demande(Dock *self, bool detach);

protected:
    bool event(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /// Fond des panneaux, recalculé quand le thème change.
    void couleur_panneau_modele();

    DockTitle *m_title = nullptr;
    std::optional<QPoint> m_press;
    bool m_panel = false;
    bool m_detached = false;
};

/// Barre de titre d'une fenêtre ancrable : titre en demi-gras, boutons Détacher et Fermer de la
/// hauteur d'une ligne de texte (ceux de Qt étaient minuscules). Fenêtre détachée : le titre et le
/// bouton Fermer sont dans le cadre du système, il reste le bouton Rattacher. Les clics hors des
/// boutons vont à la fenêtre (glissement vers les repères).
class DockTitle : public QWidget {
public:
    explicit DockTitle(Dock *dock);
    void definit_detache(bool on);
    QToolButton *bouton_detacher() const { return m_detach; }
    QToolButton *bouton_fermer() const { return m_close; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /// Tailles des boutons et des icônes à la police en cours (agrandissement compris).
    void ajuste();

    Dock *m_dock;
    QToolButton *m_detach;
    QToolButton *m_close;
    bool m_detached = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Store &store, QWidget *parent = nullptr);
    ~MainWindow() override;

    void choisit_mode(bool simple);
    Store &depot() { return m_store; }

    // --- fenêtres ---
    void ouvre_onglet(const TabKey &key);
    void ferme_onglet(const TabKey &key);
    void cache_onglet(const TabKey &key);
    bool est_ouvert(const TabKey &key) const;
    QList<TabKey> onglets_ouverts() const;
    void nouveau_tableur(std::vector<QId> columns);
    void nouveau_graphe(std::optional<std::pair<QId, QId>> curve = std::nullopt);
    void nouveau_script(const QString &code = QString());
    /// Fenêtre Modélisation sur un graphe ; `fit` : modèle à afficher, -1 pour un nouveau modèle.
    void ouvre_modelisation(ViewId graph, int plot, std::optional<int> fit = std::nullopt);
    void ouvre_statistiques(QId q);
    /// Fenêtre graphique ouverte (pour connaître la plage sélectionnée, le graphe actif…).
    GraphPanel *panneau_graphe(ViewId id) const;
    void disposition_par_defaut();
    /// Glissement d'une fenêtre ancrée : repères de dépôt sur les fenêtres et les bords.
    void commence_glisser_fenetre(Dock *dock, QPoint globalPos);
    /// Pose la fenêtre sur la cible d'un repère ; faux si c'est impossible.
    bool deplace_fenetre(Dock *dock, const docking::Target &target);
    /// Élargit une fenêtre ancrée jusqu'à `width` si possible, en prenant la place aux autres
    /// colonnes (jusqu'à leur largeur minimale) ; une fenêtre détachée s'agrandit elle-même.
    void elargit_fenetre(QDockWidget *dock, int width);
    /// Fenêtre à part, ou de nouveau dans la fenêtre principale (à la place de son genre).
    /// `geometry` : place à l'écran (sinon près de sa place dans la fenêtre principale).
    void detache_fenetre(Dock *dock, std::optional<QRect> geometry = std::nullopt);
    void rattache_fenetre(Dock *dock);
    /// Fermeture demandée par l'utilisateur : la fenêtre d'un tableur, d'un graphe, d'un script
    /// ou d'un dessin est cachée (retrouvée dans le menu Fenêtres) ou fermée (supprimée du
    /// document), au choix ; « Toujours cacher » écarte la question.
    void demande_fermeture(Dock *dock);
    /// Ferme une fenêtre pour de bon : son tableur, son graphe, son script ou son dessin est
    /// supprimé (Ctrl+Z le rétablit, sauf un dessin, refait par son script).
    void supprime_fenetre(const TabKey &key);

    // --- fichiers ---
    /// Transmet le texte visible dans les éditeurs au document avant de le lire ou de le remplacer.
    void transmet_saisie_scripts();
    /// Ces deux opérations réalisent le remplacement après le choix de l'utilisateur.
    void nouveau_fichier();
    bool ouvre_chemin(const QString &path);
    /// Enregistre ; faux si l'utilisateur a annulé ou en cas d'erreur.
    bool enregistre(bool saveAs, const QString &to = QString());
    void importe_csv(std::optional<ViewId> table);
    /// Export CSV avec ses réglages (séparateurs, en-têtes, codage).
    void exporte_csv(const std::vector<QId> &ids);
    /// Nom du fichier CSV à écrire (boîte du système), vide si l'élève renonce.
    QString demande_chemin_csv();
    bool ecrit_csv(const QString &path, const std::vector<QId> &ids, const csv::ExportOptions &opts, csv::Encoding encoding = csv::Encoding::Utf8Bom);
    void propose_recuperation(bool demarrage);
    QString chemin_actuel() const { return m_path; }

    // --- exports ---
    void exporte_graphe(ViewId id, exporter::Format fmt);
    void copie_image_graphe(ViewId id);
    void imprime_graphe(ViewId id);
    void tableur_pdf(ViewId id, bool print);
    void script_pdf(ViewId id, bool print);
    void rapport_pdf(bool print, std::optional<ViewId> table = {}, std::optional<ViewId> graphe = {});
    void exporte_dessin(const QString &name, exporter::Format fmt);
    void copie_dessin(const QString &name);
    void exporte_histogramme(exporter::Format fmt, const StatsPanel &panneau);
    /// Taille du graphe à l'écran, reprise à l'export.
    QSizeF taille_export_graphe(ViewId id) const;

    QMenu *createPopupMenu() override;

    /// Message de la barre d'état, avec le pictogramme de sa nature.
    void affiche_statut(const QString &text, icons::Status kind = icons::Status::Info);
    /// Ouvre la notice d'utilisation (F1) dans le navigateur, ou une autre page de la notice
    /// (`help::PYTHON` : la référence Python).
    void ouvre_notice(const QString &page = QString(), const QString &section = QString());

    // --- agrandissement de l'interface (Ctrl + / Ctrl − / Ctrl 0) ---
    /// Agrandissement en cours (1 = 100 %).
    double agrandissement() const { return m_uiZoom; }
    /// Agrandissement le plus grand que permet la largeur de la fenêtre, par pas de 10 %.
    double agrandissement_max() const;
    /// Demande un agrandissement (retenu dans les préférences) ; celui qui s'applique est borné
    /// par la largeur de la fenêtre.
    void change_agrandissement(double zoom);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void childEvent(QChildEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void construit_menus();
    void construit_barre_outils();
    /// Thème et langue, à droite de la barre de menus (la notice est dans le menu Aide, F1).
    void construit_coin_des_menus();
    /// Boutons du coin de la hauteur des titres des menus, icônes à l'agrandissement en cours.
    void dimensionne_coin_des_menus();
    /// Raccourcis de l'agrandissement, et celui des préférences.
    void construit_zoom();
    /// Applique l'agrandissement `zoom` (déjà borné) : police, mesures, fenêtres, barres.
    void applique_agrandissement(double zoom);
    void construit_barre_etat();
    QWidget *construit_page_accueil();
    /// Les deux boutons de la page d'accueil de même taille, plus hauts que la normale.
    void dimensionne_boutons_accueil();
    /// Bouton du thème : soleil ou lune, bulle, selon la palette en cours.
    void maj_bouton_theme();
    /// Barre d'état : une ligne, plus seulement si le message ne tient pas.
    void ajuste_barre_etat();
    /// Couleurs de la barre d'état (thème clair ou sombre).
    void style_barre_etat();
    void ouvre_dialogue();
    void rafraichit();
    void maj_titre();
    void maj_statut();
    void ajoute_recent(const QString &path);
    /// Transmet la dernière saisie au document et demande de la sauvegarder si nécessaire.
    bool valide_saisies();
    bool confirme_remplacement();
    void demande_ouverture(const QString &chemin);
    /// Remplace la séance. Le contenu du fichier permet de reconnaître un
    /// document reçu avant d'activer le suivi de ses scripts liés.
    void definit_espace_travail(Seance ws, const QJsonValue &layout, const std::optional<QByteArray> &file = std::nullopt);
    /// Demande s'il faut lancer les scripts d'un document reçu (contenu `file`).
    void demande_scripts(const QByteArray &file, quint64 document);
    QJsonValue disposition_json() const;
    void restaure_disposition(const QJsonValue &layout);
    QWidget *cree_contenu(const TabKey &key);
    void place(Dock *dock);
    /// Fenêtres ancrées (pas les fenêtres flottantes).
    std::vector<QDockWidget *> fenetres_ancrees() const;
    /// Range les fenêtres ancrées en colonnes : Grandeurs, tableurs et scripts, graphes, modélisation.
    void redispose();
    /// Fenêtre ancrée sous le pointeur (barre de titre ou onglet).
    Dock *fenetre_en(QPoint globalPos) const;
    void menu_fenetre(QMenu *menu, Dock *dock);
    void maj_titres_fenetres();
    /// Nouvelle fenêtre ancrable, reliée à la fenêtre principale (pas encore placée).
    Dock *cree_fenetre(const TabKey &key);
    /// Actions des menus qui ont un raccourci clavier (données aussi aux fenêtres détachées).
    QList<QAction *> raccourcis_fenetres() const;
    void retire_fenetre(Dock *dock);
    void ferme_toutes_fenetres();
    void maj_indication_vide();
    void apres_script_applique(ViewId id, const std::vector<ViewId> &openGraphs, const std::vector<std::pair<ViewId, QString>> &savefig,
                         const QStringList &newDrawings, const QJsonArray &actions);
    /// Ordres du module Python `mevreg` pour la fenêtre principale : fenêtres, fichiers, messages.
    void lance_actions_script(const QJsonArray &actions);
    /// Fenêtre d'un nom donné (« Grandeurs », un tableur, un graphe, un script, un dessin).
    std::optional<TabKey> fenetre_nommee(const QString &name) const;
    /// Chemin relatif rapporté au dossier du document ; extension ajoutée s'il n'y en a pas.
    QString resout_chemin(const QString &path, const QString &suffix) const;
    void enregistre_figure(ViewId graph, const QString &path);
    QString dialogue_enregistrement(const QString &defaultName, exporter::Format fmt);
    bool imprime_pages(const report::PageWriter &pages, const QString &title);
    void lance_tous_scripts();

    Store &m_store;
    /// Numéro du document affiché : une question sur les scripts d'un document déjà remplacé ne vaut plus.
    quint64 m_document = 0;
    std::map<QString, QPointer<Dock>> m_docks;
    QString m_path;
    QByteArray m_empreinteFichier;
    QString m_secours;
    std::unique_ptr<QLockFile> m_verrouSecours;
    QTimer m_delaiSecours;
    void ecrit_secours();
    void oublie_secours();
    /// Page d'accueil, affichée tant qu'aucune fenêtre n'est ouverte.
    QPointer<StatsPanel> m_statsGuide;
    bool m_modeSimple = false;
    Guide *m_guide = nullptr;
    QJsonValue m_dispositionExpert;
    QAction *m_modeSimpleAction = nullptr, *m_modeExpertAction = nullptr;
    QWidget *m_emptyHint = nullptr;
    QPushButton *m_welcomeTable = nullptr;
    QPushButton *m_welcomeOpen = nullptr;
    QPointer<QWidget> m_dropOverlay;
    /// Onglet pressé d'une pile de fenêtres : tiré hors de sa barre, il déplace sa fenêtre.
    struct TabPress {
        QPointer<QTabBar> bar;
        QPointer<Dock> dock;
    };
    std::optional<TabPress> m_tabPress;
    QToolBar *m_toolbar = nullptr;
    QMenu *m_menuFile = nullptr;
    QWidget *m_menuCorner = nullptr;
    std::vector<QToolButton *> m_cornerButtons;
    QAction *m_actTheme = nullptr;
    QAction *m_actLanguage = nullptr;
    QLabel *m_statusIcon = nullptr;
    QLabel *m_statusLabel = nullptr;
    /// Document · caractéristiques, à droite.
    QLabel *m_statusFile = nullptr;
    /// Formules en erreur, à droite (cachée s'il n'y en a pas).
    QLabel *m_statusErrors = nullptr;
    std::pair<QString, icons::Status> m_status;
    /// Taille de la fenêtre non agrandie, et la précédente (l'agrandissement est signalé après le
    /// redimensionnement).
    QSize m_windowSize, m_previousWindowSize;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_windowsMenu = nullptr;
    QList<QAction *> m_menusExperts;
    QMenu *m_pythonMenu = nullptr;
    QAction *m_undo = nullptr;
    QAction *m_redo = nullptr;
    /// Agrandissement appliqué, et police du système (à 100 %).
    double m_uiZoom = 1.0;
    QFont m_baseFont;
};
