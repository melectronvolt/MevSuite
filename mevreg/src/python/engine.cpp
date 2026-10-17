#include "python/native.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QStringList>

#include <algorithm>
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
import sys, io, builtins, traceback, importlib.abc, importlib.util
import _mevreg

class _Integres(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def find_spec(self, name, path=None, target=None):
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

def _pas_d_input(*args, **kwargs):
    raise RuntimeError("input() n'est pas disponible : utilisez plutôt un paramètre réglable par curseur")

def _capturer(g, depart):
    """Les listes de nombres et les tableaux numpy du script deviennent des grandeurs (sans unité,
    sauf si grandeur() en a donné une). Une grandeur du document changée dans le script ne l'est
    pas dans le document : un message le dit."""
    import json
    import numpy as np
    for name, value in list(g.items()):
        if name.startswith("_") or not name.isidentifier():
            continue
        if isinstance(value, np.ndarray):
            if value.dtype not in ("float64", "int64") or len(value) == 0:
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
            _mevreg._commande(json.dumps({"ordre": "avertir", "texte":
                "« %s » a changé dans le script, pas dans le document : pour changer la grandeur, grandeur(\"%s\", %s) ou mr.modifier" % (name, name, name)}))
            continue
        _mevreg._auto(name, value)

def _mevreg_run(code, quantities, parameters, folder):
    out = io.StringIO()
    old = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = out
    err = None
    plt = None
    try:
        try:
            import numpy as np
            import matplotlib.pyplot as plt
            plt._reset()
            g = {"__name__": "__main__", "__builtins__": builtins, "grandeur": _mevreg.grandeur, "input": _pas_d_input}
            arrays = {}
            for name, buf, unit, variable in quantities:
                a = np._wrap(buf)
                # un nom protégé (min, np…) n'est pas une variable du script, mais mr la connaît
                if variable:
                    g[name] = a
                arrays[name] = (a, unit)
            for name, value in parameters:
                g[name] = value
            # module mevreg : les grandeurs et paramètres du document, les ordres remis à zéro
            import mevreg
            mevreg._debut(arrays, dict(parameters), folder)
            try:
                exec(compile(code, "<script>", "exec"), g)
            finally:
                _capturer(g, arrays)
        except BaseException as e:
            err = (isinstance(e, KeyboardInterrupt), traceback.format_exc())
        try:
            if plt is not None:
                plt._finish()
        except BaseException as e:
            if err is None:
                err = (isinstance(e, KeyboardInterrupt), traceback.format_exc())
    finally:
        sys.stdout, sys.stderr = old
    return out.getvalue(), err
)PY";

std::atomic<std::uint64_t> g_interruptFor{0};
std::atomic<std::uint64_t> g_runningJob{0};

/// Appel différé exécuté par l'interpréteur entre deux instructions : interruption demandée.
int interruptCall(void *)
{
    if (g_interruptFor.load() != 0 && g_interruptFor.load() == g_runningJob.load()) {
        g_interruptFor = 0;
        PyErr_SetString(PyExc_KeyboardInterrupt, "script interrompu");
        return -1;
    }
    return 0;
}

QString pyStr(PyObject *o)
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
QString startInterpreter(PyObject **runner)
{
    PyImport_AppendInittab("_mevreg", &initModule);
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
    if (QCoreApplication::instance()) {
        const QString home = QCoreApplication::applicationDirPath() + u"/python"_s;
        const QString lib = home + u"/lib/python%1.%2"_s.arg(PY_MAJOR_VERSION).arg(PY_MINOR_VERSION);
        if (QFileInfo(lib).isDir()) {
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
    PyObject *r = PyRun_String(BOOTSTRAP, Py_file_input, globals, globals);
    if (!r) {
        PyObject *type, *value, *tb;
        PyErr_Fetch(&type, &value, &tb);
        const QString msg = pyStr(value);
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

ScriptOutput execute(PyObject *runner, std::uint64_t id, const QString &code, const ScriptInput &input)
{
    QElapsedTimer timer;
    timer.start();
    hostOutput() = HostOutput{};
    for (const auto &p : input.parameters)
        hostOutput().docParams.emplace_back(p.name, p.value);
    ScriptOutput out;
    PyObject *quantities = PyList_New(0);
    for (const auto &q : input.quantities) {
        // nom protégé : la grandeur reste lisible par mr.valeurs, sans masquer la fonction Python
        PyObject *buf = wrapBuf(arr::Arr::flt(q.values));
        PyObject *item = Py_BuildValue("(sNsN)", q.name.toUtf8().constData(), buf, q.unit.toUtf8().constData(),
                                       PyBool_FromLong(!PROTECTED.contains(q.name)));
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
    g_runningJob = id;
    const QByteArray folder = input.folder.toUtf8();
    PyObject *res = PyObject_CallFunction(runner, "s#NNs", src.constData(), static_cast<Py_ssize_t>(src.size()), quantities, params, folder.constData());
    g_runningJob = 0;
    if (!res) {
        // erreur hors du script (interruption pendant la mise en place…)
        PyObject *type, *value, *tb;
        PyErr_Fetch(&type, &value, &tb);
        const bool interrupted = type && PyErr_GivenExceptionMatches(type, PyExc_KeyboardInterrupt);
        ScriptError e;
        e.interrupted = interrupted;
        e.message = interrupted ? u"Script interrompu"_s : pyStr(value);
        e.traceback = e.message;
        out.error = e;
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(tb);
    } else {
        out.stdoutText = pyStr(PyTuple_GetItem(res, 0));
        PyObject *err = PyTuple_GetItem(res, 1);
        if (err && err != Py_None) {
            ScriptError e;
            e.interrupted = PyObject_IsTrue(PyTuple_GetItem(err, 0)) == 1;
            e.traceback = pyStr(PyTuple_GetItem(err, 1));
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
            e.line = scriptLine(e.traceback);
            out.error = e;
        }
        Py_DECREF(res);
    }
    HostOutput &host = hostOutput();
    out.quantities = std::move(host.quantities);
    out.given = std::move(host.given);
    out.figures = std::move(host.figures);
    out.drawings = std::move(host.drawings);
    out.commands = std::move(host.commands);
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
    m_thread = std::thread([this] { loop(); });
}

Engine::~Engine()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    // l'interpréteur peut être occupé par un script sans fin : on ne l'attend pas
    if (m_thread.joinable())
        m_thread.detach();
}

void Engine::loop()
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
                return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        if (!started) {
            started = true;
            startError = startInterpreter(&runner);
        }
        m_current = job.id;
        ScriptOutput out;
        if (!startError.isEmpty()) {
            ScriptError e;
            e.message = u"Python n'a pas pu démarrer : "_s + startError;
            e.traceback = e.message;
            out.error = e;
        } else {
            out = execute(runner, job.id, job.code, job.input);
        }
        m_current = 0;
        job.done(job.id, std::move(out));
    }
}

std::uint64_t Engine::enqueue(const QString &code, const ScriptInput &input, std::function<void(std::uint64_t, ScriptOutput)> done)
{
    const std::uint64_t id = m_next++;
    {
        std::lock_guard lock(m_mutex);
        m_jobs.push_back({id, code, input, std::move(done)});
    }
    m_cv.notify_one();
    return id;
}

std::uint64_t Engine::run(const QString &code, const ScriptInput &input)
{
    return enqueue(code, input, [this](std::uint64_t job, ScriptOutput out) {
        // résultat remis au fil de l'interface
        auto shared = std::make_shared<ScriptOutput>(std::move(out));
        QMetaObject::invokeMethod(this, [this, job, shared] { Q_EMIT finished(job, *shared); }, Qt::QueuedConnection);
    });
}

ScriptOutput Engine::runSync(const QString &code, const ScriptInput &input)
{
    auto promise = std::make_shared<std::promise<ScriptOutput>>();
    auto future = promise->get_future();
    enqueue(code, input, [promise](std::uint64_t, ScriptOutput out) { promise->set_value(std::move(out)); });
    return future.get();
}

void Engine::interrupt(std::uint64_t id)
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
        }
    }
    if (queued) {
        ScriptOutput out;
        ScriptError e;
        e.message = u"Script interrompu"_s;
        e.interrupted = true;
        out.error = e;
        queued->done(queued->id, std::move(out));
        return;
    }
    if (m_current.load() != id)
        return;
    g_interruptFor = id;
    // sans le verrou global : l'appel est exécuté par l'interpréteur à la prochaine instruction
    Py_AddPendingCall(&interruptCall, nullptr);
}

QStringList identifiers(const QString &code)
{
    QStringList out;
    const int n = static_cast<int>(code.size());
    int i = 0;
    while (i < n) {
        const QChar c = code[i];
        // commentaires et chaînes ignorés
        if (c == u'#') {
            while (i < n && code[i] != u'\n')
                ++i;
            continue;
        }
        if (c == u'"' || c == u'\'') {
            const bool triple = i + 2 < n && code[i + 1] == c && code[i + 2] == c;
            if (triple) {
                i += 3;
                while (i + 2 < n && !(code[i] == c && code[i + 1] == c && code[i + 2] == c))
                    ++i;
                i += 3;
            } else {
                ++i;
                while (i < n && code[i] != c && code[i] != u'\n') {
                    if (code[i] == u'\\')
                        ++i;
                    ++i;
                }
                ++i;
            }
            continue;
        }
        if (c.isLetter() || c == u'_') {
            const int start = i;
            while (i < n && (code[i].isLetterOrNumber() || code[i] == u'_'))
                ++i;
            const bool prevDot = start > 0 && code[start - 1] == u'.';
            const QString word = code.mid(start, i - start);
            if (!prevDot && !out.contains(word))
                out << word;
            continue;
        }
        ++i;
    }
    return out;
}

std::optional<int> scriptLine(const QString &traceback)
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
