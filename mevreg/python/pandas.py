"""Mini-pandas de MeVReg : plusieurs grandeurs ensemble (DataFrame), lecture et écriture de CSV.

Ce que fait pandas pour des colonnes de nombres : les calculs (moyenne, écart-type…) ignorent les
cases vides (NaN) et l'écart-type est corrigé (n − 1). read_csv devine le séparateur et la virgule
décimale quand sep et decimal ne sont pas donnés.
"""

import copy as _copy
import math as _math
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
    if _math.isfinite(f) and f.is_integer() and abs(f) < 1e15:
        return "%d" % f if isinstance(v, int) else ("%.1f" % f)
    return "%.6g" % f


def _floats(values):
    if isinstance(values, (_np.ndarray, Series)):
        return _np.asarray(values, dtype=float)
    if isinstance(values, (int, float)):
        return _np.array([values], dtype=float)
    return _np.array(list(values), dtype=float)


def _valid(a):
    return a[~_np.isnan(a)]


def _quantile(valeurs, fraction):
    if not 0 <= fraction <= 1:
        raise ValueError("Un quantile doit être compris entre 0 et 1.")
    mesures = _valid(valeurs).astype(float, copy=False)
    return float(_np.quantile(mesures, fraction)) if mesures.size else NA


_INDICATEURS = ["count", "mean", "std", "min", "25%", "50%", "75%", "max"]


def _resume_statistique(valeurs):
    """Filtre les cases vides une fois et détermine les trois quartiles ensemble."""
    mesures = _valid(valeurs).astype(float, copy=False)
    if mesures.size == 0:
        return [0] + [NA] * (len(_INDICATEURS) - 1)
    quartiles = _np.quantile(mesures, [0.25, 0.5, 0.75])
    dispersion = mesures.std(ddof=1) if mesures.size >= 2 else NA
    return [mesures.size, mesures.mean(), dispersion, mesures.min(), *quartiles, mesures.max()]


def _lignes_affichees(effectif):
    """Positions à formater dans la console, même pour une très longue acquisition."""
    if effectif <= 20:
        return list(range(effectif))
    return [*range(10), None, *range(effectif - 10, effectif)]


def _positions(index, labels):
    if len(set(index)) != len(index):
        raise ValueError("L’alignement nécessite des étiquettes de lignes uniques.")
    lookup = {label: i for i, label in enumerate(index)}
    return [lookup.get(k) for k in labels]


class Series(_np.lib.mixins.NDArrayOperatorsMixin):
    """Colonne numérique dont les opérations associent les lignes par leur étiquette."""

    def __init__(self, data=None, index=None, name=None, dtype=None):
        if isinstance(data, dict):
            index = list(data) if index is None else list(index)
            data = [data.get(k, NA) for k in index]
        if isinstance(data, Series) and index is None:
            index = data.index
        data = [] if data is None else data
        values = _np.asarray(data)
        self._values = _np.atleast_1d(values).astype(dtype or (bool if values.dtype.kind == "b" else float))
        if self._values.ndim != 1:
            raise ValueError("Une série attend une seule colonne.")
        self.index = list(range(len(self))) if index is None else list(index)
        if len(self.index) != len(self):
            raise ValueError("Une étiquette est nécessaire par ligne.")
        self.name = name

    def __array__(self, dtype=None, copy=None):
        a = _np.asarray(self._values, dtype=dtype)
        return a.copy() if copy else a

    def __len__(self):
        return len(self._values)

    def __iter__(self):
        return iter(self._values)

    @property
    def dtype(self):
        return self._values.dtype

    @property
    def shape(self):
        return self._values.shape

    @property
    def size(self):
        return len(self)

    @property
    def values(self):
        return self._values.copy()

    def to_numpy(self):
        return self.values

    def tolist(self):
        return self._values.tolist()

    def _aligned(self, labels):
        if len(labels) == 0:
            return self._values[:0]
        if labels == self.index:
            return self._values
        return _np.array([NA if i is None else self._values[i] for i in _positions(self.index, labels)])

    def __array_ufunc__(self, ufunc, method, *args, **kwargs):
        if method != "__call__" or kwargs.get("out") is not None:
            return NotImplemented
        series = [v for v in args if isinstance(v, Series)]
        labels = series[0].index
        if any(v.index != labels for v in series[1:]):
            labels = sorted(set().union(*(v.index for v in series)))
        values = ufunc(*(v._aligned(labels) if isinstance(v, Series) else v for v in args), **kwargs)
        return Series(values, index=labels, name=self.name, dtype=values.dtype)

    def _masque_pour(self, etiquettes):
        """Retrouve le booléen de chaque acquisition, même après un tri."""
        masque = self._aligned(etiquettes)
        if masque.dtype.kind != "b":
            raise ValueError("Les étiquettes du masque ne correspondent pas aux lignes.")
        return masque

    def __getitem__(self, k):
        if isinstance(k, slice):
            return Series(self._values[k], index=self.index[k], name=self.name)
        if isinstance(k, (list, tuple, _np.ndarray, Series)):
            if isinstance(k, Series):
                k = k._masque_pour(self.index) if k.dtype.kind == "b" else k.tolist()
            if _np.asarray(k).dtype.kind == "b":
                labels = [label for label, keep in zip(self.index, k) if keep]
                return Series(self._values[k], index=labels, name=self.name)
            return Series([self[label] for label in k], index=k, name=self.name)
        if k not in self.index:
            raise KeyError(k)
        if self.index.count(k) > 1:
            return self[[label == k for label in self.index]]
        return self._values[self.index.index(k)].item()

    # calculs : cases vides ignorées, écart-type corrigé, comme pandas
    def count(self):
        return int(_np.count_nonzero(~_np.isnan(self._values)))

    def sum(self):
        return float(_np.nansum(self._values))

    def mean(self):
        valeurs = _valid(self._values)
        return float(valeurs.mean()) if valeurs.size else NA

    def median(self):
        return _quantile(self._values, 0.5)

    def std(self, ddof=1):
        v = _valid(self._values)
        return float(v.std(ddof=ddof)) if len(v) > ddof else NA

    def var(self, ddof=1):
        v = _valid(self._values)
        return float(v.var(ddof=ddof)) if len(v) > ddof else NA

    def min(self):
        valeurs = _valid(self._values)
        return float(valeurs.min()) if valeurs.size else NA

    def max(self):
        valeurs = _valid(self._values)
        return float(valeurs.max()) if valeurs.size else NA

    def quantile(self, q=0.5):
        return _quantile(self._values, q)

    def describe(self):
        return Series(_resume_statistique(self._values), index=_INDICATEURS, name=self.name)

    def isna(self):
        return Series(_np.isnan(self._values), index=self.index, name=self.name)

    isnull = isna

    def notna(self):
        return ~self.isna()

    def dropna(self):
        return self[self.notna()]

    def fillna(self, value):
        a = self.values
        return Series(_np.where(_np.isnan(a), value, a), index=self.index, name=self.name)

    def head(self, n=5):
        return self[:n]

    def tail(self, n=5):
        return self[-n:] if n else self[:0]

    def abs(self):
        return Series(_np.abs(self.values), index=self.index, name=self.name)

    def round(self, decimals=0):
        return Series(_np.round(self.values, decimals), index=self.index, name=self.name)

    def cumsum(self):
        # comme pandas : une case vide reste vide, et le cumul continue après elle
        a = self.values
        vide = _np.isnan(a)
        return Series(_np.where(vide, NA, _np.cumsum(_np.where(vide, 0.0, a))), index=self.index, name=self.name)

    def diff(self):
        a = self.values
        return Series(_np.concatenate(([NA], _np.diff(a))) if len(a) else [], index=self.index, name=self.name)

    def unique(self):
        _, indices = _np.unique(self._values, return_index=True)
        return self._values[_np.sort(indices)]

    def to_list(self):
        return self.tolist()

    def items(self):
        labels = self.index if self.index is not None else range(len(self))
        return zip(labels, self.tolist())

    def __repr__(self):
        lignes = _lignes_affichees(len(self))
        etiquettes = {i: str(self.index[i]) for i in lignes if i is not None}
        valeurs = {i: _fmt(self._values[i].item()) for i in etiquettes}
        largeur_index = max(map(len, etiquettes.values()), default=1)
        largeur_valeur = max(map(len, valeurs.values()), default=1)
        texte = []
        for i in lignes:
            if i is None:
                texte.append("...")
            else:
                texte.append(etiquettes[i].ljust(largeur_index) + "    " + valeurs[i].rjust(largeur_valeur))
        nom = f"Name: {self.name}, " if self.name is not None else ""
        texte.append(f"{nom}dtype: {self.dtype}")
        return "\n".join(texte)

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
            return Series([float(df._cols[c][k]) for c in df.columns], index=df.columns, name=df.index[k])
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
            self._index = list(data.index)
            self.attrs = _copy.deepcopy(data.attrs)
        elif isinstance(data, dict):
            series = [v for v in data.values() if isinstance(v, Series)]
            if series and index is None:
                index = series[0].index
                if any(s.index != index for s in series[1:]):
                    index = sorted(set().union(*(s.index for s in series)))
            if index is not None:
                index = list(index)
            for k, v in data.items():
                self._cols[str(k)] = v._aligned(index).copy() if isinstance(v, Series) else _floats(v)
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
            if self._cols and len(self._index) != len(self):
                raise ValueError("Une étiquette est nécessaire par ligne.")

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
        if len(names) != len(set(names)):
            raise ValueError("Deux colonnes portent le même nom.")
        if "unites" in self.attrs:
            units = self.attrs["unites"]
            self.attrs["unites"] = {new: units[old] for old, new in zip(self.columns, names) if old in units}
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
        return _np.column_stack(list(self._cols.values())) if self._cols else _np.empty((len(self), 0))

    @property
    def iloc(self):
        return _ILoc(self)

    def __len__(self):
        for a in self._cols.values():
            return len(a)
        return len(self._index) if self._index is not None else 0

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
        if isinstance(k, (list, tuple)) and (not k or _np.asarray(k).dtype.kind != "b"):
            out = DataFrame({c: self[c] for c in k}, index=self.index)
            out.attrs = _copy.deepcopy(self.attrs)
            if "unites" in out.attrs:
                out.attrs["unites"] = {c: u for c, u in out.attrs["unites"].items() if c in k}
            return out
        if isinstance(k, Series):
            k = k._masque_pour(self.index)
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
            a = v._aligned(self.index) if isinstance(v, Series) and self._cols else _floats(v)
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
        out.attrs = _copy.deepcopy(self.attrs)
        return out

    def reset_index(self, drop=False):
        """Lignes numérotées de nouveau à partir de 0 ; sans drop=True, les anciennes étiquettes
        deviennent la colonne « index »."""
        out = DataFrame({} if drop else {"index": _floats([float(l) if not isinstance(l, str) else NA for l in self.index])})
        for c, a in self._cols.items():
            out._cols[c] = a.copy()
        out._pad()
        out.attrs = _copy.deepcopy(self.attrs)
        return out

    def copy(self):
        return DataFrame(self)

    def head(self, n=5):
        return self._rows(slice(0, n))

    def tail(self, n=5):
        return self._rows(slice(max(len(self) - n, 0), len(self)))

    def rename(self, columns=None):
        columns = columns or {}
        out = self.copy()
        out.columns = [columns.get(c, c) for c in self.columns]
        return out

    def drop(self, labels=None, axis=0, columns=None):
        if columns is None:
            columns = labels if axis in (1, "columns") else None
        if columns is None:
            raise ValueError("drop : seules les colonnes se retirent (columns=[…])")
        if isinstance(columns, str):
            columns = [columns]
        return self[[c for c in self.columns if c not in columns]]

    def dropna(self):
        keep = _np.ones(len(self), dtype=bool)
        for a in self._cols.values():
            keep = keep & ~_np.isnan(a)
        return self[keep]

    def fillna(self, value):
        out = self.copy()
        for c, a in self._cols.items():
            out[c] = _np.where(_np.isnan(a), value, a)
        return out

    def sort_values(self, by, ascending=True):
        a = self._cols[by]
        valid = _np.flatnonzero(~_np.isnan(a))
        order = _np.argsort(a[valid] if ascending else -a[valid], kind="stable")
        return self._rows(_np.concatenate((valid[order], _np.flatnonzero(_np.isnan(a)))))

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
        indicateurs = {nom: _resume_statistique(valeurs) for nom, valeurs in self._cols.items()}
        return DataFrame(indicateurs, index=_INDICATEURS)

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
        lignes = _lignes_affichees(len(self))
        index = self._index if self._index is not None else range(len(self))
        etiquettes = {i: str(index[i]) for i in lignes if i is not None}
        largeur_index = max(map(len, etiquettes.values()), default=1)
        colonnes = {}
        largeurs = {}
        for nom, serie in self._cols.items():
            valeurs = {i: _fmt(serie[i].item()) for i in etiquettes}
            colonnes[nom] = valeurs
            largeurs[nom] = max(len(nom), max(map(len, valeurs.values()), default=0))
        texte = [" " * largeur_index + "".join("  " + nom.rjust(largeurs[nom]) for nom in colonnes)]
        for i in lignes:
            etiquette = "..." if i is None else etiquettes[i].ljust(largeur_index)
            cellules = (valeurs.get(i, "...").rjust(largeurs[nom]) for nom, valeurs in colonnes.items())
            texte.append(etiquette + "".join("  " + cellule for cellule in cellules))
        if len(self) > 20:
            texte.extend(["", f"[{len(self)} rows x {len(colonnes)} columns]"])
        return "\n".join(texte)

    __str__ = __repr__

    def to_dict(self, orient="dict"):
        """Comme pandas : {colonne: {étiquette: valeur}} ; orient="list" : {colonne: [valeurs]} ;
        "records" : une liste de lignes {colonne: valeur} ; "index" : {étiquette: {colonne: valeur}}."""
        labels = self.index
        if orient == "dict":
            return {c: dict(zip(labels, a.tolist())) for c, a in self._cols.items()}
        if orient == "list":
            return {c: a.tolist() for c, a in self._cols.items()}
        rows = [{c: float(self._cols[c][i]) for c in self.columns} for i in range(len(self))]
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
        import csv
        import io
        buffer = io.StringIO(newline="")
        writer = csv.writer(buffer, delimiter=sep, lineterminator="\n")
        if header:
            writer.writerow(([""] if index else []) + self.columns)
        for i, label in enumerate(self.index):
            cells = [num(float(self._cols[c][i])) for c in self.columns]
            writer.writerow(([str(label)] if index else []) + cells)
        text = buffer.getvalue()
        if path_or_buf is None:
            return text
        if hasattr(path_or_buf, "write"):
            path_or_buf.write(text)
            return
        import mevreg
        with open(mevreg._chemin(path_or_buf), "w", encoding="utf-8", newline="") as f:
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


def read_csv(filepath_or_buffer, sep=None, decimal=None, delimiter=None, encoding=None, header="infer", names=None, usecols=None):
    """Lit des mesures avec les séparateurs du tableur et conserve les unités."""
    import mevreg
    if sep is not None and delimiter is not None:
        raise ValueError("Indiquez un seul séparateur.")
    sep = delimiter if delimiter is not None else sep
    if sep is not None and len(sep) != 1 and sep != r"\s+":
        raise ValueError("Le séparateur doit être un caractère.")
    if decimal not in (None, ".", ","):
        raise ValueError("Le signe décimal doit être un point ou une virgule.")
    if header == "infer":
        row = -2 if names is None else -1
    elif header is None:
        row = -1
    elif isinstance(header, int) and header >= 0:
        row = header
    else:
        raise ValueError("header attend un numéro de ligne, None ou 'infer'.")
    path = mevreg._chemin(filepath_or_buffer)
    contents = None
    if hasattr(filepath_or_buffer, "read"):
        contents = filepath_or_buffer.read()
        if isinstance(contents, bytes):
            contents = contents.decode(encoding or "utf-8-sig")
    elif encoding:
        with open(path, encoding=encoding) as f:
            contents = f.read()
    labels, units, cols = _r._lire_csv(path, sep or "", decimal or "", row, contents)
    if names is not None:
        if len(names) != len(cols):
            raise ValueError("Le nombre de noms diffère du nombre de colonnes.")
        labels = list(names)
    if len(set(labels)) != len(labels):
        raise ValueError("Deux colonnes portent le même nom. Donnez des noms distincts avec names.")
    keep = list(range(len(cols)))
    if usecols is not None:
        requested = list(usecols)
        positions = [v if isinstance(v, int) else labels.index(v) for v in requested]
        if any(i < 0 or i >= len(cols) for i in positions):
            raise ValueError("usecols contient une colonne inconnue.")
        keep = [i for i in keep if i in positions]
    df = DataFrame({labels[i]: cols[i] for i in keep})
    df.attrs["unites"] = {str(labels[i]): units[i] for i in keep}
    return df


def concat(objs, axis=0, ignore_index=False):
    """Assemble des lignes ou aligne des colonnes par étiquette."""
    objs = list(objs)
    if not objs:
        raise ValueError("Aucun tableau à concaténer.")
    unites = {}
    if axis in (1, "columns"):
        index = objs[0].index
        if any(tableau.index != index for tableau in objs):
            index = list(dict.fromkeys(cle for tableau in objs for cle in tableau.index))
        colonnes = {}
        for tableau in objs:
            for nom, serie in tableau.items():
                destination = str(len(colonnes)) if ignore_index else nom
                if destination in colonnes:
                    raise ValueError("Renommez la colonne en double : " + nom)
                colonnes[destination] = serie._aligned(index)
                unite = tableau.attrs.get("unites", {}).get(nom)
                if unite:
                    unites[destination] = unite
        resultat = DataFrame(colonnes, index=index)
    elif axis in (0, "index"):
        # Les acquisitions mises bout à bout doivent employer la même unité par colonne.
        colonnes = {}
        for nom in dict.fromkeys(nom for tableau in objs for nom in tableau.columns):
            morceaux = []
            for tableau in objs:
                unite = tableau.attrs.get("unites", {}).get(nom)
                if unite:
                    if nom in unites and unites[nom] != unite:
                        raise ValueError("Unités incompatibles dans la colonne " + nom)
                    unites[nom] = unite
                morceaux.append(tableau._cols[nom] if nom in tableau else _np.full(len(tableau), NA))
            colonnes[nom] = _np.concatenate(morceaux)
        index = None if ignore_index else [cle for tableau in objs for cle in tableau.index]
        resultat = DataFrame(colonnes, index=index)
    else:
        raise ValueError("axis doit valoir 0 ou 1.")
    if unites:
        resultat.attrs["unites"] = unites
    return resultat


def isna(x):
    return _np.isnan(_floats(x)) if not isinstance(x, (int, float)) else x != x


isnull = isna


def notna(x):
    return ~isna(x) if not isinstance(x, (int, float)) else x == x


def to_numeric(x, errors="raise"):
    if errors not in ("raise", "coerce"):
        raise ValueError("errors doit valoir 'raise' ou 'coerce'.")
    def convert(v):
        if v is None:
            return NA
        try:
            return float(v)
        except (ValueError, TypeError):
            if errors == "coerce":
                return NA
            raise
    if _np.isscalar(x):
        return convert(x)
    values = [convert(v) for v in x]
    return Series(values, index=x.index, name=x.name) if isinstance(x, Series) else _np.array(values)
