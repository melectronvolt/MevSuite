#include "core/workspace.h"
#include "python/identifiants.h"

#include <QJsonArray>

#include <algorithm>
#include <map>
#include <set>

using namespace Qt::StringLiterals;

QString Usage::texte() const
{
    switch (kind) {
    case Table: return u"tableur « %1 »"_s.arg(name);
    case Graph: return u"graphe « %1 »"_s.arg(name);
    case Formula: return u"formule de « %1 »"_s.arg(name);
    case UncertaintyOf: return u"incertitude de « %1 »"_s.arg(name);
    case HasUncertainty: return u"incertitude rattachée « %1 »"_s.arg(name);
    case Model: return u"modèle utilisateur du graphe « %1 »"_s.arg(name);
    case Script: return u"script Python « %1 »"_s.arg(name);
    }
    return {};
}

std::vector<QId> Seance::mesures_liees(const std::vector<QId> &colonnes) const
{
    std::map<QId, std::vector<QId>> voisins;
    auto relie = [&](QId a, QId b) {
        voisins[a].push_back(b);
        voisins[b].push_back(a);
    };
    for (const auto &t : tables)
        for (QId id : t.columns)
            relie(t.columns.front(), id);
    for (const auto &g : graphs)
        for (const auto &p : g.plots)
            for (const auto &c : p.curves) {
                relie(c.x, c.y);
                for (const auto &id : {c.errXLow, c.errXHigh, c.errYLow, c.errYHigh, c.dosage})
                    if (id)
                        relie(c.x, *id);
            }
    for (const Grandeur &q : doc.quantities) {
        if (q.uncertainty)
            relie(q.id, *q.uncertainty);
        if (!q.formula)
            continue;
        const auto formule = expr::analyse(*q.formula);
        if (formule.ok())
            for (const QString &nom : formule.expr->variables())
                if (const Grandeur *source = doc.par_nom(nom))
                    relie(q.id, source->id);
    }
    std::vector<QId> attente = colonnes, mesures;
    std::set<QId> vus;
    while (!attente.empty()) {
        const QId id = attente.back();
        attente.pop_back();
        if (!vus.insert(id).second)
            continue;
        const auto &suite = voisins[id];
        attente.insert(attente.end(), suite.begin(), suite.end());
        if (const Grandeur *q = doc.obtient(id); q && !q->est_calculee())
            mesures.push_back(id);
    }
    return mesures;
}

bool Seance::est_vide() const
{
    return doc.quantities.empty() && doc.parameters.empty() && tables.empty() && graphs.empty() && scripts.empty();
}

ViewId Seance::alloue_vue()
{
    m_nextView = std::max<std::uint64_t>(m_nextView, 1);
    auto used = [&](std::uint64_t id) {
        return std::any_of(tables.begin(), tables.end(), [&](const auto &t) { return t.id == id; })
               || std::any_of(graphs.begin(), graphs.end(), [&](const auto &g) { return g.id == id; })
               || std::any_of(scripts.begin(), scripts.end(), [&](const auto &s) { return s.id == id; });
    };
    while (used(m_nextView))
        ++m_nextView;
    return m_nextView++;
}

ViewId Seance::ajoute_tableur(std::vector<QId> columns)
{
    const ViewId id = alloue_vue();
    tables.push_back({id, u"Tableur %1"_s.arg(tables.size() + 1), std::move(columns)});
    return id;
}

ViewId Seance::ajoute_graphe()
{
    const ViewId id = alloue_vue();
    graphs.push_back(GraphView::fabrique(id, u"Graphe %1"_s.arg(graphs.size() + 1)));
    return id;
}

ViewId Seance::ajoute_script(const QString &code)
{
    const ViewId id = alloue_vue();
    scripts.push_back({id, u"Script %1"_s.arg(scripts.size() + 1), code, false, {}});
    return id;
}

const Script *Seance::script(ViewId id) const
{
    for (const auto &s : scripts) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

Script *Seance::script_modifiable(ViewId id)
{
    return const_cast<Script *>(std::as_const(*this).script(id));
}

const Script *Seance::script_du_dessin(const QString &nom) const
{
    for (const auto &s : scripts)
        if (s.dessins.contains(nom))
            return &s;
    return nullptr;
}

const TableView *Seance::tableur(ViewId id) const
{
    for (const auto &t : tables) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

TableView *Seance::tableur_modifiable(ViewId id)
{
    return const_cast<TableView *>(std::as_const(*this).tableur(id));
}

const GraphView *Seance::graphe(ViewId id) const
{
    for (const auto &g : graphs) {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

GraphView *Seance::graphe_modifiable(ViewId id)
{
    return const_cast<GraphView *>(std::as_const(*this).graphe(id));
}

std::vector<Usage> Seance::usages(QId id) const
{
    std::vector<Usage> out;
    const Grandeur *q = doc.obtient(id);
    if (!q)
        return out;
    for (const auto &t : tables) {
        if (std::find(t.columns.begin(), t.columns.end(), id) != t.columns.end())
            out.push_back({Usage::Table, t.name});
    }
    for (const auto &g : graphs) {
        if (std::any_of(g.plots.begin(), g.plots.end(), [&](const Plot &p) { return p.utilise(id); }))
            out.push_back({Usage::Graph, g.name});
        for (const auto &p : g.plots) {
            for (const auto &f : p.fits) {
                if (f.kind.type == fit::ModelKind::User && expr::utilise_identifiant(f.kind.expression, q->name) && f.x != id)
                    out.push_back({Usage::Model, g.name});
            }
        }
    }
    for (QId other : doc.formules_utilisant(q->name)) {
        if (other != id)
            out.push_back({Usage::Formula, doc.nom(other)});
    }
    if (const Grandeur *owner = doc.proprietaire_incertitude(id))
        out.push_back({Usage::UncertaintyOf, owner->name});
    if (q->uncertainty)
        out.push_back({Usage::HasUncertainty, doc.nom(*q->uncertainty)});
    for (const auto &s : scripts) {
        if (usages_python(s.code, q->name))
            out.push_back({Usage::Script, s.name});
    }
    return out;
}

QStringList Seance::scripts_utilisant(const QString &name) const
{
    QStringList out;
    for (const auto &s : scripts) {
        if (usages_python(s.code, name))
            out << s.name;
    }
    return out;
}

void Seance::supprime_grandeur(QId id)
{
    for (auto &t : tables)
        std::erase(t.columns, id);
    for (auto &g : graphs) {
        for (auto &p : g.plots)
            p.retire_grandeur(id);
    }
    doc.retire_grandeur(id);
}

QString Seance::renomme_grandeur(QId id, const QString &newName)
{
    auto old = doc.renomme_grandeur(id, newName);
    if (!old)
        return old.error;
    const QString now = doc.nom(id);
    for (auto &g : graphs) {
        for (auto &p : g.plots) {
            for (auto &f : p.fits) {
                if (f.kind.type == fit::ModelKind::User)
                    f.kind.expression = expr::renomme_identifiant(f.kind.expression, *old, now);
            }
        }
    }
    return {};
}

std::vector<QId> Seance::orphelins() const
{
    std::vector<QId> out;
    for (const auto &q : doc.quantities) {
        const bool shown = std::any_of(tables.begin(), tables.end(), [&](const TableView &t) {
            return std::find(t.columns.begin(), t.columns.end(), q.id) != t.columns.end();
        });
        if (!shown)
            out.push_back(q.id);
    }
    return out;
}

QJsonObject Seance::en_json() const
{
    QJsonArray ts, gs, ss;
    for (const auto &t : tables) {
        QJsonArray cols;
        for (QId c : t.columns)
            cols.append(static_cast<qint64>(c));
        ts.append(QJsonObject{{u"id"_s, static_cast<qint64>(t.id)}, {u"name"_s, t.name}, {u"columns"_s, cols}});
    }
    for (const auto &g : graphs)
        gs.append(graphe_en_json(g));
    for (const auto &s : scripts) {
        QJsonObject script{{u"id"_s, static_cast<qint64>(s.id)}, {u"name"_s, s.name}, {u"code"_s, s.code}, {u"linked"_s, s.linked}};
        if (!s.dessins.isEmpty())
            script.insert(u"drawings"_s, QJsonArray::fromStringList(s.dessins));
        ss.append(script);
    }
    return {{u"doc"_s, doc.en_json()},
            {u"tables"_s, ts},
            {u"graphs"_s, gs},
            {u"meta"_s, QJsonObject{{u"title"_s, meta.title}, {u"authors"_s, meta.authors}, {u"class"_s, meta.klass}, {u"notes"_s, meta.notes}}},
            {u"scripts"_s, ss},
            {u"next_view"_s, static_cast<qint64>(m_nextView)}};
}

Seance Seance::depuis_json(const QJsonObject &o)
{
    Seance ws;
    ws.doc = Document::depuis_json(o.value(u"doc"_s).toObject());
    for (const QJsonValue v : o.value(u"tables"_s).toArray()) {
        const QJsonObject t = v.toObject();
        TableView tv;
        tv.id = static_cast<ViewId>(t.value(u"id"_s).toInteger());
        tv.name = t.value(u"name"_s).toString();
        for (const QJsonValue c : t.value(u"columns"_s).toArray())
            tv.columns.push_back(static_cast<QId>(c.toInteger()));
        ws.tables.push_back(std::move(tv));
    }
    for (const QJsonValue v : o.value(u"graphs"_s).toArray())
        ws.graphs.push_back(graphe_depuis_json(v.toObject()));
    const QJsonObject m = o.value(u"meta"_s).toObject();
    ws.meta = {m.value(u"title"_s).toString(), m.value(u"authors"_s).toString(), m.value(u"class"_s).toString(), m.value(u"notes"_s).toString()};
    for (const QJsonValue v : o.value(u"scripts"_s).toArray()) {
        const QJsonObject s = v.toObject();
        ws.scripts.push_back({static_cast<ViewId>(s.value(u"id"_s).toInteger()), s.value(u"name"_s).toString(), s.value(u"code"_s).toString(),
                              s.value(u"linked"_s).toBool(), {}});
        for (const auto &nom : s.value(u"drawings"_s).toArray())
            if (nom.isString())
                ws.scripts.back().dessins << nom.toString();
    }
    ws.m_nextView = static_cast<std::uint64_t>(std::max<qint64>(1, o.value(u"next_view"_s).toInteger(1)));
    return ws;
}

bool usages_python(const QString &code, const QString &name)
{
    return python::identifiants(code).contains(name);
}
