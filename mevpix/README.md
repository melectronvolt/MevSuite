# MeVPix — mesurer des longueurs

Rémi MEVAERE ([suite@sciences-physiques.net](mailto:suite@sciences-physiques.net)) — logiciel libre
sous licence MIT.

MeVPix mesure des longueurs sur n'importe quelle image, en C++ 20 avec Qt 6 (Widgets) :
étalonnage sur un objet de longueur connue, mesures, profil d'intensité le long d'une ligne de
coupe (canal, épaisseur, curseurs A et B, saturation, export CSV), notice HTML intégrée, interface
en français et en anglais, thème clair ou sombre, réglages imposés (à côté du programme sous
Windows, dans `/etc/MeVSuite/` sous Linux).

L'utilisation est décrite dans la notice ([`assets/help/fr.html`](assets/help/fr.html), touche `F1`
dans le programme). Elle s'adresse d'abord aux élèves (le logiciel est pédagogique), puis aux
professeurs (installation, réglages imposés, journal des erreurs). Ses captures d'écran sont faites
par les essais de l'interface sur une figure de diffraction ([`tests/data/diffraction1.png`](tests/data/)).

## Interface

L'interface suit le style commun de la MevSuite (implémentation de référence dans
MeVSono) : mêmes places, mêmes couleurs, mêmes tailles dans les quatre logiciels.

- **Page d'accueil** (aucune image ouverte) : logo, nom, sous-titre, un seul bouton
  `📂 Ouvrir une image…` (pas d'exemple intégré au programme) et la ligne « ou glissez une image sur
  la fenêtre ». Elle tient dans la taille minimale de la fenêtre.
- **Barre d'outils** : ouvrir une image, les trois outils (Étalonner, Mesurer, Profil d'intensité),
  ajuster à la fenêtre, échelle en cours (cachée sans image). La notice s'ouvre par le menu Aide
  (`F1`). À droite de la barre des menus, deux pictogrammes dessinés par le programme
  (`src/icons.cpp`) : le **soleil ou la lune** (thème
  affiché ; un clic passe à l'autre) puis le **drapeau** de la langue (un clic passe à l'autre). Les
  drapeaux en émoji ne s'affichent pas sous Windows, d'où le dessin. Il n'y a pas de menus Langue
  et Thème.
- **Panneau latéral** : fond un peu plus sombre que la fenêtre, filet du côté de l'image ; jamais
  plus étroit que son contenu (il s'élargit avec l'agrandissement de l'interface), 300 px au départ.
  Titres en demi-gras à la taille du texte, indications en couleur discrète ; curseurs A et B plus
  foncés en thème clair (panneau, graphique), toujours vifs sur l'image.
- **Barre d'état** : bande distincte (fond plus foncé, filet au-dessus), d'une ligne de haut. À
  gauche, un pictogramme et le message : l'étape suivante (information), une réussite en vert
  (étalonnage validé, tableau copié, profil exporté), un problème en rouge (image illisible, export
  impossible…). À droite, l'image ouverte (nom, dimensions, en couleur discrète) et son zoom.
- **Thème** : au premier lancement, celui du bureau (suivi pendant que le programme tourne), puis le
  choix du bouton, enregistré dans `mevpix.ini` (`theme = auto`, `light` ou `dark`). Le thème sombre
  est celui de MeVPix (`src/theme.cpp`), plus sombre que ceux des styles de Qt.
- **Fenêtre** : elle s'ouvre à sa taille normale (1200 × 700, ramenée à l'écran s'il est plus petit,
  centrée), agrandie seulement si l'élève l'a agrandie à la séance précédente. Barre de menus
  standard de Qt : `Fichier`, `Aide`.
- **Zone image** : chaque outil n'affiche que ses segments : l'étalon (jaune) avec Étalonner, les
  mesures (turquoise) avec Mesurer, la ligne de coupe (rose) avec Profil d'intensité.

## Différences avec l'ancienne version Rust

MeVPix a d'abord été écrit en Rust avec egui, sous le nom PixMev (version retirée du dépôt). Ce qui
change :

- **Fichiers** : `mevpix.ini` et `mevpix.log`, dans le dossier commun à la suite (Linux :
  `~/.config/MeVSuite/`, Windows : `%APPDATA%\MeVSuite\`) ; ceux de l'ancien dossier `mevpix` y sont
  déplacés. Pages de la notice : dossier temporaire du système. `mevpix.ini` a le format de `pixmev.ini`, celui
  de la version Rust : ce fichier se recopie et se renomme (la clé `theme` est propre à cette version).
- **Graphique du profil** : dessiné par le programme (`src/plot.cpp`). Qt Charts n'est pas utilisé :
  il est distribué sous GPL, incompatible avec la licence MIT de MeVPix. Les gestes sont ceux de la
  version Rust : molette pour zoomer, `Maj` + molette pour se déplacer, double-clic pour revenir à la
  vue complète, clic ou glisser pour placer A et B.
- **Agrandissement de l'interface** (`Ctrl` + / `Ctrl` − / `Ctrl` 0) : il change la taille des
  polices (et des traits, poignées et étiquettes sur l'image), de 50 % à 300 %.
- **Thème clair ou sombre** : bouton soleil/lune (voir plus haut). Sous Linux, le thème du bureau
  est lu auprès du portail (`org.freedesktop.appearance`), car Qt ne le voit pas toujours (GNOME en
  sombre détecté comme clair) ; ailleurs, Qt suit le système. La zone image est claire en thème clair,
  sombre en thème sombre (elle restait toujours sombre dans la version Rust).
- **Boîtes de dialogue** : celles de Qt (portail du bureau ou boîte de Qt) ; plus besoin de zenity.
  Leurs boutons sont traduits avec les traductions de Qt (`qtbase_fr.qm`) quand elles sont installées.
- **Formats d'image** : ceux des greffons Qt installés (PNG, JPEG, BMP, GIF intégrés ; TIFF et WebP
  avec le module Qt Image Formats ; d'autres selon le système). L'orientation EXIF est appliquée.
- **À propos** : composants principaux (GCC, Qt, Qt Image Formats, MinGW-w64) et bouton « À propos de
  Qt ». Les bibliothèques livrées dépendent de la version : paquets du système pour le `.deb` et
  le `.rpm`, Qt et ses dépendances copiés dans l'AppImage et dans le dossier Windows
  (`../packaging/`).

## Compilation

Prérequis : CMake ≥ 3.21, un compilateur C++ 20 (GCC ≥ 13 ou Clang ≥ 17) et Qt ≥ 6.8 (modules
Core, Gui, Widgets, Concurrent, Test, et DBus sous Linux).

| Système          | Paquets                                                                     |
|------------------|-----------------------------------------------------------------------------|
| Fedora 45        | `cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtimageformats`             |
| Ubuntu 26.04     | `cmake ninja-build g++ qt6-base-dev qt6-image-formats-plugins`              |
| Windows (MSYS2)  | `mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-imageformats}`   |

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/mevpix
ctest --test-dir build --output-on-failure
```

Les tests (Qt Test) :

- `tests/tests.cpp` (`mevpix_tests`) reprend ceux de la version Rust : géométrie, formatage des
  nombres, échantillonnage du profil, CSV, réglages, langues, notice (chaque image, lien et ancre
  existe), chargement des images.
- `tests/uitests.cpp` (`mevpix_uitests`) pilote l'interface sans écran, comme un élève, sur la photo
  d'exemple : ouvrir, étalonner sur 2 cm du papier millimétré, mesurer la tache centrale (20,2 mm),
  profil d'intensité avec A et B sur les minima, boutons de thème et de langue. Il vérifie aussi le
  style commun : page d'accueil sans chevauchement à la taille minimale, boutons de droite de la
  barre de menus, barre d'état d'une ligne, panneau jamais rogné (interface agrandie comprise).
  Chaque étape est rendue dans `build/captures/` ; les images de la notice en viennent :

  ```sh
  python3 -I tools/images_notice.py build     # il faut Pillow
  ```

Une traduction incomplète est refusée à la compilation, comme en Rust : les tables `Tr` de
`src/i18n.cpp` sont compilées avec `-Werror=missing-field-initializers`.

Sous Windows, les mêmes commandes se lancent dans le terminal UCRT64 de MSYS2 (MinGW-w64) ;
`platform/mevpix.rc.in` ajoute à l'exécutable son icône et ses informations de version. Pour copier
le programme sur un autre poste, il faut mettre les DLL de Qt à côté (c'est ce que fait
`../packaging/windows.sh`).

La CI ([`ci.yml`](../.github/workflows/ci.yml)) compile MeVPix et lance ses tests sous Fedora 45,
Ubuntu 26.04 et Windows. Les versions distribuables (version portable et installateur pour Windows, AppImage, `.deb`,
`.rpm`) sont fabriquées par `../packaging/` et publiées dans les
[Releases](https://github.com/melectronvolt/MevSuite/releases) du dépôt (voir le `README.md` de la
racine).

## Organisation du code

- `src/model.*` : segments, unités, étalonnage, formatage des nombres (séparateur décimal selon la langue).
- `src/profile.*` : échantillonnage du profil (interpolation bilinéaire, bande de moyennage,
  saturation) et export CSV.
- `src/view.*` : chargement de l'image (orientation EXIF comprise, réductions pour l'affichage), zoom
  et déplacement, dessin des segments et des étiquettes.
- `src/session.*` : état du travail (outil, étalon, mesures, coupe, profil mis en cache).
- `src/canvas.*` : zone image (souris, molette, pincement, tracé et poignées).
- `src/plot.*` : graphique du profil d'intensité.
- `src/app.*` : fenêtre principale (menus, barre d'outils, page d'accueil, panneau latéral, barre
  d'état, chargement en arrière-plan, enregistrement des réglages).
- `src/icons.*` : pictogrammes dessinés (soleil, lune, drapeaux, barre d'état).
- `src/i18n.*` : traductions (français, anglais) et langue du système.
- `src/settings.*` : lecture et écriture de `mevpix.ini`.
- `src/theme.*` : thème clair ou sombre (choisi, ou celui du bureau).
- `src/crash.*` : erreurs fatales, journal `mevpix.log` et boîte de message avant la fermeture.
- `src/help.*` : écriture de la notice (`assets/help/`, ressources Qt) sur le disque.
- `src/about.*` : écran d'accueil, fenêtre « À propos », logo.
- `assets/` : notice (pages, feuille de style, captures d'écran) et icônes.
- `tests/data/` : figure de diffraction sur papier millimétré, utilisée par les essais de
  l'interface.
- `tools/images_notice.py` : images de la notice à partir des captures des essais.
