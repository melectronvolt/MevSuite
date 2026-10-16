// État partagé par toutes les fenêtres : espace de travail, préférences, annuler/rétablir,
// scripts Python en cours, dessins. Toute modification passe par `edit`, qui prévient les
// fenêtres et enregistre une étape d'annulation une fois l'interaction terminée.

#pragma once

#include "app/icons.h"
#include "core/drawing.h"
#include "core/workspace.h"
#include "python/engine.h"

#include <QJsonArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <map>
#include <optional>
#include <tuple>
#include <vector>

/// Agrandissement de l'interface : de 50 à 300 %.
inline constexpr double UI_ZOOM_MIN = 0.5;
inline constexpr double UI_ZOOM_MAX = 3.0;

/// Préférences de l'utilisateur, conservées d'une session à l'autre (`mevreg.ini`).
/// Fichier des réglages : mevreg.ini dans le dossier commun aux logiciels de la suite (MeVSuite).
QString settingsFile();

struct Prefs {
    /// Thème clair ou sombre (`Unknown` : celui du bureau, tant que le bouton n'a pas servi).
    Qt::ColorScheme theme = Qt::ColorScheme::Unknown;
    /// Virgule décimale : celle de la langue de l'interface (français : virgule ; anglais : point),
    /// pas un réglage. MeVReg n'existe pour l'instant qu'en français.
    bool decimalComma = true;
    int sigDigits = 6;
    /// Graphes sur fond sombre quand l'interface est sombre (les exports restent clairs).
    bool darkPlots = true;
    QStringList recent;
    /// Taille de la fenêtre non agrandie.
    std::optional<QSize> windowSize;
    /// Fenêtre agrandie à l'écran (elle s'ouvre à sa taille normale tant que l'élève ne l'a pas
    /// agrandie lui-même).
    bool maximized = false;
    /// Agrandissement de l'interface demandé (Ctrl + / Ctrl − / Ctrl 0), de 0,5 à 3 ; celui qui
    /// s'applique est borné par la largeur de la fenêtre.
    double uiZoom = 1.0;
    /// Confirmation avant de fermer la fenêtre d'un tableur, d'un graphe, d'un script ou d'un
    /// dessin (ils restent dans le document) ; « Ne plus demander » l'écarte.
    bool confirmClose = true;

    NumberStyle style() const { return NumberStyle{decimalComma}; }
    static Prefs load();
    void save() const;
};

/// État d'exécution d'un script.
struct ScriptRun {
    /// Numéro de l'exécution en cours, 0 si aucune.
    std::uint64_t running = 0;
    /// Empreinte des données lues lors de la dernière exécution (scripts liés).
    std::optional<QByteArray> inputHash;
    QString stdoutText;
    std::optional<python::ScriptError> error;
    QStringList messages;
    double durationMs = 0;
    int runs = 0;
};

class Store : public QObject {
    Q_OBJECT

public:
    explicit Store(QObject *parent = nullptr);

    Workspace ws;
    Prefs prefs;
    std::map<ViewId, ScriptRun> runs;
    /// Fenêtres de dessin produites par les scripts (non enregistrées : recalculées).
    std::map<QString, drawing::Drawing> drawings;
    /// Abscisse choisie pour la prochaine courbe d'un graphe encore vide.
    std::map<ViewId, QId> pendingAbscissa;
    /// Dossier du document enregistré (chemins relatifs des scripts), vide sinon.
    QString documentFolder;
    /// Graphe actif de chaque fenêtre graphique (grille de plusieurs graphes).
    std::map<ViewId, int> selectedPlot;
    /// Dernière plage d'abscisses tracée à la souris : graphe de la fenêtre, bornes.
    std::map<ViewId, std::tuple<int, double, double>> selectedRange;

    NumberStyle style() const { return prefs.style(); }

    /// Modifie l'espace de travail ; les fenêtres sont prévenues et une étape d'annulation est
    /// enregistrée quand l'utilisateur a fini (bouton de la souris relâché).
    template <typename F>
    void edit(F &&f)
    {
        f(ws);
        changed();
    }
    /// Signale une modification faite directement sur `ws`.
    void changed();
    /// Prévient les fenêtres d'un changement d'affichage (préférences, thème).
    void prefsChanged();
    /// Message de la barre d'état, de la nature donnée (information, réussite, avertissement, erreur).
    void message(const QString &text, icons::Status kind = icons::Status::Success);

    bool canUndo() const;
    bool canRedo() const { return !m_redo.empty(); }
    void undo();
    void redo();
    /// Nouveau document : historique vidé, document propre.
    void reset(Workspace ws);
    bool dirty() const { return m_dirty; }
    void setDirty(bool d);
    /// Enregistre tout de suite l'étape d'annulation en attente.
    void commitNow();

    // --- Python ---
    /// Code d'un script modifié dans son éditeur : pas d'étape d'annulation (l'éditeur a la sienne).
    void setScriptCode(ViewId id, const QString &code);
    void runScript(ViewId id);
    void stopScript(ViewId id);
    bool isRunning(ViewId id) const;
    /// Relance les scripts liés dont les données ont changé.
    void runLinkedScripts();

Q_SIGNALS:
    /// L'espace de travail a changé (données, fenêtres, réglages des graphes).
    void workspaceChanged();
    /// Préférences ou thème changés : tout se redessine.
    void displayChanged();
    void messageRequested(const QString &text, icons::Status kind);
    void dirtyChanged(bool dirty);
    /// Un script a démarré ou s'est terminé.
    void scriptStateChanged(ViewId id);
    /// Résultat d'un script, déjà intégré au document : graphes à ouvrir, fichiers à écrire.
    void scriptApplied(ViewId id, const std::vector<ViewId> &openGraphs, const std::vector<std::pair<ViewId, QString>> &savefig,
                       const QStringList &newDrawings, const QJsonArray &actions);

private:
    void scheduleCommit();
    void commit();
    void restore(Workspace ws);
    void onScriptFinished(std::uint64_t job, const python::ScriptOutput &out);

    std::vector<Workspace> m_undo, m_redo;
    Workspace m_snapshot;
    quint64 m_revision = 0, m_snapshotRevision = 0;
    bool m_dirty = false;
    QTimer m_commitTimer, m_linkedTimer;
    std::map<std::uint64_t, ViewId> m_jobs;
};

/// Données transmises au script : grandeurs (sauf celles qu'il produit lui-même) et paramètres.
python::ScriptInput scriptInput(const Workspace &ws, ViewId script);
/// Empreinte du code et des données qu'il lit (pour relancer un script lié).
QByteArray inputHash(const Workspace &ws, ViewId script);
