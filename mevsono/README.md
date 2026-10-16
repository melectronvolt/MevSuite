# MeVSono — analyse spectrale des sons

Rémi MEVAERE — logiciel libre sous licence MIT.

MeVSono analyse les sons, en C++ 20 avec Qt 6 (Widgets, Multimedia), en deux onglets : la mesure de
la période sur le signal, puis l'étude des fréquences. Signal et choix de l'extrait (curseurs A et B,
A toujours avant B), spectre d'amplitude (fenêtres de pondération, taille de la FFT, moyenne de Welch),
pics, fondamentale et tableau des harmoniques, mesure d'une période sur le signal (curseurs t₁ et t₂
aimantés aux passages par zéro), période par autocorrélation, écoute du son ou de l'extrait, exports
CSV, modes simplifié et expert, notice HTML intégrée, interface en français et en anglais, réglages
imposés à côté du programme. L'utilisation est décrite dans la notice (`F1`, menu Aide). Pas de sons
d'exemple dans l'interface : chacun ouvre ses enregistrements ; les sons de synthèse (`audio.cpp`)
servent aux essais et aux captures de la notice.

## Différences avec l'ancienne version Rust

MeVSono a d'abord été écrit en Rust avec egui, sous le nom SpectralMev (version retirée du dépôt). Ce
qui change :

- **Fichiers** : `mevsono.ini` et `mevsono.log`, dans le dossier commun à la suite (Linux :
  `~/.config/MeVSuite/`, Windows : `%APPDATA%\MeVSuite\`) ; ceux de l'ancien dossier `mevsono` y sont
  déplacés. Pages de la notice : dossier temporaire du système. `mevsono.ini` a le format de
  `spectralmev.ini`, celui de la version Rust : ce fichier se recopie et se renomme.
- **Lecture des sons** : les fichiers WAV (PCM 8, 16, 24, 32 bits, flottants) sont lus directement ; les
  autres par Qt Multimedia, qui s'appuie sur FFmpeg : MP3, OGG, Opus, FLAC, M4A/AAC, AIFF, CAF et la piste
  son des vidéos MP4, MOV, MKV, WebM, AVI… Un ffmpeg installé sur le poste (ou placé dans un dossier
  `ffmpeg` à côté du programme) reste le dernier recours, comme dans la version Rust.
- **Écoute** : par Qt Multimedia (`QAudioSink`) au lieu de cpal, avec les mêmes fondus et le même
  rééchantillonnage ; la tête de lecture suit le temps déjà transmis à la sortie audio.
- **FFT** : écrite dans le programme (`src/fft.cpp`, base 2 : toutes les tailles utilisées sont des
  puissances de 2), au lieu de rustfft.
- **Graphiques** : dessinés par le programme (`src/plot.cpp`, `signalplot.cpp`, `spectrumplot.cpp`). Qt
  Charts n'est pas utilisé : il est distribué sous GPL, incompatible avec la licence MIT de MeVSono. Les
  gestes sont ceux de la version Rust (molette, `Maj` + molette, clic droit glissé, double-clic).
- **Thème** : clair ou sombre, celui du bureau (lu auprès du portail sous Linux) tant que le bouton
  soleil / lune, à droite de la barre de menus, n'a pas servi ; le choix est gardé (`theme` dans `mevsono.ini`).
  La langue se change avec le bouton drapeau, à côté (plus de menu Langue).
- **Agrandissement de l'interface** (`Ctrl` + / `Ctrl` − / `Ctrl` 0) : il change la taille des polices
  (et des traits et repères des graphiques), de 50 % à 300 %.
- **Boîtes de dialogue** : celles de Qt (portail du bureau ou boîte de Qt) ; plus besoin de zenity.
- **À propos** : composants principaux (GCC, Qt, Qt Multimedia, FFmpeg, MinGW-w64) et bouton « À propos de
  Qt ». Les bibliothèques livrées dépendent de la version : paquets du système pour le `.deb` et
  le `.rpm`, Qt et ses dépendances copiés dans l'AppImage et dans le dossier Windows, avec ffmpeg
  (`../packaging/`).
- **Deux corrections** : un extrait trop court (moins de 8 échantillons) affiche « Extrait trop court » au
  lieu d'un calcul qui ne finit jamais ; le bouton « Valeurs par défaut » des réglages garde le mode expert
  (dans la version Rust, il repassait en mode simplifié et fermait la fenêtre).

## Compilation

Prérequis : CMake ≥ 3.21, un compilateur C++ 20 (GCC ≥ 13 ou Clang ≥ 17) et Qt ≥ 6.8 (modules Core, Gui,
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

Les tests reprennent ceux de la version Rust :

- `tests/tests.cpp` : calculs (fenêtres, spectre, pics, fondamentale, autocorrélation, passages par zéro),
  tracé des longs signaux, nombres, lecture des formats (fabriqués par ffmpeg s'il est installé), sons
  d'exemple, analyse en fond, écoute sur une sortie simulée, réglages, langues, notice, exports CSV.
  `MEVSONO_CARTE_SON=1` joue en plus une seconde de flûte sur la carte son ;
  `MEVSONO_EXEMPLES=dossier` y écrit les sons d'exemple en WAV.
- `tests/uitests.cpp` : essais de l'interface, pilotée sans fenêtre comme par un élève (page d'accueil,
  boutons du thème et de la langue à droite des menus, boutons du panneau, barre d'état, menus, glisser sur le signal, mesure
  d'une période, survol du spectre, écoute simulée, anglais). Chaque étape est rendue
  dans `build/captures/` ; les images de la notice en viennent :
  `python3 -I tools/images_notice.py build`.

Une traduction incomplète est refusée à la compilation, comme en Rust : les tables `Tr` de
`src/i18n.cpp` sont compilées avec `-Werror=missing-field-initializers`.

Sous Windows, les mêmes commandes se lancent dans le terminal UCRT64 de MSYS2 (MinGW-w64) ;
`platform/mevsono.rc.in` ajoute à l'exécutable son icône et ses informations de version. Pour copier
le programme sur un autre poste, il faut mettre à côté les DLL de Qt (`windeployqt`, avec le greffon
FFmpeg de Qt Multimedia et ses DLL).

La CI ([`ci.yml`](../.github/workflows/ci.yml)) compile MeVSono et lance ses tests sous Fedora 45,
Ubuntu 26.04 et Windows. Il n'y a pas encore de paquets à télécharger.

## Organisation du code

- `src/dsp.*`, `src/fft.*` : fenêtres, spectre, pics, fondamentale, harmoniques, autocorrélation, passages
  par zéro ; transformée de Fourier rapide.
- `src/audio.*` : lecture des sons et des vidéos (WAV, Qt Multimedia, ffmpeg en secours), sons d'exemple.
- `src/wave.*` : tracé rapide des longs signaux (minimum et maximum par colonne de pixels).
- `src/analyzer.*` : analyse dans un fil de fond.
- `src/player.*` : écoute du son ou de l'extrait : lecture, pause, arrêt, position de lecture.
- `src/session.*` : état du travail (son, extrait, curseurs, réglages) et calculs de l'interface sans
  widgets (extrait initial, points tracés, pic le plus proche, graduations, exports CSV).
- `src/format.*` : nombres affichés (séparateur décimal de la langue, unités).
- `src/plot.*`, `src/signalplot.*`, `src/spectrumplot.*` : graphiques du signal et du spectre.
- `src/app.*` : fenêtre principale (menus et boutons à leur droite, page d'accueil, panneau des mesures
  et des actions courantes, écoute,
  barre d'état, exports, enregistrement des réglages) ; `src/settingsdialog.*` : fenêtre des réglages ;
  `src/icons.*` : icônes dessinées (soleil, lune, drapeaux, pictogrammes de la barre d'état).
- `src/i18n.*`, `src/settings.*`, `src/help.*`, `src/about.*`, `src/crash.*`, `src/theme.*` : langues,
  réglages, notice, écran d'accueil et « À propos », erreurs fatales, thème clair ou sombre.
- `assets/` : notice (pour les élèves d'abord, puis pour les professeurs ; avec des captures des essais
  de l'interface) et icônes.
