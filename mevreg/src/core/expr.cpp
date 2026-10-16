#include "core/expr.h"

#include "core/numeric.h"
#include "core/stats.h"

#include <QHash>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace expr {

namespace {

enum class Tok { Num, Ident, Plus, Minus, Star, Slash, Caret, LParen, RParen, Comma, End };

struct Token {
    Tok kind;
    double num = 0.0;
    QString ident;
    int pos = 0;
    int len = 0;
};

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

/// Découpe le texte ; chaque jeton garde sa position (en caractères) et sa longueur.
std::optional<std::vector<Token>> lex(const QString &src, Error *err)
{
    std::vector<Token> toks;
    const int n = static_cast<int>(src.size());
    int i = 0;
    while (i < n) {
        const QChar c = src[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        const int start = i;
        if (isDigit(c) || (c == u'.' && i + 1 < n && isDigit(src[i + 1]))) {
            QString s;
            while (i < n && (isDigit(src[i]) || src[i] == u'.')) {
                s.append(src[i]);
                ++i;
            }
            // exposant : e ou E suivi d'un chiffre, éventuellement signé
            if (i < n && (src[i] == u'e' || src[i] == u'E')) {
                int j = i + 1;
                if (j < n && (src[j] == u'+' || src[j] == u'-'))
                    ++j;
                if (j < n && isDigit(src[j])) {
                    s.append(u'e');
                    s.append(src.mid(i + 1, j - i - 1));
                    i = j;
                    while (i < n && isDigit(src[i])) {
                        s.append(src[i]);
                        ++i;
                    }
                }
            }
            bool ok = false;
            const double v = s.toDouble(&ok);
            if (!ok || s.count(u'.') > 1) {
                if (err)
                    *err = {u"nombre mal écrit : « %1 »"_s.arg(s), start};
                return std::nullopt;
            }
            toks.push_back({Tok::Num, v, {}, start, i - start});
            continue;
        }
        if (isIdentStart(c)) {
            QString s;
            while (i < n && isIdentChar(src[i])) {
                s.append(src[i]);
                ++i;
            }
            toks.push_back({Tok::Ident, 0.0, s, start, i - start});
            continue;
        }
        Tok kind;
        switch (c.unicode()) {
        case u'+': kind = Tok::Plus; break;
        case u'-':
        case u'−': kind = Tok::Minus; break;
        case u'*':
        case u'×':
        case u'·':
            if (c == u'*' && i + 1 < n && src[i + 1] == u'*') {
                ++i;
                kind = Tok::Caret;
            } else {
                kind = Tok::Star;
            }
            break;
        case u'/':
        case u'÷': kind = Tok::Slash; break;
        case u'^': kind = Tok::Caret; break;
        case u'(': kind = Tok::LParen; break;
        case u')': kind = Tok::RParen; break;
        case u',':
        case u';': kind = Tok::Comma; break;
        case u'²':
        case u'³':
            toks.push_back({Tok::Caret, 0.0, {}, start, 1});
            toks.push_back({Tok::Num, c == u'²' ? 2.0 : 3.0, {}, start, 1});
            ++i;
            continue;
        default:
            if (err)
                *err = {u"caractère inattendu : « %1 »"_s.arg(c), start};
            return std::nullopt;
        }
        ++i;
        toks.push_back({kind, 0.0, {}, start, i - start});
    }
    toks.push_back({Tok::End, 0.0, {}, n, 0});
    return toks;
}

bool looksLikeDecimalComma(const QString &src)
{
    for (qsizetype i = 0; i + 2 < src.size(); ++i) {
        if (isDigit(src[i]) && src[i + 1] == u',' && isDigit(src[i + 2]))
            return true;
    }
    return false;
}

NodePtr makeNum(double v)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Num;
    n->num = v;
    return n;
}

NodePtr makeVar(const QString &name)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Var;
    n->name = name;
    return n;
}

NodePtr makeNeg(NodePtr a)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Neg;
    n->args = {std::move(a)};
    return n;
}

NodePtr makeBin(BinOp op, NodePtr a, NodePtr b)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Bin;
    n->op = op;
    n->args = {std::move(a), std::move(b)};
    return n;
}

NodePtr makeCall(const QString &name, std::vector<NodePtr> args)
{
    auto n = std::make_shared<Node>();
    n->kind = Node::Call;
    n->name = name;
    n->args = std::move(args);
    return n;
}

/// Erreur d'analyse, transportée par exception à l'intérieur du module seulement.
struct ParseError {
    Error error;
};

class Parser {
public:
    Parser(std::vector<Token> toks, bool decimalComma) : m_toks(std::move(toks)), m_decimalComma(decimalComma) {}

    NodePtr expression()
    {
        NodePtr lhs = term();
        while (true) {
            BinOp op;
            if (peek() == Tok::Plus)
                op = BinOp::Add;
            else if (peek() == Tok::Minus)
                op = BinOp::Sub;
            else
                return lhs;
            advance();
            lhs = makeBin(op, lhs, term());
        }
    }

    Tok peek() const { return m_toks[m_pos].kind; }
    int here() const { return m_toks[m_pos].pos; }

    [[noreturn]] void fail(const QString &msg, int pos = -2) const
    {
        throw ParseError{{msg, pos == -2 ? here() : pos}};
    }

    [[noreturn]] void missingOperator() const
    {
        fail(m_decimalComma ? u"le séparateur décimal est le point : écrire 0.5 et non 0,5"_s
                            : u"opérateur manquant (écrire 2*x et non 2x)"_s);
    }

private:
    const Token &advance()
    {
        const Token &t = m_toks[m_pos];
        if (m_pos + 1 < m_toks.size())
            ++m_pos;
        return t;
    }

    NodePtr term()
    {
        NodePtr lhs = unary();
        while (true) {
            BinOp op;
            if (peek() == Tok::Star)
                op = BinOp::Mul;
            else if (peek() == Tok::Slash)
                op = BinOp::Div;
            else
                return lhs;
            advance();
            lhs = makeBin(op, lhs, unary());
        }
    }

    NodePtr unary()
    {
        if (peek() == Tok::Minus) {
            advance();
            return makeNeg(unary());
        }
        if (peek() == Tok::Plus) {
            advance();
            return unary();
        }
        return power();
    }

    NodePtr power()
    {
        NodePtr base = primary();
        if (peek() == Tok::Caret) {
            advance();
            // associativité à droite, et 2^-1 autorisé
            return makeBin(BinOp::Pow, base, unary());
        }
        return base;
    }

    NodePtr primary()
    {
        const int pos = here();
        const Token t = advance();
        switch (t.kind) {
        case Tok::Num:
            return makeNum(t.num);
        case Tok::Ident: {
            if (peek() != Tok::LParen)
                return makeVar(t.ident);
            advance();
            std::vector<NodePtr> args;
            if (peek() != Tok::RParen) {
                while (true) {
                    args.push_back(expression());
                    if (peek() == Tok::Comma) {
                        advance();
                    } else if (peek() == Tok::RParen) {
                        break;
                    } else if (peek() == Tok::End) {
                        fail(u"parenthèse fermante manquante"_s);
                    } else {
                        missingOperator();
                    }
                }
            }
            advance(); // ')'
            return makeCall(t.ident, std::move(args));
        }
        case Tok::LParen: {
            NodePtr e = expression();
            if (peek() == Tok::RParen) {
                advance();
                return e;
            }
            if (peek() == Tok::End)
                fail(u"parenthèse fermante manquante"_s);
            missingOperator();
        }
        case Tok::End:
            fail(u"expression incomplète"_s, pos);
        case Tok::RParen:
            fail(u"parenthèse fermante inattendue"_s, pos);
        case Tok::Comma:
            if (m_decimalComma)
                fail(u"le séparateur décimal est le point : écrire 0.5 et non 0,5"_s, pos);
            fail(u"virgule inattendue"_s, pos);
        default:
            fail(u"opérande manquant"_s, pos);
        }
    }

    std::vector<Token> m_toks;
    size_t m_pos = 0;
    bool m_decimalComma;
};

void collectVars(const Node *n, QStringList &out)
{
    switch (n->kind) {
    case Node::Num:
        break;
    case Node::Var:
        if (!out.contains(n->name))
            out.append(n->name);
        break;
    default:
        for (const auto &a : n->args)
            collectVars(a.get(), out);
    }
}

// --- évaluation ---------------------------------------------------------------------------

struct EvalError {
    QString message;
};

template <typename F>
Value map1(Value v, F f)
{
    if (v.scalar)
        return Value::number(f(v.s));
    for (double &x : v.v)
        x = f(x);
    return v;
}

template <typename F>
Value zip2(const Value &a, const Value &b, F f)
{
    if (a.scalar && b.scalar)
        return Value::number(f(a.s, b.s));
    if (a.scalar || (a.v.size() == 1 && !b.scalar)) {
        const double x = a.scalar ? a.s : a.v[0];
        std::vector<double> out(b.v.size());
        for (size_t i = 0; i < b.v.size(); ++i)
            out[i] = f(x, b.v[i]);
        return Value::vector(std::move(out));
    }
    if (b.scalar || b.v.size() == 1) {
        const double y = b.scalar ? b.s : b.v[0];
        std::vector<double> out(a.v.size());
        for (size_t i = 0; i < a.v.size(); ++i)
            out[i] = f(a.v[i], y);
        return Value::vector(std::move(out));
    }
    const size_t n = std::max(a.v.size(), b.v.size());
    std::vector<double> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = (i < a.v.size() && i < b.v.size()) ? f(a.v[i], b.v[i]) : NAN;
    return Value::vector(std::move(out));
}

std::vector<double> finiteValues(const Value &v)
{
    if (v.scalar)
        return {v.s};
    std::vector<double> out;
    for (double x : v.v) {
        if (std::isfinite(x))
            out.push_back(x);
    }
    return out;
}

std::vector<double> toVectorArg(const Value &v, const QString &what, const QString &fname)
{
    if (v.scalar)
        throw EvalError{u"%1 : %2 doit être une grandeur, pas un nombre"_s.arg(fname, what)};
    return v.v;
}

double sign(double x)
{
    if (x > 0)
        return 1.0;
    if (x < 0)
        return -1.0;
    if (x == 0)
        return 0.0;
    return NAN;
}

Value evalNode(const Node *n, const Env &env);

Value call(const Node *n, const Env &env)
{
    const QString &name = n->name;
    const auto &args = n->args;
    auto argc = [&](size_t k) {
        if (args.size() != k) {
            throw EvalError{u"%1 attend %2 argument%3, %4 donné%5"_s.arg(name)
                                .arg(k)
                                .arg(k > 1 ? u"s" : u"")
                                .arg(args.size())
                                .arg(args.size() > 1 ? u"s" : u"")};
        }
    };
    auto unary = [&](double (*f)(double)) {
        argc(1);
        return map1(evalNode(args[0].get(), env), f);
    };
    static const QHash<QString, double (*)(double)> simple = {
        {u"sin"_s, [](double x) { return std::sin(x); }},
        {u"cos"_s, [](double x) { return std::cos(x); }},
        {u"tan"_s, [](double x) { return std::tan(x); }},
        {u"asin"_s, [](double x) { return std::asin(x); }},
        {u"acos"_s, [](double x) { return std::acos(x); }},
        {u"atan"_s, [](double x) { return std::atan(x); }},
        {u"sinh"_s, [](double x) { return std::sinh(x); }},
        {u"cosh"_s, [](double x) { return std::cosh(x); }},
        {u"tanh"_s, [](double x) { return std::tanh(x); }},
        {u"exp"_s, [](double x) { return std::exp(x); }},
        {u"ln"_s, [](double x) { return std::log(x); }},
        {u"log"_s, [](double x) { return std::log10(x); }},
        {u"log10"_s, [](double x) { return std::log10(x); }},
        {u"sqrt"_s, [](double x) { return std::sqrt(x); }},
        {u"sinc"_s, [](double x) { return x == 0.0 ? 1.0 : std::sin(x) / x; }},
        {u"abs"_s, [](double x) { return std::abs(x); }},
        {u"sign"_s, &sign},
        {u"floor"_s, [](double x) { return std::floor(x); }},
        {u"ceil"_s, [](double x) { return std::ceil(x); }},
        {u"round"_s, [](double x) { return std::round(x); }},
        {u"rad"_s, [](double x) { return x * std::numbers::pi / 180.0; }},
        {u"deg"_s, [](double x) { return x * 180.0 / std::numbers::pi; }},
    };
    if (auto f = simple.value(name))
        return unary(f);
    if (name == u"atan2") {
        argc(2);
        return zip2(evalNode(args[0].get(), env), evalNode(args[1].get(), env), [](double y, double x) { return std::atan2(y, x); });
    }
    if (name == u"pow") {
        argc(2);
        return zip2(evalNode(args[0].get(), env), evalNode(args[1].get(), env), &power);
    }
    if (name == u"min" || name == u"max") {
        const bool isMin = name == u"min";
        if (args.size() == 1) {
            const auto v = finiteValues(evalNode(args[0].get(), env));
            double r = NAN;
            for (double x : v)
                r = std::isnan(r) ? x : (isMin ? std::min(r, x) : std::max(r, x));
            return Value::number(r);
        }
        if (args.size() == 2) {
            auto f = [isMin](double a, double b) {
                // comme f64::min et f64::max : une valeur manquante est ignorée
                if (std::isnan(a))
                    return b;
                if (std::isnan(b))
                    return a;
                return isMin ? std::min(a, b) : std::max(a, b);
            };
            return zip2(evalNode(args[0].get(), env), evalNode(args[1].get(), env), f);
        }
        throw EvalError{u"%1 attend 1 ou 2 arguments"_s.arg(name)};
    }
    if (name == u"sum") {
        argc(1);
        double s = 0.0;
        for (double x : finiteValues(evalNode(args[0].get(), env)))
            s += x;
        return Value::number(s);
    }
    if (name == u"mean") {
        argc(1);
        const auto v = finiteValues(evalNode(args[0].get(), env));
        double s = 0.0;
        for (double x : v)
            s += x;
        return Value::number(v.empty() ? NAN : s / v.size());
    }
    if (name == u"std") {
        argc(1);
        return Value::number(stats::stdDev(finiteValues(evalNode(args[0].get(), env)), 0));
    }
    if (name == u"len") {
        argc(1);
        return Value::number(static_cast<double>(evalNode(args[0].get(), env).size()));
    }
    if (name == u"index") {
        argc(1);
        const size_t k = evalNode(args[0].get(), env).size();
        std::vector<double> out(k);
        for (size_t i = 0; i < k; ++i)
            out[i] = static_cast<double>(i);
        return Value::vector(std::move(out));
    }
    if (name == u"cumsum") {
        argc(1);
        auto v = evalNode(args[0].get(), env).toVector();
        double acc = 0.0;
        for (double &x : v) {
            acc += x;
            x = acc;
        }
        return Value::vector(std::move(v));
    }
    if (name == u"deriv" || name == u"integ") {
        argc(2);
        const auto y = toVectorArg(evalNode(args[0].get(), env), u"la première grandeur"_s, name);
        const auto x = toVectorArg(evalNode(args[1].get(), env), u"la seconde grandeur"_s, name);
        return Value::vector(name == u"deriv" ? numeric::gradient(y, x) : numeric::cumulativeTrapezoid(y, x));
    }
    throw EvalError{u"fonction inconnue : « %1 »"_s.arg(name)};
}

Value evalNode(const Node *n, const Env &env)
{
    switch (n->kind) {
    case Node::Num:
        return Value::number(n->num);
    case Node::Var: {
        if (env) {
            if (auto v = env(n->name))
                return *v;
        }
        if (auto c = constant(n->name))
            return Value::number(*c);
        throw EvalError{u"nom inconnu : « %1 »"_s.arg(n->name)};
    }
    case Node::Neg:
        return map1(evalNode(n->args[0].get(), env), [](double x) { return -x; });
    case Node::Bin: {
        const Value a = evalNode(n->args[0].get(), env);
        const Value b = evalNode(n->args[1].get(), env);
        switch (n->op) {
        case BinOp::Add: return zip2(a, b, [](double x, double y) { return x + y; });
        case BinOp::Sub: return zip2(a, b, [](double x, double y) { return x - y; });
        case BinOp::Mul: return zip2(a, b, [](double x, double y) { return x * y; });
        case BinOp::Div: return zip2(a, b, [](double x, double y) { return x / y; });
        case BinOp::Pow: return zip2(a, b, &power);
        }
        break;
    }
    case Node::Call:
        return call(n, env);
    }
    return Value::number(NAN);
}

} // namespace

double power(double x, double y)
{
    return std::pow(x, y);
}

bool isIdentStart(QChar c)
{
    return c.isLetter() || c == u'_';
}

bool isIdentChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'_';
}

bool isValidName(const QString &name)
{
    if (name.isEmpty() || !isIdentStart(name[0]))
        return false;
    for (QChar c : name) {
        if (!isIdentChar(c))
            return false;
    }
    return true;
}

static const QStringList &functionNames()
{
    static const QStringList names = {
        u"sin"_s, u"cos"_s, u"tan"_s, u"asin"_s, u"acos"_s, u"atan"_s, u"atan2"_s, u"sinh"_s, u"cosh"_s,
        u"tanh"_s, u"exp"_s, u"ln"_s, u"log"_s, u"log10"_s, u"sqrt"_s, u"sinc"_s, u"abs"_s, u"sign"_s,
        u"floor"_s, u"ceil"_s, u"round"_s, u"rad"_s, u"deg"_s, u"min"_s, u"max"_s, u"sum"_s, u"mean"_s,
        u"std"_s, u"len"_s, u"deriv"_s, u"integ"_s, u"cumsum"_s, u"index"_s, u"pow"_s,
    };
    return names;
}

bool isFunctionName(const QString &name)
{
    return functionNames().contains(name);
}

std::optional<double> constant(const QString &name)
{
    if (name == u"pi" || name == u"π")
        return std::numbers::pi;
    if (name == u"e")
        return std::numbers::e;
    return std::nullopt;
}

QStringList Expr::variables() const
{
    QStringList out;
    if (m_root)
        collectVars(m_root.get(), out);
    return out;
}

std::optional<Value> Expr::eval(const Env &env, Error *err) const
{
    if (!m_root) {
        if (err)
            *err = {u"expression vide"_s, -1};
        return std::nullopt;
    }
    try {
        return evalNode(m_root.get(), env);
    } catch (const EvalError &e) {
        if (err)
            *err = {e.message, -1};
        return std::nullopt;
    }
}

Parsed parse(const QString &src)
{
    Parsed out;
    if (src.trimmed().isEmpty()) {
        out.error = {u"expression vide"_s, -1};
        return out;
    }
    auto toks = lex(src, &out.error);
    if (!toks)
        return out;
    Parser p(std::move(*toks), looksLikeDecimalComma(src));
    try {
        NodePtr e = p.expression();
        if (p.peek() == Tok::RParen)
            p.fail(u"parenthèse fermante en trop"_s);
        if (p.peek() != Tok::End)
            p.missingOperator();
        out.expr = Expr(e);
    } catch (const ParseError &e) {
        out.error = e.error;
    }
    return out;
}

std::optional<Value> evalStr(const QString &src, const Env &env, Error *err)
{
    const Parsed p = parse(src);
    if (!p.ok()) {
        if (err)
            *err = p.error;
        return std::nullopt;
    }
    return p.expr->eval(env, err);
}

QString renameIdentifier(const QString &src, const QString &oldName, const QString &newName)
{
    auto toks = lex(src, nullptr);
    if (!toks)
        return src;
    QString out;
    int last = 0;
    for (size_t k = 0; k < toks->size(); ++k) {
        const Token &t = (*toks)[k];
        if (t.kind != Tok::Ident || t.ident != oldName)
            continue;
        const bool isCall = k + 1 < toks->size() && (*toks)[k + 1].kind == Tok::LParen;
        if (isCall)
            continue;
        out += src.mid(last, t.pos - last);
        out += newName;
        last = t.pos + t.len;
    }
    out += src.mid(last);
    return out;
}

bool usesIdentifier(const QString &src, const QString &name)
{
    auto toks = lex(src, nullptr);
    if (!toks)
        return false;
    for (size_t k = 0; k < toks->size(); ++k) {
        const Token &t = (*toks)[k];
        if (t.kind == Tok::Ident && t.ident == name && !(k + 1 < toks->size() && (*toks)[k + 1].kind == Tok::LParen))
            return true;
    }
    return false;
}

std::optional<std::pair<QString, QString>> splitDefinition(const QString &line)
{
    const qsizetype k = line.indexOf(u'=');
    if (k < 0)
        return std::nullopt;
    const QString lhs = line.left(k).trimmed();
    if (!isValidName(lhs))
        return std::nullopt;
    return std::make_pair(lhs, line.mid(k + 1).trimmed());
}

const std::vector<FunctionHelp> &functions()
{
    static const std::vector<FunctionHelp> list = {
        {u"sin(x)", u"sinus (x en radians)"},
        {u"cos(x)", u"cosinus"},
        {u"tan(x)", u"tangente"},
        {u"asin(x)", u"arc sinus"},
        {u"acos(x)", u"arc cosinus"},
        {u"atan(x)", u"arc tangente"},
        {u"atan2(y, x)", u"angle du point (x, y)"},
        {u"sinh(x)", u"sinus hyperbolique"},
        {u"cosh(x)", u"cosinus hyperbolique"},
        {u"tanh(x)", u"tangente hyperbolique"},
        {u"exp(x)", u"exponentielle"},
        {u"ln(x)", u"logarithme népérien"},
        {u"log(x)", u"logarithme décimal"},
        {u"sqrt(x)", u"racine carrée"},
        {u"sinc(x)", u"sinus cardinal sin(x)/x (vaut 1 en 0)"},
        {u"abs(x)", u"valeur absolue"},
        {u"sign(x)", u"signe (−1, 0 ou 1)"},
        {u"floor(x)", u"partie entière inférieure"},
        {u"ceil(x)", u"partie entière supérieure"},
        {u"round(x)", u"arrondi à l'entier"},
        {u"rad(x)", u"degrés → radians"},
        {u"deg(x)", u"radians → degrés"},
        {u"min(a, b)", u"plus petite valeur ; min(x) : minimum de la grandeur"},
        {u"max(a, b)", u"plus grande valeur ; max(x) : maximum de la grandeur"},
        {u"sum(x)", u"somme des valeurs"},
        {u"mean(x)", u"moyenne des valeurs"},
        {u"std(x)", u"écart-type (division par n)"},
        {u"len(x)", u"nombre de valeurs"},
        {u"deriv(y, x)", u"dérivée dy/dx (différences centrées, pas variable accepté)"},
        {u"integ(y, x)", u"primitive de y nulle au premier point (trapèzes)"},
        {u"cumsum(x)", u"somme cumulée"},
        {u"index(x)", u"numéro de ligne 0, 1, 2… de même longueur que x"},
    };
    return list;
}

} // namespace expr
