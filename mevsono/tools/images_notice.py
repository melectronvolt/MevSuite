#!/usr/bin/env python3
"""Copie les captures originales des essais Qt dans la notice, sans redimensionnement."""

import argparse
from pathlib import Path
from shutil import copyfile


RACINE = Path(__file__).resolve().parents[1]
# Pour chaque figure de la notice, nom du rendu français puis du rendu anglais.
FIGURES = {
    "accueil": ("accueil_audio_fr", "accueil_audio_en"),
    "prise": ("prise_micro_fr", "prise_micro_en"),
    "fichier": ("menu_fichier_prise", "menu_fichier_prise_en"),
    "mesure": ("05_mesure_simplifie", "11_en_measure"),
    "spectre": ("01_flute_simplifie", "10_en_flute"),
    "comparaison": ("comparaison_fr", "comparaison_en"),
    "mode": ("menu_mode_sombre", "menu_mode_sombre_en"),
    "expert": ("02_flute_expert", "12_en_expert"),
    "reglages": ("02_reglages", "12_en_settings"),
    "affichage": ("menu_affichage_clair", "menu_view_clair_en"),
    "axe-ajuste": ("axe_ajuste_fr", "axe_ajuste_en"),
    "axe-fixe": ("axe_fixe_fr", "axe_fixe_en"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, nargs="?", default=RACINE / "build",
                        help="dossier de compilation contenant captures/")
    rendus = parser.parse_args().build / "captures"
    aide = RACINE / "assets" / "help"
    copies = [(rendus / f"{source}.png", aide / f"{figure}-{langue}.png")
              for figure, sources in FIGURES.items()
              for langue, source in zip(("fr", "en"), sources)]
    manquants = [str(source) for source, _ in copies if not source.is_file()]
    if manquants:
        parser.exit(1, "Captures manquantes (lancer les tests d’interface et audio-virtuel.sh)\n"
                    + "\n".join(manquants) + "\n")
    for source, destination in copies:
        copyfile(source, destination)
    print(f"{len(copies)} captures copiées dans {aide}")


if __name__ == "__main__":
    main()
