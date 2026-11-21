# MeVSono — analyse spectrale des sons

Rémi MEVAERE — logiciel libre sous licence MIT.

MeVSono permet de mesurer une période sur un enregistrement et de retrouver la fréquence
correspondante dans son spectre. Ouvrez un WAV ou enregistrez un son au microphone.
Dans **Mesure de la période**, t₁ et t₂ servent à mesurer une durée. Dans **Étude des
fréquences**, A et B délimitent l’extrait analysé. Le mode expert ajoute le tableau des
harmoniques, la période par autocorrélation, le choix de la voie et les exports CSV.

**Pour préparer un TP**, la [notice en français](assets/help/fr.html) et la [version anglaise](assets/help/en.html)
proposent une première mesure à 440 Hz et sept sons d’exemple. Dans le logiciel,
`F1` ouvre la notice et donne accès aux WAV hors connexion.

**Pour modifier le logiciel**, le [guide de maintenance](MAINTENANCE.md) suit un son
depuis sa lecture jusqu’aux valeurs affichées et explique comment vérifier une modification.

Versions à installer (version portable et installateur pour Windows, AppImage, `.deb`, `.rpm`) :
[Releases](https://github.com/melectronvolt/MevSuite/releases) du dépôt GitHub.

## Choix techniques

- **Fichiers** : `mevsono.ini` et `mevsono.log`, dans le dossier commun à la suite (Linux :
  `~/.config/MeVSuite/`, Windows : `%APPDATA%\MeVSuite\`). La notice est extraite dans le cache de l’utilisateur pour être ouverte dans son navigateur.
- **Lecture des sons.** MeVSono lit directement les WAV (PCM 8, 16, 24, 32 bits et flottants). Qt
  Multimedia décode notamment les MP3, OGG, Opus, FLAC, M4A, AIFF, CAF et la piste son des vidéos
  MP4, MOV, MKV, WebM ou AVI. Le programme `ffmpeg`, installé sur le poste ou fourni dans le dossier
  `ffmpeg` à côté de MeVSono, prend le relais si nécessaire. Pour l’AAC brut, MeVSono l’utilise en
  priorité afin de lire à une cadence uniforme les prises successives d’un même enregistrement.
- **Périphériques** : le choix du micro et de la sortie vaut pour la séance. Sous Linux, si le serveur
  Pulse et `pactl` sont disponibles, MeVSono utilise cette interface pour suivre le retrait des casques USB,
  y compris avec PipeWire. Un choix explicite de `QT_AUDIO_BACKEND` reste prioritaire.
- **Écoute.** Qt Multimedia (`QAudioSink`) lit le son avec un court fondu aux bords. Si la sortie
  demande une cadence inférieure, un filtre atténue les fréquences qui dépasseraient sa bande passante.
  La tête de lecture suit le temps déjà transmis à la sortie audio.
- **Prise de son** : crête du micro en dBFS pendant l’acquisition, puis sauvegarde WAV dans le panneau du son.
- **Exports CSV** : écriture atomique par blocs en arrière-plan avec progression et annulation.
- **FFT** : écrite dans le programme (`src/fft.cpp`, base 2 : toutes les tailles utilisées sont des
  puissances de 2).
- **Graphiques** : dessinés par le programme (`src/plot.cpp`, `signalplot.cpp`, `spectrumplot.cpp`). Gestes :
  molette, `Maj` + molette, clic droit glissé, double-clic. Au clavier, les touches 1 et 2 choisissent
  le curseur et les flèches l’ajustent. Sur le spectre, les flèches parcourent les pics et conservent la lecture.
  En mode expert, « Analyser cet intervalle » transfère les positions t₁ et t₂ vers A et B.
- **Thème** : clair ou sombre, celui du bureau (lu auprès du portail sous Linux) tant que le bouton
  soleil / lune, à droite de la barre de menus, n'a pas servi ; le choix est gardé (`theme` dans `mevsono.ini`).
  La langue se change avec le bouton drapeau, à côté.
- **Agrandissement de l'interface** (`Ctrl` + / `Ctrl` − / `Ctrl` 0) : il change la taille des polices
  (et des traits et repères des graphiques), de 50 % à 300 %.
- **Boîtes de dialogue** : celles de Qt (portail du bureau ou boîte de Qt).
- **À propos** : composants principaux (GCC, Qt, Qt Multimedia, FFmpeg, MinGW-w64) et bouton « À propos de
  Qt ». Les bibliothèques livrées dépendent de la version : paquets du système pour le `.deb` et
  le `.rpm`, Qt et ses dépendances copiés dans l'AppImage et dans le dossier Windows, avec ffmpeg
  (`../packaging/`).
- **Cas limites** : un extrait trop court (moins de 8 échantillons) affiche « Extrait trop court » ; le
  bouton « Valeurs par défaut » des réglages garde le mode expert.

## Compilation

Depuis le dossier `mevsono`, les outils nécessaires sont CMake ≥ 3.21, un compilateur C++ 20 (GCC ≥ 13 ou Clang ≥ 17) et Qt ≥ 6.8 (modules Core, Gui,
Widgets, Multimedia, Concurrent, DBus sous Linux, Test).

| Système          | Paquets                                                                   |
|------------------|---------------------------------------------------------------------------|
| Fedora 45        | `cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel`       |
| Ubuntu 26.04     | `cmake ninja-build g++ qt6-base-dev qt6-multimedia-dev`                   |
| Windows (MSYS2)  | `mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-multimedia}`   |

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/mevsono
ctest --test-dir build --output-on-failure
```

Les tests :

- `tests/tests.cpp` : calculs (fenêtres, spectre, pics, fondamentale, autocorrélation, passages par zéro),
  tracé des longs signaux, nombres, lecture des formats (fabriqués par ffmpeg s'il est installé),
  analyse en fond, écoute sur une sortie simulée, réglages, langues, notice, exports CSV.
  `MEVSONO_CARTE_SON=1` joue en plus une seconde de signal sur la carte son.
- `tests/uitests.cpp` : essais de l'interface, pilotée sans fenêtre comme par un élève (page d'accueil,
  boutons du thème et de la langue à droite des menus, boutons du panneau, barre d'état, menus, glisser sur le signal, mesure
  d'une période, survol du spectre, mode expert et réglages, écoute simulée, anglais, journal des
  plantages). Chaque étape est rendue
  dans `build/captures/`.

Sous Linux, `bash tests/audio-virtuel.sh build/mevsono_uitests` teste aussi une prise à 440 Hz,
la sauvegarde WAV et le retrait du micro ou du casque en cours d’utilisation. Il utilise un serveur
PipeWire privé, WirePlumber, `dbus-run-session`, `pactl` et `paplay`. La CI le lance sous Fedora et Ubuntu.
Pour la validation sur un poste de salle Windows, brancher le casque USB après le lancement, choisir ses deux périphériques,
contrôler la crête pendant une prise, la sauvegarder en WAV et la rouvrir. Le retrait du casque pendant l’écoute
doit arrêter la lecture, et le retrait du micro pendant une prise doit permettre de conserver la partie reçue.

Les deux pages HTML et leurs captures se trouvent dans `assets/help/`. Pour mettre à jour les
captures de la notice depuis le dossier `mevsono/`, utilisez les rendus Qt et le microphone virtuel.
Les fichiers PNG et les WAV de `exemples/` sont intégrés à l’exécutable par CMake. Chaque page utilise ses propres captures et les mêmes sons de TP.

```sh
cmake --build build
ctest --test-dir build -R uitests --output-on-failure
bash tests/audio-virtuel.sh build/mevsono_uitests
python3 tools/images_notice.py build
cmake --build build
ctest --test-dir build -R mevsono_tests --output-on-failure
```

Une traduction incomplète est refusée à la compilation : les tables `Tr` de
`src/i18n.cpp` sont compilées avec `-Werror=missing-field-initializers`.

Sous Windows, les mêmes commandes se lancent dans le terminal UCRT64 de MSYS2 (MinGW-w64) ;
`platform/mevsono.rc.in` ajoute à l'exécutable son icône et ses informations de version. Pour copier
le programme sur un autre poste, il faut mettre à côté les DLL de Qt, et ffmpeg dans un dossier
`ffmpeg` (c'est ce que fait `../packaging/windows.sh`).

La CI ([`ci.yml`](../.github/workflows/ci.yml)) compile MeVSono et lance ses tests sous Fedora 45,
Ubuntu 26.04 et Windows. Les versions distribuables (version portable et installateur pour Windows, AppImage, `.deb`,
`.rpm`) sont fabriquées par `../packaging/` et publiées dans les
[Releases](https://github.com/melectronvolt/MevSuite/releases) du dépôt GitHub (voir le `README.md` de la
racine).

## Organisation du code

Le [guide de maintenance](MAINTENANCE.md) suit un fichier depuis son ouverture jusqu’au panneau
des mesures. Il donne aussi le fichier à ouvrir pour modifier un texte, une mesure ou un export.

- `src/main.cpp` démarre l’application. `src/app.cpp` coordonne les actions de la fenêtre.
  `src/espace_travail.cpp` assemble l’accueil, les panneaux et les graphiques.
  Les menus, les fichiers, les exports et la barre d’état se trouvent respectivement dans
  `menus.cpp`, `fichiers.cpp`, `exports.cpp` et `apparence.cpp`.
- `src/session.cpp` conserve le son, la voie et les curseurs. `src/analyzer.cpp` gère les demandes
  de calcul. `src/dsp.cpp` calcule le spectre et les pics avec `fft.cpp`.
  `harmoniques.cpp` cherche la fondamentale et attribue les rangs. `periodicite.cpp`
  mesure la répétition du motif dans le temps.
- `src/audio.cpp` choisit le décodeur. `audio_wav.cpp` lit et écrit les WAV. `recording.cpp` et `audiodevices.cpp` gèrent la prise
  et les périphériques. `player.cpp` pilote l’écoute et utilise `interpolation.cpp` pour
  les changements de cadence.
- `src/wave.cpp` prépare le tracé des signaux longs. `plot.cpp`, `signalplot.cpp` et
  `spectrumplot.cpp` dessinent les graphiques. `panneaumesures.cpp` présente les résultats
  dans la colonne de gauche. `icons.cpp` dessine les pictogrammes des commandes et de la barre d’état.
- `src/settings.cpp`, `settingsdialog.cpp`, `i18n.cpp`, `format.cpp` et `theme.cpp` gèrent les
  préférences, les langues, les nombres et le thème. `help.cpp` ouvre la notice embarquée,
  `about.cpp` fournit les informations sur MeVSono, et `crash.cpp` écrit le journal
  `mevsono.log`. La présentation de démarrage est partagée avec la suite dans `../commun/`.
- `assets/` contient les pages de la notice, leurs captures et les icônes. `exemples/`
  contient sept fichiers WAV à ouvrir en classe ou pour une vérification.
