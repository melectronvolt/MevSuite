// Répertoires utiles : données de l'application (notice), documents de
// l'utilisateur ; programmes de ffmpeg ; extensions des vidéos. Les chemins sont écrits avec des « / »
// (convention de Qt) ; `QDir::toNativeSeparators` les met à la mode du système pour l'affichage.

#pragma once

#include <QString>
#include <QStringList>

namespace chemins {

/// répertoire des données (la notice) ; la variable d'environnement MEVCINE_DATA permet d'en
/// choisir un autre
QString data_dir();
/// la notice (un site HTML, une page par sujet et par langue)
QString help_dir();

/// le dossier où l'utilisateur range ses vidéos (Vidéos, sinon Téléchargements, sinon son dossier
/// personnel)
QString dossier_videos_utilisateur();
/// répertoire des documents de l'utilisateur
QString document_dir();

/// ouvre un fichier avec l'application par défaut du bureau
bool ouvre_avec_le_bureau(const QString &chemin);

/// chemin absolu d'un fichier, pour les préférences et les projets
QString absolu(const QString &chemin);

/// Le programme de ffmpeg à lancer (« ffmpeg », « ffprobe ») : celui livré avec MeVCine, dans le
/// dossier `ffmpeg` à côté de l'exécutable, sinon celui du système (cherché dans le PATH).
QString outil(const QString &nom);

/// Extensions des fichiers vidéo proposés par le dialogue d'ouverture (téléphones, appareils photo,
/// caméras de sport, caméscopes, webcams). Un fichier d'une autre extension peut quand même être
/// ouvert : c'est ffmpeg qui décide s'il s'agit d'une vidéo.
extern const QStringList EXTENSIONS_VIDEO;

/// Les motifs du filtre du dialogue d'ouverture (« *.mp4 »…), en minuscules et en majuscules : sous
/// Linux, le filtre distingue les deux, et les vidéos des iPhone s'appellent IMG_1234.MOV.
QStringList filtre_videos();

} // namespace chemins
