// Modèle de données : grandeurs et paramètres d'un document.
//
// L'unité de base est la grandeur (une colonne nommée, avec unité et valeurs). Chaque grandeur
// a un identifiant interne invariable, distinct de son nom : tableurs et graphes la
// référencent par cet identifiant.

#pragma once

#include "core/expr.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

using QId = std::uint64_t;
using ParamId = std::uint64_t;

/// Portion d'une colonne. Les deux lignes sont incluses et la première porte le numéro 0.
struct PlageCases {
    QId grandeur;
    size_t premiere, derniere;
};

/// Résultat d'une opération qui peut échouer avec un message.
template <typename T>
struct Outcome {
    std::optional<T> value;
    QString error;

    static Outcome ok(T v) { return {std::move(v), {}}; }
    static Outcome echec(QString e) { return {std::nullopt, std::move(e)}; }
    explicit operator bool() const { return value.has_value(); }
    const T &operator*() const { return *value; }
};

/// Une colonne expérimentale ou calculée. Une valeur NaN représente une case vide.
struct Grandeur {
    QId id = 0;
    QString name;
    QString unit;
    std::vector<double> values;
    /// Formule du tableur (grandeur calculée) ; vide : grandeur expérimentale.
    std::optional<QString> formula;
    /// Grandeur incertitude rattachée : u(x).
    std::optional<QId> uncertainty;
    /// Script Python qui a produit cette grandeur (réécrite à chaque exécution).
    std::optional<std::uint64_t> origin;
    /// Erreur de calcul (formule invalide, dépendance circulaire…), non enregistrée.
    QString error;

    bool est_calculee() const { return formula.has_value(); }
    /// Nombre de valeurs renseignées.
    size_t effectif() const;
    double valeur(size_t row) const { return row < values.size() ? values[row] : NAN; }

    bool operator==(const Grandeur &) const = default;
};

/// Une valeur commune aux lignes de la séance, par exemple la masse d’un mobile.
struct Parametre {
    ParamId id = 0;
    QString name;
    QString unit;
    double value = 0, min = 0, max = 1, step = 0.01;

    bool operator==(const Parametre &) const = default;
};

class Document {
public:
    std::vector<Grandeur> quantities;
    std::vector<Parametre> parameters;

    /// Augmente à chaque modification : les fenêtres s'en servent pour se mettre à jour.
    std::uint64_t revision() const { return m_revision; }
    void touche() { ++m_revision; }

    const Grandeur *obtient(QId id) const;
    Grandeur *obtient_modifiable(QId id);
    const Grandeur *par_nom(const QString &name) const;
    const Parametre *parametre(ParamId id) const;
    Parametre *parametre_modifiable(ParamId id);
    const Parametre *parametre_par_nom(const QString &name) const;
    /// Nom d'une grandeur, « ? » si elle n'existe plus.
    QString nom(QId id) const;
    const std::vector<double> &valeurs(QId id) const;
    /// Grandeur dont `id` est l'incertitude.
    const Grandeur *proprietaire_incertitude(QId id) const;

    /// Vérifie qu'un nom est utilisable (syntaxe, unicité) ; message d'erreur, vide si valable.
    QString verifie_nom(const QString &name, std::optional<QId> exceptQ = {}, std::optional<ParamId> exceptP = {}) const;
    /// Premier nom libre de la forme `base`, `base2`, `base3`…
    QString nom_unique(const QString &base) const;

    Outcome<QId> ajoute_experimentale(const QString &nom, const QString &unite, std::vector<double> mesures);
    Outcome<QId> ajoute_calculee(const QString &nom, const QString &unite, const QString &formule);
    Outcome<ParamId> ajoute_parametre(const QString &nom, const QString &unite, double value);

    /// Écrit une valeur (`nullopt` : vide la case) d'une grandeur expérimentale.
    QString definit_valeur(QId id, size_t row, std::optional<double> value);
    /// Remplace toutes les valeurs d'une grandeur expérimentale.
    QString definit_valeurs(QId id, std::vector<double> values);
    /// Efface les mesures des plages choisies. Renvoie vrai si une valeur a changé.
    bool efface_cases(const std::vector<PlageCases> &plages);
    void supprime_lignes(const std::vector<QId> &ids, const std::vector<size_t> &rows);
    void permute_lignes(const std::vector<QId> &ids, const std::vector<size_t> &permutation);
    void insere_ligne(const std::vector<QId> &ids, size_t row);
    void definit_unite(QId id, const QString &unit);
    QString definit_formule(QId id, const QString &formula);
    /// Une grandeur calculée devient expérimentale (valeurs figées).
    void fige(QId id);
    void definit_incertitude(QId of, std::optional<QId> u);
    /// Crée l'incertitude `u_x` d'une grandeur, définie par une formule, et la rattache.
    Outcome<QId> cree_incertitude(QId of, const QString &formula);
    /// Renomme et met à jour les formules ; renvoie l'ancien nom.
    Outcome<QString> renomme_grandeur(QId id, const QString &newName);
    Outcome<QString> renomme_parametre(ParamId id, const QString &newName);
    /// Grandeurs calculées dont la formule utilise ce nom.
    std::vector<QId> formules_utilisant(const QString &name) const;
    /// Supprime une grandeur ; les incertitudes qui lui étaient rattachées sont détachées.
    void retire_grandeur(QId id);
    void retire_parametre(ParamId id);
    void definit_valeur_parametre(ParamId id, double value);
    void deplace_grandeur(QId id, size_t to);

    /// Recalcule toutes les grandeurs calculées, dans l'ordre des dépendances.
    void recalcule();
    /// Évalue une expression dans le contexte du document.
    std::optional<expr::Value> evalue(const QString &src, QString *error = nullptr) const;
    /// Paramètres (nom, valeur), pour les modèles utilisateur.
    std::vector<std::pair<QString, double>> valeurs_parametres() const;
    /// Nombre de lignes nécessaire pour afficher ces grandeurs.
    size_t nombre_lignes(const std::vector<QId> &ids) const;

    QJsonObject en_json() const;
    static Document depuis_json(const QJsonObject &o);

    bool operator==(const Document &o) const { return quantities == o.quantities && parameters == o.parameters && m_nextId == o.m_nextId; }

private:
    std::uint64_t alloue();
    std::optional<expr::Value> evalue_formule(const expr::Parsed &analyse, QString &erreur) const;
    void renomme_dans_formules(const QString &oldName, const QString &newName);

    std::uint64_t m_nextId = 1;
    std::uint64_t m_revision = 0;
};

/// Bornes par défaut du curseur d'un paramètre.
std::pair<double, double> bornes_par_defaut(double value);
/// Pas « rond » pour un curseur : environ 1/100 de l'étendue.
double pas_curseur_rond(double min, double max);

/// Tableau JSON de nombres, cases vides écrites `null`.
QJsonArray valeurs_en_json(const std::vector<double> &v);
std::vector<double> valeurs_depuis_json(const QJsonValue &v);
