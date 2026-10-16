// Les pointages : séquences éventuellement creuses de positions, pour chaque image de la vidéo et
// chaque objet suivi.
//
// Les positions sont en pixels de l'image native de la vidéo (après rotation), et non en pixels de
// l'écran : on peut ainsi redimensionner la fenêtre à tout moment.

#pragma once

#include "echelle.h"
#include "vecteur.h"

#include <QString>

#include <optional>
#include <tuple>
#include <utility>
#include <vector>

using Position = std::optional<Vecteur>;

class Pointage {
public:
    /// intervalle de temps entre deux images (intervalle habituel si la cadence de la vidéo est
    /// variable)
    double delta_t = 0.04;
    /// si la cadence de la vidéo est variable : date de chaque image depuis la première, comptée en
    /// intervalles habituels (voir `VideoInfo::instants`) ; les dates en seconde s'en déduisent avec
    /// `delta_t`, qui tient compte d'un ralenti
    std::optional<std::vector<double>> instants;
    /// position de l'origine du repère sur l'image
    Vecteur origine;
    Echelle echelle;
    /// sens de l'axe des abscisses (±1)
    int sens_x = 1;
    /// sens de l'axe des ordonnées (±1)
    int sens_y = 1;

    /// Crée les structures de données quand on en connaît la taille ; efface les pointages existants.
    void dimensionne(int n_suivis, double delta_t, int n_images);
    /// Ne garde que le premier objet suivi et ses pointages (passage au mode Simplifié, qui ne suit
    /// qu'un objet).
    void garde_premier_objet();

    int nb_obj() const { return m_nb_obj; }
    /// nombre d'images (pointées ou non)
    int len() const { return static_cast<int>(m_data.size()); }

    /// date de l'image d'index `index` (à partir de 0), depuis la première image de la vidéo
    double date(int index) const;
    /// vrai si les images ne sont pas régulièrement espacées dans le temps
    bool cadence_variable() const { return instants.has_value(); }
    /// date de l'image d'index `index` (à partir de 0) depuis la première image pointée : c'est la
    /// date zéro des tableaux et des exports
    double date_relative(int index) const;
    /// l'unité des positions : le mètre si l'échelle est définie, sinon le pixel
    QString unite() const;

    /// nombre d'images pointées
    int nb_images_pointees() const;

    struct LigneComplete {
        /// index de l'image (à partir de 0)
        int index;
        /// date depuis la première image pointée
        double t;
        /// position de chaque objet dans le repère choisi
        std::vector<Vecteur> positions;
    };
    /// Les images où tous les objets sont pointés, avec les positions de chaque objet dans le repère
    /// choisi (en mètre, ou en pixel si l'échelle n'est pas définie).
    std::vector<LigneComplete> lignes_completes() const;

    /// enregistre la position d'un objet (à partir de 1) sur une image (index à partir de 0)
    void pointe(int obj, Vecteur position, int index);
    /// position de l'objet `obj` (à partir de 1) sur l'image d'index `index` (à partir de 0), en pixels
    Position position(int obj, int index) const;
    /// les positions de tous les objets sur une image
    const std::vector<Position> &ligne(int index) const { return m_data[index]; }

    /// faux si aucune image n'a été pointée
    bool a_des_pointages() const;
    /// numéro de la première image pointée (à partir de 1)
    std::optional<int> premiere_image() const;
    /// numéro de la dernière image pointée (à partir de 1)
    std::optional<int> derniere_image() const;

    /// Retire le dernier pointage et l'empile pour pouvoir le refaire.
    void defaire();
    /// Dépile un pointage défait et le remet à sa place.
    void refaire();
    bool peut_defaire() const { return a_des_pointages(); }
    bool peut_refaire() const { return !m_defaits.empty(); }
    /// oublie les pointages à refaire (après un nouveau pointage)
    void purge_defaits() { m_defaits.clear(); }

    /// Données au format CSV des fichiers de projet.
    /// @param sep séparateur de champ
    /// @param en_metre vrai pour des positions en mètre, sinon en pixel
    /// @param debut numéro de la première image pointée : la date zéro
    /// @param origine origine du repère
    QString csv_string(const QString &sep, bool en_metre, int debut, Vecteur origine) const;

    /// un point en mètre, dans un repère « à l'endroit » ; `p` est en coordonnées de l'image
    Vecteur point_en_metre(Vecteur p) const;
    /// Une vitesse en m/s, dans un repère « à l'endroit » ; `v` est en pixel/s, dans le repère de
    /// l'image. Seules l'échelle et l'orientation des axes s'appliquent à une vitesse, pas la
    /// position de l'origine.
    Vecteur vitesse_en_metre(Vecteur v) const;
    /// vitesse (en pixel/s) de l'objet sur l'image d'index `index`, évaluée par l'écart avec la
    /// position précédente
    Position vitesse_px(int obj, int index) const;

    /// Vecteurs vitesse affichables : origine sur un point de la trajectoire, direction et norme
    /// d'après les points précédent et suivant ; il faut au moins trois pointages.
    /// @param echelle_vitesse nombre de pixels pour 1 m/s
    /// @return pour chaque objet, une liste de couples (origine, extrémité)
    std::vector<std::vector<std::pair<Vecteur, Vecteur>>> vecteurs_vitesse(double echelle_vitesse) const;

    /// Rejoue des pointages issus d'un fichier de projet
    /// @param lignes des lignes [t, x1, y1, ..., xn, yn], positions en mètre
    /// @param premiere_image_pointee la toute première image pointée (≥ 1)
    void restaure_pointages(const std::vector<std::vector<double>> &lignes, int premiere_image_pointee);

    /// Les pointages vus dans le référentiel d'un objet suivi : cet objet reste immobile au point
    /// `centre`, qui devient l'origine. Les images où l'objet de référence n'est pas pointé sont
    /// ignorées.
    Pointage dans_referentiel(int obj_ref, Vecteur centre) const;

private:
    /// `m_data[i][j]` : position de l'objet n° j+1 sur l'image n° i+1
    std::vector<std::vector<Position>> m_data;
    /// nombre d'objets suivis ; ils sont désignés par 1, 2, ...
    int m_nb_obj = 1;
    /// pile des pointages défaits : (index de l'image, positions)
    std::vector<std::pair<int, std::vector<Position>>> m_defaits;
};
