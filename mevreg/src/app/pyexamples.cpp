#include "app/pyexamples.h"

using namespace Qt::StringLiterals;

namespace pyexamples {

namespace {

const QString GRANDEURS = u"Grandeurs"_s;
const QString GRAPHES = u"Graphes"_s;
const QString MODELES = u"Modélisation"_s;
const QString FENETRES = u"Fenêtres et fichiers"_s;
const QString DIVERS = u"Divers"_s;

/// Début commun : des mesures de démonstration, t et x (à remplacer par les vôtres).
const QString MESURES = uR"py(import numpy as np
import mevreg as mr

# des mesures pour l'exemple : remplacez-les par les vôtres
mr.creer("t", np.arange(0, 5.5, 0.5), "s")
mr.creer("x", 2.0 * mr.valeurs("t") + 1 + np.random.normal(0, 0.1, 11), "m")
)py"_s;

} // namespace

const std::vector<QString> &categories()
{
    static const std::vector<QString> c = {GRANDEURS, GRAPHES, MODELES, FENETRES, DIVERS};
    return c;
}

const std::vector<Example> &all()
{
    static const std::vector<Example> e = {
        // --- grandeurs ---
        {GRANDEURS, u"Créer une grandeur"_s, uR"py(import numpy as np

# une liste de nombres ou un tableau numpy devient une grandeur, du nom de la variable
U = [0, 1.02, 1.98, 3.05, 3.97, 5.01]
t = np.arange(0, 10.5, 0.5)       # de 0 à 10 s, par pas de 0,5 s
)py"_s},
        {GRANDEURS, u"Créer une grandeur avec son unité"_s, uR"py(import numpy as np

# grandeur(nom, valeurs, unité) ; une grandeur qui existe déjà est remplacée
grandeur("t", np.arange(0, 10.5, 0.5), "s")
)py"_s},
        {GRANDEURS, u"Créer une grandeur à partir de valeurs"_s, uR"py(import mevreg as mr

# valeurs écrites à la main, entre crochets, séparées par des virgules
mr.creer("U", [0, 1.02, 1.98, 3.05, 3.97, 5.01], "V")
mr.creer("I", [0, 0.011, 0.020, 0.031, 0.040, 0.050], "A")
)py"_s},
        {GRANDEURS, u"Grandeur calculée par une formule"_s, MESURES + uR"py(
# formule du tableur : recalculée par MeVReg chaque fois que les mesures changent
mr.calculer("v", "deriv(x, t)", "m/s")
mr.calculer("x_cm", "100*x", "cm")
)py"_s},
        {GRANDEURS, u"Grandeur calculée avec numpy"_s, MESURES + uR"py(
# le calcul est fait par le script : relancez-le si les mesures changent
v = np.gradient(mr.valeurs("x"), mr.valeurs("t"))
mr.creer("v", v, "m/s")
)py"_s},
        {GRANDEURS, u"Lire les grandeurs du document"_s, uR"py(import mevreg as mr

# nom, unité, nombre de valeurs et moyenne de chaque grandeur
for nom in mr.grandeurs():
    print(nom, "(" + (mr.unite(nom) or "sans unité") + ") :", len(mr.valeurs(nom)), "valeurs, moyenne", mr.valeurs(nom).mean())
)py"_s},
        {GRANDEURS, u"Modifier les valeurs"_s, MESURES + uR"py(
# toutes les valeurs de x multipliées par 100 : des mètres aux centimètres
mr.modifier("x", valeurs=mr.valeurs("x") * 100, unite="cm")
)py"_s},
        {GRANDEURS, u"Changer l'unité"_s, MESURES + uR"py(
mr.modifier("t", unite="ms")
)py"_s},
        {GRANDEURS, u"Renommer une grandeur"_s, MESURES + uR"py(
# les formules qui utilisent x suivent le nouveau nom
mr.renommer("x", "position")
)py"_s},
        {GRANDEURS, u"Supprimer une grandeur"_s, MESURES + uR"py(
mr.creer("brouillon", [1, 2, 3])
mr.supprimer("brouillon")
)py"_s},
        {GRANDEURS, u"Incertitude d'une grandeur"_s, MESURES + uR"py(
# un nombre : la même incertitude-type pour toutes les mesures
mr.incertitude("x", 0.05)
# ou une formule : 2 % de la valeur
mr.incertitude("t", "0.02*t")
)py"_s},
        {GRANDEURS, u"Paramètre du document"_s, uR"py(import mevreg as mr

# utilisable ensuite dans les formules : "0.5*m*v^2"
mr.parametre("m", 0.250, "kg")
mr.parametre("g", 9.81, "m/s^2")
print(mr.parametres())
)py"_s},
        {GRANDEURS, u"Tableur avec des colonnes choisies"_s, MESURES + uR"py(
# créé, ou mis à jour s'il existe déjà
mr.tableur("Mesures", ["t", "x"])
)py"_s},

        // --- graphes ---
        {GRAPHES, u"Tracer y en fonction de x"_s, MESURES + uR"py(
# graphe créé, ou retrouvé s'il existe : x(t)
mr.graphe("Position", x="t", y="x")
)py"_s},
        {GRAPHES, u"Plusieurs courbes sur un graphe"_s, uR"py(import numpy as np
import mevreg as mr

t = np.linspace(0, 0.02, 200)
mr.creer("t", t, "s")
mr.creer("u1", 5 * np.sin(2 * np.pi * 100 * t), "V")
mr.creer("u2", 3 * np.cos(2 * np.pi * 100 * t), "V")
mr.graphe("Tensions", x="t", y=["u1", "u2"])
)py"_s},
        {GRAPHES, u"Couleur, marqueur, trait"_s, MESURES + uR"py(
# couleur : "rouge", "bleu", "vert", "orange", "noir"… ou "#ff8800"
# marqueur : "croix", "plus", "rond", "point", "carre", "aucun" ; trait : "aucun", "droit", "lisse"
mr.graphe("Position")
mr.courbe("Position", "t", "x", couleur="rouge", marqueur="rond", trait="lisse")
)py"_s},
        {GRAPHES, u"Barres d'incertitude"_s, MESURES + uR"py(
mr.incertitude("x", 0.2)
# "barres" (verticales), "deux" (verticales et horizontales), "rectangles", "aucune"
mr.graphe("Position", x="t", y="x", incertitudes="barres")
)py"_s},
        {GRAPHES, u"Bornes des axes et titres"_s, MESURES + uR"py(
mr.graphe("Position", x="t", y="x")
mr.axes("Position", xmin=0, xmax=6, ymin=0, ymax=14, titre="Mouvement rectiligne",
        x_titre="temps", y_titre="position")
)py"_s},
        {GRAPHES, u"Droites et texte sur le graphe"_s, MESURES + uR"py(
mr.graphe("Position", x="t", y="x")
mr.verticale("Position", 2.5, texte="t = 2,5 s", verrou=True)   # verrou : la souris ne la déplace pas
mr.horizontale("Position", 6)
mr.oblique("Position", 0, 1, 5, 11)        # droite passant par (0 ; 1) et (5 ; 11)
mr.texte("Position", 0.5, 10, "x = 2·t + 1", taille=14)
# mr.effacer_annotations("Position") retire toutes les droites et les textes
)py"_s},
        {GRAPHES, u"Incertitudes différentes de chaque côté"_s, MESURES + uR"py(
# une grandeur d'incertitude pour chaque côté de la barre
mr.creer("bas", 0.2 + 0 * mr.valeurs("x"), "m")
mr.creer("haut", 0.5 + 0 * mr.valeurs("x"), "m")
mr.graphe("Position", x="t", y="x")
mr.courbe("Position", "t", "x", y_bas="bas", y_haut="haut")
)py"_s},
        {GRAPHES, u"Méthode des tangentes (dosage)"_s, uR"py(import numpy as np
import mevreg as mr

# un dosage pour l'exemple : remplacez-le par vos mesures
mr.creer("V", np.linspace(0, 20, 41), "mL")
mr.creer("pH", 7 + 4 * np.tanh(1.5 * (mr.valeurs("V") - 12)))
mr.graphe("Dosage", x="V", y="pH")
mr.tangentes("Dosage", "V", "pH")
# mr.retirer_tangentes("Dosage") la retire
)py"_s},
        {GRAPHES, u"Retirer une courbe"_s, MESURES + uR"py(
mr.calculer("x2", "x + 1", "m")
mr.graphe("Position", x="t", y=["x", "x2"])
mr.retirer_courbe("Position", "x2")
)py"_s},
        {GRAPHES, u"Figure matplotlib"_s, MESURES + uR"py(import matplotlib.pyplot as plt

# la figure devient un graphe de MeVReg, modifiable à la souris
plt.figure("x(t) avec matplotlib")
plt.plot(mr.valeurs("t"), mr.valeurs("x"), "r+", label="x")
plt.xlabel("t (s)")
plt.ylabel("x (m)")
plt.legend()
plt.grid()
plt.show()
)py"_s},
        {GRAPHES, u"Exporter l'image d'un graphe"_s, MESURES + uR"py(
mr.graphe("Position", x="t", y="x")
# PNG, SVG ou PDF selon l'extension ; à côté du document s'il est enregistré
mr.exporter_graphe("Position", "position.png")
)py"_s},

        // --- modélisation ---
        {MODELES, u"Modèle affine"_s, MESURES + uR"py(
r = mr.modeliser("x", "t", "affine", graphe="Position")
print(r)
print("pente :", r.a, "±", r.incertitudes["a"], "m/s")
)py"_s},
        {MODELES, u"Modèle sur une plage de points"_s, MESURES + uR"py(
# seuls les points dont t est entre 1 et 4 s comptent
r = mr.modeliser("x", "t", "affine", plage=(1, 4), graphe="Position")
print(r)
)py"_s},
        {MODELES, u"Parabole"_s, uR"py(import numpy as np
import mevreg as mr

t = np.linspace(0, 0.6, 13)
mr.creer("t", t, "s")
mr.creer("z", 1.5 - 4.9 * t**2 + np.random.normal(0, 0.005, 13), "m")
r = mr.modeliser("z", "t", "parabole", graphe="Chute")
print(r)
print("g =", -2 * r.a, "m/s²")
)py"_s},
        {MODELES, u"Décharge d'un condensateur"_s, uR"py(import numpy as np
import mevreg as mr

t = np.linspace(0, 5, 26)
mr.creer("t", t, "s")
mr.creer("uc", 6 * np.exp(-t / 1.2) + np.random.normal(0, 0.03, 26), "V")
# y = A·exp(−(x − Δ)/τ) + V0
r = mr.modeliser("uc", "t", "decharge", graphe="Décharge")
print(r)
print("τ =", r["τ"], "s")
)py"_s},
        {MODELES, u"Modèle écrit à la main"_s, MESURES + uR"py(
# une expression en x, avec les paramètres à ajuster (ici a et b)
r = mr.modeliser("x", "t", "a*x^2 + b", graphe="Position")
print(r)
)py"_s},
        {MODELES, u"Nom du modèle dans la légende"_s, uR"py(import mevreg as mr

mr.creer("I", [0, 0.011, 0.020, 0.031, 0.040, 0.050], "A")
mr.creer("U", [0, 1.02, 1.98, 3.05, 3.97, 5.01], "V")
r = mr.modeliser("U", "I", "lineaire", graphe="Loi d'Ohm", nom="U = R·I")
print("R =", r.a, "Ω")
)py"_s},
        {MODELES, u"Comparer plusieurs modèles"_s, MESURES + uR"py(
for modele in ["lineaire", "affine", "parabole"]:
    r = mr.modeliser("x", "t", modele)
    print(modele, ": R² =", r.r2)
)py"_s},

        // --- fenêtres et fichiers ---
        {FENETRES, u"Ouvrir une fenêtre"_s, uR"py(import mevreg as mr

# « Grandeurs », « Calculs », « Modélisation », « Statistiques », « Notice »,
# ou le nom d'un tableur, d'un graphe, d'un script
mr.ouvrir("Grandeurs")
)py"_s},
        {FENETRES, u"Cacher ou fermer une fenêtre"_s, MESURES + uR"py(
mr.graphe("Brouillon", x="t", y="x")
mr.cacher("Calculs")     # elle se retrouve dans le menu Fenêtres
mr.fermer("Brouillon")   # supprimée du document (Ctrl+Z pour annuler)
)py"_s},
        {FENETRES, u"Détacher une fenêtre"_s, MESURES + uR"py(
mr.graphe("Position", x="t", y="x")
# dans une fenêtre à part (pour un second écran, le vidéoprojecteur…)
mr.detacher("Position")
)py"_s},
        {FENETRES, u"Disposition par défaut"_s, uR"py(import mevreg as mr

mr.disposition()
)py"_s},
        {FENETRES, u"Enregistrer le document"_s, uR"py(import mevreg as mr

# sous son nom actuel ; ou sous un nouveau nom : mr.enregistrer("tp_chute.phy")
mr.enregistrer()
)py"_s},
        {FENETRES, u"Exporter des grandeurs en CSV"_s, MESURES + uR"py(
# séparateur ";" "," ou "\t" ; virgule=True : 3,14 ; virgule=False : 3.14
mr.exporter_csv("mesures.csv", ["t", "x"], separateur=";", virgule=True)
)py"_s},
        {FENETRES, u"Message dans la barre d'état"_s, uR"py(import mevreg as mr

mr.message("Calculs terminés")
)py"_s},

        // --- divers ---
        {DIVERS, u"Tableaux de valeurs (numpy)"_s, uR"py(import numpy as np

a = np.linspace(0, 1, 5)     # 5 valeurs de 0 à 1
b = np.arange(0, 10, 2)      # de 0 à 10 (exclu), par pas de 2
print(a, b, a * 2, b ** 2, np.sqrt(b))
)py"_s},
        {DIVERS, u"Moyenne, écart-type, incertitude"_s, uR"py(import numpy as np

mesures = np.array([9.79, 9.83, 9.80, 9.86, 9.78, 9.82])
moyenne = mesures.mean()
s = mesures.std(ddof=1)            # écart-type corrigé (n − 1)
u = s / np.sqrt(len(mesures))      # incertitude-type de la moyenne
print("g = %.3f ± %.3f m/s²" % (moyenne, u))
)py"_s},
        {DIVERS, u"Simulation : chute libre"_s, uR"py(import numpy as np
import mevreg as mr

g = 9.81
# un tableau numpy devient une grandeur, du nom de la variable (sans unité)
t_sim = np.linspace(0, 1, 21)
z_sim = 5 - 0.5 * g * t_sim**2
mr.graphe("Simulation", x="t_sim", y="z_sim")
)py"_s},
        {DIVERS, u"Bruit de mesure (valeurs aléatoires)"_s, uR"py(import numpy as np
import mevreg as mr

np.random.seed(1)                      # mêmes valeurs à chaque exécution
t = np.linspace(0, 2, 21)
mr.creer("t", t, "s")
mr.creer("y", 3 * t + np.random.normal(0, 0.2, 21), "m")
)py"_s},
        {DIVERS, u"Boucle et condition"_s, MESURES + uR"py(
for ti, xi in zip(mr.valeurs("t"), mr.valeurs("x")):
    if xi > 5:
        print("à t =", ti, "s, x dépasse 5 m :", xi)
        break
)py"_s},
        {DIVERS, u"Afficher des résultats arrondis"_s, uR"py(import numpy as np

v = 12.345678
print("v = %.2f m/s" % v)          # 2 décimales
print("v = %.3g m/s" % v)          # 3 chiffres significatifs
print("E = %.2e J" % 0.000123)     # notation scientifique
)py"_s},
    };
    return e;
}

} // namespace pyexamples
