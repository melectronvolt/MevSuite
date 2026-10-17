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

    QString text() const;
    static std::optional<TabKey> parse(const QString &s);
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

    bool detached() const { return m_detached; }
    /// Fenêtre à part (vrai) ou fenêtre ancrable de la fenêtre principale (faux) ; la fenêtre
    /// principale la retire de sa disposition avant, et l'y replace après.
    void setDetached(bool on);
    DockTitle *titleBar() const { return m_title; }

Q_SIGNALS:
    /// Fermeture demandée (bouton Fermer, cadre de la fenêtre détachée) : la fenêtre principale
    /// demande confirmation s'il le faut, puis ferme la fenêtre.
    void closeRequested(Dock *self);
    /// La barre de titre est glissée (position du pointeur à l'écran).
    void dragRequested(Dock *self, QPoint globalPos);
    /// Bouton Détacher (vrai) ou Rattacher (faux).
    void detachRequested(Dock *self, bool detach);

protected:
    bool event(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /// Fond des panneaux, recalculé quand le thème change.
    void fitPanelColor();

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
    void setDetached(bool on);
    QToolButton *detachButton() const { return m_detach; }
    QToolButton *closeButton() const { return m_close; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /// Tailles des boutons et des icônes à la police en cours (agrandissement compris).
    void fit();

    Dock *m_dock;
    QToolButton *m_detach;
    QToolButton *m_close;
    bool m_detached = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Store &store, QWidget *parent = nullptr);

    Store &store() { return m_store; }

    // --- fenêtres ---
    void openTab(const TabKey &key);
    void closeTab(const TabKey &key);
    void hideTab(const TabKey &key);
    bool isOpen(const TabKey &key) const;
    QList<TabKey> openTabs() const;
    void newTable(std::vector<QId> columns);
    void newGraph(std::optional<std::pair<QId, QId>> curve = std::nullopt);
    /// Nouveau script : code de départ, ou celui donné (exemple du menu Python).
    void newScript(const QString &code = QString(), const QString &name = QString());
    /// Fenêtre Modélisation sur un graphe ; `fit` : modèle à afficher, -1 pour un nouveau modèle.
    void openModeling(ViewId graph, int plot, std::optional<int> fit = std::nullopt);
    void openStatistics(QId q);
    /// Fenêtre graphique ouverte (pour connaître la plage sélectionnée, le graphe actif…).
    GraphPanel *graphPanel(ViewId id) const;
    void defaultLayout();
    /// Glissement d'une fenêtre ancrée : repères de dépôt sur les fenêtres et les bords.
    void beginDockDrag(Dock *dock, QPoint globalPos);
    /// Pose la fenêtre sur la cible d'un repère ; faux si c'est impossible.
    bool moveDock(Dock *dock, const docking::Target &target);
    /// Élargit une fenêtre ancrée jusqu'à `width` si possible, en prenant la place aux autres
    /// colonnes (jusqu'à leur largeur minimale) ; une fenêtre détachée s'agrandit elle-même.
    void widenDock(QDockWidget *dock, int width);
    /// Fenêtre à part, ou de nouveau dans la fenêtre principale (à la place de son genre).
    /// `geometry` : place à l'écran (sinon près de sa place dans la fenêtre principale).
    void detachDock(Dock *dock, std::optional<QRect> geometry = std::nullopt);
    void attachDock(Dock *dock);
    /// Fermeture demandée par l'utilisateur : la fenêtre d'un tableur, d'un graphe, d'un script
    /// ou d'un dessin est cachée (retrouvée dans le menu Fenêtres) ou fermée (supprimée du
    /// document), au choix ; « Toujours cacher » écarte la question.
    void requestClose(Dock *dock);
    /// Ferme une fenêtre pour de bon : son tableur, son graphe, son script ou son dessin est
    /// supprimé (Ctrl+Z le rétablit, sauf un dessin, refait par son script).
    void deleteWindow(const TabKey &key);

    // --- fichiers ---
    void newFile();
    bool openPath(const QString &path);
    /// Enregistre ; faux si l'utilisateur a annulé ou en cas d'erreur.
    bool save(bool saveAs, const QString &to = QString());
    /// Espace de travail d'exemple (essais automatiques seulement : le logiciel n'en propose pas).
    void loadExample(int index);
    void importCsv(std::optional<ViewId> table);
    /// Export CSV avec ses réglages (séparateurs, en-têtes, codage).
    void exportCsv(const std::vector<QId> &ids);
    /// Nom du fichier CSV à écrire (boîte du système), vide si l'élève renonce.
    QString askCsvPath();
    bool writeCsv(const QString &path, const std::vector<QId> &ids, const csv::ExportOptions &opts, csv::Encoding encoding = csv::Encoding::Utf8Bom);
    QString currentPath() const { return m_path; }

    // --- exports ---
    void exportGraph(ViewId id, exporter::Format fmt);
    void copyGraphImage(ViewId id);
    void printGraph(ViewId id);
    void tablePdf(ViewId id, bool print);
    void scriptPdf(ViewId id, bool print);
    void reportPdf(bool print);
    void exportDrawing(const QString &name, exporter::Format fmt);
    void copyDrawing(const QString &name);
    void exportHistogram(exporter::Format fmt);
    /// Taille du graphe à l'écran, reprise à l'export.
    QSizeF graphExportSize(ViewId id) const;

    QMenu *createPopupMenu() override;

    /// Message de la barre d'état, avec le pictogramme de sa nature.
    void setStatus(const QString &text, icons::Status kind = icons::Status::Info);
    /// Ouvre la notice d'utilisation (F1) dans le navigateur, ou une autre page de la notice
    /// (`help::PYTHON` : la référence Python).
    void openHelp(const QString &page = QString());

    // --- agrandissement de l'interface (Ctrl + / Ctrl − / Ctrl 0) ---
    /// Agrandissement en cours (1 = 100 %).
    double uiZoom() const { return m_uiZoom; }
    /// Agrandissement le plus grand que permet la largeur de la fenêtre, par pas de 10 %.
    double maxUiZoom() const;
    /// Demande un agrandissement (retenu dans les préférences) ; celui qui s'applique est borné
    /// par la largeur de la fenêtre.
    void setUiZoom(double zoom);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void childEvent(QChildEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildMenus();
    void buildToolbar();
    /// Thème et langue, à droite de la barre de menus (la notice est dans le menu Aide, F1).
    void buildMenuCorner();
    /// Boutons du coin de la hauteur des titres des menus, icônes à l'agrandissement en cours.
    void sizeMenuCorner();
    /// Raccourcis de l'agrandissement, et celui des préférences.
    void buildZoom();
    /// Applique l'agrandissement `zoom` (déjà borné) : police, mesures, fenêtres, barres.
    void applyUiZoom(double zoom);
    void buildStatusBar();
    QWidget *buildWelcomePage();
    /// Les deux boutons de la page d'accueil de même taille, plus hauts que la normale.
    void sizeWelcomeButtons();
    /// Bouton du thème : soleil ou lune, bulle, selon la palette en cours.
    void updateThemeButton();
    /// Barre d'état : une ligne, plus seulement si le message ne tient pas.
    void fitStatusBar();
    /// Couleurs de la barre d'état (thème clair ou sombre).
    void styleStatusBar();
    void openDialog();
    void refresh();
    void updateTitle();
    void updateStatus();
    void addRecent(const QString &path);
    /// Exécute `then` après avoir proposé d'enregistrer les modifications.
    void guarded(const std::function<void()> &then);
    /// Nouveau document ; `file` : le contenu du fichier ouvert (ses scripts ne se lancent qu'après
    /// accord, sauf s'il a été enregistré sur ce poste).
    void setWorkspace(Workspace ws, const QJsonValue &layout, const std::optional<QByteArray> &file = std::nullopt);
    /// Demande s'il faut lancer les scripts d'un document reçu (contenu `file`).
    void askScripts(const QByteArray &file, quint64 document);
    QJsonValue layoutJson() const;
    void restoreLayout(const QJsonValue &layout);
    QWidget *createContent(const TabKey &key);
    void place(Dock *dock);
    /// Fenêtres ancrées (pas les fenêtres flottantes).
    std::vector<QDockWidget *> dockedDocks() const;
    /// Range les fenêtres ancrées en colonnes : Grandeurs, tableurs et scripts, graphes, modélisation.
    void relayout();
    /// Fenêtre ancrée sous le pointeur (barre de titre ou onglet).
    Dock *dockAt(QPoint globalPos) const;
    void dockMenu(QMenu *menu, Dock *dock);
    void updateDockTitles();
    /// Nouvelle fenêtre ancrable, reliée à la fenêtre principale (pas encore placée).
    Dock *createDock(const TabKey &key);
    /// Actions des menus qui ont un raccourci clavier (données aussi aux fenêtres détachées).
    QList<QAction *> windowShortcuts() const;
    void removeDock(Dock *dock);
    void closeAllDocks();
    void updateEmptyHint();
    void onScriptApplied(ViewId id, const std::vector<ViewId> &openGraphs, const std::vector<std::pair<ViewId, QString>> &savefig,
                         const QStringList &newDrawings, const QJsonArray &actions);
    /// Ordres du module Python `mevreg` pour la fenêtre principale : fenêtres, fichiers, messages.
    void runScriptActions(const QJsonArray &actions);
    /// Fenêtre d'un nom donné (« Grandeurs », un tableur, un graphe, un script, un dessin).
    std::optional<TabKey> windowNamed(const QString &name) const;
    /// Chemin relatif rapporté au dossier du document ; extension ajoutée s'il n'y en a pas.
    QString resolvePath(const QString &path, const QString &suffix) const;
    void savefig(ViewId graph, const QString &path);
    QString saveDialog(const QString &defaultName, exporter::Format fmt);
    bool printPages(const report::PageWriter &pages, const QString &title);
    void runAllScripts();
    void flushScripts();

    Store &m_store;
    /// Numéro du document affiché : une question sur les scripts d'un document déjà remplacé ne vaut plus.
    quint64 m_document = 0;
    std::map<QString, QPointer<Dock>> m_docks;
    QString m_path;
    bool m_allowClose = false;
    /// Page d'accueil, affichée tant qu'aucune fenêtre n'est ouverte.
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
    QMenu *m_pythonMenu = nullptr;
    QAction *m_undo = nullptr;
    QAction *m_redo = nullptr;
    /// Agrandissement appliqué, et police du système (à 100 %).
    double m_uiZoom = 1.0;
    QFont m_baseFont;
};
