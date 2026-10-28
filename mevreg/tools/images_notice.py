#!/usr/bin/env python3
"""Refait les images de la notice (assets/help/*.jpg) à partir des captures des essais de
l'interface (test_app, lancé avec MEVREG_SHOTS) :

    MEVREG_SHOTS=$PWD/build/captures ctest --test-dir build -R app && python3 -I tools/images_notice.py build

(chemin absolu pour MEVREG_SHOTS : l'essai tourne dans le dossier build).

Il faut Pillow (python3-pillow).
"""
import os
import sys

from PIL import Image

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILATION = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RACINE, "build")
RENDUS = os.path.join(COMPILATION, "captures")
AIDE = os.path.join(RACINE, "assets", "help")

# capture de l'essai → image de la notice ; les fenêtres entières passent à 1024 px de large,
# les boîtes de dialogue, les menus et les fenêtres à part gardent leur taille. Toute nouvelle
# image est aussi à ajouter à help::files() (src/app/help.cpp) et aux ressources (CMakeLists.txt).
IMAGES = [
    ("notice-accueil.png", "accueil.jpg"),
    ("notice-fenetre.png", "fenetre.jpg"),
    ("notice-tableur.png", "tableur.jpg"),
    ("import-csv.png", "import-csv.jpg"),
    ("notice-incertitude.png", "incertitude.jpg"),
    ("notice-graphe.png", "graphe.jpg"),
    ("notice-modele.png", "modele.jpg"),
    ("notice-tangentes.png", "tangentes.jpg"),
    ("export-csv.png", "export-csv.jpg"),
    ("notice-menu-fenetres.png", "menu-fenetres.jpg"),
    ("scripts-confirmation.png", "scripts-recus.jpg"),
    ("notice-python.png", "python.jpg"),
]

for rendu, nom in IMAGES:
    image = Image.open(os.path.join(RENDUS, rendu)).convert("RGB")
    if rendu.startswith("notice-") and image.width >= 1024 and nom != "modele.jpg":
        image = image.resize((1024, round(image.height * 1024 / image.width)), Image.LANCZOS)
    sortie = os.path.join(AIDE, nom)
    image.save(sortie, quality=85, optimize=True)
    print(sortie, image.size, os.path.getsize(sortie) // 1024, "Kio")
