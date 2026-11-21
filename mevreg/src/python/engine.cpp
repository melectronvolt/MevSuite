#include "python/native.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <future>
#include <optional>

using namespace Qt::StringLiterals;

namespace python {

namespace {

/// Noms qu'on n'injecte pas dans le script : ils masqueraient des fonctions Python.
const QStringList PROTECTED = {
    u"print"_s, u"range"_s, u"len"_s, u"list"_s, u"dict"_s, u"set"_s, u"tuple"_s, u"str"_s, u"int"_s, u"float"_s,
    u"bool"_s, u"complex"_s, u"abs"_s, u"min"_s, u"max"_s, u"sum"_s, u"round"_s, u"input"_s, u"open"_s, u"type"_s,
    u"id"_s, u"map"_s, u"filter"_s, u"zip"_s, u"sorted"_s, u"reversed"_s, u"enumerate"_s, u"iter"_s, u"next"_s,
    u"format"_s, u"repr"_s, u"hash"_s, u"help"_s, u"dir"_s, u"vars"_s, u"object"_s, u"super"_s, u"isinstance"_s,
    u"pow"_s, u"divmod"_s, u"np"_s, u"plt"_s, u"numpy"_s, u"math"_s, u"grandeur"_s, u"mevreg"_s, u"mr"_s, u"exec"_s, u"eval"_s,
    u"compile"_s, u"globals"_s, u"locals"_s, u"chr"_s, u"ord"_s, u"all"_s, u"any"_s, u"slice"_s, u"property"_s,
    u"staticmethod"_s, u"classmethod"_s, u"None"_s, u"True"_s, u"False"_s,
};

/// Chargement des modules intégrés (numpy, matplotlib, fenetre) depuis les ressources du
/// programme, avant tout autre emplacement, et exécution des scripts.
constexpr char BOOTSTRAP[] = R"PY(
import sys, os, io, builtins, traceback, importlib.abc, importlib.util
import _mevreg

class _Integres(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def find_spec(self, name, path=None, target=None):
        if name == "numpy":
            return importlib.util.spec_from_file_location(name, os.path.join(_numpy_dir, "__init__.py"),
                                                          submodule_search_locations=[_numpy_dir])
        src = _mevreg._source(name)
        if src is None:
            return None
        return importlib.util.spec_from_loader(name, self, is_package=src[1])

    def create_module(self, spec):
        return None

    def exec_module(self, module):
        src = _mevreg._source(module.__name__)
        exec(compile(src[0], module.__name__.replace(".", "/") + ".py", "exec"), module.__dict__)

sys.meta_path.insert(0, _Integres())
if os.name == "nt" and _dll_dir:
    _dll_handle = os.add_dll_directory(_dll_dir)
import numpy as np
# Les deux orthographes se rencontrent dans les scripts de TP (NumPy 1.x et 2.x).
if not hasattr(np, "trapezoid"):
    np.trapezoid = np.trapz
if not hasattr(np, "trapz"):
    np.trapz = np.trapezoid

def _pas_d_input(*args, **kwargs):
    raise RuntimeError("input() n'est pas disponible : utilisez plutôt un paramètre réglable par curseur")

def _capturer(g, depart, precedentes):
    """Les listes de nombres et les tableaux numpy du script deviennent des grandeurs (sans unité,
    sauf si grandeur() en a donné une). Une grandeur du document changée dans le script ne l'est
    pas dans le document : un message le dit."""
    import json
    import numpy as np
    for name, value in list(g.items()):
        if name.startswith("_") or not name.isidentifier():
            continue
        pd = sys.modules.get("pandas")
        if pd is not None and isinstance(value, pd.Series):
            value = value.to_numpy()
        if isinstance(value, np.ndarray):
            if value.ndim != 1 or value.dtype.kind not in "fiu" or value.size == 0:
                continue
        elif not (type(value) is list and value and all(type(v) in (int, float) for v in value)):
            continue
        if name in depart:
            avant = depart[name][0]
            if value is avant:
                continue
            a = [float(v) for v in value]
            b = avant.tolist()
            if len(a) == len(b) and all(x == y or (x != x and y != y) for x, y in zip(a, b)):
                continue
            if name in precedentes:
                _mevreg._auto(name, value)
                continue
            if name in _mevreg._document("grandeurs") and np.array_equal(
                    value, _mevreg._document("valeurs", name), equal_nan=True):
                continue
            _mevreg._commande(json.dumps({"ordre": "avertir", "texte":
                "« %s » a changé dans le script, pas dans le document : pour changer la grandeur, grandeur(\"%s\", %s) ou mr.modifier" % (name, name, name)}))
            continue
        _mevreg._auto(name, value)

def _mevreg_run(code, grandeurs, parametres, dossier):
    sortie = io.StringIO()
    flux_initiaux = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = sortie
    erreur = None
    plt = None
    try:
        import numpy as np
        import matplotlib.pyplot as plt
        import mevreg
        plt._reset()
        variables = {"__name__": "__main__", "__builtins__": builtins, "grandeur": mevreg.creer, "input": _pas_d_input}
        valeurs_initiales = {}
        precedentes = set()
        for nom, valeurs, unite, variable, produite in grandeurs:
            if produite:
                precedentes.add(nom)
            tableau = np.asarray(valeurs, dtype=float)
            # Un nom protégé (min, np…) reste accessible par le module mevreg.
            if variable:
                variables[nom] = tableau
            valeurs_initiales[nom] = (tableau.copy(), unite)
        for nom, valeur in parametres:
            variables[nom] = valeur
        mevreg._debut(dossier, precedentes)
        exec(compile(code, "<script>", "exec"), variables)
        _capturer(variables, valeurs_initiales, precedentes)
        plt._envoyer_figures()
    except BaseException as exc:
        erreur = (isinstance(exc, KeyboardInterrupt), traceback.format_exc())
    finally:
        sys.stdout, sys.stderr = flux_initiaux
        if plt is not None:
            plt._reset()
    return sortie.getvalue(), erreur
)PY";

std::atomic<std::uint64_t> g_interruptFor{0};
std::atomic<std::uint64_t> g_runningJob{0};

ScriptOutput script_interrompu()
{
    ScriptOutput out;
    ScriptError erreur;
    erreur.interrupted = true;
    erreur.message = u"Script interrompu"_s;
    out.error = std::move(erreur);
    return out;
}

/// Appel différé exécuté par l'interpréteur entre deux instructions : interruption demandée.
int appel_interruption(void *)
{
    if (g_interruptFor.load() != 0 && g_interruptFor.load() == g_runningJob.load()) {
        g_interruptFor = 0;
        PyErr_SetString(PyExc_KeyboardInterrupt, "script interrompu");
        return -1;
    }
    return 0;
}

QString texte_python(PyObject *o)
{
    if (!o)
        return {};
    PyObject *s = PyObject_Str(o);
    if (!s) {
        PyErr_Clear();
        return {};
    }
    Py_ssize_t n = 0;
    const char *u = PyUnicode_AsUTF8AndSize(s, &n);
    QString r = u ? QString::fromUtf8(u, n) : QString();
    Py_DECREF(s);
    return r;
}

/// Démarre l'interpréteur ; message d'erreur, vide si réussi.
QString demarre_interpreteur(PyObject **runner)
{
    PyImport_AppendInittab("_mevreg", &initialise_module);
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.site_import = 0;
    config.user_site_directory = 0;
    config.write_bytecode = 0;
    config.install_signal_handlers = 0;
    config.parse_argv = 0;
    config.buffered_stdio = 0;
    PyConfig_SetString(&config, &config.program_name, L"mevreg");
    // versions portables (Windows, AppImage) : bibliothèque standard livrée à côté du programme,
    // dans python/lib/python3.X ; ses chemins sont donnés tels quels, sans dépendre de la façon dont
    // chaque Python (MSYS2, Linux) les déduit de `home`
    QString numpyDir = QString::fromUtf8(MEVREG_NUMPY_DIR);
    QString dllDir;
#ifdef MEVREG_PYTHON_BIN
    // En développement les extensions de MSYS2 trouvent leurs DLL dans le bin de l’interpréteur.
    dllDir = QString::fromUtf8(MEVREG_PYTHON_BIN);
#endif
    if (QCoreApplication::instance()) {
        const QString home = QCoreApplication::applicationDirPath() + u"/python"_s;
        const QString lib = home + u"/lib/python%1.%2"_s.arg(PY_MAJOR_VERSION).arg(PY_MINOR_VERSION);
        if (QFileInfo(home).isDir()) {
            numpyDir = home + u"/packages/numpy"_s;
            dllDir = QCoreApplication::applicationDirPath();
            PyConfig_SetString(&config, &config.home, QDir::toNativeSeparators(home).toStdWString().c_str());
            config.module_search_paths_set = 1;
            for (const QString &dir : {lib, lib + u"/lib-dynload"_s})
                PyWideStringList_Append(&config.module_search_paths, QDir::toNativeSeparators(dir).toStdWString().c_str());
        }
    }
    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status))
        return QString::fromUtf8(status.err_msg ? status.err_msg : "erreur inconnue");
    PyObject *main = PyImport_AddModule("__main__");
    PyObject *globals = PyModule_GetDict(main);
    for (const auto &[key, value] : {std::pair{"_numpy_dir", numpyDir}, std::pair{"_dll_dir", dllDir}}) {
        PyObject *path = PyUnicode_FromWideChar(value.toStdWString().c_str(), -1);
        if (!path || PyDict_SetItemString(globals, key, path) < 0) {
            Py_XDECREF(path);
            return u"chemin du moteur Python illisible"_s;
        }
        Py_DECREF(path);
    }
    PyObject *r = PyRun_String(BOOTSTRAP, Py_file_input, globals, globals);
    if (!r) {
        PyObject *type, *value, *tb;
        PyErr_Fetch(&type, &value, &tb);
        const QString msg = texte_python(value);
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(tb);
        return u"démarrage du module intégré impossible : "_s + msg;
    }
    Py_DECREF(r);
    *runner = PyDict_GetItemString(globals, "_mevreg_run");
    Py_XINCREF(*runner);
    return *runner ? QString() : u"fonction d'exécution introuvable"_s;
}

ScriptOutput execute(PyObject *runner, const QString &code, const ScriptInput &input)
{
    QElapsedTimer timer;
    timer.start();
    sortie_hote() = HostOutput{};
    auto &doc = sortie_hote().document;
    for (const auto &p : input.parameters)
        doc.ajoute_parametre(p.name, p.unit, p.value);
    for (const auto &q : input.quantities)
        doc.ajoute_experimentale(q.name, q.unit, q.values);
    for (const auto &[name, formula] : input.formulas.asKeyValueRange())
        if (const auto *q = doc.par_nom(name)) doc.definit_formule(q->id, formula);
    for (const auto &[name, uncertainty] : input.uncertainties.asKeyValueRange()) {
        const auto *q = doc.par_nom(name), *u = doc.par_nom(uncertainty);
        if (q && u) doc.definit_incertitude(q->id, u->id);
    }
    ScriptOutput out;
    PyObject *quantities = PyList_New(0);
    for (const auto &q : input.quantities) {
        // nom protégé : la grandeur reste lisible par mr.valeurs, sans masquer la fonction Python
        PyObject *buf = liste_reels(q.values);
        PyObject *item = Py_BuildValue("(sNsNN)", q.name.toUtf8().constData(), buf, q.unit.toUtf8().constData(),
                                       PyBool_FromLong(!PROTECTED.contains(q.name)), PyBool_FromLong(input.previousOutputs.contains(q.name)));
        PyList_Append(quantities, item);
        Py_DECREF(item);
    }
    PyObject *params = PyList_New(0);
    for (const auto &p : input.parameters) {
        if (PROTECTED.contains(p.name))
            continue;
        PyObject *item = Py_BuildValue("(sd)", p.name.toUtf8().constData(), p.value);
        PyList_Append(params, item);
        Py_DECREF(item);
    }
    const QByteArray src = code.toUtf8();
    const QByteArray folder = input.folder.toUtf8();
    PyObject *res = PyObject_CallFunction(runner, "s#NNs", src.constData(), static_cast<Py_ssize_t>(src.size()), quantities, params, folder.constData());
    if (!res) {
        // erreur hors du script (interruption pendant la mise en place…)
        PyObject *type, *value, *tb;
        PyErr_Fetch(&type, &value, &tb);
        const bool interrupted = type && PyErr_GivenExceptionMatches(type, PyExc_KeyboardInterrupt);
        ScriptError e;
        e.interrupted = interrupted;
        e.message = interrupted ? u"Script interrompu"_s : texte_python(value);
        e.traceback = e.message;
        out.error = e;
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(tb);
    } else {
        out.stdoutText = texte_python(PyTuple_GetItem(res, 0));
        PyObject *err = PyTuple_GetItem(res, 1);
        if (err && err != Py_None) {
            ScriptError e;
            e.interrupted = PyObject_IsTrue(PyTuple_GetItem(err, 0)) == 1;
            e.traceback = texte_python(PyTuple_GetItem(err, 1));
            if (e.interrupted) {
                e.message = u"Script interrompu"_s;
            } else {
                const QStringList lines = e.traceback.split(u'\n');
                for (qsizetype k = lines.size(); k-- > 0;) {
                    if (!lines[k].trimmed().isEmpty()) {
                        e.message = lines[k].trimmed();
                        break;
                    }
                }
            }
            e.line = ligne_script(e.traceback);
            out.error = e;
        }
        Py_DECREF(res);
    }
    HostOutput &host = sortie_hote();
    if (!out.error) {
        out.quantities = std::move(host.quantities);
        out.given = std::move(host.given);
        out.figures = std::move(host.figures);
        out.drawings = std::move(host.drawings);
        out.commands = std::move(host.commands);
    }
    // Une figure et ses axes se référencent mutuellement, de même qu'une fonction et
    // son dictionnaire de variables globales. Une fois l'appel Python terminé, le
    // ramasse-miettes peut libérer leurs tableaux avant de rendre la main à l'interface.
    PyGC_Collect();
    host = HostOutput{};
    out.durationMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    return out;
}

} // namespace

Engine &Engine::instance()
{
    static Engine e;
    return e;
}

Engine::Engine()
{
    // Les modules Python peuvent encore utiliser la sortie hôte pendant leur finalisation.
    sortie_hote();
    m_thread = std::thread([this] { boucle(); });
}

Engine::~Engine()
{
    demande_arret();
    m_thread.join();
}

void Engine::demande_arret()
{
    std::deque<Job> annules;
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
        annules.swap(m_jobs);
        if (const auto id = m_current.load()) {
            g_interruptFor = id;
            if (g_runningJob == id)
                Py_AddPendingCall(&appel_interruption, nullptr);
        }
    }
    m_cv.notify_all();
    for (auto &job : annules)
        job.done(job.id, script_interrompu());
}

bool Engine::arrete_avant_sortie()
{
    demande_arret();
    std::unique_lock lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::seconds(3), [this] { return m_termine; });
}

void Engine::boucle()
{
    PyObject *runner = nullptr;
    QString startError;
    bool started = false;
    while (true) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_stop)
                break;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            m_current = job.id;
        }
        if (!started) {
            started = true;
            startError = demarre_interpreteur(&runner);
        }
        bool annule;
        {
            std::lock_guard lock(m_mutex);
            annule = m_stop || g_interruptFor == job.id;
            if (!annule && startError.isEmpty())
                g_runningJob = job.id;
        }
        ScriptOutput out;
        if (annule) {
            out = script_interrompu();
        } else if (!startError.isEmpty()) {
            ScriptError e;
            e.message = u"Python n'a pas pu démarrer : "_s + startError;
            e.traceback = e.message;
            out.error = e;
        } else {
            out = execute(runner, job.code, job.input);
        }
        {
            std::lock_guard lock(m_mutex);
            g_runningJob = 0;
            m_current = 0;
        }
        job.done(job.id, std::move(out));
    }
    if (started && Py_IsInitialized()) {
        Py_XDECREF(runner);
        Py_FinalizeEx();
    }
    {
        std::lock_guard lock(m_mutex);
        m_termine = true;
    }
    m_cv.notify_all();
}

std::uint64_t Engine::met_en_file(const QString &code, const ScriptInput &input, std::function<void(std::uint64_t, ScriptOutput)> done)
{
    const std::uint64_t id = m_next++;
    {
        std::lock_guard lock(m_mutex);
        if (!m_stop) {
            m_jobs.push_back({id, code, input, std::move(done)});
            m_cv.notify_one();
            return id;
        }
    }
    done(id, script_interrompu());
    return id;
}

std::uint64_t Engine::lance(const QString &code, const ScriptInput &input)
{
    return met_en_file(code, input, [this](std::uint64_t job, ScriptOutput out) {
        // résultat remis au fil de l'interface
        auto shared = std::make_shared<ScriptOutput>(std::move(out));
        QMetaObject::invokeMethod(this, [this, job, shared] { Q_EMIT fini(job, *shared); }, Qt::QueuedConnection);
    });
}

ScriptOutput Engine::lance_synchrone(const QString &code, const ScriptInput &input)
{
    auto promise = std::make_shared<std::promise<ScriptOutput>>();
    auto future = promise->get_future();
    met_en_file(code, input, [promise](std::uint64_t, ScriptOutput out) { promise->set_value(std::move(out)); });
    return future.get();
}

void Engine::interrompt(std::uint64_t id)
{
    if (id == 0)
        return;
    // script qui attend son tour : retiré de la file, terminé comme interrompu
    std::optional<Job> queued;
    {
        std::lock_guard lock(m_mutex);
        const auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [id](const Job &j) { return j.id == id; });
        if (it != m_jobs.end()) {
            queued = std::move(*it);
            m_jobs.erase(it);
        } else if (m_current == id) {
            g_interruptFor = id;
            if (g_runningJob == id)
                Py_AddPendingCall(&appel_interruption, nullptr);
        }
    }
    if (queued)
        queued->done(queued->id, script_interrompu());
}

std::optional<int> ligne_script(const QString &traceback)
{
    std::optional<int> line;
    for (const QString &l : traceback.split(u'\n')) {
        const QString t = l.trimmed();
        const QString prefix = u"File \"<script>\", line "_s;
        if (t.startsWith(prefix)) {
            QString num;
            for (QChar c : t.mid(prefix.size())) {
                if (!c.isDigit())
                    break;
                num += c;
            }
            bool ok = false;
            const int v = num.toInt(&ok);
            if (ok)
                line = v;
        }
    }
    return line;
}

} // namespace python
