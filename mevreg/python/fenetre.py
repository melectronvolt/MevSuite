"""Fenêtres de dessin de MeVReg : schémas et figures en unités physiques.

    import fenetre as fe
    d = fe.Dessin("Fente", xmin=-0.05, xmax=0.05, ymin=-0.02, ymax=0.02, unite="m", fond="noir")
    d.bandes(I, palette="laser", longueur_onde=633e-9)   # I : intensité selon x
    d.fleche(0, 0, 0.0127, 0, couleur="blanc")
    d.texte(0.013, 0.002, "λD/a", couleur="blanc")
    d.montrer()

Palettes : "gris", "laser" (couleur réelle de la longueur d'onde), "chaud",
"viridis", "bleu-rouge" (valeurs signées).
"""

import json as _json
import _mevreg as _r
import numpy as _np

PALETTES = ("gris", "laser", "chaud", "viridis", "bleu-rouge")

_FR = {
    "noir": "#000000", "blanc": "#ffffff", "rouge": "#ff0000", "vert": "#008000", "bleu": "#0000ff",
    "jaune": "#ffff00", "orange": "#ffa500", "gris": "#808080", "violet": "#800080", "rose": "#ffc0cb",
    "marron": "#a52a2a", "cyan": "#00ffff", "magenta": "#ff00ff", "gris clair": "#d3d3d3",
}


def _couleur(c, defaut="#000000"):
    if c is None:
        return defaut
    if isinstance(c, str) and c.lower() in _FR:
        return _FR[c.lower()]
    from matplotlib.pyplot import _color
    return _color(c) or defaut


def couleur_longueur_onde(lambda_m):
    """Couleur '#rrggbb' perçue pour une lumière de longueur d'onde lambda_m (en mètres)."""
    return _r.wavelength_color(float(lambda_m))


def _palette(p):
    if p not in PALETTES:
        raise ValueError("palette inconnue : %r (palettes : %s)" % (p, ", ".join(PALETTES)))
    return p


class Dessin:
    def __init__(self, nom="Dessin", xmin=-1.0, xmax=1.0, ymin=-1.0, ymax=1.0, unite="", fond="blanc"):
        if xmax <= xmin or ymax <= ymin:
            raise ValueError("fenêtre vide : il faut xmin < xmax et ymin < ymax")
        self.nom = str(nom)
        self.xmin, self.xmax, self.ymin, self.ymax = float(xmin), float(xmax), float(ymin), float(ymax)
        self.unite = str(unite)
        self.fond = _couleur(fond, "#ffffff")
        self._items = []
        self._bufs = []

    def _buf(self, v):
        self._bufs.append(_np.asarray(v, dtype=float)._b)
        return len(self._bufs) - 1

    # -- formes -------------------------------------------------------------------
    def ligne(self, x1, y1, x2, y2, couleur="noir", epaisseur=1.5, style="-"):
        self._items.append({"type": "Line", "x1": float(x1), "y1": float(y1), "x2": float(x2), "y2": float(y2),
                            "color": _couleur(couleur), "width": float(epaisseur), "dash": style})

    def fleche(self, x1, y1, x2, y2, couleur="noir", epaisseur=1.5):
        self._items.append({"type": "Arrow", "x1": float(x1), "y1": float(y1), "x2": float(x2), "y2": float(y2),
                            "color": _couleur(couleur), "width": float(epaisseur)})

    def cercle(self, x, y, r, couleur="noir", epaisseur=1.5, rempli=None):
        self._items.append({"type": "Circle", "x": float(x), "y": float(y), "r": float(r), "color": _couleur(couleur),
                            "width": float(epaisseur), "fill": None if rempli is None else _couleur(rempli)})

    def rectangle(self, x, y, largeur, hauteur, couleur="noir", epaisseur=1.5, rempli=None):
        self._items.append({"type": "Rect", "x": float(x), "y": float(y), "w": float(largeur), "h": float(hauteur),
                            "color": _couleur(couleur), "width": float(epaisseur),
                            "fill": None if rempli is None else _couleur(rempli)})

    def point(self, x, y, couleur="noir", taille=5):
        self._items.append({"type": "Point", "x": float(x), "y": float(y), "color": _couleur(couleur), "size": float(taille)})

    def texte(self, x, y, texte, couleur="noir", taille=12):
        self._items.append({"type": "Text", "x": float(x), "y": float(y), "text": str(texte), "color": _couleur(couleur),
                            "size": float(taille)})

    # -- cartes d'intensité ----------------------------------------------------------
    def bandes(self, profil, palette="gris", longueur_onde=None, normaliser=True):
        """Intensité selon x (de xmin à xmax), répétée verticalement : figure d'une fente."""
        self._items.append({"type": "Bands", "data": self._buf(profil), "palette": _palette(palette),
                            "wavelength": None if longueur_onde is None else float(longueur_onde),
                            "normalize": bool(normaliser)})

    def radial(self, profil, rmax=None, palette="gris", longueur_onde=None, normaliser=True):
        """Intensité selon la distance au centre (de 0 à rmax) : figure d'un trou circulaire."""
        if rmax is None:
            rmax = max(abs(self.xmin), abs(self.xmax), abs(self.ymin), abs(self.ymax))
        self._items.append({"type": "Radial", "data": self._buf(profil), "rmax": float(rmax),
                            "palette": _palette(palette),
                            "wavelength": None if longueur_onde is None else float(longueur_onde),
                            "normalize": bool(normaliser)})

    def carte(self, expression, palette="gris", longueur_onde=None, normaliser=True, nx=500, ny=None, **parametres):
        """Intensité donnée par une expression en x, y (et r), évaluée en C++.

        d.carte("cos(pi*a*x/(l*D))^2 * sinc(pi*b*x/(l*D))^2", a=5e-4, b=1e-4, l=633e-9, D=2)
        """
        if ny is None:
            ny = max(1, int(round(nx * (self.ymax - self.ymin) / (self.xmax - self.xmin))))
        noms = [str(k) for k in parametres]
        valeurs = [float(parametres[k]) for k in parametres]
        b = _r.carte(str(expression), [self.xmin, self.xmax, self.ymin, self.ymax], int(nx), int(ny), noms, valeurs)
        self._bufs.append(b)
        self._items.append({"type": "Map", "data": len(self._bufs) - 1, "nx": int(nx), "ny": int(ny),
                            "palette": _palette(palette),
                            "wavelength": None if longueur_onde is None else float(longueur_onde),
                            "normalize": bool(normaliser)})

    def effacer(self):
        self._items = []
        self._bufs = []

    def montrer(self):
        d = {"name": self.nom, "xmin": self.xmin, "xmax": self.xmax, "ymin": self.ymin, "ymax": self.ymax,
             "unit": self.unite, "background": self.fond, "items": self._items}
        _r.dessin(_json.dumps(d), list(self._bufs))

    show = montrer


def __getattr__(name):
    raise AttributeError("fenetre." + name + " n'est pas disponible dans ce logiciel")
