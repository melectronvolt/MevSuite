#!/usr/bin/env python3
"""Refait les images de la notice (assets/help/*.jpg) à partir des rendus des essais de
l'interface (captures/ du dossier de compilation, écrites par mevpix_uitests) :

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

# rendu de l'essai → image de la notice
IMAGES = [
    ("01_fr_accueil.png", "accueil-fr.jpg"),
    ("11_en_accueil.png", "accueil-en.jpg"),
    ("02_fr_mesure.png", "fenetre-fr.jpg"),
    ("02_fr_etalonnage.png", "etalonnage-fr.jpg"),
    ("02_fr_profil.png", "profil-fr.jpg"),
    ("12_en_mesure.png", "fenetre-en.jpg"),
    ("12_en_etalonnage.png", "etalonnage-en.jpg"),
    ("12_en_profil.png", "profil-en.jpg"),
]

for rendu, nom in IMAGES:
    image = Image.open(os.path.join(RENDUS, rendu)).convert("RGB")
    image = image.resize((1024, 640), Image.LANCZOS)
    sortie = os.path.join(AIDE, nom)
    image.save(sortie, quality=85, optimize=True)
    print(sortie, os.path.getsize(sortie) // 1024, "Kio")
