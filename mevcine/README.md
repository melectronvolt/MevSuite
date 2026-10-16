# MeVCine — étude cinématique de vidéos

Rémi MEVAERE — logiciel libre sous licence GPL version 3 ou ultérieure : voir `COPYING`, et `NOTICE`
pour les composants libres intégrés et leurs licences.

MeVCine, en C++ 23 avec Qt 6 (Widgets), sert à étudier le mouvement d'objets filmés : mode
simplifié en six étapes (origine, axes, échelle, pointage, tableau, export) et mode expert en
quatre onglets (pointage avec plusieurs objets et suivi automatique, coordonnées, trajectoires avec
chronophotographie, chronogramme, vecteurs vitesse et changement de référentiel, grapheur) ; lecture des vidéos de téléphone par ffmpeg (HEVC, HDR, portrait, ralenti, cadence
variable) ; export en CSV et en tableaux numpy ; projets `.mevcine` et anciens `.mecavideo` ;
confirmation avant de perdre des mesures et sauvegarde automatique ; notice HTML ; interface en
français et en anglais, thème clair ou sombre. L'utilisation est décrite dans la notice (`F1`), écrite
pour les élèves d'abord, pour les professeurs ensuite.

## Différences avec l'ancienne version Rust

MeVCine a d'abord été écrit en Rust avec egui, sous le nom CineMev (version retirée du dépôt). Ce qui
change :

- **Fichiers** : `mevcine.ini`, `mevcine-secours.mevcine` et `mevcine.log`, dans le dossier commun
  à la suite (`~/.config/MeVSuite/` sous Linux, `%APPDATA%\MeVSuite\` sous Windows) ; ceux de
  l'ancien dossier `mevcine` (`mevcine.conf`, `secours.mevcine`, `mevcine-erreurs.log`) y sont
  déplacés et renommés ; variables d'environnement `MEVCINE_CONF` et `MEVCINE_DATA`. Les formats sont
  ceux de la version Rust : `mevcine.ini` est l'ancien `cinemev.conf`, et les projets `.mevcine` sont les anciens `.cinemev`
  (signature `mevcine` au lieu de `cinemev`). Les projets `.cinemev` s'ouvrent toujours.
- **Interface** : le style commun aux logiciels de la suite (modèle
  `mevsono/src`). Pas de barre d'outils : ouvrir, enregistrer, mode et notice sont dans les menus ; à
  droite de la barre des menus, le soleil ou la lune du thème et le drapeau de la langue
  (dessinés par le programme dans `src/app/icones.cpp`), qui remplacent les menus Langue et Thème ; une barre
  d'état typée d'une ligne (consigne, réussite, attente, erreur ; vidéo, image et mode à droite) ;
  des pages d'accueil épurées (logo, nom, deux cartes ou un bouton, une ligne) ; un panneau latéral
  plus sombre, jamais rogné ; `Ctrl` + / `Ctrl` − / `Ctrl` 0 agrandissent toute l'interface (de 50 à
  300 %, réglage `agrandissement`). La fenêtre s'ouvre à sa taille normale (1200 × 700, ramenée à
  l'écran), jamais agrandie d'office. Mode simplifié : la loupe et un petit tableau des coordonnées en haut à
  gauche, les consignes au milieu, Précédent et Suivant toujours visibles ; l'origine se déplace aussi
  en la faisant glisser (étapes 1 et 2). Mode expert : une barre « 1. Origine › 2. Axes › 3. Échelle »
  en haut de l'onglet Pointage, les onglets dans l'ordre Pointage, Coordonnées, Trajectoires,
  Grapheur, et des vecteurs vitesse à échelle automatique, avec une légende ; « ⏹ Arrêter le
  pointage » puis « ▶ Reprendre le pointage » ; l'onglet Coordonnées donne toute la place au tableau,
  l'export tient sur une ligne. Dans les deux modes, le pointage se fait aussi en plein écran (`F11`),
  la loupe restant affichée dans un cadre qui s'écarte de la souris. La longueur de l'étalon
  se donne en mm, en cm ou en m (la version Rust n'acceptait que le mètre) ; les exports restent en
  mètre.
- **Thème** : `src/app/theme.cpp` suit le thème du bureau (sous Linux, lu auprès du portail
  `org.freedesktop.appearance` par D-Bus, car Qt ne le voit pas toujours) jusqu'au premier choix,
  retenu ensuite (`theme = light` ou `dark` dans `mevcine.ini`, `auto` pour suivre le bureau ; les
  anciennes valeurs `clair` et `sombre` sont lues) ; le thème sombre a sa propre palette, celle de toute
  la suite, plus sombre que celle de Fusion.
- **Affichage** : Qt Widgets dessine sans accélération graphique ; il n'y a plus de moteur de rendu à
  choisir (`MEVCINE_RENDU`), ni de police de secours livrée (`data/polices`) : les symboles des boutons
  viennent des polices du système.
- **Dialogues** : ceux de Qt (dialogues de fichiers du bureau, sinon de Qt ; messages et
  confirmations dans des fenêtres modales, toujours un à la fois). La touche Entrée ne confirme
  jamais une action qui ferait perdre des mesures. Les boutons des dialogues de Qt sont traduits avec
  les traductions de Qt (`qtbase_fr.qm`) quand elles sont installées.
- **Lecture des vidéos** : la même méthode (`ffprobe`, puis `ffmpeg` qui envoie des images PPM dans un
  tube, dates lues dans le journal du filtre `showinfo`), avec `QProcess`. Les images sont des
  `QImage` (RVB 24 bits) ; la conversion HDR et le suivi automatique sont calculés en parallèle avec
  Qt Concurrent (au lieu de rayon).
- **Grapheur** : dessiné par le programme (`src/app/onglet_graph.cpp`), sans Qt Charts : molette pour
  zoomer, glisser pour se déplacer, double-clic pour revenir à la vue complète.
- **Traductions** : le même format gettext, `data/lang/en.po` (repris de MeVCine). Les textes sont
  écrits en français dans `trad("…")` (et non `tr`, qui désignerait `QObject::tr` dans une classe Qt).
  Le test `traductions_completes` vérifie que chaque texte est traduit, avec les mêmes arguments ; le
  modèle `data/lang/mevcine.pot` se refait avec `MEVCINE_ECRIT_POT=1 build/mevcine_tests modelePo`.
- **À propos** : composants principaux (pymecavideo, Qt, FFmpeg, GCC, mingw-w64), notice de licence de
  pymecavideo, et bouton « À propos de Qt ». Les bibliothèques livrées dépendent de la version :
  paquets du système pour le `.deb` et le `.rpm`, Qt et ses dépendances copiés dans l'AppImage et
  dans le dossier Windows, avec ffmpeg et ffprobe (`../packaging/`).
- **Notice** : un site (`aide/` → `data/help/`) en deux parties, « Pour les élèves » puis « Pour les
  professeurs » (lignes `== titre` de `pages.txt`) ; ses captures d'écran sont faites par les tests de
  l'interface Qt, et la vue d'ensemble de la fenêtre est annotée de pastilles numérotées (SVG). Sa
  feuille de style est celle de toute la suite (`aide/help.css`, copie exacte de celle de MeVSono,
  vérifiée par le test `aide_dans_chaque_langue`) ; `aide/aide.css` y ajoute ce qui sert à un site de
  plusieurs pages.
- **Vidéos** : pas de vidéos d'exemple dans l'interface, chacun ouvre les siennes ; celles de
  `data/video` (vidéos de pymecavideo, CC BY-SA) servent aux essais et aux captures de la notice, et ne
  sont pas livrées.

## Compilation

Prérequis : CMake ≥ 3.21, un compilateur C++ 23 (GCC ≥ 13, pour `std::expected`), Qt ≥ 6.8 (modules
Core, Gui, Widgets, Concurrent, Test, et DBus sous Linux ; 6.8 pour le choix du thème) ; à l'exécution, `ffmpeg` et `ffprobe` (dans le dossier `ffmpeg`
à côté du programme, sinon dans le `PATH`).

| Système          | Paquets                                                        |
|------------------|----------------------------------------------------------------|
| Fedora 45        | `cmake ninja-build gcc-c++ qt6-qtbase-devel ffmpeg` (RPM Fusion) |
| Ubuntu 26.04     | `cmake ninja-build g++ qt6-base-dev ffmpeg`                    |
| Windows (MSYS2)  | `mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,ffmpeg}` |

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mevcine                        # écran d'accueil : choix du mode
./build/mevcine --simple chute.mp4     # mode simplifié, avec une vidéo
./build/mevcine --expert projet.mevcine
./build/mevcine -d 2 ...               # messages de débogage
ctest --test-dir build --output-on-failure
```

Sous Windows, les mêmes commandes se lancent dans le terminal UCRT64 de MSYS2 (MinGW-w64) ;
`platform/mevcine.rc.in` ajoute à l'exécutable son icône et ses informations de version. Pour copier
le programme sur un autre poste, il faut mettre les DLL de Qt à côté (`windeployqt`), et ffmpeg et
ffprobe dans un dossier `ffmpeg`.

La CI ([`ci.yml`](../.github/workflows/ci.yml)) compile MeVCine et lance ses tests sous Fedora 45,
Ubuntu 26.04 et Windows. Il n'y a pas encore de paquets à télécharger.

## Tests

- `tests/tests.cpp` (`mevcine_tests`) reprend les tests unitaires de la version Rust : saisie de
  l'échelle, formats des nombres, pointages, calculs, CSV et texte Python (exécuté par `python3` s'il
  est installé avec numpy), projets et anciens `.mecavideo`, traductions complètes, notice à jour,
  conversion HDR, suivi automatique (1080p et 4K), lecture des vidéos des essais et des formats des
  téléphones fabriqués par ffmpeg (un format est sauté si l'encodeur manque).
- `tests/tests_interface.cpp` (`mevcine_tests_interface`) pilote l'interface, comme les tests
  egui_kittest de la version Rust : accueil, mode simplifié complet, mode expert complet, suivi
  automatique, deux objets, rotation, ralenti, cadence variable, ancien projet, confirmations,
  sauvegarde automatique et récupération, écran de démarrage, boutons de la langue et du thème,
  glisser de l'origine, unités de l'étalon, petite fenêtre, et le mode simplifié dans chaque langue (en anglais, aucun texte affiché ne doit rester en français). Chaque étape est
  capturée dans `build/rendus-tests/`.

Les tests de l'interface tournent sans écran (`QT_QPA_PLATFORM=offscreen`, réglé par `ctest`). Après
un changement de l'interface ou de la notice :

```sh
python3 -I scripts/site-aide.py                 # refait data/help à partir de aide/
(cd build && QT_QPA_PLATFORM=offscreen ./mevcine_tests_interface guide_dans_chaque_langue)
python3 -I scripts/captures-aide.py             # refait les captures de la notice
```

## Organisation du code

Les noms sont ceux de la version Rust, pour comparer les deux versions fichier par fichier.

| Fichier                                   | Rôle                                                        |
|-------------------------------------------|-------------------------------------------------------------|
| `src/main.cpp`                            | ligne de commande, lancement                                |
| `src/erreurs.*`                           | erreurs internes : journal, boîte de message                |
| `src/app/app.*`                           | modes, machine à états, échelle, refaire une mesure, arrêt du pointage, fichiers, menus et leur coin, barre d'état, dialogues |
| `src/app/accueil.*`                       | choix du mode, écran d'ouverture d'une vidéo                |
| `src/app/demarrage.*`                     | l'écran de démarrage                                        |
| `src/app/a_propos.*`                      | la fenêtre « À propos »                                     |
| `src/app/guide.*`                         | le mode simplifié (six étapes)                              |
| `src/app/tableau.*`                       | le tableau des mesures, commun aux deux modes               |
| `src/app/onglet_pointage.*`               | l'image, la loupe et les coordonnées, les gestes ; l'onglet Pointage (expert) |
| `src/app/onglet_trajectoire.*`            | l'onglet Trajectoires, la vidéo recadrée                    |
| `src/app/onglet_coord.*`                  | l'onglet Coordonnées                                        |
| `src/app/plein_ecran.*`                   | le pointage en plein écran, la loupe dans un cadre qui s'écarte de la souris |
| `src/app/onglet_graph.*`                  | l'onglet Grapheur et son graphique                          |
| `src/app/dialogues.*`                     | messages, confirmations, longueur de l'étalon (expert)      |
| `src/app/dessin.*`                        | image ↔ écran, repère, flèches, couleurs, panneau latéral   |
| `src/app/icones.*`                        | soleil, lune, drapeaux, pictogrammes de la barre d'état, petits repères |
| `src/app/theme.*`                         | thème clair ou sombre, suivi du bureau                      |
| `src/pointage.*`, `src/echelle.h`, `src/vecteur.h` | les pointages, en pixels de la vidéo               |
| `src/calculs.*`                           | tableau des mesures, vitesses et accélérations              |
| `src/export.*`                            | CSV et texte Python                                         |
| `src/format.*`                            | nombres affichés, saisie de l'échelle                       |
| `src/video.*`, `src/hdr.*`                | lecture des vidéos par ffmpeg, conversion HDR               |
| `src/detect.*`                            | suivi automatique                                           |
| `src/prefs.*`, `src/chemins.*`            | réglages, projets, dossiers                                 |
| `src/i18n.*`                              | langues, `trad("…")`, fichiers .po                          |
