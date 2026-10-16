"""Mini-numpy de MeVReg : le sous-ensemble de numpy utile au lycée.

Mêmes noms et même syntaxe que numpy : un script qui n'utilise que ces
fonctions donne le même résultat ici et dans Capytale. Les calculs sont
faits en C++ sur des tableaux natifs (module _mevreg).
"""

import math as _math
import _mevreg as _r

__version__ = "2.0-mevreg"

pi = _math.pi
e = _math.e
inf = float("inf")
nan = float("nan")
NaN = nan
Inf = inf
euler_gamma = 0.5772156649015329
newaxis = None

float64 = "float64"
float32 = "float64"
int64 = "int64"
int32 = "int64"
complex128 = "complex128"
bool_ = "bool"


def _dtype_name(dtype):
    if dtype is None:
        return None
    if dtype is float:
        return "float"
    if dtype is int:
        return "int"
    if dtype is bool:
        return "bool"
    if dtype is complex:
        return "complex"
    return str(dtype)


class ndarray:
    """Tableau à une dimension, comme numpy.ndarray."""

    __slots__ = ("_b",)

    def __init__(self, b):
        self._b = b

    # -- description -------------------------------------------------------------
    @property
    def size(self):
        return self._b.len()

    @property
    def shape(self):
        return (self._b.len(),)

    @property
    def ndim(self):
        return 1

    @property
    def dtype(self):
        return {"bool": "bool", "int": "int64", "float": "float64", "complex": "complex128"}[self._b.kind()]

    @property
    def T(self):
        return self

    @property
    def real(self):
        return ndarray(self._b.unary("real"))

    @property
    def imag(self):
        return ndarray(self._b.unary("imag"))

    def __len__(self):
        return self._b.len()

    def __iter__(self):
        return iter(self._b.tolist())

    def __repr__(self):
        return self._b.to_repr()

    def __str__(self):
        return self._b.to_str()

    def __bool__(self):
        n = self._b.len()
        if n == 1:
            return bool(self._b.get(0))
        raise ValueError(
            "The truth value of an array with more than one element is ambiguous. Use a.any() or a.all()"
        )

    def __float__(self):
        if self._b.len() == 1:
            return float(self._b.get(0))
        raise TypeError("only length-1 arrays can be converted to Python scalars")

    def __int__(self):
        if self._b.len() == 1:
            return int(self._b.get(0))
        raise TypeError("only length-1 arrays can be converted to Python scalars")

    def __index__(self):
        return int(self)

    def __contains__(self, v):
        return v in self._b.tolist()

    def __array__(self):
        return self

    # -- indexation ----------------------------------------------------------------
    def __getitem__(self, k):
        if isinstance(k, tuple):
            if len(k) == 1:
                k = k[0]
            else:
                raise IndexError("too many indices for array: array is 1-dimensional")
        if isinstance(k, bool):
            raise IndexError("indexation par un booléen seul non disponible")
        if isinstance(k, int):
            return self._b.get(k)
        if isinstance(k, slice):
            return ndarray(self._b.slice(k.start, k.stop, k.step))
        if isinstance(k, ndarray):
            return ndarray(self._b.take(k._b))
        if isinstance(k, (list, tuple)):
            return ndarray(self._b.take(_r.from_seq(k)))
        if hasattr(k, "__index__"):
            return self._b.get(k.__index__())
        raise IndexError(
            "only integers, slices (`:`), ellipsis (`...`), numpy.newaxis (`None`) and integer or boolean arrays are valid indices"
        )

    def __setitem__(self, k, v):
        if isinstance(k, tuple) and len(k) == 1:
            k = k[0]
        if isinstance(v, ndarray):
            v = v._b
        if isinstance(k, int) and not isinstance(k, bool):
            if isinstance(v, _r.Buf):
                if v.len() != 1:
                    raise ValueError("setting an array element with a sequence.")
                v = v.get(0)
            self._b.set(k, v)
        elif isinstance(k, slice):
            self._b.set_slice(k.start, k.stop, k.step, v)
        elif isinstance(k, ndarray):
            self._b.put(k._b, v)
        elif isinstance(k, (list, tuple)):
            self._b.put(_r.from_seq(k), v)
        else:
            raise IndexError("indice non valable")

    # -- opérations -------------------------------------------------------------------
    def _op(self, other, op, rev=False):
        if isinstance(other, ndarray):
            other = other._b
        elif isinstance(other, (list, tuple)):
            other = _r.from_seq(other)
        elif other is None or isinstance(other, str):
            return NotImplemented
        return ndarray(self._b.binop(op, other, rev))

    def __add__(self, o):
        return self._op(o, "add")

    def __radd__(self, o):
        return self._op(o, "add", True)

    def __sub__(self, o):
        return self._op(o, "sub")

    def __rsub__(self, o):
        return self._op(o, "sub", True)

    def __mul__(self, o):
        return self._op(o, "mul")

    def __rmul__(self, o):
        return self._op(o, "mul", True)

    def __truediv__(self, o):
        return self._op(o, "div")

    def __rtruediv__(self, o):
        return self._op(o, "div", True)

    def __floordiv__(self, o):
        return self._op(o, "floordiv")

    def __rfloordiv__(self, o):
        return self._op(o, "floordiv", True)

    def __mod__(self, o):
        return self._op(o, "mod")

    def __rmod__(self, o):
        return self._op(o, "mod", True)

    def __pow__(self, o):
        return self._op(o, "pow")

    def __rpow__(self, o):
        return self._op(o, "pow", True)

    def __and__(self, o):
        return self._op(o, "and")

    def __rand__(self, o):
        return self._op(o, "and", True)

    def __or__(self, o):
        return self._op(o, "or")

    def __ror__(self, o):
        return self._op(o, "or", True)

    def __xor__(self, o):
        return self._op(o, "xor")

    def __lt__(self, o):
        return self._op(o, "lt")

    def __le__(self, o):
        return self._op(o, "le")

    def __gt__(self, o):
        return self._op(o, "gt")

    def __ge__(self, o):
        return self._op(o, "ge")

    def __eq__(self, o):
        r = self._op(o, "eq")
        return False if r is NotImplemented else r

    def __ne__(self, o):
        r = self._op(o, "ne")
        return True if r is NotImplemented else r

    __hash__ = None

    def __neg__(self):
        return ndarray(self._b.unary("neg"))

    def __pos__(self):
        return ndarray(self._b.copy())

    def __abs__(self):
        return ndarray(self._b.unary("abs"))

    def __invert__(self):
        return ndarray(self._b.unary("invert"))

    def __iadd__(self, o):
        r = self + o
        self._b = r._b
        return self

    def __isub__(self, o):
        r = self - o
        self._b = r._b
        return self

    def __imul__(self, o):
        r = self * o
        self._b = r._b
        return self

    def __itruediv__(self, o):
        r = self / o
        self._b = r._b
        return self

    def __matmul__(self, o):
        return dot(self, o)

    # -- méthodes ------------------------------------------------------------------------
    def copy(self):
        return ndarray(self._b.copy())

    def tolist(self):
        return self._b.tolist()

    def astype(self, dtype):
        return ndarray(self._b.astype(_dtype_name(dtype)))

    def sum(self):
        return self._b.reduce("sum", 0.0)

    def prod(self):
        return self._b.reduce("prod", 0.0)

    def mean(self):
        return self._b.reduce("mean", 0.0)

    def std(self, ddof=0):
        return self._b.reduce("std", float(ddof))

    def var(self, ddof=0):
        return self._b.reduce("var", float(ddof))

    def min(self):
        return self._b.reduce("min", 0.0)

    def max(self):
        return self._b.reduce("max", 0.0)

    def argmin(self):
        return self._b.reduce("argmin", 0.0)

    def argmax(self):
        return self._b.reduce("argmax", 0.0)

    def any(self):
        return self._b.reduce("any", 0.0)

    def all(self):
        return self._b.reduce("all", 0.0)

    def ptp(self):
        return self._b.reduce("ptp", 0.0)

    def cumsum(self):
        return ndarray(self._b.cumulative("cumsum"))

    def cumprod(self):
        return ndarray(self._b.cumulative("cumprod"))

    def round(self, decimals=0):
        return ndarray(self._b.round(decimals))

    def sort(self):
        self._b = self._b.take(self._b.argsort())

    def argsort(self):
        return ndarray(self._b.argsort())

    def conj(self):
        return ndarray(self._b.unary("conj"))

    def conjugate(self):
        return ndarray(self._b.unary("conj"))

    def flatten(self):
        return self.copy()

    def ravel(self):
        return self

    def reshape(self, *shape):
        if len(shape) == 1 and isinstance(shape[0], tuple):
            shape = shape[0]
        if len(shape) == 1 and shape[0] in (-1, self.size):
            return self
        raise ValueError("les tableaux à plusieurs dimensions ne sont pas disponibles dans ce logiciel")

    def dot(self, o):
        return dot(self, o)

    def clip(self, a_min, a_max):
        return clip(self, a_min, a_max)

    def __getattr__(self, name):
        raise AttributeError("ndarray." + name + " n'est pas disponible dans ce logiciel")


def _wrap(b):
    return ndarray(b)


def _buf(x):
    if isinstance(x, ndarray):
        return x._b
    return _r.from_seq(x)


def _is_scalar(x):
    return isinstance(x, (int, float, complex, bool))


def _unary(name, x):
    if _is_scalar(x):
        r = _r.from_seq([x]).unary(name).get(0)
        return r
    return ndarray(_buf(x).unary(name))


# -- création ------------------------------------------------------------------------------


def array(obj, dtype=None, copy=True):
    b = _r.from_seq(obj)
    if dtype is not None:
        b = b.astype(_dtype_name(dtype))
    return ndarray(b)


def asarray(obj, dtype=None):
    if isinstance(obj, ndarray) and dtype is None:
        return obj
    return array(obj, dtype)


def _size(shape):
    if isinstance(shape, (tuple, list)):
        if len(shape) == 1:
            return int(shape[0])
        raise ValueError("les tableaux à plusieurs dimensions ne sont pas disponibles dans ce logiciel")
    return int(shape)


def zeros(shape, dtype=float):
    return ndarray(_r.full(_size(shape), 0.0)).astype(dtype)


def ones(shape, dtype=float):
    return ndarray(_r.full(_size(shape), 1.0)).astype(dtype)


def empty(shape, dtype=float):
    return zeros(shape, dtype)


def full(shape, fill_value, dtype=None):
    a = ndarray(_r.full(_size(shape), fill_value))
    return a if dtype is None else a.astype(dtype)


def zeros_like(a, dtype=None):
    a = asarray(a)
    return zeros(a.size, dtype or a.dtype)


def ones_like(a, dtype=None):
    a = asarray(a)
    return ones(a.size, dtype or a.dtype)


def empty_like(a, dtype=None):
    return zeros_like(a, dtype)


def full_like(a, fill_value, dtype=None):
    return full(asarray(a).size, fill_value, dtype)


def copy(a):
    return array(a)


def linspace(start, stop, num=50, endpoint=True, retstep=False):
    a = ndarray(_r.linspace(float(start), float(stop), int(num), bool(endpoint)))
    if retstep:
        div = (num - 1) if endpoint else num
        return a, (stop - start) / div if div > 0 else nan
    return a


def logspace(start, stop, num=50, endpoint=True, base=10.0):
    return power(base, linspace(start, stop, num, endpoint))


def arange(start, stop=None, step=1, dtype=None):
    if stop is None:
        start, stop = 0, start
    is_int = True
    for v in (start, stop, step):
        if not isinstance(v, int) or isinstance(v, bool):
            is_int = False
    a = ndarray(_r.arange(float(start), float(stop), float(step), is_int))
    return a if dtype is None else a.astype(dtype)


def concatenate(arrays):
    return ndarray(_r.concatenate([_buf(a) for a in arrays]))


def append(arr, values):
    if _is_scalar(values):
        values = [values]
    return concatenate([arr, values])


# -- fonctions mathématiques ------------------------------------------------------------------


def sin(x):
    return _unary("sin", x)


def cos(x):
    return _unary("cos", x)


def tan(x):
    return _unary("tan", x)


def arcsin(x):
    return _unary("arcsin", x)


def arccos(x):
    return _unary("arccos", x)


def arctan(x):
    return _unary("arctan", x)


def sinh(x):
    return _unary("sinh", x)


def cosh(x):
    return _unary("cosh", x)


def tanh(x):
    return _unary("tanh", x)


def exp(x):
    return _unary("exp", x)


def exp2(x):
    return _unary("exp2", x)


def expm1(x):
    return _unary("expm1", x)


def log(x):
    return _unary("log", x)


def log10(x):
    return _unary("log10", x)


def log2(x):
    return _unary("log2", x)


def log1p(x):
    return _unary("log1p", x)


def sqrt(x):
    return _unary("sqrt", x)


def cbrt(x):
    return _unary("cbrt", x)


def square(x):
    return _unary("square", x)


def abs(x):
    return _unary("abs", x)


absolute = abs
fabs = abs


def sign(x):
    return _unary("sign", x)


def floor(x):
    return _unary("floor", x)


def ceil(x):
    return _unary("ceil", x)


def trunc(x):
    return _unary("trunc", x)


def rint(x):
    return _unary("rint", x)


def round(x, decimals=0):
    if _is_scalar(x):
        return _r.from_seq([x]).round(decimals).get(0)
    return ndarray(_buf(x).round(decimals))


around = round


def radians(x):
    return _unary("radians", x)


def degrees(x):
    return _unary("degrees", x)


deg2rad = radians
rad2deg = degrees


def reciprocal(x):
    return _unary("reciprocal", x)


def isnan(x):
    return _unary("isnan", x)


def isfinite(x):
    return _unary("isfinite", x)


def isinf(x):
    return _unary("isinf", x)


def real(x):
    return _unary("real", x)


def imag(x):
    return _unary("imag", x)


def angle(x, deg=False):
    a = _unary("angle", x)
    return degrees(a) if deg else a


def conj(x):
    return _unary("conj", x)


conjugate = conj


def _binary(name, a, b):
    if _is_scalar(a) and _is_scalar(b):
        return _r.from_seq([a]).binop(name, b, False).get(0)
    if _is_scalar(a):
        return ndarray(_buf(b).binop(name, a, True))
    return ndarray(_buf(a).binop(name, b if _is_scalar(b) else _buf(b), False))


def add(a, b):
    return _binary("add", a, b)


def subtract(a, b):
    return _binary("sub", a, b)


def multiply(a, b):
    return _binary("mul", a, b)


def divide(a, b):
    return _binary("div", a, b)


true_divide = divide


def power(a, b):
    return _binary("pow", a, b)


def mod(a, b):
    return _binary("mod", a, b)


remainder = mod


def arctan2(y, x):
    return _binary("arctan2", y, x)


def hypot(a, b):
    return _binary("hypot", a, b)


def maximum(a, b):
    return _binary("maximum", a, b)


def minimum(a, b):
    return _binary("minimum", a, b)


def clip(a, a_min, a_max):
    r = asarray(a)
    if a_min is not None:
        r = maximum(r, a_min)
    if a_max is not None:
        r = minimum(r, a_max)
    return r


# -- réductions -----------------------------------------------------------------------------


def _check_axis(axis):
    if axis not in (None, 0, -1):
        raise ValueError("axis : les tableaux à plusieurs dimensions ne sont pas disponibles dans ce logiciel")


def sum(a, axis=None):
    _check_axis(axis)
    return asarray(a).sum()


def prod(a, axis=None):
    _check_axis(axis)
    return asarray(a).prod()


def mean(a, axis=None):
    _check_axis(axis)
    return asarray(a).mean()


def average(a, weights=None):
    if weights is None:
        return mean(a)
    a = asarray(a)
    w = asarray(weights)
    return (a * w).sum() / w.sum()


def std(a, axis=None, ddof=0):
    _check_axis(axis)
    return asarray(a).std(ddof)


def var(a, axis=None, ddof=0):
    _check_axis(axis)
    return asarray(a).var(ddof)


def median(a):
    return _buf(a).reduce("median", 0.0)


def min(a, axis=None):
    _check_axis(axis)
    return asarray(a).min()


def max(a, axis=None):
    _check_axis(axis)
    return asarray(a).max()


amin = min
amax = max


def ptp(a):
    return asarray(a).ptp()


def argmin(a):
    return asarray(a).argmin()


def argmax(a):
    return asarray(a).argmax()


def any(a):
    return asarray(a).any()


def all(a):
    return asarray(a).all()


def nansum(a):
    a = asarray(a)
    return a[~isnan(a)].sum()


def nanmean(a):
    a = asarray(a)
    return a[~isnan(a)].mean()


def nanstd(a, ddof=0):
    a = asarray(a)
    return a[~isnan(a)].std(ddof)


def nanmin(a):
    a = asarray(a)
    return a[~isnan(a)].min()


def nanmax(a):
    a = asarray(a)
    return a[~isnan(a)].max()


def cumsum(a):
    return asarray(a).cumsum()


def cumprod(a):
    return asarray(a).cumprod()


def diff(a, n=1):
    r = asarray(a)
    for _ in range(n):
        r = ndarray(r._b.cumulative("diff"))
    return r


def gradient(f, *varargs):
    f = asarray(f)
    if len(varargs) == 0:
        x = 1.0
    elif len(varargs) == 1:
        x = varargs[0]
    else:
        raise TypeError("np.gradient : un seul pas ou un seul tableau d'abscisses à une dimension")
    if _is_scalar(x):
        x = [float(x)]
    return ndarray(_r.gradient(f._b, _buf(x)))


def trapezoid(y, x=None, dx=1.0):
    return _r.trapezoid(_buf(y), None if x is None else _buf(x), float(dx))


trapz = trapezoid


def dot(a, b):
    if _is_scalar(a) or _is_scalar(b):
        return multiply(a, b)
    return (asarray(a) * asarray(b)).sum()


inner = dot
vdot = dot


def cross(a, b):
    a = asarray(a)
    b = asarray(b)
    if a.size != 3 or b.size != 3:
        raise ValueError("np.cross : vecteurs à 3 composantes uniquement dans ce logiciel")
    return array([a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]])


def sort(a):
    a = asarray(a)
    return ndarray(a._b.take(a._b.argsort()))


def argsort(a):
    return asarray(a).argsort()


def flip(a):
    return asarray(a)[::-1]


def unique(a):
    s = sort(a).tolist()
    out = []
    for v in s:
        if not out or out[-1] != v:
            out.append(v)
    return array(out, asarray(a).dtype)


def where(condition, x=None, y=None):
    if x is None and y is None:
        return (ndarray(_r.nonzero(_buf(condition))),)
    return ndarray(_r.where3(_buf(condition), _buf(x) if not _is_scalar(x) else x, _buf(y) if not _is_scalar(y) else y))


def nonzero(a):
    return (ndarray(_r.nonzero(_buf(a))),)


def count_nonzero(a):
    return nonzero(a)[0].size


def interp(x, xp, fp):
    if _is_scalar(x):
        return _r.interp(_r.from_seq([x]), _buf(xp), _buf(fp)).get(0)
    return ndarray(_r.interp(_buf(x), _buf(xp), _buf(fp)))


def polyfit(x, y, deg):
    return ndarray(_r.polyfit(_buf(x), _buf(y), int(deg)))


def polyval(p, x):
    if _is_scalar(x):
        return _r.polyval(_buf(p), _r.from_seq([x])).get(0)
    return ndarray(_r.polyval(_buf(p), _buf(x)))


def histogram(a, bins=10, range=None):
    lo, hi = (None, None) if range is None else range
    counts, edges = _r.histogram(_buf(a), int(bins), lo, hi)
    return ndarray(counts), ndarray(edges)


def isclose(a, b, rtol=1e-05, atol=1e-08):
    return abs(subtract(a, b)) <= atol + rtol * abs(b)


def allclose(a, b, rtol=1e-05, atol=1e-08):
    r = isclose(a, b, rtol, atol)
    return bool(r) if _is_scalar(r) else r.all()


def array_equal(a, b):
    a = asarray(a)
    b = asarray(b)
    return a.size == b.size and bool((a == b).all())


def size(a):
    return asarray(a).size


def shape(a):
    return asarray(a).shape


def ndim(a):
    return 1


def meshgrid(*args, **kwargs):
    raise NotImplementedError("np.meshgrid : les tableaux à deux dimensions ne sont pas disponibles dans ce logiciel")


from numpy import fft, random, linalg  # noqa: E402


def __getattr__(name):
    raise AttributeError("np." + name + " n'est pas disponible dans ce logiciel")
