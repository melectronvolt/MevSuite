"""Mini-pandas de MeVReg : plusieurs grandeurs ensemble (DataFrame), lecture et écriture de CSV.

    import pandas as pd
    df = pd.read_csv("mesures.csv")          # séparateurs devinés, comme l'import de MeVReg
    df["v"] = np.gradient(df["x"], df["t"])
    print(df.describe())
    df.plot(x="t", y=["x", "v"])
    df.to_csv("resultats.csv", sep=";", decimal=",", index=False)

Ce que fait pandas pour des colonnes de nombres : les calculs (moyenne, écart-type…) ignorent les
cases vides (NaN) et l'écart-type est corrigé (n − 1). read_csv devine le séparateur et la virgule
décimale quand sep et decimal ne sont pas donnés.
"""

import numpy as _np
import _mevreg as _r

__version__ = "mevreg"
NA = float("nan")
nan = NA


def _fmt(v):
    if isinstance(v, str):
        return v
    try:
        f = float(v)
    except (TypeError, ValueError):
        return str(v)
    if f != f:
        return "NaN"
    if f == int(f) and abs(f) < 1e15:
        return "%d" % f if isinstance(v, int) else ("%.1f" % f)
    return "%.6g" % f


def _floats(values):
    if isinstance(values, _np.ndarray):
        return values.astype(float)
    if isinstance(values, (int, float)):
        return _np.array([values], dtype=float)
    return _np.array(list(values), dtype=float)


def _valid(a):
    return a[~_np.isnan(a)]


def _quantile(a, q):
    v = _np.sort(_valid(a))
    n = len(v)
    if n == 0:
        return NA
    pos = (n - 1) * q
    lo = int(pos)
    hi = min(lo + 1, n - 1)
    return float(v[lo] + (v[hi] - v[lo]) * (pos - lo))


class Series(_np.ndarray):
    """Une colonne de nombres, avec son nom (et des étiquettes, pour les résultats par colonne)."""

    __slots__ = ("name", "index")

    def __init__(self, data=None, index=None, name=None, dtype=None):
        if isinstance(data, dict):
            index = list(data.keys())
            data = list(data.values())
        if data is None:
            data = []
        a = _floats(data).copy()
        _np.ndarray.__init__(self, a._b)
        self.name = name
        self.index = list(index) if index is not None else None

    @property
    def values(self):
        return _np.ndarray(self._b.copy())

    def to_numpy(self):
        return self.values

    def tolist(self):
        return self._b.tolist()

    def __getitem__(self, k):
        if isinstance(k, str):
            if self.index is None or k not in self.index:
                raise KeyError(k)
            return float(self._b.get(self.index.index(k)))
        return _np.ndarray.__getitem__(self, k)

    # calculs : cases vides ignorées, écart-type corrigé, comme pandas
    def count(self):
        return int(len(_valid(self.values)))

    def sum(self):
        return float(_np.nansum(self.values))

    def mean(self):
        return float(_np.nanmean(self.values))

    def median(self):
        return _quantile(self.values, 0.5)

    def std(self, ddof=1):
        v = _valid(self.values)
        return float(v.std(ddof=ddof)) if len(v) > ddof else NA

    def var(self, ddof=1):
        v = _valid(self.values)
        return float(v.var(ddof=ddof)) if len(v) > ddof else NA

    def min(self):
        return float(_np.nanmin(self.values))

    def max(self):
        return float(_np.nanmax(self.values))

    def quantile(self, q=0.5):
        return _quantile(self.values, q)

    def describe(self):
        a = self.values
        return Series([self.count(), self.mean(), self.std(), self.min(), _quantile(a, 0.25), _quantile(a, 0.5), _quantile(a, 0.75), self.max()],
                      index=["count", "mean", "std", "min", "25%", "50%", "75%", "max"], name=self.name)

    def isna(self):
        return _np.isnan(self.values)

    isnull = isna

    def notna(self):
        return ~_np.isnan(self.values)

    def dropna(self):
        return Series(_valid(self.values), name=self.name)

    def fillna(self, value):
        a = self.values
        return Series(_np.where(_np.isnan(a), value, a), name=self.name)

    def head(self, n=5):
        return Series(self.values[:n], name=self.name)

    def tail(self, n=5):
        return Series(self.values[-n:] if n else [], name=self.name)

    def abs(self):
        return Series(_np.abs(self.values), name=self.name)

    def round(self, decimals=0):
        return Series(_np.round(self.values, decimals), name=self.name)

    def cumsum(self):
        # comme pandas : une case vide reste vide, et le cumul continue après elle
        a = self.values
        vide = _np.isnan(a)
        return Series(_np.where(vide, NA, _np.cumsum(_np.where(vide, 0.0, a))), index=self.index, name=self.name)

    def diff(self):
        a = self.values
        return Series(_np.concatenate(([NA], _np.diff(a))), name=self.name)

    def unique(self):
        return _np.unique(self.values)

    def to_list(self):
        return self.tolist()

    def items(self):
        labels = self.index if self.index is not None else range(len(self))
        return zip(labels, self.tolist())

    def __repr__(self):
        labels = [str(k) for k in (self.index if self.index is not None else range(len(self)))]
        vals = [_fmt(v) for v in self.tolist()]
        lines = []
        rows = list(range(len(vals)))
        if len(rows) > 20:
            rows = rows[:10] + [None] + rows[-10:]
        wl = max([len(l) for l in labels] + [1])
        wv = max([len(v) for v in vals] + [1])
        for i in rows:
            if i is None:
                lines.append("...")
            else:
                lines.append(labels[i].ljust(wl) + "    " + vals[i].rjust(wv))
        tail = ("Name: %s, " % self.name if self.name is not None else "") + "dtype: float64"
        return "\n".join(lines + [tail])

    __str__ = __repr__


class _ILoc:
    def __init__(self, df):
        self._df = df

    def __getitem__(self, k):
        df = self._df
        if isinstance(k, tuple):
            rows, cols = k
            sub = self[rows]
            names = df.columns
            if isinstance(cols, int):
                return sub[names[cols]]
            chosen = names[cols] if isinstance(cols, slice) else [names[c] for c in cols]
            return sub[chosen] if isinstance(sub, DataFrame) else Series([sub[n] for n in chosen], index=chosen)
        if isinstance(k, int):
            n = len(df)
            if k < 0:
                k += n
            if not 0 <= k < n:
                raise IndexError("ligne %d : il y en a %d" % (k, n))
            return Series([float(df._cols[c]._b.get(k)) for c in df.columns], index=df.columns, name=k)
        return df._rows(k)


class DataFrame:
    """Plusieurs colonnes de nombres de même longueur, chacune avec son nom."""

    def __init__(self, data=None, columns=None, index=None):
        self._cols = {}
        self._index = None
        self.attrs = {}
        if data is None:
            data = {}
        if isinstance(data, DataFrame):
            for c in data.columns:
                self._cols[c] = data._cols[c].copy()
            self._index = data._index
            self.attrs = dict(data.attrs)
        elif isinstance(data, dict):
            for k, v in data.items():
                self._cols[str(k)] = _floats(v)
        else:
            rows = [list(r) for r in data]
            ncol = max([len(r) for r in rows] + [0])
            names = list(columns) if columns is not None else [str(j) for j in range(ncol)]
            for j, name in enumerate(names):
                self._cols[str(name)] = _floats([r[j] if j < len(r) else NA for r in rows])
            columns = None
        if columns is not None:
            self._cols = {str(c): self._cols.get(str(c), _np.full(0, NA)) for c in columns}
        self._pad()
        if index is not None:
            self._index = list(index)

    def _pad(self):
        # colonnes de longueurs différentes : complétées par des cases vides
        n = max([len(a) for a in self._cols.values()] + [0])
        for c, a in self._cols.items():
            if len(a) < n:
                self._cols[c] = _np.concatenate((a, _np.full(n - len(a), NA)))

    # --- description ---
    @property
    def columns(self):
        return list(self._cols)

    @columns.setter
    def columns(self, names):
        names = [str(n) for n in names]
        if len(names) != len(self._cols):
            raise ValueError("%d noms pour %d colonnes" % (len(names), len(self._cols)))
        self._cols = dict(zip(names, self._cols.values()))

    @property
    def shape(self):
        return (len(self), len(self._cols))

    @property
    def empty(self):
        return len(self) == 0 or not self._cols

    @property
    def index(self):
        return self._index if self._index is not None else list(range(len(self)))

    @property
    def values(self):
        return [[float(self._cols[c]._b.get(i)) for c in self.columns] for i in range(len(self))]

    @property
    def iloc(self):
        return _ILoc(self)

    def __len__(self):
        for a in self._cols.values():
            return len(a)
        return 0

    def __contains__(self, name):
        return name in self._cols

    def __iter__(self):
        return iter(self.columns)

    def keys(self):
        return self.columns

    def items(self):
        return [(c, Series(self._cols[c], index=self._index, name=c)) for c in self.columns]

    # --- colonnes ---
    def __getitem__(self, k):
        if isinstance(k, str):
            if k not in self._cols:
                raise KeyError("pas de colonne « %s » (colonnes : %s)" % (k, ", ".join(self._cols)))
            return Series(self._cols[k], index=self._index, name=k)
        if isinstance(k, (list, tuple)):
            return DataFrame({c: self[c] for c in k}, index=self._index)
        # tranche, ou tableau de booléens : les lignes où il est vrai
        return self._rows(k)

    def __getattr__(self, name):
        cols = self.__dict__.get("_cols", {})
        if name in cols:
            return Series(cols[name], index=self.__dict__.get("_index"), name=name)
        raise AttributeError("DataFrame : pas de colonne ni de fonction « %s »" % name)

    def __setitem__(self, k, v):
        n = len(self)
        if isinstance(v, (int, float)):
            self._cols[str(k)] = _np.full(max(n, 1) if not self._cols else n, float(v))
        else:
            a = _floats(v)
            if self._cols and len(a) != n:
                raise ValueError("la colonne « %s » a %d valeurs pour %d lignes" % (k, len(a), n))
            self._cols[str(k)] = a

    def __delitem__(self, k):
        del self._cols[k]

    def _rows(self, k):
        """Lignes choisies par une tranche, des booléens ou des positions, avec leurs étiquettes
        (comme pandas : filtrer ou trier ne renumérote pas les lignes)."""
        labels = self.index
        if isinstance(k, slice):
            kept = labels[k]
        else:
            k = _np.array(k)
            if k.dtype == "bool":
                kept = [l for l, m in zip(labels, k.tolist()) if m]
            else:
                kept = [labels[int(i)] for i in k.tolist()]
        out = DataFrame({c: a[k] for c, a in self._cols.items()}, index=kept)
        out.attrs = dict(self.attrs)
        return out

    def reset_index(self, drop=False):
        """Lignes numérotées de nouveau à partir de 0 ; sans drop=True, les anciennes étiquettes
        deviennent la colonne « index »."""
        out = DataFrame({} if drop else {"index": _floats([float(l) if not isinstance(l, str) else NA for l in self.index])})
        for c, a in self._cols.items():
            out._cols[c] = a.copy()
        out._pad()
        out.attrs = dict(self.attrs)
        return out

    def copy(self):
        return DataFrame(self)

    def head(self, n=5):
        return self._rows(slice(0, n))

    def tail(self, n=5):
        return self._rows(slice(max(len(self) - n, 0), len(self)))

    def rename(self, columns=None):
        columns = columns or {}
        out = DataFrame({columns.get(c, c): a for c, a in self._cols.items()})
        out.attrs = dict(self.attrs)
        return out

    def drop(self, labels=None, axis=0, columns=None):
        if columns is None:
            columns = labels if axis in (1, "columns") else None
        if columns is None:
            raise ValueError("drop : seules les colonnes se retirent (columns=[…])")
        if isinstance(columns, str):
            columns = [columns]
        return DataFrame({c: a for c, a in self._cols.items() if c not in columns})

    def dropna(self):
        keep = _np.ones(len(self), dtype=bool)
        for a in self._cols.values():
            keep = keep & ~_np.isnan(a)
        return self[keep]

    def fillna(self, value):
        return DataFrame({c: _np.where(_np.isnan(a), value, a) for c, a in self._cols.items()})

    def sort_values(self, by, ascending=True):
        order = _np.argsort(self._cols[by])
        if not ascending:
            order = order[::-1]
        return self._rows(order)

    def assign(self, **colonnes):
        out = self.copy()
        for k, v in colonnes.items():
            out[k] = v(out) if callable(v) else v
        return out

    # --- calculs par colonne ---
    def _per_column(self, fn):
        return Series([fn(Series(a)) for a in self._cols.values()], index=self.columns)

    def count(self):
        return self._per_column(lambda s: s.count())

    def sum(self):
        return self._per_column(lambda s: s.sum())

    def mean(self):
        return self._per_column(lambda s: s.mean())

    def median(self):
        return self._per_column(lambda s: s.median())

    def std(self, ddof=1):
        return self._per_column(lambda s: s.std(ddof))

    def var(self, ddof=1):
        return self._per_column(lambda s: s.var(ddof))

    def min(self):
        return self._per_column(lambda s: s.min())

    def max(self):
        return self._per_column(lambda s: s.max())

    def describe(self):
        stats = {c: Series(a).describe() for c, a in self._cols.items()}
        return DataFrame({c: s.values for c, s in stats.items()}, index=["count", "mean", "std", "min", "25%", "50%", "75%", "max"])

    def corr(self):
        cols = self.columns
        out = {}
        for c in cols:
            vals = []
            for d in cols:
                a, b = self._cols[c], self._cols[d]
                ok = ~(_np.isnan(a) | _np.isnan(b))
                x, y = a[ok], b[ok]
                sx, sy = x.std(), y.std()
                vals.append(float(((x - x.mean()) * (y - y.mean())).mean() / (sx * sy)) if sx > 0 and sy > 0 else NA)
            out[c] = vals
        return DataFrame(out, index=cols)

    # --- affichage ---
    def __repr__(self):
        cols = self.columns
        labels = [str(k) for k in self.index]
        table = [[_fmt(v) for v in self._cols[c].tolist()] for c in cols]
        rows = list(range(len(self)))
        if len(rows) > 20:
            rows = rows[:10] + [None] + rows[-10:]
        wl = max([len(l) for l in labels] + [1])
        widths = [max([len(c)] + [len(v) for v in table[j]]) for j, c in enumerate(cols)]
        lines = [" " * wl + "".join("  " + c.rjust(widths[j]) for j, c in enumerate(cols))]
        for i in rows:
            if i is None:
                lines.append("..." + "".join("  " + "...".rjust(widths[j]) for j in range(len(cols))))
            else:
                lines.append(labels[i].ljust(wl) + "".join("  " + table[j][i].rjust(widths[j]) for j in range(len(cols))))
        if len(self) > 20:
            lines.append("")
            lines.append("[%d rows x %d columns]" % (len(self), len(cols)))
        return "\n".join(lines)

    __str__ = __repr__

    def to_dict(self, orient="dict"):
        """Comme pandas : {colonne: {étiquette: valeur}} ; orient="list" : {colonne: [valeurs]} ;
        "records" : une liste de lignes {colonne: valeur} ; "index" : {étiquette: {colonne: valeur}}."""
        labels = self.index
        if orient == "dict":
            return {c: dict(zip(labels, a.tolist())) for c, a in self._cols.items()}
        if orient == "list":
            return {c: a.tolist() for c, a in self._cols.items()}
        rows = [{c: float(self._cols[c]._b.get(i)) for c in self.columns} for i in range(len(self))]
        if orient == "records":
            return rows
        if orient == "index":
            return dict(zip(labels, rows))
        raise ValueError("to_dict : orient vaut \"dict\", \"list\", \"records\" ou \"index\"")

    def to_numpy(self):
        return self.values

    # --- fichiers et graphes ---
    def to_csv(self, path_or_buf=None, sep=",", decimal=".", index=True, header=True, float_format=None):
        def num(v):
            if v != v:
                return ""
            s = (float_format % v) if float_format else repr(float(v))
            if s.endswith(".0"):
                s = s[:-2]
            return s.replace(".", decimal) if decimal != "." else s
        lines = []
        if header:
            lines.append(sep.join(([""] if index else []) + self.columns))
        for i, label in enumerate(self.index):
            cells = [num(float(self._cols[c]._b.get(i))) for c in self.columns]
            lines.append(sep.join(([str(label)] if index else []) + cells))
        text = "\n".join(lines) + "\n"
        if path_or_buf is None:
            return text
        import mevreg
        with open(mevreg._chemin(path_or_buf), "w", encoding="utf-8") as f:
            f.write(text)

    def plot(self, x=None, y=None, kind="line", style=None, title=None, grid=False, xlabel=None, ylabel=None):
        """Graphe des colonnes y en fonction de x (avec matplotlib)."""
        import matplotlib.pyplot as plt
        xs = self[x] if x is not None else _np.arange(len(self))
        ys = [y] if isinstance(y, str) else (y if y is not None else [c for c in self.columns if c != x])
        fig, ax = plt.subplots()
        for c in ys:
            if kind == "scatter":
                ax.scatter(xs, self[c], label=c)
            elif style:
                ax.plot(xs, self[c], style, label=c)
            elif kind == "line":
                ax.plot(xs, self[c], label=c)
            else:
                ax.plot(xs, self[c], "+", label=c)
        if x is not None:
            ax.set_xlabel(xlabel or x)
        if ylabel:
            ax.set_ylabel(ylabel)
        if title:
            ax.set_title(title)
        if grid:
            ax.grid()
        ax.legend()
        return ax


def read_csv(filepath_or_buffer, sep=None, decimal=None, delimiter=None, encoding=None, header="infer", **kwargs):
    """Lit un fichier CSV : sep et decimal devinés s'ils ne sont pas donnés (comme l'import de
    MeVReg) ; les unités des en-têtes (« t (s) ») sont gardées dans df.attrs["unites"]."""
    import mevreg
    sep = delimiter if delimiter is not None else sep
    names, units, cols = _r._lire_csv(mevreg._chemin(filepath_or_buffer), sep or "", decimal or "")
    df = DataFrame({n: _np._wrap(b) for n, b in zip(names, cols)})
    df.attrs["unites"] = dict(zip(names, units))
    return df


def concat(objs, axis=0):
    """Colonnes mises bout à bout (axis=0) ou côte à côte (axis=1)."""
    objs = list(objs)
    if axis in (1, "columns"):
        out = {}
        for o in objs:
            for c in o.columns:
                out[c] = o._cols[c]
        return DataFrame(out)
    cols = []
    for o in objs:
        for c in o.columns:
            if c not in cols:
                cols.append(c)
    return DataFrame({c: _np.concatenate([o._cols[c] if c in o._cols else _np.full(len(o), NA) for o in objs]) for c in cols})


def isna(x):
    return _np.isnan(_floats(x)) if not isinstance(x, (int, float)) else x != x


isnull = isna


def notna(x):
    return ~isna(x) if not isinstance(x, (int, float)) else x == x


def to_numeric(x, errors="raise"):
    return _floats(x)
