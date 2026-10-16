"""Mini numpy.fft : transformée de Fourier discrète (calculée en C++)."""

import _mevreg as _r
from numpy import ndarray, asarray, concatenate, _buf


def fft(a, n=None):
    return ndarray(_buf(a).fft(False, n))


def ifft(a, n=None):
    return ndarray(_buf(a).fft(True, n))


def rfft(a, n=None):
    b = _buf(a)
    size = b.len() if n is None else int(n)
    return ndarray(b.fft(False, n).slice(0, size // 2 + 1, 1))


def fftfreq(n, d=1.0):
    return ndarray(_r.fftfreq(int(n), float(d), False))


def rfftfreq(n, d=1.0):
    return ndarray(_r.fftfreq(int(n), float(d), True))


def fftshift(x):
    x = asarray(x)
    k = x.size // 2
    if k == 0:
        return x.copy()
    return concatenate([x[-k:], x[:-k]])


def ifftshift(x):
    x = asarray(x)
    k = x.size - x.size // 2
    if k == 0 or k == x.size:
        return x.copy()
    return concatenate([x[-k:], x[:-k]])


def __getattr__(name):
    raise AttributeError("np.fft." + name + " n'est pas disponible dans ce logiciel")
