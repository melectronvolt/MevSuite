// Lecture des images d'une vidéo, une par une.
//
// On pilote les programmes `ffprobe` (pour les métadonnées) et `ffmpeg` (pour décoder les images,
// envoyées au format PPM dans un tube). Un processus ffmpeg reste ouvert tant qu'on lit les images
// dans l'ordre ; un saut en arrière ou trop loin en avant relance ffmpeg à la bonne date. Les images
// décodées sont gardées dans un cache.
//
// Si les trames sont régulièrement espacées, l'image n° i est celle qui est affichée à la date
// (i - 1) / fps : les numéros d'images correspondent au temps. Si la cadence est variable (vidéos de
// téléphone, images perdues), chaque trame est une image, et les calculs utilisent sa date réelle
// (voir `VideoInfo::instants`). Chaque trame décodée est identifiée par sa date, que le filtre
// `showinfo` de ffmpeg écrit sur sa sortie d'erreur.
//
// Les vidéos HDR des téléphones (HLG, PQ) sont décodées sur 16 bits, puis converties en couleurs
// ordinaires (SDR) par `ConversionHdr`.
//
// Seul le premier flux vidéo « véritable » est lu (spécificateur `V` de ffmpeg) : les miniatures et
// pochettes que certains fichiers rangent comme flux vidéo sont ignorées.

#pragma once

#include "hdr.h"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QThread>

#include <atomic>
#include <cstddef>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class QProcess;

struct VideoInfo {
    /// images par seconde (cadence habituelle si elle est variable)
    double fps = 25.0;
    /// nombre d'images
    int nb_images = 0;
    /// dimensions des images décodées, sans rotation demandée par l'utilisateur
    int largeur = 0;
    int hauteur = 0;
    /// vrai si les trames ne sont pas régulièrement espacées dans le temps (images perdues, cadence
    /// qui change avec la lumière…), ce qui est signalé
    bool cadence_variable = false;
    /// vrai si les dates réelles des trames servent : cadence variable, ou presque régulière
    /// (téléphones : 40 puis 44 ms…). Chaque trame est alors une image, à sa date réelle.
    bool dates_reelles = false;
    /// cadence de prise de vue indiquée par le fichier, quand elle diffère de la cadence de lecture
    /// (vidéos au ralenti ou en accéléré des téléphones Android)
    std::optional<double> fps_capture;
    /// fonction de transfert d'une vidéo HDR
    std::optional<Transfert> hdr;
    /// nom du codec selon ffprobe (« h264 », « hevc »...)
    QString codec;
    /// dates de présentation des trames du flux, en seconde, dans l'ordre ; vide si le conteneur ne
    /// les donne pas (trames supposées régulières)
    std::vector<double> dates;
    /// date de la première trame décodée, en seconde
    double t0 = 0.0;

    /// la trame (à partir de 0) affichée pour l'image d'index `i` (à partir de 0) : la dernière dont
    /// la date ne dépasse pas celle de l'image
    int trame(int i) const;
    /// la trame (à partir de 0) dont la date est la plus proche de `t`
    int trame_de_date(double t) const;
    /// Avec les dates réelles, la date de chaque image depuis la première, comptée en intervalles
    /// habituels (1 / fps) : 0 ; 1,02 ; 3,01... Sinon (images régulières), rien.
    std::optional<std::vector<double>> instants() const;
};

/// Lecture bloquante d'un flux d'octets : copie au plus `n` octets dans `dest` et renvoie leur
/// nombre ; 0 (ou moins) à la fin du flux.
using Lecture = std::function<qint64(char *dest, qint64 n)>;
/// La ligne suivante d'un journal ; rien à la fin du journal.
using LectureLigne = std::function<std::optional<QByteArray>()>;

/// Lit une image au format PPM binaire (P6) depuis un flux. Une image sur 16 bits est convertie par
/// `hdr`, ou à défaut réduite à 8 bits.
std::expected<QImage, QString> lit_ppm(const Lecture &lecture, const ConversionHdr *hdr);
/// lit la date de la prochaine trame dans le journal du filtre showinfo
std::optional<double> lit_date(const LectureLigne &ligne);
/// un nombre rationnel de ffprobe (« 30000/1001 »), positif et fini
std::optional<double> rationnel(const QString &s);
/// Si les trames ne sont pas régulièrement espacées (images perdues, cadence qui change avec la
/// lumière...), c'est-à-dire si un intervalle au moins s'écarte de l'intervalle médian, renvoie
/// l'intervalle habituel entre deux trames.
std::optional<double> cadence_variable(const std::vector<double> &dates);
/// Vrai si les dates des trames s'écartent d'une cadence régulière de plus que leurs arrondis :
/// 2 % d'un intervalle, plus la précision `pas` des dates du conteneur (sa base de temps, en
/// seconde ; une milliseconde pour les MKV et les WebM).
bool cadence_presque_reguliere(const std::vector<double> &dates, double pas);
/// nom lisible d'un codec, d'après le nom donné par ffprobe
QString nom_codec(const QString &codec);
/// applique une rotation dans le sens horaire (0, 90, 180 ou 270 degrés)
QImage tourne(const QImage &img, int rotation);

class VideoReader {
public:
    /// Ouvre une vidéo ; échoue si ffprobe ne la reconnaît pas ou si ffmpeg ne sait pas en décoder la
    /// première image. `annule`, regardé pendant les attentes de ffprobe et de ffmpeg, interrompt
    /// l'ouverture (faite dans un autre fil que celui de la fenêtre).
    static std::expected<std::unique_ptr<VideoReader>, QString> ouvre(const QString &chemin,
                                                                       const std::atomic_bool *annule = nullptr);
    /// confie le décodage au fil `fil` : la vidéo, ouverte dans un autre fil, est lue dans celui-ci
    void confie_au_fil(QThread *fil);
    ~VideoReader();
    VideoReader(const VideoReader &) = delete;
    VideoReader &operator=(const VideoReader &) = delete;

    VideoInfo info;

    int rotation() const { return m_rotation; }
    void set_rotation(int rotation);
    /// dimensions des images, compte tenu de la rotation
    QSize dimensions() const;

    /// Récupère une image de la vidéo (numéro à partir de 1), tournée selon la rotation demandée.
    std::expected<QImage, QString> image(int index);

    /// pour les tests : oublie les images décodées et arrête ffmpeg, pour un accès direct
    void oublie_cache_et_flux();
    /// pour les tests : lit une vidéo HDR sans la convertir
    void sans_conversion_hdr();

private:
    struct Flux;

    explicit VideoReader(const QString &chemin, VideoInfo info);
    void vide_cache();
    void met_en_cache(int trame, const QImage &img);
    /// lance ffmpeg pour décoder les trames à partir de la trame `i`
    std::expected<std::unique_ptr<Flux>, QString> lance_ffmpeg(int i) const;

    QString m_chemin;
    /// rotation demandée : 0, 90, 180 ou 270 degrés dans le sens horaire
    int m_rotation = 0;
    /// conversion des images d'une vidéo HDR en SDR
    std::shared_ptr<const ConversionHdr> m_hdr;
    std::unique_ptr<Flux> m_flux;
    std::unordered_map<int, QImage> m_cache;
    std::deque<int> m_ordre_cache;
    qsizetype m_taille_cache = 0;
};
