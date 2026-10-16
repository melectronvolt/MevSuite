// Python intégré : interpréteur CPython embarqué (sans site-packages), mini-numpy et
// mini-matplotlib dont les calculs sont faits en C++.
//
// Échange avec le logiciel : les grandeurs et paramètres sont copiés à l'entrée du script ;
// à la sortie, le logiciel récupère les grandeurs créées par `grandeur(...)`, les figures, le
// texte affiché et l'éventuelle erreur. Aucun aller-retour pendant l'exécution.

#pragma once

#include "core/drawing.h"
#include "python/figure.h"

#include <QJsonArray>
#include <QObject>
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
    /// Dossier du document : les chemins relatifs des fichiers (CSV, images) y sont rapportés.
    QString folder;
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
    /// Grandeurs créées par `grandeur(nom, valeurs, unité)`, dans l'ordre.
    std::vector<NamedValues> quantities;
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
    std::uint64_t run(const QString &code, const ScriptInput &input);
    /// Exécute un script et attend son résultat (tests, outils).
    ScriptOutput runSync(const QString &code, const ScriptInput &input);
    /// Script en cours d'exécution (0 : aucun).
    std::uint64_t running() const { return m_current.load(); }
    /// Interrompt le script `id` s'il tourne encore (KeyboardInterrupt).
    void interrupt(std::uint64_t id);

Q_SIGNALS:
    void finished(std::uint64_t id, const python::ScriptOutput &out);

private:
    Engine();

    struct Job {
        std::uint64_t id;
        QString code;
        ScriptInput input;
        std::function<void(std::uint64_t, ScriptOutput)> done;
    };

    void loop();
    std::uint64_t enqueue(const QString &code, const ScriptInput &input, std::function<void(std::uint64_t, ScriptOutput)> done);

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    bool m_stop = false;
    std::atomic<std::uint64_t> m_next{1};
    std::atomic<std::uint64_t> m_current{0};
};

/// Noms utilisés dans un script (identifiants Python, hors chaînes, commentaires et
/// attributs), pour savoir quelles grandeurs il lit et le relancer quand elles changent.
QStringList identifiers(const QString &code);
/// Ligne du script indiquée par une trace d'erreur.
std::optional<int> scriptLine(const QString &traceback);

} // namespace python

Q_DECLARE_METATYPE(python::ScriptOutput)
