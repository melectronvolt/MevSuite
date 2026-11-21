# Journal des modifications

Les changements visibles des quatre logiciels de la MevSuite, qui partagent un même numéro de
version. Les plus récents en haut ; « Non publié » : ce qui sera dans la prochaine version.

## [0.4.0] — 2026-11-20

### MeVPix

- **Ouverture et zoom.** MeVPix démarre sur l’accueil sans charger d’image. Les loupes + et − et
  le bouton « Ajuster à la fenêtre » se trouvent en bas à droite de la photo. Le zoom de précision
  affiche les pixels en pleine résolution, y compris sur les grandes images.
- **Menus et barre d’outils.** Chaque commande des menus Fichier et Aide a son pictogramme.
  Les icônes d’ouverture, d’étalonnage, de mesure et de profil accompagnent leur libellé dans la barre d’outils.
- **Formats d’image.** La boîte d’ouverture et la notice F1 indiquent les formats pris en charge
  par les lecteurs Qt installés. La barre d’état signale les conversions vers 8 bits par composante.
- **Séances.** `Ctrl+S` enregistre dans la séance courante. « Enregistrer la séance sous… » crée
  une copie. Le titre indique le nom de la séance et les modifications en attente. Avant de fermer
  ou d’ouvrir un autre fichier, on peut enregistrer, abandonner les modifications ou annuler.
  Une sauvegarde annulée ou en échec laisse le travail ouvert.
- **Édition et étalonnage.** Échap annule le geste en cours. `Ctrl+Z` revient aussi sur les
  déplacements, les changements d’échelle et les suppressions de mesures. Si une référence a été
  modifiée après validation, MeVPix demande quelle échelle utiliser pour copier le tableau,
  exporter le profil ou produire une image annotée.
- **Profil d’intensité.** Les boutons A et B et les flèches du clavier permettent d’avancer
  d’un échantillon. Le survol lit l’intensité sur la courbe. Un double-clic rétablit la vue
  entière en conservant les curseurs. Le tracé conserve les extrema quand les points sont
  denses, et le calcul signale les bandes tronquées au bord de l’image. Le CSV contient toute la coupe.
- **Image annotée.** Le presse-papiers et le PNG peuvent inclure la coupe, les curseurs A/B et,
  sous la photo, le graphique avec Δx. Les deux sorties reproduisent la composition de l’aperçu.
- **Documentation.** Les notices française et anglaise illustrent les menus, les outils,
  la sauvegarde et les exports avec des captures agrandissables dans chaque langue.
  F1 les ouvre hors connexion depuis le cache personnel ou un dossier temporaire privé.

### MeVCine

- **Pointage.** La visée assistée utilise les pointages et les corrections de l’élève pour
  proposer la position suivante. Quand plusieurs positions sont plausibles, l’élève reprend
  la main. Le projet conserve les références de chaque objet.
- **Séances.** Chaque séance a sa propre copie de secours, y compris lorsque deux fenêtres
  travaillent sur la même vidéo. Le grapheur permet de cliquer sur une mesure pour retrouver
  l’image correspondante.

### MeVReg

- **Séance guidée.** Le mode Simplifié accompagne l’élève de la saisie des mesures au compte
  rendu. Le TP de la loi d’Ohm fourni avec la notice permet d’ajuster une droite et d’examiner
  les résidus.
- **Python.** Les scripts utilisent NumPy pour le calcul matriciel et les transformées de Fourier.
  La version portable pour Windows emporte NumPy, les réglages et les fichiers de récupération
  sur la clé USB lorsque celle-ci est accessible en écriture.

### MeVSono

- **Prise de son.** L’élève peut choisir son microphone et sa sortie audio, surveiller le niveau
  pendant l’enregistrement et conserver sa prise en WAV.
- **Analyse.** Les flèches du clavier parcourent les pics du spectre. La recherche de la
  fondamentale et de ses harmoniques tient compte des battements et indique quand la période
  reste indéterminée. L’export CSV affiche sa progression et peut être annulé.

## [0.3.4] — 2026-11-17

### Les quatre logiciels

- **Téléchargement** : chaque fichier de la release est aussi publié sous un nom sans numéro de
  version (`MeVCine-windows-x64-setup.exe`, `mevcine_amd64.deb`…). L'adresse
  `https://github.com/melectronvolt/MevSuite/releases/latest/download/<fichier>` mène ainsi
  toujours à la dernière version : liens permanents, mises à jour d'un parc (WAPT, GPO) sans
  changer l'adresse à chaque version. Le README les donne en entier.
- **Code source** : publié aussi sur la Forge des communs numériques éducatifs, en copie ; les
  versions à installer restent publiées sur GitHub seulement (README, « Où trouver MevSuite »).

### MeVCine

- **Suivi automatique** : un objet que le suivi ne retrouve pas sur une image n'est plus remplacé par
  l'objet suivant ; chaque objet garde ses propres mesures.
- **Projets** : un objet non pointé au milieu d'une ligne est enregistré (« nan ») au lieu de décaler
  les colonnes des objets suivants ; le fichier s'arrête à la dernière image pointée.
- Un fichier qui n'a pas été écrit par MeVCine est refusé avec le message « Fichier non reconnu ».
