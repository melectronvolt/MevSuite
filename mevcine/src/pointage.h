// Les pointages : séquences éventuellement creuses de positions, pour chaque image de la vidéo et
// chaque objet suivi.
//
// Les positions sont en pixels de l'image native de la vidéo (après rotation), et non en pixels de
// l'écran : on peut ainsi redimensionner la fenêtre à tout moment.

#pragma once

#include "echelle.h"
#include "vecteur.h"

#include <QString>

#include <map>
#include <optional>
#include <utility>
#include <vector>

using Position = std::optional<Vecteur>;

class Pointage {
public:
    /// intervalle de temps entre deux images (intervalle habituel si la cadence de la vidéo est
    /// variable)
    double delta_t = 0.04;
    /// si la cadence de la vidéo est variable : date de chaque image depuis la première, comptée en
    /// intervalles habituels (voir `InformationsVideo::instants`) ; les dates en seconde s'en déduisent avec
    /// `delta_t`, qui tient compte d'un ralenti
    std::optional<std::vector<double>> instants;
    /// la cadence de la vidéo est variable (images perdues, ralentis) : elle est signalée à l'élève
    /// (des dates presque régulières sont gardées sans le dire)
    bool cadence_irreguliere = false;
    /// position de l'origine du repère sur l'image
    Vecteur origine;
    Echelle echelle;
    /// sens de l'axe des abscisses (±1)
    int sens_x = 1;
    /// sens de l'axe des ordonnées (±1)
    int sens_y = 1;

    /// Crée les structures de données quand on en connaît la taille ; efface les pointages existants.
    void alloue_mesures(int n_suivis, double delta_t, int n_images);
    int nombre_objets() const { return m_nombre_objets; }
    /// nombre d'images (pointées ou non)
    int nombre_images() const { return m_nombre_images; }

    /// date de l'image d'index `index` (à partir de 0), depuis la première image de la vidéo
    double date(int index) const;
    /// vrai si les images ne sont pas régulièrement espacées dans le temps
    bool cadence_variable() const { return cadence_irreguliere; }
    /// date de l'image d'index `index` (à partir de 0) depuis la première image pointée : c'est la
    /// date zéro des tableaux et des exports
    double date_relative(int index) const;
    /// l'unité des positions : le mètre si l'échelle est définie, sinon le pixel
    QString unite() const;

    /// Index à afficher dans le tableau, y compris les images où tous les mobiles ont été perdus.
    std::vector<int> images_mesurees() const;
    /// nombre d'images où au moins un objet est pointé
    int nb_images_pointees() const;

    struct LigneExport {
        /// index de l'image (à partir de 0)
        int index;
        /// date depuis la première image pointée
        double t;
        /// position de chaque objet dans le repère choisi
        std::vector<Position> positions;
    };
    /// Les images de la première à la dernière mesure, avec les positions de chaque objet dans le repère
    /// choisi (en mètre, ou en pixel si l'échelle n'est pas définie).
    std::vector<LigneExport> lignes_export() const;

    /// Place du prochain clic d'une série manuelle interrompue. Ces deux numéros commencent à 1,
    /// comme dans la fenêtre et le fichier. Pointage utilise un index d'image à partir de 0.
    struct ProchainClic {
        int image;
        int objet;
        bool operator==(const ProchainClic &) const = default;
    };

    using Cellule = std::pair<int, int>; // index d’image (base 0), numéro d’objet (base 1)
    enum class Saisie { Manuelle, Assistee, AssisteeCorrigee, Automatique, AbsenceConfirmee, AVerifier, Perdue };
    static bool absence_signalee(Saisie s) { return s == Saisie::Perdue || s == Saisie::AbsenceConfirmee; }
    static bool est_assistee(Saisie s) { return s == Saisie::Assistee || s == Saisie::AssisteeCorrigee; }
    static bool est_reference(Saisie s) { return s == Saisie::Manuelle || s == Saisie::AssisteeCorrigee; }
    Saisie saisie(int obj, int index) const;
    const std::map<Cellule, Saisie> &provenances() const { return m_provenances; }
    void marque_saisie(int obj, int index, Saisie provenance);
    using ExempleVisee = std::pair<int, Vecteur>; // index de l'image et position pointée
    /// Au plus cinq exemples de l'élève répartis entre le premier et le dernier, avant l'image demandée.
    std::vector<ExempleVisee> exemples_visee(int obj, int avant) const;

    /// enregistre la position d'un objet (à partir de 1) sur une image (index à partir de 0)
    void memorise_point(int obj, Vecteur position, int index, Saisie provenance = Saisie::Manuelle, bool nouvelle_action = false);
    void memorise_absence(int obj, int index);
    void confirme_suivi(int obj, int index);
    /// position de l'objet `obj` (à partir de 1) sur l'image d'index `index` (à partir de 0), en pixels
    Position point_de_l_objet(int obj, int index) const;
    /// les positions de tous les objets sur une image
    const std::vector<Position> &ligne(int index) const;

    /// Lignes comportant une position, classées par index d’image. Une absence seule est
    /// consignée dans provenances(). Les deux listes utilisent les mêmes index.
    const auto &positions_par_image() const { return m_positions; }

    /// faux si aucun objet n'est pointé sur aucune image
    bool a_des_pointages() const;
    /// Première image de mesure (à partir de 1), absences du suivi comprises.
    std::optional<int> image_initiale() const;
    /// Dernière image de mesure, absences du suivi comprises.
    std::optional<int> image_finale() const;

    void supprime_points(const std::vector<Cellule> &cellules);
    /// Annule une action et renvoie le numéro de son image. Si l'élève était au milieu d'une
    /// série, conserve également la place du prochain clic pour « Rétablir ».
    std::optional<int> annule_mesure(std::optional<ProchainClic> en_cours = {});
    /// Remet les mesures en place. Une série interrompue reprend sur le même objet.
    std::optional<ProchainClic> retablit_mesure();
    bool annulation_possible() const { return !m_actions.empty(); }
    bool retablissement_possible() const { return !m_defaits.empty(); }
    bool derniere_suppression() const { return !m_actions.empty() && m_actions.back().suppression; }

    /// Données des fichiers de projet : une ligne par image, de la première à la dernière image
    /// pointée. Un objet non pointé au milieu d'une ligne s'écrit « nan » (deux champs), pour que les
    /// colonnes restent alignées ; les objets manquants en fin de ligne sont omis.
    /// @param sep séparateur de champ
    /// @param en_metre vrai pour des positions en mètre, sinon en pixel
    /// @param debut numéro de la première image pointée : la date zéro
    /// @param origine origine du repère
    QString mesures_en_csv(const QString &sep, bool en_metre, int debut, Vecteur origine, int chiffres = 6) const;

    /// Position dans le repère de la séance, en mètres après étalonnage ou en pixels avant.
    Vecteur position_dans_repere(Vecteur p) const;
    /// Applique l’échelle et le sens des axes à une vitesse ou une accélération.
    /// Déplacer l’origine change les positions, mais conserve ces deux vecteurs.
    Vecteur vecteur_dans_repere(Vecteur v) const;
    struct Cinematique {
        Vecteur vitesse, acceleration;
    };
    /// Dérivées de la parabole passant par trois positions consécutives aux dates réelles,
    /// évaluées à la date de l’image centrale (en px/s et px/s²).
    std::optional<Cinematique> cinematique_px(int obj, int index) const;
    Position vitesse_px(int obj, int index) const;

    using FlechesVitesse = std::vector<std::vector<std::pair<Vecteur, Vecteur>>>;
    /// Vecteurs vitesse affichables : origine sur un point de la trajectoire, direction et norme
    /// d'après les points précédent et suivant ; il faut au moins trois pointages.
    /// @param echelle_vitesse nombre de pixels pour 1 m/s
    /// @return pour chaque objet, une liste de couples (origine, extrémité)
    FlechesVitesse fleches_vitesse(double echelle_vitesse) const;

    /// Rejoue des pointages issus d'un fichier de projet, sur un pointage fraîchement alloué.
    /// Les valeurs non finies (« nan », champ vide) sont ignorées.
    /// @param lignes des lignes [t, x1, y1, ..., xn, yn], en mètres ou en pixels sans étalon
    /// @param premiere_image_pointee la toute première image pointée (≥ 1)
    /// @param en_cours série à reprendre en dernier pour que « Annuler » retrouve le dernier geste
    void recharge_mesures(const std::vector<std::vector<double>> &lignes, int premiere_image_pointee,
                          std::optional<ProchainClic> en_cours = {});

    /// Les pointages vus dans le référentiel d'un objet suivi : cet objet reste immobile au point
    /// `centre`, qui devient l'origine. Les images où l'objet de référence n'est pas pointé sont
    /// ignorées.
    Pointage dans_referentiel(int obj_ref, Vecteur centre) const;

private:
    int m_nombre_images = 0;
    /// Une ligne existe dès qu'au moins un objet a été pointé sur cette image.
    /// Un mobile perdu peut figurer dans m_provenances sans avoir de position.
    std::map<int, std::vector<Position>> m_positions;
    std::vector<Position> m_ligne_vide{std::nullopt};
    /// nombre d'objets suivis ; ils sont désignés par 1, 2, ...
    int m_nombre_objets = 1;
    std::map<Cellule, Saisie> m_provenances;
    // Index trié, construit lorsqu'un objet utilise la visée pour la première fois.
    mutable std::vector<std::optional<std::vector<int>>> m_images_reference;
    struct MesureMemorisee {
        int index_image;
        int numero_objet;
        Position position;
        Saisie saisie;
    };
    struct Modification {
        bool suppression = false;
        std::optional<ProchainClic> en_cours;
        std::vector<MesureMemorisee> cellules;
    };
    std::vector<Modification> m_actions, m_defaits;
    void enregistre_position(int obj, Position position, int index, Saisie provenance, bool nouvelle_action);
    Position remplace_position(int objet, int index, Position position);
    void echange(Modification &changement);
};
