"""Mini numpy.random : tirages aléatoires (générateur reproductible avec seed)."""

import _mevreg as _r
from numpy import ndarray, asarray


def _count(size):
    if size is None:
        return None
    if isinstance(size, (tuple, list)):
        if len(size) == 1:
            return int(size[0])
        raise ValueError("np.random : les tableaux à plusieurs dimensions ne sont pas disponibles dans ce logiciel")
    return int(size)


def _out(b, n):
    if n is None:
        return b.get(0)
    return ndarray(b)


def seed(s=None):
    if s is None:
        import time
        s = int(time.time() * 1000)
    _r.seed(int(s))


def random(size=None):
    n = _count(size)
    return _out(_r.random_uniform(1 if n is None else n, 0.0, 1.0), n)


random_sample = random
ranf = random
sample = random


def rand(*shape):
    return random(shape[0] if shape else None)


def uniform(low=0.0, high=1.0, size=None):
    n = _count(size)
    return _out(_r.random_uniform(1 if n is None else n, float(low), float(high)), n)


def normal(loc=0.0, scale=1.0, size=None):
    n = _count(size)
    return _out(_r.random_normal(1 if n is None else n, float(loc), float(scale)), n)


def randn(*shape):
    return normal(0.0, 1.0, shape[0] if shape else None)


def standard_normal(size=None):
    return normal(0.0, 1.0, size)


def randint(low, high=None, size=None):
    if high is None:
        low, high = 0, low
    n = _count(size)
    return _out(_r.random_int(1 if n is None else n, int(low), int(high)), n)


def choice(a, size=None, replace=True):
    if isinstance(a, int):
        a = list(range(a))
    a = asarray(a)
    n = _count(size)
    if not replace:
        if n is None:
            n1 = 1
        else:
            n1 = n
        if n1 > a.size:
            raise ValueError("Cannot take a larger sample than population when 'replace=False'")
        idx = permutation(a.size)[:n1]
    else:
        idx = _r.random_int(1 if n is None else n, 0, a.size)
        idx = ndarray(idx)
    r = a[idx]
    return r[0] if n is None else r


def permutation(x):
    if isinstance(x, int):
        x = list(range(x))
    a = asarray(x).copy()
    shuffle(a)
    return a


def shuffle(x):
    n = len(x)
    for i in range(n - 1, 0, -1):
        j = int(_r.random_int(1, 0, i + 1).get(0))
        x[i], x[j] = x[j], x[i]


class Generator:
    """Générateur à la manière de np.random.default_rng()."""

    def __init__(self, s=None):
        seed(s)

    def random(self, size=None):
        return random(size)

    def uniform(self, low=0.0, high=1.0, size=None):
        return uniform(low, high, size)

    def normal(self, loc=0.0, scale=1.0, size=None):
        return normal(loc, scale, size)

    def standard_normal(self, size=None):
        return normal(0.0, 1.0, size)

    def integers(self, low, high=None, size=None):
        return randint(low, high, size)

    def choice(self, a, size=None, replace=True):
        return choice(a, size, replace)

    def shuffle(self, x):
        shuffle(x)

    def permutation(self, x):
        return permutation(x)


def default_rng(seed=None):
    return Generator(seed)


def __getattr__(name):
    raise AttributeError("np.random." + name + " n'est pas disponible dans ce logiciel")
