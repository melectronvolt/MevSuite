#include "audio.h"

#include "savefile.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <numbers>

using namespace Qt::StringLiterals;

namespace audio {

QStringList extensions()
{
    return {u"wav"_s, u"mp3"_s, u"ogg"_s, u"oga"_s, u"flac"_s, u"m4a"_s, u"aac"_s,  u"aif"_s,  u"aiff"_s,
            u"caf"_s, u"opus"_s, u"mp4"_s, u"mov"_s, u"m4v"_s, u"mkv"_s, u"webm"_s, u"avi"_s};
}

QString channelCode(Channel c)
{
    switch (c) {
    case Channel::Mix: return u"mix"_s;
    case Channel::Left: return u"left"_s;
    case Channel::Right: return u"right"_s;
    }
    return {};
}

std::optional<Channel> channelFromCode(const QString &code)
{
    for (Channel c : ALL_CHANNELS) {
        if (channelCode(c) == code)
            return c;
    }
    return std::nullopt;
}

Audio Audio::make(QString name, double rate, std::vector<std::vector<float>> channels, bool truncated)
{
    size_t len = channels.empty() ? 0 : channels.front().size();
    for (const auto &c : channels)
        len = std::min(len, c.size());
    Audio audio;
    audio.name = std::move(name);
    audio.rate = rate;
    audio.truncated = truncated;
    // un échantillon infini ou « NaN » (fichier flottant abîmé) fausserait tout le signal et le spectre
    for (auto &c : channels) {
        for (float &v : c) {
            if (!std::isfinite(v)) {
                v = 0.0f;
                audio.invalidSamples = true;
            }
        }
    }
    if (channels.size() == 1) {
        channels[0].resize(len);
        auto mono = std::make_shared<const std::vector<float>>(std::move(channels[0]));
        audio.mix = mono;
        audio.channels = {mono};
        return audio;
    }
    std::vector<float> mix(len, 0.0f);
    const float k = float(channels.size());
    for (size_t i = 0; i < len; ++i) {
        float sum = 0.0f;
        for (const auto &c : channels)
            sum += c[i];
        mix[i] = sum / k;
    }
    audio.mix = std::make_shared<const std::vector<float>>(std::move(mix));
    for (auto &c : channels) {
        c.resize(len);
        audio.channels.push_back(std::make_shared<const std::vector<float>>(std::move(c)));
    }
    return audio;
}

const Samples &Audio::signal(Channel channel) const
{
    if (channels.size() >= 2) {
        if (channel == Channel::Left)
            return channels[0];
        if (channel == Channel::Right)
            return channels[1];
    }
    return mix;
}

float Audio::peak() const
{
    float m = 0.0f;
    const auto scan = [&m](const Samples &s) {
        if (s) {
            for (float x : *s)
                m = std::max(m, std::abs(x));
        }
    };
    scan(mix);
    // un son mono partage ses échantillons avec la moyenne
    if (channels.size() >= 2) {
        for (const Samples &c : channels)
            scan(c);
    }
    return m;
}

namespace {

// ------------------------------------------------------------------ fichiers WAV

quint16 le16(const char *p)
{
    return quint16(quint8(p[0]) | (quint8(p[1]) << 8));
}

quint32 le32(const char *p)
{
    return quint32(quint8(p[0])) | (quint32(quint8(p[1])) << 8) | (quint32(quint8(p[2])) << 16)
        | (quint32(quint8(p[3])) << 24);
}

constexpr quint16 WAVE_PCM = 1;
constexpr quint16 WAVE_FLOAT = 3;
constexpr quint16 WAVE_EXTENSIBLE = 0xFFFE;

/// Valeur d'un échantillon entre −1 et 1, selon son codage.
using Decode = float (*)(const char *);

Decode decoder(quint16 tag, int bits)
{
    if (tag == WAVE_PCM) {
        switch (bits) {
        case 8: return [](const char *p) { return (float(quint8(p[0])) - 128.0f) / 128.0f; };
        case 16: return [](const char *p) { return float(qint16(le16(p))) / 32768.0f; };
        case 24:
            return [](const char *p) {
                // trois octets, placés dans les poids forts d'un entier de 32 bits
                const quint32 bitsValue = (quint32(quint8(p[0])) << 8) | (quint32(quint8(p[1])) << 16)
                    | (quint32(quint8(p[2])) << 24);
                return float(double(qint32(bitsValue)) / 2147483648.0);
            };
        case 32: return [](const char *p) { return float(double(qint32(le32(p))) / 2147483648.0); };
        }
    } else if (tag == WAVE_FLOAT) {
        switch (bits) {
        case 32:
            return [](const char *p) {
                const quint32 bitsValue = le32(p);
                float v;
                std::memcpy(&v, &bitsValue, sizeof v);
                return v;
            };
        case 64:
            return [](const char *p) {
                const quint64 bitsValue = quint64(le32(p)) | (quint64(le32(p + 4)) << 32);
                double v;
                std::memcpy(&v, &bitsValue, sizeof v);
                return float(v);
            };
        }
    }
    return nullptr;
}

/// Vrai si le fichier, de `from` à la fin, n'est qu'une suite de blocs RIFF (identifiant de quatre
/// caractères, taille qui mène au bloc suivant puis à la fin du fichier) : des métadonnées LIST ou
/// id3, et non des échantillons. Le fichier revient ensuite à `from`.
bool onlyChunks(QFile &file, qint64 from)
{
    const qint64 end = file.size();
    qint64 pos = from;
    bool ok = pos + 8 <= end;
    while (ok && pos + 8 <= end) {
        file.seek(pos);
        const QByteArray header = file.read(8);
        ok = header.size() == 8 && std::all_of(header.begin(), header.begin() + 4, [](char c) { return c >= 0x20 && c <= 0x7e; });
        const quint32 size = ok ? le32(header.constData() + 4) : 0;
        pos += 8 + qint64(size) + (size % 2);
    }
    file.seek(from);
    // l'octet de bourrage d'un dernier bloc de taille impaire manque parfois
    return ok && (pos == end || pos == end + 1);
}

/// Lit un fichier WAV (PCM 8, 16, 24 ou 32 bits, ou flottant). Vide si ce n'est pas un fichier WAV
/// que ce lecteur sait lire (le décodeur de Qt essaie alors).
std::optional<LoadResult> readWav(QFile &file, const QString &name)
{
    const QByteArray riff = file.read(12);
    if (riff.size() < 12 || !riff.startsWith("RIFF") || riff.mid(8, 4) != "WAVE")
        return std::nullopt;
    quint16 tag = 0;
    int channelCount = 0, bits = 0, blockAlign = 0;
    quint32 rate = 0;
    quint32 dataSize = 0;
    bool haveFormat = false;
    while (true) {
        const QByteArray header = file.read(8);
        if (header.size() < 8)
            return std::nullopt; // pas de données
        const QByteArray id = header.left(4);
        const quint32 size = le32(header.constData() + 4);
        if (id == "fmt ") {
            const QByteArray fmt = file.read(size);
            if (fmt.size() < 16)
                return std::nullopt;
            tag = le16(fmt.constData());
            channelCount = le16(fmt.constData() + 2);
            rate = le32(fmt.constData() + 4);
            blockAlign = le16(fmt.constData() + 12);
            bits = le16(fmt.constData() + 14);
            // WAVE_FORMAT_EXTENSIBLE : le vrai codage est au début du GUID du sous-format
            if (tag == WAVE_EXTENSIBLE && fmt.size() >= 26)
                tag = le16(fmt.constData() + 24);
            haveFormat = true;
            if (size % 2)
                file.read(1);
        } else if (id == "data") {
            if (!haveFormat)
                return std::nullopt;
            dataSize = size;
            break;
        } else {
            file.skip(qint64(size) + (size % 2));
        }
    }
    const int bytes = bits / 8;
    const Decode decode = decoder(tag, bits);
    if (!decode || channelCount < 1 || rate == 0 || bits % 8 || blockAlign < channelCount * bytes)
        return std::nullopt;

    // le bloc des données seulement : des métadonnées (LIST, id3…) suivent souvent le son. Taille
    // absente (fichier écrit en continu) : jusqu'à la fin du fichier, sauf si ce qui suit n'est que des
    // blocs de métadonnées (le bloc des données est alors vide). Taille plus grande que le fichier :
    // enregistrement coupé, signalé.
    const qint64 start = file.pos();
    qint64 available = file.size() - start;
    bool cut = false;
    if (dataSize == 0 && onlyChunks(file, start))
        return LoadResult{std::nullopt, LoadError::NoAudio, {}};
    // (0xFFFFFFFF, 0x7FFFFFFF : tailles provisoires d'un fichier écrit en continu)
    if (dataSize != 0 && dataSize < 0x7FFFFFFFu) {
        cut = qint64(dataSize) > available;
        available = std::min(available, qint64(dataSize));
    }
    const qint64 frames = available / blockAlign;
    const double maxFrames = std::ceil(MAX_SECONDS * rate);
    const bool truncated = double(frames) > maxFrames;
    const qint64 wanted = truncated ? qint64(maxFrames) : frames;
    std::vector<std::vector<float>> channels(static_cast<size_t>(channelCount));
    for (auto &c : channels)
        c.reserve(size_t(wanted));
    constexpr qint64 CHUNK = 65536;
    for (qint64 done = 0; done < wanted;) {
        const qint64 n = std::min(CHUNK, wanted - done);
        const QByteArray block = file.read(n * blockAlign);
        const qint64 got = block.size() / blockAlign;
        for (qint64 f = 0; f < got; ++f) {
            const char *frame = block.constData() + f * blockAlign;
            for (int c = 0; c < channelCount; ++c)
                channels[size_t(c)].push_back(decode(frame + c * bytes));
        }
        done += got;
        if (got < n)
            break;
    }
    if (channels[0].empty())
        return LoadResult{std::nullopt, LoadError::NoAudio, {}};
    Audio audio = Audio::make(name, double(rate), std::move(channels), truncated);
    audio.damaged = cut && !truncated;
    return LoadResult{std::move(audio), LoadError::Other, {}};
}

// ------------------------------------------------------------------ autres formats (Qt Multimedia)

/// Ajoute les échantillons d'un bloc décodé, voie par voie.
void append(const QAudioBuffer &buffer, std::vector<std::vector<float>> &channels, double &rate)
{
    const QAudioFormat format = buffer.format();
    const int k = std::max(format.channelCount(), 1);
    if (channels.empty()) {
        channels.resize(size_t(k));
        rate = format.sampleRate();
    }
    const qsizetype frames = buffer.frameCount();
    const int used = std::min(k, int(channels.size()));
    const auto copy = [&](auto data, auto convert) {
        for (qsizetype f = 0; f < frames; ++f) {
            for (int c = 0; c < used; ++c)
                channels[size_t(c)].push_back(convert(data[f * k + c]));
        }
    };
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        copy(buffer.constData<quint8>(), [](quint8 v) { return (float(v) - 128.0f) / 128.0f; });
        break;
    case QAudioFormat::Int16:
        copy(buffer.constData<qint16>(), [](qint16 v) { return float(v) / 32768.0f; });
        break;
    case QAudioFormat::Int32:
        copy(buffer.constData<qint32>(), [](qint32 v) { return float(double(v) / 2147483648.0); });
        break;
    case QAudioFormat::Float:
        copy(buffer.constData<float>(), [](float v) { return v; });
        break;
    default:
        break;
    }
}

/// Décode le son par Qt Multimedia (FFmpeg) : la première piste audio d'un fichier son ou vidéo.
LoadResult decodeWithQt(const QString &path, const QString &name, const std::atomic_bool *cancel)
{
    QAudioDecoder decoder;
    if (!decoder.isSupported())
        return {std::nullopt, LoadError::Unsupported, {}};
    std::vector<std::vector<float>> channels;
    double rate = 0.0;
    bool truncated = false, finished = false;
    std::optional<QAudioDecoder::Error> failure;
    QEventLoop loop;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        if (cancel && cancel->load()) {
            decoder.stop();
            loop.quit();
            return;
        }
        while (decoder.bufferAvailable() && !truncated) {
            const QAudioBuffer buffer = decoder.read();
            if (buffer.isValid())
                append(buffer, channels, rate);
            if (!channels.empty() && double(channels[0].size()) > MAX_SECONDS * rate) {
                truncated = true;
                decoder.stop();
                loop.quit();
            }
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&] {
        finished = true;
        loop.quit();
    });
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, [&](QAudioDecoder::Error e) {
        failure = e;
        loop.quit();
    });
    decoder.setSource(QUrl::fromLocalFile(path));
    decoder.start();
    if (!failure && !finished)
        loop.exec();
    if (cancel && cancel->load())
        return {std::nullopt, LoadError::Other, {}};
    const bool empty = channels.empty() || channels[0].empty() || !(rate > 0.0);
    // fin de fichier abîmée : on garde ce qui a été lu, en le signalant
    if (!empty) {
        Audio audio = Audio::make(name, rate, std::move(channels), truncated);
        audio.damaged = failure.has_value() && !truncated;
        return {std::move(audio), LoadError::Other, {}};
    }
    if (!failure)
        return {std::nullopt, LoadError::NoAudio, {}};
    const QString message = decoder.errorString();
    switch (*failure) {
    case QAudioDecoder::AccessDeniedError: return {std::nullopt, LoadError::Other, message};
    case QAudioDecoder::ResourceError:
        if (message.contains(u"audio"_s, Qt::CaseInsensitive))
            return {std::nullopt, LoadError::NoAudio, {}};
        return {std::nullopt, LoadError::Unsupported, {}};
    default: return {std::nullopt, LoadError::Unsupported, {}};
    }
}

// ------------------------------------------------------------------ conversion par ffmpeg

/// Programme ffmpeg : celui du dossier `ffmpeg` à côté du programme, sinon celui du système.
QString ffmpegProgram()
{
#ifdef Q_OS_WIN
    const QString exe = u"ffmpeg.exe"_s;
#else
    const QString exe = u"ffmpeg"_s;
#endif
    const QDir dir(QDir(QCoreApplication::applicationDirPath()).filePath(u"ffmpeg"_s));
    for (const QString &candidate : {dir.filePath(exe), dir.filePath(u"bin/"_s + exe)}) {
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return u"ffmpeg"_s;
}

struct Converted {
    QString wav;
    std::optional<LoadError> error;
};

/// Convertit la première piste audio de `path` en un fichier WAV temporaire. Vide : pas de ffmpeg.
/// Sous Windows, Qt lance ffmpeg sans fenêtre de console (le programme n'en a pas).
std::optional<Converted> convertWithFfmpeg(const QString &path, const std::atomic_bool *cancel)
{
    static std::atomic<unsigned> next = 0;
    const QString wav = QDir(QDir::tempPath())
                            .filePath(u"mevsono-%1-%2.wav"_s.arg(QCoreApplication::applicationPid()).arg(next++));
    QProcess process;
    process.setProgram(ffmpegProgram());
    // une seconde de plus que la limite : le lecteur WAV voit alors que le son a été coupé et le signale
    process.setArguments({u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s, u"-i"_s, path, u"-map"_s, u"0:a:0"_s, u"-vn"_s,
                          u"-c:a"_s, u"pcm_f32le"_s, u"-t"_s, QString::number(MAX_SECONDS + 1), wav});
    process.setStandardInputFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForStarted())
        return std::nullopt;
    // Sans limite de durée (un long son se convertit lentement), mais la fermeture de la fenêtre arrête
    // ffmpeg : sinon le programme attendrait la fin d'un ffmpeg bloqué pour se terminer.
    while (!process.waitForFinished(100)) {
        if (process.state() == QProcess::NotRunning)
            break;
        if (cancel && cancel->load()) {
            process.kill();
            process.waitForFinished();
            QFile::remove(wav);
            return Converted{{}, LoadError::Other};
        }
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && QFileInfo(wav).isFile())
        return Converted{wav, std::nullopt};
    QFile::remove(wav);
    const QString message = QString::fromLocal8Bit(process.readAllStandardError());
    return Converted{{}, message.contains(u"matches no streams"_s) ? LoadError::NoAudio : LoadError::Unsupported};
}

} // namespace

LoadResult load(const QString &path, const std::atomic_bool *cancel)
{
    const QString name = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {std::nullopt, LoadError::Other, file.errorString()};
    if (auto wav = readWav(file, name))
        return std::move(*wav);
    file.close();
    LoadResult result = decodeWithQt(path, name, cancel);
    if (result.audio || result.error == LoadError::Other)
        return result;
    // le décodeur ne connaît pas le format, ou le fichier n'a pas de piste qu'il sait décoder
    const auto converted = convertWithFfmpeg(path, cancel);
    if (!converted)
        return result;
    if (converted->error)
        return {std::nullopt, *converted->error, {}};
    QFile wavFile(converted->wav);
    std::optional<LoadResult> decoded;
    if (wavFile.open(QIODevice::ReadOnly))
        decoded = readWav(wavFile, name);
    wavFile.close();
    QFile::remove(converted->wav);
    return decoded ? std::move(*decoded) : LoadResult{std::nullopt, LoadError::Unsupported, {}};
}

bool writeWav(const QString &path, int rate, const std::vector<std::vector<float>> &channels)
{
    if (channels.empty())
        return false;
    const size_t n = channels[0].size();
    const auto k = quint16(channels.size());
    const auto dataLen = quint32(n * channels.size() * 2);
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataLen);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(WAVE_PCM) << k << quint32(rate) << quint32(rate * k * 2) << quint16(k * 2)
           << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataLen;
    for (size_t i = 0; i < n; ++i) {
        for (const auto &c : channels)
            stream << qint16(std::clamp(c[i], -1.0f, 1.0f) * 32767.0f);
    }
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && writeAll(file, out) && file.commit();
}

// ------------------------------------------------------------------ sons d'exemple

namespace {

double sine(double f, double t)
{
    return std::sin(2.0 * std::numbers::pi * f * t);
}

/// Bruit pseudo-aléatoire reproductible, entre −1 et 1.
class Noise {
public:
    explicit Noise(uint64_t seed)
        : m_state(seed * 6364136223846793005ull + 1442695040888963407ull)
    {
    }

    double next()
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return (double(m_state >> 11) / double(uint64_t(1) << 53)) * 2.0 - 1.0;
    }

private:
    uint64_t m_state;
};

/// Filtre passe-bande du second ordre (Robert Bristow-Johnson), de gain 1 à `f` Hz.
class Bandpass {
public:
    Bandpass(double f, double q)
    {
        const double w = 2.0 * std::numbers::pi * f / RATE;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        m_b = {alpha / a0, 0.0, -alpha / a0};
        m_a = {-2.0 * std::cos(w) / a0, (1.0 - alpha) / a0};
    }

    double tick(double x)
    {
        const double y = m_b[0] * x + m_b[1] * m_x[0] + m_b[2] * m_x[1] - m_a[0] * m_y[0] - m_a[1] * m_y[1];
        m_x = {x, m_x[0]};
        m_y = {y, m_y[0]};
        return y;
    }

private:
    std::array<double, 3> m_b{};
    std::array<double, 2> m_a{};
    std::array<double, 2> m_x{};
    std::array<double, 2> m_y{};
};

/// Bruit lent (fluctuations du souffle) : bruit filtré passe-bas, d'écart type 1/3 (presque toujours
/// entre −1 et 1).
class SlowNoise {
public:
    /// `cutoff` : fréquence de coupure (Hz).
    SlowNoise(uint64_t seed, double cutoff)
        : m_noise(seed)
        , m_coefficient(1.0 - std::exp(-2.0 * std::numbers::pi * cutoff / RATE))
    {
    }

    double next()
    {
        // deux passe-bas du premier ordre : ils divisent la variance du bruit (1/3) par 4 / c environ
        const double gain = std::sqrt(12.0 / m_coefficient) / 3.0;
        m_state[0] += m_coefficient * (m_noise.next() * gain - m_state[0]);
        m_state[1] += m_coefficient * (m_state[0] - m_state[1]);
        return m_state[1];
    }

private:
    Noise m_noise;
    std::array<double, 2> m_state{};
    double m_coefficient;
};

/// `seconds` secondes du signal `f(t)`, avec une montée et une descente de 10 ms (pas de clic).
std::vector<float> synth(double seconds, const std::function<double(double)> &f)
{
    const auto n = size_t(seconds * RATE);
    const double ramp = 0.01 * RATE;
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double fade = std::min({double(i) / ramp, double(n - i) / ramp, 1.0});
        out[i] = float(fade * f(double(i) / RATE));
    }
    return out;
}

/// Son d'harmoniques d'amplitudes relatives `amplitudes` (rang 1, 2…), d'amplitude crête ≈ `level`,
/// avec un léger bruit de souffle (−60 dB), comme un enregistrement.
std::vector<float> harmonicSound(double f0, const std::vector<double> &amplitudes, double seconds, double level)
{
    double total = 0.0;
    for (double a : amplitudes)
        total += a;
    Noise noise(7);
    return synth(seconds, [&](double t) {
        double s = 0.0;
        for (size_t k = 0; k < amplitudes.size(); ++k)
            s += amplitudes[k] * sine(double(k + 1) * f0, t);
        return level * s / total + 0.001 * noise.next();
    });
}

/// Corde pincée (algorithme de Karplus et Strong) : une impulsion de bruit qui circule dans une
/// ligne à retard d'une période, et s'amortit.
std::vector<float> pluckedString(double f0, double seconds)
{
    // retard de la boucle : `len` − 0,5 échantillon (ligne et moyenne de deux échantillons voisins),
    // plus `frac` (filtre passe-tout du premier ordre, entre 0,1 et 1,1) : la note est juste
    const double period = RATE / f0;
    const auto len = size_t(std::floor(period + 0.4));
    const double frac = period + 0.5 - double(len);
    const double c = (1.0 - frac) / (1.0 + frac);
    Noise noise(3);
    std::vector<double> line(len);
    double mean = 0.0;
    for (double &x : line) {
        x = noise.next();
        mean += x;
    }
    mean /= double(len);
    for (double &x : line)
        x -= mean;
    const auto n = size_t(seconds * RATE);
    double previousIn = 0.0, previousOut = 0.0;
    std::vector<double> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t k = i % len;
        const double averaged = 0.996 * 0.5 * (line[k] + line[(k + 1) % len]);
        const double y = c * averaged + previousIn - c * previousOut;
        previousIn = averaged;
        previousOut = y;
        line[k] = y;
        out.push_back(y);
    }
    double peak = 1e-9;
    for (double x : out)
        peak = std::max(peak, std::abs(x));
    std::vector<float> result(n);
    for (size_t i = 0; i < n; ++i)
        result[i] = float(0.6 * out[i] / peak);
    return result;
}

/// Durée de l'extinction du son après l'arrêt du souffle, et de la réverbération.
constexpr double TAIL = 0.5;

/// Enveloppe d'une note tenue : montée en `attack` secondes, arrêt en `release` secondes au bout de
/// `seconds` secondes (demi-cosinus, sans claquement).
double envelope(double t, double seconds, double attack, double release)
{
    const auto smooth = [](double x) { return 0.5 - 0.5 * std::cos(std::numbers::pi * std::clamp(x, 0.0, 1.0)); };
    return smooth(t / attack) * smooth((seconds - t) / release);
}

/// Réverbération d'une petite pièce (Freeverb : huit filtres en peigne amortis, quatre passe-tout).
std::vector<double> reverb(const std::vector<double> &input)
{
    constexpr std::array<size_t, 8> COMBS = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    constexpr std::array<size_t, 4> ALLPASSES = {556, 441, 341, 225};
    constexpr double FEEDBACK = 0.8, DAMPING = 0.3;
    struct Comb {
        std::vector<double> buffer;
        size_t i = 0;
        double store = 0.0;
    };
    struct Allpass {
        std::vector<double> buffer;
        size_t i = 0;
    };
    std::vector<Comb> combs;
    for (size_t len : COMBS)
        combs.push_back({std::vector<double>(len, 0.0)});
    std::vector<Allpass> allpasses;
    for (size_t len : ALLPASSES)
        allpasses.push_back({std::vector<double>(len, 0.0)});
    std::vector<double> out;
    out.reserve(input.size());
    for (double x : input) {
        x *= 0.015;
        double y = 0.0;
        for (Comb &c : combs) {
            const double delayed = c.buffer[c.i];
            c.store = delayed * (1.0 - DAMPING) + c.store * DAMPING;
            c.buffer[c.i] = x + c.store * FEEDBACK;
            c.i = (c.i + 1) % c.buffer.size();
            y += delayed;
        }
        for (Allpass &a : allpasses) {
            const double delayed = a.buffer[a.i];
            a.buffer[a.i] = y + delayed * 0.5;
            y = delayed - y;
            a.i = (a.i + 1) % a.buffer.size();
        }
        out.push_back(y);
    }
    return out;
}

/// Fin commune des sons d'instruments : composante continue retirée, réverbération (part `room`),
/// crête ramenée à `level`.
std::vector<float> finish(const std::vector<double> &samples, double level, double room)
{
    double previous = 0.0, blocked = 0.0;
    std::vector<double> dry;
    dry.reserve(samples.size());
    for (double x : samples) {
        blocked = x - previous + 0.995 * blocked;
        previous = x;
        dry.push_back(blocked);
    }
    const std::vector<double> wet = reverb(dry);
    std::vector<double> mixed(dry.size());
    double peak = 1e-9;
    for (size_t i = 0; i < dry.size(); ++i) {
        mixed[i] = dry[i] + room * wet[i];
        peak = std::max(peak, std::abs(mixed[i]));
    }
    std::vector<float> out(mixed.size());
    for (size_t i = 0; i < mixed.size(); ++i)
        out[i] = float(level * mixed[i] / peak);
    return out;
}

/// Tuyau d'orgue bouché (fermé à son extrémité) : ses modes de vibration, et donc ses harmoniques,
/// sont les multiples impairs de la fondamentale. Un son d'orgue est tenu et stable ; le tuyau parle
/// avec un petit chuintement et un troisième harmonique un peu en avance, on entend le vent, et
/// l'église résonne.
std::vector<float> organPipe(double f0, double seconds)
{
    // Niveaux des harmoniques 1, 3, 5, 7 et 9, en décibels.
    constexpr std::array<double, 5> LEVELS = {0.0, -12.0, -22.0, -30.0, -37.0};
    const auto n = size_t((seconds + TAIL) * RATE);
    std::array<double, 5> amplitudes{};
    for (size_t k = 0; k < LEVELS.size(); ++k)
        amplitudes[k] = std::pow(10.0, LEVELS[k] / 20.0);
    Noise noise(21);
    Bandpass wind(1800.0, 0.7);
    std::vector<double> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = double(i) / RATE;
        double tone = 0.0;
        for (size_t k = 0; k < amplitudes.size(); ++k) {
            const double rank = double(2 * k + 1);
            // les harmoniques aigus parlent les premiers ; le troisième dépasse un instant
            const double spit = k == 1 ? 1.0 + 1.5 * std::exp(-t / 0.04) : 1.0;
            tone += amplitudes[k] * spit * envelope(t, seconds, 0.06 / std::sqrt(rank), 0.05) * sine(rank * f0, t);
        }
        const double chiff = 0.3 * std::exp(-t / 0.03);
        out.push_back(tone + (0.004 + chiff) * envelope(t, seconds, 0.005, 0.05) * wind.tick(noise.next()));
    }
    return finish(out, 0.5, 0.4);
}

/// Flûte : somme d'harmoniques au spectre d'une flûte (La 440 Hz joué mezzo forte), les aigus
/// s'installant un peu après la fondamentale, avec le souffle de l'instrumentiste (bruit, plus fort
/// à l'attaque) et la réverbération d'une pièce.
std::vector<float> flute(double f0, double seconds)
{
    // Niveaux des harmoniques 1 à 6, en décibels.
    constexpr std::array<double, 6> LEVELS = {0.0, -9.0, -15.0, -24.0, -30.0, -35.0};
    const auto n = size_t((seconds + TAIL) * RATE);
    std::array<double, 6> amplitudes{};
    for (size_t k = 0; k < LEVELS.size(); ++k)
        amplitudes[k] = std::pow(10.0, LEVELS[k] / 20.0);
    Noise noise(9);
    SlowNoise flutter(17, 300.0);
    SlowNoise drift(13, 1.5);
    Bandpass hiss(2500.0, 0.8);
    std::vector<double> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = double(i) / RATE;
        const double blow = envelope(t, seconds, 0.03, 0.12);
        // souffle irrégulier : le son n'est jamais tout à fait pur
        const double life = 1.0 + 0.01 * drift.next() + 0.03 * flutter.next();
        double tone = 0.0;
        for (size_t k = 0; k < amplitudes.size(); ++k) {
            const double h = double(k + 1);
            tone += amplitudes[k] * envelope(t, seconds, 0.07 - 0.008 * double(k), 0.12) * sine(h * f0, t);
        }
        const double chiff = 1.0 + 3.0 * std::exp(-t / 0.06);
        out.push_back(tone * life + 0.06 * blow * chiff * hiss.tick(noise.next()));
    }
    return finish(out, 0.5, 0.25);
}

} // namespace

Audio generate(Example example, const QString &name)
{
    std::vector<float> samples;
    switch (example) {
    case Example::TuningFork:
        samples = synth(3.0, [](double t) { return 0.6 * std::exp(-t / 1.5) * sine(440.0, t); });
        break;
    case Example::Flute: samples = flute(440.0, 2.5); break;
    case Example::OrganPipe: samples = organPipe(261.63, 2.5); break;
    case Example::String: samples = pluckedString(110.0, 3.0); break;
    case Example::Square: {
        std::vector<double> odd(40);
        for (size_t n = 1; n <= odd.size(); ++n)
            odd[n - 1] = n % 2 == 1 ? 1.0 / double(n) : 0.0;
        samples = harmonicSound(200.0, odd, 2.0, 0.4);
        break;
    }
    case Example::Beats:
        samples = synth(4.0, [](double t) { return 0.3 * (sine(440.0, t) + sine(444.0, t)); });
        break;
    }
    std::vector<std::vector<float>> channels;
    channels.push_back(std::move(samples));
    return Audio::make(name, RATE, std::move(channels), false);
}

} // namespace audio
