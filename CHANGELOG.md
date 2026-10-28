# Journal des modifications

Les changements visibles des quatre logiciels de la MevSuite, qui partagent un même numéro de
version. Les plus récents en haut ; « Non publié » : ce qui sera dans la prochaine version.

## [0.3.1] — 2026-10-28

### MeVReg

- **Notice** à jour de la version 0.3.0 : menu Fenêtres, scripts d'un document reçu, virgule
  décimale dans les formules, incertitude-type d'une tolérance (÷ √3) et différente de chaque côté,
  méthode des tangentes pas à pas, journal `mevreg.log`, double clic sur les `.phy`. Nouvelles
  images (incertitude, tangentes, export CSV, menu Fenêtres, scripts reçus) ; graphe et aperçu de
  l'import CSV plus lisibles.
- **Référence Python** : couleurs et marqueurs de `mr.courbe`, `r.n`, `to_csv`, `to_dict` et
  `reset_index` de pandas, scripts liés et scripts d'un document reçu.
- **Panneau Calculs** : la ligne « nom = expression » accepte aussi la virgule décimale
  (`Ec = 0,5*m*v^2` était refusé), et son aide le dit.
- **Menu Fenêtres** : une case cochée marque les fenêtres affichées (seule leur icône était
  encadrée, peu visible) ; l'entrée s'appelle « Calculs », comme la fenêtre et `mr.ouvrir`.

### MeVCine

- **Notice** à jour de la version 0.3.0 : pas de choix du mode au lancement (menu Mode), ouverture
  d'une vidéo, cadence variable ou presque régulière, sauvegarde automatique, journal `mevcine.log`,
  installateur Windows, paquets Linux et AppImage, dépannage réécrit. Nouvelles images : menus
  Fichier et Mode, pointage en plein écran. Le TP du palet commence à l'image 15 (le palet est
  encore dans la main au début) ; les images le montrent pointé comme par un élève.
- Vidéo au ralenti ou en accéléré : le message nomme le champ « Images par seconde » tel qu'il
  s'affiche, et dit où il est en mode simplifié.

### MeVSono

- **Notice** à jour de la version 0.3.0 : les deux onglets et `Ctrl` + `Tab`, l'écoute en mode
  simplifié, les messages d'avertissement et de plantage (`mevsono.log`), les exports
  (`spectre.csv`, `extrait.csv`), les réglages de l'analyse (nouvelle image). Captures refaites
  (tableau des harmoniques entier) ; la page anglaise montre le mode expert comme la française.
- Réglages : le seuil des pics s'écrit avec le signe moins (« −40,0 dB »), comme ailleurs ; une valeur
  tapée dans la fenêtre des réglages n'est plus réécrite au milieu de la saisie (« -55,5 » donnait
  « −55,,0 », et le seuil restait à −55). Titres de la fenêtre en demi-gras, à la taille normale,
  comme ceux des panneaux.

### MeVPix

- **Notice** : page d'accueil en image ; nouvelle partie « Garder ses résultats » (copier le
  tableau, exporter le profil, capture d'écran : MeVPix n'enregistre rien) ; profil d'intensité :
  positions comptées depuis le début de la coupe, creux dus au papier millimétré, réticule, fichier
  CSV, alerte « 254 ou 255 ». En anglais, le réglage « Width » porte son vrai nom.
- Barre d'outils : « 📂 Ouvrir une image… » garde ses points de suspension, comme sur la page
  d'accueil et dans le menu.

### Toute la suite

- **Notices, partie professeurs** : dossier d'installation `Program Files\MeVSuite\<Logiciel>`,
  installation **et désinstallation** silencieuses (WAPT), lien vers les versions publiées, journal
  des erreurs et des plantages.
- README : une capture de chaque logiciel ; où trouver la notice, les réglages et les journaux.
- **Réglages imposés** (MeVPix, MeVSono) : sous Linux, le fichier se place dans `/etc/MeVSuite/`, avec
  les paquets comme avec l'AppImage (les paquets le cherchaient dans `/usr/bin`, l'AppImage n'en
  acceptait pas) ; sous Windows, toujours à côté du programme.
- Notices : les boîtes de dialogue et les menus sont montrés à leur taille réelle, nets, au lieu
  d'être agrandis ou réduits à une largeur fixe.

## [0.3.0] — 2026-10-28

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
    (`mr.supprimer("x")` puis `mr.creer("x", …)` supprimait x) ; une grandeur créée puis tracée par
    le script (`plt.plot`) garde son unité et va dans le tableur des résultats ;
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
  - les graphes gardent les incertitudes choisies côté par côté (une incertitude constante d'un côté
    comprise), les droites et les textes, la méthode des tangentes, l'étendue de la courbe des
    modèles et leur nom dans la légende ; les barres d'incertitude sont positives, comme dans
    MeVReg (matplotlib refuse les négatives).
- **Coller dans le tableur** : les cases qui ne sont pas des nombres (laissées vides) sont signalées,
  avec la première valeur en cause.
- **Export CSV** : un nom ou une unité qui contient le séparateur (« V, crête ») est mis entre
  guillemets ; l'en-tête garde ses colonnes.
- **Module `mevreg`** :
  - une grandeur nommée comme un nom de Python (`np`, `plt`…) reste lisible par `mr.valeurs` ;
  - `mr.axes(…, ymin=0)` : une seule borne suffit, l'autre suit les valeurs tracées ;
  - `mr.courbe(…, x_bas=, x_haut=, y_bas=, y_haut=)` : une grandeur d'incertitude pour chaque côté ;
  - droites et textes verrouillables (`verrou=True`), `mr.effacer_annotations`, `mr.tangentes` et
    `mr.retirer_tangentes` ; deux exemples de plus dans Python → Exemples ;
  - `mr.exporter_csv(…, separateur=",")` écrit les nombres avec un point (la virgule décimale les
    coupait en deux colonnes).
- **Notice** : dans le tableau des raccourcis, Entrée (descendre) et Tab (passer à droite) sont
  séparés.
- **Document reçu** : ses scripts Python ne se lancent qu'après accord (un script peut lire, modifier
  ou effacer des fichiers) ; « Ne pas lancer » est le choix par défaut, et le bouton « Exécuter » de
  la fenêtre d'un script le lance quand on l'a lu. Un document enregistré sur ce poste, ou dont les
  scripts ont été acceptés, les lance sans question.
- **Graphes** : titre par défaut à la taille des titres des axes (13 au lieu de 15) ; tableur : unité
  et incertitude sous le nom d'une grandeur à la taille normale (elles étaient plus petites).
- **Menu Fenêtres** : un clic sur une fenêtre affichée (cochée) la cache, un autre la réaffiche ; il
  ne servait qu'à la faire passer devant. La croix ✕ la ferme toujours (supprimée du document).
- **Réglages du graphe** : cocher « une grandeur pour chaque côté » (incertitudes) faisait planter
  MeVReg.
- **Python** : une grandeur recréée sous un nom libéré par un renommage (`mr.creer("x", …)`,
  `mr.renommer("x", "a")`, `mr.creer("x", …)`) garde ses valeurs ; la grandeur renommée aussi (elle
  prenait celles de la seconde).
- **Modélisation** :
  - les paramètres de tailles très différentes sont trouvés justes : une charge de constante de
    temps 1 ns, durées en seconde, donnait τ faux de 3,5 % ;
  - polynôme sur des abscisses très loin de zéro (x ≈ 10⁹) : R² est celui du polynôme affiché, avec
    un avertissement qui conseille de décaler les abscisses (R² annonçait un ajustement parfait) ;
  - résultats (copie, `print` d'un résultat de `mr.modeliser`) : « (fixé) » seulement pour un
    paramètre fixé ; sinon « incertitude non calculable » (deux points pour deux paramètres) ;
    « 1 point » au singulier.
- **Incertitude sur une grandeur** : l'exemple « 2 % + 1 digit » est une tolérance du constructeur,
  divisée par √3 pour donner l'incertitude-type : `(0.02*abs(U) + 0.01)/sqrt(3)`.
- **Mini-pandas** : `cumsum` saute les cases vides comme pandas (tout devenait vide après la
  première) ; `to_dict()` donne `{colonne: {étiquette: valeur}}` (et `orient="list"`, `"records"`,
  `"index"`) ; filtrer ou trier garde les étiquettes des lignes ; `reset_index`.
- **Export vers Python** : un titre ou un texte de graphe sur plusieurs lignes ne rend plus le
  script inutilisable.
- **Import CSV** : un nom entre guillemets écrit sur deux lignes garde son unité ; phrases de
  l'aperçu accordées (« 1 grandeur sera ajoutée », « 1 case ne contient pas de nombre »).
- **Agrandissement** : celui demandé dans une fenêtre étroite est retenu, et revient quand la fenêtre
  s'élargit (il était ramené à ce que permettait la fenêtre).
- **Menus** : Analyse (Alt+L) et Fenêtres (Alt+T) ont leur propre raccourci (Alt+A et Alt+N
  servaient deux fois).
- **Préférences** : la case « Demander s'il faut cacher ou fermer un tableur, un graphe, un script ou
  un dessin » et sa bulle décrivent les deux choix.
- **Grandeur calculée** : l'aide dit que le point et la virgule décimale sont acceptés ; le panneau
  Calculs accepte aussi la virgule décimale (`0,05*x` donnait une erreur).
- **Compilation** : Qt 6.8 au moins (Qt 6.5, annoncé, ne suffisait pas).
- **Linux** : les documents `.phy` s'ouvrent d'un double clic (type de fichier installé avec le
  paquet).

### MeVCine

- Avec plusieurs objets, une image pointée à moitié compte comme travail non enregistré : la
  confirmation avant de perdre des mesures la protège aussi.
- Une sauvegarde automatique impossible est signalée (une fois) : le travail n'est plus protégé.
- Cadence presque régulière (certains téléphones : 40 puis 44 ms) : les dates réelles des images sont
  utilisées, au lieu d'une cadence moyenne. Les arrondis des dates enregistrées dans le fichier (à la
  milliseconde dans les MKV et les WebM) ne comptent pas : une vidéo à cadence fixe garde des dates
  régulières.
- Ouverture d'une vidéo : curseur d'attente ; une vidéo dont l'examen ne finit pas est déclarée
  illisible au bout de deux minutes, au lieu de figer la fenêtre.
- La notice s'ouvre depuis une copie dans le dossier temporaire du système, comme dans les autres
  logiciels : ses liens restent valables après la fermeture de MeVCine (AppImage).
- Grapheur : grille et graduations aux couleurs communes de la suite.
- Accueil : plus de choix du mode au démarrage. MeVCine s'ouvre dans le mode de la dernière fois
  (simplifié au premier lancement) et propose tout de suite d'ouvrir une vidéo ; le mode se change
  dans le menu Mode.
- Trajectoire et chronogramme sur le fond des graphiques de la suite : sombre en thème sombre (ils
  restaient sur une feuille blanche), couleurs des objets éclaircies pour s'y voir.
- Accélération juste quand l'intervalle entre deux images change (image perdue, cadence variable des
  téléphones) : elle était fausse jusqu'à 50 % aux images qui suivent un saut.
- Ouverture d'une vidéo : l'examen se fait à part, la fenêtre reste dessinée ; clics, touches et
  fichiers déposés sont ignorés pendant ce temps, et fermer la fenêtre interrompt l'ouverture (il
  fallait attendre la fin de ffprobe, jusqu'à deux minutes).
- Un projet dont la longueur de l'étalon n'est pas valide (0) est refusé avec un message (il
  s'ouvrait avec des positions infinies).
- « À propos » : même présentation que dans les autres logiciels (logiciels libres utilisés,
  licence, fichier de réglages) ; « Code adapté de pymecavideo » traduit en anglais.
- Textes : le mode simplifié ne garde que l'objet 1 (ils promettaient toutes les mesures) ; le
  projet enregistre le chemin de la vidéo, pas la vidéo ; « 1 image » au singulier ; après une
  erreur interne, la sauvegarde automatique est annoncée « disponible » (elle peut dater un peu) et
  le journal seulement s'il a pu être écrit.
- Linux : les projets `.mevcine` s'ouvrent d'un double clic (type de fichier installé avec le
  paquet) ; Windows : de même, par l'installateur.

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
- Amplitude des pics juste, où que la fréquence tombe entre deux raies de la FFT : elle était fausse
  jusqu'à 27 % avec la fenêtre rectangulaire, 4 % avec Hann, 2 % avec le sommet plat ; la fréquence
  des pics est aussi plus juste.
- WAV : un bloc de données vide suivi de métadonnées n'est plus lu comme un son (fondamentale
  inventée) ; un enregistrement coupé (plus court que la taille annoncée) est signalé ; des valeurs
  illisibles (NaN, infinies) sont mises à 0 et signalées ; deux voies aux valeurs énormes (fichier
  flottant abîmé) ne donnent plus un mélange infini.
- Tableau des harmoniques : toutes ses colonnes se voient, le panneau s'élargit pour elles (f/f₁,
  A (dB) et A/A₁ étaient coupées) ; copié, l'en-tête de la dernière colonne est « A / A₁ (%) ».
- Clavier : `Ctrl` + `Tab` (ou `Ctrl` + `Page suivante`) passe d'une activité à l'autre, et `Tab`
  mène aux onglets.
- Fermer la fenêtre pendant un chargement l'interrompt : ffmpeg est arrêté (le programme restait
  ouvert tant qu'un ffmpeg bloqué ne finissait pas), la lecture d'un long fichier WAV aussi.
- Son silencieux : graduations courtes, lisibles (elles débordaient sur les titres des axes).
- Textes : l'aide de la taille de la FFT et de la finesse parle du découpage en segments (au-delà
  de 262 144 échantillons) ; « 1 échantillon » au singulier ; pannes de la sortie audio et dossier
  impossible à créer dans la langue choisie ; après une erreur imprévue, le journal n'est annoncé que
  s'il a pu être écrit.
- Compilation avec Qt 6.8.0 (une écriture de durée était ambiguë pour cette version).

### MeVPix

- Fermer la fenêtre avec des mesures ou une coupe demande confirmation (rien n'est enregistré) ;
  « Annuler » est le bouton par défaut.
- Étalonnage : déplacer un étalon court, même d'une fraction de pixel, demande de le valider à
  nouveau (la tolérance est maintenant proportionnelle à sa longueur).
- Étiquettes sur l'image : police de l'interface (elles suivent l'agrandissement), noms des curseurs
  A et B en demi-gras.
- Export CSV : comme dans MeVSono, plus de fichier sans extension.
- Ouvrir une autre image demande confirmation aussi quand seule une coupe est tracée ; « Annuler »
  est le bouton par défaut, comme à la fermeture (Entrée remplaçait l'image).
- Une image déposée pendant un chargement est ignorée (« Chargement… » restait affiché après
  « Annuler »).
- Profil d'intensité : l'alerte de saturation tient compte de tous les pixels qui comptent dans la
  valeur interpolée (un voisin écrêté n'était pas vu) ; elle annonce « 254 ou 255 », et une intensité
  réelle « peut-être » plus grande.
- Format d'image illisible : le message conseille de convertir en PNG ou en JPEG (il conseillait
  aussi TIFF, que la version AppImage ne lit pas).
- Après une erreur imprévue, le journal n'est annoncé que s'il a pu être écrit ; dossier des
  réglages impossible à créer : message dans la langue choisie.
- Anglais : « profile line » partout (la confirmation de fermeture disait « cut »).
- Panneau latéral : largeur revue à chaque nouvelle mise en page de son contenu, comme dans MeVSono.
- Notice : le format TIFF n'est pas lu par la version AppImage.

### Toute la suite

- Les README de chaque logiciel renvoient aux versions distribuables (ils disaient qu'il n'y en avait
  pas).
- Disque plein (clé USB…) : un fichier écrit en partie n'est plus mis à la place de l'ancien ni
  annoncé enregistré. L'écriture échoue avec sa cause, et l'ancien fichier reste intact : projets
  MeVCine, exports CSV de MeVPix, MeVCine et MeVSono, réglages, notices.
- Boutons du thème et de la langue, à droite des menus : à la taille de l'interface agrandie (ils
  restaient petits dans MeVCine).
- Windows : les licences des bibliothèques livrées (Qt, Python…) et la liste de leurs versions sont
  dans le dossier `licences` de chaque logiciel.
- **Plantage** (accès mémoire invalide…) : le logiciel ne disparaît plus sans rien dire. Il note la
  cause et la pile d'appels dans son journal (`mevpix.log`, `mevcine.log`, `mevreg.log`,
  `mevsono.log`, dans le dossier des réglages), puis un message dit où le trouver. Jusque-là, seules
  les erreurs internes détectées par le logiciel y étaient notées. MeVReg, qui n'avait pas de
  journal, en a un. Une erreur interne dans un calcul de fond est montrée aussi ; un journal qui n'a
  pas pu être écrit (disque plein) n'est pas annoncé.
- Windows : une erreur interne (exception) est notée avec son message, au lieu d'un simple code.
- Agrandissement de l'interface : `Ctrl` + dans une fenêtre trop étroite pour l'afficher ne fait plus
  baisser l'agrandissement retenu.

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
