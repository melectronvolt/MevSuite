#include "player.h"
#include "interpolation.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

namespace {

/// Durée des fondus (secondes) : pas de claquement au départ, à la pause, ni aux bords de l'extrait.
constexpr double FADE = 0.005;
/// Délai (millisecondes) au bout duquel une sortie qui s'éteint en fondu est fermée, même si elle
/// ne réclame plus de son.
constexpr qint64 CLOSE_AFTER = 250;

// ------------------------------------------------------------------ sortie audio de l'ordinateur

/// Source du son pour Qt : chaque lecture demandée par la sortie audio est remplie par `Playback`,
/// puis convertie au format de la sortie.
class PlaybackDevice : public QIODevice {
public:
    PlaybackDevice(Playback playback, const QAudioFormat &format)
        : m_playback(std::move(playback))
        , m_format(format)
    {
    }

    bool isSequential() const override { return true; }
    // Le son ne s'épuise pas : après la fin de la plage, la sortie reçoit du silence jusqu'à l'arrêt.
    qint64 bytesAvailable() const override { return (qint64(1) << 30) + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const int bytesPerFrame = m_format.bytesPerFrame();
        const int channels = m_format.channelCount();
        const qint64 frames = bytesPerFrame > 0 ? maxSize / bytesPerFrame : 0;
        if (frames <= 0 || channels <= 0)
            return 0;
        m_buffer.resize(size_t(frames) * size_t(channels));
        m_playback.remplit(m_buffer.data(), size_t(frames), channels);
        // QIODevice fournit des octets, sans garantie d'alignement sur un entier.
        const auto ecritEntiers = [&](auto convertit) {
            char *destination = data;
            for (float amplitude : m_buffer) {
                const auto entier = convertit(amplitude);
                std::memcpy(destination, &entier, sizeof entier);
                destination += sizeof entier;
            }
        };
        switch (m_format.sampleFormat()) {
        case QAudioFormat::Float: std::memcpy(data, m_buffer.data(), m_buffer.size() * sizeof(float)); break;
        case QAudioFormat::Int16:
            ecritEntiers([](float v) { return qint16(std::lround(v * 32767.0f)); });
            break;
        case QAudioFormat::Int32:
            ecritEntiers([](float v) { return qint32(std::llround(v * 2147483647.0)); });
            break;
        case QAudioFormat::UInt8:
            ecritEntiers([](float v) { return quint8(std::lround(128.0f + v * 127.0f)); });
            break;
        default: return 0;
        }
        return frames * bytesPerFrame;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    Playback m_playback;
    QAudioFormat m_format;
    std::vector<float> m_buffer;
};

AudioFailure echec_audio(QtAudio::Error error)
{
    switch (error) {
    case QtAudio::OpenError: return AudioFailure::Open;
    case QtAudio::IOError: return AudioFailure::InputOutput;
    case QtAudio::FatalError: return AudioFailure::Fatal;
    default: return AudioFailure::Other;
    }
}

/// Format de la sortie : la fréquence du son si la sortie l'accepte (pas de rééchantillonnage), en
/// flottants si possible ; sinon, le format préféré de la sortie.
QAudioFormat format_sortie(const QAudioDevice &device, double rate)
{
    QAudioFormat preferred = device.preferredFormat();
    if (preferred.channelCount() <= 0)
        preferred.setChannelCount(2);
    if (preferred.sampleFormat() == QAudioFormat::Unknown)
        preferred.setSampleFormat(QAudioFormat::Float);
    const int wanted = rate > 0 && rate <= std::numeric_limits<int>::max()
        ? int(std::round(rate)) : preferred.sampleRate();
    for (int sampleRate : {wanted, preferred.sampleRate()}) {
        if (sampleRate < device.minimumSampleRate() || sampleRate > device.maximumSampleRate())
            continue;
        for (QAudioFormat::SampleFormat sampleFormat : {QAudioFormat::Float, preferred.sampleFormat()}) {
            QAudioFormat f = preferred;
            f.setSampleRate(sampleRate);
            f.setSampleFormat(sampleFormat);
            if (device.isFormatSupported(f))
                return f;
        }
    }
    return preferred;
}

class DeviceOutput : public AudioOutput {
public:
    DeviceOutput(const QAudioDevice &device, const QAudioFormat &format, Playback playback)
        : m_device(std::make_unique<PlaybackDevice>(std::move(playback), format))
        , m_sink(std::make_unique<QAudioSink>(device, format))
    {
        m_device->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        m_sink->start(m_device.get());
    }

    ~DeviceOutput() override
    {
        m_sink->stop();
        m_sink.reset();
    }

    /// Erreur à l'ouverture, ou pendant la lecture.
    std::optional<AudioFailure> erreur() const override
    {
        const QtAudio::Error e = m_sink->error();
        if (e == QtAudio::NoError)
            return std::nullopt;
        return echec_audio(e);
    }

    double duree_jouee() const override { return double(m_sink->processedUSecs()) / 1e6; }

private:
    std::unique_ptr<PlaybackDevice> m_device;
    std::unique_ptr<QAudioSink> m_sink;
};

// ------------------------------------------------------------------ sortie simulée (essais)

/// Sortie audio simulée des essais : un fil consomme le son en temps réel, à 48 kHz, par blocs de
/// 10 ms.
class SimulatedOutput : public AudioOutput {
public:
    static constexpr double RATE = 48000.0;
    static constexpr size_t BLOCK = 480;

    explicit SimulatedOutput(Playback playback)
        : m_playback(std::move(playback))
    {
        m_clock.start();
        m_thread = std::thread([this] {
            std::vector<float> block(2 * BLOCK);
            auto next = std::chrono::steady_clock::now();
            while (!m_closed) {
                m_playback.remplit(block.data(), BLOCK, 2);
                m_written += BLOCK;
                next += std::chrono::milliseconds(10);
                std::this_thread::sleep_until(next);
            }
        });
    }

    ~SimulatedOutput() override
    {
        m_closed = true;
        m_thread.join();
    }

    double duree_jouee() const override
    {
        return std::min(double(m_clock.nsecsElapsed()) / 1e9, double(m_written.load()) / RATE);
    }

private:
    Playback m_playback;
    QElapsedTimer m_clock;
    std::atomic<size_t> m_written = 0;
    std::atomic<bool> m_closed = false;
    std::thread m_thread;
};

} // namespace

// ------------------------------------------------------------------ Playback

Playback::Playback(Samples samples, double rate, double from, size_t end, double outputRate,
                   std::shared_ptr<PlaybackShared> shared)
    : m_samples(std::move(samples))
    , m_first(size_t(std::clamp(from, 0.0, double(m_samples->size()))))
    , m_end(std::min(end, m_samples->size()))
    , m_position(std::clamp(from, 0.0, double(m_end)))
    , m_step(rate / outputRate)
    , m_bande(m_step > 1.0 ? 0.94 / m_step : 1.0)
    , m_outputRate(outputRate)
    , m_fade(std::max(FADE * outputRate, 1.0))
    , m_shared(std::move(shared))
{
}

void Playback::remplit(float *out, size_t frames, int channels)
{
    if (!m_fading && m_shared->stop)
        m_fading = m_fade;
    channels = std::max(channels, 1);
    for (size_t f = 0; f < frames; ++f) {
        const float v = echantillon_suivant();
        std::fill(out + f * size_t(channels), out + (f + 1) * size_t(channels), v);
    }
    if (m_position >= double(m_end) || (m_fading && *m_fading <= 0.0))
        m_shared->done = true;
}

float Playback::echantillon_suivant()
{
    const double end = double(m_end);
    if (m_position >= end)
        return 0.0f;
    // fondus : à l'entrée, avant la fin de la plage, et après une pause ou un arrêt
    double gain = std::min({m_written / m_fade, (end - m_position) / m_step / m_fade, 1.0});
    if (m_fading) {
        if (*m_fading <= 0.0)
            return 0.0f;
        gain = std::min(gain, *m_fading / m_fade);
        *m_fading -= 1.0;
    }
    const auto i = size_t(m_position);
    double valeur;
    if (m_bande == 1.0 && m_position == double(i)) {
        valeur = (*m_samples)[i];
    } else {
        // Le filtre s'arrête aux bornes de l'extrait, même à la reprise après une pause.
        // Les 6 % de marge laissent sa transition sous la fréquence de Nyquist de la sortie.
        const auto extrait = std::span(*m_samples).subspan(m_first, m_end - m_first);
        valeur = echantillon_filtre(extrait, m_position - double(m_first), m_bande);
    }
    m_position += m_step;
    m_written += 1.0;
    return float(std::clamp(gain * valeur, -1.0, 1.0));
}

// ------------------------------------------------------------------ Player

Player::Player(bool simulated)
    : m_simulated(simulated)
{
}

Player::~Player() = default;

void Player::simule(bool simulated)
{
    arrete();
    m_simulated = simulated;
}

std::optional<PlayError> Player::joue(const Clip &clip, const Samples &samples, double rate,
                                      const std::optional<QAudioDevice> &sortie)
{
    if (m_state == State::Playing && m_clip == clip)
        return std::nullopt;
    const bool reprise = en_pause() && m_clip == clip && m_pausedAt < double(clip.last);
    const double from = reprise ? m_pausedAt : double(clip.first);
    if (!reprise)
        arrete();
    if (clip.est_vide())
        return std::nullopt;
    auto shared = std::make_shared<PlaybackShared>();
    std::unique_ptr<AudioOutput> device;
    if (m_simulated) {
        device = std::make_unique<SimulatedOutput>(
            Playback(samples, rate, from, clip.last, SimulatedOutput::RATE, shared));
    } else {
        const QAudioDevice output = sortie ? *sortie : QMediaDevices::defaultAudioOutput();
        if (output.isNull())
            return PlayError{true, {}};
        const QAudioFormat format = format_sortie(output, rate);
        auto opened = std::make_unique<DeviceOutput>(
            output, format, Playback(samples, rate, from, clip.last, format.sampleRate(), shared));
        if (const auto e = opened->erreur())
            return PlayError{false, *e};
        device = std::move(opened);
    }
    m_state = State::Playing;
    m_clip = clip;
    m_rate = rate;
    m_from = from;
    m_output = {std::move(device), std::move(shared)};
    return std::nullopt;
}

void Player::pause()
{
    if (!en_lecture())
        return;
    const auto at = position();
    if (!at)
        return;
    m_state = State::Paused;
    m_pausedAt = *at;
    ferme(std::move(m_output));
}

void Player::arrete()
{
    if (m_state == State::Playing)
        ferme(std::move(m_output));
    m_state = State::Stopped;
}

void Player::ferme(Output output)
{
    if (!output.device)
        return;
    output.shared->stop = true;
    QElapsedTimer since;
    since.start();
    m_closing.emplace_back(std::move(output), since);
}

std::optional<AudioFailure> Player::met_a_jour()
{
    std::erase_if(m_closing, [](const auto &c) { return c.first.shared->done || c.second.elapsed() >= CLOSE_AFTER; });
    if (!en_lecture())
        return std::nullopt;
    const auto erreur = m_output.device->erreur();
    const auto positionActuelle = position();
    const bool finEntendue = positionActuelle && *positionActuelle >= double(m_clip.last);
    if (erreur || (m_output.shared->done && finEntendue)) {
        // Le pilote peut signaler la perte de la sortie avant audioOutputsChanged.
        m_state = erreur && positionActuelle && !finEntendue ? State::Paused : State::Stopped;
        if (m_state == State::Paused)
            m_pausedAt = *positionActuelle;
        m_output = {};
    }
    return erreur;
}

std::optional<double> Player::position() const
{
    switch (m_state) {
    case State::Stopped: return std::nullopt;
    case State::Paused: return m_pausedAt;
    case State::Playing: break;
    }
    const double heard = m_from + m_output.device->duree_jouee() * m_rate;
    return std::clamp(heard, m_from, std::max(m_from, double(m_clip.last)));
}

const Clip *Player::extrait_en_lecture() const
{
    return m_state == State::Stopped ? nullptr : &m_clip;
}
