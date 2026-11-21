#include "core/document.h"

#include "core/formules.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace {

const QStringList RESERVED = {u"pi"_s, u"π"_s};

void retire_nan_finaux(std::vector<double> &v)
{
    while (!v.empty() && std::isnan(v.back()))
        v.pop_back();
}

} // namespace

size_t Grandeur::effectif() const
{
    return static_cast<size_t>(std::count_if(values.begin(), values.end(), [](double v) { return std::isfinite(v); }));
}

std::uint64_t Document::alloue()
{
    // jamais un identifiant déjà pris, même si le fichier lu est incohérent
    std::uint64_t maxUsed = 0;
    for (const auto &q : quantities)
        maxUsed = std::max(maxUsed, q.id);
    for (const auto &p : parameters)
        maxUsed = std::max(maxUsed, p.id);
    m_nextId = std::max(m_nextId, maxUsed + 1);
    return m_nextId++;
}

const Grandeur *Document::obtient(QId id) const
{
    for (const auto &q : quantities) {
        if (q.id == id)
            return &q;
    }
    return nullptr;
}

Grandeur *Document::obtient_modifiable(QId id)
{
    ++m_revision;
    for (auto &q : quantities) {
        if (q.id == id)
            return &q;
    }
    return nullptr;
}

const Grandeur *Document::par_nom(const QString &name) const
{
    for (const auto &q : quantities) {
        if (q.name == name)
            return &q;
    }
    return nullptr;
}

const Parametre *Document::parametre(ParamId id) const
{
    for (const auto &p : parameters) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

Parametre *Document::parametre_modifiable(ParamId id)
{
    ++m_revision;
    for (auto &p : parameters) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

const Parametre *Document::parametre_par_nom(const QString &name) const
{
    for (const auto &p : parameters) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

QString Document::nom(QId id) const
{
    const Grandeur *q = obtient(id);
    return q ? q->name : u"?"_s;
}

const std::vector<double> &Document::valeurs(QId id) const
{
    static const std::vector<double> empty;
    const Grandeur *q = obtient(id);
    return q ? q->values : empty;
}

const Grandeur *Document::proprietaire_incertitude(QId id) const
{
    for (const auto &q : quantities) {
        if (q.uncertainty == id)
            return &q;
    }
    return nullptr;
}

QString Document::verifie_nom(const QString &rawName, std::optional<QId> exceptQ, std::optional<ParamId> exceptP) const
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return u"le nom ne peut pas être vide"_s;
    if (!expr::est_nom_valide(name))
        return u"« %1 » n'est pas un nom valable : lettres, chiffres et _ uniquement, en commençant par une lettre"_s.arg(name);
    if (expr::est_nom_fonction(name) || RESERVED.contains(name))
        return u"« %1 » est un nom réservé (fonction ou constante)"_s.arg(name);
    for (const auto &q : quantities) {
        if (q.name == name && q.id != exceptQ)
            return u"une grandeur s'appelle déjà « %1 »"_s.arg(name);
    }
    for (const auto &p : parameters) {
        if (p.name == name && p.id != exceptP)
            return u"un paramètre s'appelle déjà « %1 »"_s.arg(name);
    }
    return {};
}

QString Document::nom_unique(const QString &rawBase) const
{
    const QString base = expr::est_nom_valide(rawBase) ? rawBase : u"g"_s;
    if (verifie_nom(base).isEmpty())
        return base;
    for (int i = 2;; ++i) {
        const QString n = base + QString::number(i);
        if (verifie_nom(n).isEmpty())
            return n;
    }
}

Outcome<QId> Document::ajoute_experimentale(const QString &nom, const QString &unite, std::vector<double> mesures)
{
    if (const QString e = verifie_nom(nom); !e.isEmpty())
        return Outcome<QId>::echec(e);
    const QId id = alloue();
    Grandeur grandeur;
    grandeur.id = id;
    grandeur.name = nom.trimmed();
    grandeur.unit = unite.trimmed();
    grandeur.values = std::move(mesures);
    quantities.push_back(std::move(grandeur));
    ++m_revision;
    // une formule qui attendait ce nom peut maintenant être calculée
    recalcule();
    return Outcome<QId>::ok(id);
}

Outcome<QId> Document::ajoute_calculee(const QString &nom, const QString &unite, const QString &formule)
{
    if (const QString e = verifie_nom(nom); !e.isEmpty())
        return Outcome<QId>::echec(e);
    const QId id = alloue();
    Grandeur grandeur;
    grandeur.id = id;
    grandeur.name = nom.trimmed();
    grandeur.unit = unite.trimmed();
    grandeur.formula = formule.trimmed();
    quantities.push_back(std::move(grandeur));
    ++m_revision;
    recalcule();
    return Outcome<QId>::ok(id);
}

Outcome<ParamId> Document::ajoute_parametre(const QString &nom, const QString &unite, double value)
{
    if (const QString e = verifie_nom(nom); !e.isEmpty())
        return Outcome<ParamId>::echec(e);
    const ParamId id = alloue();
    Parametre parametre;
    parametre.id = id;
    parametre.name = nom.trimmed();
    parametre.unit = unite.trimmed();
    parametre.value = value;
    std::tie(parametre.min, parametre.max) = bornes_par_defaut(value);
    parametre.step = pas_curseur_rond(parametre.min, parametre.max);
    parameters.push_back(std::move(parametre));
    ++m_revision;
    recalcule();
    return Outcome<ParamId>::ok(id);
}

QString Document::definit_valeur(QId id, size_t row, std::optional<double> value)
{
    Grandeur *q = obtient_modifiable(id);
    if (!q)
        return u"grandeur introuvable"_s;
    if (q->est_calculee())
        return u"« %1 » est calculée par une formule : ses valeurs ne se saisissent pas"_s.arg(q->name);
    if (row >= q->values.size()) {
        if (!value)
            return {};
        q->values.resize(row + 1, NAN);
    }
    q->values[row] = value.value_or(NAN);
    retire_nan_finaux(q->values);
    ++m_revision;
    recalcule();
    return {};
}

QString Document::definit_valeurs(QId id, std::vector<double> values)
{
    Grandeur *q = nullptr;
    for (auto &x : quantities) {
        if (x.id == id)
            q = &x;
    }
    if (!q)
        return u"grandeur introuvable"_s;
    if (q->est_calculee())
        return u"« %1 » est calculée par une formule"_s.arg(q->name);
    retire_nan_finaux(values);
    q->values = std::move(values);
    ++m_revision;
    recalcule();
    return {};
}

bool Document::efface_cases(const std::vector<PlageCases> &plages)
{
    std::set<Grandeur *> modifiees;
    for (const auto &plage : plages) {
        auto q = std::find_if(quantities.begin(), quantities.end(), [&](const Grandeur &colonne) { return colonne.id == plage.grandeur; });
        if (q == quantities.end() || q->est_calculee())
            continue;
        for (size_t i = plage.premiere; i < q->values.size() && i <= plage.derniere; ++i) {
            if (!std::isnan(q->values[i])) {
                q->values[i] = NAN;
                modifiees.insert(&*q);
            }
        }
    }
    if (modifiees.empty())
        return false;
    for (auto *q : modifiees)
        retire_nan_finaux(q->values);
    ++m_revision;
    recalcule();
    return true;
}

void Document::supprime_lignes(const std::vector<QId> &ids, const std::vector<size_t> &rows)
{
    const std::set<size_t> drop(rows.begin(), rows.end());
    for (auto &q : quantities) {
        if (q.est_calculee() || std::find(ids.begin(), ids.end(), q.id) == ids.end())
            continue;
        std::vector<double> kept;
        for (size_t i = 0; i < q.values.size(); ++i) {
            if (!drop.count(i))
                kept.push_back(q.values[i]);
        }
        retire_nan_finaux(kept);
        q.values = std::move(kept);
    }
    ++m_revision;
    recalcule();
}

void Document::permute_lignes(const std::vector<QId> &ids, const std::vector<size_t> &permutation)
{
    for (QId id : ids) {
        auto *q = obtient_modifiable(id);
        if (!q || q->est_calculee()) continue;
        std::vector<double> valeurs;
        valeurs.reserve(permutation.size());
        for (size_t i : permutation) valeurs.push_back(q->valeur(i));
        retire_nan_finaux(valeurs);
        q->values = std::move(valeurs);
    }
    ++m_revision;
    recalcule();
}

void Document::insere_ligne(const std::vector<QId> &ids, size_t row)
{
    for (auto &q : quantities) {
        if (q.est_calculee() || std::find(ids.begin(), ids.end(), q.id) == ids.end())
            continue;
        if (row < q.values.size())
            q.values.insert(q.values.begin() + static_cast<std::ptrdiff_t>(row), NAN);
    }
    ++m_revision;
    recalcule();
}

void Document::definit_unite(QId id, const QString &unit)
{
    for (auto &q : quantities) {
        if (q.id == id && q.unit != unit.trimmed()) {
            q.unit = unit.trimmed();
            ++m_revision;
        }
    }
}

QString Document::definit_formule(QId id, const QString &formula)
{
    for (auto &q : quantities) {
        if (q.id == id) {
            q.formula = formula.trimmed();
            ++m_revision;
            recalcule();
            return {};
        }
    }
    return u"grandeur introuvable"_s;
}

void Document::fige(QId id)
{
    for (auto &q : quantities) {
        if (q.id == id) {
            q.formula.reset();
            q.error.clear();
            ++m_revision;
        }
    }
}

void Document::definit_incertitude(QId of, std::optional<QId> u)
{
    for (auto &q : quantities) {
        if (q.id == of) {
            q.uncertainty = u;
            ++m_revision;
        }
    }
    recalcule();
}

static QString erreur_incertitude(const std::vector<double> &valeurs)
{
    const auto it = std::find_if(valeurs.begin(), valeurs.end(), [](double u) { return u < 0.0; });
    return it == valeurs.end() ? QString() : u"Incertitude négative à la ligne %1. Vérifiez la valeur ou la formule."_s.arg(1 + std::distance(valeurs.begin(), it));
}

Outcome<QId> Document::cree_incertitude(QId of, const QString &formula)
{
    const Grandeur *q = obtient(of);
    if (!q)
        return Outcome<QId>::echec(u"grandeur introuvable"_s);
    QString erreur;
    const auto valeurs = evalue(formula, &erreur);
    if (!valeurs)
        return Outcome<QId>::echec(erreur);
    if (const QString e = erreur_incertitude(valeurs->en_vecteur()); !e.isEmpty())
        return Outcome<QId>::echec(e);
    const QString name = nom_unique(u"u_"_s + q->name);
    const QString unit = q->unit;
    auto id = ajoute_calculee(name, unit, formula);
    if (!id)
        return id;
    definit_incertitude(of, *id);
    return id;
}

void Document::renomme_dans_formules(const QString &oldName, const QString &newName)
{
    for (auto &q : quantities) {
        if (q.formula)
            q.formula = expr::renomme_identifiant(*q.formula, oldName, newName);
    }
}

Outcome<QString> Document::renomme_grandeur(QId id, const QString &rawName)
{
    const QString newName = rawName.trimmed();
    if (const QString e = verifie_nom(newName, id); !e.isEmpty())
        return Outcome<QString>::echec(e);
    const Grandeur *q = obtient(id);
    if (!q)
        return Outcome<QString>::echec(u"grandeur introuvable"_s);
    const QString old = q->name;
    if (old == newName)
        return Outcome<QString>::ok(old);
    renomme_dans_formules(old, newName);
    for (auto &x : quantities) {
        if (x.id == id)
            x.name = newName;
    }
    ++m_revision;
    recalcule();
    return Outcome<QString>::ok(old);
}

Outcome<QString> Document::renomme_parametre(ParamId id, const QString &rawName)
{
    const QString newName = rawName.trimmed();
    if (const QString e = verifie_nom(newName, std::nullopt, id); !e.isEmpty())
        return Outcome<QString>::echec(e);
    const Parametre *p = parametre(id);
    if (!p)
        return Outcome<QString>::echec(u"paramètre introuvable"_s);
    const QString old = p->name;
    if (old == newName)
        return Outcome<QString>::ok(old);
    renomme_dans_formules(old, newName);
    for (auto &x : parameters) {
        if (x.id == id)
            x.name = newName;
    }
    ++m_revision;
    recalcule();
    return Outcome<QString>::ok(old);
}

std::vector<QId> Document::formules_utilisant(const QString &name) const
{
    std::vector<QId> out;
    for (const auto &q : quantities) {
        if (q.formula && expr::utilise_identifiant(*q.formula, name))
            out.push_back(q.id);
    }
    return out;
}

void Document::retire_grandeur(QId id)
{
    std::erase_if(quantities, [&](const Grandeur &q) { return q.id == id; });
    for (auto &q : quantities) {
        if (q.uncertainty == id)
            q.uncertainty.reset();
    }
    ++m_revision;
    recalcule();
}

void Document::retire_parametre(ParamId id)
{
    std::erase_if(parameters, [&](const Parametre &p) { return p.id == id; });
    ++m_revision;
    recalcule();
}

void Document::definit_valeur_parametre(ParamId id, double value)
{
    for (auto &p : parameters) {
        if (p.id == id && p.value != value) {
            p.value = value;
            ++m_revision;
            recalcule();
            return;
        }
    }
}

void Document::deplace_grandeur(QId id, size_t to)
{
    auto it = std::find_if(quantities.begin(), quantities.end(), [&](const Grandeur &q) { return q.id == id; });
    if (it == quantities.end())
        return;
    Grandeur grandeur = std::move(*it);
    quantities.erase(it);
    to = std::min(to, quantities.size());
    quantities.insert(quantities.begin() + static_cast<std::ptrdiff_t>(to), std::move(grandeur));
    ++m_revision;
}

std::optional<expr::Value> Document::evalue_formule(const expr::Parsed &analyse, QString &erreur) const
{
    if (!analyse.ok()) {
        erreur = analyse.error.message;
        return std::nullopt;
    }
    QString sourceEnErreur;
    auto valeur = [&](const QString &nom) -> std::optional<expr::Value> {
        if (const auto *q = par_nom(nom)) {
            // Une colonne vide peut être une mesure légitime. Une formule en échec
            // n'a aucune valeur à transmettre, même à sum() qui accepterait le vide.
            if (!q->error.isEmpty() && q->values.empty()) {
                sourceEnErreur = nom;
                return std::nullopt;
            }
            return expr::Value::vecteur(q->values);
        }
        if (const auto *p = parametre_par_nom(nom))
            return expr::Value::nombre(p->value);
        return std::nullopt;
    };
    expr::Error detail;
    auto resultat = analyse.expr->evalue(valeur, &detail);
    if (!sourceEnErreur.isEmpty()) {
        erreur = u"La formule de « %1 » est en erreur."_s.arg(sourceEnErreur);
        return std::nullopt;
    }
    erreur = detail.message;
    return resultat;
}

void Document::recalcule()
{
    const auto plan = ordonne_formules(*this);
    for (auto &q : quantities)
        q.error.clear();
    QStringList bloquees;
    for (size_t i : plan.bloquees)
        bloquees << quantities[i].name;
    for (size_t i : plan.bloquees) {
        quantities[i].values.clear();
        quantities[i].error = u"Dépendance circulaire. Grandeurs bloquées : %1"_s.arg(bloquees.join(u", "_s));
    }
    // Une grandeur peut elle-même être calculée à partir de son incertitude.
    // On part alors d'une seule case (l'incertitude avant diffusion). Seule
    // cette longueur est lue avant que la formule fournisse les vraies mesures.
    for (size_t i : plan.mesuresDifferees)
        quantities[i].values = {NAN};
    std::vector<std::vector<size_t>> historique;
    if (!plan.mesuresDifferees.empty())
        historique.emplace_back(plan.mesuresDifferees.size(), 1);
    std::set<QId> instables;
    while (true) {
        for (size_t i : plan.calculables) {
            auto &q = quantities[i];
            if (instables.contains(q.id)) {
                q.values.clear();
                q.error = u"Les longueurs des incertitudes dépendent les unes des autres. Vérifiez leurs rattachements."_s;
                continue;
            }
            const auto valeur = evalue_formule(plan.analyses[i], q.error);
            q.values = valeur ? valeur->en_vecteur() : std::vector<double>{};
            if (q.values.size() == 1 && !plan.mesuresParIncertitude[i].empty()) {
                const double constante = q.values.front();
                size_t lignes = 0;
                for (size_t mesure : plan.mesuresParIncertitude[i])
                    lignes = std::max(lignes, quantities[mesure].values.size());
                q.values.resize(lignes, constante);
            }
        }
        // Les formules suivantes ont déjà lu la colonne étendue. On les reprend
        // seulement si une grandeur calculée plus tard fournit une nouvelle longueur.
        if (plan.mesuresDifferees.empty())
            break;
        std::vector<size_t> tailles;
        for (size_t i : plan.mesuresDifferees)
            tailles.push_back(quantities[i].values.size());
        if (tailles == historique.back())
            break;
        const auto precedent = std::find(historique.begin(), historique.end(), tailles);
        if (precedent != historique.end()) {
            // Revenir aux mêmes longueurs après plusieurs tours révèle une boucle.
            // On signale les rattachements qui oscillent, puis les autres colonnes
            // se calculent normalement, comme après une erreur dans une formule.
            for (size_t k = 0; k < tailles.size(); ++k) {
                if (std::any_of(precedent, historique.end(), [&](const auto &tour) { return tour[k] != tailles[k]; }))
                    instables.insert(*quantities[plan.mesuresDifferees[k]].uncertainty);
            }
            historique.clear();
        }
        historique.push_back(std::move(tailles));
    }
    for (size_t i = 0; i < quantities.size(); ++i)
        if (!plan.mesuresParIncertitude[i].empty() && quantities[i].error.isEmpty())
            quantities[i].error = erreur_incertitude(quantities[i].values);
}

std::optional<expr::Value> Document::evalue(const QString &src, QString *error) const
{
    QString erreur;
    auto resultat = evalue_formule(expr::analyse(src), erreur);
    if (error)
        *error = erreur;
    return resultat;
}

std::vector<std::pair<QString, double>> Document::valeurs_parametres() const
{
    std::vector<std::pair<QString, double>> out;
    for (const auto &p : parameters)
        out.emplace_back(p.name, p.value);
    return out;
}

size_t Document::nombre_lignes(const std::vector<QId> &ids) const
{
    size_t n = 0;
    for (QId id : ids) {
        if (const Grandeur *q = obtient(id))
            n = std::max(n, q->values.size());
    }
    return n;
}

QJsonArray valeurs_en_json(const std::vector<double> &v)
{
    QJsonArray a;
    for (double x : v)
        a.append(std::isfinite(x) ? QJsonValue(x) : QJsonValue(QJsonValue::Null));
    return a;
}

std::vector<double> valeurs_depuis_json(const QJsonValue &v)
{
    std::vector<double> out;
    const QJsonArray cellules = v.toArray();
    out.reserve(static_cast<size_t>(cellules.size()));
    for (const QJsonValue x : cellules)
        out.push_back(x.isDouble() ? x.toDouble() : NAN);
    return out;
}

QJsonObject Document::en_json() const
{
    QJsonArray qs;
    for (const auto &q : quantities) {
        QJsonObject o{{u"id"_s, static_cast<qint64>(q.id)}, {u"name"_s, q.name}, {u"unit"_s, q.unit}, {u"values"_s, valeurs_en_json(q.values)}};
        if (q.formula)
            o.insert(u"kind"_s, QJsonObject{{u"type"_s, u"Calculated"_s}, {u"formula"_s, *q.formula}});
        else
            o.insert(u"kind"_s, QJsonObject{{u"type"_s, u"Experimental"_s}});
        if (q.uncertainty)
            o.insert(u"uncertainty"_s, static_cast<qint64>(*q.uncertainty));
        if (q.origin)
            o.insert(u"origin"_s, static_cast<qint64>(*q.origin));
        qs.append(o);
    }
    QJsonArray ps;
    for (const auto &p : parameters) {
        ps.append(QJsonObject{{u"id"_s, static_cast<qint64>(p.id)}, {u"name"_s, p.name}, {u"unit"_s, p.unit}, {u"value"_s, p.value},
                              {u"min"_s, p.min}, {u"max"_s, p.max}, {u"step"_s, p.step}});
    }
    return {{u"quantities"_s, qs}, {u"parameters"_s, ps}, {u"next_id"_s, static_cast<qint64>(m_nextId)}};
}

Document Document::depuis_json(const QJsonObject &o)
{
    Document d;
    for (const QJsonValue v : o.value(u"quantities"_s).toArray()) {
        const QJsonObject qo = v.toObject();
        Grandeur q;
        q.id = static_cast<QId>(qo.value(u"id"_s).toInteger());
        q.name = qo.value(u"name"_s).toString();
        q.unit = qo.value(u"unit"_s).toString();
        q.values = valeurs_depuis_json(qo.value(u"values"_s));
        const QJsonObject kind = qo.value(u"kind"_s).toObject();
        if (kind.value(u"type"_s).toString() == u"Calculated")
            q.formula = kind.value(u"formula"_s).toString();
        if (qo.value(u"uncertainty"_s).isDouble())
            q.uncertainty = static_cast<QId>(qo.value(u"uncertainty"_s).toInteger());
        if (qo.value(u"origin"_s).isDouble())
            q.origin = static_cast<std::uint64_t>(qo.value(u"origin"_s).toInteger());
        d.quantities.push_back(std::move(q));
    }
    for (const QJsonValue v : o.value(u"parameters"_s).toArray()) {
        const QJsonObject po = v.toObject();
        Parametre p;
        p.id = static_cast<ParamId>(po.value(u"id"_s).toInteger());
        p.name = po.value(u"name"_s).toString();
        p.unit = po.value(u"unit"_s).toString();
        p.value = po.value(u"value"_s).toDouble();
        p.min = po.value(u"min"_s).toDouble();
        p.max = po.value(u"max"_s).toDouble(1.0);
        p.step = po.value(u"step"_s).toDouble();
        d.parameters.push_back(p);
    }
    d.m_nextId = static_cast<std::uint64_t>(std::max<qint64>(1, o.value(u"next_id"_s).toInteger(1)));
    // Les erreurs et les valeurs calculées se déduisent des mesures, y compris
    // dans un ancien fichier qui aurait gardé le dernier résultat de la formule.
    d.recalcule();
    return d;
}

std::pair<double, double> bornes_par_defaut(double value)
{
    if (value == 0.0)
        return {-1.0, 1.0};
    if (value > 0.0)
        return {0.0, 2.0 * value};
    return {2.0 * value, 0.0};
}

double pas_curseur_rond(double min, double max)
{
    const double r = std::abs(max - min);
    if (r == 0.0 || !std::isfinite(r))
        return 0.01;
    return std::pow(10.0, std::floor(std::log10(r / 100.0)));
}
