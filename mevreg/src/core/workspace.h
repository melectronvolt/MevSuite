// Espace de travail : document, tableurs, graphes, scripts, et fichier `.phy`.
//
// Le fichier `.phy` est celui de MeVReg : un espace de travail enregistré par l'un s'ouvre
// dans l'autre (la disposition des fenêtres, propre à chaque logiciel, est alors refaite).

#pragma once

#include "core/document.h"
#include "core/plotspec.h"

#include <QJsonValue>
#include <QString>

#include <optional>
#include <vector>

/// Tableur : liste ordonnée de grandeurs affichées en colonnes.
struct TableView {
    ViewId id = 0;
    QString name;
    std::vector<QId> columns;
    bool operator==(const TableView &) const = default;
};

/// Script Python de l'espace de travail.
struct Script {
    ViewId id = 0;
    QString name;
    QString code;
    /// Relancé automatiquement quand une grandeur ou un paramètre qu'il lit change.
    bool linked = false;
    bool operator==(const Script &) const = default;
};

/// Informations du TP, reprises dans l'en-tête des exports PDF.
struct Meta {
    QString title, authors, klass, notes;
    bool operator==(const Meta &) const = default;
};

/// Utilisation d'une grandeur, pour prévenir avant suppression ou renommage.
struct Usage {
    enum Kind { Table, Graph, Formula, UncertaintyOf, HasUncertainty, Model, Script } kind;
    QString name;
    QString text() const;
    bool operator==(const Usage &) const = default;
};

class Workspace {
public:
    Document doc;
    std::vector<TableView> tables;
    std::vector<GraphView> graphs;
    Meta meta;
    std::vector<Script> scripts;

    /// Ni grandeur, ni paramètre, ni fenêtre : l'espace de travail du démarrage.
    bool isEmpty() const;
    ViewId allocView();
    ViewId addTable(std::vector<QId> columns);
    ViewId addGraph();
    ViewId addScript(const QString &code);

    const Script *script(ViewId id) const;
    Script *scriptMut(ViewId id);
    const TableView *table(ViewId id) const;
    TableView *tableMut(ViewId id);
    const GraphView *graph(ViewId id) const;
    GraphView *graphMut(ViewId id);

    /// Où une grandeur est-elle utilisée ?
    std::vector<Usage> usages(QId id) const;
    /// Scripts qui utilisent encore un nom (après un renommage, par exemple).
    QStringList scriptsUsing(const QString &name) const;
    /// Supprime une grandeur partout : tableurs, graphes, rattachements.
    void deleteQuantity(QId id);
    /// Renomme une grandeur : formules du tableur et modèles utilisateur suivent ; message d'erreur, vide si réussi.
    QString renameQuantity(QId id, const QString &newName);
    /// Grandeurs qui ne sont affichées dans aucun tableur.
    std::vector<QId> orphans() const;

    QJsonObject toJson() const;
    static Workspace fromJson(const QJsonObject &o);

private:
    std::uint64_t m_nextView = 1;
};

/// Vrai si le code Python utilise l'identifiant `name` (hors chaînes, commentaires et attributs).
bool pythonUses(const QString &code, const QString &name);

namespace phy {

inline constexpr char FORMAT_NAME[] = "mevreg-phy";
inline constexpr int FORMAT_VERSION = 1;

/// Texte du fichier ; `layout` est la disposition des fenêtres (libre).
QString save(const Workspace &ws, const QJsonValue &layout);

struct Loaded {
    Workspace ws;
    QJsonValue layout;
};

/// Lit un fichier `.phy` de n'importe quelle version précédente ; message dans `error` sinon.
std::optional<Loaded> load(const QString &text, QString *error);

} // namespace phy
