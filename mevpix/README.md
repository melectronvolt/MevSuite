# MeVPix

MeVPix mesure des longueurs et relève un profil d’intensité sur une image. L’étalonnage permet
de travailler en µm, mm, cm ou m. Sans étalonnage, les positions et les longueurs sont en pixels.
Le logiciel fonctionne sous Windows et Linux, en français et en anglais, sous licence MIT.

La touche **F1** ouvre la notice illustrée et les images de TP hors connexion, dans la langue de
l’interface. Elle indique aussi les formats que le poste sait lire.
[Notice française](assets/help/fr.html) · [English user guide](assets/help/en.html)

## Deux TP pour prendre en main le logiciel

| Manipulation | Matériel et résultat de contrôle |
|---|---|
| Étalonner puis mesurer deux diamètres | Ouvrir [la rondelle](exemples/rondelle_diametres.png). Étalonner sur les 20 mm entre les traits de la barre. En passant par le centre, on trouve environ 30 mm pour le diamètre extérieur et 12 mm pour le trou. |
| Retrouver un interfrange à partir d’un profil | Ouvrir [les fentes de Young](exemples/franges_interference.png). Étalonner sur 10 mm, puis couper horizontalement la pose de gauche. Le deuxième maximum de chaque côté du centre donne un écart d’environ 6,1 mm, soit quatre interfranges d’environ 1,5 mm. |

Les élèves peuvent déposer le fichier de séance pour conserver leurs pointages, le tableau ou le CSV
pour exploiter les valeurs, et l’image annotée pour leur compte rendu.
[Déroulé des deux TP dans la notice](assets/help/fr.html#tp)

## Ouvrir, mesurer et enregistrer

Au lancement sans fichier, MeVPix affiche son écran d’accueil. Cliquez sur **Ouvrir une image…**,
utilisez **Ctrl+O** ou déposez une image ou une séance `.mevpix` sur la fenêtre. Un chemin peut
également être passé au lancement, par exemple `mevpix photo.png` ou `mevpix mesure.mevpix`.

| Outil | Utilisation |
|-------|-------------|
| **Étalonner** | Tracer le segment jaune sur une longueur connue, saisir la valeur et l’unité, puis valider avec le bouton ou **Entrée**. Les mesures existantes passent à la nouvelle échelle. |
| **Mesurer** | Tracer un segment par longueur. Les mesures M1, M2… apparaissent sur l’image et dans le tableau. Les poignées corrigent les extrémités, la corbeille supprime une mesure et **Tout effacer** vide le tableau. |
| **Profil d’intensité** | Tracer la coupe puis choisir le canal et la largeur de bande (1 à 50 px). Déplacer A et B sur la coupe ou le graphique pour lire leurs positions, leurs intensités et leur écart Δx. |

Chaque outil affiche ses propres tracés et les conserve quand on en choisit un autre. **Maj**
contraint un segment à l’horizontale ou à la verticale. **Échap** annule le geste en cours.
**Ctrl+Z** revient sur les modifications terminées, y compris une suppression ou un effacement du
tableau. L’historique conserve jusqu’à 100 modifications pour le travail ouvert.

Pour une photo prise en classe, placer la référence et l’objet dans le même plan et photographier
de face. Une seule échelle s’applique à toute l’image. La précision du pointage dépend de la
longueur de l’étalon et de la netteté des bords.

**Ctrl+S** enregistre l’image de travail et les tracés dans une séance autonome `.mevpix`.
Le premier enregistrement demande un nom. Les suivants utilisent le même fichier.
Vous pouvez continuer à mesurer pendant l’écriture. Si vous ajoutez M2 après avoir lancé la
sauvegarde de M1, l’astérisque reste visible. Un nouveau **Ctrl+S** enregistre aussi M2.
**Fichier › Enregistrer la séance sous…** crée une copie qui devient la séance courante.
Un astérisque dans le titre signale les modifications à enregistrer. À la fermeture ou au
remplacement du travail, MeVPix propose de les enregistrer, de les abandonner ou d’annuler.
La réouverture retrouve l’étalonnage, les mesures, l’outil, la coupe, le canal, l’épaisseur et A/B.
Si le fichier a changé sur disque, **Enregistrer une copie…** conserve le travail de cette fenêtre
dans un autre fichier. **Annuler** permet de poursuivre le travail en cours.

## Zoom et lecture du profil

- **Photo.** En bas à droite de l’image, les loupes **+** et **−** changent le zoom. Les quatre flèches
  déplacent la photo dans le sens indiqué. Maintenez une flèche pour poursuivre le déplacement.
  **Ajuster à la fenêtre** recentre la photo entière. La molette et le pincement zooment autour du pointeur.
  Le glissement avec le bouton droit ou central déplace la vue. À fort zoom, MeVPix affiche les pixels
  à leur pleine résolution.
- **Graphique.** La molette zoome horizontalement, **Maj+molette** déplace la vue et le double-clic
  rétablit la coupe entière en conservant A, B et Δx. Un clic ou un glissement place le curseur le
  plus proche. Les boutons **A** et **B**, ou les touches **A** et **B** dans le graphique,
  sélectionnent le curseur que les flèches gauche et droite déplacent d’un échantillon. Au survol,
  le point suit la courbe à l’abscisse du pointeur.
- **Intensité.** L’échantillonnage interpole les pixels et moyenne la bande dans le canal choisi
  (gris pondéré, rouge, vert ou bleu). Au bord, la moyenne utilise la partie de la bande située dans
  l’image. Le panneau signale les bandes tronquées et la proportion d’échantillons saturés.
  Le niveau de gris combine les composantes codées sur 8 bits selon 0,299 R + 0,587 V + 0,114 B.
  Une exploitation photométrique demande de connaître la réponse de la caméra. Le tracé conserve
  les minima et maxima lorsque plusieurs échantillons occupent une colonne à l’écran.
- **Interface.** **Ctrl++** et **Ctrl+−** changent la taille des textes et des commandes.
  **Ctrl+0** rétablit la taille normale. Le soleil ou la lune et le drapeau, à droite des menus,
  changent le thème et la langue.

## Copier et exporter

| Résultat | Commande et contenu |
|----------|---------------------|
| Tableau des longueurs | **Mesurer › Copier le tableau** copie deux colonnes séparées par une tabulation. La seconde indique l’unité validée ou px. |
| Profil numérique | **Profil d’intensité › Exporter le profil (CSV)…** enregistre tous les couples position–intensité de la coupe, y compris les points hors de la vue et de l’intervalle A–B. En français, les colonnes sont séparées par un point-virgule et la virgule est décimale. En anglais, le séparateur est une virgule et le point est décimal. |
| Image annotée | **Fichier › Image annotée…** permet de sélectionner les mesures (toutes, une seule ou aucune) et d’ajouter l’étalon, la coupe avec A/B et le graphique complet avec Δx. La coupe et le graphique se choisissent indépendamment. Le PNG et le presse-papiers conservent la résolution de la photo et son profil de couleur, s’il existe. |

Si une échelle a déjà été validée, modifier la référence, la longueur ou l’unité déclenche un
choix avant chacune de ces sorties. On peut valider la nouvelle échelle, garder l’échelle validée
ou annuler. Une nouvelle échelle doit être valide pour pouvoir l’appliquer.

## Formats, réglages et diagnostic

MeVPix applique l’orientation de la photo et reconnaît les formats selon les lecteurs d’images Qt
installés. La liste de **F1** et le filtre de la boîte d’ouverture utilisent ces mêmes lecteurs.
Si un transfert JPEG s’arrête avant la fin de la photo, MeVPix garde la séance en cours.
Recopiez la photo depuis l’original, puis ouvrez cette nouvelle copie.
La prise en charge de HEIC et HEIF dépend du poste. Quand le décodeur fournit plus de 8 bits par
composante, la barre d’état signale la conversion. Le profil et l’image enregistrée dans la séance
utilisent les valeurs converties sur 8 bits (0 à 255).

Les préférences personnelles se trouvent dans `%APPDATA%\MeVSuite\mevpix.ini` sous Windows et
dans `$XDG_CONFIG_HOME/MeVSuite/mevpix.ini` ou, par défaut, `~/.config/MeVSuite/mevpix.ini`
sous Linux. Au démarrage, des réglages imposés peuvent prendre la priorité. MeVPix cherche
`mevpix.ini` à côté de l’exécutable sous Windows. Sous Linux, il cherche d’abord
`/etc/MeVSuite/mevpix.ini`, puis à côté de l’exécutable si le premier fichier est absent.
Les clés et leurs valeurs sont détaillées dans la [notice](assets/help/fr.html#reglages).

**Aide › À propos de MeVPix…** indique les chemins des réglages et les licences des composants.
La notice est écrite dans le cache personnel, ou dans un dossier temporaire privé si le cache est
inaccessible. Après une erreur fatale, MeVPix indique le chemin du journal `mevpix.log` lorsqu’il a
pu l’écrire à côté des réglages personnels. Pour un signalement, joindre ce journal et préciser la
version, le système et l’action effectuée. [Contacter Rémi MEVAERE](mailto:suite@sciences-physiques.net)

## Compiler et tester

Depuis ce dossier, utiliser CMake 3.21 ou ultérieur, Ninja et un compilateur C++20. GCC 13 et
Clang 17 ou ultérieurs conviennent. MeVPix utilise Qt 6.8 ou ultérieur avec Core, Gui, Widgets,
Concurrent et, sous Linux, DBus. Les tests activés par défaut utilisent aussi Qt Test.
Conserver le dossier `commun/` à côté de `mevpix/` dans le dépôt. L’écran de démarrage et
les liens de contact de la fenêtre « À propos » y sont partagés par les logiciels de la suite.

| Système | Paquets |
|---------|---------|
| Fedora 45 | `cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtimageformats` |
| Ubuntu 26.04 | `cmake ninja-build g++ qt6-base-dev qt6-image-formats-plugins` |
| Windows (MSYS2, terminal UCRT64) | `mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-imageformats}` |

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

L’exécutable se trouve dans `build/mevpix` (`build/mevpix.exe` sous Windows). Pour compiler
uniquement le logiciel, ajouter `-DBUILD_TESTING=OFF` à la commande de configuration.

| Suite CTest | Vérifications |
|-------------|---------------|
| `mevpix_formats` | Le programme décode une image JPEG avec orientation EXIF, reconnaît un PNG malgré son extension et relit un HEIC/HEIF si un encodeur est disponible. |
| `mevpix_tests` | Géométrie et étalonnage, interpolation, saturation, formats numériques et CSV, intégrité des séances, conversions de profondeur, zoom sur grande image, réglages, langues et notice hors connexion. |
| `mevpix_uitests` | Essais de l’interface hors écran, dont les gestes d’édition et leur annulation, A/B, zoom, sauvegardes, protection du travail et comparaison du PNG au presse-papiers. Les captures sont écrites dans `build/captures/` avec les commandes ci-dessus. |

Les tests de calcul et d’interface créent des mires contrôlées. Deux autres essais ouvrent les
images de TP embarquées et vérifient les diamètres de la rondelle et l’interfrange annoncés dans
la notice. Les originaux figurent dans [exemples/](exemples/). Le test d’échange de la suite vérifie que MeVReg
relit les CSV produits par MeVPix en français et en anglais. [Script d’échange](../packaging/test-echanges.sh)

Pour actualiser les illustrations après une modification de l’interface, lancer les tests, puis
copier les captures de chaque langue et recompiler les ressources de la notice.

```sh
python3 tools/images_notice.py build
cmake --build build
```

L’intégration continue compile le logiciel et lance ses tests sous Fedora, Ubuntu et Windows.
Les scripts de distribution produisent le ZIP portable et l’installateur Windows, l’AppImage et
les paquets Linux. [Configuration de la CI](../.github/workflows/ci.yml) · [Installation et téléchargements](../README.md#télécharger)

## Organisation du code

Le [guide de maintenance](MAINTENANCE.md) suit une mesure de 25 pixels depuis la souris
jusqu’au tableau. Il explique aussi le format des séances et indique quel test relancer pour
vérifier un geste, une sauvegarde ou un export.

| Fichiers dans `src/` | Responsabilité |
|----------------------|----------------|
| `model.*`, `profile.*` | Segments, unités, étalonnage, formatage du tableau, calcul du profil et export CSV. |
| `session.*`, `projet.*` | État éditable, historique, profil mis en cache et description des tracés dans le fichier de séance. |
| `fichier_seance.*`, `savefile.h` | Vérification du fichier sur disque, écriture temporaire et remplacement après réussite. |
| `jpeg.*`, `view.*`, `canvas.*` | Vérification de la fin d’un JPEG, décodage des images, niveaux de résolution, zoom et édition des tracés à la souris. |
| `plot.*`, `annotation.*` | Graphique du profil, curseurs et composition commune à l’aperçu, au presse-papiers et au PNG. |
| `main.cpp`, `app.h`, `app.cpp`, `app_affichage.cpp` | Démarrage, fenêtre, menus et liaison entre les outils. `app_affichage.cpp` actualise les textes, la barre d’état et les commandes visibles. |
| `app_outils.cpp`, `tableau_mesures.*` | Panneaux des trois outils, validation des gestes, lecture des curseurs et tableau des longueurs. |
| `app_fichiers.cpp` | Ouverture en arrière-plan, commandes de sauvegarde, traitement des conflits et exports. |
| `i18n.*`, `settings.*`, `theme.*`, `icons.*`, `style.h` | Langues, préférences, thème, pictogrammes et dessin. Une traduction manquante dans une table est signalée à la compilation avec GCC ou Clang. |
| `about.*`, `crash.*`, `help.*` | Accueil et licences, journal des erreurs fatales, ouverture des ressources HTML et CSS de `assets/help/`. |

Sous Windows, [mevpix.rc.in](platform/mevpix.rc.in) ajoute l’icône et la version à l’exécutable.
Le [script de distribution Windows](../packaging/windows.sh) rassemble les DLL nécessaires.
