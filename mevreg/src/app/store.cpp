#include "app/store.h"

#include "app/scripting.h"
#include "plot/resolve.h"

#include <QCryptographicHash>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QDir>

using namespace Qt::StringLiterals;

QString settingsFile()
{
    // dossier commun aux logiciels de la suite : %APPDATA%\MeVSuite sous Windows, ~/.config/MeVSuite
    // ailleurs (essais : dossier de test de Qt) ; un fichier par logiciel, sans autre dossier
    QString base;
#ifdef Q_OS_WIN
    if (!QStandardPaths::isTestModeEnabled())
        base = qEnvironmentVariable("APPDATA");
#endif
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString dir = QDir(base).filePath(u"MeVSuite"_s);
    const QString file = QDir(dir).filePath(u"mevreg.ini"_s);
    // réglages des versions précédentes (dossier « MevSuite »), déplacés une fois
    static bool migrated = false;
    if (!migrated) {
        migrated = true;
        const QString old = QSettings(QSettings::IniFormat, QSettings::UserScope, u"MevSuite"_s, u"mevreg"_s).fileName();
        if (QFileInfo(old).isFile() && !QFileInfo(file).exists() && QFileInfo(old).absoluteFilePath() != QFileInfo(file).absoluteFilePath()) {
            QDir().mkpath(dir);
            QFile::rename(old, file);
            QDir().rmdir(QFileInfo(old).absolutePath());
        }
    }
    return file;
}

namespace {

constexpr size_t UNDO_LIMIT = 200;

QSettings settings()
{
    return QSettings(settingsFile(), QSettings::IniFormat);
}

/// Les modélisations suivent les données.
void refreshFits(Workspace &ws)
{
    for (auto &g : ws.graphs) {
        for (auto &p : g.plots)
            updateFits(p, ws.doc);
    }
}

} // namespace

Prefs Prefs::load()
{
    QSettings s = settings();
    Prefs p;
    // auto (ou « system », écrit par les premières versions) : thème du bureau
    const QString t = s.value(u"theme"_s, u"auto"_s).toString();
    p.theme = t == u"dark" ? Qt::ColorScheme::Dark : t == u"light" ? Qt::ColorScheme::Light : Qt::ColorScheme::Unknown;
    p.sigDigits = std::clamp(s.value(u"sig_digits"_s, 6).toInt(), 2, 15);
    p.darkPlots = s.value(u"dark_plots"_s, true).toBool();
    p.recent = s.value(u"recent"_s).toStringList();
    const int w = s.value(u"window_width"_s, 0).toInt(), h = s.value(u"window_height"_s, 0).toInt();
    if (w > 0 && h > 0)
        p.windowSize = QSize(w, h);
    p.maximized = s.value(u"maximized"_s, false).toBool();
    bool ok = false;
    const double zoom = s.value(u"ui_zoom"_s, 1.0).toDouble(&ok);
    p.uiZoom = ok && zoom >= UI_ZOOM_MIN && zoom <= UI_ZOOM_MAX ? zoom : 1.0;
    p.confirmClose = s.value(u"confirm_close"_s, true).toBool();
    p.trustedDocuments = s.value(u"trusted_documents"_s).toStringList();
    return p;
}

namespace {

QString fingerprint(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

} // namespace

void Prefs::trust(const QByteArray &bytes)
{
    const QString f = fingerprint(bytes);
    trustedDocuments.removeAll(f);
    trustedDocuments.prepend(f);
    while (trustedDocuments.size() > 200)
        trustedDocuments.removeLast();
}

bool Prefs::trusted(const QByteArray &bytes) const
{
    return trustedDocuments.contains(fingerprint(bytes));
}

void Prefs::save() const
{
    QSettings s = settings();
    s.setValue(u"theme"_s, theme == Qt::ColorScheme::Dark ? u"dark"_s : theme == Qt::ColorScheme::Light ? u"light"_s : u"auto"_s);
    s.setValue(u"sig_digits"_s, sigDigits);
    s.setValue(u"dark_plots"_s, darkPlots);
    s.setValue(u"recent"_s, recent);
    if (windowSize) {
        s.setValue(u"window_width"_s, windowSize->width());
        s.setValue(u"window_height"_s, windowSize->height());
    }
    s.setValue(u"maximized"_s, maximized);
    s.setValue(u"ui_zoom"_s, uiZoom);
    s.setValue(u"confirm_close"_s, confirmClose);
    s.setValue(u"trusted_documents"_s, trustedDocuments);
    // position et taille enregistrées par les premières versions : remplacées par les clés ci-dessus ;
    // virgule décimale : elle suit maintenant la langue
    s.remove(u"geometry"_s);
    s.remove(u"decimal_comma"_s);
}

Store::Store(QObject *parent) : QObject(parent)
{
    m_snapshot = ws;
    m_commitTimer.setSingleShot(true);
    connect(&m_commitTimer, &QTimer::timeout, this, &Store::commit);
    m_linkedTimer.setSingleShot(true);
    m_linkedTimer.setInterval(60);
    connect(&m_linkedTimer, &QTimer::timeout, this, &Store::runLinkedScripts);
    connect(&python::Engine::instance(), &python::Engine::finished, this, &Store::onScriptFinished);
}

void Store::changed()
{
    ++m_revision;
    refreshFits(ws);
    Q_EMIT workspaceChanged();
    scheduleCommit();
    if (std::any_of(ws.scripts.begin(), ws.scripts.end(), [](const Script &s) { return s.linked; }))
        m_linkedTimer.start(60);
}

void Store::prefsChanged()
{
    prefs.save();
    Q_EMIT displayChanged();
}

void Store::message(const QString &text, icons::Status kind)
{
    Q_EMIT messageRequested(text, kind);
}

void Store::setDirty(bool d)
{
    if (m_dirty != d) {
        m_dirty = d;
        Q_EMIT dirtyChanged(d);
    }
}

void Store::scheduleCommit()
{
    m_commitTimer.start(0);
}

void Store::commit()
{
    if (m_revision == m_snapshotRevision)
        return;
    // un glissement de curseur ou de point ne compte que pour une étape
    if (QGuiApplication::mouseButtons() != Qt::NoButton) {
        m_commitTimer.start(120);
        return;
    }
    commitNow();
}

void Store::commitNow()
{
    m_commitTimer.stop();
    if (m_revision == m_snapshotRevision)
        return;
    m_undo.push_back(std::move(m_snapshot));
    if (m_undo.size() > UNDO_LIMIT)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_snapshot = ws;
    m_snapshotRevision = m_revision;
    setDirty(true);
}

bool Store::canUndo() const
{
    return !m_undo.empty() || m_revision != m_snapshotRevision;
}

void Store::restore(Workspace w)
{
    // le code des scripts n'est pas concerné : chaque éditeur a son propre « annuler »
    for (auto &s : w.scripts) {
        if (const Script *cur = ws.script(s.id))
            s.code = cur->code;
    }
    ws = std::move(w);
    ws.doc.touch();
    refreshFits(ws);
    m_snapshot = ws;
    ++m_revision;
    m_snapshotRevision = m_revision;
    setDirty(true);
    Q_EMIT workspaceChanged();
}

void Store::undo()
{
    // une modification pas encore validée l'est d'abord
    commitNow();
    if (m_undo.empty())
        return;
    Workspace prev = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back(ws);
    restore(std::move(prev));
}

void Store::redo()
{
    if (m_redo.empty())
        return;
    Workspace next = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back(ws);
    restore(std::move(next));
}

void Store::reset(Workspace w)
{
    ws = std::move(w);
    refreshFits(ws);
    m_undo.clear();
    m_redo.clear();
    m_snapshot = ws;
    ++m_revision;
    m_snapshotRevision = m_revision;
    // scripts de l'ancien document encore en cours ou en attente : arrêtés, et leurs résultats
    // ne seront pas appliqués au nouveau (`onScriptFinished` ne les connaît plus)
    for (const auto &[job, script] : m_jobs)
        python::Engine::instance().interrupt(job);
    m_jobs.clear();
    runs.clear();
    drawings.clear();
    blockedScripts.clear();
    setDirty(false);
    Q_EMIT workspaceChanged();
}

// --- Python -----------------------------------------------------------------------------------

python::ScriptInput scriptInput(const Workspace &ws, ViewId script)
{
    python::ScriptInput in;
    for (const auto &q : ws.doc.quantities) {
        if (q.origin != script)
            in.quantities.push_back({q.name, q.unit, q.values});
    }
    for (const auto &p : ws.doc.parameters)
        in.parameters.push_back({p.name, p.unit, p.value});
    return in;
}

QByteArray inputHash(const Workspace &ws, ViewId script)
{
    const Script *s = ws.script(script);
    if (!s)
        return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(s->code.toUtf8());
    const QStringList ids = python::identifiers(s->code);
    // le module mevreg lit les grandeurs par leur nom, dans une chaîne (mr.valeurs("x"), mr.tableau()) :
    // un script qui s'en sert dépend de toutes les grandeurs
    const bool all = ids.contains(u"mevreg"_s);
    for (const auto &q : ws.doc.quantities) {
        if (q.origin != script && (all || ids.contains(q.name))) {
            h.addData(q.name.toUtf8());
            h.addData(QByteArrayView(reinterpret_cast<const char *>(q.values.data()), static_cast<qsizetype>(q.values.size() * sizeof(double))));
        }
    }
    for (const auto &p : ws.doc.parameters) {
        if (ids.contains(p.name)) {
            h.addData(p.name.toUtf8());
            h.addData(QByteArrayView(reinterpret_cast<const char *>(&p.value), sizeof(double)));
        }
    }
    return h.result();
}

void Store::setScriptCode(ViewId id, const QString &code)
{
    Script *s = ws.scriptMut(id);
    if (!s || s->code == code)
        return;
    s->code = code;
    if (Script *snap = m_snapshot.scriptMut(id))
        snap->code = code;
    setDirty(true);
    if (s->linked)
        m_linkedTimer.start(700);
}

bool Store::isRunning(ViewId id) const
{
    auto it = runs.find(id);
    return it != runs.end() && it->second.running != 0;
}

void Store::runScript(ViewId id)
{
    const Script *s = ws.script(id);
    if (!s || isRunning(id))
        return;
    python::ScriptInput input = scriptInput(ws, id);
    input.folder = documentFolder;
    const std::uint64_t job = python::Engine::instance().run(s->code, input);
    m_jobs[job] = id;
    ScriptRun &r = runs[id];
    r.running = job;
    r.inputHash = inputHash(ws, id);
    Q_EMIT scriptStateChanged(id);
}

void Store::stopScript(ViewId id)
{
    auto it = runs.find(id);
    if (it != runs.end() && it->second.running)
        python::Engine::instance().interrupt(it->second.running);
}

void Store::runLinkedScripts()
{
    // pas pendant un glisser : le script tourne une fois le curseur relâché… sauf s'il est lié,
    // auquel cas on le relance dès qu'il a fini, pour suivre le curseur en direct
    for (const auto &s : ws.scripts) {
        if (!s.linked || isRunning(s.id) || blockedScripts.count(s.id))
            continue;
        auto it = runs.find(s.id);
        if (it == runs.end() || it->second.inputHash != inputHash(ws, s.id))
            runScript(s.id);
    }
}

void Store::onScriptFinished(std::uint64_t job, const python::ScriptOutput &out)
{
    auto it = m_jobs.find(job);
    if (it == m_jobs.end())
        return;
    const ViewId id = it->second;
    m_jobs.erase(it);
    ScriptRun &r = runs[id];
    if (r.running == job)
        r.running = 0;
    ++r.runs;
    r.stdoutText = out.stdoutText;
    r.error = out.error;
    r.durationMs = out.durationMs;
    QStringList newDrawings;
    for (const auto &d : out.drawings) {
        if (!drawings.count(d.desc.name))
            newDrawings << d.desc.name;
        drawings[d.desc.name] = drawing::Drawing::make(d.desc, d.data);
    }
    if (!ws.script(id)) {
        Q_EMIT scriptStateChanged(id);
        return;
    }
    const scripting::Applied applied = scripting::applyOutput(ws, id, out);
    r.messages = applied.messages;
    changed();
    Q_EMIT scriptStateChanged(id);
    Q_EMIT scriptApplied(id, applied.openGraphs, applied.savefig, newDrawings, applied.actions);
    // les données ont pu changer pendant l'exécution : un script lié se relance
    if (ws.script(id) && ws.script(id)->linked)
        m_linkedTimer.start(60);
}
