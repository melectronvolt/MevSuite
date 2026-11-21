# Prendre en main le code de MeVCine

Pour découvrir le programme, ouvrez `exemples/deux_chariots_uniformes.mp4` en mode expert,
choisissez deux objets et cliquez sur « Démarrer le pointage ». Cliquez sur le premier chariot.
L’image reste affichée en attendant le second. Enregistrez avec **Ctrl+S**, puis rouvrez le projet.
Le prochain clic doit placer le second chariot sur la même image. Cet essai traverse la fenêtre,
le tableau de mesures et la lecture d’un fichier.

## Retrouver le code d’une action

Un fichier `.h` indique ce qu’une partie du programme sait faire. Le `.cpp` du même nom en donne
le détail. Ainsi, `Pointage::memorise_point` désigne la fonction `memorise_point` de la classe
`Pointage`. On peut rechercher ce nom tel quel dans l’éditeur.

| Si le problème concerne… | Commencer par… | Ce qu’on y trouve |
|---|---|---|
| Un clic décalé après un zoom | [src/app/onglet_pointage.cpp](src/app/onglet_pointage.cpp) et [src/app/dessin.cpp](src/app/dessin.cpp) | La conversion d’un emplacement dans la fenêtre en pixels de la vidéo. |
| Le mauvais objet ou la mauvaise image après un clic | [src/app/saisie_pointage.cpp](src/app/saisie_pointage.cpp), fonctions `clic_pointage` et `passe_objet_suivant` | La décision de rester sur l’image ou de passer à la suivante. |
| Une coordonnée, une date ou une vitesse | [src/pointage.cpp](src/pointage.cpp) et [src/calculs.cpp](src/calculs.cpp) | Les mesures conservées et les grandeurs calculées à partir de ces mesures. |
| Un suivi qui change d’objet ou continue sur une image illisible | [src/app/suivi_automatique.cpp](src/app/suivi_automatique.cpp) et [src/detect.cpp](src/detect.cpp) | Le traitement d’une image, la recherche du motif et la provenance de la position. |
| Une donnée qui disparaît à la réouverture | [src/seance.cpp](src/seance.cpp) et [src/app/fichiers.cpp](src/app/fichiers.cpp) | Le contenu d’une séance et l’ordre dans lequel la fenêtre rétablit ses réglages. |
| Une sauvegarde automatique introuvable | [src/secours.cpp](src/secours.cpp) | Le nom de la copie, son emplacement, les fichiers proposés dans le menu de récupération. |
| Une image vidéo ou sa durée | [src/video.cpp](src/video.cpp) | Les informations demandées à FFprobe et les images demandées à FFmpeg. |
| Une colonne du CSV ou du texte Python | [src/export.cpp](src/export.cpp) | La mise en forme des mesures destinées au tableur ou à Python. |

## Suivre un clic jusqu’au tableau

1. Quand la souris est relâchée, `ZoneVideo` convertit sa position avec `dessin::Vue::image`. La bille
   garde les mêmes coordonnées si l’élève agrandit la fenêtre.
2. `App::geste` consulte l’état courant. Pendant l’étalonnage, un clic fixe une extrémité de l’étalon.
   Pendant le pointage, il arrive dans `App::clic_pointage`. Les états et leur sens sont décrits dans
   `src/app/app.h`.
3. `Pointage::memorise_point` conserve la position en pixels de la vidéo. Pour l’objet 2 de l’image 7,
   l’appel utilise le numéro d’objet **2** et l’index d’image **6**. Les images sont numérotées à partir
   de 1 dans la fenêtre et rangées à partir de 0 en mémoire. Une case vide signifie « position inconnue ».
   Une bille placée à l’origine a, elle, deux coordonnées valant zéro.
4. Le tableau et les exports expriment les mesures dans le repère choisi. Avec un étalon de
   100 pixels pour 1 mètre et l’axe vertical dirigé vers le haut, une bille située 20 pixels
   au-dessus de l’origine a pour ordonnée **+0,20 m**. Dans l’image, les pixels sont numérotés
   du haut vers le bas. Cette conversion se trouve dans `Pointage::position_dans_repere`.

## Passer des positions à la vitesse

`Pointage::positions_par_image` ne contient que les images où au moins un objet a une position.
Les absences du suivi sont consignées à part. Les bornes de la série incluent chacun de ces gestes.
Le tableau affiche les images mesurées. Les exports conservent aussi les images intermédiaires
entièrement vides pour garder l’alignement des dates et des colonnes de tous les mobiles.

Pour calculer une vitesse, `Pointage::cinematique_px` utilise trois positions successives du même objet.
Prenons trois abscisses de 0, 10 et 20 pixels, séparées de 0,040 s. La vitesse à l’image centrale
vaut 20 / 0,080 = 250 px/s. Avec un étalon de 100 pixels pour 1 m et l’axe orienté vers la droite,
le grapheur affiche +2,5 m/s. Changer l’origine modifie les abscisses et conserve cette vitesse.

À cadence variable, le calcul tient compte de la durée de chacun des deux intervalles. L’essai
`acceleration_a_cadence_variable` utilise une chute de 9,81 m/s² pour vérifier à la fois vitesse
et accélération. `Grandeur::valeur` applique ensuite le repère et extrait la composante ou la norme
choisie sur chaque axe du grapheur. L’absence d’un des trois points laisse la dérivée indéterminée.

## Lire un projet et son secours

Un projet `.mevcine` se lit dans un éditeur de texte. Les lignes commençant par `#` donnent
les réglages. Le tableau qui suit contient le temps en secondes, puis deux colonnes par objet.
La première ligne de mesures correspond à `index_depart`. Les positions sont en mètres après
étalonnage et en pixels avant. La vidéo reste dans son propre fichier.
Le lecteur contrôle la date et les paires de coordonnées avant de remplacer la séance affichée.
Une paire « nan, nan » ou deux champs vides signalent un objet sans position. Les paires vides
en fin de ligne peuvent être omises dans les projets historiques. Une coordonnée isolée est
signalée avec son numéro de ligne. L’application cherche la vidéo à l’emplacement enregistré
et à côté du projet, puis vérifie l’empreinte quand elle est disponible.

Si l’élève a placé le premier de deux chariots sur l’image 7, la ligne contient déjà sa position.
Les réglages `image_en_cours = 7` et `objet_suivant = 2` indiquent où poursuivre. Une fois
la série terminée, `objet_suivant = 0`. La clé facultative `pointage_arrete` conserve l’arrêt explicite ou la fin du suivi automatique. Ce renseignement permet aussi de conserver une case que
l’élève a volontairement effacée. Pour les anciens projets, qui ne donnaient aucune consigne de
reprise, MeVCine examine les objets présents sur la dernière ligne manuelle.

Les deux chemins d’enregistrement utilisent `seance::texte`. **Ctrl+S** écrit dans le fichier
choisi par l’élève. La copie automatique va dans le dossier des réglages du compte. Son nom
associe la vidéo à un identifiant propre à la séance. Le fichier voisin en `.lock` signale
qu’une fenêtre en garde la main. Si une seconde fenêtre récupère cette séance pendant le travail,
elle poursuit sur sa propre copie.

Le premier changement déclenche un secours. Les clics rapprochés sont regroupés sur deux secondes.
Avant de changer de vidéo ou de fermer, la fenêtre vérifie les derniers changements. Si le projet
enregistré couvre tout le travail, sa copie de secours peut être retirée. La fenêtre vérifie
d’abord le contenu du fichier sur disque. Si un autre élève l’a remplacé, le secours reste
disponible. Si l’écriture échoue, l’élève reçoit un message et le programme réessaie. L’écriture passe par `QSaveFile`, qui prépare
un fichier temporaire et remplace le précédent une fois l’écriture terminée.

## Comprendre la durée d’un film

FFprobe lit les dates auxquelles les images doivent apparaître. FFmpeg décode leurs pixels.
Sur un film de téléphone, le délai entre deux images peut varier. La date du dernier point et
la durée du film sont deux valeurs différentes. Dans la vidéo de test à cadence variable,
l’image 119 commence à **4,30 s** et reste affichée jusqu’à **4,33 s**. Si la prise de vue a
été faite au ralenti, la cadence de capture choisie par l’élève sert à retrouver la durée du mouvement.

À l’ouverture, MeVCine demande une seule image. À partir de la deuxième, il prépare un petit lot
pour les clics suivants. La limite porte sur les pixels que FFmpeg peut envoyer d’avance.
Le cache, dans `LecteurVideo`, garde séparément les images déjà consultées pour qu’un retour en
arrière soit rapide. Dans la vidéo recadrée autour d’un mobile, le délai entre deux positions
suit leurs dates de prise de vue, y compris quand des images intermédiaires manquent.

## Vérifier une modification

Depuis le dossier `mevcine`, ouvrez un terminal et lancez les commandes suivantes. La première
prépare la compilation dans `build`, la deuxième fabrique le programme et la troisième exécute
les vérifications. Les paquets à installer sont indiqués dans le [README](README.md#compilation).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Pour travailler sur une seule anomalie, relancez l’essai qui la concerne. Ces commandes
fonctionnent dans un terminal Linux ou MSYS2/UCRT64 sous Windows.

```sh
./build/mevcine_tests decodage_par_lots video_cadence_variable
QT_QPA_PLATFORM=offscreen ./build/mevcine_tests_interface enregistrer_entre_deux_objets
QT_QPA_PLATFORM=offscreen ./build/mevcine_tests_interface recuperer_deux_seances_du_meme_film
```

`offscreen` permet aux essais de cliquer dans une fenêtre sans l’afficher. Ils utilisent une
configuration temporaire. En cas d’échec, repérez la première ligne `FAIL!` et le nom de la
fonction. Dans `tests/tests_interface.cpp`, cette fonction décrit les gestes effectués et le
résultat attendu. Pour modifier une consigne destinée aux élèves, ses textes français et anglais
se trouvent dans `aide/fr/` et `aide/en/`. La commande `python3 -I scripts/site-aide.py` reconstruit
les pages livrées avec le programme.
