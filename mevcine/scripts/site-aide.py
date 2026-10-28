#!/usr/bin/env python3
"""Fabrique la notice de MeVCine, un site de pages HTML dans data/help/,
à partir de ses sources dans aide/ :

    python3 -I scripts/site-aide.py

Pour chaque langue, aide/<code>/ contient :

- site.txt : les textes du site (clé = valeur) : titre, nom de la langue,
  sommaire, page précédente, page suivante… ;
- pages.txt : une ligne par page, dans l'ordre du sommaire :
  « nom-du-fichier <tabulation> titre court » ; une ligne « == titre »
  ouvre une partie du sommaire (« Pour les élèves », « Pour les
  professeurs ») ;
- <nom>.html : le contenu de chaque page (un fragment qui commence par
  son <h1>), écrit à la main.

Le gabarit aide/gabarit.html (sommaire, autres langues, page précédente et
suivante) est commun. La feuille de style aide/help.css est celle de tous
les logiciels de la suite (copiée telle quelle de MeVSono) ; aide/aide.css
y ajoute ce qui sert à un site de plusieurs pages.
Les captures d'écran viennent de scripts/captures-aide.py, dans
data/help/<code>/images/. Le test `aide_dans_chaque_langue` vérifie que le
site est à jour de ses sources, que chaque image existe et que chaque lien
mène quelque part.
"""
import html
import os
import re
import shutil
import sys

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.join(RACINE, "aide")
SORTIE = os.path.join(RACINE, "data", "help")
LANGUES = ["fr", "en"]


def version():
    """le marqueur de la version : MeVCine le remplace par la sienne quand il copie la notice
    (chemins::copie_aide), et le site reste juste d'une version à l'autre"""
    return "{{version}}"


def lit_cles(chemin):
    """un fichier « clé = valeur », une entrée par ligne"""
    valeurs = {}
    with open(chemin, encoding="utf-8") as f:
        for ligne in f:
            ligne = ligne.strip()
            if not ligne or ligne.startswith("#") or "=" not in ligne:
                continue
            cle, valeur = ligne.split("=", 1)
            valeurs[cle.strip()] = valeur.strip()
    return valeurs


def lit_pages(chemin):
    """les pages, dans l'ordre : (nom de fichier, titre court, partie du sommaire qu'elle ouvre)"""
    pages = []
    partie = None
    with open(chemin, encoding="utf-8") as f:
        for ligne in f:
            if not ligne.strip() or ligne.startswith("#"):
                continue
            if ligne.startswith("=="):
                partie = ligne[2:].strip()
                continue
            nom, titre = ligne.rstrip("\n").split("\t", 1)
            pages.append((nom.strip(), titre.strip(), partie))
            partie = None
    return pages


def titre_h1(fragment, nom):
    m = re.search(r"<h1[^>]*>(.*?)</h1>", fragment, re.S)
    if not m:
        sys.exit(f"{nom} : pas de <h1>")
    return html.unescape(re.sub(r"<[^>]+>", "", m.group(1))).strip()


def main():
    with open(os.path.join(SOURCES, "gabarit.html"), encoding="utf-8") as f:
        gabarit = f.read()
    sites = {}
    for code in LANGUES:
        dossier = os.path.join(SOURCES, code)
        if not os.path.isdir(dossier):
            sys.exit(f"aide/{code} manque")
        sites[code] = (lit_cles(os.path.join(dossier, "site.txt")), lit_pages(os.path.join(dossier, "pages.txt")))
    v = version()
    os.makedirs(SORTIE, exist_ok=True)
    for css in ("help.css", "aide.css"):
        shutil.copy(os.path.join(SOURCES, css), os.path.join(SORTIE, css))
    shutil.copy(os.path.join(RACINE, "data", "icones", "mevcine.svg"), os.path.join(SORTIE, "mevcine.svg"))
    total = 0
    for code in LANGUES:
        textes, pages = sites[code]
        noms = [nom for nom, _, _ in pages]
        if "index" not in noms:
            sys.exit(f"aide/{code}/pages.txt : pas de page index")
        dossier_sortie = os.path.join(SORTIE, code)
        os.makedirs(dossier_sortie, exist_ok=True)
        # les pages supprimées des sources disparaissent du site
        for f in os.listdir(dossier_sortie):
            if f.endswith(".html") and f[:-5] not in noms:
                os.remove(os.path.join(dossier_sortie, f))
        for k, (nom, titre_court, _) in enumerate(pages):
            source = os.path.join(SOURCES, code, f"{nom}.html")
            with open(source, encoding="utf-8") as f:
                fragment = f.read().strip()
            # le sommaire : une liste par partie (« Pour les élèves », « Pour les professeurs »), pages
            # numérotées
            liens = []
            for i, (n, t, partie) in enumerate(pages):
                if partie or i == 0:
                    if i > 0:
                        liens.append("  </ol>")
                    if partie:
                        liens.append(f'  <p class="group">{html.escape(partie)}</p>')
                    liens.append("  <ol>")
                actuel = ' class="current"' if n == nom else ""
                liens.append(f'    <li><a href="{n}.html"{actuel}>{i + 1}. {html.escape(t)}</a></li>')
            liens.append("  </ol>")
            liens_pages = "\n".join(liens)
            # la même page dans les autres langues (si elle y existe)
            liens_langues = " ".join(
                f'<a href="../{autre}/{nom}.html" lang="{autre}" hreflang="{autre}">{html.escape(sites[autre][0]["langue"])}</a>'
                for autre in LANGUES
                if autre != code and nom in [n for n, _, _ in sites[autre][1]]
            )
            voisins = []
            if k > 0:
                n, t, _ = pages[k - 1]
                voisins.append(f'<a class="precedent" href="{n}.html"><small>◀ {html.escape(textes["precedent"])}</small>{html.escape(t)}</a>')
            if k + 1 < len(pages):
                n, t, _ = pages[k + 1]
                voisins.append(f'<a class="suivant" href="{n}.html"><small>{html.escape(textes["suivant"])} ▶</small>{html.escape(t)}</a>')
            page = gabarit
            for cle, valeur in {
                "code": code,
                # « MeVCine — Démarrer », et pour la présentation, « MeVCine — Notice d'utilisation »
                "titre_page": html.escape(
                    f"MeVCine — {textes['titre']}" if titre_h1(fragment, source) == "MeVCine"
                    else f"MeVCine — {titre_h1(fragment, source)}"),
                "titre_site": html.escape(textes["titre"]),
                "version": v,
                "liens_langues": liens_langues,
                "sommaire": html.escape(textes["sommaire"]),
                "liens_pages": liens_pages,
                "contenu": fragment,
                "voisins": "\n".join(voisins),
                "pied": html.escape(textes["pied"].replace("{version}", v)),
            }.items():
                page = page.replace("{{" + cle + "}}", valeur)
            # {{version}} reste : MeVCine le remplace en copiant la notice
            reste = re.search(r"{{(?!version}})\w+}}", page)
            if reste:
                sys.exit(f"{source} : marque {reste.group(0)} non remplacée")
            with open(os.path.join(dossier_sortie, f"{nom}.html"), "w", encoding="utf-8") as f:
                f.write(page)
            total += 1
    print(f"{total} pages écrites dans {SORTIE}")


if __name__ == "__main__":
    main()
