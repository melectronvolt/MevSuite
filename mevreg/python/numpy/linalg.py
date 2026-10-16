"""Mini numpy.linalg : norme d'un vecteur."""

from numpy import asarray, sqrt


def norm(x, ord=None):
    x = asarray(x)
    if ord is None or ord == 2:
        return sqrt((abs(x) ** 2).sum())
    if ord == 1:
        return abs(x).sum()
    if ord == float("inf"):
        return abs(x).max()
    raise ValueError("np.linalg.norm : ordre non disponible")


def __getattr__(name):
    raise AttributeError("np.linalg." + name + " n'est pas disponible dans ce logiciel")
