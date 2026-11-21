"""Mini matplotlib de MeVReg : seul matplotlib.pyplot est disponible."""

__version__ = "3.9-mevreg"
rcParams = {}


def use(*args, **kwargs):
    pass


def __getattr__(name):
    raise AttributeError("matplotlib." + name + " n'est pas disponible dans ce logiciel")
