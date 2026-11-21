// Espace de travail : document, tableurs, graphes, scripts, et fichier `.phy`.
//
// Les identifiants relient les fenêtres aux grandeurs, y compris après un changement de nom.

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
    /// Dessins à retrouver dans le menu Fenêtres et à régénérer avec ce script.
    QStringList dessins;
    bool operator==(const Script &) const = default;
};

/// Informations du document, reprises dans l'en-tête des exports PDF.
struct Meta {
    QString title, authors, klass, notes;
    bool operator==(const Meta &) const = default;
};

/// Utilisation d'une grandeur, pour prévenir avant suppression ou renommage.
struct Usage {
    enum Kind { Table, Graph, Formula, UncertaintyOf, HasUncertainty, Model, Script } kind;
    QString name;
    QString texte() const;
    bool operator==(const Usage &) const = default;
};

/// Les données d’un TP et les représentations qui les utilisent.
class Seance {
public:
    Document doc;
    std::vector<TableView> tables;
    std::vector<GraphView> graphs;
    Meta meta;
    std::vector<Script> scripts;

    /// Ni grandeur, ni paramètre, ni fenêtre : l'espace de travail du démarrage.
    bool est_vide() const;
    ViewId alloue_vue();
    ViewId ajoute_tableur(std::vector<QId> columns);
    ViewId ajoute_graphe();
    ViewId ajoute_script(const QString &code);

    const Script *script(ViewId id) const;
    const Script *script_du_dessin(const QString &nom) const;
    Script *script_modifiable(ViewId id);
    const TableView *tableur(ViewId id) const;
    TableView *tableur_modifiable(ViewId id);
    const GraphView *graphe(ViewId id) const;
    GraphView *graphe_modifiable(ViewId id);

    /// Colonnes expérimentales dont les lignes doivent rester appariées lors d'un tri, d'une insertion ou d'une suppression.
    std::vector<QId> mesures_liees(const std::vector<QId> &colonnes) const;
    /// Où une grandeur est-elle utilisée ?
    std::vector<Usage> usages(QId id) const;
    /// Scripts qui utilisent encore un nom (après un renommage, par exemple).
    QStringList scripts_utilisant(const QString &name) const;
    /// Supprime une grandeur partout : tableurs, graphes, rattachements.
    void supprime_grandeur(QId id);
    /// Renomme une grandeur : formules du tableur et modèles utilisateur suivent ; message d'erreur, vide si réussi.
    QString renomme_grandeur(QId id, const QString &newName);
    /// Grandeurs qui ne sont affichées dans aucun tableur.
    std::vector<QId> orphelins() const;

    QJsonObject en_json() const;
    static Seance depuis_json(const QJsonObject &o);

private:
    std::uint64_t m_nextView = 1;
};

/// Expressions entre accolades des chaînes formatées comprises. Vrai si le code utilise l'identifiant `name` (hors chaînes, commentaires et attributs).
bool usages_python(const QString &code, const QString &name);

namespace phy {

inline constexpr char FORMAT_NAME[] = "mevreg-phy";
inline constexpr int FORMAT_VERSION = 1;

/// Octets UTF-8 du fichier. La disposition des fenêtres est conservée dans `layout`.
QByteArray enregistre(const Seance &seance, const QJsonValue &disposition);

struct SeanceLue {
    Seance seance;
    QJsonValue disposition;
};

/// Lit les versions 0 et 1 du format. En cas de refus, `erreur` indique la cause.
std::optional<SeanceLue> charge(const QByteArray &octets, QString *erreur);

} // namespace phy
