#include "audio.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

using namespace Qt::StringLiterals;

namespace {
void prepare_backend_audio()
{
    // MeVSono ne décode que le son, y compris dans un fichier vidéo.
    for (const char *option : {"QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"}) {
        if (!qEnvironmentVariableIsSet(option))
            qputenv(option, ",");
    }
#if defined(Q_OS_LINUX) && QT_FEATURE_pulseaudio == 1
    // Qt/PipeWire 6.11.2 peut planter si la sortie disparaît pendant son ouverture.
    const bool serveurPulse = !qEnvironmentVariableIsEmpty("PULSE_SERVER") ||
        QFileInfo::exists(qEnvironmentVariable("XDG_RUNTIME_DIR") + u"/pulse/native"_s);
    if (serveurPulse && qEnvironmentVariableIsEmpty("QT_AUDIO_BACKEND") &&
        !QStandardPaths::findExecutable(u"pactl"_s).isEmpty())
        qputenv("QT_AUDIO_BACKEND", "pulseaudio");
#endif
}
}
Q_COREAPP_STARTUP_FUNCTION(prepare_backend_audio)

namespace audio {

QStringList extensions()
{
    return {u"wav"_s, u"mp3"_s, u"ogg"_s, u"oga"_s, u"flac"_s, u"m4a"_s, u"aac"_s, u"adts"_s, u"aif"_s, u"aiff"_s,
            u"caf"_s, u"opus"_s, u"mp4"_s, u"mov"_s, u"m4v"_s, u"mkv"_s, u"webm"_s, u"avi"_s};
}

QString code_canal(Channel c)
{
    switch (c) {
    case Channel::Mix: return u"mix"_s;
    case Channel::Left: return u"left"_s;
    case Channel::Right: return u"right"_s;
    }
    return {};
}

std::optional<Channel> canal_du_code(const QString &code)
{
    for (Channel c : ALL_CHANNELS) {
        if (code_canal(c) == code)
            return c;
    }
    return std::nullopt;
}

Audio Audio::fabrique(QString nom, double cadence, std::vector<std::vector<float>> voies, bool dureeLimitee)
{
    size_t longueur = voies.empty() ? 0 : voies.front().size();
    for (const auto &voie : voies)
        longueur = std::min(longueur, voie.size());

    Audio son;
    son.name = std::move(nom);
    son.rate = cadence;
    son.truncated = dureeLimitee;
    for (auto &voie : voies) {
        // Même la partie tronquée peut signaler un fichier flottant abîmé.
        for (float amplitude : std::span(voie).subspan(longueur)) {
            if (!std::isfinite(amplitude))
                son.invalidSamples = true;
        }
        voie.resize(longueur);
        for (float &amplitude : voie) {
            if (!std::isfinite(amplitude)) {
                amplitude = 0.f;
                son.invalidSamples = true;
            }
            // Deux voies opposées s'annulent dans le mélange. Leur crête reste mesurable.
            son.m_crete = std::max(son.m_crete, std::abs(amplitude));
        }
    }

    const bool stereoIdentique = voies.size() == 2 && (longueur == 0 ||
        std::memcmp(voies[0].data(), voies[1].data(), longueur * sizeof(float)) == 0);
    if (voies.size() == 1 || stereoIdentique) {
        son.mix = std::make_shared<const std::vector<float>>(std::move(voies[0]));
        son.channels.assign(voies.size(), son.mix);
        return son;
    }

    // La somme en double évite de déborder avant la division lorsqu'un WAV
    // flottant contient de très grandes amplitudes sur plusieurs voies.
    std::vector<float> moyenne(longueur);
    if (voies.size() == 2) {
        const auto &gauche = voies[0];
        const auto &droite = voies[1];
        for (size_t i = 0; i < longueur; ++i)
            moyenne[i] = float((double(gauche[i]) + double(droite[i])) * 0.5);
    } else {
        for (size_t i = 0; i < longueur; ++i) {
            double somme = 0.0;
            for (const auto &voie : voies)
                somme += voie[i];
            moyenne[i] = float(somme / double(voies.size()));
        }
    }
    son.mix = std::make_shared<const std::vector<float>>(std::move(moyenne));
    for (auto &voie : voies)
        son.channels.push_back(std::make_shared<const std::vector<float>>(std::move(voie)));
    return son;
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

// ------------------------------------------------------------------ autres formats (Qt Multimedia)

namespace {

/// Sépare les voies entrelacées du tampon. Pour un microphone à plusieurs
/// entrées, la prise conserve les deux premières voies dans leur ordre d'origine.
void ajoute_tampon(const QAudioBuffer &tampon, qsizetype trames, std::vector<std::vector<float>> &voies)
{
    const QAudioFormat format = tampon.format();
    const int voiesEntrelacees = format.channelCount();
    const int voiesAConserver = std::min(voiesEntrelacees, int(voies.size()));
    const auto copie = [&](auto donnees, auto convertit) {
        for (qsizetype trame = 0; trame < trames; ++trame) {
            for (int voie = 0; voie < voiesAConserver; ++voie)
                voies[size_t(voie)].push_back(convertit(donnees[trame * voiesEntrelacees + voie]));
        }
    };
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        copie(tampon.constData<quint8>(), [](quint8 v) { return (float(v) - 128.0f) / 128.0f; });
        break;
    case QAudioFormat::Int16:
        copie(tampon.constData<qint16>(), [](qint16 v) { return float(v) / 32768.0f; });
        break;
    case QAudioFormat::Int32:
        copie(tampon.constData<qint32>(), [](qint32 v) { return float(double(v) / 2147483648.0); });
        break;
    case QAudioFormat::Float:
        copie(tampon.constData<float>(), [](float v) { return v; });
        break;
    default:
        break;
    }
}

/// Qt Multimedia lit la piste audio marquée par défaut ou, à défaut, la première.
LoadResult decode_avec_qt(const QString &path, const QString &name, const std::atomic_bool *cancel)
{
    QAudioDecoder decoder;
    if (!decoder.isSupported())
        return {std::nullopt, LoadError::Unsupported, {}};
    std::vector<std::vector<float>> channels;
    double rate = 0.0;
    bool truncated = false, finished = false, formatChange = false;
    std::optional<QAudioDecoder::Error> failure;
    QEventLoop loop;
    // Un décodeur qui attend son premier bloc n’émet pas encore bufferReady.
    // La minuterie lui permet aussi de s’arrêter quand la fenêtre se ferme à ce moment-là.
    QTimer surveillance;
    if (cancel) {
        QObject::connect(&surveillance, &QTimer::timeout, &loop, [&] {
            if (*cancel) {
                decoder.stop();
                loop.quit();
            }
        });
        surveillance.start(100);
    }
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        while (decoder.bufferAvailable() && !truncated && !formatChange && !(cancel && *cancel)) {
            const QAudioBuffer buffer = decoder.read();
            if (!buffer.isValid())
                continue;
            const auto format = buffer.format();
            if (!channels.empty() && (format.sampleRate() != rate || size_t(format.channelCount()) != channels.size())) {
                // Une durée et une fréquence d'échantillonnage communes sont nécessaires aux mesures.
                formatChange = true;
                decoder.stop();
                loop.quit();
                break;
            }
            if (channels.empty()) {
                rate = format.sampleRate();
                channels.resize(size_t(format.channelCount()));
            }
            const size_t restantes = size_t(MAX_SECONDS * rate) - channels.front().size();
            const auto trames = std::min(size_t(buffer.frameCount()), restantes);
            ajoute_tampon(buffer, qsizetype(trames), channels);
            if (trames < size_t(buffer.frameCount())) {
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
    if (!failure && !finished && !truncated && !formatChange && !(cancel && *cancel))
        loop.exec();
    if (cancel && cancel->load())
        return {std::nullopt, LoadError::Other, {}};
    if (formatChange) {
        // Reprendre le fichier entier à cadence constante. Si la conversion échoue,
        // les trames déjà lues restent disponibles avec un avertissement.
        if (auto conversion = convertit_avec_ffmpeg(path, name, cancel); conversion && conversion->audio)
            return std::move(*conversion);
        if (cancel && *cancel)
            return {std::nullopt, LoadError::Other, {}};
    }
    const bool empty = channels.empty() || channels[0].empty() || !(rate > 0.0);
    // fin de fichier abîmée : on garde ce qui a été lu, en le signalant
    if (!empty) {
        Audio audio = Audio::fabrique(name, rate, std::move(channels), truncated);
        audio.damaged = (failure.has_value() || formatChange) && !truncated;
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

/// Cherche d'abord l'outil à côté de MeVSono, puis dans les programmes du système.
QString outil_ffmpeg(const QString &nom)
{
#ifdef Q_OS_WIN
    const QString exe = nom + u".exe"_s;
#else
    const QString exe = nom;
#endif
    const QDir dir(QDir(QCoreApplication::applicationDirPath()).filePath(u"ffmpeg"_s));
    for (const QString &candidate : {dir.filePath(exe), dir.filePath(u"bin/"_s + exe)}) {
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return nom;
}

bool execute_outil(QProcess &process, const std::atomic_bool *cancel)
{
    if (cancel && *cancel)
        return false;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForStarted())
        return false;
    // Sans limite de durée (un long son se convertit lentement), mais la fermeture de la fenêtre arrête
    // ffmpeg : sinon le programme attendrait la fin d'un ffmpeg bloqué pour se terminer.
    while (!process.waitForFinished(100)) {
        if (process.state() == QProcess::NotRunning)
            break;
        if (cancel && cancel->load()) {
            process.kill();
            process.waitForFinished();
            return false;
        }
    }
    return true;
}

/// Les anciennes versions de ffmpeg ne savent pas désigner une piste par son drapeau « default ».
QString piste_avec_ffprobe(const QString &path, const std::atomic_bool *cancel)
{
    QProcess sonde;
    sonde.setProgram(outil_ffmpeg(u"ffprobe"_s));
    sonde.setArguments({u"-v"_s, u"error"_s, u"-select_streams"_s, u"a"_s, u"-show_entries"_s,
                        u"stream=index:stream_disposition=default"_s, u"-of"_s, u"json"_s, u"-i"_s, path});
    if (execute_outil(sonde, cancel) && sonde.exitStatus() == QProcess::NormalExit && sonde.exitCode() == 0) {
        const auto pistes = QJsonDocument::fromJson(sonde.readAllStandardOutput()).object().value(u"streams"_s).toArray();
        for (const auto &element : pistes) {
            const auto piste = element.toObject();
            const int index = piste.value(u"index"_s).toInt(-1);
            if (index >= 0 && piste.value(u"disposition"_s).toObject().value(u"default"_s).toInt() == 1)
                return u"0:%1"_s.arg(index);
        }
    }
    return u"0:a:0"_s;
}

} // namespace

std::optional<LoadResult> convertit_avec_ffmpeg(const QString &path, const QString &name, const std::atomic_bool *cancel)
{
    QTemporaryDir dossier;
    if (!dossier.isValid())
        return LoadResult{std::nullopt, LoadError::Other, dossier.errorString()};
    const QString wav = dossier.filePath(u"piste.wav"_s);
    const QString parDefaut = u"0:a:disp:default:0"_s;
    QString piste = parDefaut;
    for (;;) {
        QProcess process;
        process.setProgram(outil_ffmpeg(u"ffmpeg"_s));
        // Une seconde de plus permet au lecteur WAV de signaler la limite de durée.
        process.setArguments({u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s, u"-i"_s, path, u"-map"_s, piste, u"-vn"_s,
                              u"-c:a"_s, u"pcm_f32le"_s, u"-t"_s, QString::number(MAX_SECONDS + 1), wav});
        const bool lance = execute_outil(process, cancel);
        if (cancel && *cancel)
            return LoadResult{std::nullopt, LoadError::Other, {}};
        if (!lance)
            return std::nullopt;
        const bool termine = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        QFile fichier(wav);
        if (termine || fichier.size() > 0) {
            if (!fichier.open(QIODevice::ReadOnly))
                return LoadResult{std::nullopt, LoadError::Other, fichier.errorString()};
            if (auto lecture = lit_wav(fichier, name, cancel)) {
                if (lecture->audio) {
                    // Même après une erreur du convertisseur, le début du WAV peut être écouté.
                    if (!termine && !lecture->audio->truncated)
                        lecture->audio->damaged = true;
                    return lecture;
                }
                if (termine || (cancel && *cancel))
                    return lecture;
            }
            if (termine)
                return LoadResult{std::nullopt, LoadError::Incomplete, {}};
        }
        const QByteArray erreur = process.readAllStandardError();
        const bool absente = erreur.contains("matches no streams");
        if (piste == parDefaut && erreur.contains("Invalid stream specifier")) {
            piste = piste_avec_ffprobe(path, cancel);
        } else if (piste == parDefaut && absente) {
            piste = u"0:a:0"_s;
        } else {
            return LoadResult{std::nullopt, absente ? LoadError::NoAudio : LoadError::Unsupported, {}};
        }
    }
}

LoadResult charge(const QString &path, const std::atomic_bool *cancel)
{
    if (cancel && *cancel)
        return {std::nullopt, LoadError::Other, {}};
    // Le lecteur a besoin d'un fichier dont la fin est connue. Ouvrir un tube
    // nommé ici attendrait un producteur, même après la fermeture de la fenêtre.
    const QFileInfo fichier(path);
    if (fichier.exists() && !fichier.isFile())
        return {std::nullopt, LoadError::Unsupported, {}};
    const QString name = fichier.fileName().isEmpty() ? path : fichier.fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {std::nullopt, LoadError::Other, file.errorString()};
    if (auto wav = lit_wav(file, name, cancel))
        return std::move(*wav);
    if (cancel && *cancel)
        return {std::nullopt, LoadError::Other, {}};
    // Un flux AAC brut peut changer de cadence entre deux prises. Le convertir dans un
    // processus séparé protège la prise ouverte et donne une cadence unique à tout le signal.
    file.seek(0);
    const bool aacBrut = name.endsWith(u".aac"_s, Qt::CaseInsensitive) || name.endsWith(u".adts"_s, Qt::CaseInsensitive)
                         || QMimeDatabase().mimeTypeForData(file.peek(16)).name() == u"audio/aac"_s;
    file.close();
    if (aacBrut) {
        if (auto conversion = convertit_avec_ffmpeg(path, name, cancel))
            return std::move(*conversion);
    }
    LoadResult result = decode_avec_qt(path, name, cancel);
    if (aacBrut || result.audio || result.error == LoadError::Other)
        return result;
    // le décodeur ne connaît pas le format, ou le fichier n'a pas de piste qu'il sait décoder
    if (auto converted = convertit_avec_ffmpeg(path, name, cancel))
        return std::move(*converted);
    return result;
}

Prise::Prise(const QAudioFormat &format)
    : m_format(format), m_voies(size_t(std::clamp(format.channelCount(), 1, 2))),
      m_limite(format.isValid() ? size_t(MAX_SECONDS * format.sampleRate()) : 0)
{
}

void Prise::ajoute(const QByteArray &octets)
{
    if (pleine() || octets.isEmpty())
        return;
    m_reste.append(octets);
    const int octetsParTrame = m_format.bytesPerFrame();
    const size_t debut = m_voies.front().size();
    const size_t trames = std::min(size_t(m_reste.size() / octetsParTrame), m_limite - debut);
    if (trames == 0)
        return;
    ajoute_tampon(QAudioBuffer(m_reste, m_format), qsizetype(trames), m_voies);
    for (const auto &voie : m_voies) {
        for (const float amplitude : std::span(voie).subspan(debut)) {
            if (std::isfinite(amplitude))
                m_crete = std::max(m_crete, std::abs(amplitude));
        }
    }
    m_reste.remove(0, qsizetype(trames) * octetsParTrame);
}

float Prise::releve_crete()
{
    return std::exchange(m_crete, 0.f);
}

double Prise::duree() const
{
    return m_format.isValid() ? double(m_voies.front().size()) / m_format.sampleRate() : 0.0;
}
bool Prise::pleine() const { return m_voies.front().size() >= m_limite; }

std::optional<Audio> Prise::termine(QString nom)
{
    if (m_voies.front().empty())
        return std::nullopt;
    const bool limite = pleine();
    return Audio::fabrique(std::move(nom), m_format.sampleRate(), std::move(m_voies), limite);
}

} // namespace audio
