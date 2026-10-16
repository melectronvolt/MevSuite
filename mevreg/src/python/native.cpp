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
#include "python/npformat.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;
using arr::Arr;
using arr::Kind;
using arr::Scalar;

namespace python {

HostOutput &hostOutput()
{
    static HostOutput out;
    return out;
}

namespace {

arr::Rng &rng()
{
    static arr::Rng r(0x5EED);
    return r;
}

PyObject *BufType = nullptr;

struct BufObject {
    PyObject_HEAD
    Arr *a;
};

bool isBuf(PyObject *o)
{
    return BufType && PyObject_TypeCheck(o, reinterpret_cast<PyTypeObject *>(BufType));
}

Arr &bufArr(PyObject *o)
{
    return *reinterpret_cast<BufObject *>(o)->a;
}

/// Lève l'exception Python correspondant à une erreur de calcul.
PyObject *raise(const arr::Error &e)
{
    PyObject *type = e.type == arr::Error::Index ? PyExc_IndexError : e.type == arr::Error::Type_ ? PyExc_TypeError : PyExc_ValueError;
    PyErr_SetString(type, e.what());
    return nullptr;
}

/// Exécute `f` en transformant les erreurs de calcul en exceptions Python.
template <typename F>
PyObject *guard(F f)
{
    try {
        return f();
    } catch (const arr::Error &e) {
        return raise(e);
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

QString toQString(PyObject *s)
{
    Py_ssize_t n = 0;
    const char *u = PyUnicode_AsUTF8AndSize(s, &n);
    return u ? QString::fromUtf8(u, n) : QString();
}

PyObject *fromQString(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return PyUnicode_FromStringAndSize(b.constData(), b.size());
}

/// Valeur Python d'un élément, avec le bon type.
PyObject *scalarToPy(const Scalar &s)
{
    switch (s.kind) {
    case Kind::Bool:
        return PyBool_FromLong(s.re != 0.0);
    case Kind::Int:
        if (std::isfinite(s.re) && std::abs(s.re) < 9.0e15)
            return PyLong_FromLongLong(static_cast<long long>(s.re));
        return PyFloat_FromDouble(s.re);
    case Kind::Float:
        return PyFloat_FromDouble(s.re);
    case Kind::Complex:
        return PyComplex_FromDoubles(s.re, s.im);
    }
    Py_RETURN_NONE;
}

Scalar elem(const Arr &a, size_t i)
{
    return {a.kind, a.re[i], a.imAt(i)};
}

/// Tableau `_b` d'un objet `ndarray`, ou nullptr.
PyObject *innerBuf(PyObject *o)
{
    if (isBuf(o))
        return o;
    if (!PyObject_HasAttrString(o, "_b"))
        return nullptr;
    PyObject *b = PyObject_GetAttrString(o, "_b");
    if (!b) {
        PyErr_Clear();
        return nullptr;
    }
    Py_DECREF(b); // l'objet garde sa référence
    return isBuf(b) ? b : nullptr;
}

/// Nombre Python (bool, int, float, complex) → valeur typée ; faux et exception sinon.
bool pyToScalar(PyObject *o, Scalar &out)
{
    if (PyBool_Check(o)) {
        out = {Kind::Bool, o == Py_True ? 1.0 : 0.0, 0.0};
        return true;
    }
    if (PyLong_Check(o)) {
        const double v = PyLong_AsDouble(o);
        if (v == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            out = {Kind::Int, NAN, 0.0};
            return true;
        }
        out = {Kind::Int, v, 0.0};
        return true;
    }
    if (PyFloat_Check(o)) {
        out = {Kind::Float, PyFloat_AsDouble(o), 0.0};
        return true;
    }
    if (PyComplex_Check(o)) {
        out = {Kind::Complex, PyComplex_RealAsDouble(o), PyComplex_ImagAsDouble(o)};
        return true;
    }
    if (PyObject *b = innerBuf(o)) {
        const Arr &a = bufArr(b);
        if (a.size() == 1) {
            out = elem(a, 0);
            return true;
        }
        PyErr_SetString(PyExc_TypeError, "un tableau de plusieurs valeurs ne peut pas être converti en nombre");
        return false;
    }
    // objets convertibles en flottant (__float__)
    const double v = PyFloat_AsDouble(o);
    if (v == -1.0 && PyErr_Occurred())
        return false;
    out = {Kind::Float, v, 0.0};
    return true;
}

bool seqToArr(PyObject *o, Arr &out)
{
    PyObject *it = PyObject_GetIter(o);
    if (!it)
        return false;
    Kind kind = Kind::Bool;
    std::vector<double> re, im;
    bool empty = true;
    while (PyObject *item = PyIter_Next(it)) {
        empty = false;
        if (PyList_Check(item) || PyTuple_Check(item)) {
            Py_DECREF(item);
            Py_DECREF(it);
            PyErr_SetString(PyExc_ValueError,
                            "les tableaux à plusieurs dimensions ne sont pas disponibles dans ce logiciel (np.array à une dimension seulement)");
            return false;
        }
        Scalar s;
        const bool ok = pyToScalar(item, s);
        Py_DECREF(item);
        if (!ok) {
            Py_DECREF(it);
            return false;
        }
        kind = std::max(kind, s.kind);
        re.push_back(s.re);
        im.push_back(s.im);
    }
    Py_DECREF(it);
    if (PyErr_Occurred())
        return false;
    if (empty)
        kind = Kind::Float;
    out = kind == Kind::Complex ? Arr::cplx(std::move(re), std::move(im)) : Arr::make(kind, std::move(re));
    return true;
}

/// Opérande : tableau (`Buf`, `ndarray`), séquence ou nombre.
bool operand(PyObject *o, Arr &out)
{
    if (PyObject *b = innerBuf(o)) {
        out = bufArr(b);
        return true;
    }
    if (PyList_Check(o) || PyTuple_Check(o) || PyRange_Check(o))
        return seqToArr(o, out);
    Scalar s;
    if (!pyToScalar(o, s))
        return false;
    out = Arr::scalar(s.kind, s.re, s.im);
    return true;
}

std::optional<Kind> kindOf(const QString &name)
{
    if (name == u"bool")
        return Kind::Bool;
    if (name == u"int" || name == u"int64" || name == u"int32")
        return Kind::Int;
    if (name == u"float" || name == u"float64" || name == u"float32")
        return Kind::Float;
    if (name == u"complex" || name == u"complex128")
        return Kind::Complex;
    return std::nullopt;
}

/// Entier facultatif (None accepté).
bool optIndex(PyObject *o, std::optional<long long> &out)
{
    if (o == Py_None) {
        out.reset();
        return true;
    }
    PyObject *i = PyNumber_Index(o);
    if (!i)
        return false;
    out = PyLong_AsLongLong(i);
    Py_DECREF(i);
    return !PyErr_Occurred();
}

void store(Arr &a, size_t k, const Scalar &s)
{
    if (s.kind == Kind::Complex && a.kind != Kind::Complex) {
        // numpy refuserait ; on garde la partie réelle
        a.re[k] = s.re;
        return;
    }
    switch (a.kind) {
    case Kind::Int: a.re[k] = std::trunc(s.re); break;
    case Kind::Bool: a.re[k] = s.re != 0.0 ? 1.0 : 0.0; break;
    default: a.re[k] = s.re;
    }
    if (a.kind == Kind::Complex)
        a.im[k] = s.im;
}

bool assign(Arr &a, const std::vector<size_t> &idx, const Arr &v)
{
    if (v.size() != 1 && v.size() != idx.size()) {
        PyErr_Format(PyExc_ValueError, "could not broadcast input array from shape (%zu,) into shape (%zu,)", v.size(), idx.size());
        return false;
    }
    for (size_t j = 0; j < idx.size(); ++j) {
        const size_t src = v.size() == 1 ? 0 : j;
        store(a, idx[j], elem(v, src));
    }
    return true;
}

std::vector<size_t> indexList(const Arr &a, const Arr &ix)
{
    std::vector<size_t> out;
    switch (ix.kind) {
    case Kind::Bool:
        if (ix.size() != a.size()) {
            throw arr::Error(u"boolean index did not match indexed array along axis 0; size of axis is %1 but size of corresponding boolean axis is %2"_s.arg(a.size()).arg(ix.size()),
                             arr::Error::Index);
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (ix.re[i] != 0.0)
                out.push_back(i);
        }
        return out;
    case Kind::Int:
        for (double v : ix.re)
            out.push_back(arr::index(a.size(), static_cast<long long>(v)));
        return out;
    default:
        throw arr::Error(u"only integers, slices (`:`), ellipsis (`...`), numpy.newaxis (`None`) and integer or boolean arrays are valid indices"_s,
                         arr::Error::Index);
    }
}

// --- type Buf ------------------------------------------------------------------------------

void bufDealloc(PyObject *self)
{
    delete reinterpret_cast<BufObject *>(self)->a;
    PyTypeObject *tp = Py_TYPE(self);
    tp->tp_free(self);
    Py_DECREF(tp);
}

PyObject *bufLen(PyObject *self, PyObject *)
{
    return PyLong_FromSize_t(bufArr(self).size());
}

PyObject *bufKind(PyObject *self, PyObject *)
{
    switch (bufArr(self).kind) {
    case Kind::Bool: return PyUnicode_FromString("bool");
    case Kind::Int: return PyUnicode_FromString("int");
    case Kind::Float: return PyUnicode_FromString("float");
    case Kind::Complex: return PyUnicode_FromString("complex");
    }
    Py_RETURN_NONE;
}

PyObject *bufCopy(PyObject *self, PyObject *)
{
    return wrapBuf(bufArr(self));
}

PyObject *bufAstype(PyObject *self, PyObject *args)
{
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;
    const auto k = kindOf(QString::fromUtf8(name));
    if (!k)
        return PyErr_Format(PyExc_TypeError, "type de données non disponible : %s", name);
    return wrapBuf(bufArr(self).astype(*k));
}

PyObject *bufTolist(PyObject *self, PyObject *)
{
    const Arr &a = bufArr(self);
    PyObject *list = PyList_New(static_cast<Py_ssize_t>(a.size()));
    for (size_t i = 0; i < a.size(); ++i)
        PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), scalarToPy(elem(a, i)));
    return list;
}

PyObject *bufGet(PyObject *self, PyObject *args)
{
    long long i = 0;
    if (!PyArg_ParseTuple(args, "L", &i))
        return nullptr;
    return guard([&] {
        const Arr &a = bufArr(self);
        return scalarToPy(elem(a, arr::index(a.size(), i)));
    });
}

PyObject *bufSet(PyObject *self, PyObject *args)
{
    long long i = 0;
    PyObject *value = nullptr;
    if (!PyArg_ParseTuple(args, "LO", &i, &value))
        return nullptr;
    Scalar s;
    if (!pyToScalar(value, s))
        return nullptr;
    return guard([&]() -> PyObject * {
        Arr &a = bufArr(self);
        store(a, arr::index(a.size(), i), s);
        Py_RETURN_NONE;
    });
}

bool sliceArgs(PyObject *args, int extra, std::optional<long long> &start, std::optional<long long> &stop, std::optional<long long> &step, PyObject **value)
{
    PyObject *a = nullptr, *b = nullptr, *c = nullptr;
    if (extra) {
        if (!PyArg_ParseTuple(args, "OOOO", &a, &b, &c, value))
            return false;
    } else if (!PyArg_ParseTuple(args, "OOO", &a, &b, &c)) {
        return false;
    }
    return optIndex(a, start) && optIndex(b, stop) && optIndex(c, step);
}

PyObject *bufSlice(PyObject *self, PyObject *args)
{
    std::optional<long long> start, stop, step;
    if (!sliceArgs(args, 0, start, stop, step, nullptr))
        return nullptr;
    return guard([&] {
        const Arr &a = bufArr(self);
        return wrapBuf(arr::take(a, arr::sliceIndices(a.size(), start, stop, step)));
    });
}

PyObject *bufSetSlice(PyObject *self, PyObject *args)
{
    std::optional<long long> start, stop, step;
    PyObject *value = nullptr;
    if (!sliceArgs(args, 1, start, stop, step, &value))
        return nullptr;
    Arr v;
    if (!operand(value, v))
        return nullptr;
    return guard([&]() -> PyObject * {
        Arr &a = bufArr(self);
        if (!assign(a, arr::sliceIndices(a.size(), start, stop, step), v))
            return nullptr;
        Py_RETURN_NONE;
    });
}

/// Indexation par un tableau d'entiers ou de booléens.
PyObject *bufTake(PyObject *self, PyObject *args)
{
    PyObject *index = nullptr;
    if (!PyArg_ParseTuple(args, "O", &index))
        return nullptr;
    Arr ix;
    if (!operand(index, ix))
        return nullptr;
    return guard([&] {
        const Arr &a = bufArr(self);
        return wrapBuf(arr::take(a, indexList(a, ix)));
    });
}

PyObject *bufPut(PyObject *self, PyObject *args)
{
    PyObject *index = nullptr, *value = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &index, &value))
        return nullptr;
    Arr ix, v;
    if (!operand(index, ix) || !operand(value, v))
        return nullptr;
    return guard([&]() -> PyObject * {
        Arr &a = bufArr(self);
        if (!assign(a, indexList(a, ix), v))
            return nullptr;
        Py_RETURN_NONE;
    });
}

PyObject *bufBinop(PyObject *self, PyObject *args)
{
    const char *name = nullptr;
    PyObject *other = nullptr;
    int reverse = 0;
    if (!PyArg_ParseTuple(args, "sOp", &name, &other, &reverse))
        return nullptr;
    const auto op = arr::parseOp(QString::fromUtf8(name));
    if (!op)
        return PyErr_Format(PyExc_ValueError, "opération inconnue : %s", name);
    Arr b;
    if (!operand(other, b))
        return nullptr;
    return guard([&] {
        const Arr &a = bufArr(self);
        return wrapBuf(reverse ? arr::binop(b, a, *op) : arr::binop(a, b, *op));
    });
}

PyObject *bufUnary(PyObject *self, PyObject *args)
{
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;
    return guard([&] { return wrapBuf(arr::unary(bufArr(self), QString::fromUtf8(name))); });
}

PyObject *bufRound(PyObject *self, PyObject *args)
{
    int decimals = 0;
    if (!PyArg_ParseTuple(args, "i", &decimals))
        return nullptr;
    return wrapBuf(arr::round(bufArr(self), decimals));
}

PyObject *bufReduce(PyObject *self, PyObject *args)
{
    const char *name = nullptr;
    double ddof = 0;
    if (!PyArg_ParseTuple(args, "sd", &name, &ddof))
        return nullptr;
    return guard([&] { return scalarToPy(arr::reduce(bufArr(self), QString::fromUtf8(name), ddof)); });
}

PyObject *bufCumulative(PyObject *self, PyObject *args)
{
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;
    return guard([&] { return wrapBuf(arr::cumulative(bufArr(self), QString::fromUtf8(name))); });
}

PyObject *bufArgsort(PyObject *self, PyObject *)
{
    const Arr &a = bufArr(self);
    std::vector<size_t> idx(a.size());
    std::iota(idx.begin(), idx.end(), size_t{0});
    // comme numpy : les NaN à la fin, ordre stable
    std::stable_sort(idx.begin(), idx.end(), [&](size_t i, size_t j) {
        const double x = a.re[i], y = a.re[j];
        if (std::isnan(x))
            return false;
        if (std::isnan(y))
            return true;
        return x < y;
    });
    std::vector<double> v(idx.begin(), idx.end());
    return wrapBuf(Arr::make(Kind::Int, std::move(v)));
}

PyObject *bufFft(PyObject *self, PyObject *args)
{
    int inverse = 0;
    PyObject *nObj = Py_None;
    if (!PyArg_ParseTuple(args, "p|O", &inverse, &nObj))
        return nullptr;
    std::optional<long long> n;
    if (!optIndex(nObj, n))
        return nullptr;
    std::optional<size_t> len;
    if (n)
        len = static_cast<size_t>(std::max(*n, 0LL));
    return wrapBuf(arr::fft(bufArr(self), inverse, len));
}

PyObject *bufToStr(PyObject *self, PyObject *)
{
    return fromQString(npformat::arrayStr(bufArr(self)));
}

PyObject *bufToRepr(PyObject *self, PyObject *)
{
    return fromQString(npformat::arrayRepr(bufArr(self)));
}

PyMethodDef bufMethods[] = {
    {"len", bufLen, METH_NOARGS, nullptr},
    {"kind", bufKind, METH_NOARGS, nullptr},
    {"copy", bufCopy, METH_NOARGS, nullptr},
    {"astype", bufAstype, METH_VARARGS, nullptr},
    {"tolist", bufTolist, METH_NOARGS, nullptr},
    {"get", bufGet, METH_VARARGS, nullptr},
    {"set", bufSet, METH_VARARGS, nullptr},
    {"slice", bufSlice, METH_VARARGS, nullptr},
    {"set_slice", bufSetSlice, METH_VARARGS, nullptr},
    {"take", bufTake, METH_VARARGS, nullptr},
    {"put", bufPut, METH_VARARGS, nullptr},
    {"binop", bufBinop, METH_VARARGS, nullptr},
    {"unary", bufUnary, METH_VARARGS, nullptr},
    {"round", bufRound, METH_VARARGS, nullptr},
    {"reduce", bufReduce, METH_VARARGS, nullptr},
    {"cumulative", bufCumulative, METH_VARARGS, nullptr},
    {"argsort", bufArgsort, METH_NOARGS, nullptr},
    {"fft", bufFft, METH_VARARGS, nullptr},
    {"to_str", bufToStr, METH_NOARGS, nullptr},
    {"to_repr", bufToRepr, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

PyType_Slot bufSlots[] = {
    {Py_tp_dealloc, reinterpret_cast<void *>(bufDealloc)},
    {Py_tp_methods, bufMethods},
    {Py_tp_doc, const_cast<char *>("Tableau du mini-numpy (valeurs en C++)")},
    {0, nullptr},
};

PyType_Spec bufSpec = {"_mevreg.Buf", sizeof(BufObject), 0, Py_TPFLAGS_DEFAULT, bufSlots};

// --- fonctions du module -----------------------------------------------------------------------

PyObject *fromSeq(PyObject *, PyObject *args)
{
    PyObject *o = nullptr;
    if (!PyArg_ParseTuple(args, "O", &o))
        return nullptr;
    if (PyObject *b = innerBuf(o))
        return wrapBuf(bufArr(b));
    if (PyUnicode_Check(o) || PyBytes_Check(o)) {
        PyErr_SetString(PyExc_TypeError, "une chaîne de caractères ne peut pas former un tableau de nombres");
        return nullptr;
    }
    Arr a;
    if (PyObject_HasAttrString(o, "__iter__")) {
        if (!seqToArr(o, a))
            return nullptr;
        return wrapBuf(std::move(a));
    }
    // un nombre seul : tableau d'une valeur
    Scalar s;
    if (!pyToScalar(o, s))
        return nullptr;
    return wrapBuf(Arr::scalar(s.kind, s.re, s.im));
}

PyObject *full(PyObject *, PyObject *args)
{
    Py_ssize_t n = 0;
    PyObject *value = nullptr;
    if (!PyArg_ParseTuple(args, "nO", &n, &value))
        return nullptr;
    Scalar s;
    if (!pyToScalar(value, s))
        return nullptr;
    const size_t k = static_cast<size_t>(std::max<Py_ssize_t>(n, 0));
    if (s.kind == Kind::Complex)
        return wrapBuf(Arr::cplx(std::vector<double>(k, s.re), std::vector<double>(k, s.im)));
    return wrapBuf(Arr::make(s.kind, std::vector<double>(k, s.re)));
}

PyObject *linspaceFn(PyObject *, PyObject *args)
{
    double start = 0, stop = 0;
    Py_ssize_t num = 0;
    int endpoint = 1;
    if (!PyArg_ParseTuple(args, "ddnp", &start, &stop, &num, &endpoint))
        return nullptr;
    if (num < 0) {
        PyErr_Format(PyExc_ValueError, "Number of samples, %zd, must be non-negative.", num);
        return nullptr;
    }
    return wrapBuf(arr::linspace(start, stop, static_cast<size_t>(num), endpoint));
}

PyObject *arangeFn(PyObject *, PyObject *args)
{
    double start = 0, stop = 0, step = 1;
    int isInt = 0;
    if (!PyArg_ParseTuple(args, "dddp", &start, &stop, &step, &isInt))
        return nullptr;
    return guard([&] { return wrapBuf(arr::arange(start, stop, step, isInt)); });
}

PyObject *concatenate(PyObject *, PyObject *args)
{
    PyObject *parts = nullptr;
    if (!PyArg_ParseTuple(args, "O", &parts))
        return nullptr;
    PyObject *seq = PySequence_Fast(parts, "concatenate : une liste de tableaux est attendue");
    if (!seq)
        return nullptr;
    std::vector<Arr> arrs;
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(seq); ++i) {
        Arr a;
        if (!operand(PySequence_Fast_GET_ITEM(seq, i), a)) {
            Py_DECREF(seq);
            return nullptr;
        }
        arrs.push_back(std::move(a));
    }
    Py_DECREF(seq);
    Kind kind = Kind::Bool;
    for (const auto &a : arrs)
        kind = std::max(kind, a.kind);
    if (arrs.empty())
        kind = Kind::Float;
    Arr out;
    out.kind = kind;
    for (const auto &a0 : arrs) {
        const Arr a = a0.astype(kind);
        out.re.insert(out.re.end(), a.re.begin(), a.re.end());
        out.im.insert(out.im.end(), a.im.begin(), a.im.end());
    }
    return wrapBuf(std::move(out));
}

PyObject *where3(PyObject *, PyObject *args)
{
    PyObject *co = nullptr, *ao = nullptr, *bo = nullptr;
    if (!PyArg_ParseTuple(args, "OOO", &co, &ao, &bo))
        return nullptr;
    Arr c, x, y;
    if (!operand(co, c) || !operand(ao, x) || !operand(bo, y))
        return nullptr;
    const size_t n = std::max({c.size(), x.size(), y.size()});
    for (size_t l : {c.size(), x.size(), y.size()}) {
        if (l != 1 && l != n) {
            PyErr_SetString(PyExc_ValueError, "np.where : tailles incompatibles");
            return nullptr;
        }
    }
    const Kind kind = std::min(std::max({x.kind, y.kind, Kind::Int}), Kind::Float);
    auto at = [](const Arr &v, size_t i) { return v.size() == 1 ? v.re[0] : v.re[i]; };
    std::vector<double> re(n);
    for (size_t i = 0; i < n; ++i)
        re[i] = at(c, i) != 0.0 ? at(x, i) : at(y, i);
    return wrapBuf(Arr::make(kind, std::move(re)));
}

PyObject *nonzero(PyObject *, PyObject *args)
{
    PyObject *co = nullptr;
    if (!PyArg_ParseTuple(args, "O", &co))
        return nullptr;
    Arr c;
    if (!operand(co, c))
        return nullptr;
    std::vector<double> idx;
    for (size_t i = 0; i < c.size(); ++i) {
        if (c.re[i] != 0.0 || c.imAt(i) != 0.0)
            idx.push_back(static_cast<double>(i));
    }
    return wrapBuf(Arr::make(Kind::Int, std::move(idx)));
}

PyObject *gradientFn(PyObject *, PyObject *args)
{
    PyObject *fo = nullptr, *xo = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &fo, &xo))
        return nullptr;
    Arr f, x;
    if (!operand(fo, f) || !operand(xo, x))
        return nullptr;
    return guard([&] { return wrapBuf(Arr::flt(arr::gradient(f.re, x.re))); });
}

PyObject *trapezoidFn(PyObject *, PyObject *args)
{
    PyObject *yo = nullptr, *xo = nullptr;
    double dx = 1.0;
    if (!PyArg_ParseTuple(args, "OOd", &yo, &xo, &dx))
        return nullptr;
    Arr y, x;
    if (!operand(yo, y))
        return nullptr;
    if (xo != Py_None && !operand(xo, x))
        return nullptr;
    return guard([&] { return PyFloat_FromDouble(arr::trapezoid(y.re, x.re, dx)); });
}

PyObject *interpFn(PyObject *, PyObject *args)
{
    PyObject *xo = nullptr, *xpo = nullptr, *fpo = nullptr;
    if (!PyArg_ParseTuple(args, "OOO", &xo, &xpo, &fpo))
        return nullptr;
    Arr x, xp, fp;
    if (!operand(xo, x) || !operand(xpo, xp) || !operand(fpo, fp))
        return nullptr;
    if (xp.size() != fp.size() || xp.size() == 0) {
        PyErr_SetString(PyExc_ValueError, "fp and xp are not of the same length.");
        return nullptr;
    }
    std::vector<double> r(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        r[i] = numeric::interp(x.re[i], xp.re, fp.re);
    return wrapBuf(Arr::flt(std::move(r)));
}

PyObject *polyfitFn(PyObject *, PyObject *args)
{
    PyObject *xo = nullptr, *yo = nullptr;
    Py_ssize_t deg = 0;
    if (!PyArg_ParseTuple(args, "OOn", &xo, &yo, &deg))
        return nullptr;
    Arr x, y;
    if (!operand(xo, x) || !operand(yo, y))
        return nullptr;
    return guard([&] { return wrapBuf(Arr::flt(arr::polyfit(x.re, y.re, static_cast<size_t>(std::max<Py_ssize_t>(deg, 0))))); });
}

PyObject *polyvalFn(PyObject *, PyObject *args)
{
    PyObject *po = nullptr, *xo = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &po, &xo))
        return nullptr;
    Arr p, x;
    if (!operand(po, p) || !operand(xo, x))
        return nullptr;
    // entiers conservés si coefficients et abscisses sont entiers
    const Kind kind = (p.kind <= Kind::Int && x.kind <= Kind::Int) ? Kind::Int : Kind::Float;
    return wrapBuf(Arr::make(kind, arr::polyval(p.re, x.re)));
}

PyObject *fftfreqFn(PyObject *, PyObject *args)
{
    Py_ssize_t n = 0;
    double d = 1.0;
    int half = 0;
    if (!PyArg_ParseTuple(args, "ndp", &n, &d, &half))
        return nullptr;
    return wrapBuf(arr::fftfreq(static_cast<size_t>(std::max<Py_ssize_t>(n, 0)), d, half));
}

/// Histogramme : (effectifs, bornes des classes).
PyObject *histogramFn(PyObject *, PyObject *args)
{
    PyObject *xo = nullptr, *loO = Py_None, *hiO = Py_None;
    Py_ssize_t bins = 10;
    if (!PyArg_ParseTuple(args, "On|OO", &xo, &bins, &loO, &hiO))
        return nullptr;
    Arr x;
    if (!operand(xo, x))
        return nullptr;
    std::vector<double> finite;
    for (double v : x.re) {
        if (std::isfinite(v))
            finite.push_back(v);
    }
    double a, b;
    if (loO != Py_None && hiO != Py_None) {
        a = PyFloat_AsDouble(loO);
        b = PyFloat_AsDouble(hiO);
        if (PyErr_Occurred())
            return nullptr;
    } else {
        const auto r = numeric::finiteRange(finite).value_or(std::make_pair(0.0, 1.0));
        a = r.first;
        b = r.second;
    }
    if (a == b) {
        a -= 0.5;
        b += 0.5;
    }
    const size_t nb = static_cast<size_t>(std::max<Py_ssize_t>(bins, 1));
    const double w = (b - a) / static_cast<double>(nb);
    std::vector<double> edges(nb + 1), counts(nb, 0.0);
    for (size_t i = 0; i <= nb; ++i)
        edges[i] = a + w * static_cast<double>(i);
    for (double v : finite) {
        if (v < a || v > b)
            continue;
        size_t k = std::min(static_cast<size_t>(std::floor((v - a) / w)), nb - 1);
        if (k + 1 < nb && v >= edges[k + 1])
            ++k;
        counts[k] += 1.0;
    }
    PyObject *c = wrapBuf(Arr::make(Kind::Int, std::move(counts)));
    PyObject *e = wrapBuf(Arr::flt(std::move(edges)));
    return Py_BuildValue("(NN)", c, e);
}

PyObject *seedFn(PyObject *, PyObject *args)
{
    unsigned long long s = 0;
    if (!PyArg_ParseTuple(args, "K", &s))
        return nullptr;
    rng() = arr::Rng(s);
    Py_RETURN_NONE;
}

PyObject *randomUniform(PyObject *, PyObject *args)
{
    Py_ssize_t n = 0;
    double lo = 0, hi = 1;
    if (!PyArg_ParseTuple(args, "ndd", &n, &lo, &hi))
        return nullptr;
    std::vector<double> v(static_cast<size_t>(std::max<Py_ssize_t>(n, 0)));
    for (double &x : v)
        x = lo + (hi - lo) * rng().uniform();
    return wrapBuf(Arr::flt(std::move(v)));
}

PyObject *randomNormal(PyObject *, PyObject *args)
{
    Py_ssize_t n = 0;
    double mu = 0, sigma = 1;
    if (!PyArg_ParseTuple(args, "ndd", &n, &mu, &sigma))
        return nullptr;
    std::vector<double> v(static_cast<size_t>(std::max<Py_ssize_t>(n, 0)));
    for (double &x : v)
        x = mu + sigma * rng().normal();
    return wrapBuf(Arr::flt(std::move(v)));
}

PyObject *randomInt(PyObject *, PyObject *args)
{
    Py_ssize_t n = 0;
    long long lo = 0, hi = 1;
    if (!PyArg_ParseTuple(args, "nLL", &n, &lo, &hi))
        return nullptr;
    if (hi <= lo) {
        PyErr_SetString(PyExc_ValueError, "low >= high");
        return nullptr;
    }
    std::vector<double> v(static_cast<size_t>(std::max<Py_ssize_t>(n, 0)));
    for (double &x : v)
        x = static_cast<double>(lo + static_cast<long long>(rng().below(static_cast<std::uint64_t>(hi - lo))));
    return wrapBuf(Arr::make(Kind::Int, std::move(v)));
}

/// `grandeur("v", v, "m/s")` : crée ou remplace une grandeur du document.
PyObject *grandeur(PyObject *, PyObject *args)
{
    PyObject *nameO = nullptr, *values = nullptr;
    const char *unit = "";
    if (!PyArg_ParseTuple(args, "UO|s", &nameO, &values, &unit))
        return nullptr;
    const QString name = toQString(nameO);
    if (!expr::isValidName(name) || expr::isFunctionName(name))
        return PyErr_Format(PyExc_ValueError, "« %U » n'est pas un nom de grandeur valable", nameO);
    Arr a;
    if (!operand(values, a))
        return nullptr;
    if (a.kind == Kind::Complex) {
        PyErr_SetString(PyExc_ValueError, "une grandeur ne peut pas contenir de nombres complexes (utilisez np.abs ou .real)");
        return nullptr;
    }
    auto &q = hostOutput().quantities;
    std::erase_if(q, [&](const NamedValues &v) { return v.name == name; });
    q.push_back({name, QString::fromUtf8(unit), a.re});
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
    hostOutput().commands.append(d.object());
    Py_RETURN_NONE;
}

/// `_ajuster(type, degré, expression, nom_x, x, nom_y, y, plage)` : modélisation faite tout de
/// suite, comme dans la fenêtre Modélisation ; (noms, valeurs, incertitudes, R², n, équation,
/// erreur).
PyObject *ajuster(PyObject *, PyObject *args)
{
    const char *type = nullptr, *expression = nullptr, *xname = nullptr, *yname = nullptr;
    int degree = 3;
    PyObject *xo = nullptr, *yo = nullptr, *range = nullptr;
    if (!PyArg_ParseTuple(args, "sissOsOO", &type, &degree, &expression, &xname, &xo, &yname, &yo, &range))
        return nullptr;
    Arr xa, ya;
    if (!operand(xo, xa) || !operand(yo, ya))
        return nullptr;
    fit::ModelKind kind;
    const auto t = fit::ModelKind::typeFromName(QString::fromUtf8(type));
    if (!t) {
        PyErr_Format(PyExc_ValueError, "modèle inconnu : %s", type);
        return nullptr;
    }
    kind.type = *t;
    kind.degree = std::clamp(degree, 1, 6);
    kind.expression = QString::fromUtf8(expression);
    // un petit document : les deux grandeurs et les paramètres du vrai
    Document doc;
    for (const auto &[n, v] : hostOutput().docParams)
        doc.addParameter(n, QString(), v);
    const auto xid = doc.addExperimental(QString::fromUtf8(xname), QString(), xa.re);
    const auto yid = doc.addExperimental(QString::fromUtf8(yname) == QString::fromUtf8(xname) ? u"y_"_s + QString::fromUtf8(yname) : QString::fromUtf8(yname), QString(), ya.re);
    if (!xid || !yid) {
        PyErr_SetString(PyExc_ValueError, qPrintable(!xid ? xid.error : yid.error));
        return nullptr;
    }
    FitSpec spec = FitSpec::make(kind, *xid, *yid, PALETTE[3]);
    if (range && range != Py_None) {
        double a = 0, b = 0;
        if (!PyArg_ParseTuple(range, "dd", &a, &b))
            return nullptr;
        spec.range = std::minmax(a, b);
    }
    computeFit(spec, doc);
    PyObject *names = PyList_New(0), *values = PyList_New(0), *uncs = PyList_New(0);
    double r2 = NAN;
    long long n = 0;
    QString equation;
    if (spec.result) {
        for (qsizetype k = 0; k < spec.paramNames.size(); ++k) {
            PyObject *nm = fromQString(spec.paramNames[k]);
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
        if (auto model = fit::Model::make(kind, QString::fromUtf8(xname), doc.paramValues()))
            equation = model->display(spec.result->params, QString::fromUtf8(xname), QString::fromUtf8(yname), NumberStyle{false});
    }
    PyObject *eq = fromQString(equation), *err = fromQString(spec.resultError);
    return Py_BuildValue("(NNNdLNN)", names, values, uncs, r2, n, eq, err);
}

/// `_lire_csv(chemin, sep, decimal)` : fichier de données lu comme par l'import de MeVReg
/// (séparateurs devinés quand ils sont vides) ; (en-têtes, unités, colonnes).
PyObject *lireCsv(PyObject *, PyObject *args)
{
    PyObject *pathO = nullptr;
    const char *sep = "", *decimal = "";
    if (!PyArg_ParseTuple(args, "U|ss", &pathO, &sep, &decimal))
        return nullptr;
    const QString path = toQString(pathO);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        PyErr_Format(PyExc_FileNotFoundError, "fichier introuvable ou illisible : %U", pathO);
        return nullptr;
    }
    const QStringList lines = csv::textLines(csv::decodeText(f.readAll()));
    if (lines.isEmpty()) {
        PyErr_Format(PyExc_ValueError, "fichier vide : %U", pathO);
        return nullptr;
    }
    csv::ReadOptions o = csv::detect(lines);
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
    const csv::ReadOptions again = [&] {
        csv::ReadOptions d = csv::detect(lines, o.decimal);
        d.separator = o.separator;
        d.decimal = o.decimal;
        return d;
    }();
    const csv::Reading r = csv::read(lines, again);
    const QStringList raw = again.nameLine >= 0 ? csv::splitLine(lines[again.nameLine], again.separator) : QStringList();
    PyObject *headers = PyList_New(0), *units = PyList_New(0), *cols = PyList_New(0);
    for (size_t j = 0; j < r.columns.size(); ++j) {
        const QString h = raw.value(static_cast<qsizetype>(j)).isEmpty() ? QString::number(j) : raw.value(static_cast<qsizetype>(j));
        PyObject *ho = fromQString(h), *uo = fromQString(r.columns[j].unit);
        PyList_Append(headers, ho);
        PyList_Append(units, uo);
        Py_DECREF(ho);
        Py_DECREF(uo);
        // toutes les colonnes à la longueur des données (cases vides : NaN)
        std::vector<double> v = r.columns[j].values;
        v.resize(r.rows.size(), NAN);
        PyObject *b = wrapBuf(Arr::flt(std::move(v)));
        PyList_Append(cols, b);
        Py_DECREF(b);
    }
    return Py_BuildValue("(NNN)", headers, units, cols);
}

/// `_auto(nom, valeurs)` : liste de nombres ou tableau du script, devenu grandeur sans unité ;
/// ignoré si le script l'a déjà donnée par `grandeur()` (avec son unité) ou si le nom ne convient pas.
PyObject *autoQuantity(PyObject *, PyObject *args)
{
    PyObject *nameO = nullptr, *values = nullptr;
    if (!PyArg_ParseTuple(args, "UO", &nameO, &values))
        return nullptr;
    const QString name = toQString(nameO);
    auto &q = hostOutput().quantities;
    if (!expr::isValidName(name) || expr::isFunctionName(name) || std::any_of(q.begin(), q.end(), [&](const NamedValues &v) { return v.name == name; }))
        Py_RETURN_NONE;
    Arr a;
    if (!operand(values, a)) {
        PyErr_Clear();
        Py_RETURN_NONE;
    }
    if (a.kind == Kind::Complex || a.kind == Kind::Bool)
        Py_RETURN_NONE;
    q.push_back({name, QString(), a.re});
    Py_RETURN_NONE;
}

bool bufList(PyObject *list, std::vector<std::vector<double>> &out, bool modulus)
{
    PyObject *seq = PySequence_Fast(list, "liste de tableaux attendue");
    if (!seq)
        return false;
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(seq); ++i) {
        Arr a;
        if (!operand(PySequence_Fast_GET_ITEM(seq, i), a)) {
            Py_DECREF(seq);
            return false;
        }
        if (modulus && a.kind == Kind::Complex) {
            std::vector<double> m(a.size());
            for (size_t k = 0; k < a.size(); ++k)
                m[k] = std::hypot(a.re[k], a.im[k]);
            out.push_back(std::move(m));
        } else {
            out.push_back(a.re);
        }
    }
    Py_DECREF(seq);
    return true;
}

bool figureFrom(PyObject *desc, PyObject *bufs, FigureMsg &out)
{
    const QJsonDocument d = QJsonDocument::fromJson(toQString(desc).toUtf8());
    if (!d.isObject()) {
        PyErr_SetString(PyExc_RuntimeError, "description de figure invalide");
        return false;
    }
    out.desc = figure::FigureDesc::fromJson(d.object());
    return bufList(bufs, out.data, true);
}

/// `plt.show()` : figure à dessiner par le logiciel.
PyObject *show(PyObject *, PyObject *args)
{
    PyObject *desc = nullptr, *bufs = nullptr;
    if (!PyArg_ParseTuple(args, "UO", &desc, &bufs))
        return nullptr;
    FigureMsg f;
    if (!figureFrom(desc, bufs, f))
        return nullptr;
    hostOutput().figures.push_back(std::move(f));
    Py_RETURN_NONE;
}

/// `plt.savefig(chemin)` : figure à exporter en image.
PyObject *savefig(PyObject *, PyObject *args)
{
    PyObject *desc = nullptr, *bufs = nullptr, *path = nullptr;
    if (!PyArg_ParseTuple(args, "UOU", &desc, &bufs, &path))
        return nullptr;
    FigureMsg f;
    if (!figureFrom(desc, bufs, f))
        return nullptr;
    f.savefig = toQString(path);
    hostOutput().figures.push_back(std::move(f));
    Py_RETURN_NONE;
}

/// `Dessin.montrer()` : fenêtre de dessin à afficher.
PyObject *dessin(PyObject *, PyObject *args)
{
    PyObject *desc = nullptr, *bufs = nullptr;
    if (!PyArg_ParseTuple(args, "UO", &desc, &bufs))
        return nullptr;
    const QJsonDocument d = QJsonDocument::fromJson(toQString(desc).toUtf8());
    QString err;
    auto dd = d.isObject() ? drawing::Desc::fromJson(d.object(), &err) : std::nullopt;
    if (!dd) {
        PyErr_Format(PyExc_RuntimeError, "description de dessin invalide : %s", err.toUtf8().constData());
        return nullptr;
    }
    DrawingMsg m{*dd, {}};
    if (!bufList(bufs, m.data, false))
        return nullptr;
    auto &list = hostOutput().drawings;
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
    Arr bounds, values;
    if (!operand(boundsO, bounds) || !operand(valuesO, values))
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
        params.emplace_back(toQString(PySequence_Fast_GET_ITEM(names, i)), values.re[i]);
    Py_DECREF(names);
    // les paramètres du document sont aussi utilisables
    for (const auto &[n, v] : hostOutput().docParams) {
        if (std::none_of(params.begin(), params.end(), [&](const auto &p) { return p.first == n; }))
            params.emplace_back(n, v);
    }
    QString err;
    std::optional<std::vector<double>> v;
    Py_BEGIN_ALLOW_THREADS
    v = drawing::evalMap(toQString(exprO), bounds.re[0], bounds.re[1], bounds.re[2], bounds.re[3], static_cast<int>(nx), static_cast<int>(ny), params, &err);
    Py_END_ALLOW_THREADS
    if (!v) {
        PyErr_SetString(PyExc_ValueError, err.toUtf8().constData());
        return nullptr;
    }
    return wrapBuf(Arr::flt(std::move(*v)));
}

PyObject *wavelengthColor(PyObject *, PyObject *args)
{
    double l = 0;
    if (!PyArg_ParseTuple(args, "d", &l))
        return nullptr;
    const auto c = drawing::wavelengthRgb(l);
    auto ch = [](double v) { return static_cast<int>(std::lround(v * 255.0)); };
    return PyUnicode_FromFormat("#%02x%02x%02x", ch(c[0]), ch(c[1]), ch(c[2]));
}

/// Fichier source d'un module intégré (`numpy`, `numpy.fft`, `matplotlib.pyplot`…).
QString sourcePath(const QString &name, bool *package)
{
    ensureCoreResources();
    const QString base = u":/python/"_s + QString(name).replace(u'.', u'/');
    if (QFile::exists(base + u"/__init__.py")) {
        if (package)
            *package = true;
        return base + u"/__init__.py"_s;
    }
    if (QFile::exists(base + u".py")) {
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
    const QString path = sourcePath(toQString(nameO), &package);
    if (path.isEmpty())
        Py_RETURN_NONE;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        Py_RETURN_NONE;
    const QByteArray text = f.readAll();
    return Py_BuildValue("(s#O)", text.constData(), static_cast<Py_ssize_t>(text.size()), package ? Py_True : Py_False);
}

PyMethodDef moduleMethods[] = {
    {"from_seq", fromSeq, METH_VARARGS, nullptr},
    {"full", full, METH_VARARGS, nullptr},
    {"linspace", linspaceFn, METH_VARARGS, nullptr},
    {"arange", arangeFn, METH_VARARGS, nullptr},
    {"concatenate", concatenate, METH_VARARGS, nullptr},
    {"where3", where3, METH_VARARGS, nullptr},
    {"nonzero", nonzero, METH_VARARGS, nullptr},
    {"gradient", gradientFn, METH_VARARGS, nullptr},
    {"trapezoid", trapezoidFn, METH_VARARGS, nullptr},
    {"interp", interpFn, METH_VARARGS, nullptr},
    {"polyfit", polyfitFn, METH_VARARGS, nullptr},
    {"polyval", polyvalFn, METH_VARARGS, nullptr},
    {"fftfreq", fftfreqFn, METH_VARARGS, nullptr},
    {"histogram", histogramFn, METH_VARARGS, nullptr},
    {"seed", seedFn, METH_VARARGS, nullptr},
    {"random_uniform", randomUniform, METH_VARARGS, nullptr},
    {"random_normal", randomNormal, METH_VARARGS, nullptr},
    {"random_int", randomInt, METH_VARARGS, nullptr},
    {"grandeur", grandeur, METH_VARARGS, "grandeur(nom, valeurs, unite='') : crée ou remplace une grandeur du document."},
    {"show", show, METH_VARARGS, nullptr},
    {"savefig", savefig, METH_VARARGS, nullptr},
    {"dessin", dessin, METH_VARARGS, nullptr},
    {"carte", carte, METH_VARARGS, nullptr},
    {"wavelength_color", wavelengthColor, METH_VARARGS, nullptr},
    {"_source", source, METH_VARARGS, nullptr},
    {"_commande", commande, METH_VARARGS, nullptr},
    {"_ajuster", ajuster, METH_VARARGS, nullptr},
    {"_lire_csv", lireCsv, METH_VARARGS, nullptr},
    {"_auto", autoQuantity, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef moduleDef = {PyModuleDef_HEAD_INIT, "_mevreg", "Calculs du mini-numpy et échanges avec MeVReg", -1, moduleMethods,
                         nullptr, nullptr, nullptr, nullptr};

} // namespace

PyObject *wrapBuf(Arr a)
{
    auto *o = PyObject_New(BufObject, reinterpret_cast<PyTypeObject *>(BufType));
    if (!o)
        return nullptr;
    o->a = new Arr(std::move(a));
    return reinterpret_cast<PyObject *>(o);
}

PyObject *initModule()
{
    PyObject *m = PyModule_Create(&moduleDef);
    if (!m)
        return nullptr;
    BufType = PyType_FromSpec(&bufSpec);
    if (!BufType) {
        Py_DECREF(m);
        return nullptr;
    }
    Py_INCREF(BufType);
    if (PyModule_AddObject(m, "Buf", BufType) < 0) {
        Py_DECREF(BufType);
        Py_DECREF(m);
        return nullptr;
    }
    return m;
}

} // namespace python
