"""Met à jour les captures des notices après un passage de mevpix_uitests."""

import argparse
from pathlib import Path
from shutil import copyfile


def main():
    projet = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compilation", type=Path, nargs="?", default=projet / "build")
    captures = parser.parse_args().compilation / "captures"
    destination = projet / "assets" / "help"

    for langue, accueil, outils in [("fr", "01", "02"), ("en", "11", "12")]:
        fichiers = {"accueil": f"{accueil}_{langue}_accueil"}
        for nom in ("etalonnage", "mesure", "profil"):
            fichiers[nom] = f"{outils}_{langue}_{nom}"
        for nom in ("menu-fichier", "menu-aide", "zoom", "sauvegarde", "echelle", "annotation"):
            fichiers[nom] = f"{nom}-{langue}"
        for nom, source in fichiers.items():
            copyfile(captures / f"{source}.png", destination / f"{nom}-{langue}.png")


if __name__ == "__main__":
    main()
