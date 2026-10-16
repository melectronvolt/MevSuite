#!/usr/bin/env python3
"""Refait les captures d'écran de la notice, dans chaque langue, à partir
des rendus des tests de l'interface (build/rendus-tests, écrits par
mevcine_tests_interface) :

    (cd build && QT_QPA_PLATFORM=offscreen ./mevcine_tests_interface guide_dans_chaque_langue)
    python3 -I scripts/captures-aide.py

Les images vont dans data/help/<code>/images/. Il faut Pillow
(python3-pillow).
"""
import os
from PIL import Image

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDUS = os.path.join(RACINE, "build", "rendus-tests")
AIDE = os.path.join(RACINE, "data", "help")

# rendu du test → image de la notice (les écrans en PNG, les vues de la
# vidéo en JPEG)
CAPTURES = [
    ("80_{}_accueil.png", "accueil.png"),
    ("91_{}_sans_video.png", "sans-video.png"),
    ("84_{}_origine.png", "origine.jpg"),
    ("81_{}_axes.png", "axes.jpg"),
    ("82_{}_echelle.png", "echelle.jpg"),
    ("85_{}_pointage.png", "pointage.jpg"),
    ("83_{}_export.png", "export.jpg"),
    ("86_{}_expert_pointage.png", "expert-pointage.jpg"),
    ("87_{}_expert_trajectoires.png", "expert-trajectoires.jpg"),
    ("88_{}_expert_coordonnees.png", "expert-coordonnees.jpg"),
    ("89_{}_expert_grapheur.png", "expert-grapheur.jpg"),
    ("90_{}_confirmation.png", "confirmation.jpg"),
]

for code in ["fr", "en"]:
    dossier = os.path.join(AIDE, code, "images")
    os.makedirs(dossier, exist_ok=True)
    for rendu, nom in CAPTURES:
        image = Image.open(os.path.join(RENDUS, rendu.format(code))).convert("RGB")
        image = image.resize((960, 600), Image.LANCZOS)
        sortie = os.path.join(dossier, nom)
        if sortie.endswith(".jpg"):
            image.save(sortie, quality=85, optimize=True)
        else:
            image.save(sortie, optimize=True)
        print(sortie)
