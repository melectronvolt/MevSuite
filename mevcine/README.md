# MeVCine — étude cinématique de vidéos

Rémi MEVAERE — logiciel libre sous licence GPL version 3 ou ultérieure : voir `COPYING`, et `NOTICE`
pour les composants libres intégrés et leurs licences.

MeVCine permet de mesurer le mouvement d’un objet filmé. En mode simplifié, l’élève place un repère,
étalonne l’image et pointe le mobile. En mode expert, il peut suivre plusieurs mobiles, examiner
une trajectoire dans un autre référentiel et tracer les positions, vitesses et accélérations.

Pour préparer une séance, consultez la [notice française](data/help/fr/index.html) et le
[parcours pour les professeurs](data/help/fr/professeurs.html). La touche **F1** ouvre la rubrique
correspondant au travail en cours. La [version anglaise](data/help/en/index.html) présente les mêmes expériences.

Pour modifier le programme, le [guide de maintenance](MAINTENANCE.md) suit un clic sur la bille
jusqu’aux coordonnées enregistrées. Il précise les unités, les conventions de numérotation,
les fichiers responsables de chaque opération et les essais à lancer.

## Choix techniques

- **Fichiers**. Le dossier du compte contient les préférences (`mevcine.ini`), le journal
  (`mevcine.log`) et un secours par séance, même si deux fenêtres utilisent la même vidéo.
  On le trouve dans `~/.config/MeVSuite/` sous Linux et `%APPDATA%\MeVSuite\` sous Windows.
  `MEVCINE_CONF` permet de choisir un autre dossier de réglages et `MEVCINE_DATA` un autre dossier de données.
- **Interface.** À sa première ouverture, MeVCine propose le mode simplifié ou expert. Il retrouve ensuite
  le dernier mode du compte. Les options `--simple` et `--expert` permettent de préparer un raccourci pour la classe.
  Le mode simplifié comporte cinq étapes. Le mode expert comporte les onglets Pointage, Coordonnées,
  Trajectoires et Grapheur. Le plein écran et la loupe restent disponibles pendant le pointage.
- **Mesures.** La séance conserve les positions en pixels de la vidéo. Le tableau et les exports les convertissent
  dans le repère choisi. Avec un étalon, les longueurs sont en mètres. Avant étalonnage, les colonnes sont en pixels.
  Les images sans position utilisent une ligne vide partagée. Un mobile perdu ou une absence confirmée garde
  son numéro d’image, son numéro d’objet et sa date.
- **Thème** : `src/app/theme.cpp` suit le thème du bureau (sous Linux, lu auprès du portail
  `org.freedesktop.appearance` par D-Bus, car Qt ne le voit pas toujours) jusqu'au premier choix,
  retenu ensuite (`theme = light` ou `dark` dans `mevcine.ini`, `auto` pour suivre le bureau) ; le thème sombre a sa propre palette, celle de toute
  la suite, plus sombre que celle de Fusion.
- **Affichage** : Qt Widgets dessine les pictogrammes des boutons en fonction du thème et de leur taille.
- **Dialogues** : ceux de Qt (dialogues de fichiers du bureau, sinon de Qt ; messages et
  confirmations dans des fenêtres modales, toujours un à la fois). La touche Entrée ne confirme
  jamais une action qui ferait perdre des mesures. Les boutons des dialogues de Qt sont traduits avec
  les traductions de Qt (`qtbase_fr.qm`) quand elles sont installées.
- **Lecture des vidéos** : la même méthode (`ffprobe`, puis `ffmpeg` qui envoie des images PPM dans un
  tube, dates lues dans le journal du filtre `showinfo`), avec `QProcess`. L'examen d'une vidéo se
  fait dans un autre fil, avec le curseur d'attente ; au bout de deux minutes, la vidéo est déclarée
  illisible. L'ouverture décode une seule image. La navigation demande ensuite des lots de 32 images au plus,
  dimensionnés pour 32 Mio d'images brutes, avec un minimum d'une image. Les dates réelles des images servent dès que la cadence n'est pas régulière (téléphones). Les images sont des
  `QImage` (RVB 24 bits) ; la conversion HDR et le suivi automatique sont calculés en parallèle avec
  Qt Concurrent.
- **Suivi automatique** : chaque objet conserve son motif initial et ses deux dernières positions fiables.
  La recherche locale suit le mouvement aux dates des images. Pour la recherche étendue, les objets
  partagent les réductions de la trame. Les correspondances ambiguës ou absentes sont signalées dans le tableau.
  À la dernière image, le suivi arrête le pointage et présente les coordonnées. STOP rend la main
  avant la fin. Une image illisible permet de réessayer puis de continuer le pointage manuel.
- **Visée assistée**. Le guide utilise les pointages manuels et les corrections de chaque objet.
  Il compare jusqu’à cinq exemples répartis entre le premier et le plus récent, autour de la dernière position
  mesurée. Il vérifie chaque correspondance en sens inverse et affine le déplacement entre les pixels.
  Si les exemples reconnus désignent des positions distantes de plus de trois pixels, le guide demande un clic
  dans la vidéo. L’élève peut corriger dans la loupe ou avec Maj et les flèches. Valider la cible intacte
  avec Espace ou un clic au centre enregistre uniquement la mesure. Les références sont conservées dans le projet.
- Le **grapheur** relie les mesures consécutives par une courbe lissée. Au survol, le réticule
  indique les coordonnées d’une mesure et son image source. Un clic sur le point ouvre l’image.
  La molette zoome, le glissement déplace la vue et le double-clic rétablit la vue complète.
- **Traductions** : format gettext, `data/lang/en.po`. Les textes sont
  écrits en français dans `trad("…")` (et non `tr`, qui désignerait `QObject::tr` dans une classe Qt).
  Le test `traductions_completes` vérifie que chaque texte est traduit, avec les mêmes arguments ; le
  modèle `data/lang/mevcine.pot` se refait avec `MEVCINE_ECRIT_POT=1 build/mevcine_tests modelePo`.
- **À propos** : composants principaux (Qt, FFmpeg, GCC, mingw-w64) et bouton « À propos de Qt ». Les
  bibliothèques livrées dépendent de la version :
  paquets du système pour le `.deb` et le `.rpm`, Qt et ses dépendances copiés dans l'AppImage et
  dans le dossier Windows, avec ffmpeg et ffprobe (`../packaging/`).
- **Notice** : un site (`aide/` → `data/help/`) en deux parties, « Pour les élèves » puis « Pour les
  professeurs » (lignes `== titre` de `pages.txt`). Sa
  feuille de style est celle de toute la suite (`aide/help.css`, copie exacte de celle de MeVSono,
  vérifiée par le test `aide_dans_chaque_langue`) ; `aide/aide.css` y ajoute ce qui sert à un site de
  plusieurs pages. La notice s'ouvre directement dans le navigateur. Par la touche F1, MeVCine
  ouvre la rubrique active depuis le cache du compte et y inscrit sa version
  (`chemins::copie_aide`).
- **Vidéos** : les élèves ouvrent leurs fichiers. Les tests créent leurs mires vidéo avec ffmpeg.

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
./build/mevcine                        # le mode de la dernière fois
./build/mevcine --simple chute.mp4     # mode simplifié, avec une vidéo
./build/mevcine --expert projet.mevcine
./build/mevcine -d 2 ...               # messages de débogage
ctest --test-dir build --output-on-failure
```

Sous Windows, les mêmes commandes se lancent dans le terminal UCRT64 de MSYS2 (MinGW-w64) ;
`platform/mevcine.rc.in` ajoute à l'exécutable son icône et ses informations de version. Pour copier
le programme sur un autre poste, il faut mettre les DLL de Qt à côté, et ffmpeg et ffprobe dans un
dossier `ffmpeg` (c'est ce que fait `../packaging/windows.sh`).

La CI ([`ci.yml`](../.github/workflows/ci.yml)) compile MeVCine et lance ses tests sous Fedora 45,
Ubuntu 26.04 et Windows. Les versions distribuables (version portable et installateur pour Windows, AppImage, `.deb`,
`.rpm`) sont fabriquées par `../packaging/` et publiées dans les
[Releases](https://github.com/melectronvolt/MevSuite/releases) du dépôt GitHub (voir le `README.md` de la
racine).

## Tests

- `tests/tests.cpp` (`mevcine_tests`) : saisie de
  l'échelle, formats des nombres, pointages, calculs, CSV et texte Python (exécuté par `python3` s'il
  est installé avec numpy), projets, traductions complètes, notice à jour,
  conversion HDR, suivi automatique (1080p et 4K), lecture des vidéos des essais et des formats des
  téléphones fabriqués par ffmpeg (un format est sauté si l'encodeur manque).
- `tests/tests_interface.cpp` (`mevcine_tests_interface`) pilote l'interface : accueil, mode simplifié complet, mode expert complet, suivi
  automatique, deux objets, rotation, ralenti, cadence variable, confirmations,
  sauvegarde automatique et récupération, écran de démarrage, boutons de la langue et du thème,
  glisser de l'origine, unités de l'étalon, petite fenêtre, et le parcours de la notice dans chaque
  langue (`guide_dans_chaque_langue`). Ce parcours génère dans le dossier de tests un film de bille avec un étalon,
  puis capture les étapes guidées, les quatre menus, le suivi automatique et la lecture du grapheur
  dans `build/rendus-tests/`. La notice embarque les captures françaises et anglaises.

Les tests de l'interface tournent sans écran (`QT_QPA_PLATFORM=offscreen`, réglé par `ctest`). Après
un changement de l'interface ou de la notice :

```sh
(cd build && QT_QPA_PLATFORM=offscreen ./mevcine_tests_interface guide_dans_chaque_langue)
python3 -I scripts/captures-aide.py build/rendus-tests
python3 -I scripts/site-aide.py
```

## Organisation du code

| Fichier                                   | Rôle                                                        |
|-------------------------------------------|-------------------------------------------------------------|
| `src/main.cpp`                            | ligne de commande, lancement                                |
| `src/erreurs.*`                           | erreurs internes : journal, boîte de message                |
| `src/app/app.*`                           | modes, état courant du pointage, gestes dans l’image, échelle et commandes de la fenêtre |
| `src/app/saisie_pointage.cpp` | visée, clic sur un objet, correction, annulation et reprise |
| `src/app/suivi_automatique.cpp` | sélection des motifs et traitement successif des images |
| `src/app/fichiers.cpp`                    | commandes du menu Fichier, confirmation de fermeture, ouverture de la vidéo d’un projet |
| `src/app/accueil.*`                       | l'écran sans vidéo, qui propose d'en ouvrir une             |
| `src/app/demarrage.*`                     | l'écran de démarrage                                        |
| `src/app/a_propos.*`                      | la fenêtre « À propos »                                     |
| `src/app/guide.*`                         | le mode simplifié (cinq étapes)                             |
| `src/app/tableau.*`                       | le tableau des mesures, commun aux deux modes               |
| `src/app/onglet_pointage.*`               | l'image, la loupe et les coordonnées, les gestes ; l'onglet Pointage (expert) |
| `src/app/onglet_trajectoire.*`            | l'onglet Trajectoires, la vidéo recadrée                    |
| `src/app/onglet_coord.*`                  | l'onglet Coordonnées                                        |
| `src/app/plein_ecran.*`                   | le pointage en plein écran et le placement de la loupe        |
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
| `src/seance.*`                            | enregistrement et relecture des mesures, de leur provenance et du prochain objet à pointer |
| `src/secours.*`                           | copie automatique de chaque séance et récupération après une interruption |
| `src/prefs.*`, `src/chemins.*`            | format des fichiers texte, écriture sur disque et emplacement des dossiers |
| `src/i18n.*`                              | langues, `trad("…")`, fichiers .po                          |
