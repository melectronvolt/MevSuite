#include "core/document.h"

#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace {

const QStringList RESERVED = {u"pi"_s, u"π"_s};

void trimTrailingNan(std::vector<double> &v)
{
    while (!v.empty() && std::isnan(v.back()))
        v.pop_back();
}

} // namespace

size_t Quantity::count() const
{
    return static_cast<size_t>(std::count_if(values.begin(), values.end(), [](double v) { return std::isfinite(v); }));
}

std::uint64_t Document::alloc()
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

const Quantity *Document::get(QId id) const
{
    for (const auto &q : quantities) {
        if (q.id == id)
            return &q;
    }
    return nullptr;
}

Quantity *Document::getMut(QId id)
{
    ++m_revision;
    for (auto &q : quantities) {
        if (q.id == id)
            return &q;
    }
    return nullptr;
}

const Quantity *Document::byName(const QString &name) const
{
    for (const auto &q : quantities) {
        if (q.name == name)
            return &q;
    }
    return nullptr;
}

const Parameter *Document::param(ParamId id) const
{
    for (const auto &p : parameters) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

Parameter *Document::paramMut(ParamId id)
{
    ++m_revision;
    for (auto &p : parameters) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

const Parameter *Document::paramByName(const QString &name) const
{
    for (const auto &p : parameters) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

QString Document::name(QId id) const
{
    const Quantity *q = get(id);
    return q ? q->name : u"?"_s;
}

const std::vector<double> &Document::values(QId id) const
{
    static const std::vector<double> empty;
    const Quantity *q = get(id);
    return q ? q->values : empty;
}

const Quantity *Document::uncertaintyOwner(QId id) const
{
    for (const auto &q : quantities) {
        if (q.uncertainty == id)
            return &q;
    }
    return nullptr;
}

QString Document::checkName(const QString &rawName, std::optional<QId> exceptQ, std::optional<ParamId> exceptP) const
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return u"le nom ne peut pas être vide"_s;
    if (!expr::isValidName(name))
        return u"« %1 » n'est pas un nom valable : lettres, chiffres et _ uniquement, en commençant par une lettre"_s.arg(name);
    if (expr::isFunctionName(name) || RESERVED.contains(name))
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

QString Document::uniqueName(const QString &rawBase) const
{
    const QString base = expr::isValidName(rawBase) ? rawBase : u"g"_s;
    if (checkName(base).isEmpty())
        return base;
    for (int i = 2;; ++i) {
        const QString n = base + QString::number(i);
        if (checkName(n).isEmpty())
            return n;
    }
}

Outcome<QId> Document::addExperimental(const QString &name, const QString &unit, std::vector<double> values)
{
    if (const QString e = checkName(name); !e.isEmpty())
        return Outcome<QId>::fail(e);
    Quantity q;
    q.id = alloc();
    q.name = name.trimmed();
    q.unit = unit.trimmed();
    q.values = std::move(values);
    quantities.push_back(q);
    ++m_revision;
    // une formule qui attendait ce nom peut maintenant être calculée
    recompute();
    return Outcome<QId>::ok(q.id);
}

Outcome<QId> Document::addCalculated(const QString &name, const QString &unit, const QString &formula)
{
    if (const QString e = checkName(name); !e.isEmpty())
        return Outcome<QId>::fail(e);
    Quantity q;
    q.id = alloc();
    q.name = name.trimmed();
    q.unit = unit.trimmed();
    q.formula = formula.trimmed();
    quantities.push_back(q);
    ++m_revision;
    recompute();
    return Outcome<QId>::ok(q.id);
}

Outcome<ParamId> Document::addParameter(const QString &name, const QString &unit, double value)
{
    if (const QString e = checkName(name); !e.isEmpty())
        return Outcome<ParamId>::fail(e);
    Parameter p;
    p.id = alloc();
    p.name = name.trimmed();
    p.unit = unit.trimmed();
    p.value = value;
    std::tie(p.min, p.max) = defaultBounds(value);
    p.step = niceSliderStep(p.min, p.max);
    parameters.push_back(p);
    ++m_revision;
    recompute();
    return Outcome<ParamId>::ok(p.id);
}

QString Document::setValue(QId id, size_t row, std::optional<double> value)
{
    Quantity *q = nullptr;
    for (auto &x : quantities) {
        if (x.id == id)
            q = &x;
    }
    if (!q)
        return u"grandeur introuvable"_s;
    if (q->isCalculated())
        return u"« %1 » est calculée par une formule : ses valeurs ne se saisissent pas"_s.arg(q->name);
    if (row >= q->values.size()) {
        if (!value)
            return {};
        q->values.resize(row + 1, NAN);
    }
    q->values[row] = value.value_or(NAN);
    trimTrailingNan(q->values);
    ++m_revision;
    recompute();
    return {};
}

QString Document::setValues(QId id, std::vector<double> values)
{
    Quantity *q = nullptr;
    for (auto &x : quantities) {
        if (x.id == id)
            q = &x;
    }
    if (!q)
        return u"grandeur introuvable"_s;
    if (q->isCalculated())
        return u"« %1 » est calculée par une formule"_s.arg(q->name);
    trimTrailingNan(values);
    q->values = std::move(values);
    ++m_revision;
    recompute();
    return {};
}

void Document::deleteRows(const std::vector<QId> &ids, const std::vector<size_t> &rows)
{
    const std::set<size_t> drop(rows.begin(), rows.end());
    for (auto &q : quantities) {
        if (q.isCalculated() || std::find(ids.begin(), ids.end(), q.id) == ids.end())
            continue;
        std::vector<double> kept;
        for (size_t i = 0; i < q.values.size(); ++i) {
            if (!drop.count(i))
                kept.push_back(q.values[i]);
        }
        trimTrailingNan(kept);
        q.values = std::move(kept);
    }
    ++m_revision;
    recompute();
}

void Document::insertRow(const std::vector<QId> &ids, size_t row)
{
    for (auto &q : quantities) {
        if (q.isCalculated() || std::find(ids.begin(), ids.end(), q.id) == ids.end())
            continue;
        if (row < q.values.size())
            q.values.insert(q.values.begin() + static_cast<std::ptrdiff_t>(row), NAN);
    }
    ++m_revision;
    recompute();
}

void Document::setUnit(QId id, const QString &unit)
{
    for (auto &q : quantities) {
        if (q.id == id && q.unit != unit.trimmed()) {
            q.unit = unit.trimmed();
            ++m_revision;
        }
    }
}

QString Document::setFormula(QId id, const QString &formula)
{
    for (auto &q : quantities) {
        if (q.id == id) {
            q.formula = formula.trimmed();
            ++m_revision;
            recompute();
            return {};
        }
    }
    return u"grandeur introuvable"_s;
}

void Document::freeze(QId id)
{
    for (auto &q : quantities) {
        if (q.id == id) {
            q.formula.reset();
            q.error.clear();
            ++m_revision;
        }
    }
}

void Document::setUncertainty(QId of, std::optional<QId> u)
{
    for (auto &q : quantities) {
        if (q.id == of) {
            q.uncertainty = u;
            ++m_revision;
        }
    }
    recompute();
}

Outcome<QId> Document::createUncertainty(QId of, const QString &formula)
{
    const Quantity *q = get(of);
    if (!q)
        return Outcome<QId>::fail(u"grandeur introuvable"_s);
    const QString name = uniqueName(u"u_"_s + q->name);
    const QString unit = q->unit;
    auto id = addCalculated(name, unit, formula);
    if (!id)
        return id;
    setUncertainty(of, *id);
    return id;
}

void Document::renameInFormulas(const QString &oldName, const QString &newName)
{
    for (auto &q : quantities) {
        if (q.formula)
            q.formula = expr::renameIdentifier(*q.formula, oldName, newName);
    }
}

Outcome<QString> Document::renameQuantity(QId id, const QString &rawName)
{
    const QString newName = rawName.trimmed();
    if (const QString e = checkName(newName, id); !e.isEmpty())
        return Outcome<QString>::fail(e);
    const Quantity *q = get(id);
    if (!q)
        return Outcome<QString>::fail(u"grandeur introuvable"_s);
    const QString old = q->name;
    if (old == newName)
        return Outcome<QString>::ok(old);
    renameInFormulas(old, newName);
    for (auto &x : quantities) {
        if (x.id == id)
            x.name = newName;
    }
    ++m_revision;
    recompute();
    return Outcome<QString>::ok(old);
}

Outcome<QString> Document::renameParameter(ParamId id, const QString &rawName)
{
    const QString newName = rawName.trimmed();
    if (const QString e = checkName(newName, std::nullopt, id); !e.isEmpty())
        return Outcome<QString>::fail(e);
    const Parameter *p = param(id);
    if (!p)
        return Outcome<QString>::fail(u"paramètre introuvable"_s);
    const QString old = p->name;
    if (old == newName)
        return Outcome<QString>::ok(old);
    renameInFormulas(old, newName);
    for (auto &x : parameters) {
        if (x.id == id)
            x.name = newName;
    }
    ++m_revision;
    recompute();
    return Outcome<QString>::ok(old);
}

std::vector<QId> Document::formulasUsing(const QString &name) const
{
    std::vector<QId> out;
    for (const auto &q : quantities) {
        if (q.formula && expr::usesIdentifier(*q.formula, name))
            out.push_back(q.id);
    }
    return out;
}

void Document::removeQuantity(QId id)
{
    std::erase_if(quantities, [&](const Quantity &q) { return q.id == id; });
    for (auto &q : quantities) {
        if (q.uncertainty == id)
            q.uncertainty.reset();
    }
    ++m_revision;
    recompute();
}

void Document::removeParameter(ParamId id)
{
    std::erase_if(parameters, [&](const Parameter &p) { return p.id == id; });
    ++m_revision;
    recompute();
}

void Document::setParamValue(ParamId id, double value)
{
    for (auto &p : parameters) {
        if (p.id == id && p.value != value) {
            p.value = value;
            ++m_revision;
            recompute();
            return;
        }
    }
}

void Document::moveQuantity(QId id, size_t to)
{
    auto it = std::find_if(quantities.begin(), quantities.end(), [&](const Quantity &q) { return q.id == id; });
    if (it == quantities.end())
        return;
    Quantity q = *it;
    quantities.erase(it);
    to = std::min(to, quantities.size());
    quantities.insert(quantities.begin() + static_cast<std::ptrdiff_t>(to), q);
    ++m_revision;
}

expr::Env Document::envFor(size_t current) const
{
    return [this, current](const QString &name) -> std::optional<expr::Value> {
        for (size_t i = 0; i < quantities.size(); ++i) {
            if (quantities[i].name == name) {
                if (i == current)
                    return std::nullopt;
                return expr::Value::vector(quantities[i].values);
            }
        }
        for (const auto &p : parameters) {
            if (p.name == name)
                return expr::Value::number(p.value);
        }
        return std::nullopt;
    };
}

void Document::recompute()
{
    const size_t n = quantities.size();
    QHash<QString, size_t> names;
    for (size_t i = 0; i < n; ++i)
        names.insert(quantities[i].name, i);
    // analyse des formules
    std::vector<std::optional<expr::Parsed>> parsed(n);
    std::vector<std::vector<size_t>> deps(n);
    for (size_t i = 0; i < n; ++i) {
        const auto &q = quantities[i];
        if (!q.formula)
            continue;
        parsed[i] = expr::parse(*q.formula);
        if (!parsed[i]->ok())
            continue;
        for (const QString &v : parsed[i]->expr->variables()) {
            auto it = names.find(v);
            if (it != names.end() && quantities[*it].isCalculated() && std::find(deps[i].begin(), deps[i].end(), *it) == deps[i].end())
                deps[i].push_back(*it);
        }
    }
    // ordre topologique (Kahn) sur les grandeurs calculées
    std::vector<size_t> indeg(n);
    std::vector<std::vector<size_t>> users(n);
    for (size_t i = 0; i < n; ++i) {
        indeg[i] = deps[i].size();
        for (size_t j : deps[i])
            users[j].push_back(i);
    }
    std::vector<size_t> ready;
    for (size_t i = n; i-- > 0;) {
        if (parsed[i] && indeg[i] == 0)
            ready.push_back(i);
    }
    std::vector<size_t> order;
    std::vector<bool> done(n, false);
    while (!ready.empty()) {
        const size_t i = ready.back();
        ready.pop_back();
        order.push_back(i);
        done[i] = true;
        for (size_t u : users[i]) {
            if (--indeg[u] == 0)
                ready.push_back(u);
        }
    }
    // évaluation
    for (size_t i : order) {
        auto &q = quantities[i];
        const auto &p = *parsed[i];
        if (!p.ok()) {
            q.values.clear();
            q.error = p.error.message;
            continue;
        }
        expr::Error err;
        auto v = p.expr->eval(envFor(i), &err);
        if (v) {
            q.values = v->toVector();
            q.error.clear();
        } else {
            q.values.clear();
            q.error = err.message;
        }
    }
    QStringList cycle;
    for (size_t i = 0; i < n; ++i) {
        if (parsed[i] && !done[i])
            cycle << quantities[i].name;
    }
    for (size_t i = 0; i < n; ++i) {
        if (parsed[i] && !done[i]) {
            quantities[i].values.clear();
            quantities[i].error = u"dépendance circulaire entre "_s + cycle.join(u", "_s);
        }
    }
    // une incertitude constante s'étend à toutes les valeurs de sa grandeur
    for (const auto &owner : quantities) {
        if (!owner.uncertainty)
            continue;
        for (auto &u : quantities) {
            if (u.id == *owner.uncertainty && u.isCalculated() && u.values.size() == 1 && owner.values.size() > 1)
                u.values.assign(owner.values.size(), u.values[0]);
        }
    }
}

std::optional<expr::Value> Document::eval(const QString &src, QString *error) const
{
    expr::Error err;
    auto v = expr::evalStr(src, envFor(SIZE_MAX), &err);
    if (!v && error)
        *error = err.message;
    return v;
}

std::vector<std::pair<QString, double>> Document::paramValues() const
{
    std::vector<std::pair<QString, double>> out;
    for (const auto &p : parameters)
        out.emplace_back(p.name, p.value);
    return out;
}

size_t Document::rowCount(const std::vector<QId> &ids) const
{
    size_t n = 0;
    for (QId id : ids) {
        if (const Quantity *q = get(id))
            n = std::max(n, q->values.size());
    }
    return n;
}

QJsonArray valuesToJson(const std::vector<double> &v)
{
    QJsonArray a;
    for (double x : v)
        a.append(std::isfinite(x) ? QJsonValue(x) : QJsonValue(QJsonValue::Null));
    return a;
}

std::vector<double> valuesFromJson(const QJsonValue &v)
{
    std::vector<double> out;
    for (const QJsonValue x : v.toArray())
        out.push_back(x.isDouble() ? x.toDouble() : NAN);
    return out;
}

QJsonObject Document::toJson() const
{
    QJsonArray qs;
    for (const auto &q : quantities) {
        QJsonObject o{{u"id"_s, static_cast<qint64>(q.id)}, {u"name"_s, q.name}, {u"unit"_s, q.unit}, {u"values"_s, valuesToJson(q.values)}};
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

Document Document::fromJson(const QJsonObject &o)
{
    Document d;
    for (const QJsonValue v : o.value(u"quantities"_s).toArray()) {
        const QJsonObject qo = v.toObject();
        Quantity q;
        q.id = static_cast<QId>(qo.value(u"id"_s).toInteger());
        q.name = qo.value(u"name"_s).toString();
        q.unit = qo.value(u"unit"_s).toString();
        q.values = valuesFromJson(qo.value(u"values"_s));
        const QJsonObject kind = qo.value(u"kind"_s).toObject();
        if (kind.value(u"type"_s).toString() == u"Calculated")
            q.formula = kind.value(u"formula"_s).toString();
        if (qo.value(u"uncertainty"_s).isDouble())
            q.uncertainty = static_cast<QId>(qo.value(u"uncertainty"_s).toInteger());
        if (qo.value(u"origin"_s).isDouble())
            q.origin = static_cast<std::uint64_t>(qo.value(u"origin"_s).toInteger());
        d.quantities.push_back(q);
    }
    for (const QJsonValue v : o.value(u"parameters"_s).toArray()) {
        const QJsonObject po = v.toObject();
        Parameter p;
        p.id = static_cast<ParamId>(po.value(u"id"_s).toInteger());
        p.name = po.value(u"name"_s).toString();
        p.unit = po.value(u"unit"_s).toString();
        p.value = po.value(u"value"_s).toDouble();
        p.min = po.value(u"min"_s).toDouble();
        p.max = po.value(u"max"_s).toDouble(1.0);
        p.step = po.value(u"step"_s).toDouble();
        d.parameters.push_back(p);
    }
    d.m_nextId = static_cast<std::uint64_t>(o.value(u"next_id"_s).toInteger(1));
    return d;
}

std::pair<double, double> defaultBounds(double value)
{
    if (value == 0.0)
        return {-1.0, 1.0};
    if (value > 0.0)
        return {0.0, 2.0 * value};
    return {2.0 * value, 0.0};
}

double niceSliderStep(double min, double max)
{
    const double r = std::abs(max - min);
    if (r == 0.0 || !std::isfinite(r))
        return 0.01;
    return std::pow(10.0, std::floor(std::log10(r / 100.0)));
}
