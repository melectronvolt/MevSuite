#!/usr/bin/env python3
"""Police des émoji de l'AppImage : Noto Color Emoji réduite aux émoji des sources.

    police-emoji.py <NotoColorEmoji.ttf> <police produite> <dossier des sources>…

Le Qt joint à l'AppImage (celui d'aqtinstall) est compilé sans la prise en charge des polices en
couleur au format COLR : la police émoji de Fedora (Noto Color Emoji en COLRv1) y est choisie, mais
rien n'est dessiné (« 📂 Ouvrir » devient « Ouvrir »). Noto Color Emoji en images (format CBDT),
que ce Qt sait dessiner, est réduite aux caractères employés par les logiciels (quelques dizaines de
kilo-octets) et renommée « MeVSuite Emoji », pour ne pas se confondre avec celle du système. Le
logiciel la charge au démarrage (`usr/share/mevsuite/mevsuite-emoji.ttf` dans l'AppImage).
Il faut fontTools (pip install fonttools).
"""

import pathlib
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

FAMILLE = "MeVSuite Emoji"


def caracteres(dossiers):
    """Les caractères des sources (C++, traductions, pages de la notice) à partir de U+2300 : les
    flèches, lettres et symboles mathématiques plus bas sont dans les polices de texte."""
    vus = set()
    for dossier in dossiers:
        for f in pathlib.Path(dossier).rglob("*"):
            if f.is_file() and f.suffix in (".cpp", ".h", ".po", ".html", ".py"):
                vus |= {ord(c) for c in f.read_text(encoding="utf-8", errors="ignore") if ord(c) >= 0x2300}
    return vus


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    source, sortie, dossiers = sys.argv[1], sys.argv[2], sys.argv[3:]
    police = TTFont(source)
    gardes = sorted(caracteres(dossiers) & set(police.getBestCmap()))
    if not gardes:
        sys.exit("aucun émoji trouvé dans les sources")
    options = subset.Options()
    options.layout_features = ["*"]
    reduction = subset.Subsetter(options)
    reduction.populate(unicodes=gardes)
    reduction.subset(police)
    for nom in police["name"].names:
        if nom.nameID in (1, 4, 16):
            nom.string = FAMILLE
        elif nom.nameID == 6:
            nom.string = FAMILLE.replace(" ", "")
    police.save(sortie)
    print(f"{sortie} : {len(gardes)} émoji ({' '.join(chr(c) for c in gardes)})")


if __name__ == "__main__":
    main()
