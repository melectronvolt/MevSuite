#include "core/workspace.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>

using namespace Qt::StringLiterals;

QString Usage::text() const
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

bool Workspace::isEmpty() const
{
    return doc.quantities.empty() && doc.parameters.empty() && tables.empty() && graphs.empty() && scripts.empty();
}

ViewId Workspace::allocView()
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

ViewId Workspace::addTable(std::vector<QId> columns)
{
    const ViewId id = allocView();
    tables.push_back({id, u"Tableur %1"_s.arg(tables.size() + 1), std::move(columns)});
    return id;
}

ViewId Workspace::addGraph()
{
    const ViewId id = allocView();
    graphs.push_back(GraphView::make(id, u"Graphe %1"_s.arg(graphs.size() + 1)));
    return id;
}

ViewId Workspace::addScript(const QString &code)
{
    const ViewId id = allocView();
    scripts.push_back({id, u"Script %1"_s.arg(scripts.size() + 1), code, false});
    return id;
}

const Script *Workspace::script(ViewId id) const
{
    for (const auto &s : scripts) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

Script *Workspace::scriptMut(ViewId id)
{
    return const_cast<Script *>(std::as_const(*this).script(id));
}

const TableView *Workspace::table(ViewId id) const
{
    for (const auto &t : tables) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

TableView *Workspace::tableMut(ViewId id)
{
    return const_cast<TableView *>(std::as_const(*this).table(id));
}

const GraphView *Workspace::graph(ViewId id) const
{
    for (const auto &g : graphs) {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

GraphView *Workspace::graphMut(ViewId id)
{
    return const_cast<GraphView *>(std::as_const(*this).graph(id));
}

std::vector<Usage> Workspace::usages(QId id) const
{
    std::vector<Usage> out;
    const Quantity *q = doc.get(id);
    if (!q)
        return out;
    for (const auto &t : tables) {
        if (std::find(t.columns.begin(), t.columns.end(), id) != t.columns.end())
            out.push_back({Usage::Table, t.name});
    }
    for (const auto &g : graphs) {
        if (std::any_of(g.plots.begin(), g.plots.end(), [&](const Plot &p) { return p.uses(id); }))
            out.push_back({Usage::Graph, g.name});
        for (const auto &p : g.plots) {
            for (const auto &f : p.fits) {
                if (f.kind.type == fit::ModelKind::User && expr::usesIdentifier(f.kind.expression, q->name) && f.x != id)
                    out.push_back({Usage::Model, g.name});
            }
        }
    }
    for (QId other : doc.formulasUsing(q->name)) {
        if (other != id)
            out.push_back({Usage::Formula, doc.name(other)});
    }
    if (const Quantity *owner = doc.uncertaintyOwner(id))
        out.push_back({Usage::UncertaintyOf, owner->name});
    if (q->uncertainty)
        out.push_back({Usage::HasUncertainty, doc.name(*q->uncertainty)});
    for (const auto &s : scripts) {
        if (pythonUses(s.code, q->name))
            out.push_back({Usage::Script, s.name});
    }
    return out;
}

QStringList Workspace::scriptsUsing(const QString &name) const
{
    QStringList out;
    for (const auto &s : scripts) {
        if (pythonUses(s.code, name))
            out << s.name;
    }
    return out;
}

void Workspace::deleteQuantity(QId id)
{
    for (auto &t : tables)
        std::erase(t.columns, id);
    for (auto &g : graphs) {
        for (auto &p : g.plots)
            p.removeQuantity(id);
    }
    doc.removeQuantity(id);
}

QString Workspace::renameQuantity(QId id, const QString &newName)
{
    auto old = doc.renameQuantity(id, newName);
    if (!old)
        return old.error;
    const QString now = doc.name(id);
    for (auto &g : graphs) {
        for (auto &p : g.plots) {
            for (auto &f : p.fits) {
                if (f.kind.type == fit::ModelKind::User)
                    f.kind.expression = expr::renameIdentifier(f.kind.expression, *old, now);
            }
        }
    }
    return {};
}

std::vector<QId> Workspace::orphans() const
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

QJsonObject Workspace::toJson() const
{
    QJsonArray ts, gs, ss;
    for (const auto &t : tables) {
        QJsonArray cols;
        for (QId c : t.columns)
            cols.append(static_cast<qint64>(c));
        ts.append(QJsonObject{{u"id"_s, static_cast<qint64>(t.id)}, {u"name"_s, t.name}, {u"columns"_s, cols}});
    }
    for (const auto &g : graphs)
        gs.append(graphToJson(g));
    for (const auto &s : scripts)
        ss.append(QJsonObject{{u"id"_s, static_cast<qint64>(s.id)}, {u"name"_s, s.name}, {u"code"_s, s.code}, {u"linked"_s, s.linked}});
    return {{u"doc"_s, doc.toJson()},
            {u"tables"_s, ts},
            {u"graphs"_s, gs},
            {u"meta"_s, QJsonObject{{u"title"_s, meta.title}, {u"authors"_s, meta.authors}, {u"class"_s, meta.klass}, {u"notes"_s, meta.notes}}},
            {u"scripts"_s, ss},
            {u"next_view"_s, static_cast<qint64>(m_nextView)}};
}

Workspace Workspace::fromJson(const QJsonObject &o)
{
    Workspace ws;
    ws.doc = Document::fromJson(o.value(u"doc"_s).toObject());
    for (const QJsonValue v : o.value(u"tables"_s).toArray()) {
        const QJsonObject t = v.toObject();
        TableView tv;
        tv.id = static_cast<ViewId>(t.value(u"id"_s).toInteger());
        tv.name = t.value(u"name"_s).toString();
        for (const QJsonValue c : t.value(u"columns"_s).toArray())
            tv.columns.push_back(static_cast<QId>(c.toInteger()));
        ws.tables.push_back(tv);
    }
    for (const QJsonValue v : o.value(u"graphs"_s).toArray())
        ws.graphs.push_back(graphFromJson(v.toObject()));
    const QJsonObject m = o.value(u"meta"_s).toObject();
    ws.meta = {m.value(u"title"_s).toString(), m.value(u"authors"_s).toString(), m.value(u"class"_s).toString(), m.value(u"notes"_s).toString()};
    for (const QJsonValue v : o.value(u"scripts"_s).toArray()) {
        const QJsonObject s = v.toObject();
        ws.scripts.push_back({static_cast<ViewId>(s.value(u"id"_s).toInteger()), s.value(u"name"_s).toString(), s.value(u"code"_s).toString(),
                              s.value(u"linked"_s).toBool()});
    }
    ws.m_nextView = static_cast<std::uint64_t>(o.value(u"next_view"_s).toInteger(1));
    return ws;
}

bool pythonUses(const QString &code, const QString &name)
{
    const int n = static_cast<int>(code.size());
    int i = 0;
    while (i < n) {
        const QChar c = code[i];
        if (c == u'#') {
            while (i < n && code[i] != u'\n')
                ++i;
            continue;
        }
        if (c == u'"' || c == u'\'') {
            ++i;
            while (i < n && code[i] != c) {
                if (code[i] == u'\\')
                    ++i;
                ++i;
            }
            ++i;
            continue;
        }
        if (c.isLetter() || c == u'_') {
            const int start = i;
            while (i < n && (code[i].isLetterOrNumber() || code[i] == u'_'))
                ++i;
            if (QStringView(code).mid(start, i - start) == name && (start == 0 || code[start - 1] != u'.'))
                return true;
            continue;
        }
        ++i;
    }
    return false;
}

namespace phy {

QString save(const Workspace &ws, const QJsonValue &layout)
{
    QJsonObject file{{u"format"_s, QString::fromLatin1(FORMAT_NAME)},
                     {u"version"_s, FORMAT_VERSION},
                     {u"app_version"_s, u"mevreg-" MEVREG_VERSION ""_s},
                     {u"workspace"_s, ws.toJson()}};
    file.insert(u"layout"_s, layout.isUndefined() ? QJsonValue(QJsonValue::Null) : layout);
    return QString::fromUtf8(QJsonDocument(file).toJson(QJsonDocument::Indented));
}

std::optional<Loaded> load(const QString &text, QString *error)
{
    auto fail = [&](const QString &m) -> std::optional<Loaded> {
        if (error)
            *error = m;
        return std::nullopt;
    };
    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(text.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject())
        return fail(u"fichier illisible : %1"_s.arg(perr.errorString()));
    const QJsonObject file = d.object();
    if (file.value(u"format"_s).toString() != QString::fromLatin1(FORMAT_NAME))
        return fail(u"ce fichier n'est pas un espace de travail MeVReg"_s);
    const int version = file.value(u"version"_s).toInt(-1);
    if (version > FORMAT_VERSION)
        return fail(u"ce fichier a été créé par une version plus récente du logiciel (format %1) : mettez le logiciel à jour"_s.arg(version));
    if (version < 0)
        return fail(u"version de format inconnue"_s);
    if (!file.value(u"workspace"_s).isObject())
        return fail(u"contenu invalide : espace de travail absent"_s);
    // mise à niveau des anciennes versions du format : aucune pour l'instant (0 et 1)
    Loaded l{Workspace::fromJson(file.value(u"workspace"_s).toObject()), file.value(u"layout"_s)};
    l.ws.doc.recompute();
    l.ws.doc.touch();
    return l;
}

} // namespace phy
