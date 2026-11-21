"""Exécute un export avec NumPy et recueille les coordonnées envoyées à matplotlib."""

import runpy
import sys
from types import ModuleType

import numpy as np


class Axes:
    def plot(self, x, y, *style, label=None, **options):
        np.testing.assert_equal(np.shape(x), np.shape(y))
        if label is not None:
            pyplot.courbes[label] = x, y

    def errorbar(self, x, y, yerr=None, xerr=None, **options):
        np.testing.assert_equal(np.shape(x), np.shape(y))
        for incertitude in (xerr, yerr):
            if incertitude is not None:
                u = np.asarray(incertitude)
                assert u.ndim == 0 or u.shape in ((len(y),), (2, len(y))), u.shape
                assert not (u < 0).any()

    def get_xlim(self):
        return 0.0, 1.0

    def __getattr__(self, nom):
        # Les appels de mise en page aboutissent ici, sans ouvrir de fenêtre.
        return lambda *args, **options: self


pyplot = ModuleType("matplotlib.pyplot")
pyplot.courbes = {}
pyplot.__getattr__ = lambda nom: getattr(Axes(), nom)
matplotlib = ModuleType("matplotlib")
matplotlib.pyplot = pyplot
sys.modules["matplotlib"] = matplotlib
sys.modules["matplotlib.pyplot"] = pyplot
runpy.run_path(sys.argv[1], run_name="__main__")
