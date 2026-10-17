# MeVReg — traitement de données

Rémi MEVAERE — logiciel libre sous licence MIT.

MeVReg traite les données des TP, en C++ 20 avec Qt 6 (Widgets) : tableur de grandeurs à identifiant
invariable, formules et paramètres à curseur, graphes, modélisation (17 modèles), incertitudes,
dérivée, intégrale, spectre de Fourier, méthodes de dosage, statistiques, Python intégré (mini-numpy,
mini-matplotlib, mini-pandas et module `mevreg`), fenêtres de dessin (`fenetre`), import CSV avec
aperçu, exports PNG, SVG, PDF et CSV, compte rendu du TP, export vers Python pour Capytale.
L'interface est en français.

L'utilisation est décrite dans la notice ([`assets/help/notice.html`](assets/help/notice.html),
touche `F1` ou menu **Aide → Notice d'utilisation…**, ouverte dans le navigateur) : une partie pour
les élèves (fenêtre, saisie, import CSV, calculs, incertitudes, graphes, modélisation, exports), puis
une pour les professeurs (Python, préparation d'un TP, installation). La référence Python
([`assets/help/python.html`](assets/help/python.html)) décrit les fonctions de numpy, matplotlib et
pandas disponibles et toutes celles du module `mevreg`, avec leurs paramètres. Les images de la
notice viennent des essais de l'interface : `MEVREG_SHOTS=build/captures ./build/test_app`, puis
`python3 -I tools/images_notice.py build`.

Pas d'exemples livrés : élèves et professeurs ouvrent leurs propres fichiers. Seul le menu
**Python → Exemples** propose des scripts courts.

## Interface

MeVReg suit le style commun des logiciels de la MevSuite :

- **Fenêtre** ouverte à sa taille normale (1200 × 700 au premier lancement, ramenée à l'écran),
  agrandie seulement si l'élève l'avait agrandie ; titre `MeVReg — document`. Écran de démarrage
  de deux secondes (un clic l'efface).
- **Page d'accueil** tant qu'aucune fenêtre n'est ouverte : **Nouveau tableur** et **Ouvrir…**
  (document `.phy` ou fichier de mesures CSV), ou un fichier glissé sur la fenêtre.
- **Barre d'outils** unique et fixe : les fenêtres de travail (Grandeurs, Tableur, Graphe, Python),
  avec des icônes dessinées au trait. Ouvrir, enregistrer, annuler et rétablir sont dans les menus.
  À droite de la barre des menus : le thème (soleil ou lune) puis le drapeau, désactivé : MeVReg
  n'existe pour l'instant qu'en français.
- **Fenêtres ancrées** : titres en demi-gras, boutons Détacher et Fermer de la hauteur d'une ligne de
  texte ; fermer demande s'il faut cacher la fenêtre (menu Fenêtres pour la rouvrir) ou la
  supprimer (`Ctrl+Z` la rend). Les fenêtres de réglages et de résultats (Grandeurs, Calculs,
  Modélisation, Statistiques) ont le fond un peu plus sombre des panneaux latéraux de la suite. La
  Modélisation s'ouvre à part, à côté de la fenêtre principale.
- **Barre d'état** d'une ligne : pictogramme et message (vert : réussi, orange : à vérifier, rouge :
  erreur) ; à droite, le document, le titre du TP et le nombre de grandeurs, puis les formules en
  erreur.

Les fichiers `.phy` sont ceux de la version Rust : un espace de travail enregistré par l'une s'ouvre
dans l'autre. Seule la disposition des fenêtres, propre à chaque version, est alors refaite.

## Graphes et barres d'incertitude

Le moteur de tracé (`src/plot/`) dessine avec QPainter, par le même code à l'écran, en PNG (×3), en
SVG et en PDF (vectoriels) et sur l'imprimante : le rendu est identique partout. Les polices DejaVu
sont intégrées au programme, pour que les graphes ne dépendent pas des polices du système.

- **Traits nets** : à l'écran et en PNG, les traits fins (axes, graduations, quadrillage, barres
  d'incertitude) sont alignés sur la grille des pixels, y compris sur les écrans haute densité ;
  en SVG et en PDF, les coordonnées restent exactes.
- **Barres d'incertitude** : barres verticales, barres en x et en y, ou rectangles d'incertitude
  (fond translucide et contour). Les barres sont dessinées sans arrondi, leurs embouts sont
  centrés au pixel près, et leur épaisseur et la taille des embouts se règlent (embouts
  proportionnés au marqueur par défaut, 0 pour des barres sans embout).
- **Lisibilité** : une barre s'arrête au bord d'un marqueur plein au lieu de le traverser ; les
  marqueurs creux sont remplis de la couleur du fond ; les barres sont dessinées sous les courbes
  et les marqueurs. Une barre qui sort du graphe est coupée net au bord, sans embout trompeur ; en
  échelle logarithmique, une borne inférieure négative est prolongée jusqu'au bord.
- **Légende** : chaque entrée montre le marqueur avec ses barres (ou son rectangle) ; la légende se
  place dans le coin où elle cache le moins de points.
- **Réticule** : accroché à un point, il affiche la valeur avec son incertitude, arrondie à la même
  décimale (`U = (5,042 ± 0,070) V`).
- **Incertitude créée en un clic** : boîte « Incertitude » (constante ou formule, par exemple
  `0.02*U + 0.01`), colonne `u_x` placée à droite de `x`, barres affichées aussitôt ; les réglages
  d'une courbe signalent une incertitude manquante et proposent de la créer.

Le fichier `.phy` enregistre en plus `error_width` et `cap_size` pour chaque courbe ; la version Rust
les ignore.

## Différences avec l'ancienne version Rust

MeVReg a d'abord été écrit en Rust avec egui, sous le nom RegMev (version retirée du dépôt). Ce qui
change :

- **Python** : interpréteur CPython intégré (au lieu de RustPython), isolé (`site` non chargé, aucun
  module installé sur la machine n'est visible). Les modules `numpy`, `matplotlib.pyplot` et
  `fenetre` sont ceux de la version Rust : le calcul est fait en C++, l'affichage des tableaux est celui de
  numpy (vérifié sur les mêmes 137 cas de référence que la version Rust). Les scripts tournent dans
  un fil à part et s'interrompent par le bouton Arrêter. Une liste de nombres ou un tableau numpy d'un
  script devient une grandeur, du nom de la variable (sans unité ; `grandeur(nom, valeurs, unité)`
  en donne une avec son unité) ; une grandeur du document changée dans le script ne l'est pas dans le
  document (un message le dit). En plus :
  - `pandas` (mini-pandas) : `DataFrame`, `Series`, `read_csv` (séparateurs devinés comme à
    l'import, unités des en-têtes dans `df.attrs["unites"]`), `to_csv`, `describe`, `mean`, `std`
    (cases vides ignorées, écart-type corrigé), `dropna`, `sort_values`, `iloc`, `concat`, `plot` ;
  - `mevreg` (`import mevreg as mr`), une aide pour exploiter tout le logiciel : grandeurs (`creer`,
    `calculer`, `modifier`, `renommer`, `supprimer`, `incertitude`, `parametre`), `tableau` et
    `importer` (avec pandas), `lire_csv`, `tableur`, graphes (`graphe`, `courbe`, `axes`,
    `verticale`, `horizontale`, `oblique`, `texte`, `retirer_courbe`), `modeliser` (résultat avec
    incertitudes, modèle tracé sur le graphe), fenêtres (`ouvrir`, `cacher`, `fermer`, `detacher`,
    `rattacher`, `disposition`), fichiers (`enregistrer`, `exporter_csv`, `exporter_graphe`),
    `message`. Les lectures sont immédiates ; les changements sont faits à la fin du script, dans
    l'ordre. Les chemins relatifs sont rapportés au dossier du document.
  - menu **Python → Exemples** : une quarantaine d'exemples courts, par thème (grandeurs, graphes,
    modélisation, fenêtres et fichiers, divers), chacun dans un nouveau script prêt à exécuter ;
    l'export Capytale commence par `plt.clf()`.
- **Graphes** : droites verticales, horizontales et obliques, textes, glissés à la souris (une
  droite oblique s'incline par ses poignées), sauf s'ils sont verrouillés (cadenas des réglages) ;
  taille du texte à côté de sa couleur. Incertitudes : une grandeur au choix pour chaque côté
  (haut, bas, gauche, droite), pour des barres ou des rectangles dissymétriques. Colonnes d'un
  tableur glissées sur un graphe (par l'en-tête, ou sélectionnées, Ctrl + clic pour plusieurs : la
  première en abscisse). Modélisation dans une fenêtre à part ; plage des points et étendue de la
  courbe choisies par des barres glissées sur le graphe ; la courbe du modèle se prolonge sur tout
  le graphe ; nom du modèle dans la légende. Méthode des tangentes : couleur, verrou, suppression
  (onglet Modèles des réglages, ou clic droit) ; retirer la courbe la retire. Réglages du graphe
  jamais écrasés (devant le graphe s'il manque de place), fermés par leur croix.
- **Tableur** : colonnes réparties sur la largeur (au moins une largeur minimale, puis défilement),
  élargies à la main (double-clic sur la poignée : largeur automatique) ; numéros de lignes
  centrés. Export CSV réglable : séparateur des valeurs, séparateur décimal, en-têtes, codage, avec
  un aperçu.
- **Fenêtres** : fenêtres ancrables de Qt. Glissée par sa barre de titre ou par son onglet, une
  fenêtre ne se place pas là où Qt le devine d'après le pointeur : comme dans la version Rust, des repères
  s'affichent sur la fenêtre survolée (au centre : en onglet ; sur un côté : à côté) et sur les bords
  de l'espace de travail (sur toute la hauteur ou toute la largeur), avec l'aperçu de la place prise ;
  Échap annule. Une fenêtre ouverte ensuite s'ajoute sans défaire la disposition choisie. Le bouton
  Détacher en fait une fenêtre à part, avec le cadre du système (déplacée, agrandie, fermée comme les
  autres, sous Wayland aussi : le cadre que Qt dessine aux fenêtres ancrables flottantes ne s'y
  déplace pas) ; son bouton **Rattacher**, ou **Fenêtres → Rattacher les fenêtres détachées**, la
  remet en place. Fermer la fenêtre d'un tableur, d'un graphe, d'un script ou d'un dessin demande
  confirmation : il reste dans le document, le menu Fenêtres le rouvre (« Ne plus demander », que
  les préférences rétablissent). La disposition est enregistrée dans le `.phy` sous une clé propre à
  cette version.
- **Import CSV** (menu Fichier, fenêtre Grandeurs, tableur, ou fichier glissé sur la fenêtre) : un
  aperçu montre le fichier ligne par ligne (noms, unités, valeurs, ignorée) et les colonnes qui seront
  importées, à cocher, à renommer ; séparateur des valeurs, séparateur décimal, lignes des noms et des
  unités, première ligne de valeurs et lignes vides sont devinés et se corrigent. Une case écrite avec
  l'autre séparateur décimal n'est pas devinée : elle est signalée en rouge, comme les lignes d'un
  autre nombre de cases et les noms déjà pris.
- **Impression** : boîte d'impression du système (Qt Print Support), au lieu de l'ouverture du PDF
  dans la visionneuse.
- **Réglages** : `mevreg.ini`, dans le dossier commun à la suite (Linux : `~/.config/MeVSuite/`,
  Windows : `%APPDATA%\MeVSuite\`) ; l'ancien `MevSuite/mevreg.ini` y est déplacé : `theme = auto | light | dark`, `window_width`,
  `window_height`, `maximized`, `ui_zoom`, `sig_digits`, `dark_plots`, `confirm_close`,
  `recent`. La virgule décimale n'est pas un réglage : elle suit la langue (français : virgule).
- **Thème clair ou sombre** : celui du bureau (`theme = auto`), suivi pendant que le programme tourne
  (sous Linux, lu auprès du portail du bureau `org.freedesktop.appearance`), tant que le bouton
  soleil / lune de la barre de menus n'a pas servi. Palette sombre commune à la suite. En thème
  sombre, les graphes passent sur fond sombre (réglage dans les préférences) ; les exports restent
  sur fond blanc.
- **Annuler** : les modifications du code d'un script ne sont pas des étapes d'annulation du
  document ; l'éditeur a son propre « annuler ».
- **Graphiques** : Qt Charts n'est pas utilisé (licence GPL, incompatible avec la licence MIT).

## Compilation

Prérequis : CMake ≥ 3.21, un compilateur C++ 20 (GCC ≥ 13 ou Clang ≥ 17), Qt ≥ 6.8 (modules Core,
Gui, Widgets, Svg, PrintSupport, Concurrent, Test, et DBus sous Linux) et Python ≥ 3.10 avec ses
fichiers de développement.

| Système          | Paquets                                                                         |
|------------------|---------------------------------------------------------------------------------|
| Fedora 45        | `cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtsvg-devel python3-devel`      |
| Ubuntu 26.04     | `cmake ninja-build g++ qt6-base-dev qt6-svg-dev python3-dev`                    |
| Windows (MSYS2)  | `mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-svg,python}`         |

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/mevreg  (build/mevreg tp.phy ouvre directement un espace de travail)
ctest --test-dir build --output-on-failure
```

Options : `-DMEVREG_APP=OFF` compile seulement le cœur et ses tests (sans la fenêtre ; Qt Widgets
reste nécessaire, pour Qt Print Support),
`-DMEVREG_TESTS=OFF` ne compile pas les tests.

Les tests (Qt Test, `tests/`) : `test_core` (formules, document, modélisation, statistiques, FFT,
dosage, CSV, fichiers `.phy` de la version Rust), `test_python` (interpréteur, mini-numpy comparé à numpy
caractère par caractère, mini-matplotlib, interruption), `test_plot` (graduations, mise en page,
rendu des barres d'incertitude au pixel près, exports) et `test_app` (fenêtre principale, exemples,
enregistrement et réouverture avec la disposition, annuler, réglages, modélisation, scripts, boîtes
de dialogue, déplacement des fenêtres). Avec `MEVREG_SHOTS=dossier`, `test_plot` et `test_app` y
enregistrent des captures de contrôle.

À l'exécution, Python utilise la bibliothèque standard de l'interpréteur avec lequel le programme a
été compilé, ou celle du dossier `python/` placé à côté de l'exécutable s'il existe (version
distribuée). Sous Windows, les mêmes commandes se lancent dans le terminal UCRT64 de MSYS2
(MinGW-w64) ; `platform/mevreg.rc.in` ajoute à l'exécutable son icône et ses informations de
version.

La CI ([`ci.yml`](../.github/workflows/ci.yml)) compile MeVReg et lance ses tests sous Fedora 45,
Ubuntu 26.04 et Windows. Les versions distribuables (version portable et installateur pour Windows, AppImage, `.deb`,
`.rpm`) sont fabriquées par `../packaging/` et publiées dans les
[Releases](https://github.com/melectronvolt/MevSuite/releases) du dépôt (voir le `README.md` de la
racine).

## Organisation du code

- `src/core/` : sans interface. `format` (nombres, unités), `expr` (formules), `document`
  (grandeurs, paramètres, recalcul), `numeric`, `series`, `linalg`, `spline`, `fft`, `stats`,
  `titration`, `fit` (modèles et ajustement), `plotspec` (description des graphes), `workspace`
  (espace de travail, fichier `.phy`), `csv`, `pyexport` (export vers Python), `drawing`
  (fenêtres de dessin).
- `src/plot/` : `ticks` (graduations), `resolve` (valeurs à tracer, modèles, tangentes),
  `renderer` (mise en page et dessin, barres d'incertitude), `export` (PNG, SVG, PDF),
  `report` (documents de plusieurs pages : tableur, script, compte rendu).
- `src/python/` : `engine` (interpréteur dans son fil, entrées et sorties des scripts), `native`
  (module `_mevreg` : tableaux, chargement des modules intégrés, ordres et modélisation du module
  `mevreg`, lecture des CSV de pandas), `arr` et `npformat` (calculs et affichage de type numpy),
  `figure` (figures de matplotlib) ; les modules Python (`numpy`, `matplotlib`, `pandas`, `mevreg`,
  `fenetre`) sont dans `python/`.
- `src/app/` : `store` (état partagé, annuler/rétablir, scripts en cours), `mainwindow` (menus,
  fenêtres ancrables, fichiers, exports, impression), `quantitiespanel`, `calculationspanel`,
  `tableview`, `graphwidget` et `graphsettings` (graphe et ses réglages), `modelingpanel`,
  `statspanel`, `scriptpanel`, `drawingview`, `dialogs`, `csvimport` (aperçu d'import, export CSV
  réglable), `pyexamples` (menu Python → Exemples), `docking` (déplacement des fenêtres par
  repères de dépôt), `scripting` (résultats des scripts),
  `examples` (documents des essais), `help` (notice écrite sur le disque et ouverte dans le
  navigateur), `about`, `theme`, `icons` (icônes dessinées au trait), `widgets`.
- `assets/` : polices DejaVu, icônes, notice (`help/` : pages, feuille de style, captures).
- `tools/images_notice.py` : images de la notice à partir des captures des essais.
- `platform/` : ressources de l'exécutable Windows, lanceur Linux (`mevreg.desktop`).
