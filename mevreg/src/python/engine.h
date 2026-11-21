// Python intégré : interpréteur CPython embarqué (sans site-packages), NumPy et
// tracés Matplotlib traduits en graphes du logiciel.
//
// Échange avec le logiciel : les grandeurs et paramètres sont copiés à l'entrée du script ;
// à la sortie, le logiciel récupère les grandeurs créées par `grandeur(...)`, les figures, le
// texte affiché et l'éventuelle erreur. Aucun aller-retour pendant l'exécution.

#pragma once

#include "python/identifiants.h"

#include "core/drawing.h"
#include "core/document.h"
#include "python/figure.h"

#include <QJsonArray>
#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace python {

/// Applique une commande de données au document local du script ou à celui du tableur.
QString modifie_donnees(Document &doc, const QJsonObject &ordre);

struct NamedValues {
    QString name;
    QString unit;
    std::vector<double> values;
};

struct NamedParam {
    QString name;
    QString unit;
    double value = 0;
};

/// Données copiées dans le script.
struct ScriptInput {
    std::vector<NamedValues> quantities;
    std::vector<NamedParam> parameters;
    QStringList previousOutputs;
    /// Dossier du document : les chemins relatifs des fichiers (CSV, images) y sont rapportés.
    QString folder;
    /// Formules et incertitudes des grandeurs présentes à l'entrée du script.
    QMap<QString, QString> formulas, uncertainties;
};

/// Figure envoyée par `plt.show()` ou `plt.savefig()`.
struct FigureMsg {
    figure::FigureDesc desc;
    /// Tableaux de données référencés par la description.
    std::vector<std::vector<double>> data;
    /// Chemin demandé par `savefig`.
    std::optional<QString> savefig;
};

struct DrawingMsg {
    drawing::Desc desc;
    std::vector<std::vector<double>> data;
};

struct ScriptError {
    /// Dernière ligne du message : « NameError: name 'x' is not defined ».
    QString message;
    /// Ligne du script en cause, si elle est connue.
    std::optional<int> line;
    /// Trace complète.
    QString traceback;
    bool interrupted = false;
};

struct ScriptOutput {
    QString stdoutText;
    std::optional<ScriptError> error;
    /// Grandeurs créées par `grandeur(nom, valeurs, unité)`, dans l'ordre (les dernières valeurs de
    /// chaque nom).
    std::vector<NamedValues> quantities;
    /// Valeurs de chacun de ces appels, dans l'ordre (ordres « grandeur », champ « indice ») : un nom
    /// repris après un renommage garde ses deux séries.
    std::vector<NamedValues> given;
    std::vector<FigureMsg> figures;
    /// Fenêtres de dessin (module `fenetre`).
    std::vector<DrawingMsg> drawings;
    /// Ordres du module `mevreg` : {"ordre": …, arguments}, appliqués après le script.
    QJsonArray commands;
    double durationMs = 0;
};

/// Exécution des scripts dans un fil dédié : l'interface reste réactive et un script trop long
/// peut être interrompu. L'interpréteur est unique dans le programme.
class Engine : public QObject {
    Q_OBJECT

public:
    static Engine &instance();
    ~Engine() override;

    /// Lance un script ; renvoie son numéro. Le résultat arrive par le signal `finished`.
    std::uint64_t lance(const QString &code, const ScriptInput &input);
    /// Exécute un script et attend son résultat (tests, outils).
    ScriptOutput lance_synchrone(const QString &code, const ScriptInput &input);
    /// Script en cours d'exécution (0 : aucun).
    std::uint64_t en_cours() const { return m_current.load(); }
    /// Interrompt le script `id` s'il tourne encore (KeyboardInterrupt).
    void interrompt(std::uint64_t id);
    /// Demande l'arrêt et attend la finalisation sur le fil Python. Faux si un appel
    /// natif ou un fil lancé par un script bloque encore la sortie du programme.
    bool arrete_avant_sortie();

Q_SIGNALS:
    void fini(std::uint64_t id, const python::ScriptOutput &out);

private:
    Engine();

    struct Job {
        std::uint64_t id;
        QString code;
        ScriptInput input;
        std::function<void(std::uint64_t, ScriptOutput)> done;
    };

    void boucle();
    void demande_arret();
    std::uint64_t met_en_file(const QString &code, const ScriptInput &input, std::function<void(std::uint64_t, ScriptOutput)> done);

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    bool m_stop = false;
    bool m_termine = false;
    std::atomic<std::uint64_t> m_next{1};
    std::atomic<std::uint64_t> m_current{0};
};

/// Ligne du script indiquée par une trace d'erreur.
std::optional<int> ligne_script(const QString &traceback);

} // namespace python

Q_DECLARE_METATYPE(python::ScriptOutput)
