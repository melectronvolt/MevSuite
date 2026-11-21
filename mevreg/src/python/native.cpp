#include "python/native.h"

#include "core/csv.h"
#include "core/document.h"
#include "core/drawing.h"
#include "core/fit.h"
#include "core/plotspec.h"
#include "plot/resolve.h"
#include "core/expr.h"
#include "core/resources.h"
#include "core/numeric.h"
#include <cstring>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;

namespace python {

HostOutput &sortie_hote()
{
    static HostOutput out;
    return out;
}

PyObject *liste_reels(const std::vector<double> &v)
{
    PyObject *list = PyList_New(static_cast<Py_ssize_t>(v.size()));
    if (!list)
        return nullptr;
    for (size_t i = 0; i < v.size(); ++i) {
        PyObject *value = PyFloat_FromDouble(v[i]);
        if (!value) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), value);
    }
    return list;
}

QString modifie_donnees(Document &doc, const QJsonObject &o)
{
    const QString order = o.value(u"ordre"_s).toString(), name = o.value(u"nom"_s).toString();
    const QString unit = o.value(u"unite"_s).toString(), formula = o.value(u"formule"_s).toString();
    const Grandeur *q = doc.par_nom(name);
    if (order == u"parametre") {
        const double value = o.value(u"valeur"_s).toDouble(NAN);
        if (!std::isfinite(value))
            return u"Le paramètre doit être un nombre fini."_s;
        if (const Parametre *p = doc.parametre_par_nom(name)) {
            const auto id = p->id;
            doc.definit_valeur_parametre(id, value);
            if (!unit.isEmpty())
                doc.parametre_modifiable(id)->unit = unit;
            return {};
        }
        return doc.ajoute_parametre(name, unit, value).error;
    }
    if (order == u"calculer" || order == u"incertitude") {
        // Valider la formule sur une copie permet au script de rattraper une erreur et de continuer.
        Document next = doc;
        QId id = 0;
        if (order == u"calculer" && !q) {
            const auto added = next.ajoute_calculee(name, unit, formula);
            if (!added) return added.error;
            id = *added;
        } else if (order == u"incertitude" && q && !q->uncertainty) {
            const auto added = next.cree_incertitude(q->id, formula);
            if (!added) return added.error;
            id = *added;
        } else if (q) {
            id = order == u"calculer" ? q->id : *q->uncertainty;
            if (const auto e = next.definit_formule(id, formula); !e.isEmpty()) return e;
            if (!unit.isEmpty()) next.definit_unite(id, unit);
        } else {
            return u"Grandeur introuvable : %1"_s.arg(name);
        }
        if (const auto *result = next.obtient(id); result && !result->error.isEmpty())
            return result->error;
        doc = std::move(next);
    } else if (order == u"unite" || order == u"renommer" || order == u"supprimer") {
        if (!q) return u"Grandeur introuvable : %1"_s.arg(name);
        if (order == u"renommer")
            return doc.renomme_grandeur(q->id, o.value(u"nouveau"_s).toString()).error;
        if (order == u"supprimer")
            doc.retire_grandeur(q->id);
        else
            doc.definit_unite(q->id, unit);
    }
    return {};
}

namespace {
QString en_qstring(PyObject *s)
{
    Py_ssize_t n = 0;
    const char *u = PyUnicode_AsUTF8AndSize(s, &n);
    return u ? QString::fromUtf8(u, n) : QString();
}

PyObject *depuis_qstring(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return PyUnicode_FromStringAndSize(b.constData(), b.size());
}

// Les colonnes issues de NumPy sont contiguës et en double précision.
bool lit_reels(PyObject *o, std::vector<double> &out)
{
    Py_buffer view{};
    if (PyObject_CheckBuffer(o) && PyObject_GetBuffer(o, &view, PyBUF_FORMAT | PyBUF_STRIDES) == 0) {
        const bool direct = view.ndim == 1 && view.format && std::strcmp(view.format, "d") == 0 && PyBuffer_IsContiguous(&view, 'C');
        if (direct) {
            out.resize(static_cast<size_t>(view.len) / sizeof(double));
            if (!out.empty())
                std::memcpy(out.data(), view.buf, static_cast<size_t>(view.len));
        }
        PyBuffer_Release(&view);
        if (direct)
            return true;
    }
    if (PyErr_Occurred()) return false;
    PyObject *seq = PySequence_Fast(o, "une colonne de nombres est attendue");
    if (!seq) return false;
    out.resize(static_cast<size_t>(PySequence_Fast_GET_SIZE(seq)));
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = PyFloat_AsDouble(PySequence_Fast_GET_ITEM(seq, static_cast<Py_ssize_t>(i)));
        if (PyErr_Occurred()) break;
    }
    Py_DECREF(seq);
    return !PyErr_Occurred();
}

PyObject *erreur_python(PyObject *type, const QString &message)
{
    PyErr_SetString(type, message.toUtf8().constData());
    return nullptr;
}

PyObject *lit_document(PyObject *, PyObject *args)
{
    const char *op = nullptr;
    PyObject *name = nullptr;
    if (!PyArg_ParseTuple(args, "s|U", &op, &name)) return nullptr;
    const auto &doc = sortie_hote().document;
    if (!std::strcmp(op, "parametres")) {
        PyObject *p = PyDict_New();
        for (const auto &param : doc.parameters) {
            PyObject *v = PyFloat_FromDouble(param.value);
            PyDict_SetItemString(p, param.name.toUtf8().constData(), v);
            Py_DECREF(v);
        }
        return p;
    }
    if (!name) {
        PyObject *names = PyList_New(static_cast<Py_ssize_t>(doc.quantities.size()));
        for (size_t i = 0; i < doc.quantities.size(); ++i)
            PyList_SET_ITEM(names, static_cast<Py_ssize_t>(i), depuis_qstring(doc.quantities[i].name));
        return names;
    }
    if (!std::strcmp(op, "verifie")) {
        const QString n = en_qstring(name);
        const Grandeur *q = doc.par_nom(n);
        const QString e = q && q->est_calculee() ? u"Grandeur calculée : %1"_s.arg(n)
                                                 : doc.verifie_nom(n, q ? std::optional<QId>(q->id) : std::nullopt);
        if (!e.isEmpty()) return erreur_python(PyExc_ValueError, e);
        Py_RETURN_NONE;
    }
    if (const auto *q = doc.par_nom(en_qstring(name)))
        return !std::strcmp(op, "unite") ? depuis_qstring(q->unit) : liste_reels(q->values);
    return erreur_python(PyExc_KeyError, u"Pas de grandeur « %1 »"_s.arg(en_qstring(name)));
}

/// `grandeur("v", v, "m/s")` : crée ou remplace une grandeur du document.
PyObject *grandeur(PyObject *, PyObject *args)
{
    PyObject *nameO = nullptr, *values = nullptr;
    const char *unit = "";
    if (!PyArg_ParseTuple(args, "UO|s", &nameO, &values, &unit))
        return nullptr;
    const QString name = en_qstring(nameO);
    if (!expr::est_nom_valide(name) || expr::est_nom_fonction(name))
        return erreur_python(PyExc_ValueError, u"« %1 » n’est pas un nom de grandeur valable"_s.arg(name));
    std::vector<double> column;
    if (!lit_reels(values, column)) return nullptr;
    HostOutput &host = sortie_hote();
    auto &doc = host.document;
    QString e;
    if (const auto *old = doc.par_nom(name)) {
        const auto id = old->id;
        e = doc.definit_valeurs(id, column);
        if (e.isEmpty() && *unit) doc.definit_unite(id, QString::fromUtf8(unit));
    } else {
        e = doc.ajoute_experimentale(name, QString::fromUtf8(unit), column).error;
    }
    if (!e.isEmpty()) {
        PyErr_SetString(PyExc_ValueError, e.toUtf8().constData());
        return nullptr;
    }
    auto &q = host.quantities;
    std::erase_if(q, [&](const NamedValues &v) { return v.name == name; });
    q.push_back({name, QString::fromUtf8(unit), std::move(column)});
    // repère dans la suite des ordres : la grandeur est mise dans le document à cette place, avec les
    // valeurs de cet appel (le même nom peut être repris plus loin, après un renommage)
    host.given.push_back(q.back());
    host.commands.append(QJsonObject{
        {u"ordre"_s, u"grandeur"_s}, {u"nom"_s, name}, {u"indice"_s, static_cast<qint64>(host.given.size() - 1)}});
    Py_RETURN_NONE;
}

/// `_commande('{"ordre": "graphe", …}')` : ordre du module `mevreg`, appliqué après le script.
PyObject *commande(PyObject *, PyObject *args)
{
    const char *json = nullptr;
    if (!PyArg_ParseTuple(args, "s", &json))
        return nullptr;
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray(json), &err);
    if (!d.isObject()) {
        PyErr_SetString(PyExc_ValueError, "ordre illisible");
        return nullptr;
    }
    if (const QString e = modifie_donnees(sortie_hote().document, d.object()); !e.isEmpty()) {
        PyErr_SetString(PyExc_ValueError, e.toUtf8().constData());
        return nullptr;
    }
    sortie_hote().commands.append(d.object());
    Py_RETURN_NONE;
}

/// `_ajuster(type, degré, expression, nom_x, x, nom_y, y, plage)` : modélisation faite tout de
/// suite, comme dans la fenêtre Modélisation ; (noms, valeurs, incertitudes, R², n, équation,
/// erreur, noms des paramètres fixés).
PyObject *ajuster(PyObject *, PyObject *args)
{
    const char *type = nullptr, *expression = nullptr, *xname = nullptr, *yname = nullptr;
    int degree = 3;
    PyObject *range = nullptr;
    if (!PyArg_ParseTuple(args, "sisssO", &type, &degree, &expression, &xname, &yname, &range))
        return nullptr;
    fit::ModelKind kind;
    const auto t = fit::ModelKind::type_depuis_nom(QString::fromUtf8(type));
    if (!t) {
        return erreur_python(PyExc_ValueError, u"Modèle inconnu : %1"_s.arg(QString::fromUtf8(type)));
    }
    kind.type = *t;
    kind.degree = std::clamp(degree, 1, 6);
    kind.expression = QString::fromUtf8(expression);
    const Document &doc = sortie_hote().document;
    const auto *x = doc.par_nom(QString::fromUtf8(xname)), *y = doc.par_nom(QString::fromUtf8(yname));
    if (!x || !y)
        return PyErr_Format(PyExc_KeyError, "grandeur inconnue");
    FitSpec spec = FitSpec::fabrique(kind, x->id, y->id, PALETTE[3]);
    if (range && range != Py_None) {
        double a = 0, b = 0;
        if (!PyArg_ParseTuple(range, "dd", &a, &b))
            return nullptr;
        spec.range = std::minmax(a, b);
    }
    calcule_modele(spec, doc);
    PyObject *names = PyList_New(0), *values = PyList_New(0), *uncs = PyList_New(0);
    double r2 = NAN;
    long long n = 0;
    QString equation;
    if (spec.result) {
        for (qsizetype k = 0; k < spec.paramNames.size(); ++k) {
            PyObject *nm = depuis_qstring(spec.paramNames[k]);
            PyList_Append(names, nm);
            Py_DECREF(nm);
            PyObject *v = PyFloat_FromDouble(static_cast<size_t>(k) < spec.result->params.size() ? spec.result->params[static_cast<size_t>(k)] : NAN);
            PyList_Append(values, v);
            Py_DECREF(v);
            PyObject *u = PyFloat_FromDouble(static_cast<size_t>(k) < spec.result->uncertainties.size() ? spec.result->uncertainties[static_cast<size_t>(k)] : NAN);
            PyList_Append(uncs, u);
            Py_DECREF(u);
        }
        r2 = spec.result->r2;
        n = static_cast<long long>(spec.result->n);
        if (auto model = fit::Model::fabrique(kind, QString::fromUtf8(xname), doc.valeurs_parametres()))
            equation = model->affichage(spec.result->params, QString::fromUtf8(xname), QString::fromUtf8(yname), NumberStyle{false});
    }
    PyObject *fixedNames = PyList_New(0);
    for (const auto &[name, value] : spec.fixed) {
        PyObject *nm = depuis_qstring(name);
        PyList_Append(fixedNames, nm);
        Py_DECREF(nm);
    }
    PyObject *eq = depuis_qstring(equation), *err = depuis_qstring(spec.resultError);
    return Py_BuildValue("(NNNdLNNN)", names, values, uncs, r2, n, eq, err, fixedNames);
}

/// `_lire_csv(chemin, sep, decimal)` : fichier de données lu comme par l'import de MeVReg
/// (séparateurs devinés quand ils sont vides) ; (en-têtes, unités, colonnes).
PyObject *lire_csv(PyObject *, PyObject *args)
{
    PyObject *pathO = nullptr, *contents = Py_None;
    const char *sep = "", *decimal = "";
    int header = -2;
    if (!PyArg_ParseTuple(args, "U|ssiO", &pathO, &sep, &decimal, &header, &contents))
        return nullptr;
    QString text;
    if (contents == Py_None) {
        QFile f(en_qstring(pathO));
        if (!f.open(QIODevice::ReadOnly))
            return erreur_python(PyExc_FileNotFoundError, u"Fichier introuvable ou illisible : %1"_s.arg(en_qstring(pathO)));
        text = csv::decode_texte(f.readAll());
    } else if (PyUnicode_Check(contents)) {
        text = en_qstring(contents);
    } else {
        return PyErr_Format(PyExc_TypeError, "texte CSV attendu");
    }
    const QStringList lines = csv::lignes_texte(text);
    if (lines.isEmpty()) {
        PyErr_Format(PyExc_ValueError, "fichier vide : %U", pathO);
        return nullptr;
    }
    csv::ReadOptions o = csv::detecte(lines);
    const QString s = QString::fromUtf8(sep);
    if (s == u" " || s == u"\\s+")
        o.separator = csv::SPACES;
    else if (!s.isEmpty())
        o.separator = s.at(0);
    if (const QString d = QString::fromUtf8(decimal); !d.isEmpty())
        o.decimal = d == u"," ? csv::Decimal::Comma : csv::Decimal::Point;
    if (o.separator == u',')
        o.decimal = csv::Decimal::Point;
    // la détection dépend du séparateur décimal : refaite avec les séparateurs imposés
    csv::ReadOptions again = [&] {
        csv::ReadOptions d = csv::detecte(lines, o.decimal);
        d.separator = o.separator;
        d.decimal = o.decimal;
        return d;
    }();
    if (header >= lines.size())
        return erreur_python(PyExc_ValueError, u"Ligne d’en-tête absente"_s);
    if (header >= -1) {
        again.nameLine = header;
        again.firstLine = header + 1;
        again.unitLine = -1;
    }
    const csv::Reading r = csv::lit(lines, again);
    const QStringList raw = again.nameLine >= 0 ? csv::decoupe_ligne(lines[again.nameLine], again.separator) : QStringList();
    PyObject *headers = PyList_New(0), *units = PyList_New(0), *cols = PyList_New(0);
    for (size_t j = 0; j < r.columns.size(); ++j) {
        const QString h = raw.value(static_cast<qsizetype>(j)).isEmpty() ? QString::number(j) : raw.value(static_cast<qsizetype>(j));
        PyObject *ho = depuis_qstring(h), *uo = depuis_qstring(r.columns[j].unit);
        PyList_Append(headers, ho);
        PyList_Append(units, uo);
        Py_DECREF(ho);
        Py_DECREF(uo);
        // toutes les colonnes à la longueur des données (cases vides : NaN)
        std::vector<double> v = r.columns[j].values;
        v.resize(r.rows.size(), NAN);
        PyObject *b = liste_reels(v);
        PyList_Append(cols, b);
        Py_DECREF(b);
    }
    return Py_BuildValue("(NNN)", headers, units, cols);
}

/// `_auto(nom, valeurs)` : liste de nombres ou tableau du script, devenu grandeur sans unité ;
/// ignoré si le script l'a déjà donnée par `grandeur()` (avec son unité) ou si le nom ne convient pas.
PyObject *grandeur_auto(PyObject *, PyObject *args)
{
    PyObject *nameO = nullptr, *values = nullptr;
    if (!PyArg_ParseTuple(args, "UO", &nameO, &values))
        return nullptr;
    const QString name = en_qstring(nameO);
    auto &q = sortie_hote().quantities;
    if (!expr::est_nom_valide(name) || expr::est_nom_fonction(name) || std::any_of(q.begin(), q.end(), [&](const NamedValues &v) { return v.name == name; }))
        Py_RETURN_NONE;
    std::vector<double> column;
    if (!lit_reels(values, column)) return nullptr;
    q.push_back({name, QString(), std::move(column)});
    Py_RETURN_NONE;
}

bool tampon_liste(PyObject *list, std::vector<std::vector<double>> &out)
{
    PyObject *seq = PySequence_Fast(list, "liste de tableaux attendue");
    if (!seq) return false;
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(seq); ++i) {
        if (!lit_reels(PySequence_Fast_GET_ITEM(seq, i), out.emplace_back())) {
            Py_DECREF(seq);
            return false;
        }
    }
    Py_DECREF(seq);
    return true;
}

bool figure_de(PyObject *desc, PyObject *bufs, FigureMsg &out)
{
    const QJsonDocument d = QJsonDocument::fromJson(en_qstring(desc).toUtf8());
    if (!d.isObject()) {
        PyErr_SetString(PyExc_RuntimeError, "description de figure invalide");
        return false;
    }
    out.desc = figure::FigureDesc::depuis_json(d.object());
    return tampon_liste(bufs, out.data);
}

/// `plt.show()` : figure à dessiner par le logiciel.
PyObject *montre(PyObject *, PyObject *args)
{
    PyObject *desc = nullptr, *bufs = nullptr;
    if (!PyArg_ParseTuple(args, "UO", &desc, &bufs))
        return nullptr;
    FigureMsg f;
    if (!figure_de(desc, bufs, f))
        return nullptr;
    sortie_hote().figures.push_back(std::move(f));
    Py_RETURN_NONE;
}

/// `plt.savefig(chemin)` : figure à exporter en image.
PyObject *enregistre_figure(PyObject *, PyObject *args)
{
    PyObject *desc = nullptr, *bufs = nullptr, *path = nullptr;
    if (!PyArg_ParseTuple(args, "UOU", &desc, &bufs, &path))
        return nullptr;
    FigureMsg f;
    if (!figure_de(desc, bufs, f))
        return nullptr;
    f.savefig = en_qstring(path);
    sortie_hote().figures.push_back(std::move(f));
    Py_RETURN_NONE;
}

/// `Dessin.montrer()` : fenêtre de dessin à afficher.
PyObject *dessin(PyObject *, PyObject *args)
{
    PyObject *desc = nullptr, *bufs = nullptr;
    if (!PyArg_ParseTuple(args, "UO", &desc, &bufs))
        return nullptr;
    const QJsonDocument d = QJsonDocument::fromJson(en_qstring(desc).toUtf8());
    QString err;
    auto dd = d.isObject() ? drawing::Desc::depuis_json(d.object(), &err) : std::nullopt;
    if (!dd) {
        PyErr_Format(PyExc_RuntimeError, "description de dessin invalide : %s", err.toUtf8().constData());
        return nullptr;
    }
    DrawingMsg m{*dd, {}};
    if (!tampon_liste(bufs, m.data))
        return nullptr;
    auto &list = sortie_hote().drawings;
    std::erase_if(list, [&](const DrawingMsg &x) { return x.desc.name == m.desc.name; });
    list.push_back(std::move(m));
    Py_RETURN_NONE;
}

/// Carte d'intensité : expression en x, y évaluée sur une grille (ligne du haut d'abord).
PyObject *carte(PyObject *, PyObject *args)
{
    PyObject *exprO = nullptr, *boundsO = nullptr, *namesO = nullptr, *valuesO = nullptr;
    Py_ssize_t nx = 0, ny = 0;
    if (!PyArg_ParseTuple(args, "UOnnOO", &exprO, &boundsO, &nx, &ny, &namesO, &valuesO))
        return nullptr;
    std::vector<double> bounds, values;
    if (!lit_reels(boundsO, bounds) || !lit_reels(valuesO, values))
        return nullptr;
    if (bounds.size() != 4) {
        PyErr_SetString(PyExc_ValueError, "carte : bornes invalides");
        return nullptr;
    }
    std::vector<std::pair<QString, double>> params;
    PyObject *names = PySequence_Fast(namesO, "carte : liste de noms attendue");
    if (!names)
        return nullptr;
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(names) && static_cast<size_t>(i) < values.size(); ++i)
        params.emplace_back(en_qstring(PySequence_Fast_GET_ITEM(names, i)), values[i]);
    Py_DECREF(names);
    // les paramètres du document sont aussi utilisables
    for (const auto &[n, v] : sortie_hote().document.valeurs_parametres()) {
        if (std::none_of(params.begin(), params.end(), [&](const auto &p) { return p.first == n; }))
            params.emplace_back(n, v);
    }
    QString err;
    std::optional<std::vector<double>> v;
    const QString expression = en_qstring(exprO);
    Py_BEGIN_ALLOW_THREADS
    v = drawing::evalue_carte(expression, bounds[0], bounds[1], bounds[2], bounds[3], static_cast<int>(nx), static_cast<int>(ny), params, &err);
    Py_END_ALLOW_THREADS
    if (!v) {
        PyErr_SetString(PyExc_ValueError, err.toUtf8().constData());
        return nullptr;
    }
    return liste_reels(*v);
}

PyObject *couleur_longueur_onde(PyObject *, PyObject *args)
{
    double l = 0;
    if (!PyArg_ParseTuple(args, "d", &l))
        return nullptr;
    const auto c = drawing::rvb_longueur_onde(l);
    auto ch = [](double v) { return static_cast<int>(std::lround(v * 255.0)); };
    return PyUnicode_FromFormat("#%02x%02x%02x", ch(c[0]), ch(c[1]), ch(c[2]));
}

/// Fichier source d'un module intégré (`numpy`, `numpy.fft`, `matplotlib.pyplot`…).
QString chemin_source(const QString &name, bool *package)
{
    assure_ressources_coeur();
    const QString base = u":/python/"_s + QString(name).replace(u'.', u'/');
    if (QFile::exists(base + u"/__init__.py"_s)) {
        if (package)
            *package = true;
        return base + u"/__init__.py"_s;
    }
    if (QFile::exists(base + u".py"_s)) {
        if (package)
            *package = false;
        return base + u".py"_s;
    }
    return {};
}

PyObject *source(PyObject *, PyObject *args)
{
    PyObject *nameO = nullptr;
    if (!PyArg_ParseTuple(args, "U", &nameO))
        return nullptr;
    bool package = false;
    const QString path = chemin_source(en_qstring(nameO), &package);
    if (path.isEmpty())
        Py_RETURN_NONE;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        Py_RETURN_NONE;
    const QByteArray text = f.readAll();
    return Py_BuildValue("(s#O)", text.constData(), static_cast<Py_ssize_t>(text.size()), package ? Py_True : Py_False);
}

PyMethodDef moduleMethods[] = {
    {"_document", lit_document, METH_VARARGS, nullptr},
    {"grandeur", grandeur, METH_VARARGS, "grandeur(nom, valeurs, unite='') : crée ou remplace une grandeur du document."},
    {"show", montre, METH_VARARGS, nullptr},
    {"savefig", enregistre_figure, METH_VARARGS, nullptr},
    {"dessin", dessin, METH_VARARGS, nullptr},
    {"carte", carte, METH_VARARGS, nullptr},
    {"wavelength_color", couleur_longueur_onde, METH_VARARGS, nullptr},
    {"_source", source, METH_VARARGS, nullptr},
    {"_commande", commande, METH_VARARGS, nullptr},
    {"_ajuster", ajuster, METH_VARARGS, nullptr},
    {"_lire_csv", lire_csv, METH_VARARGS, nullptr},
    {"_auto", grandeur_auto, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef moduleDef = {PyModuleDef_HEAD_INIT, "_mevreg", "Échanges avec MeVReg", -1, moduleMethods,
                         nullptr, nullptr, nullptr, nullptr};

} // namespace

PyObject *initialise_module()
{
    return PyModule_Create(&moduleDef);
}

} // namespace python
