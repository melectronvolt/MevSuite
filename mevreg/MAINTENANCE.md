# Entretenir MeVReg

Pour une première intervention, prendre une séance courte dont on connaît le résultat. Le fichier de mesures de la loi d’Ohm fourni avec la notice convient. On peut le rouvrir après une modification et contrôler la pente de 47 Ω. [Mesures en CSV](assets/help/resistance.csv) · [Déroulement du TP](assets/help/notice.html)

## Compiler et vérifier une première fois

Ouvrir un terminal dans le dossier `MevSuite`, celui qui contient `mevreg` et `commun`. Sous Windows, utiliser le terminal **UCRT64** de MSYS2. La liste des paquets nécessaires pour chaque système figure dans le [README de MeVReg](README.md#compilation).

```sh
cmake -S mevreg -B build/mevreg -G Ninja
cmake --build build/mevreg --parallel 4
ctest --test-dir build/mevreg --output-on-failure
```

La première commande prépare le dossier de travail du compilateur. La deuxième y construit le programme. La troisième joue les essais et indique le nom de ceux qui échouent. À la fin, on doit lire `100% tests passed`. Après avoir modifié un fichier source, reprendre aux deux dernières commandes. Pour ouvrir la version qu’on vient de construire, lancer `./build/mevreg/mevreg` (ou `./build/mevreg/mevreg.exe` sous Windows).

Si CMake signale que NumPy est inaccessible, vérifier quel Python l’importe dans les mêmes conditions que MeVReg.

```sh
python3 -I -c "import sys, numpy; print(sys.executable, numpy.__version__)"
```

Si cette commande réussit, reprendre la configuration en donnant à `-DPython3_EXECUTABLE=` le chemin affiché. Par exemple, sous Linux avec le Python de la distribution, la commande est `cmake -S mevreg -B build/mevreg -DPython3_EXECUTABLE=/usr/bin/python3`. Si l’import échoue, installer les paquets Python de développement et NumPy indiqués pour son système.

## Retrouver le code qui fait le travail

Dans un fichier `.h`, on lit les noms des opérations, ce qu’elles reçoivent et ce qu’elles renvoient. Le fichier `.cpp` contient les instructions exécutées. Par exemple, `Document::definit_valeur` désigne l’opération qui inscrit une mesure dans un `Document`. On peut chercher ce nom dans l’éditeur de code.

| Ce qu’on souhaite modifier | Où commencer |
| --- | --- |
| Un libellé, un menu, une étape du mode Simplifié | [Menus et accueil](src/app/mainwindow.cpp) · [Étapes du TP](src/app/guide.cpp) |
| Le démarrage, la fenêtre À propos ou le site de l’auteur | [Présentation de MeVReg](src/app/about.cpp) · [Coordonnées communes aux logiciels](../commun/coordonnees_auteur.h) |
| Une cellule, la copie ou la sélection de plusieurs colonnes | [Tableur](src/app/tableview.cpp) · [Écriture des mesures](src/core/document.cpp) |
| Une formule, une dérivée ou une intégrale | [Syntaxe et opérations](src/core/expr.cpp) · [Calculs numériques](src/core/numeric.cpp) · [Ordre des formules](src/core/formules.cpp) |
| Un ajustement, ses paramètres ou les résidus | [Modèles](src/core/fit.cpp) · [Mesures retenues pour le graphe](src/plot/resolve.cpp) · [Courbe des résidus](src/app/statspanel.cpp) |
| Le contenu d’une séance ou la demande d’enregistrement | [Vérification du fichier](src/core/fichier_seance.cpp) · [Contenu de la séance](src/core/workspace.cpp) · [Ouverture, enregistrement et secours](src/app/mainwindow_fichiers.cpp) |
| L’emplacement des panneaux, le dessin d’un graphe ou son impression | [Fenêtres de travail](src/app/mainwindow_fenetres.cpp) · [Dessin des graphes](src/plot/renderer.cpp) · [Exports et impression](src/app/mainwindow_exports.cpp) |
| Un script lancé dans MeVReg ou le script remis à un élève pour Capytale | [Interpréteur intégré](src/python/engine.cpp) · [Export de la séance](src/core/pyexport.cpp) · [Traduction des formules en NumPy](src/core/pyformule.cpp) |
| L’effectif, la dispersion ou les barres d’un histogramme | [Statistiques des mesures](src/core/stats.cpp) · [Panneau Statistiques](src/app/statspanel.cpp) |
| La notice, ses illustrations ou les exercices Python | [Guide de TP](assets/help/notice.html) · [Atelier Python](assets/help/python.html) · [Extraction de l’aide hors connexion](src/app/help.cpp) |

## Suivre une mesure du clavier au graphe

1. L’élève saisit `4,7` dans la colonne `U`. Dans le tableur, `TableModel::setData` reconnaît le nombre et retrouve la grandeur par son identifiant, appelé `QId`. Cet identifiant reste le même si la colonne change de nom ou de place.
2. Le tableur appelle `Store::edite`. Le `Store` conserve la séance ouverte et son historique. Il donne accès à `Seance`, qui réunit les données, les tableurs, les graphes et les scripts. Les mesures et les formules se trouvent dans `Seance::doc`.
3. `Document::definit_valeur` écrit la valeur et recalcule les formules. Si une autre colonne contient `P = U*I`, la nouvelle puissance est disponible avant que l’interface se redessine.
4. Le `Store` vérifie les entrées de chaque ajustement. Si l’élève corrige une température, l’ajustement de la tension en fonction du temps conserve ses coefficients. Quand les valeurs de la tension changent, même par une formule, le modèle est recalculé. Le signal Qt `espace_travail_change` prévient ensuite les fenêtres inscrites pour qu’elles actualisent leur affichage.

Le mode Simplifié et le mode Expert lisent le même document. Pour modifier une règle de calcul utilisée dans les deux modes, partir du dossier `src/core`. Pour modifier la façon de présenter le résultat à un élève, partir du panneau concerné. [Opérations du document](src/core/document.h) · [Séance](src/core/workspace.h) · [État et historique](src/app/store.h)

Le vocabulaire du modèle reprend celui du TP. Une `Grandeur` contient les valeurs d’une colonne. Une case vide y est représentée par `NaN`, un marqueur numérique que l’on teste avec `std::isfinite` pour compter les mesures exploitables. Un `Parametre` peut représenter une masse commune à toutes les lignes. `Document::nombre_lignes` indique la hauteur nécessaire au tableur, tandis que `Grandeur::effectif` compte les lectures renseignées. Une colonne de trois lignes dont la deuxième est vide a donc un effectif de deux. [Définitions du document](src/core/document.h)

Le texte d’une cellule en cours de frappe se trouve d’abord dans un éditeur Qt. Avant d’enregistrer ou de remplacer une séance, `MainWindow::valide_saisies` demande au tableur de transmettre cette valeur. Le bouton Enregistrer et le raccourci Ctrl+S doivent aboutir au même résultat. Le test `enregistre_cellule_en_cours` tape une virgule décimale puis relit le fichier effectivement écrit. [Édition de la cellule](src/app/tableview.cpp) · [Enregistrement](src/app/mainwindow_fichiers.cpp)

## Cas à vérifier en modifiant le code

**Sélectionner plusieurs mesures.** Choisir les colonnes A et C avec Ctrl, puis couper. Le presse-papiers contient A et C. B garde ses mesures. Avec deux cellules prises en diagonale, les emplacements intermédiaires de la copie restent vides. Supprimer des *lignes* a un autre effet que vider des cellules. Les lignes choisies sont retirées ensemble des grandeurs liées pour conserver les couples de mesures. L’essai `selection_discontinue_tableur` contrôle aussi l’annulation de la coupe. [Essais de l’interface](tests/test_app.cpp)

**Calculer avec une mesure manquante.** Pour `x = [1, 2, 3]` et `y = [4, 5]`, la colonne `x+y` contient 5, 7 et une case vide. Dans le code, cette case porte la valeur `NaN`. Une colonne d’une seule valeur est un cas particulier, sa valeur s’applique à chaque ligne. Si on supprime la source d’une formule, toute formule qui en dépend doit signaler l’erreur. Une somme égale à zéro serait ici un résultat trompeur. Pour comparer indépendamment le tableur et l’export, `export_colonnes_inegales` exécute le script produit avec NumPy. [Essais des calculs et de l’export](tests/test_core.cpp)

**Quitter une séance pendant une saisie.** L’éditeur Python transmet son texte au document après une courte pause dans la frappe. Avant de remplacer la séance, `confirme_remplacement` transmet aussitôt le texte des éditeurs et termine l’étape d’historique en cours. Le choix « Enregistrer » doit réussir pour que le remplacement continue. Pour vérifier ce parcours, `derniere_frappe_avant_nouveau` saisit une lettre, déclenche « Nouveau », annule, puis recommence et relit le fichier enregistré. [Gestion des fichiers](src/app/mainwindow_fichiers.cpp) · [Essai de sauvegarde](tests/test_app.cpp)

Une **incertitude constante** doit avoir autant de valeurs que la série mesurée avant d’être lue par une autre formule. Avec quatre tensions et une incertitude de 0,2 V, `len(u_U)` vaut 4. L’essai `incertitude_dans_une_formule` vérifie ce calcul dans la séance rouverte et dans le script exporté vers NumPy. [Calcul des colonnes](src/core/document.cpp) · [Export Python](src/core/pyexport.cpp)

À **l’ouverture d’une séance**, le fichier contient déjà les colonnes corrigées. Rejouer un script d’étalonnage pourrait appliquer deux fois la correction. Les scripts liés prennent les valeurs relues comme point de départ. Les dessins gardent le nom de leur fenêtre dans le script qui les a produits. L’essai `reouverture_conserve_correction_python` vérifie que régénérer un dessin laisse les mesures d’un autre script intactes. [Gestion des fichiers](src/app/mainwindow_fichiers.cpp) · [Essais de la fenêtre principale](tests/test_app.cpp)

Pour **l’aperçu CSV**, la fenêtre demande les huit premières lignes, en-têtes compris. Le nombre annoncé au bas de l’aperçu se déduit de la plus longue colonne. Si vous changez l’écriture d’un nombre ou d’une unité, l’essai `apercu_csv_et_fichier_complet` compare l’aperçu au début du fichier complet. [Formatage CSV](src/core/csv.cpp)

**Petites fluctuations autour d’une grande valeur.** Pour deux lectures distantes de 2, l’écart-type de population vaut 1 quel que soit le décalage commun. Le calcul de la moyenne conserve les petits termes par une somme compensée. Un second passage calcule la dispersion en tenant compte de l’arrondi de la moyenne. Le panneau affiche séparément l’écart-type corrigé et l’incertitude-type de la moyenne. Le test `dispersion_autour_d_une_grande_valeur` couvre aussi l’histogramme d’une série constante. [Statistiques](src/core/stats.cpp) · [Essais numériques](tests/test_core.cpp)

**Script lié à une mesure.** Dans `print(f"{U.mean():.2f}")`, seule la partie entre accolades lit la grandeur `U`. Une correction de la tension doit relancer ce calcul. Le lecteur d’identifiants reconnaît les expressions des chaînes formatées, les commentaires et l’écriture scientifique. Il fonctionne aussi pendant la frappe, avant que le script soit exécutable. [Lecture des noms](src/python/identifiants.cpp) · [Suivi des entrées](src/app/store.cpp)

**Exemples de la notice.** Les trois blocs de l’atelier marqués `data-essai` sont exécutés tels quels avec le Python embarqué. Pour changer un exemple, calculer le résultat attendu, modifier le bloc HTML et l’attendu correspondant dans `exercices_notice`. L’essai `pages_notice` vérifie les ancres, les illustrations et la présence des fonctions publiques du module `mevreg`. F1 place les fichiers dans le cache propre au compte utilisateur. [Atelier détaillé](assets/help/python.html) · [Essais de la notice](tests/test_app.cpp)

## Faire et contrôler une petite modification

Pour changer la bulle du bouton **Ouvrir…** de la page d’accueil, rechercher `m_welcomeOpen->setToolTip` dans `src/app/mainwindow.cpp`. Modifier le texte entre guillemets, enregistrer le fichier et recompiler. Lancer le programme depuis le dossier de compilation et survoler le bouton de la page d’accueil pour relire le texte.

Pour une modification de calcul, écrire d’abord quelques valeurs attendues à la main. Pour un modèle, le résidu se calcule par **mesure − prévision** dans l’unité de l’ordonnée. Une tension mesurée de 1,10 V pour une prévision de 1,03 V donne +0,07 V. L’essai `residus_affiches_modele_personnel` vérifie ce signe dans la courbe affichée.

Ces commandes rejouent seulement le cas que l’on vient de toucher. Les commandes directes ci-dessous sont celles d’un terminal Linux. Sur un poste Windows, le nom des exécutables se termine par `.exe`.

```sh
./build/mevreg/test_core export_colonnes_inegales
QT_QPA_PLATFORM=offscreen ./build/mevreg/test_app residus_affiches_modele_personnel
ctest --test-dir build/mevreg -R '^seances$' --output-on-failure
```

`offscreen` fait fonctionner les fenêtres de l’essai sans les afficher sur le bureau. En cas d’échec, chercher la première ligne `FAIL!`, relever la valeur obtenue, la valeur attendue et le nom de l’essai. Rejouer ce seul cas après la correction, puis relancer la suite complète. Pour comprendre comment un TP utilise plusieurs fonctions à la fois, commencer par `loi_ohm_depuis_fichier`. [Parcours de TP](tests/test_seances.cpp)

## Contrôler les fichiers de séance et les échanges

Un fichier `.phy` contient du texte au format JSON. Dans une copie ouverte avec un éditeur de texte, on peut retrouver le numéro de version du format, les grandeurs dans `workspace.doc.quantities` et les fenêtres dans `layout`. À la lecture, MeVReg recalcule les formules à partir des mesures. Si on ajoute un réglage à enregistrer, choisir la valeur à utiliser quand ce réglage est absent d’une ancienne séance. Un document de l’ancien format peut omettre des listes facultatives. Les grandeurs et paramètres partagent un espace d’identifiants, les fenêtres un autre. Si deux éléments d’un même espace ont le même identifiant, le lecteur signale le fichier avant de remplacer le travail en cours. Les essais `phy_aller_retour`, `phy_version_un` et `phy_verifie_la_structure_avant_ouverture` couvrent ces situations. [Lecture et écriture des séances](src/core/fichier_seance.cpp) · [Essais du format](tests/test_core.cpp)

Pour contrôler un changement d’import CSV, les trois autres logiciels de la suite peuvent produire eux-mêmes leurs fichiers d’essai. Quand leurs tests sont compilés, lancer depuis `MevSuite` la commande suivante, en adaptant les quatre chemins aux dossiers de compilation du poste.

```sh
bash packaging/test-echanges.sh build/mevpix build/mevcine build/mevsono build/mevreg
```

Le dernier résultat, `TestCore::imports_de_la_suite()`, indique si MeVReg a retrouvé les valeurs et les unités dans ces fichiers. [Essai d’échange entre logiciels](../packaging/test-echanges.sh)

## Interpréter un contrôle de la mémoire

Sous Linux, Clang peut signaler un accès mémoire invalide ou un comportement indéfini en indiquant la ligne concernée. Depuis la racine de MevSuite, on peut préparer une compilation distincte pour ce contrôle.

```sh
cmake -S mevreg -B build/mevreg-controle -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/mevreg-controle --parallel 4
QT_QPA_PLATFORM=offscreen ./build/mevreg-controle/test_app enregistre_cellule_en_cours
./build/mevreg-controle/test_python libere_tableau_apres_trace
./build/mevreg-controle/test_python tableaux_indices_unites_et_longues_acquisitions
```

**Pendant une séance**, le test `libere_tableau_apres_trace` observe la durée de vie d’un tableau NumPy de 2 Mio. Il vérifie sa libération avant le script suivant, après un affichage automatique, un appel à `plt.show()` ou `plt.close()`, une erreur et une interruption demandée par MeVReg. Le moteur vide la liste des figures à la fin du script, puis sollicite le ramasse-miettes de Python. Une figure et ses axes peuvent ainsi être libérés même s’ils se désignent mutuellement. La courbe affichée possède sa propre copie des mesures.

**À la fermeture**, LeakSanitizer examine aussi les allocations de l’interpréteur et de ses extensions. Le test attend que le dernier script soit en cours avant de demander l’arrêt du moteur, pour reproduire la même séquence à chaque essai. Sous Fedora, avec Python 3.15.2 et NumPy 2.5.4, le test des longues acquisitions laisse 604 810 octets signalés après 1, 12 ou 100 exécutions dans un même processus. Dans un second essai, le calcul du spectre d’une sinusoïde de 200 000 points laisse 643 511 octets avec MeVReg et 641 975 octets dans un programme qui utilise uniquement Python, sa bibliothèque standard et NumPy. Ces deux relevés restent également stables de 1 à 100 exécutions. Ils caractérisent ces versions et ces exercices. CPython documente la possibilité d’allocations résiduelles après sa finalisation. [Documentation de `Py_FinalizeEx`](https://docs.python.org/fr/3.15/c-api/interp-lifecycle.html#c.Py_FinalizeEx)

Pour refaire l’essai des longues acquisitions dans le même interpréteur, passer plusieurs fois son nom à Qt Test. Ces commandes Bash effectuent douze passages depuis la racine de MevSuite.

```bash
essais=()
for numero in {1..12}
do
    essais+=(tableaux_indices_unites_et_longues_acquisitions)
done
ASAN_OPTIONS=detect_leaks=1 ./build/mevreg-controle/test_python "${essais[@]}"
```
