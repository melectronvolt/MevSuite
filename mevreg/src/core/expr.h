// Moteur d'expressions du tableur : `Ec = 0.5*m*v^2`, `deriv(x, t)`…
//
// Les valeurs sont des scalaires ou des vecteurs ; les opérations se font élément par
// élément, avec la diffusion de numpy (un vecteur d'une seule valeur se comporte comme un
// scalaire). Deux vecteurs de longueurs différentes donnent un résultat de la plus grande
// longueur, complété par des cases vides.

#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace expr {

/// Valeur d'une expression : scalaire ou vecteur.
struct Value {
    bool scalar = true;
    double s = 0.0;
    std::vector<double> v;

    static Value number(double x) { return {true, x, {}}; }
    static Value vector(std::vector<double> xs) { return {false, 0.0, std::move(xs)}; }

    size_t size() const { return scalar ? 1 : v.size(); }
    /// Valeurs sous forme de vecteur (un scalaire donne un vecteur d'une valeur).
    std::vector<double> toVector() const { return scalar ? std::vector<double>{s} : v; }

    bool operator==(const Value &) const = default;
};

/// Fournit la valeur des noms utilisés dans une expression.
using Env = std::function<std::optional<Value>(const QString &)>;

struct Error {
    QString message;
    /// Position (en caractères) de l'erreur dans le texte, -1 si inconnue.
    int pos = -1;
};

enum class BinOp { Add, Sub, Mul, Div, Pow };

struct Node;
using NodePtr = std::shared_ptr<const Node>;

struct Node {
    enum Kind { Num, Var, Neg, Bin, Call } kind;
    double num = 0.0;
    QString name;
    BinOp op = BinOp::Add;
    std::vector<NodePtr> args;
};

/// Expression analysée.
class Expr {
public:
    Expr() = default;
    explicit Expr(NodePtr root) : m_root(std::move(root)) {}

    const Node *root() const { return m_root.get(); }
    /// Noms de variables utilisés (hors noms de fonctions), sans doublon, dans l'ordre.
    QStringList variables() const;
    /// Évaluation ; `nullopt` en cas d'erreur, décrite dans `err`.
    std::optional<Value> eval(const Env &env, Error *err = nullptr) const;

private:
    NodePtr m_root;
};

/// Résultat d'une analyse : expression ou erreur.
struct Parsed {
    std::optional<Expr> expr;
    Error error;
    bool ok() const { return expr.has_value(); }
};

Parsed parse(const QString &src);
/// Analyse puis évalue ; `nullopt` en cas d'erreur, décrite dans `err`.
std::optional<Value> evalStr(const QString &src, const Env &env, Error *err = nullptr);

bool isIdentStart(QChar c);
bool isIdentChar(QChar c);
/// Vrai si `name` est un identifiant valable pour une grandeur ou un paramètre.
bool isValidName(const QString &name);
/// Vrai si `name` est un nom de fonction réservé.
bool isFunctionName(const QString &name);
/// Constantes reconnues quand aucune grandeur ni aucun paramètre ne porte ce nom.
std::optional<double> constant(const QString &name);

/// Remplace un identifiant par un autre sans toucher au reste du texte (noms de fonctions exclus).
QString renameIdentifier(const QString &src, const QString &oldName, const QString &newName);
/// Vrai si le texte utilise l'identifiant `name` (hors appel de fonction).
bool usesIdentifier(const QString &src, const QString &name);
/// Sépare une définition `nom = expression` ; `nullopt` si la partie gauche n'est pas un nom.
std::optional<std::pair<QString, QString>> splitDefinition(const QString &line);

/// Fonctions disponibles, avec une courte description (aide de l'interface).
struct FunctionHelp {
    const char16_t *signature;
    const char16_t *description;
};
const std::vector<FunctionHelp> &functions();

/// Puissance : entier exact par multiplications répétées (bases négatives acceptées).
double power(double x, double y);

} // namespace expr
