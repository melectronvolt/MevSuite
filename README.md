# MevSuite

Quatre logiciels libres de Rémi MEVAERE pour les TP de physique-chimie au lycée, disponibles
sous Windows et Linux. Chacun dispose d’une notice intégrée et peut être installé sur un parc de postes.

**Le dépôt principal du code et de la CI/CD est sur GitHub.** [https://github.com/melectronvolt/MevSuite](https://github.com/melectronvolt/MevSuite)

GitHub Actions compile et teste les quatre logiciels sous Linux et Windows. Les installateurs,
les archives portables et leurs sommes de contrôle sont distribués dans les releases GitHub.
[Résultats des tests](https://github.com/melectronvolt/MevSuite/actions/workflows/ci.yml) · [Versions à télécharger](https://github.com/melectronvolt/MevSuite/releases)

| Logiciel | Pour quoi faire | Langues | Licence |
|----------|-----------------|---------|---------|
| **MeVPix** | mesurer des longueurs sur une image, relever un profil d’intensité et enregistrer la séance | français, anglais | MIT |
| **MeVCine** | étudier un mouvement filmé : pointage image par image, repère, échelle, export CSV et Python | français, anglais | GPL-3.0-or-later |
| **MeVReg** | traiter les données d'un TP : tableur de grandeurs, graphes, modélisation, incertitudes, Python intégré | français | MIT |
| **MeVSono** | analyser un son : signal, mesure de la période, spectre, fondamentale et harmoniques | français, anglais | MIT |

Les quatre logiciels partagent des conventions d’interface et portent le numéro de version 0.4.0.
Les nouveautés de cette version figurent dans le
[journal des modifications](CHANGELOG.md).

## Où trouver le code source

| Hébergement | Rôle | Adresse de clonage SSH |
|-------------|------|------------------------|
| [GitHub](https://github.com/melectronvolt/MevSuite) | Dépôt principal, tests et releases | `git@github.com:melectronvolt/MevSuite.git` |
| [git.mevaere.fr](https://git.mevaere.fr/remi/MevSuite) | Copie du code source | `ssh://git@git.mevaere.fr:2222/remi/MevSuite.git` |
| [Forge des communs numériques éducatifs](https://forge.apps.education.fr/rmevaere/mevsuite) | Copie du code source | `git@forge.apps.education.fr:rmevaere/mevsuite.git` |

## Télécharger

Ces adresses deviennent actives dès la première release publique et mènent ensuite à la
**dernière version publiée**. Elles peuvent être copiées telles quelles dans un lien, une page
de l’ENT ou un paquet WAPT.

### MeVPix

MeVPix démarre sans image préchargée. On peut ouvrir une photo ou reprendre une
séance `.mevpix`, copier les mesures, exporter le profil en CSV et enregistrer une image annotée
avec le graphique. Le tableau ci-dessous renvoie à la dernière version publiée.
[Documentation de MeVPix](mevpix/README.md)

| Système | Adresse de la dernière version |
|---------|--------------------------------|
| Windows, installateur | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVPix-windows-x64-setup.exe |
| Windows, portable | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVPix-windows-x64.zip |
| Linux, AppImage | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVPix-x86_64.AppImage |
| Ubuntu 26.04 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevpix_amd64.deb |
| Fedora 45 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevpix.fc45.x86_64.rpm |

### MeVCine

| Système | Adresse de la dernière version |
|---------|--------------------------------|
| Windows, installateur | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVCine-windows-x64-setup.exe |
| Windows, portable | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVCine-windows-x64.zip |
| Linux, AppImage | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVCine-x86_64.AppImage |
| Ubuntu 26.04 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevcine_amd64.deb |
| Fedora 45 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevcine.fc45.x86_64.rpm |

### MeVReg

| Système | Adresse de la dernière version |
|---------|--------------------------------|
| Windows, installateur | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVReg-windows-x64-setup.exe |
| Windows, portable | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVReg-windows-x64.zip |
| Linux, AppImage | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVReg-x86_64.AppImage |
| Ubuntu 26.04 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevreg_amd64.deb |
| Fedora 45 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevreg.fc45.x86_64.rpm |

### MeVSono

| Système | Adresse de la dernière version |
|---------|--------------------------------|
| Windows, installateur | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVSono-windows-x64-setup.exe |
| Windows, portable | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVSono-windows-x64.zip |
| Linux, AppImage | https://github.com/melectronvolt/MevSuite/releases/latest/download/MeVSono-x86_64.AppImage |
| Ubuntu 26.04 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevsono_amd64.deb |
| Fedora 45 | https://github.com/melectronvolt/MevSuite/releases/latest/download/mevsono.fc45.x86_64.rpm |

### Sommes de contrôle et versions précises

Sommes de contrôle SHA-256 de tous les fichiers de la dernière version :
https://github.com/melectronvolt/MevSuite/releases/latest/download/SHA256SUMS.txt

Chaque version et ses notes sont dans les
[Releases](https://github.com/melectronvolt/MevSuite/releases). Chaque fichier y est
publié deux fois : avec son numéro de version (`MeVCine-0.4.0-windows-x64-setup.exe`), pour
garder une version précise, et sans (`MeVCine-windows-x64-setup.exe`), pour les liens ci-dessus.

## Installer

### Windows (x64, Windows 10 et 11)

- **Installateur** (`…-setup.exe`) : installe le logiciel pour tous les utilisateurs dans
  `C:\Program Files\MeVSuite\<Logiciel>`, avec un raccourci dans le menu Démarrer (dossier
  MeVSuite). Une nouvelle version s'installe par-dessus l'ancienne et la remplace.
- **Version portable** (`.zip`) : décompresser, puis lancer `<logiciel>.exe` dans le dossier
  obtenu. Rien n'est installé ; le dossier se copie tel quel sur une clé USB ou un partage réseau.

Les programmes ne sont pas signés : au premier lancement, Windows peut afficher « Windows a protégé
votre ordinateur ». Cliquer sur « Informations complémentaires », puis « Exécuter quand même ».

### Linux (x86_64)

- **AppImage**, sans installation, pour les distributions récentes (glibc 2.39 : Ubuntu 24.04,
  Debian 13, Fedora 40 ou plus récentes) : la rendre exécutable (`chmod +x <fichier>.AppImage`),
  puis la lancer.
- **Ubuntu 26.04** : `sudo apt install ./<fichier>.deb`.
- **Fedora 45** : `sudo dnf install ./<fichier>.rpm`. Pour lire les vidéos des téléphones (H.264,
  HEVC), MeVCine a besoin du ffmpeg de [RPM Fusion](https://rpmfusion.org).

Les versions Windows et les AppImage contiennent tout ce qu'il faut : Qt, Python pour MeVReg, ffmpeg
pour MeVCine (et pour MeVSono sous Windows). Les paquets `.deb` et `.rpm` utilisent le Qt, le Python
et le ffmpeg du système.

### Sur un parc de postes (WAPT, GPO, scripts)

- **Installation silencieuse** :
  `<Logiciel>-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`.
- **Désinstallation silencieuse** :
  `"C:\Program Files\MeVSuite\<Logiciel>\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`.
- **Mise à jour** : l'identifiant de l'installateur (AppId) ne change pas d'une version à l'autre ;
  la nouvelle version remplace l'ancienne, et les réglages des utilisateurs sont gardés.
- **Toujours la dernière version** : télécharger
  `https://github.com/melectronvolt/MevSuite/releases/latest/download/<fichier sans version>`
  (adresses complètes dans « Télécharger »). Pour connaître le numéro de la dernière version :
  `https://api.github.com/repos/melectronvolt/MevSuite/releases/latest` renvoie un JSON dont le
  champ `tag_name` le donne (`v0.4.0`) ; sans l'API, l'adresse
  `https://github.com/melectronvolt/MevSuite/releases/latest` redirige vers `…/releases/tag/v0.4.0`.
- **Réglages imposés (MeVPix, MeVSono).** Un fichier `<logiciel>.ini` à côté du programme sous
  Windows, ou `/etc/MeVSuite/<logiciel>.ini` sous Linux, peut imposer des valeurs au démarrage.
  Pour MeVPix sous Linux, si ce fichier est absent, le programme cherche aussi à côté de l’exécutable.
  [Réglages de MeVPix](mevpix/assets/help/fr.html#reglages)

## Utiliser

- **Notice.** La touche **F1** ou le menu **Aide** ouvre la notice du logiciel dans le navigateur.
  Celle de MeVPix illustre les menus, les outils, les séances et les exports par des captures
  dans les deux langues et décrit l’installation. Sa liste de
  formats d’image correspond aux lecteurs installés sur le poste.
- **Réglages** : un seul dossier pour les quatre logiciels, `~/.config/MeVSuite/` sous Linux,
  `%APPDATA%\MeVSuite\` sous Windows, avec un fichier par logiciel (`mevpix.ini`…).
- **Erreurs** : après une erreur imprévue ou un plantage, le logiciel note la cause dans son journal
  (`mevpix.log`, `mevcine.log`, `mevreg.log`, `mevsono.log`, dans ce même dossier) et dit où le
  trouver. Joindre ce fichier à un signalement : suite@sciences-physiques.net.

## Compiler

Chaque logiciel est un projet CMake indépendant, dans son dossier ([`mevpix/`](mevpix/),
[`mevcine/`](mevcine/), [`mevreg/`](mevreg/), [`mevsono/`](mevsono/)), avec son `README.md`
(fonctions, besoins exacts, tests, organisation du code).

Il faut CMake 3.21 ou plus récent, Ninja, un compilateur C++ 20 (C++ 23 pour MeVCine : GCC 14 ou
plus récent) et Qt 6.8 ou plus récent.

| Système | Paquets pour les quatre logiciels |
|---------|-----------------------------------|
| Fedora 45 | `cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtsvg-devel qt6-qtmultimedia-devel qt6-qtimageformats python3-devel python3-numpy ffmpeg` (ffmpeg de RPM Fusion) |
| Ubuntu 26.04 | `cmake ninja-build g++ qt6-base-dev qt6-svg-dev qt6-multimedia-dev qt6-image-formats-plugins python3-dev python3-numpy ffmpeg` |
| Windows x64 | [MSYS2](https://www.msys2.org), terminal UCRT64 : `pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-svg,qt6-multimedia,qt6-imageformats,python,python-numpy,ffmpeg}` |

Dans le dossier d'un logiciel :

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                         # → build/<logiciel>
ctest --test-dir build --output-on-failure  # tests, sans écran
```

ffmpeg sert à MeVCine (lecture des vidéos) et aux tests de MeVSono ; Python (avec ses fichiers de
développement) est intégré à MeVReg. Les tests de l'interface tournent sans écran (plateforme
`offscreen`) et enregistrent des captures dans `build/captures/`.

## Versions distribuables

Les scripts de [`packaging/`](packaging/) fabriquent les fichiers des Releases, dans `dist/` :

```sh
packaging/paquet-linux.sh <logiciel> deb    # sur Ubuntu 26.04 (dpkg-dev)
packaging/paquet-linux.sh <logiciel> rpm    # sur Fedora 45 (rpm-build)
packaging/appimage.sh <logiciel>            # sur Ubuntu 24.04 (Qt installé par aqtinstall)
packaging/windows.sh <logiciel>             # dans MSYS2 UCRT64, avec Inno Setup 6
packaging/essai-linux.sh <logiciel> <fichier>   # essai d'un .deb, .rpm ou AppImage sur un système neuf
```

Les paquets `.deb` et `.rpm` sont faits par CPack ([`packaging/paquets.cmake`](packaging/paquets.cmake),
inclus par chaque `CMakeLists.txt` : lanceur, icônes, licence, dépendances). L'installateur Windows
est décrit par [`packaging/windows/installateur.iss`](packaging/windows/installateur.iss).

### Intégration continue et publication

- Chaque envoi sur `main` et chaque demande d’intégration déclenchent la compilation et les tests
  des quatre logiciels sur Fedora 45, Ubuntu 26.04 et Windows x64. Les captures des tests de
  l’interface sont téléchargeables dans le compte rendu de l’exécution. [Workflow de tests](.github/workflows/ci.yml)
- Pour préparer une version, l’auteur crée sur GitHub un brouillon de release associé à
  `v<version>`, puis pousse l’étiquette correspondante. Le numéro doit être celui des quatre
  `CMakeLists.txt` et d’une section de `CHANGELOG.md`.
- GitHub Actions teste le code étiqueté, fabrique les paquets et vérifie leur installation et leur
  démarrage. Une fois ces contrôles réussis, il complète le brouillon de l’auteur avec les fichiers,
  leurs noms de téléchargement stables, les sommes SHA-256 et les notes de version. L’auteur
  vérifie le brouillon et le publie depuis son compte. Un lancement manuel du workflow permet
  aussi de tester la fabrication et de télécharger les paquets depuis GitHub Actions. [Workflow de release](.github/workflows/release.yml)

## Licences

- MeVPix, MeVReg et MeVSono : licence MIT ([`LICENSE`](LICENSE)).
- MeVCine : licence GPL version 3 ou ultérieure ([`mevcine/COPYING`](mevcine/COPYING)) ; le
  fichier [`mevcine/NOTICE`](mevcine/NOTICE) décrit les composants libres qu'il intègre.

Chaque logiciel affiche dans **Aide → À propos** les composants libres qu'il utilise, avec leur
licence, et la fenêtre « À propos de Qt ».

## Organisation du dépôt

```
mevpix/      MeVPix
mevcine/     MeVCine
mevreg/      MeVReg
mevsono/     MeVSono
packaging/   fabrication des versions distribuables (.deb, .rpm, AppImage, Windows)
.github/     intégration continue et publication (GitHub Actions, sur GitHub seulement)
```

« MeV » (mégaélectronvolt) est la signature de l'auteur : les noms s'écrivent avec le V majuscule
(MeVPix…) dans les logiciels et leurs notices, et en minuscules pour les dossiers, les programmes
et les fichiers (`mevpix`…).
