// État partagé par toutes les fenêtres : espace de travail, préférences, annuler/rétablir,
// scripts Python en cours, dessins. Une modification via `edite` prévient les
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
#include <set>
#include <tuple>
#include <vector>

/// Agrandissement de l'interface : de 50 à 300 %.
inline constexpr double UI_ZOOM_MIN = 0.5;
inline constexpr double UI_ZOOM_MAX = 3.0;

/// Préférences de l'utilisateur, conservées d'une session à l'autre (`mevreg.ini`).
/// Fichier des réglages : mevreg.ini dans le dossier commun aux logiciels de la suite (MeVSuite).
QString fichier_reglages();

struct Prefs {
    bool modeSimple = true;
    /// Thème clair ou sombre (`Unknown` : celui du bureau, tant que le bouton n'a pas servi).
    Qt::ColorScheme theme = Qt::ColorScheme::Unknown;
    /// Virgule décimale : celle de la langue de l'interface (français : virgule ; anglais : point),
    /// pas un réglage. MeVReg n'existe pour l'instant qu'en français.
    bool decimalComma = true;
    int sigDigits = 6;
    static constexpr int texteMin = 75, texteMax = 300;
    static constexpr int ligneMin = 18, ligneMax = 120;
    int texteTableur = 120;  // en pourcentage de la police de l’interface
    int hauteurLigne = 28;   // en pixels à un zoom de 100 %
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
    /// Empreintes des documents enregistrés ici ou déjà acceptés. Leurs scripts
    /// liés peuvent reprendre le suivi quand une entrée change.
    QStringList trustedDocuments;

    NumberStyle style() const { return NumberStyle{decimalComma}; }
    /// Le document de contenu `bytes` a été enregistré ici, ou ses scripts acceptés.
    void fait_confiance(const QByteArray &bytes);
    bool est_de_confiance(const QByteArray &bytes) const;
    static Prefs charge();
    void enregistre() const;
};

/// État d'exécution d'un script.
struct ScriptRun {
    /// Numéro de l'exécution en cours, 0 si aucune.
    std::uint64_t running = 0;
    /// Entrées à l'ouverture du document ou lors du dernier calcul du script lié.
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

    Seance ws;
    Prefs prefs;
    std::map<ViewId, ScriptRun> runs;
    /// Fenêtres de dessin produites par les scripts (non enregistrées : recalculées).
    std::map<QString, drawing::Drawing> drawings;
    /// Abscisse choisie pour la prochaine courbe d'un graphe encore vide.
    std::map<ViewId, QId> pendingAbscissa;
    /// Dossier du document enregistré (chemins relatifs des scripts), vide sinon.
    QString documentFolder;
    /// Scripts qui ne se lancent pas d'eux-mêmes (script lié, données changées) : ceux d'un document
    /// reçu dont l'utilisateur n'a pas accepté les scripts, tant qu'il ne les lance pas lui-même.
    std::set<ViewId> blockedScripts;
    /// Graphe actif de chaque fenêtre graphique (grille de plusieurs graphes).
    std::map<ViewId, int> selectedPlot;
    /// Dernière plage d'abscisses tracée à la souris : graphe de la fenêtre, bornes.
    std::map<ViewId, std::tuple<int, double, double>> selectedRange;

    NumberStyle style() const { return prefs.style(); }

    /// Modifie l'espace de travail ; les fenêtres sont prévenues et une étape d'annulation est
    /// enregistrée quand l'utilisateur a fini (bouton de la souris relâché).
    template <typename F>
    void edite(F &&f)
    {
        f(ws);
        modifie();
    }
    /// Signale une modification faite directement sur `ws`.
    void modifie();
    /// Prévient les fenêtres d'un changement d'affichage (préférences, thème).
    void preferences_modifiees();
    /// Message de la barre d'état, de la nature donnée (information, réussite, avertissement, erreur).
    void message(const QString &text, icons::Status kind = icons::Status::Success);

    bool peut_annuler() const;
    bool peut_retablir() const { return !m_redo.empty(); }
    void defait();
    void retablit();
    /// Nouveau document : historique vidé, document propre.
    void reinitialise(Seance ws);
    bool est_modifie() const { return m_dirty; }
    void marque_modifie(bool d);
    /// Enregistre tout de suite l'étape d'annulation en attente.
    void valide_maintenant();

    // --- Python ---
    /// Code d'un script modifié dans son éditeur : pas d'étape d'annulation (l'éditeur a la sienne).
    void definit_code_script(ViewId id, const QString &code);
    void lance_script(ViewId id);
    void arrete_script(ViewId id);
    bool est_en_cours(ViewId id) const;
    /// Relance les scripts liés dont les données ont changé.
    void lance_scripts_lies();

Q_SIGNALS:
    /// L'espace de travail a changé (données, fenêtres, réglages des graphes).
    void espace_travail_change();
    /// Préférences ou thème changés : tout se redessine.
    void affichage_change();
    void message_demande(const QString &text, icons::Status kind);
    void modification_changee(bool dirty);
    void code_script_change();
    /// Un script a démarré ou s'est terminé.
    void etat_script_change(ViewId id);
    /// Résultat d'un script, déjà intégré au document : graphes à ouvrir, fichiers à écrire.
    void script_applique(ViewId id, const std::vector<ViewId> &openGraphs, const std::vector<std::pair<ViewId, QString>> &savefig,
                       const QStringList &newDrawings, const QJsonArray &actions);

private:
    void prevoit_validation();
    void valide();
    void restaure(Seance ws);
    void apres_script_termine(std::uint64_t job, const python::ScriptOutput &out);

    struct Etape {
        Etape(Seance etat, const Seance &voisin);
        Seance recompose(const Seance &voisin) &&;

        Seance document;
        // Grandes séries de même taille : seules les cases qui diffèrent de l'étape voisine.
        std::map<QId, std::vector<std::pair<size_t, double>>> cellules;
    };
    std::vector<Etape> m_undo, m_redo;
    Seance m_snapshot;
    quint64 m_revision = 0, m_snapshotRevision = 0;
    bool m_dirty = false;
    QTimer m_commitTimer, m_linkedTimer;
    std::map<std::uint64_t, ViewId> m_jobs;
};

/// Données transmises au script : grandeurs (sauf celles qu'il produit lui-même) et paramètres.
python::ScriptInput entree_script(const Seance &ws, ViewId script);
/// Empreinte du code et des données qu'il lit (pour relancer un script lié).
QByteArray empreinte_entrees(const Seance &ws, ViewId script);
