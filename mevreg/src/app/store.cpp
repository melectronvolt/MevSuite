#include "app/store.h"

#include "app/scripting.h"
#include "plot/resolve.h"

#include <bit>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>

using namespace Qt::StringLiterals;

QString fichier_reglages()
{
    // dossier commun aux logiciels de la suite : %APPDATA%\MeVSuite sous Windows, ~/.config/MeVSuite
    // ailleurs (essais : dossier de test de Qt) ; un fichier par logiciel, sans autre dossier
    QString base;
#ifdef Q_OS_WIN
    if (!QStandardPaths::isTestModeEnabled()) {
        static const QString portable = []() -> QString {
            const QDir app(QCoreApplication::applicationDirPath());
            if (!app.exists(u"mevreg.portable"_s) || !app.mkpath(u"profil"_s))
                return {};
            const QString dir = app.filePath(u"profil"_s);
            QTemporaryFile essai(QDir(dir).filePath(u".ecriture-XXXXXX"_s));
            return essai.open() ? QDir(dir).filePath(u"mevreg.ini"_s) : QString();
        }();
        if (!portable.isEmpty())
            return portable;
        base = qEnvironmentVariable("APPDATA");
    }
#endif
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString dir = QDir(base).filePath(u"MeVSuite"_s);
    const QString file = QDir(dir).filePath(u"mevreg.ini"_s);
    // réglages du dossier « MevSuite » (autre casse), déplacés une fois
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

QSettings reglages()
{
    return QSettings(fichier_reglages(), QSettings::IniFormat);
}

/// Les modélisations suivent les données.
void rafraichit_modeles(Seance &ws)
{
    for (auto &g : ws.graphs) {
        for (auto &p : g.plots)
            maj_modeles(p, ws.doc);
    }
}

} // namespace

Prefs Prefs::charge()
{
    QSettings s = reglages();
    Prefs p;
    p.modeSimple = s.value(u"mode"_s, u"simple"_s).toString() != u"expert";
    // auto (ou « system ») : thème du bureau
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
    p.texteTableur = std::clamp(s.value(u"table_text_percent"_s, p.texteTableur).toInt(), Prefs::texteMin, Prefs::texteMax);
    p.hauteurLigne = std::clamp(s.value(u"table_row_height"_s, p.hauteurLigne).toInt(), Prefs::ligneMin, Prefs::ligneMax);
    p.confirmClose = s.value(u"confirm_close"_s, true).toBool();
    p.trustedDocuments = s.value(u"trusted_documents"_s).toStringList();
    return p;
}

namespace {

QString empreinte(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

} // namespace

void Prefs::fait_confiance(const QByteArray &bytes)
{
    const QString f = empreinte(bytes);
    trustedDocuments.removeAll(f);
    trustedDocuments.prepend(f);
    while (trustedDocuments.size() > 200)
        trustedDocuments.removeLast();
}

bool Prefs::est_de_confiance(const QByteArray &bytes) const
{
    return trustedDocuments.contains(empreinte(bytes));
}

void Prefs::enregistre() const
{
    QSettings s = reglages();
    s.setValue(u"mode"_s, modeSimple ? u"simple"_s : u"expert"_s);
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
    s.setValue(u"table_text_percent"_s, texteTableur);
    s.setValue(u"table_row_height"_s, hauteurLigne);
    s.setValue(u"confirm_close"_s, confirmClose);
    s.setValue(u"trusted_documents"_s, trustedDocuments);
    // clés qui ne servent plus : position et taille (remplacées par les clés ci-dessus), virgule
    // décimale (elle suit la langue)
    s.remove(u"geometry"_s);
    s.remove(u"decimal_comma"_s);
}

Store::Store(QObject *parent) : QObject(parent)
{
    m_snapshot = ws;
    m_commitTimer.setSingleShot(true);
    connect(&m_commitTimer, &QTimer::timeout, this, &Store::valide);
    m_linkedTimer.setSingleShot(true);
    m_linkedTimer.setInterval(60);
    connect(&m_linkedTimer, &QTimer::timeout, this, &Store::lance_scripts_lies);
    connect(&python::Engine::instance(), &python::Engine::fini, this, &Store::apres_script_termine);
}

void Store::modifie()
{
    ++m_revision;
    rafraichit_modeles(ws);
    Q_EMIT espace_travail_change();
    prevoit_validation();
    if (std::any_of(ws.scripts.begin(), ws.scripts.end(), [](const Script &s) { return s.linked; }))
        m_linkedTimer.start(60);
}

void Store::preferences_modifiees()
{
    prefs.enregistre();
    Q_EMIT affichage_change();
}

void Store::message(const QString &text, icons::Status kind)
{
    Q_EMIT message_demande(text, kind);
}

void Store::marque_modifie(bool d)
{
    if (m_dirty != d) {
        m_dirty = d;
        Q_EMIT modification_changee(d);
    }
}

void Store::prevoit_validation()
{
    m_commitTimer.start(0);
}

void Store::valide()
{
    if (m_revision == m_snapshotRevision)
        return;
    // un glissement de curseur ou de point ne compte que pour une étape
    if (QGuiApplication::mouseButtons() != Qt::NoButton) {
        m_commitTimer.start(120);
        return;
    }
    valide_maintenant();
}

Store::Etape::Etape(Seance etat, const Seance &voisin) : document(std::move(etat))
{
    for (auto &q : document.doc.quantities) {
        const auto *autre = voisin.doc.obtient(q.id);
        if (q.values.size() < 128 || !autre || q.values.size() != autre->values.size())
            continue;
        std::vector<std::pair<size_t, double>> delta;
        constexpr size_t limite = 64;
        for (size_t i = 0; i < q.values.size() && delta.size() <= limite; ++i) {
            if (std::bit_cast<quint64>(q.values[i]) != std::bit_cast<quint64>(autre->values[i]))
                delta.emplace_back(i, q.values[i]);
        }
        if (delta.size() <= limite) {
            cellules.emplace(q.id, std::move(delta));
            std::vector<double>().swap(q.values);
        }
    }
}

Seance Store::Etape::recompose(const Seance &voisin) &&
{
    for (const auto &[id, delta] : cellules) {
        auto &valeurs = document.doc.obtient_modifiable(id)->values;
        valeurs = voisin.doc.valeurs(id);
        for (const auto &[i, v] : delta)
            valeurs[i] = v;
    }
    return std::move(document);
}

void Store::valide_maintenant()
{
    m_commitTimer.stop();
    if (m_revision == m_snapshotRevision)
        return;
    m_undo.emplace_back(std::move(m_snapshot), ws);
    if (m_undo.size() > UNDO_LIMIT)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_snapshot = ws;
    m_snapshotRevision = m_revision;
    marque_modifie(true);
}

bool Store::peut_annuler() const
{
    return !m_undo.empty() || m_revision != m_snapshotRevision;
}

void Store::restaure(Seance w)
{
    // le code des scripts n'est pas concerné : chaque éditeur a son propre « annuler »
    for (auto &s : w.scripts) {
        if (const Script *cur = ws.script(s.id))
            s.code = cur->code;
    }
    ws = std::move(w);
    ws.doc.touche();
    rafraichit_modeles(ws);
    m_snapshot = ws;
    ++m_revision;
    m_snapshotRevision = m_revision;
    marque_modifie(true);
    Q_EMIT espace_travail_change();
}

void Store::defait()
{
    // une modification pas encore validée l'est d'abord
    valide_maintenant();
    if (m_undo.empty())
        return;
    Seance prev = std::move(m_undo.back()).recompose(ws);
    m_undo.pop_back();
    m_redo.emplace_back(ws, prev);
    restaure(std::move(prev));
}

void Store::retablit()
{
    valide_maintenant();
    if (m_redo.empty())
        return;
    Seance next = std::move(m_redo.back()).recompose(ws);
    m_redo.pop_back();
    m_undo.emplace_back(ws, next);
    restaure(std::move(next));
}

void Store::reinitialise(Seance w)
{
    m_linkedTimer.stop();
    ws = std::move(w);
    rafraichit_modeles(ws);
    m_undo.clear();
    m_redo.clear();
    m_snapshot = ws;
    ++m_revision;
    m_snapshotRevision = m_revision;
    // scripts de l'ancien document encore en cours ou en attente : arrêtés, et leurs résultats
    // ne seront pas appliqués au nouveau (`apres_script_termine` ne les connaît plus)
    for (const auto &[job, script] : m_jobs)
        python::Engine::instance().interrompt(job);
    m_jobs.clear();
    runs.clear();
    drawings.clear();
    selectedPlot.clear();
    selectedRange.clear();
    pendingAbscissa.clear();
    blockedScripts.clear();
    marque_modifie(false);
    Q_EMIT espace_travail_change();
}

// --- Python -----------------------------------------------------------------------------------

python::ScriptInput entree_script(const Seance &ws, ViewId script)
{
    python::ScriptInput in;
    for (const auto &q : ws.doc.quantities) {
        if (q.origin == script)
            in.previousOutputs << q.name;
        in.quantities.push_back({q.name, q.unit, q.values});
        if (q.formula)
            in.formulas.insert(q.name, *q.formula);
        if (q.uncertainty)
            in.uncertainties.insert(q.name, ws.doc.nom(*q.uncertainty));
    }
    for (const auto &p : ws.doc.parameters)
        in.parameters.push_back({p.name, p.unit, p.value});
    return in;
}

QByteArray empreinte_entrees(const Seance &ws, ViewId script)
{
    const Script *s = ws.script(script);
    if (!s)
        return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    const auto ajoute = [&h](QByteArrayView champ) {
        const quint64 taille = champ.size();
        h.addData(QByteArrayView(reinterpret_cast<const char *>(&taille), sizeof(taille)));
        h.addData(champ);
    };
    ajoute(s->code.toUtf8());
    const QStringList ids = python::identifiants(s->code);
    // le module mevreg lit les grandeurs par leur nom, dans une chaîne (mr.valeurs("x"), mr.tableau()) :
    // un script qui s'en sert dépend de toutes les grandeurs
    const bool all = ids.contains(u"mevreg"_s);
    for (const auto &q : ws.doc.quantities) {
        if (q.origin != script && (all || ids.contains(q.name))) {
            ajoute(q.name.toUtf8());
            if (all)
                ajoute(q.unit.toUtf8());
            ajoute(QByteArrayView(reinterpret_cast<const char *>(q.values.data()), static_cast<qsizetype>(q.values.size() * sizeof(double))));
        }
    }
    for (const auto &p : ws.doc.parameters) {
        if (all || ids.contains(p.name)) {
            ajoute(p.name.toUtf8());
            if (all)
                ajoute(p.unit.toUtf8());
            ajoute(QByteArrayView(reinterpret_cast<const char *>(&p.value), sizeof(double)));
        }
    }
    return h.result();
}

void Store::definit_code_script(ViewId id, const QString &code)
{
    Script *s = ws.script_modifiable(id);
    if (!s || s->code == code)
        return;
    s->code = code;
    if (Script *snap = m_snapshot.script_modifiable(id))
        snap->code = code;
    marque_modifie(true);
    Q_EMIT code_script_change();
    if (s->linked)
        m_linkedTimer.start(700);
}

bool Store::est_en_cours(ViewId id) const
{
    auto it = runs.find(id);
    return it != runs.end() && it->second.running != 0;
}

void Store::lance_script(ViewId id)
{
    const Script *s = ws.script(id);
    if (!s || est_en_cours(id))
        return;
    python::ScriptInput input = entree_script(ws, id);
    input.folder = documentFolder;
    const std::uint64_t job = python::Engine::instance().lance(s->code, input);
    m_jobs[job] = id;
    ScriptRun &r = runs[id];
    r.running = job;
    r.inputHash = empreinte_entrees(ws, id);
    Q_EMIT etat_script_change(id);
}

void Store::arrete_script(ViewId id)
{
    auto it = runs.find(id);
    if (it != runs.end() && it->second.running)
        python::Engine::instance().interrompt(it->second.running);
}

void Store::lance_scripts_lies()
{
    // pas pendant un glisser : le script tourne une fois le curseur relâché… sauf s'il est lié,
    // auquel cas on le relance dès qu'il a fini, pour suivre le curseur en direct
    for (const auto &s : ws.scripts) {
        if (!s.linked || est_en_cours(s.id) || blockedScripts.count(s.id))
            continue;
        auto it = runs.find(s.id);
        if (it == runs.end() || it->second.inputHash != empreinte_entrees(ws, s.id))
            lance_script(s.id);
    }
}

void Store::apres_script_termine(std::uint64_t job, const python::ScriptOutput &out)
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
    const bool entreesIdentiques = r.inputHash == empreinte_entrees(ws, id);
    if (out.error || !ws.script(id) || !entreesIdentiques) {
        r.messages.clear();
        if (out.error)
            r.messages << u"Le document a conservé son état avant le script."_s;
        else if (!entreesIdentiques)
            r.messages << u"Les entrées ont changé pendant le calcul. Une nouvelle exécution utilisera les mesures actuelles."_s;
        Q_EMIT etat_script_change(id);
        if (ws.script(id) && ws.script(id)->linked)
            m_linkedTimer.start(60);
        return;
    }
    QStringList newDrawings;
    for (const auto &d : out.drawings) {
        if (!drawings.count(d.desc.name))
            newDrawings << d.desc.name;
        drawings[d.desc.name] = drawing::Drawing::fabrique(d.desc, d.data);
        // Deux scripts peuvent dessiner dans la même fenêtre. Son bouton doit
        // relancer celui qui a produit le dernier dessin affiché.
        for (auto &s : ws.scripts) {
            if (s.id != id)
                s.dessins.removeAll(d.desc.name);
            else if (!s.dessins.contains(d.desc.name))
                s.dessins << d.desc.name;
        }
    }
    const scripting::Applied applied = scripting::applique_sortie(ws, id, out);
    r.messages = applied.messages;
    r.inputHash = empreinte_entrees(ws, id);
    modifie();
    Q_EMIT etat_script_change(id);
    Q_EMIT script_applique(id, applied.openGraphs, applied.savefig, newDrawings, applied.actions);
    // les données ont pu changer pendant l'exécution : un script lié se relance
    if (ws.script(id) && ws.script(id)->linked)
        m_linkedTimer.start(60);
}
