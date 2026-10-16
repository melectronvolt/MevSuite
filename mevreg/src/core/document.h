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

/// Résultat d'une opération qui peut échouer avec un message.
template <typename T>
struct Outcome {
    std::optional<T> value;
    QString error;

    static Outcome ok(T v) { return {std::move(v), {}}; }
    static Outcome fail(QString e) { return {std::nullopt, std::move(e)}; }
    explicit operator bool() const { return value.has_value(); }
    const T &operator*() const { return *value; }
};

struct Quantity {
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

    bool isCalculated() const { return formula.has_value(); }
    /// Nombre de valeurs renseignées.
    size_t count() const;
    double value(size_t row) const { return row < values.size() ? values[row] : NAN; }

    bool operator==(const Quantity &) const = default;
};

struct Parameter {
    ParamId id = 0;
    QString name;
    QString unit;
    double value = 0, min = 0, max = 1, step = 0.01;

    bool operator==(const Parameter &) const = default;
};

class Document {
public:
    std::vector<Quantity> quantities;
    std::vector<Parameter> parameters;

    /// Augmente à chaque modification : les fenêtres s'en servent pour se mettre à jour.
    std::uint64_t revision() const { return m_revision; }
    void touch() { ++m_revision; }

    const Quantity *get(QId id) const;
    Quantity *getMut(QId id);
    const Quantity *byName(const QString &name) const;
    const Parameter *param(ParamId id) const;
    Parameter *paramMut(ParamId id);
    const Parameter *paramByName(const QString &name) const;
    /// Nom d'une grandeur, « ? » si elle n'existe plus.
    QString name(QId id) const;
    const std::vector<double> &values(QId id) const;
    /// Grandeur dont `id` est l'incertitude.
    const Quantity *uncertaintyOwner(QId id) const;

    /// Vérifie qu'un nom est utilisable (syntaxe, unicité) ; message d'erreur, vide si valable.
    QString checkName(const QString &name, std::optional<QId> exceptQ = {}, std::optional<ParamId> exceptP = {}) const;
    /// Premier nom libre de la forme `base`, `base2`, `base3`…
    QString uniqueName(const QString &base) const;

    Outcome<QId> addExperimental(const QString &name, const QString &unit, std::vector<double> values);
    Outcome<QId> addCalculated(const QString &name, const QString &unit, const QString &formula);
    Outcome<ParamId> addParameter(const QString &name, const QString &unit, double value);

    /// Écrit une valeur (`nullopt` : vide la case) d'une grandeur expérimentale.
    QString setValue(QId id, size_t row, std::optional<double> value);
    /// Remplace toutes les valeurs d'une grandeur expérimentale.
    QString setValues(QId id, std::vector<double> values);
    void deleteRows(const std::vector<QId> &ids, const std::vector<size_t> &rows);
    void insertRow(const std::vector<QId> &ids, size_t row);
    void setUnit(QId id, const QString &unit);
    QString setFormula(QId id, const QString &formula);
    /// Une grandeur calculée devient expérimentale (valeurs figées).
    void freeze(QId id);
    void setUncertainty(QId of, std::optional<QId> u);
    /// Crée l'incertitude `u_x` d'une grandeur, définie par une formule, et la rattache.
    Outcome<QId> createUncertainty(QId of, const QString &formula);
    /// Renomme et met à jour les formules ; renvoie l'ancien nom.
    Outcome<QString> renameQuantity(QId id, const QString &newName);
    Outcome<QString> renameParameter(ParamId id, const QString &newName);
    /// Grandeurs calculées dont la formule utilise ce nom.
    std::vector<QId> formulasUsing(const QString &name) const;
    /// Supprime une grandeur ; les incertitudes qui lui étaient rattachées sont détachées.
    void removeQuantity(QId id);
    void removeParameter(ParamId id);
    void setParamValue(ParamId id, double value);
    void moveQuantity(QId id, size_t to);

    /// Recalcule toutes les grandeurs calculées, dans l'ordre des dépendances.
    void recompute();
    /// Évalue une expression dans le contexte du document.
    std::optional<expr::Value> eval(const QString &src, QString *error = nullptr) const;
    /// Paramètres (nom, valeur), pour les modèles utilisateur.
    std::vector<std::pair<QString, double>> paramValues() const;
    /// Nombre de lignes nécessaire pour afficher ces grandeurs.
    size_t rowCount(const std::vector<QId> &ids) const;

    QJsonObject toJson() const;
    static Document fromJson(const QJsonObject &o);

    bool operator==(const Document &o) const { return quantities == o.quantities && parameters == o.parameters && m_nextId == o.m_nextId; }

private:
    std::uint64_t alloc();
    expr::Env envFor(size_t current) const;
    void renameInFormulas(const QString &oldName, const QString &newName);

    std::uint64_t m_nextId = 1;
    std::uint64_t m_revision = 0;
};

/// Bornes par défaut du curseur d'un paramètre.
std::pair<double, double> defaultBounds(double value);
/// Pas « rond » pour un curseur : environ 1/100 de l'étendue.
double niceSliderStep(double min, double max);

/// Tableau JSON de nombres, cases vides écrites `null`.
QJsonArray valuesToJson(const std::vector<double> &v);
std::vector<double> valuesFromJson(const QJsonValue &v);
