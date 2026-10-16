// Modélisation : ajustement d'un modèle à des points de mesure.
//
// Les modèles linéaires en leurs paramètres (constante à polynôme, logarithmes) sont ajustés
// exactement par moindres carrés ; les autres par Levenberg-Marquardt, à partir de valeurs
// initiales estimées automatiquement.

#pragma once

#include "core/expr.h"
#include "core/format.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace fit {

/// Type de modèle ; `degree` sert au polynôme, `expression` au modèle utilisateur.
struct ModelKind {
    enum Type {
        Constant, Linear, Affine, Polynomial, Parabola, Power, ExpX, ExpInvX, Charge, Discharge,
        Ln, Log10, Sine, Cosine, DampedSine, DampedCosine, User,
    } type = Affine;
    int degree = 3;
    QString expression;

    bool operator==(const ModelKind &o) const
    {
        return type == o.type && (type != Polynomial || degree == o.degree) && (type != User || expression == o.expression);
    }

    /// Les modèles proposés dans la grille de vignettes, dans l'ordre.
    static std::vector<ModelKind> catalog();
    QString label() const;
    /// Expression générale, en x et y.
    QString templ() const;
    /// Paramètres fixés par défaut : Δ ne peut pas être ajusté en même temps que A.
    std::vector<std::pair<QString, double>> defaultFixed() const;
    bool isLinear() const;
    /// Nom du type dans les fichiers (« Affine », « Polynomial »…).
    QString typeName() const;
    static std::optional<Type> typeFromName(const QString &name);
};

/// Modèle prêt à être évalué.
class Model {
public:
    /// Pour un modèle utilisateur, `xname` est le nom de la variable et `constants` les
    /// paramètres du document, connus. En cas d'erreur, `nullopt` et le message dans `error`.
    static std::optional<Model> make(const ModelKind &kind, const QString &xname, const std::vector<std::pair<QString, double>> &constants, QString *error = nullptr);

    const ModelKind &kind() const { return m_kind; }
    const QStringList &params() const { return m_params; }

    std::vector<double> eval(const std::vector<double> &p, const std::vector<double> &x) const;
    double eval1(const std::vector<double> &p, double x) const;
    /// Fonctions de base des modèles linéaires en leurs paramètres.
    std::optional<std::vector<double>> basis(double x) const;
    /// Expression du modèle avec ses paramètres numériques, pour l'affichage.
    QString display(const std::vector<double> &p, const QString &xname, const QString &yname, NumberStyle style) const;
    /// Formule du tableur équivalente (pleine précision), pour créer la grandeur modèle.
    QString formula(const std::vector<double> &p, const QString &xname) const;

private:
    QString userSource() const;

    ModelKind m_kind;
    QStringList m_params;
    std::optional<expr::Expr> m_userExpr;
    QString m_xname;
    std::vector<std::pair<QString, double>> m_constants;
};

struct FitResult {
    std::vector<double> params;
    /// Incertitude-type de chaque paramètre (NaN si fixé ou indéterminée).
    std::vector<double> uncertainties;
    /// Coefficient de détermination R².
    double r2 = NAN;
    /// Écart-type des résidus, √(Σr²/(n − p)).
    double residualStd = NAN;
    size_t n = 0;
    int iterations = 0;
    /// Remarque à afficher (paramètres liés, convergence difficile…).
    QString warning;
};

struct FitOptions {
    /// Pour chaque paramètre : sa valeur s'il est fixé.
    std::vector<std::optional<double>> fixed;
    /// Valeurs initiales imposées (sinon estimées automatiquement).
    std::optional<std::vector<double>> initial;
};

/// Ajuste le modèle aux points (x, y) ; les points incomplets sont ignorés.
std::optional<FitResult> fit(const Model &model, const std::vector<double> &x, const std::vector<double> &y, const FitOptions &opts, QString *error);
/// Valeurs initiales raisonnables pour un modèle non linéaire.
std::vector<double> initialGuess(const Model &model, const std::vector<double> &xs, const std::vector<double> &ys, const std::vector<std::optional<double>> &fixed);

} // namespace fit
