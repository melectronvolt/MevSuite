#!/usr/bin/env python3
"""Refait les captures d'écran de la notice, dans chaque langue, à partir
des rendus des tests de l'interface (build/rendus-tests, écrits par
mevcine_tests_interface) :

    (cd build && QT_QPA_PLATFORM=offscreen ./mevcine_tests_interface guide_dans_chaque_langue)
    python3 -I scripts/captures-aide.py

Les images vont dans data/help/<code>/images/ ; celles qui ne servent plus
sont effacées. Il faut Pillow (python3-pillow).
"""
import os
from PIL import Image

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDUS = os.path.join(RACINE, "build", "rendus-tests")
AIDE = os.path.join(RACINE, "data", "help")

# rendu du test → image de la notice, et sa taille : la fenêtre entière est
# réduite à 960 × 600 (les écrans en PNG, les vues de la vidéo en JPEG) ; un
# menu déroulé garde sa taille, pour rester lisible
FENETRE = (960, 600)
CAPTURES = [
    ("91_{}_sans_video.png", "sans-video.png", FENETRE),
    ("93_{}_menu_fichier.png", "menu-fichier.png", None),
    ("94_{}_menu_mode.png", "menu-mode.png", None),
    ("84_{}_origine.png", "origine.jpg", FENETRE),
    ("81_{}_axes.png", "axes.jpg", FENETRE),
    ("82_{}_echelle.png", "echelle.jpg", FENETRE),
    ("85_{}_pointage.png", "pointage.jpg", FENETRE),
    ("92_{}_plein_ecran.png", "plein-ecran.jpg", FENETRE),
    ("83_{}_export.png", "export.jpg", FENETRE),
    ("86_{}_expert_pointage.png", "expert-pointage.jpg", FENETRE),
    ("87_{}_expert_trajectoires.png", "expert-trajectoires.jpg", FENETRE),
    ("88_{}_expert_coordonnees.png", "expert-coordonnees.jpg", FENETRE),
    ("89_{}_expert_grapheur.png", "expert-grapheur.jpg", FENETRE),
    ("90_{}_confirmation.png", "confirmation.jpg", FENETRE),
]

for code in ["fr", "en"]:
    dossier = os.path.join(AIDE, code, "images")
    os.makedirs(dossier, exist_ok=True)
    noms = set()
    for rendu, nom, taille in CAPTURES:
        image = Image.open(os.path.join(RENDUS, rendu.format(code))).convert("RGB")
        if taille:
            image = image.resize(taille, Image.LANCZOS)
        sortie = os.path.join(dossier, nom)
        if sortie.endswith(".jpg"):
            image.save(sortie, quality=85, optimize=True)
        else:
            image.save(sortie, optimize=True)
        noms.add(nom)
        print(sortie)
    for f in sorted(os.listdir(dossier)):
        if f not in noms:
            os.remove(os.path.join(dossier, f))
            print("effacé :", os.path.join(dossier, f))
