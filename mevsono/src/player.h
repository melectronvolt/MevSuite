// Écoute du son : tout le son, ou l'extrait entre A et B, sur la sortie audio de l'ordinateur
// (Qt Multimedia). Lecture, pause et arrêt ; la position de lecture, suivie en continu, est tracée
// sur le signal.

#pragma once

#include "audio.h"

#include <QElapsedTimer>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

/// Ce qui est joué : un son (son numéro), une de ses voies, et une plage `first..last` de ses
/// échantillons.
struct Clip {
    quint64 audio = 0;
    audio::Channel channel = audio::Channel::Mix;
    size_t first = 0;
    size_t last = 0;

    bool empty() const { return last <= first; }
    bool operator==(const Clip &) const = default;
};

/// Panne de la sortie audio (le texte affiché est traduit : `Tr::audioFailures`).
enum class AudioFailure { Open, InputOutput, Fatal, Other };

/// Partagé entre l'interface et le fil audio.
struct PlaybackShared {
    /// Pause ou arrêt demandé : le son s'éteint en fondu.
    std::atomic<bool> stop = false;
    /// Plus rien à jouer : fin de la plage atteinte, fondu de sortie terminé, ou erreur.
    std::atomic<bool> done = false;
    /// Erreur de la sortie audio pendant la lecture.
    std::mutex errorMutex;
    std::optional<AudioFailure> error;
};

/// Travail du fil audio : recopier la plage du signal sur toutes les voies de la sortie, à la
/// fréquence d'échantillonnage de la sortie (interpolation linéaire si elle diffère de celle du son).
class Playback {
public:
    Playback(Samples samples, double rate, double from, size_t end, double outputRate,
             std::shared_ptr<PlaybackShared> shared);

    /// Remplit `frames` trames de `channels` voies entrelacées.
    void fill(float *out, size_t frames, int channels);
    /// Position atteinte, en échantillons du son (non entière si les fréquences diffèrent).
    double position() const { return m_position; }
    double outputRate() const { return m_outputRate; }
    PlaybackShared &shared() { return *m_shared; }

private:
    float nextSample();

    Samples m_samples;
    size_t m_end;
    double m_position;
    /// Avance par échantillon de sortie : fréquence du son / fréquence de la sortie.
    double m_step;
    double m_outputRate;
    /// Durée des fondus, en échantillons de sortie.
    double m_fade;
    /// Échantillons de sortie écrits depuis le départ (fondu d'entrée).
    double m_written = 0.0;
    /// Échantillons de sortie restants du fondu de sortie, une fois la pause ou l'arrêt demandé.
    std::optional<double> m_fading;
    std::shared_ptr<PlaybackShared> m_shared;
};

/// Échec de l'ouverture de la sortie audio.
struct PlayError {
    /// Ni haut-parleurs ni casque (ou pas de serveur de son).
    bool noDevice = false;
    AudioFailure failure = AudioFailure::Other;
};

/// Sortie audio ouverte : la détruire coupe le son.
class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    /// Durée déjà jouée depuis le départ, en secondes.
    virtual double played() const = 0;
    /// Relève les changements d'état de la sortie (erreurs) ; appelé régulièrement par le lecteur.
    virtual void poll() {}
};

class Player {
public:
    /// `simulated` : sortie simulée, sans carte son (essais) ; le son est consommé en temps réel.
    explicit Player(bool simulated = false);
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    void setSimulated(bool simulated);

    /// Lit `clip` (échantillons `samples`, à `rate` Hz) : reprend où la pause l'a laissé, sinon part
    /// du début de sa plage.
    std::optional<PlayError> play(const Clip &clip, const Samples &samples, double rate);
    /// Met en pause : la prochaine lecture du même extrait reprendra ici.
    void pause();
    /// Arrête : la prochaine lecture partira du début.
    void stop();
    /// À appeler régulièrement : ferme les sorties éteintes, et s'arrête en fin de lecture. Renvoie
    /// l'erreur de la sortie audio, si elle s'est produite pendant la lecture.
    std::optional<AudioFailure> update();

    /// Position de lecture, en échantillons du son : celle qu'on entend, ou celle de la pause.
    std::optional<double> position() const;
    /// Ce qui est lu, ou en pause.
    const Clip *clip() const;
    bool playing() const { return m_state == State::Playing; }
    bool paused() const { return m_state == State::Paused; }
    /// Vrai tant que l'affichage doit suivre : lecture en cours, ou sortie qui s'éteint.
    bool busy() const { return playing() || !m_closing.empty(); }

private:
    struct Output {
        std::unique_ptr<AudioOutput> device;
        std::shared_ptr<PlaybackShared> shared;
    };

    void close(Output output);

    enum class State { Stopped, Playing, Paused };
    State m_state = State::Stopped;
    Clip m_clip;
    /// Lecture : fréquence d'échantillonnage du son et position de départ ; pause : position.
    double m_rate = 0.0;
    double m_from = 0.0;
    double m_pausedAt = 0.0;
    Output m_output;
    /// Sorties qui s'éteignent en fondu après une pause ou un arrêt, et l'instant de la demande.
    std::vector<std::pair<Output, QElapsedTimer>> m_closing;
    bool m_simulated = false;
};
