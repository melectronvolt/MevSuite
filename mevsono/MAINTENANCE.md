# Entretenir MeVSono

Pour prendre le logiciel en main, ouvrir `exemples/sinus_440_Hz.wav`. Dans l’onglet
**Mesure de la période**, deux sommets voisins sont espacés d’environ 2,27 ms.
Dans **Étude des fréquences**, un pic apparaît à 440 Hz. Ces deux observations portent
sur le même son. C’est un bon premier essai après une modification.

## Compiler, lancer et vérifier une modification

Ouvrir un terminal dans le dossier `mevsono`, celui qui contient ce guide et
`CMakeLists.txt`. Les commandes ci-dessous conviennent au terminal Linux et au
terminal **UCRT64** de MSYS2 sous Windows. Les paquets à installer sont indiqués
dans la [rubrique Compilation du README](README.md#compilation).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

La première commande prépare le dossier `build`. La deuxième y construit le programme
et les exécutables de test. La troisième lance les tests de calcul et d’interface.
Le résultat attendu est `100% tests passed`. Les essais qui ont besoin d’un périphérique
audio particulier sont indiqués comme ignorés lorsqu’il manque. Pour ouvrir le son
d’exemple, lancer `./build/mevsono exemples/sinus_440_Hz.wav` sous Linux, ou
`./build/mevsono.exe exemples/sinus_440_Hz.wav` sous Windows.

Après avoir modifié une source, relancer la deuxième commande avant de tester ou d’ouvrir
le logiciel. Les fichiers à modifier se trouvent dans `src`, `tests` et `assets`.
CMake peut recréer tout le contenu de `build`.

## Suivre un son dans le code

1. **Ouvrir le fichier.** Dans `fichiers.cpp`, `MainWindow::charge_chemin` lance la lecture
   en arrière-plan pour laisser la fenêtre utilisable. Dans `audio.cpp`, `audio::charge` confie
   les WAV courants à `lit_wav` dans `audio_wav.cpp`. Pour la plupart des autres formats, il essaie le décodeur de Qt,
   puis le programme ffmpeg si nécessaire. Le résultat contient les échantillons, les voies
   et la fréquence d’échantillonnage. À 48 000 Hz, chaque voie contient 48 000 valeurs par seconde.
   Le lecteur WAV compte les trames reçues. Une trame regroupe les valeurs des différentes voies
   au même instant. Si le support cesse de répondre, les trames complètes restent utilisables
   et la barre d’état prévient l’élève. La limite de 30 minutes est signalée quand elle est atteinte.
   Une vidéo peut contenir plusieurs pistes, par exemple deux langues. Le convertisseur choisit
   la première piste audio marquée par défaut. Si aucune ne porte cette indication, il prend la
   première piste audio. Avec un ancien ffmpeg, son compagnon ffprobe lit cette indication.
   Si cet outil manque aussi, la première piste audio reste lisible.
   L’AAC brut (`.aac` ou `.adts`) peut réunir des prises enregistrées à des cadences différentes.
   MeVSono le confie en priorité à ffmpeg pour obtenir une seule cadence avant toute mesure.
   Si ffmpeg est absent, le décodeur Qt reste disponible. Quand Qt signale un changement
   de cadence ou de nombre de voies en cours de lecture, MeVSono tente de convertir
   le fichier entier. Si cette reprise échoue, il conserve la portion déjà lue et
   signale la lecture partielle.
2. **Choisir ce qu’on étudie.** Dans `session.cpp`, `ApercuSon::prepare` prépare le premier
   tracé et ses curseurs. En mode expert, il cherche la demi-seconde la plus forte sur la voie
   choisie. Pour cela, `fenetre_la_plus_forte` déplace la fenêtre par pas de 10 ms, retire
   l’énergie des échantillons qui sortent et ajoute celle des échantillons qui entrent. Le dernier
   pas atteint la fin du son, même s’il dure moins de 10 ms. En mode simplifié, il garde les 80 % centraux du son. Pour un fichier, ce travail
   se fait pendant l’ouverture. Si la voie ou le mode a changé entre-temps, MeVSono prépare
   un nouvel aperçu à partir des échantillons déjà lus. `Session::installe_son` le place à l’écran.
   Les curseurs **A** et **B** délimitent l’extrait analysé. Les curseurs
   **t₁** et **t₂** servent à mesurer une durée à la main. Le bouton **Analyser cet intervalle**
   recopie leur position dans A et B. Dans le code, les curseurs sont en secondes.
   `intervalle_extrait` les convertit en indices entiers. Par exemple, à 48 000 Hz,
   de 0,10 à 0,12 s, les indices utilisés vont de 4 800 inclus à 5 760 exclu.
   L’extrait contient donc 960 échantillons.
3. **Lancer le calcul.** En mode simplifié, la mesure manuelle de période utilise uniquement
   les deux curseurs. Le premier calcul démarre quand on ouvre **Étude des fréquences**.
   En mode expert, les deux onglets affichent aussi la période automatique.
   `Analyzer`, dans `analyzer.cpp`, reçoit le numéro du son,
   la voie, les deux indices et les réglages. L’ensemble forme la clé `AnalysisKey`.
   Déplacer un curseur pendant le calcul peut changer cette clé. Au retour, l’interface
   accepte un résultat seulement si sa clé correspond toujours à la sélection.
4. **Faire les deux mesures.** Dans `dsp.cpp`, `dsp::analyse` part des échantillons de
   l’extrait. La FFT, ou transformée de Fourier rapide, donne les amplitudes sur une grille
   de fréquences appelée *spectre*. Un *pic* est un sommet de ce spectre. Le code de
   `harmoniques.cpp` cherche une fréquence fondamentale dont les multiples expliquent les
   pics. Le code de `periodicite.cpp` compare le signal à une copie décalée pour trouver
   sa période. Il vérifie aussi que le motif se répète dans l’extrait. Quand la répétition
   est insuffisante, le panneau peut afficher des pics et laisser la fondamentale indéterminée.
   Sur un long enregistrement, `repere_portions` dans `periodicite.cpp` retient, dans
   chacune de 64 zones, le petit bloc où le signal varie le plus. Une note de diapason
   brève peut ainsi servir à vérifier la période même si elle est entourée de silence.
5. **Afficher les valeurs.** À la fin du calcul, `Analyzer` émet le signal Qt
   `mis_a_jour`. Un signal Qt est une notification entre objets. Son raccordement par
   `connect` dans `app.cpp` déclenche `MainWindow::analyse_mise_a_jour`.
   `panneaumesures.cpp` présente les résultats, `spectrumplot.cpp` dessine le spectre.
   Tous deux demandent à `Session::harmoniques` les rangs correspondant à la fondamentale
   active, y compris lorsqu’elle a été choisie à la main.

Deux écritures du C++ reviennent au fil de ce parcours. `std::optional<double>` signifie
qu’une mesure peut être absente. Par exemple, un son silencieux n’a pas de période à afficher.
`std::span<const float>` donne accès en lecture à une portion des échantillons. L’analyse
peut ainsi examiner les 960 valeurs de l’exemple sans recopier tout le son. L’objet
`Samples` garde les données disponibles aussi longtemps que l’analyse ou l’écoute les utilise.

### Relier les grandeurs physiques aux noms du code

| Grandeur | Nom dans le code | Exemple |
| --- | --- | --- |
| Fréquence d’échantillonnage | `audio.rate` ou `Spectrum::rate`, en Hz | À 48 000 Hz, deux échantillons sont espacés d’environ 20,8 µs. |
| Longueur d’un segment de son | `Spectrum::segmentLen`, en échantillons | 4 800 échantillons à 48 000 Hz donnent 0,100 s de signal. |
| Taille du calcul de Fourier | `Spectrum::fftLen`, en points | Avec 8 192 points, deux raies sont espacées de 48 000 / 8 192 ≈ 5,86 Hz. |
| Sommet d’un pic | `Peak::freq` en Hz et `Peak::amplitude` en pleine échelle numérique | Une sinusoïde d’amplitude 0,5 donne un niveau proche de −6,02 dBFS. |

### Lire un tableau d’harmoniques

Un signal composé de 200 Hz et 500 Hz se répète toutes les 10 ms. Sa fondamentale
vaut donc 100 Hz. Le tableau place le pic à 200 Hz au rang 2 et celui à 500 Hz au
rang 5. Le rang 1 porte un tiret. La colonne d’amplitude relative au rang 1 porte
aussi un tiret, car son amplitude n’a pas été mesurée. Chaque pic ne peut remplir
qu’un seul rang. Le test `fondamentale_avec_deux_rangs_non_consecutifs` reprend cet
exemple en faisant varier le pic le plus fort.

Pour vérifier le cas des battements, ouvrir `exemples/battements_440_et_444_Hz.wav`
en mode expert et écarter A et B d’au moins deux secondes. Les pics à 440 et 444 Hz ont
la même amplitude. Leur motif commun dure 250 ms, ce qui donne 4 Hz, sous la limite
de recherche de 20 Hz. La fondamentale et la période automatiques restent indéterminées.
Dans `harmoniques.cpp`, on additionne les carrés des amplitudes des pics attribués à
des rangs distincts. La détection demande au moins deux tiers de la puissance des pics
étudiés. Ici, attribuer un seul des deux pics au premier rang en explique seulement la
moitié. Sur la demi-seconde initiale, les pics peuvent se confondre. `periodicite.cpp`
compare alors de courtes portions de l’onde de part et d’autre des creux. Dans cet
exemple, leur opposition de phase révèle que le motif a changé.

L’écart entre deux raies de la FFT vaut la fréquence d’échantillonnage divisée par la
taille de la FFT. Par exemple, 48 000 / 1 024 donne 46,875 Hz. Ajouter des zéros à la fin
de l’extrait resserre la grille de calcul. Pour distinguer deux notes très proches,
c’est la durée de son effectivement analysée dans chaque segment qui compte aussi.
Les valeurs *écart entre raies* et *résolution* du panneau expert viennent de
`dsp::Spectrum`.

Pour situer le sommet entre deux raies, `affine_pic` dans `dsp.cpp` réduit de moitié l’intervalle
à chaque essai pour les fenêtres usuelles. La fenêtre à sommet plat utilise le barycentre du lobe.
La recherche par moitiés s’arrête quand cet intervalle mesure au plus 0,0001 Hz et
0,0001 fois l’écart entre deux raies. Cette seconde condition préserve la précision
de l’amplitude sur les longs extraits enregistrés à basse cadence. Une fois la fréquence et
l’amplitude du sommet estimées, `pics` applique les limites en hertz et le seuil en décibels.
Avec une amplitude maximale de 0,8 et un seuil de −20 dB, la limite vaut 0,8 × 10^(−20/20),
soit 0,08. Le test `seuil_des_pics_entre_raies` place une sinusoïde entre deux
fréquences de la FFT pour vérifier que le seuil porte bien sur son sommet.

### Comprendre pourquoi l’écran et le casque traitent le son différemment

À l’écran, un son de trente minutes peut tenir sur mille colonnes. `Envelope`
(`wave.cpp`) conserve les minimums et maximums par blocs de 64 échantillons, puis
par blocs de 128, de 256, etc. Pour chaque colonne, le tracé combine des blocs entiers
et lit les quelques échantillons situés aux bords. Un pic bref reste visible au zoom arrière.
Dans `plot.cpp`, `cadre_graphique` mesure la place prise par les graduations. Les deux
graphiques réutilisent ce rectangle pour convertir tous les points d’un dessin en pixels.
Un déplacement du réticule ou de la tête de lecture redessine avec les points déjà calculés.
Un nouveau son, un changement de voie ou d’extrait, un zoom ou un changement d’échelle
peuvent demander de recalculer la courbe correspondante.

À l’écoute, le casque peut demander 16 000 échantillons par seconde pour un fichier
enregistré à 48 000. À 16 000, la fréquence maximale représentable est de 8 000 Hz,
la moitié de la cadence de sortie. Avant de réduire la cadence, `player.cpp` utilise
le filtre de `interpolation.cpp` pour atténuer les fréquences trop hautes.
Sans ce filtrage, une composante à 10 000 Hz s’entendrait comme un faux son à 6 000 Hz.
L’analyse et le CSV de l’extrait utilisent toujours les échantillons d’origine.

À la sauvegarde d’une prise WAV, `ecrit_pcm` dans `audio_wav.cpp` reconvertit les amplitudes en entiers
signés sur 16 bits, de −32 768 à +32 767. Le test `sauvegarde_pcm16_fidele` fait passer
chacune de ces valeurs par la prise, la sauvegarde et la réouverture, puis vérifie l’égalité.

## Trouver le fichier à modifier

Dans `src`, un fichier `.h` décrit ce qu’on peut demander à un composant.
Le fichier `.cpp` contient les opérations qui réalisent la demande. Ainsi,
`dsp.h` donne les unités et le résultat de `periode_autocorrelation`,
et `periodicite.cpp` contient son calcul.

| Ce que l’on veut changer | Où commencer |
| --- | --- |
| Un texte français ou anglais | [i18n.cpp](src/i18n.cpp) contient les deux traductions. [i18n.h](src/i18n.h) donne le nom de chaque texte. |
| L’unité, le nombre de décimales ou la virgule | [format.cpp](src/format.cpp) met les valeurs en forme pour l’écran et les exports. |
| L’accueil, les panneaux ou les boutons d’écoute | [espace_travail.cpp](src/espace_travail.cpp) construit ces widgets et règle leur disposition. [app.cpp](src/app.cpp) coordonne les changements de son, d’onglet et de mode. |
| Un menu, un raccourci ou une action | [menus.cpp](src/menus.cpp) définit les commandes, les touches associées et les fonctions appelées. |
| Une ligne du panneau ou du tableau | [panneaumesures.cpp](src/panneaumesures.cpp). Les méthodes `construit_…` créent les contrôles. Les méthodes `affiche_…` actualisent les valeurs. |
| Les axes, le zoom ou les curseurs | [plot.cpp](src/plot.cpp) pour les gestes communs, [signalplot.cpp](src/signalplot.cpp) pour la courbe temporelle et [spectrumplot.cpp](src/spectrumplot.cpp) pour le spectre. |
| La lecture, le microphone ou les périphériques | [player.cpp](src/player.cpp) pilote l’écoute, [recording.cpp](src/recording.cpp) pilote une prise, [audiodevices.cpp](src/audiodevices.cpp) suit les entrées et les sorties disponibles. |
| Les colonnes d’un CSV ou sa sauvegarde | [session.cpp](src/session.cpp) produit les lignes. [exports.cpp](src/exports.cpp) les écrit par blocs dans un fichier provisoire. Le remplacement du fichier choisi se fait après la dernière ligne. |
| Un réglage qui doit rester d’une séance à l’autre | [settings.h](src/settings.h) donne les valeurs par défaut. [settings.cpp](src/settings.cpp) lit et écrit le fichier INI. [settingsdialog.cpp](src/settingsdialog.cpp) présente le réglage. |
| La notice accessible par F1 | [fr.html](assets/help/fr.html) et [en.html](assets/help/en.html) contiennent les pages embarquées. Les sept WAV proviennent de `exemples/`. [help.cpp](src/help.cpp) adapte leurs liens lors de la copie dans le cache personnel. Le [README](README.md#compilation) donne les commandes pour refaire les captures. |

## Vérifier une correction ciblée

Pour retrouver un calcul sans connaître son fichier, chercher le nom de fonction dans
l’éditeur. Un nom présent dans `tests/tests.cpp` conduit souvent à un essai qui fournit
à la fois les données d’entrée et la valeur attendue. `QCOMPARE` vérifie une égalité.
Pour les fréquences mesurées, `QVERIFY` vérifie généralement que l’écart reste dans
la tolérance choisie. Les sons de test sont fabriqués en mémoire par [signaux.h](tests/signaux.h).

Depuis le dossier `mevsono`, cette commande lance seulement l’essai de la fondamentale
à 100 Hz décrit plus haut. Sur Windows, l’exécutable porte l’extension `.exe`.

```sh
./build/mevsono_tests fondamentale_avec_deux_rangs_non_consecutifs
```

Pour un changement de libellé, chercher le texte français dans `i18n.cpp`, relever son nom
et chercher ce nom dans `src`. Modifier les traductions concernées, reconstruire, puis lancer
`ctest --test-dir build -R uitests --output-on-failure`. Les captures obtenues sont dans
`build/captures`. Pour un changement de calcul, ajouter dans `tests/tests.cpp` un son simple
dont on peut prévoir la réponse. Vérifier que l’essai révèle l’écart avant la correction,
puis que toute la suite passe après celle-ci.

Si un essai échoue, lire la première ligne `FAIL!` et le nom qui la suit.
Par exemple, pour `Tests::periode_des_sons_aigus`, relancer
`./build/mevsono_tests periode_des_sons_aigus`. Un échec concernant le micro ou le casque
se vérifie séparément sous Linux avec `bash tests/audio-virtuel.sh build/mevsono_uitests`.
Ce dernier essai utilise des périphériques virtuels privés et enregistre un son à 440 Hz.

### Mesurer l’ouverture d’un fichier long

Sous Linux, les commandes suivantes créent un WAV stéréo de 30 minutes dans un dossier
temporaire (346 Mo environ), puis l’ouvrent en mode simplifié avec la fenêtre de test.
On peut aussi donner le chemin d’un enregistrement existant.

```sh
essai=$(mktemp -d)
ffmpeg -v error -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=1800' -ac 2 -c:a pcm_s16le "$essai/son.wav"
QT_QPA_PLATFORM=offscreen MEVSONO_MESURE_OUVERTURE="$essai/son.wav" ./build/mevsono_uitests mesure_ouverture_fichier
```

Le premier nombre est le temps écoulé avant que le son soit disponible. Le second est le
plus long intervalle entre deux appels d’une minuterie réglée sur 10 ms, jusqu’à 200 ms
après l’ouverture. Un intervalle de 250 ms signifie que la fenêtre n’a pas traité cette
minuterie pendant un quart de seconde. Pour comparer deux versions, garder le même fichier
et le même poste, alterner les versions et retenir la médiane d’au moins trois passages.
