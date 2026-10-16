"""Mini matplotlib.pyplot de MeVReg.

Même syntaxe que matplotlib, mais le script ne dessine rien lui-même : il
décrit les graphes, que le logiciel dessine ensuite avec son propre moteur.
Les graphes restent interactifs et modifiables à la souris.
"""

import json as _json
import sys as _sys
import _mevreg as _r
import numpy as _np

_CYCLE = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf"]

_NAMED = {
    "b": "#0000ff", "g": "#008000", "r": "#ff0000", "c": "#00bfbf", "m": "#bf00bf", "y": "#bfbf00",
    "k": "#000000", "w": "#ffffff",
    "blue": "#0000ff", "green": "#008000", "red": "#ff0000", "cyan": "#00ffff", "magenta": "#ff00ff",
    "yellow": "#ffff00", "black": "#000000", "white": "#ffffff", "orange": "#ffa500", "purple": "#800080",
    "gray": "#808080", "grey": "#808080", "brown": "#a52a2a", "pink": "#ffc0cb", "olive": "#808000",
    "navy": "#000080", "darkblue": "#00008b", "darkred": "#8b0000", "darkgreen": "#006400",
    "lightblue": "#add8e6", "lightgreen": "#90ee90", "lightgray": "#d3d3d3", "lightgrey": "#d3d3d3",
    "darkgray": "#a9a9a9", "darkgrey": "#a9a9a9", "gold": "#ffd700", "violet": "#ee82ee",
    "indigo": "#4b0082", "turquoise": "#40e0d0", "salmon": "#fa8072", "crimson": "#dc143c",
    "darkorange": "#ff8c00", "teal": "#008080", "lime": "#00ff00", "maroon": "#800000",
    "tab:blue": _CYCLE[0], "tab:orange": _CYCLE[1], "tab:green": _CYCLE[2], "tab:red": _CYCLE[3],
    "tab:purple": _CYCLE[4], "tab:brown": _CYCLE[5], "tab:pink": _CYCLE[6], "tab:gray": _CYCLE[7],
    "tab:olive": _CYCLE[8], "tab:cyan": _CYCLE[9],
}

_MARKERS = ".,ov^<>12348spP*hH+xXDd|_"
_LINESTYLES = {"-": "-", "--": "--", "-.": "-.", ":": ":", "solid": "-", "dashed": "--", "dashdot": "-.",
               "dotted": ":", "None": "", "none": "", " ": "", "": ""}


def _color(c):
    """Couleur matplotlib → '#rrggbb' (None si absente)."""
    if c is None:
        return None
    if isinstance(c, (tuple, list)):
        vals = [float(v) for v in c[:3]]
        return "#%02x%02x%02x" % tuple(int(round(max(0.0, min(1.0, v)) * 255)) for v in vals)
    c = str(c).strip()
    if c.startswith("#") and len(c) in (7, 9):
        return c[:7].lower()
    if len(c) == 2 and c[0] == "C" and c[1].isdigit():
        return _CYCLE[int(c[1])]
    low = c.lower()
    if low in _NAMED:
        return _NAMED[low]
    try:
        g = float(c)
        v = int(round(g * 255))
        return "#%02x%02x%02x" % (v, v, v)
    except ValueError:
        raise ValueError("couleur inconnue : %r" % c)


def _parse_fmt(fmt):
    """Chaîne de format 'ro', 'b+-', 'k--' → (couleur, marqueur, style de trait)."""
    color = marker = None
    linestyle = None
    i = 0
    while i < len(fmt):
        two = fmt[i:i + 2]
        if two in ("--", "-."):
            linestyle = two
            i += 2
            continue
        ch = fmt[i]
        if ch == "C" and i + 1 < len(fmt) and fmt[i + 1].isdigit():
            color = _CYCLE[int(fmt[i + 1])]
            i += 2
            continue
        if ch in "-:":
            linestyle = ch
        elif ch in "bgrcmykw":
            color = _NAMED[ch]
        elif ch in _MARKERS:
            marker = ch
        else:
            raise ValueError("format de tracé inconnu : %r" % fmt)
        i += 1
    if linestyle is None:
        linestyle = "" if marker is not None else "-"
    return color, marker, linestyle


def _name_of(obj):
    """Nom de la variable du script qui désigne ce tableau (ou None)."""
    if obj is None:
        return None
    try:
        f = _sys._getframe(1)
    except Exception:
        return None
    while f is not None:
        if f.f_code.co_filename == "<script>":
            for d in (f.f_locals, f.f_globals):
                for k, v in list(d.items()):
                    if v is obj and not k.startswith("_"):
                        return k
        f = f.f_back
    return None


def _arr(v):
    if v is None:
        return None
    if isinstance(v, _np.ndarray):
        return v
    if isinstance(v, (int, float)):
        return _np.array([float(v)])
    return _np.asarray(v)


class _Artist:
    """Objet renvoyé par les fonctions de tracé (méthodes sans effet)."""

    def __init__(self, series=None):
        self._s = series

    def set_label(self, label):
        if self._s is not None:
            self._s["label"] = str(label)

    def set_color(self, c):
        if self._s is not None:
            self._s["color"] = _color(c)

    def set_data(self, x, y):
        if self._s is not None:
            self._s["xa"] = _arr(x)
            self._s["ya"] = _arr(y)

    def __getattr__(self, name):
        def _noop(*a, **k):
            return None
        return _noop


class Axes:
    def __init__(self, fig, index, twin_of=None):
        self.figure = fig
        self.index = index
        self._series = []
        self._hlines = []
        self._vlines = []
        self._texts = []
        self._title = None
        self._xlabel = None
        self._ylabel = None
        self._xlim = None
        self._ylim = None
        self._grid = False
        self._legend = False
        self._xscale = "linear"
        self._yscale = "linear"
        self._twin = None
        self._twin_of = twin_of
        self._ci = 0

    # couleur suivante, partagée avec l'axe jumeau
    def _next_color(self):
        base = self._twin_of if self._twin_of is not None else self
        c = _CYCLE[base._ci % len(_CYCLE)]
        base._ci += 1
        return c

    def _add(self, kind, x, y, label=None, color=None, marker=None, linestyle=None, linewidth=None,
             markersize=None, xerr=None, yerr=None, width=None):
        ya = _arr(y)
        xa = _arr(x) if x is not None else _np.arange(len(ya))
        if xa.size != ya.size and xa.size != 1 and ya.size != 1:
            raise ValueError(
                "x and y must have same first dimension, but have shapes (%d,) and (%d,)" % (xa.size, ya.size))
        s = {
            "kind": kind, "xa": xa, "ya": ya,
            "xname": _name_of(x), "yname": _name_of(y),
            "label": None if label is None else str(label),
            "color": _color(color) if color is not None else self._next_color(),
            "marker": marker, "linestyle": linestyle,
            "linewidth": linewidth, "markersize": markersize,
            "xerr": _arr(xerr), "yerr": _arr(yerr), "width": width,
            "right": self._twin_of is not None,
        }
        self._series.append(s)
        return _Artist(s)

    def plot(self, *args, **kw):
        args = list(args)
        out = []
        while args:
            if len(args) >= 2 and not isinstance(args[1], str):
                x, y = args[0], args[1]
                args = args[2:]
            else:
                x, y = None, args[0]
                args = args[1:]
            fmt = ""
            if args and isinstance(args[0], str):
                fmt = args.pop(0)
            color, marker, ls = _parse_fmt(fmt)
            color = kw.get("color", kw.get("c", color))
            marker = kw.get("marker", marker)
            ls = _LINESTYLES.get(kw.get("linestyle", kw.get("ls", ls)), ls)
            if kw.get("marker") is not None and "linestyle" not in kw and "ls" not in kw and fmt == "":
                ls = "-"
            out.append(self._add("line", x, y, kw.get("label"), color, marker, ls,
                                 kw.get("linewidth", kw.get("lw")), kw.get("markersize", kw.get("ms"))))
        return out

    def scatter(self, x, y, s=None, c=None, marker="o", label=None, color=None, **kw):
        size = None if s is None else max(2.0, float(s) ** 0.5 * 1.2) if isinstance(s, (int, float)) else None
        return self._add("scatter", x, y, label, color if color is not None else (c if isinstance(c, str) else None),
                         marker or "o", "", None, size)

    def errorbar(self, x, y, yerr=None, xerr=None, fmt="", ecolor=None, capsize=None, label=None, **kw):
        color, marker, ls = _parse_fmt(fmt if fmt not in ("none", "None") else "o")
        if fmt in ("none", "None"):
            marker, ls = None, ""
        color = kw.get("color", kw.get("c", color))
        marker = kw.get("marker", marker)
        ls = _LINESTYLES.get(kw.get("linestyle", kw.get("ls", ls)), ls)
        return self._add("line", x, y, label, color, marker, ls, kw.get("linewidth", kw.get("lw")),
                         kw.get("markersize", kw.get("ms")), xerr, yerr)

    def hist(self, x, bins=10, range=None, color=None, label=None, density=False, **kw):
        if not isinstance(bins, int):
            raise ValueError("plt.hist : donnez un nombre de classes (bins=10)")
        counts, edges = _np.histogram(x, bins, range)
        width = edges[1] - edges[0]
        centers = (edges[:-1] + edges[1:]) / 2
        heights = counts / (counts.sum() * width) if density else counts
        self._add("bar", centers, heights, label, color, None, "", None, None, None, None, width)
        return heights, edges, None

    def bar(self, x, height, width=0.8, color=None, label=None, **kw):
        self._add("bar", x, height, label, color, None, "", None, None, None, None, float(width))
        return _Artist()

    def axhline(self, y=0, color=None, linestyle="-", ls=None, label=None, **kw):
        self._hlines.append({"value": float(y), "color": _color(color) or "#000000",
                             "linestyle": _LINESTYLES.get(ls or linestyle, "-"), "label": label,
                             "right": self._twin_of is not None})
        return _Artist()

    def axvline(self, x=0, color=None, linestyle="-", ls=None, label=None, **kw):
        self._vlines.append({"value": float(x), "color": _color(color) or "#000000",
                             "linestyle": _LINESTYLES.get(ls or linestyle, "-"), "label": label,
                             "right": False})
        return _Artist()

    def text(self, x, y, s, color=None, **kw):
        self._texts.append({"x": float(x), "y": float(y), "text": str(s), "color": _color(color),
                            "right": self._twin_of is not None})
        return _Artist()

    def annotate(self, text, xy, xytext=None, **kw):
        p = xytext if xytext is not None else xy
        return self.text(p[0], p[1], text, kw.get("color"))

    def set_title(self, t, **kw):
        self._title = str(t)

    def set_xlabel(self, t, **kw):
        self._xlabel = str(t)

    def set_ylabel(self, t, **kw):
        self._ylabel = str(t)

    def _lim(self, a, b, kw, lo, hi):
        if isinstance(a, (tuple, list)):
            a, b = a
        a = kw.get(lo, a)
        b = kw.get(hi, b)
        return [None if a is None else float(a), None if b is None else float(b)]

    def set_xlim(self, a=None, b=None, **kw):
        self._xlim = self._lim(a, b, kw, "left", "right")

    def set_ylim(self, a=None, b=None, **kw):
        self._ylim = self._lim(a, b, kw, "bottom", "top")

    def get_xlim(self):
        return tuple(self._xlim) if self._xlim else (0.0, 1.0)

    def get_ylim(self):
        return tuple(self._ylim) if self._ylim else (0.0, 1.0)

    def grid(self, visible=True, *args, **kw):
        self._grid = bool(visible) if visible is not None else True
        if self._twin_of is not None:
            self._twin_of._grid = self._grid

    def legend(self, *args, **kw):
        base = self._twin_of if self._twin_of is not None else self
        base._legend = True
        if args and isinstance(args[0], (list, tuple)):
            for s, lab in zip(self._series, args[0]):
                s["label"] = str(lab)

    def set_xscale(self, v, **kw):
        self._xscale = v

    def set_yscale(self, v, **kw):
        self._yscale = v

    def twinx(self):
        if self._twin is None:
            self._twin = Axes(self.figure, self.index, twin_of=self)
        return self._twin

    # réglages purement cosmétiques : acceptés sans effet
    def _noop(self, *a, **k):
        return None

    set_aspect = tick_params = minorticks_on = minorticks_off = set_xticks = set_yticks = _noop
    set_xticklabels = set_yticklabels = axis = margins = locator_params = set_facecolor = _noop

    def _desc(self, bufs):
        def buf(a):
            if a is None:
                return None
            bufs.append(a._b)
            return len(bufs) - 1

        def series(s):
            return {
                "kind": s["kind"], "x": buf(s["xa"]), "y": buf(s["ya"]),
                "xerr": buf(s["xerr"]), "yerr": buf(s["yerr"]),
                "xname": s["xname"], "yname": s["yname"], "label": s["label"], "color": s["color"],
                "marker": s["marker"], "linestyle": s["linestyle"],
                "linewidth": None if s["linewidth"] is None else float(s["linewidth"]),
                "markersize": None if s["markersize"] is None else float(s["markersize"]),
                "right": s["right"], "width": None if s["width"] is None else float(s["width"]),
            }

        allseries = list(self._series)
        hl = list(self._hlines)
        tx = list(self._texts)
        y2label = None
        y2lim = None
        y2scale = "linear"
        if self._twin is not None:
            allseries += self._twin._series
            hl += self._twin._hlines
            tx += self._twin._texts
            y2label = self._twin._ylabel
            y2lim = self._twin._ylim
            y2scale = self._twin._yscale
        return {
            "index": self.index, "title": self._title, "xlabel": self._xlabel, "ylabel": self._ylabel,
            "y2label": y2label, "xlim": self._xlim, "ylim": self._ylim, "y2lim": y2lim,
            "grid": self._grid, "legend": self._legend, "xscale": self._xscale, "yscale": self._yscale,
            "y2scale": y2scale, "series": [series(s) for s in allseries], "hlines": hl,
            "vlines": list(self._vlines), "texts": tx,
        }

    def _empty(self):
        return not (self._series or self._hlines or self._vlines or self._texts or
                    (self._twin is not None and not self._twin._empty()))

    def __getattr__(self, name):
        raise AttributeError("Axes." + name + " n'est pas disponible dans ce logiciel")


class _AxesArray:
    """Tableau d'axes renvoyé par subplots : ax[0], ax[1, 0], ax[1][0]."""

    def __init__(self, rows, cols, axes):
        self._rows = rows
        self._cols = cols
        self._axes = axes

    def __getitem__(self, k):
        if isinstance(k, tuple):
            r, c = k
            return self._axes[r * self._cols + c]
        if self._rows > 1 and self._cols > 1:
            return _AxesArray(1, self._cols, self._axes[k * self._cols:(k + 1) * self._cols])
        return self._axes[k]

    def __iter__(self):
        if self._rows > 1 and self._cols > 1:
            return iter([self[i] for i in range(self._rows)])
        return iter(self._axes)

    def __len__(self):
        return self._rows if (self._rows > 1 and self._cols > 1) else len(self._axes)

    def flatten(self):
        return list(self._axes)

    ravel = flatten

    @property
    def flat(self):
        return iter(self._axes)


class Figure:
    def __init__(self, name):
        self.name = name
        self._axes = {}
        self.rows = 1
        self.cols = 1
        self._current = None
        self._suptitle = None

    def add_subplot(self, *args, **kw):
        if len(args) == 0:
            r, c, i = 1, 1, 1
        elif len(args) == 1:
            n = int(args[0])
            r, c, i = n // 100, (n // 10) % 10, n % 10
        else:
            r, c, i = int(args[0]), int(args[1]), int(args[2])
        if r * c > 16:
            raise ValueError("au plus 16 graphes par figure")
        self.rows = max(self.rows, r) if self._axes else r
        self.cols = max(self.cols, c) if self._axes else c
        if i not in self._axes:
            self._axes[i] = Axes(self, i)
        self._current = self._axes[i]
        return self._current

    def gca(self):
        if self._current is None:
            return self.add_subplot(1, 1, 1)
        return self._current

    def suptitle(self, t, **kw):
        self._suptitle = str(t)

    def tight_layout(self, *a, **k):
        pass

    def savefig(self, fname, **kw):
        desc, bufs = self._desc()
        _r.savefig(desc, bufs, str(fname))

    def show(self):
        _send(self)

    def _desc(self):
        bufs = []
        axes = [self._axes[k]._desc(bufs) for k in sorted(self._axes)]
        d = {"name": self.name, "rows": self.rows, "cols": self.cols, "suptitle": self._suptitle, "axes": axes}
        return _json.dumps(d), bufs

    def _empty(self):
        return all(a._empty() for a in self._axes.values())

    def __getattr__(self, name):
        def _noop(*a, **k):
            return None
        if name.startswith("set_") or name in ("subplots_adjust", "align_labels", "colorbar"):
            return _noop
        raise AttributeError("Figure." + name + " n'est pas disponible dans ce logiciel")


_figs = {}
_order = []
_current = None
_counter = [0]


def _reset():
    global _current
    _figs.clear()
    del _order[:]
    _current = None
    _counter[0] = 0


def _send(fig):
    if fig._empty():
        return
    desc, bufs = fig._desc()
    _r.show(desc, bufs)


def _finish():
    """Fin du script : les figures non montrées sont affichées quand même."""
    for name in list(_order):
        _send(_figs[name])
    _reset()


def figure(num=None, figsize=None, **kw):
    global _current
    if num is None:
        _counter[0] += 1
        num = _counter[0]
        while ("Figure %d" % num) in _figs:
            num += 1
            _counter[0] = num
    name = num if isinstance(num, str) else "Figure %d" % int(num)
    if name not in _figs:
        _figs[name] = Figure(name)
        _order.append(name)
    _current = _figs[name]
    return _current


def gcf():
    if _current is None:
        return figure()
    return _current


def gca():
    return gcf().gca()


def subplot(*args, **kw):
    return gcf().add_subplot(*args)


def subplots(nrows=1, ncols=1, sharex=False, sharey=False, figsize=None, **kw):
    fig = figure()
    axes = []
    for i in range(nrows * ncols):
        axes.append(fig.add_subplot(nrows, ncols, i + 1))
    fig._current = axes[0]
    if nrows * ncols == 1:
        return fig, axes[0]
    return fig, _AxesArray(nrows, ncols, axes)


def plot(*args, **kw):
    return gca().plot(*args, **kw)


def scatter(*args, **kw):
    return gca().scatter(*args, **kw)


def errorbar(*args, **kw):
    return gca().errorbar(*args, **kw)


def hist(*args, **kw):
    return gca().hist(*args, **kw)


def bar(*args, **kw):
    return gca().bar(*args, **kw)


def axhline(*args, **kw):
    return gca().axhline(*args, **kw)


def axvline(*args, **kw):
    return gca().axvline(*args, **kw)


def text(*args, **kw):
    return gca().text(*args, **kw)


def annotate(*args, **kw):
    return gca().annotate(*args, **kw)


def title(t, **kw):
    gca().set_title(t)


def xlabel(t, **kw):
    gca().set_xlabel(t)


def ylabel(t, **kw):
    gca().set_ylabel(t)


def xlim(*args, **kw):
    if not args and not kw:
        return gca().get_xlim()
    gca().set_xlim(*args, **kw)


def ylim(*args, **kw):
    if not args and not kw:
        return gca().get_ylim()
    gca().set_ylim(*args, **kw)


def grid(visible=True, *args, **kw):
    gca().grid(visible)


def legend(*args, **kw):
    gca().legend(*args, **kw)


def xscale(v, **kw):
    gca().set_xscale(v)


def yscale(v, **kw):
    gca().set_yscale(v)


def loglog(*args, **kw):
    ax = gca()
    ax.set_xscale("log")
    ax.set_yscale("log")
    return ax.plot(*args, **kw)


def semilogx(*args, **kw):
    ax = gca()
    ax.set_xscale("log")
    return ax.plot(*args, **kw)


def semilogy(*args, **kw):
    ax = gca()
    ax.set_yscale("log")
    return ax.plot(*args, **kw)


def twinx(ax=None):
    return (ax or gca()).twinx()


def suptitle(t, **kw):
    gcf().suptitle(t)


def tight_layout(*a, **k):
    pass


def savefig(fname, **kw):
    gcf().savefig(fname)


def show(*args, **kw):
    for name in list(_order):
        _send(_figs[name])
    _reset()


def close(fig=None):
    global _current
    if fig is None and _current is not None:
        name = _current.name
    elif isinstance(fig, Figure):
        name = fig.name
    elif fig == "all":
        _reset()
        return
    else:
        name = fig if isinstance(fig, str) else "Figure %d" % int(fig)
    if name in _figs:
        del _figs[name]
        _order.remove(name)
    if _current is not None and _current.name == name:
        _current = None


def clf():
    f = gcf()
    f._axes.clear()
    f._current = None


def cla():
    f = gcf()
    ax = f.gca()
    f._axes[ax.index] = Axes(f, ax.index)
    f._current = f._axes[ax.index]


def _noop(*a, **k):
    return None


xticks = yticks = minorticks_on = minorticks_off = axis = margins = subplots_adjust = ion = ioff = pause = _noop
draw = tick_params = _noop


def __getattr__(name):
    raise AttributeError("plt." + name + " n'est pas disponible dans ce logiciel")
