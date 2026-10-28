#!/usr/bin/env python3
"""Refait les images de la notice (assets/help/*.jpg) à partir des rendus des essais de
l'interface (captures/ du dossier de compilation, écrites par mevsono_uitests) :

    ctest --test-dir build -R uitests && python3 -I tools/images_notice.py build

Il faut Pillow (python3-pillow).
"""
import os
import sys

from PIL import Image

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILATION = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RACINE, "build")
RENDUS = os.path.join(COMPILATION, "captures")
AIDE = os.path.join(RACINE, "assets", "help")

# Fenêtre entière : ramenée à 1024 × 640.
FENETRE = (1024, 640)

# rendu de l'essai → image de la notice, et sa taille (None : la taille du rendu, pour une boîte de
# dialogue seule, lisible à sa taille réelle)
IMAGES = [
    ("00_accueil.png", "accueil-fr.jpg", FENETRE),
    ("01_flute_simplifie.png", "fenetre-fr.jpg", FENETRE),
    ("05_mesure_simplifie.png", "mesure-fr.jpg", FENETRE),
    ("02_flute_expert.png", "expert-fr.jpg", FENETRE),
    ("02_reglages.png", "reglages-fr.jpg", None),
    ("09_en_accueil.png", "accueil-en.jpg", FENETRE),
    ("10_en_flute.png", "fenetre-en.jpg", FENETRE),
    ("11_en_measure.png", "mesure-en.jpg", FENETRE),
    ("12_en_expert.png", "expert-en.jpg", FENETRE),
    ("12_en_settings.png", "reglages-en.jpg", None),
]

for rendu, nom, taille in IMAGES:
    image = Image.open(os.path.join(RENDUS, rendu)).convert("RGB")
    if taille:
        image = image.resize(taille, Image.LANCZOS)
    sortie = os.path.join(AIDE, nom)
    image.save(sortie, quality=85, optimize=True)
    print(sortie, image.size, os.path.getsize(sortie) // 1024, "Kio")
