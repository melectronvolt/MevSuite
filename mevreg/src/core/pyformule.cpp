#include "core/pyformule.h"
#include "core/document.h"
#include "core/pyexport.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace pyexport {

namespace {

const QStringList KEYWORDS = {
    u"False"_s, u"None"_s, u"True"_s, u"and"_s, u"as"_s, u"assert"_s, u"async"_s, u"await"_s, u"break"_s, u"class"_s,
    u"continue"_s, u"def"_s, u"del"_s, u"elif"_s, u"else"_s, u"except"_s, u"finally"_s, u"for"_s, u"from"_s, u"global"_s,
    u"if"_s, u"import"_s, u"in"_s, u"is"_s, u"lambda"_s, u"nonlocal"_s, u"not"_s, u"or"_s, u"pass"_s, u"raise"_s,
    u"return"_s, u"try"_s, u"while"_s, u"with"_s, u"yield"_s, u"np"_s, u"plt"_s, u"integ"_s, u"deriv"_s, u"_mevreg_incertitude"_s, u"_valides"_s,
    u"_mevreg_lignes"_s, u"_mevreg_reduit"_s, u"_mevreg_diffuse"_s, u"_mevreg_tailles"_s, u"_mevreg_tours"_s,
    u"ax"_s, u"ax2"_s, u"xl"_s, u"ValueError"_s,
};

struct OperateurPython {
    QString signe;
    QString fonction;
    int priorite;
};

OperateurPython operateur_python(expr::BinOp op)
{
    switch (op) {
    case expr::BinOp::Add: return {u" + "_s, u"np.add"_s, 1};
    case expr::BinOp::Sub: return {u" - "_s, u"np.subtract"_s, 1};
    case expr::BinOp::Mul: return {u"*"_s, u"np.multiply"_s, 2};
    case expr::BinOp::Div: return {u"/"_s, u"np.divide"_s, 2};
    case expr::BinOp::Pow: return {u"**"_s, u"np.power"_s, 4};
    }
    Q_UNREACHABLE();
}

int priorite(const expr::Node *e)
{
    if (e->kind == expr::Node::Bin)
        return operateur_python(e->op).priorite;
    if (e->kind == expr::Node::Neg)
        return 3;
    return 5;
}

} // namespace

QString FormulesPython::nom(const QString &name) const
{
    if (!KEYWORDS.contains(name))
        return name;
    // mot réservé : suffixé de « _ », autant de fois qu'il faut pour ne pas prendre le nom d'une
    // autre grandeur ou d'un paramètre du document (« class » et « class_ »)
    QString n = name + u'_';
    while (m_document && (m_document->par_nom(n) || m_document->parametre_par_nom(n)))
        n += u'_';
    return n;
}

QString FormulesPython::formule(const expr::Expr &expression)
{
    // Avec deux longueurs différentes, MeVReg conserve les numéros de ligne.
    // Une valeur seule, comme un facteur d'étalonnage, s'applique à toute la colonne.
    std::optional<size_t> longueur;
    if (m_document) {
        for (const QString &variable : expression.variables()) {
            const auto *q = m_document->par_nom(variable);
            if (!q || q->values.size() == 1)
                continue;
            if (longueur && *longueur != q->values.size())
                return noeud(expression.racine(), true);
            longueur = q->values.size();
        }
    }
    return noeud(expression.racine(), false);
}

QString FormulesPython::par_ligne(const QString &operation, const QString &a, const QString &b)
{
    m_lignes = true;
    return u"_mevreg_lignes(%1, %2, %3)"_s.arg(operation, a, b);
}

QString FormulesPython::noeud(const expr::Node *e, bool aligne)
{
    using expr::BinOp;
    using expr::Node;
    switch (e->kind) {
    case Node::Num:
        // entiers écrits sans décimale : v**2, 2*np.pi
        if (std::floor(e->num) == e->num && std::abs(e->num) < 1e15)
            return QString::number(static_cast<qint64>(e->num));
        return nombre_python(e->num);
    case Node::Var:
        if (e->name == u"pi" || e->name == u"π")
            return u"np.pi"_s;
        if (e->name == u"e" && (!m_document || (!m_document->par_nom(e->name) && !m_document->parametre_par_nom(e->name))))
            return u"np.e"_s;
        return nom(e->name);
    case Node::Neg: {
        const QString s = noeud(e->args[0].get(), aligne);
        return priorite(e->args[0].get()) < 4 ? u"-("_s + s + u')' : u'-' + s;
    }
    case Node::Bin: {
        const Node *a = e->args[0].get();
        const Node *b = e->args[1].get();
        // a + (−b) s'écrit a - b, et a − (−b) s'écrit a + b
        if ((e->op == BinOp::Add || e->op == BinOp::Sub) && b->kind == Node::Neg) {
            Node flipped = *e;
            flipped.op = e->op == BinOp::Add ? BinOp::Sub : BinOp::Add;
            flipped.args = {e->args[0], b->args[0]};
            return noeud(&flipped, aligne);
        }
        const auto op = operateur_python(e->op);
        if (aligne)
            return par_ligne(op.fonction, noeud(a, aligne), noeud(b, aligne));
        const int p = op.priorite;
        auto wrap = [&](const Node *x, bool right) {
            const QString s = noeud(x, aligne);
            const int px = priorite(x);
            const bool need = px < p || (right && px == p && (e->op == BinOp::Sub || e->op == BinOp::Div)) || (e->op == BinOp::Pow && !right && px <= p);
            return need ? u'(' + s + u')' : s;
        };
        return wrap(a, false) + op.signe + wrap(b, true);
    }
    case Node::Call: {
        QStringList args;
        for (const auto &x : e->args)
            args << noeud(x.get(), aligne);
        const QString j = args.join(u", "_s);
        const QString &f = e->name;
        const size_t n = e->args.size();
        if (f == u"asin") return u"np.arcsin("_s + j + u')';
        if (f == u"acos") return u"np.arccos("_s + j + u')';
        if (f == u"atan") return u"np.arctan("_s + j + u')';

        if (f == u"ln") return u"np.log("_s + j + u')';
        if (f == u"log" || f == u"log10") return u"np.log10("_s + j + u')';
        if (f == u"rad") return u"np.radians("_s + j + u')';
        if (f == u"deg") return u"np.degrees("_s + j + u')';

        // sinc de numpy : sin(πx)/(πx)
        if (f == u"sinc") return u"np.sinc(("_s + j + u") / np.pi)"_s;
        if (f == u"round")
            return u"(np.trunc(%1) + np.sign(%1)*(np.abs(%1 - np.trunc(%1)) >= 0.5))"_s.arg(u"("_s + j + u")"_s);
        if (n == 2 && (f == u"min" || f == u"max" || f == u"atan2" || f == u"pow")) {
            QString operation;
            if (f == u"min") operation = u"np.fmin"_s;
            if (f == u"max") operation = u"np.fmax"_s;
            if (f == u"atan2") operation = u"np.arctan2"_s;
            if (f == u"pow") operation = u"np.power"_s;
            return aligne ? par_ligne(operation, args[0], args[1]) : operation + u'(' + j + u')';
        }
        if (f == u"min" || f == u"max" || f == u"sum" || f == u"mean" || f == u"std") {
            if (m_document) {
                m_reduction = true;
                return u"_mevreg_reduit(np.%1, %2)"_s.arg(f, j);
            }
            return u"np.nan%1(%2)"_s.arg(f, j);
        }
        if (f == u"len") return u"np.size("_s + j + u')';
        if (f == u"deriv") {
            m_derivee = true;
            return (m_document ? u"deriv("_s : u"np.gradient("_s) + j + u')';
        }
        if (f == u"integ") {
            m_integrale = true;
            return u"integ("_s + j + u')';
        }
        if (f == u"index") return u"np.arange(np.size("_s + j + u"))"_s;
        return u"np."_s + f + u'(' + j + u')';
    }
    }
    return {};
}

QString FormulesPython::fonctions_auxiliaires() const
{
    QString s;
    if (m_reduction)
        s += uR"PY(def _mevreg_reduit(operation, valeurs):
    """Les statistiques d'une colonne portent sur les mesures finies."""
    valeurs = np.asarray(valeurs, dtype=np.float64)
    if valeurs.ndim:
        valeurs = valeurs[np.isfinite(valeurs)]
    if valeurs.size == 0 and operation is not np.sum:
        return np.nan
    return operation(valeurs)

)PY"_s;
    if (m_lignes)
        s += uR"PY(def _mevreg_lignes(operation, a, b):
    """Calcule les lignes communes et laisse les lignes sans partenaire vides."""
    a, b = np.asarray(a, dtype=np.float64), np.asarray(b, dtype=np.float64)
    if a.size == b.size or a.size == 1 or b.size == 1:
        return operation(a, b)
    n = min(a.size, b.size)
    resultat = np.full(max(a.size, b.size), np.nan)
    resultat[:n] = operation(a[:n], b[:n])
    return resultat

)PY"_s;
    if (m_derivee || m_integrale)
        s += uR"PY(def _valides(y, x):
    """Repère les lignes où les deux mesures sont renseignées."""
    y, x = np.atleast_1d(y), np.atleast_1d(x)
    n = min(y.size, x.size)
    lignes = np.flatnonzero(np.isfinite(y[:n]) & np.isfinite(x[:n]))
    resultat = np.full(max(y.size, x.size), np.nan)
    return y[lignes], x[lignes], lignes, resultat

)PY"_s;
    if (m_derivee)
        s += uR"PY(def deriv(y, x):
    """Dérivée dy/dx sur les couples de mesures disponibles."""
    y, x, lignes, resultat = _valides(y, x)
    if lignes.size >= 2:
        with np.errstate(divide="ignore", invalid="ignore"):
            pente = np.gradient(y, x)
        resultat[lignes] = np.where(np.isfinite(pente), pente, np.nan)
    return resultat

)PY"_s;
    if (m_integrale)
        s += uR"PY(def integ(y, x):
    """Primitive nulle au premier point, puis somme des aires des trapèzes."""
    y, x, lignes, resultat = _valides(y, x)
    if lignes.size:
        resultat[lignes[0]] = 0.0
        resultat[lignes[1:]] = np.cumsum((y[1:] + y[:-1]) / 2 * np.diff(x))
    return resultat

)PY"_s;
    return s;
}

QString nom_python(const QString &nom)
{
    return FormulesPython{}.nom(nom);
}

QString en_numpy(const expr::Node *e)
{
    return FormulesPython{}.noeud(e);
}

} // namespace pyexport
