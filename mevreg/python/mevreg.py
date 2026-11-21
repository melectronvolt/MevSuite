"""MeVReg dans les scripts : grandeurs, tableurs, graphes, modélisation, fenêtres, fichiers.

Les lectures (valeurs, unité, liste des grandeurs) se font tout de suite. Les changements du
document et des graphes sont faits à la fin du script, dans l'ordre où ils ont été demandés, puis
ceux des fenêtres et des fichiers.
"""

import json as _json
import _mevreg as _r
import numpy as _np

# dossier du document (chemins relatifs des fichiers), vide s'il n'est pas encore enregistré
_dossier = ""
_precedentes = set()

_MODELES = {
    "constante": "Constant", "lineaire": "Linear", "linéaire": "Linear", "affine": "Affine",
    "polynome": "Polynomial", "polynôme": "Polynomial", "parabole": "Parabola", "puissance": "Power",
    "exponentielle": "ExpX", "exp": "ExpX", "exp_inverse": "ExpInvX", "charge": "Charge",
    "decharge": "Discharge", "décharge": "Discharge", "ln": "Ln", "log": "Log10", "log10": "Log10",
    "sinus": "Sine", "cosinus": "Cosine", "sinus_amorti": "DampedSine", "cosinus_amorti": "DampedCosine",
}


def _debut(dossier, precedentes):
    global _dossier
    _dossier = dossier
    _precedentes.clear()
    _precedentes.update(precedentes)


def _chemin(chemin):
    """Chemin relatif rapporté au dossier du document."""
    import os
    chemin = str(chemin)
    if _dossier and not os.path.isabs(chemin):
        return os.path.join(_dossier, chemin)
    return chemin


def _ordre(ordre, **arguments):
    arguments = {k: v for k, v in arguments.items() if v is not None}
    arguments["ordre"] = ordre
    _r._commande(_json.dumps(arguments))


def _nom(g):
    if not isinstance(g, str):
        raise TypeError("nom de grandeur attendu entre guillemets, par exemple \"t\"")
    return g


def _noms(gs):
    if isinstance(gs, str):
        return [gs]
    return [_nom(g) for g in gs]


def _existe(nom):
    if nom not in grandeurs():
        raise KeyError("pas de grandeur « %s » (grandeurs : %s)" % (nom, ", ".join(grandeurs()) or "aucune"))


# --- grandeurs -------------------------------------------------------------------------------

def grandeurs():
    """Noms des grandeurs du document, dans l'ordre."""
    return _r._document("grandeurs")


def valeurs(nom):
    """Valeurs de la grandeur (tableau numpy)."""
    return _np.array(_r._document("valeurs", _nom(nom)), dtype=float)


def unite(nom):
    """Unité de la grandeur (« m/s »…), vide s'il n'y en a pas."""
    return _r._document("unite", _nom(nom))


def parametres():
    """Paramètres du document : {nom: valeur}."""
    return _r._document("parametres")


def creer(nom, valeurs, unite=""):
    """Crée la grandeur (ou remplace ses valeurs si elle existe)."""
    _r.grandeur(_nom(nom), _colonne(valeurs), unite)
    _precedentes.discard(nom)


def _colonne(valeurs):
    a = _np.atleast_1d(_np.asarray(valeurs))
    if _np.iscomplexobj(a):
        raise ValueError("Une grandeur doit être réelle. Choisissez sa partie réelle ou son module avec np.abs.")
    if a.ndim != 1:
        raise ValueError("Une grandeur doit contenir une seule colonne de mesures.")
    return _np.ascontiguousarray(a, dtype=float)


def calculer(nom, formule, unite=""):
    """Grandeur calculée par une formule du tableur (« 0.5*m*v^2 »), recalculée quand les mesures changent."""
    _ordre("calculer", nom=_nom(nom), formule=str(formule), unite=unite)


def modifier(nom, valeurs=None, unite=None, nouveau_nom=None):
    """Change les valeurs, l'unité ou le nom d'une grandeur."""
    _existe(_nom(nom))
    if valeurs is not None:
        creer(nom, valeurs, unite if unite is not None else _r._document("unite", nom))
    elif unite is not None:
        _ordre("unite", nom=nom, unite=unite)
    if nouveau_nom is not None:
        renommer(nom, nouveau_nom)


def renommer(nom, nouveau_nom):
    """Renomme une grandeur (les formules qui l'utilisent suivent)."""
    _ordre("renommer", nom=_nom(nom), nouveau=_nom(nouveau_nom))


def supprimer(nom):
    """Supprime une grandeur du document."""
    _ordre("supprimer", nom=_nom(nom))


def incertitude(nom, valeur):
    """Incertitude-type u(nom) : un nombre (0.02) ou une formule ("0.02*U + 0.01")."""
    _ordre("incertitude", nom=_nom(nom), formule=str(valeur))


def parametre(nom, valeur, unite=""):
    """Crée ou change un paramètre du document (utilisable dans les formules)."""
    _ordre("parametre", nom=_nom(nom), valeur=float(valeur), unite=unite)


# --- plusieurs grandeurs ensemble (pandas) -----------------------------------------------------

def _nom_et_unite(entete):
    """« t (s) », « U [V] », « x / m » : nom de grandeur valable et unité."""
    import re
    texte = str(entete).strip()
    unite_ = ""
    m = re.match(r"^(.*?)\s*[\(\[]([^\)\]]*)[\)\]]\s*$", texte) or re.match(r"^(.*?)\s+/\s+(.*)$", texte)
    if m and m.group(1):
        texte, unite_ = m.group(1), m.group(2).strip()
    nom = re.sub(r"[ .\-]+", "_", texte)
    nom = re.sub(r"[^\w]", "", nom).strip("_") or "c"
    if nom[0].isdigit():
        nom = "g_" + nom
    return nom, unite_


def tableau(noms=None):
    """Les grandeurs du document (toutes, ou celles nommées) dans un DataFrame pandas ; leurs
    unités dans df.attrs["unites"]."""
    import pandas as pd
    noms = grandeurs() if noms is None else _noms(noms)
    for n in noms:
        _existe(n)
    df = pd.DataFrame({n: valeurs(n) for n in noms})
    df.attrs["unites"] = {n: unite(n) for n in noms}
    return df


def importer(df, tableur=None, remplacer=False):
    """Importe les colonnes et leurs unités. En cas de nom déjà utilisé, renvoie un nouveau nom.
    remplacer=True autorise la mise à jour explicite de mesures de même unité."""
    unites = df.attrs.get("unites", {})
    existants = set(grandeurs()) - _precedentes
    reserves = set(parametres())
    colonnes = []
    noms = []
    for c in df.columns:
        base, u = _nom_et_unite(c)
        u = str(u or unites.get(c, "") or "")
        nom = base
        if nom in reserves or (nom in existants and not remplacer):
            racine = base + "_" + _nom_et_unite(u)[0] if u else base
            nom, i = racine, 2
            while nom in reserves or nom in existants:
                nom = racine + "_" + str(i)
                i += 1
        if nom in existants and unite(nom) != u:
            raise ValueError("La grandeur %s est en %s, la colonne importée en %s." % (nom, unite(nom), u))
        _r._document("verifie", nom)
        reserves.add(nom)
        noms.append(nom)
        colonnes.append((nom, _colonne(df[c]), u))
    for nom, v, u in colonnes:
        creer(nom, v, u)
    if tableur is not None:
        globals()["tableur"](tableur, noms)
    return noms


def lire_csv(chemin, tableur=None, sep=None, decimal=None):
    """Importe un fichier CSV dans le document (séparateurs devinés s'ils ne sont pas donnés) ;
    renvoie le DataFrame lu."""
    import pandas as pd
    import os
    df = pd.read_csv(chemin, sep=sep, decimal=decimal)
    importer(df, tableur if tableur is not None else os.path.splitext(os.path.basename(str(chemin)))[0])
    return df


# --- tableurs ------------------------------------------------------------------------------

def tableur(nom, colonnes):
    """Tableur nommé, avec ces grandeurs en colonnes (créé, ou mis à jour s'il existe)."""
    _ordre("tableur", nom=str(nom), colonnes=_noms(colonnes))


# --- graphes -------------------------------------------------------------------------------

def graphe(nom, x=None, y=None, incertitudes=None):
    """Graphe nommé (créé, ou retrouvé s'il existe) ; avec x et y : la courbe y(x), y pouvant
    être une liste de grandeurs. incertitudes : "barres", "deux", "rectangles" ou "aucune"."""
    _ordre("graphe", nom=str(nom), x=None if x is None else _nom(x), y=None if y is None else _noms(y), incertitudes=incertitudes)


def courbe(graphe, x, y, couleur=None, marqueur=None, trait=None, incertitudes=None,
           x_bas=None, x_haut=None, y_bas=None, y_haut=None):
    """Ajoute la courbe y(x) au graphe. couleur : "rouge", "#ff0000"… ; marqueur : "croix",
    "plus", "rond", "point", "carre", "aucun" ; trait : "aucun", "droit", "lisse". x_bas, x_haut,
    y_bas, y_haut : une grandeur d'incertitude pour chaque côté (barres dissymétriques)."""
    cotes = {k: (None if v is None else _nom(v)) for k, v in
             (("x_bas", x_bas), ("x_haut", x_haut), ("y_bas", y_bas), ("y_haut", y_haut))}
    _ordre("courbe", graphe=str(graphe), x=_nom(x), y=_nom(y), couleur=couleur, marqueur=marqueur, trait=trait,
           incertitudes=incertitudes, **cotes)


def retirer_courbe(graphe, y):
    """Retire du graphe les courbes de la grandeur y."""
    _ordre("retirer_courbe", graphe=str(graphe), y=_nom(y))


def axes(graphe, xmin=None, xmax=None, ymin=None, ymax=None, titre=None, x_titre=None, y_titre=None):
    """Bornes des axes (les autres restent automatiques), titre du graphe, titres des axes."""
    _ordre("axes", graphe=str(graphe), xmin=xmin, xmax=xmax, ymin=ymin, ymax=ymax, titre=titre, x_titre=x_titre, y_titre=y_titre)


def verticale(graphe, x, texte="", verrou=False):
    """Droite verticale x = …, avec une étiquette ; verrou=True : la souris ne la déplace pas."""
    _ordre("annotation", graphe=str(graphe), type="verticale", x=float(x), texte=texte, verrou=bool(verrou))


def horizontale(graphe, y, texte="", verrou=False):
    """Droite horizontale y = …, avec une étiquette."""
    _ordre("annotation", graphe=str(graphe), type="horizontale", y=float(y), texte=texte, verrou=bool(verrou))


def oblique(graphe, x1, y1, x2, y2, texte="", verrou=False):
    """Droite oblique passant par (x1, y1) et (x2, y2)."""
    _ordre("annotation", graphe=str(graphe), type="oblique", x=float(x1), y=float(y1), x2=float(x2), y2=float(y2),
           texte=texte, verrou=bool(verrou))


def texte(graphe, x, y, texte, taille=12, verrou=False):
    """Texte placé en (x, y) sur le graphe."""
    _ordre("annotation", graphe=str(graphe), type="texte", x=float(x), y=float(y), texte=str(texte), taille=float(taille),
           verrou=bool(verrou))


def effacer_annotations(graphe):
    """Retire du graphe toutes ses droites et tous ses textes."""
    _ordre("effacer_annotations", graphe=str(graphe))


def tangentes(graphe, x, y, contact=None, verrou=False):
    """Méthode des tangentes sur la courbe y(x) du graphe (dosage) ; contact : abscisse du point de
    contact de la première tangente (sinon choisie par le logiciel)."""
    _ordre("tangentes", graphe=str(graphe), x=_nom(x), y=_nom(y), contact=None if contact is None else float(contact),
           verrou=bool(verrou))


def retirer_tangentes(graphe):
    """Retire du graphe la méthode des tangentes."""
    _ordre("retirer_tangentes", graphe=str(graphe))


# --- modélisation --------------------------------------------------------------------------

class Resultat:
    """Résultat d'une modélisation : r.a, r["a"], r.incertitudes["a"], r.r2, r.equation."""

    def __init__(self, noms, valeurs, incertitudes, r2, n, equation, fixes=()):
        self.parametres = dict(zip(noms, valeurs))
        self.incertitudes = dict(zip(noms, incertitudes))
        self.fixes = set(fixes)
        self.r2 = r2
        self.n = n
        self.equation = equation

    def __getitem__(self, nom):
        return self.parametres[nom]

    def __getattr__(self, nom):
        p = self.__dict__.get("parametres", {})
        if nom in p:
            return p[nom]
        raise AttributeError(nom)

    def __repr__(self):
        lignes = [self.equation]
        for k, v in self.parametres.items():
            u = self.incertitudes.get(k)
            if k in self.fixes:
                lignes.append("  %s = %.6g (fixé)" % (k, v))
            elif u == u:
                lignes.append("  %s = %.6g ± %.2g" % (k, v, u))
            else:
                # deux points pour deux paramètres, paramètres liés : pas d'incertitude
                lignes.append("  %s = %.6g (incertitude non calculable)" % (k, v))
        lignes.append("  R² = %.6f, %d point%s" % (self.r2, self.n, "s" if self.n > 1 else ""))
        return "\n".join(lignes)


def modeliser(y, x, modele="affine", degre=3, plage=None, graphe=None, nom=None):
    """Ajuste un modèle à y(x) et renvoie ses paramètres (avec incertitudes). modele : "affine",
    "lineaire", "parabole", "polynome" (degre), "puissance", "exponentielle", "charge",
    "decharge", "sinus"…, ou une expression ("a*x^2 + b"). plage : (xmin, xmax). Le modèle est
    aussi tracé sur le graphe donné, ou sur un graphe qui montre déjà y(x)."""
    _existe(_nom(x))
    _existe(_nom(y))
    m = str(modele)
    type_ = _MODELES.get(m.lower().replace(" ", "_"))
    expression = ""
    if type_ is None:
        type_, expression = "User", m
        # « x » désigne l'abscisse, quel que soit son nom
        if x != "x":
            import re
            if not re.search(r"\b%s\b" % re.escape(x), m):
                expression = re.sub(r"\bx\b", x, m)
    rng = None if plage is None else (float(plage[0]), float(plage[1]))
    noms, vals, incs, r2, n, equation, erreur, fixes = _r._ajuster(type_, int(degre), expression, x, y, rng)
    if erreur:
        raise ValueError("modélisation impossible : " + erreur)
    _ordre("modeliser", x=x, y=y, type=type_, degre=int(degre), expression=expression,
           plage=None if rng is None else list(rng), graphe=None if graphe is None else str(graphe), nom=nom)
    return Resultat(noms, vals, incs, r2, n, equation, fixes)


# --- fenêtres ------------------------------------------------------------------------------

def ouvrir(nom):
    """Affiche la fenêtre : « Grandeurs », « Calculs », « Modélisation », « Statistiques »,
    « Notice », ou le nom d'un tableur, d'un graphe, d'un script, d'un dessin."""
    _ordre("ouvrir", nom=str(nom))


def cacher(nom):
    """Cache la fenêtre (elle se retrouve dans le menu Fenêtres)."""
    _ordre("cacher", nom=str(nom))


def fermer(nom):
    """Ferme la fenêtre pour de bon : son tableur, son graphe, son script est supprimé."""
    _ordre("fermer", nom=str(nom))


def detacher(nom):
    """Met la fenêtre à part, hors de la fenêtre principale."""
    _ordre("detacher", nom=str(nom))


def rattacher(nom):
    """Remet une fenêtre détachée dans la fenêtre principale."""
    _ordre("rattacher", nom=str(nom))


def disposition():
    """Disposition par défaut des fenêtres."""
    _ordre("disposition")


# --- fichiers ------------------------------------------------------------------------------

def enregistrer(chemin=None):
    """Enregistre le document (sous ce nom de fichier .phy s'il est donné)."""
    _ordre("enregistrer", chemin=chemin)


def exporter_csv(chemin, grandeurs=None, separateur=";", virgule=True):
    """Grandeurs (toutes par défaut) dans un fichier CSV : séparateur ";" "," ou "\\t",
    virgule décimale ou point (toujours le point avec le séparateur ",", qui couperait les nombres)."""
    _ordre("exporter_csv", chemin=str(chemin), grandeurs=None if grandeurs is None else _noms(grandeurs),
           separateur=separateur, virgule=bool(virgule) and separateur != ",")


def exporter_graphe(nom, chemin):
    """Image du graphe : PNG, SVG ou PDF selon l'extension du fichier."""
    _ordre("exporter_graphe", nom=str(nom), chemin=str(chemin))


# --- divers --------------------------------------------------------------------------------

def message(texte):
    """Message dans la barre d'état de MeVReg."""
    _ordre("message", texte=str(texte))
