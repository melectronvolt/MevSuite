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
