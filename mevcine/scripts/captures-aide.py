#!/usr/bin/env python3
"""Installe les captures de guide_dans_chaque_langue dans la notice livrée avec MeVCine.

    QT_QPA_PLATFORM=offscreen build/mevcine_tests_interface guide_dans_chaque_langue
    python3 -I scripts/captures-aide.py build/rendus-tests
"""
import argparse
from pathlib import Path
from shutil import copyfile

RACINE = Path(__file__).resolve().parents[1]
CAPTURES = {
    "95_{}_premier_demarrage": "premier-demarrage", "91_{}_sans_video": "ouvrir",
    "84_{}_origine": "origine", "81_{}_axes": "axes", "82_{}_echelle": "echelle",
    "85_{}_pointage": "pointage", "85_{}_image": "bille", "85_{}_visee": "visee", "92_{}_plein_ecran": "plein-ecran",
    "83_{}_export": "export", "86_{}_expert_pointage": "expert-pointage",
    "87_{}_expert_trajectoires": "trajectoires", "88_{}_expert_coordonnees": "coordonnees",
    "89_{}_expert_grapheur": "grapheur", "99_{}_calcul": "calcul-vitesse",
    "98_{}_suivi": "suivi", "90_{}_dialogue": "confirmation",
    "93_{}_menu_fichier": "menu-fichier", "94_{}_menu_mode": "menu-mode",
    "96_{}_menu_edition": "menu-edition", "97_{}_menu_aide": "menu-aide",
    "100_{}_tableau": "tableau-mesures",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rendus", type=Path, nargs="?", default=RACINE / "build" / "rendus-tests")
    rendus = parser.parse_args().rendus
    fichiers = [(rendus / (rendu.format(langue) + ".png"),
                 RACINE / "data" / "help" / langue / "images" / (nom + ".png"))
                for langue in ("fr", "en") for rendu, nom in CAPTURES.items()]
    for source, _ in fichiers:
        if not source.is_file():
            parser.error(f"capture absente : {source}. Exécutez le test guide_dans_chaque_langue.")
    for source, cible in fichiers:
        cible.parent.mkdir(parents=True, exist_ok=True)
        copyfile(source, cible)
    print(f"{len(fichiers)} captures copiées dans la notice.")


if __name__ == "__main__":
    main()
