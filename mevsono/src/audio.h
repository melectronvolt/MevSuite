// Lecture des sons : fichiers audio (WAV, MP3, OGG, FLAC, M4A…) et pistes audio des vidéos (MP4,
// MOV, MKV, WebM), décodés dans le programme. Les fichiers WAV sont lus directement ; les autres
// formats par Qt Multimedia (FFmpeg). Pour un format qu'il ne lit pas, un ffmpeg installé sur le
// poste, ou placé dans le dossier `ffmpeg` à côté du programme, convertit d'abord la piste audio.
// Et des sons d'exemple, fabriqués par le programme.

#pragma once

#include "dsp.h"

#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace audio {

/// Durée maximale lue (au-delà, le reste du fichier est ignoré).
inline constexpr double MAX_SECONDS = 30.0 * 60.0;

/// Extensions proposées par la boîte de dialogue Ouvrir.
QStringList extensions();

/// Voie étudiée d'un son stéréo.
enum class Channel {
    /// Moyenne des voies.
    Mix,
    Left,
    Right,
};

inline constexpr std::array<Channel, 3> ALL_CHANNELS = {Channel::Mix, Channel::Left, Channel::Right};

QString channelCode(Channel c);
std::optional<Channel> channelFromCode(const QString &code);

/// Un son chargé : ses voies, et leur moyenne.
struct Audio {
    /// Nom affiché (nom du fichier, ou de l'exemple).
    QString name;
    double rate = 0.0;
    std::vector<Samples> channels;
    Samples mix;
    /// La lecture s'est arrêtée à `MAX_SECONDS`.
    bool truncated = false;
    /// Le décodage s'est arrêté sur une erreur (fichier abîmé) : seul le début du son est là.
    bool damaged = false;

    /// Voies de même longueur (les plus longues sont raccourcies) et leur moyenne.
    static Audio make(QString name, double rate, std::vector<std::vector<float>> channels, bool truncated);

    size_t len() const { return mix ? mix->size() : 0; }
    double duration() const { return double(len()) / rate; }
    /// Signal de la voie demandée (la moyenne pour un son mono).
    const Samples &signal(Channel channel) const;
    /// Plus grande valeur absolue des échantillons.
    float peak() const;
};

enum class LoadError {
    /// Format non reconnu, et pas de ffmpeg pour le convertir.
    Unsupported,
    /// Fichier sans piste audio.
    NoAudio,
    /// Autre erreur (fichier introuvable…), décrite par `message`.
    Other,
};

struct LoadResult {
    std::optional<Audio> audio;
    LoadError error = LoadError::Other;
    QString message;
};

/// Lit un fichier son ou vidéo. Peut être appelé depuis un fil de fond.
LoadResult load(const QString &path);

/// Écrit un fichier WAV 16 bits (mono ou stéréo) : essais, et exemples à écouter.
bool writeWav(const QString &path, int rate, const std::vector<std::vector<float>> &channels);

/// Sons d'exemple, fabriqués par le programme (sons de synthèse).
enum class Example {
    /// Diapason : son pur à 440 Hz, qui s'éteint lentement.
    TuningFork,
    /// Flûte : La 440 Hz, harmoniques décroissants.
    Flute,
    /// Tuyau d'orgue bouché : Do 262 Hz, harmoniques impairs.
    OrganPipe,
    /// Corde pincée (guitare) : La 110 Hz.
    String,
    /// Signal carré à 200 Hz.
    Square,
    /// Battements : 440 Hz et 444 Hz.
    Beats,
};

inline constexpr std::array<Example, 6> ALL_EXAMPLES = {Example::TuningFork, Example::Flute,  Example::OrganPipe,
                                                        Example::String,     Example::Square, Example::Beats};

/// Fréquence d'échantillonnage des exemples.
inline constexpr double RATE = 44100.0;

/// Son de l'exemple, mono à 44,1 kHz. `name` : nom affiché.
Audio generate(Example example, const QString &name);

} // namespace audio
