# Prendre en main le code de MeVPix

Pour une première intervention, partez d’un geste que vous connaissez dans le logiciel.
Si M1 affiche une longueur surprenante, vous pouvez suivre cette mesure depuis la photo jusqu’au
tableau, puis vérifier sa valeur avec un exemple simple.

## Retrouver une mesure dans le code

Prenons une photo sur laquelle **100 pixels représentent 1 mm**. Un segment horizontal de
25 pixels doit mesurer **0,250 mm**. Agrandir la photo à l’écran doit faciliter le placement des
poignées tout en conservant cette valeur.

1. **La souris indique une position dans la fenêtre.** Dans `CanevasImage::mousePressEvent`,
   `vers_image` convertit cette position en pixels de la photo en tenant compte du zoom et du
   déplacement de la vue. L’origine est le coin supérieur gauche. Un nouveau tracé commence dans
   la photo. Si la souris en sort pendant le geste, l’extrémité s’arrête au bord.
   [Gestion de la souris](src/canvas.cpp) · [Conversion des coordonnées](src/view.h)
2. **Le relâchement valide le segment.** Un `Segment` contient deux points, `a` et `b`.
   `longueur_px()` calcule leur distance, y compris pour une ligne oblique. Le canevas émet le
   signal `segment_trace` quand la nouvelle ligne mesure au moins un pixel de la photo et que le
   geste est assez grand à l’écran pour se distinguer d’un clic. Un *signal* est un événement
   qu’un autre objet peut recevoir. La ligne `connect` dans le constructeur de `FenetrePrincipale`
   relie ce signal à la fonction `valide`. [Segment](src/model.h) · [Branchement](src/app.cpp)
3. **La séance conserve les deux points.** Avec l’outil Mesurer, `FenetrePrincipale::valide` ajoute le
   segment à `m_seance.mesures`. M1 est l’élément d’indice 0, M2 celui d’indice 1.
   L’étalonnage validé donne ici `1 / 100 = 0,01` mm par pixel. On conserve les points en pixels
   pour pouvoir changer d’étalonnage et recalculer toutes les longueurs.
   [Outils de mesure](src/app_outils.cpp) · [Calcul de l’échelle](src/model.cpp)
4. **Le tableau affiche le résultat.** Quand Qt dessine une cellule, le modèle lit directement le
   segment de la séance et applique l’échelle. Ici, 25 pixels donnent `0,250 mm`. Si on déplace
   une poignée de M1, `TableauMesures::actualise_longueur` demande de redessiner sa cellule de
   longueur. Un nouvel étalonnage actualise toutes les longueurs. Le panneau affiche jusqu’à douze
   lignes à la fois, et le défilement donne accès aux suivantes. [Tableau des mesures](src/tableau_mesures.cpp)

Un fichier `.h` décrit les données et les opérations disponibles. Le fichier `.cpp` contient
leur réalisation. Par exemple, chercher `Etalonnage::fabrique` conduit à la fonction qui valide
l’échelle. Les noms commençant par `m_` désignent des données conservées par un objet entre deux
appels. `std::optional<Etalonnage>` indique que l’échelle peut être absente, comme à l’ouverture
d’une photo.

### Retrouver le texte d’un bouton

Pour retrouver **Enregistrer une copie…**, cherchez ces mots dans [i18n.cpp](src/i18n.cpp).
Le texte français et le texte anglais portent le même nom, `saveCopy`. Cherchez ensuite
`t.saveCopy` dans [app_fichiers.cpp](src/app_fichiers.cpp). La fonction `propose_copie`
crée le bouton et indique à la fenêtre si la personne a choisi de garder une copie.
Un changement de libellé se fait dans les deux traductions. Un changement du comportement
du bouton se fait dans cette fonction ou dans la commande qui l’appelle.

## Comprendre un profil ou une image annotée

Une photo JPEG arrivée en partie peut présenter une zone grise à la place des lignes manquantes.
Avant de confier la photo à Qt, `jpeg_complet` cherche la marque de fin de l’image principale.
Une miniature peut déjà être complète dans l’en-tête alors que le reste de la photo est encore en
cours de copie. Un JPEG progressif, lui, contient plusieurs passages de dessin avant sa marque de fin.
Si la vérification échoue, la fenêtre conserve les mesures affichées. Si l’utilisateur annule
l’ouverture d’une photo valide, elle libère la nouvelle photo et ses réductions d’affichage.
[Vérification du JPEG](src/jpeg.cpp) · [Ouverture](src/app_fichiers.cpp)

`Seance::profil` prélève les intensités sur la photo en pleine résolution. Le résultat est gardé en
mémoire et recalculé si la coupe, le canal ou l’épaisseur changent. La coupe va de son premier
point vers son second. A et B sont enregistrés comme fractions de sa longueur. Sur une coupe de
1 mm, une fraction de 0,25 place le curseur à 0,250 mm de l’origine. Quand les deux fractions
coïncident, Δx vaut zéro et chacun garde son abscisse. [Échantillonnage](src/profile.cpp) ·
[Lecture de A et B dans le panneau](src/app_outils.cpp)

Pour vérifier une valeur de gris à la main, partez d’un pixel rouge pur dont les composantes
valent (255, 0, 0). Le canal rouge donne 255. En gris, la somme pondérée donne
`0,299 × 255 ≈ 76,2`. Les deux canaux signalent un risque de saturation car le rouge atteint
255. La moyenne sur la bande intervient après la lecture des pixels. Même si cette moyenne
baisse, l’avertissement reste associé à l’échantillon qui contient le pixel saturé.
Dans le CSV, l’abscisse utilise l’échelle validée lors de l’export et l’ordonnée utilise ce
niveau numérique. [Calcul et export du profil](src/profile.cpp)

Pour un décalage visible seulement sur le PNG ou dans le presse-papiers, ouvrez `image_annotee`.
Le dessin de la photo et celui des segments doivent utiliser les mêmes pixels. Qt peut associer
un facteur d’affichage de 2 à un nom comme `mire@2x.png`. Une photo de 400 × 100 pixels doit
toujours occuper 400 × 100 pixels dans l’export en pleine résolution. L’aperçu réduit ensuite
l’ensemble, photo et tracés compris. Pour une couleur différente après un copier-coller, vérifiez
aussi le profil colorimétrique. Il indique au logiciel destinataire comment interpréter les trois
valeurs rouge, vert et bleu. La composition reprend celui de la photo. [Composition de l’image](src/annotation.cpp)

## Distinguer l’annulation, la séance et les préférences

| Si vous cherchez… | Où regarder |
|---|---|
| Ce que Ctrl+Z peut annuler | `Travail` rassemble les données modifiables. `Seance::memorise` garde l’état précédent après une édition terminée. Plusieurs mouvements de souris pendant un seul glissement donnent une seule annulation. [Historique](src/session.cpp) |
| Ce qui doit rouvrir avec la photo | `donnees_seance` écrit les champs. `restaure_seance` les vérifie dans une séance provisoire avant de remplacer celle qui est affichée. [Données de séance](src/projet.cpp) |
| Un réglage qui doit revenir au prochain lancement | `Reglages` lit et écrit `mevpix.ini`. La fenêtre attend une seconde après un changement. Si l’écriture échoue, les valeurs restent à enregistrer. Le prochain changement ou la fermeture déclenche un nouvel essai. [Réglages](src/settings.cpp) · [Déclenchement de l’écriture](src/app.cpp) |

Si deux fenêtres sont ouvertes, chacune compare ses choix à ceux qu’elle a enregistrés en dernier.
Sous un bref verrou, elle relit le fichier personnel et y reporte ses propres changements. Ainsi,
redimensionner une fenêtre conserve la langue choisie dans l’autre. Pour ajouter une préférence,
prévoir sa lecture, son écriture et sa comparaison dans `Reglages::enregistre`. [Réglages](src/settings.cpp)

Le fichier `.mevpix` commence par une ligne `MeVPix/1`. La deuxième ligne est du JSON, du texte
structuré qui contient notamment les coordonnées des segments, l’unité, l’outil et les curseurs.
Le reste du fichier est la photo de travail au format PNG. Une ancienne séance peut contenir
une coupe de moins d’un pixel. Son emplacement est conservé pour qu’on puisse écarter les poignées
et retrouver un profil exploitable. Pour ajouter un champ facultatif au format, prévoir une valeur
à la lecture des séances antérieures. Les numéros déjà attribués aux outils, unités et canaux
sont utilisés dans les fichiers existants. [Lecture de la photo et de la séance](src/view.cpp)

### Que se passe-t-il si deux personnes enregistrent la même séance ?

À l’ouverture, MeVPix retient une *empreinte*, une courte suite d’octets calculée à partir de tout le
contenu du fichier, photo comprise. À l’enregistrement, un petit fichier `.lock` réserve l’écriture à une
fenêtre. La nouvelle séance est écrite dans un fichier temporaire. Juste avant de remplacer
l’original, MeVPix compare son empreinte à celle qu’il avait retenue. Si l’autre personne a
enregistré entre-temps, chacun peut conserver sa version grâce à **Enregistrer une copie…**.
Quand on appuie sur **Ctrl+S**, `enregistre_projet` copie les tracés dans un `ContenuSeance`.
Le travail d’écriture utilise cette copie et partage les pixels de la photo. On peut donc déplacer
M1 pendant qu’il écrit. À son retour, `apres_sauvegarde` compare l’état enregistré aux tracés
affichés. Si M1 a bougé entre-temps, l’astérisque reste visible. Un deuxième **Ctrl+S** mémorise
la nouvelle position et son écriture attend la première. Une demande de fermeture attend le résultat
et propose d’enregistrer les modifications encore présentes.
[Commandes et dialogues](src/app_fichiers.cpp) · [Écriture sur disque](src/fichier_seance.cpp)

## Vérifier une modification

Depuis le dossier `mevpix` du dépôt, ouvrez un terminal. La première commande prépare la
compilation, la deuxième construit le logiciel et la troisième exécute les tests. Les paquets
à installer selon le système figurent dans le [README](README.md#compiler-et-tester).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Après avoir modifié un fichier `.cpp`, relancez les deux dernières commandes. CTest indique
quelle suite a échoué. Qt Test, à l’intérieur de cette suite, donne le nom de l’essai, la valeur
obtenue, la valeur attendue et la ligne à ouvrir.

Pour essayer la modification à la souris, lancez la version que vous venez de compiler sur une
image du dépôt. Depuis le même terminal, tapez `./build/mevpix exemples/rondelle_diametres.png`.

Pour travailler sur un seul cas, ces deux commandes se lancent sous Linux ou dans le terminal
MSYS2 sous Windows. L’option `offscreen` permet de manipuler les fenêtres de test sans les afficher.

```sh
QT_QPA_PLATFORM=offscreen ./build/mevpix_uitests deux_fenetres_conservent_leurs_mesures
QT_QPA_PLATFORM=offscreen ./build/mevpix_uitests curseurs_superposes_en_unite_etalonnee
```

Le dernier mot de la commande désigne le cas à vérifier. Vous pouvez le remplacer par l’un de ceux-ci.

| Comportement à vérifier | Nom du test d’interface |
|---|---|
| Déplacer une mesure sans redimensionner les autres lignes | `deplacement_actualise_une_seule_ligne` |
| Déplacer l’image avec chacune des quatre flèches et la recentrer | `zoom_image_par_boutons` |
| Commencer un tracé dans la marge ou rapprocher les extrémités d’une coupe | `trace_dans_la_marge_et_coupe_trop_courte` |
| Rouvrir une ancienne coupe dont les poignées se confondent | `ancienne_coupe_reparable` |
| Comparer le PNG et le presse-papiers pour une photo ordinaire et une photo nommée `mire@2x.png` | `annotation_coupe_graphique_png_et_copie` |
| Remplacer un fichier qui change pendant la confirmation | `remplacement_modifie_pendant_la_confirmation` |
| Ajouter ou supprimer une mesure pendant l’écriture, puis fermer | `edition_et_fermeture_pendant_sauvegarde` |
| Garder M1 quand une photo est refusée ou qu’un JPEG est incomplet | `ouverture_refusee_garde_la_mesure` |
| Retrouver la langue et la taille modifiées dans deux fenêtres différentes | `deux_fenetres_partagent_les_reglages` |
| Retrouver ses préférences après une erreur d’écriture temporaire | `reglages_reessayes_a_la_fermeture` |

Pour une formule, cherchez un exemple chiffré dans [les tests de calcul](tests/tests.cpp).
Pour un geste, un dialogue ou le presse-papiers, [les tests d’interface](tests/uitests.cpp)
montrent comment ouvrir une mire, déplacer une poignée et relire le résultat.

## Maintenir la notice de TP

Les deux pages de [la notice](assets/help/fr.html) partagent leur feuille de style et le script
qui repère la rubrique courante dans le sommaire. Les noms d’ancre sont les mêmes en français et
en anglais. Lorsqu’une commande change, vérifiez ses libellés dans les deux langues, son geste à
la souris ou au clavier et les captures qui la montrent. La procédure pour les régénérer se
trouve dans le [README](README.md#compiler-et-tester).

Les fichiers originaux des deux TP se trouvent dans [exemples/](exemples/). CMake les incorpore
au programme. Les liens des pages sources remontent à ce dossier pour fonctionner lorsqu’on ouvre
le HTML depuis le dépôt. À l’ouverture de F1, le programme adapte les liens au dossier de la notice
extraite. Le test `liens_et_images_de_la_notice` vérifie les ressources hors connexion dans chaque
langue. [Écriture de la notice](src/help.cpp) · [Ressources CMake](CMakeLists.txt)

Les tests `tp_notice_diametres_rondelle` et `tp_notice_interfrange` ouvrent les PNG embarqués.
Le premier relève les graduations et les bords du métal sur une ligne traversant le centre de
la rondelle. Le second retrouve les cinq maxima du profil sur la pose courte et divise la distance
entre les deux maxima extrêmes par quatre. Relancez ces tests avant de changer une image du TP
ou une valeur de contrôle dans la notice.
