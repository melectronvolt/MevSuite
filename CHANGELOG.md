# Journal des modifications

Les changements visibles des quatre logiciels de la MevSuite, qui partagent un même numéro de
version. Les plus récents en haut ; « Non publié » : ce qui sera dans la prochaine version.

## [Non publié]

### MeVReg

- **Tableur** : les colonnes se règlent à la main de façon fiable. La poignée, au bord droit d'un
  en-tête, s'attrape à quelques points de part et d'autre du trait ; elle est soulignée au survol,
  avec la flèche ↔. Viser le bord ne fait plus partir la colonne en glisser-déposer. La largeur
  choisie est gardée, les autres colonnes se partagent le reste ; double-clic sur le bord : retour au
  partage égal.
- **Fenêtre Grandeurs** : la colonne « Nom » se règle à la main comme les autres (elle prenait
  toujours toute la largeur restante).
- **Boîtes de dialogue** : un message d'erreur agrandit la boîte au lieu d'écraser les champs de
  saisie, et il s'efface dès qu'un champ est corrigé (il parlait encore de l'ancienne saisie).
- **Nouvelle grandeur** (bouton « + » du tableur) : l'indication renvoie au bouton « Calculée » du
  tableur (elle citait un bouton « f(x) » qui n'existe pas).
- **Formules** : la virgule entre les arguments d'une fonction est gardée (`min(2,3)` donnait 2,3) ;
  ailleurs, une virgule entre deux chiffres reste une virgule décimale (`0,05*x`, `sin(0,5)`).
- **Enregistrer** : le document est écrit à côté puis mis à la place de l'ancien ; un échec (disque
  plein, clé USB retirée) laisse le fichier précédent intact au lieu de le vider. De même pour les
  exports CSV et Python.
- **Python** :
  - les ordres de `mevreg` sont faits dans l'ordre du script, créations de grandeurs comprises
    (`mr.supprimer("x")` puis `mr.creer("x", …)` supprimait x) ;
  - un script lié qui lit les grandeurs par `mevreg` (`mr.valeurs("x")`) est relancé quand elles
    changent ;
  - « Arrêter » retire aussi un script qui attendait son tour ;
  - un script encore en cours à l'ouverture d'un autre document est arrêté, et ses résultats ne vont
    pas dans le nouveau document.
- **Export vers Python (Capytale)** :
  - un script qui utilise `mevreg` ou `fenetre` (propres à MeVReg) est mis en commentaire : le fichier
    exporté ne s'arrête plus sur une erreur ;
  - avec des cases vides, dérivées et primitives les sautent comme dans MeVReg (fonctions `deriv` et
    `integ` ajoutées au script) ; `integ (y, t)` écrit avec une espace est reconnu ;
  - deux grandeurs aux noms proches d'un mot réservé (`class`, `class_`) restent deux variables ;
  - les graphes gardent les incertitudes choisies côté par côté, les droites et les textes, la méthode
    des tangentes, l'étendue de la courbe des modèles et leur nom dans la légende.
- **Coller dans le tableur** : les cases qui ne sont pas des nombres (laissées vides) sont signalées,
  avec la première valeur en cause.
- **Export CSV** : un nom ou une unité qui contient le séparateur (« V, crête ») est mis entre
  guillemets ; l'en-tête garde ses colonnes.
- **Module `mevreg`** :
  - une grandeur nommée comme un nom de Python (`np`, `plt`…) reste lisible par `mr.valeurs` ;
  - `mr.axes(…, ymin=0)` : une seule borne suffit, l'autre suit les valeurs tracées ;
  - `mr.courbe(…, x_bas=, x_haut=, y_bas=, y_haut=)` : une grandeur d'incertitude pour chaque côté ;
  - droites et textes verrouillables (`verrou=True`), `mr.effacer_annotations`, `mr.tangentes` et
    `mr.retirer_tangentes` ; deux exemples de plus dans Python → Exemples.
- **Notice** : dans le tableau des raccourcis, Entrée (descendre) et Tab (passer à droite) sont
  séparés.

### MeVCine

- Avec plusieurs objets, une image pointée à moitié compte comme travail non enregistré : la
  confirmation avant de perdre des mesures la protège aussi.
- Une sauvegarde automatique impossible est signalée (une fois) : le travail n'est plus protégé.
- Cadence presque régulière (certains téléphones : 40 puis 44 ms) : les dates réelles des images sont
  utilisées, au lieu d'une cadence moyenne.
- Ouverture d'une vidéo : curseur d'attente ; une vidéo dont l'examen ne finit pas est déclarée
  illisible au bout de deux minutes, au lieu de figer la fenêtre.
- La notice s'ouvre depuis une copie dans le dossier temporaire du système, comme dans les autres
  logiciels : ses liens restent valables après la fermeture de MeVCine (AppImage).
- Grapheur : grille et graduations aux couleurs communes de la suite.

### MeVSono

- Fichier WAV suivi de métadonnées (fréquent avec les enregistreurs) : elles ne sont plus lues comme
  des échantillons (bruit à la fin du son).
- Pendant le calcul du spectre d'un nouvel extrait, l'export du spectre et la copie du tableau des
  harmoniques attendent la fin du calcul (ils donnaient l'ancien extrait).
- L'agrandissement des textes demandé est retenu : réduit quand la fenêtre rétrécit, il revient quand
  elle s'élargit, et c'est lui qui est enregistré.
- Tableau des harmoniques : affiché même sans pic à la fondamentale (tiret au rang 1, rapports à sa
  fréquence).
- Un fichier abîmé lu en partie, et un son coupé à 30 minutes quand ffmpeg le lit, sont signalés.
- Export CSV : un nom tapé sans « .csv » ne crée plus un fichier sans extension ; si le fichier
  « .csv » existe déjà, le programme demande avant de le remplacer.

### MeVPix

- Fermer la fenêtre avec des mesures ou une coupe demande confirmation (rien n'est enregistré) ;
  « Annuler » est le bouton par défaut.
- Étalonnage : déplacer un étalon court, même d'une fraction de pixel, demande de le valider à
  nouveau (la tolérance est maintenant proportionnelle à sa longueur).
- Étiquettes sur l'image : police de l'interface (elles suivent l'agrandissement), noms des curseurs
  A et B en demi-gras.
- Export CSV : comme dans MeVSono, plus de fichier sans extension.
- Notice : le format TIFF n'est pas lu par la version AppImage.

### Toute la suite

- Les README de chaque logiciel renvoient aux versions distribuables (ils disaient qu'il n'y en avait
  pas).

## [0.2.0] — 2026-10-16

Première version publiée des logiciels en C++ avec Qt 6 ; ils remplacent les versions écrites en
Rust (PixMev, CineMev, RegMev, SpectralMev).

### Toute la suite

- Même fenêtre dans les quatre logiciels : menus, thème clair ou sombre (soleil ou lune) et langue
  (drapeau) à droite des menus, barre d'état typée, page d'accueil épurée, agrandissement des
  textes (`Ctrl` + / `Ctrl` − / `Ctrl` 0).
- Pas d'exemples livrés : élèves et professeurs ouvrent leurs propres fichiers.
- Réglages de tous les logiciels dans un seul dossier `MeVSuite` (`%APPDATA%\MeVSuite`,
  `~/.config/MeVSuite`).
- Notices pour les élèves d'abord, puis pour les professeurs, ouvertes dans le navigateur (menu
  Aide, `F1`).
- Versions distribuables : version portable et installateur pour Windows (Program Files, menu
  Démarrer, installation silencieuse pour WAPT), AppImage, paquets `.deb` (Ubuntu 26.04) et `.rpm`
  (Fedora 45).

### MeVPix

- Mesures de longueurs sur une image : étalonnage, mesures, profil d'intensité, export CSV.

### MeVCine

- Étude cinématique de vidéos : pointage (plein écran, loupe), repère, échelle, exports CSV et
  Python, mode simplifié et mode expert.

### MeVReg

- Tableur de grandeurs, formules et paramètres, graphes avec incertitudes, modélisation (plage et
  étendue de la courbe réglables sur le graphe), statistiques, dérivée, intégrale, spectre,
  méthodes de dosage.
- Import CSV avec aperçu (séparateurs, lignes des noms et des unités, lignes vides) ; export CSV
  réglable.
- Python intégré : numpy, matplotlib et pandas simplifiés, module `mevreg` qui pilote tout le
  logiciel, menu Python → Exemples ; référence Python dans la notice.
- Fenêtres détachables, à cacher ou à fermer ; droites, textes et tangentes déplaçables et
  verrouillables sur les graphes.

### MeVSono

- Analyse des sons : mesure de la période (curseurs t₁ et t₂), spectre de l'extrait A–B,
  fondamentale et harmoniques, écoute, mode simplifié et mode expert.
