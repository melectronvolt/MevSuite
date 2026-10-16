Les quatre logiciels de la suite, version {{version}}, pour Windows et Linux.

| Logiciel | Rôle |
|----------|------|
| **MeVPix** | mesures de longueurs sur une image (étalonnage, mesures, profil d'intensité) |
| **MeVCine** | étude cinématique de vidéos (pointage, repère, échelle, export CSV et Python) |
| **MeVReg** | traitement des données des TP (tableur, graphes, modélisation, incertitudes, Python) |
| **MeVSono** | analyse spectrale des sons (signal, période, spectre, harmoniques) |

### Windows (x64)

- **Version portable**, sans installation : `<Logiciel>-{{version}}-windows-x64.zip`. Décompresser,
  puis lancer `<logiciel>.exe` dans le dossier obtenu ; le dossier se copie tel quel sur une clé
  USB ou un partage réseau.
- **Installateur** : `<Logiciel>-{{version}}-windows-x64-setup.exe`. Installation pour tous les
  utilisateurs dans `Program Files\MeVSuite`, raccourci dans le menu Démarrer (dossier MeVSuite).
  Déploiement sur un parc (WAPT, GPO) :
  `<Logiciel>-{{version}}-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART` ;
  désinstallation : `"C:\Program Files\MeVSuite\<Logiciel>\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`.

Les programmes ne sont pas signés : au premier lancement, Windows peut afficher « Windows a protégé
votre ordinateur » ; cliquer sur « Informations complémentaires », puis « Exécuter quand même ».

### Linux (x86_64)

- **AppImage**, sans installation, pour les distributions récentes (Ubuntu 24.04, Debian 13,
  Fedora 40 ou plus récentes) : `<Logiciel>-{{version}}-x86_64.AppImage`, à rendre exécutable puis
  à lancer.
- **Ubuntu 26.04** : `<logiciel>_{{version}}_amd64.deb` (`sudo apt install ./<fichier>.deb`).
- **Fedora 45** : `<logiciel>-{{version}}-1.fc45.x86_64.rpm` (`sudo dnf install ./<fichier>.rpm`).
  MeVCine lit les vidéos des téléphones (H.264, HEVC) avec le ffmpeg de RPM Fusion.

Chaque logiciel a sa notice intégrée (menu Aide, touche F1). Contact : Rémi MEVAERE,
suite@sciences-physiques.net.
