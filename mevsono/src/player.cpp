#include "player.h"

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

using namespace Qt::StringLiterals;

namespace {

/// Durée des fondus (secondes) : pas de claquement au départ, à la pause, ni aux bords de l'extrait.
constexpr double FADE = 0.005;
/// Délai (millisecondes) au bout duquel une sortie qui s'éteint en fondu est fermée, même si elle
/// ne réclame plus de son.
constexpr qint64 CLOSE_AFTER = 250;

void setError(PlaybackShared &shared, const QString &message)
{
    {
        const std::lock_guard lock(shared.errorMutex);
        if (!shared.error)
            shared.error = message;
    }
    shared.done = true;
}

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
        m_playback.fill(m_buffer.data(), size_t(frames), channels);
        const auto convert = [&](auto *out, auto scale) {
            for (size_t i = 0; i < m_buffer.size(); ++i)
                out[i] = scale(m_buffer[i]);
        };
        switch (m_format.sampleFormat()) {
        case QAudioFormat::Float: std::memcpy(data, m_buffer.data(), m_buffer.size() * sizeof(float)); break;
        case QAudioFormat::Int16:
            convert(reinterpret_cast<qint16 *>(data), [](float v) { return qint16(std::lround(v * 32767.0f)); });
            break;
        case QAudioFormat::Int32:
            convert(reinterpret_cast<qint32 *>(data), [](float v) { return qint32(std::llround(v * 2147483647.0)); });
            break;
        case QAudioFormat::UInt8:
            convert(reinterpret_cast<quint8 *>(data), [](float v) { return quint8(std::lround(128.0f + v * 127.0f)); });
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

QString describe(QtAudio::Error error)
{
    switch (error) {
    case QtAudio::OpenError: return u"audio output: open error"_s;
    case QtAudio::IOError: return u"audio output: I/O error"_s;
    case QtAudio::FatalError: return u"audio output: fatal error"_s;
    default: return u"audio output error"_s;
    }
}

/// Format de la sortie : la fréquence du son si la sortie l'accepte (pas de rééchantillonnage), en
/// flottants si possible ; sinon, le format préféré de la sortie.
QAudioFormat outputFormat(const QAudioDevice &device, double rate)
{
    QAudioFormat preferred = device.preferredFormat();
    if (preferred.channelCount() <= 0)
        preferred.setChannelCount(2);
    if (preferred.sampleFormat() == QAudioFormat::Unknown)
        preferred.setSampleFormat(QAudioFormat::Float);
    const int wanted = int(std::lround(rate));
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
    DeviceOutput(const QAudioDevice &device, const QAudioFormat &format, Playback playback,
                 std::shared_ptr<PlaybackShared> shared)
        : m_device(std::make_unique<PlaybackDevice>(std::move(playback), format))
        , m_sink(std::make_unique<QAudioSink>(device, format))
        , m_shared(std::move(shared))
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
    std::optional<QString> error() const
    {
        const QtAudio::Error e = m_sink->error();
        if (e == QtAudio::NoError)
            return std::nullopt;
        return describe(e);
    }

    double played() const override { return double(m_sink->processedUSecs()) / 1e6; }

    void poll() override
    {
        if (const auto e = error())
            setError(*m_shared, *e);
    }

private:
    std::unique_ptr<PlaybackDevice> m_device;
    std::unique_ptr<QAudioSink> m_sink;
    std::shared_ptr<PlaybackShared> m_shared;
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
                m_playback.fill(block.data(), BLOCK, 2);
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

    double played() const override
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
    , m_end(std::min(end, m_samples->size()))
    , m_position(from)
    , m_step(rate / outputRate)
    , m_outputRate(outputRate)
    , m_fade(std::max(FADE * outputRate, 1.0))
    , m_shared(std::move(shared))
{
}

void Playback::fill(float *out, size_t frames, int channels)
{
    if (!m_fading && m_shared->stop)
        m_fading = m_fade;
    channels = std::max(channels, 1);
    for (size_t f = 0; f < frames; ++f) {
        const float v = nextSample();
        std::fill(out + f * size_t(channels), out + (f + 1) * size_t(channels), v);
    }
    if (m_position >= double(m_end) || (m_fading && *m_fading <= 0.0))
        m_shared->done = true;
}

float Playback::nextSample()
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
    const auto frac = float(m_position - double(i));
    const std::vector<float> &s = *m_samples;
    const float a = s[i];
    const float b = s[std::min(i + 1, m_end - 1)];
    m_position += m_step;
    m_written += 1.0;
    return std::clamp(float(gain) * (a + (b - a) * frac), -1.0f, 1.0f);
}

// ------------------------------------------------------------------ Player

Player::Player(bool simulated)
    : m_simulated(simulated)
{
}

Player::~Player() = default;

void Player::setSimulated(bool simulated)
{
    stop();
    m_simulated = simulated;
}

std::optional<PlayError> Player::play(const Clip &clip, const Samples &samples, double rate)
{
    if (m_state == State::Playing && m_clip == clip)
        return std::nullopt;
    const double from = m_state == State::Paused && m_clip == clip && m_pausedAt < double(clip.last)
                            ? m_pausedAt
                            : double(clip.first);
    stop();
    if (clip.empty())
        return std::nullopt;
    auto shared = std::make_shared<PlaybackShared>();
    std::unique_ptr<AudioOutput> device;
    if (m_simulated) {
        device = std::make_unique<SimulatedOutput>(
            Playback(samples, rate, from, clip.last, SimulatedOutput::RATE, shared));
    } else {
        const QAudioDevice output = QMediaDevices::defaultAudioOutput();
        if (output.isNull())
            return PlayError{true, {}};
        const QAudioFormat format = outputFormat(output, rate);
        auto opened = std::make_unique<DeviceOutput>(
            output, format, Playback(samples, rate, from, clip.last, format.sampleRate(), shared), shared);
        if (const auto e = opened->error())
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
    if (!playing())
        return;
    const auto at = position();
    if (!at)
        return;
    m_state = State::Paused;
    m_pausedAt = *at;
    close(std::move(m_output));
}

void Player::stop()
{
    if (m_state == State::Playing)
        close(std::move(m_output));
    m_state = State::Stopped;
}

void Player::close(Output output)
{
    if (!output.device)
        return;
    output.shared->stop = true;
    QElapsedTimer since;
    since.start();
    m_closing.emplace_back(std::move(output), since);
}

std::optional<QString> Player::update()
{
    std::erase_if(m_closing, [](const auto &c) { return c.first.shared->done || c.second.elapsed() >= CLOSE_AFTER; });
    if (!playing())
        return std::nullopt;
    m_output.device->poll();
    std::optional<QString> error;
    {
        const std::lock_guard lock(m_output.shared->errorMutex);
        error = std::exchange(m_output.shared->error, std::nullopt);
    }
    const auto at = position();
    const bool finished = m_output.shared->done && at && *at >= double(m_clip.last);
    if (error || finished) {
        m_state = State::Stopped;
        m_output = {};
    }
    return error;
}

std::optional<double> Player::position() const
{
    switch (m_state) {
    case State::Stopped: return std::nullopt;
    case State::Paused: return m_pausedAt;
    case State::Playing: break;
    }
    const double heard = m_from + m_output.device->played() * m_rate;
    return std::clamp(heard, m_from, std::max(m_from, double(m_clip.last)));
}

const Clip *Player::clip() const
{
    return m_state == State::Stopped ? nullptr : &m_clip;
}
