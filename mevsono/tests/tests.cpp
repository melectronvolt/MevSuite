// Tests du code sans interface, repris des tests de la version Rust.

#include "analyzer.h"
#include "audio.h"
#include "dsp.h"
#include "format.h"
#include "help.h"
#include "i18n.h"
#include "player.h"
#include "session.h"
#include "settings.h"
#include "wave.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <functional>
#include <numbers>
#include <thread>

using namespace Qt::StringLiterals;
using namespace dsp;

namespace {

constexpr double RATE = 44100.0;

/// Somme de sinusoïdes (fréquence, amplitude), échantillonnée à 44,1 kHz.
std::vector<float> tones(const std::vector<std::pair<double, double>> &components, double seconds)
{
    const auto n = size_t(seconds * RATE);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = double(i) / RATE;
        double v = 0.0;
        for (const auto &[f, a] : components)
            v += a * std::sin(2.0 * std::numbers::pi * f * t);
        out[i] = float(v);
    }
    return out;
}

std::vector<std::pair<double, double>> harmonicSeries(double f0, const std::vector<double> &amplitudes)
{
    std::vector<std::pair<double, double>> out;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if (amplitudes[i] > 0.0)
            out.emplace_back(double(i + 1) * f0, amplitudes[i]);
    }
    return out;
}

Analysis analyseTones(const std::vector<std::pair<double, double>> &components)
{
    const std::vector<float> x = tones(components, 0.5);
    return *analyse(x, 0, x.size(), RATE, {Window::Hann, 0}, PeakParams{});
}

std::optional<double> fundamentalOf(const audio::Audio &a, audio::Channel channel)
{
    const Samples &signal = a.signal(channel);
    const auto r = analyse(*signal, 0, signal->size(), a.rate, {Window::Hann, 0}, PeakParams{});
    return r ? r->fundamental : std::nullopt;
}

Playback playback(const std::vector<float> &samples, double from, size_t end, double rate, double outputRate)
{
    return Playback(std::make_shared<const std::vector<float>>(samples), rate, from, end, outputRate,
                    std::make_shared<PlaybackShared>());
}

/// Attend que `condition` soit vraie, en appelant `update` comme le fait l'interface.
void wait(Player &player, const std::function<bool(const Player &)> &condition)
{
    QElapsedTimer started;
    started.start();
    while (!condition(player)) {
        QVERIFY2(started.elapsed() < 5000, "attente trop longue");
        QCOMPARE(player.update(), std::nullopt);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class Tests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ dsp
    void windowsHaveExpectedShape()
    {
        for (Window w : ALL_WINDOWS) {
            const std::vector<double> c = windowCoefficients(w, 1024);
            QCOMPARE(c.size(), size_t(1024));
            // symétrique autour du milieu, maximale au milieu
            QVERIFY(std::abs(c[1] - c[1023]) < 1e-12);
            QVERIFY(std::ranges::all_of(c, [&](double x) { return x <= c[512] + 1e-12; }));
            QCOMPARE(windowFromCode(windowCode(w)), w);
        }
        QVERIFY(std::abs(windowCoefficients(Window::Hann, 8)[0]) < 1e-12);
    }

    void sineAmplitudeAndFrequencyForEveryWindow()
    {
        const std::vector<float> x = tones({{1000.3, 0.5}}, 0.5);
        for (Window w : ALL_WINDOWS) {
            const auto sp = spectrum(x, RATE, {w, 0});
            QVERIFY(sp);
            std::vector<Peak> p = peaks(*sp, w, PeakParams{});
            // la fenêtre rectangulaire laisse voir des lobes secondaires : on garde le plus grand pic
            if (w != Window::Rectangular)
                QCOMPARE(p.size(), size_t(1));
            std::ranges::stable_sort(p, [](const Peak &a, const Peak &b) { return a.amplitude > b.amplitude; });
            // interpolation moins précise sur le lobe étroit de la fenêtre rectangulaire
            const double dfMax = w == Window::Rectangular ? 0.1 : 0.05;
            QVERIFY2(std::abs(p[0].freq - 1000.3) < dfMax, qPrintable(windowCode(w)));
            const double tolerance = w == Window::Rectangular ? 0.06 : 0.01;
            QVERIFY2(std::abs(p[0].amplitude - 0.5) < 0.5 * tolerance, qPrintable(windowCode(w)));
            // la fondamentale reste juste, même avec les lobes secondaires
            const auto f0 = fundamental(p, sp->df(), 20.0);
            QVERIFY(f0 && std::abs(*f0 - 1000.3) < dfMax);
        }
    }

    void spectrumSizesAndAveraging()
    {
        const std::vector<float> x = tones({{440.0, 0.8}}, 1.0);
        // automatique : tout l'extrait, complété jusqu'à la puissance de 2 suivante
        const auto autoSize = spectrum(x, RATE, {Window::Hann, 0});
        QCOMPARE(autoSize->fftLen, size_t(65536));
        QCOMPARE(autoSize->segmentLen, size_t(44100));
        QCOMPARE(autoSize->segments, size_t(1));
        QCOMPARE(autoSize->amplitude.size(), size_t(32769));
        // taille fixée plus petite que l'extrait : segments moyennés, recouverts de moitié
        const auto welch = spectrum(x, RATE, {Window::Hann, 4096});
        QCOMPARE(welch->fftLen, size_t(4096));
        QCOMPARE(welch->segmentLen, size_t(4096));
        QCOMPARE(welch->segments, size_t((44100 - 4096) / 2048 + 1));
        const std::vector<Peak> p = peaks(*welch, Window::Hann, PeakParams{});
        QVERIFY(std::abs(p[0].freq - 440.0) < 0.5);
        QVERIFY(std::abs(p[0].amplitude - 0.8) < 0.02);
        QVERIFY(std::abs(welch->df() - RATE / 4096.0) < 1e-9);
        QVERIFY(!spectrum(std::span(x).first(4), RATE, {Window::Hann, 0}));
        // la composante continue est retirée
        std::vector<float> offset = x;
        for (float &v : offset)
            v += 0.3f;
        QVERIFY(spectrum(offset, RATE, {Window::Hann, 0})->amplitude[0] < 1e-6);
    }

    void closeFrequenciesAreSeparated()
    {
        // battements 440 et 446 Hz : séparés avec 1 s d'extrait (finesse ≈ 1 Hz)
        const std::vector<float> x = tones({{440.0, 0.5}, {446.0, 0.5}}, 1.0);
        const auto sp = spectrum(x, RATE, {Window::Hann, 0});
        const std::vector<Peak> p = peaks(*sp, Window::Hann, PeakParams{});
        QCOMPARE(p.size(), size_t(2));
        QVERIFY(std::abs(p[0].freq - 440.0) < 0.1 && std::abs(p[1].freq - 446.0) < 0.1);
    }

    void fundamentalOfMusicalSounds()
    {
        // flûte : tous les harmoniques
        auto a = analyseTones(harmonicSeries(440.0, {1.0, 0.4, 0.25, 0.1, 0.05}));
        QVERIFY(std::abs(a.fundamental.value() - 440.0) < 0.05);
        // clarinette : harmoniques impairs
        a = analyseTones(harmonicSeries(147.0, {1.0, 0.0, 0.6, 0.0, 0.35, 0.0, 0.2}));
        QVERIFY(std::abs(a.fundamental.value() - 147.0) < 0.05);
        // harmonique 2 plus fort que la fondamentale
        a = analyseTones(harmonicSeries(110.0, {0.3, 1.0, 0.5, 0.4, 0.2}));
        QVERIFY(std::abs(a.fundamental.value() - 110.0) < 0.05);
        // fondamentale absente (seuls les harmoniques 2 à 5)
        a = analyseTones(harmonicSeries(200.0, {0.0, 1.0, 0.8, 0.6, 0.4}));
        QVERIFY(std::abs(a.fundamental.value() - 200.0) < 0.05);
        // son pur
        a = analyseTones({{523.25, 0.5}});
        QVERIFY(std::abs(a.fundamental.value() - 523.25) < 0.05);
        // période par autocorrélation
        a = analyseTones(harmonicSeries(220.0, {1.0, 0.5, 0.3}));
        QVERIFY(std::abs(a.period.value() - 1.0 / 220.0) < 1e-6);
    }

    void harmonicTable()
    {
        const Analysis a = analyseTones(harmonicSeries(200.0, {1.0, 0.5, 0.0, 0.25}));
        const std::vector<Harmonic> h = harmonics(a.peaks, a.fundamental.value(), a.spectrum.df(), 5);
        QCOMPARE(h.size(), size_t(5));
        QVERIFY(h[0].peak && h[1].peak && h[3].peak);
        QVERIFY(!h[2].peak && !h[4].peak);
        QVERIFY(std::abs(h[1].peak->amplitude - 0.5) < 0.01);
        QVERIFY(std::abs(h[3].expected - 800.0) < 0.1);
    }

    void silenceAndNoiseHaveNoFundamental()
    {
        const std::vector<float> silence(4096, 0.0f);
        const auto a = analyse(silence, 0, 4096, RATE, {Window::Hann, 0}, PeakParams{});
        QVERIFY(a && a->peaks.empty() && !a->fundamental && !a->period);
        QVERIFY(!analyse(silence, 10, 4097, RATE, {Window::Hann, 0}, PeakParams{}));
    }

    void zeroCrossings()
    {
        const std::vector<float> x = tones({{100.0, 1.0}}, 0.1);
        // passages montants à 0, 10 ms, 20 ms…
        const auto t = risingZeroCrossing(x, RATE, 0.0102, 0.002);
        QVERIFY(t && std::abs(*t - 0.010) < 1e-6);
        QVERIFY(!risingZeroCrossing(x, RATE, 0.015, 0.001));
    }

    // ------------------------------------------------------------ tracé du signal
    void envelopeMatchesADirectScan()
    {
        std::vector<float> samples(100'003);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = std::sin(float(i) * 0.37f) * float(i % 997) / 997.0f;
        const Envelope env(samples);
        const auto scan = [&](size_t first, size_t last) {
            const auto [lo, hi] = std::minmax_element(samples.begin() + qsizetype(first), samples.begin() + qsizetype(last));
            return std::pair(*lo, *hi);
        };
        for (const auto &[first, last] : std::vector<std::pair<size_t, size_t>>{
                 {0, 10}, {5, 70'000}, {123, 100'003}, {64, 128}, {99'000, 100'003}})
            QCOMPARE(env.minMax(samples, first, last), scan(first, last));
        // peu d'échantillons visibles : les échantillons eux-mêmes
        const double rate = 1000.0;
        std::vector<QPointF> pts = env.points(samples, rate, 1.0, 1.1, 800);
        QVERIFY(pts.size() <= 103);
        for (size_t i = 1; i < pts.size(); ++i)
            QVERIFY(pts[i].x() > pts[i - 1].x());
        // beaucoup : deux points par colonne, qui encadrent le signal
        pts = env.points(samples, rate, 0.0, 100.0, 500);
        QCOMPARE(pts.size(), size_t(1000));
        const auto [lo, hi] = scan(0, samples.size());
        double ymin = INFINITY, ymax = -INFINITY;
        for (const QPointF &p : pts) {
            ymin = std::min(ymin, p.y());
            ymax = std::max(ymax, p.y());
        }
        QCOMPARE(ymin, double(lo));
        QCOMPARE(ymax, double(hi));
        QVERIFY(env.points(samples, rate, 200.0, 300.0, 500).empty());
        QVERIFY(Envelope({}).points({}, rate, 0.0, 1.0, 10).empty());
    }

    // ------------------------------------------------------------ nombres
    void numbers()
    {
        QCOMPARE(fmtNum(440.123, 4, u','), u"440,1"_s);
        QCOMPARE(fmtNum(0.0022727, 4, u'.'), u"0.002273"_s);
        QCOMPARE(fmtNum(9.9996, 4, u','), u"10,00"_s);
        QCOMPARE(fmtDec(-12.34, 1, u','), u"−12,3"_s);
        QCOMPARE(fmtDec(-0.01, 1, u','), u"0,0"_s);
        QCOMPARE(csvNum(-12.34, 2, u','), u"-12,34"_s);
        QCOMPARE(fmtTime(0.0022727, u','), u"2,273 ms"_s);
        QCOMPARE(fmtTime(1.5, u'.'), u"1.5000 s"_s);
        QCOMPARE(fmtFreq(440.04, u','), u"440,0 Hz"_s);
        QCOMPARE(fmtFreq(55.123, u'.'), u"55.12 Hz"_s);
        QCOMPARE(fmtFreq(12'345.6, u','), u"12346 Hz"_s);
        QCOMPARE(fmtFreqShort(2000.0, u','), u"2 kHz"_s);
        QCOMPARE(fmtFreqShort(1500.0, u','), u"1,5 kHz"_s);
        QCOMPARE(fmtFreqShort(50.0, u'.'), u"50 Hz"_s);
        QCOMPARE(fmtPosition(0.73, u','), u"0,7300 s"_s);
        QCOMPARE(fmtDuration(3.2, u','), u"3,20 s"_s);
        QCOMPARE(fmtDuration(125.4, u','), u"2 min 05 s"_s);
        QCOMPARE(fmtDuration(0.85, u'.'), u"850 ms"_s);
        QVERIFY(std::abs(toDb(0.5) + 6.0206) < 1e-3);
        QCOMPARE(shortest(-55.5), u"-55.5"_s);
        QCOMPARE(shortest(5000.0), u"5000"_s);
    }

    // ------------------------------------------------------------ lecture des sons
    void readsStereoWav()
    {
        QTemporaryDir dir;
        std::vector<float> left(22050), right(22050);
        for (size_t i = 0; i < left.size(); ++i) {
            left[i] = float(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * double(i) / 22050.0));
            right[i] = float(0.5 * std::sin(2.0 * std::numbers::pi * 660.0 * double(i) / 22050.0));
        }
        const QString path = dir.filePath(u"stéréo test.wav"_s);
        QVERIFY(audio::writeWav(path, 22050, {left, right}));
        const audio::LoadResult r = audio::load(path);
        QVERIFY(r.audio);
        const audio::Audio &a = *r.audio;
        QCOMPARE(a.name, u"stéréo test.wav"_s);
        QCOMPARE(a.rate, 22050.0);
        QCOMPARE(a.channels.size(), size_t(2));
        QCOMPARE(a.len(), size_t(22050));
        QVERIFY(std::abs(a.duration() - 1.0) < 1e-9);
        QVERIFY(std::abs(fundamentalOf(a, audio::Channel::Left).value() - 440.0) < 0.05);
        QVERIFY(std::abs(fundamentalOf(a, audio::Channel::Right).value() - 660.0) < 0.05);
        QVERIFY(std::abs(a.peak() - 0.5) < 0.01);
    }

    void readsFloatAnd24BitWav()
    {
        // 24 bits et flottant 32 bits : fabriqués par ffmpeg s'il est installé
        QTemporaryDir dir;
        for (const auto &[name, codec] : std::vector<std::pair<QString, QString>>{
                 {u"s24.wav"_s, u"pcm_s24le"_s}, {u"f32.wav"_s, u"pcm_f32le"_s}, {u"u8.wav"_s, u"pcm_u8"_s}}) {
            const QString path = dir.filePath(name);
            const int code = QProcess::execute(u"ffmpeg"_s, {u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s, u"-f"_s,
                                                             u"lavfi"_s, u"-i"_s,
                                                             u"sine=frequency=440:sample_rate=48000:duration=1"_s,
                                                             u"-c:a"_s, codec, path});
            if (code != 0) {
                qWarning("%s : ffmpeg absent, format non essayé", qPrintable(name));
                continue;
            }
            const audio::LoadResult r = audio::load(path);
            QVERIFY2(r.audio, qPrintable(name));
            QCOMPARE(r.audio->rate, 48000.0);
            QVERIFY2(std::abs(r.audio->duration() - 1.0) < 0.01, qPrintable(name));
            QVERIFY2(std::abs(fundamentalOf(*r.audio, audio::Channel::Mix).value() - 440.0) < 0.1, qPrintable(name));
            // sinusoïde d'amplitude 1/8 (générateur « sine » de ffmpeg)
            QVERIFY2(std::abs(r.audio->peak() - 0.125) < 0.01, qPrintable(name));
        }
    }

    void rejectsNonAudioFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"texte.wav"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("pas un son");
        }
        QVERIFY(!audio::load(path).audio);
        const audio::LoadResult missing = audio::load(dir.filePath(u"absent.mp3"_s));
        QVERIFY(!missing.audio);
        QCOMPARE(missing.error, audio::LoadError::Other);
    }

    /// Formats compressés et vidéos, fabriqués par ffmpeg s'il est installé : lus par Qt Multimedia,
    /// ou par ffmpeg pour ceux qu'il ne connaît pas.
    void readsCompressedAudioAndVideos()
    {
        QTemporaryDir dir;
        const QStringList video = {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=size=160x120:rate=25"_s, u"-shortest"_s};
        const std::vector<std::pair<QString, QStringList>> formats = {
            {u"son.mp3"_s, {u"-c:a"_s, u"libmp3lame"_s}},
            {u"son.ogg"_s, {u"-c:a"_s, u"libvorbis"_s}},
            {u"son.flac"_s, {}},
            {u"son.m4a"_s, {u"-c:a"_s, u"aac"_s}},
            {u"son.opus"_s, {u"-c:a"_s, u"libopus"_s}},
            {u"son.aiff"_s, {}},
            {u"video.mp4"_s, video + QStringList{u"-c:v"_s, u"mpeg4"_s, u"-c:a"_s, u"aac"_s}},
            {u"video.avi"_s, video + QStringList{u"-c:v"_s, u"mpeg4"_s, u"-c:a"_s, u"pcm_s16le"_s}},
            {u"video.mkv"_s, video + QStringList{u"-c:v"_s, u"mpeg4"_s, u"-c:a"_s, u"libvorbis"_s}},
        };
        for (const auto &[name, args] : formats) {
            const QString path = dir.filePath(name);
            QStringList all = {u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                               u"sine=frequency=440:sample_rate=48000:duration=1"_s};
            all << args << path;
            if (QProcess::execute(u"ffmpeg"_s, all) != 0) {
                qWarning("%s : ffmpeg absent ou sans l'encodeur, format non essayé", qPrintable(name));
                continue;
            }
            const audio::LoadResult r = audio::load(path);
            QVERIFY2(r.audio, qPrintable(u"%1 : %2 %3"_s.arg(name).arg(int(r.error)).arg(r.message)));
            QCOMPARE(r.audio->rate, 48000.0);
            QVERIFY2(std::abs(r.audio->duration() - 1.0) < 0.1, qPrintable(name));
            const double f0 = fundamentalOf(*r.audio, audio::Channel::Mix).value_or(0.0);
            QVERIFY2(std::abs(f0 - 440.0) < 0.2, qPrintable(u"%1 : %2"_s.arg(name).arg(f0)));
        }
        // vidéo sans piste son
        const QString silent = dir.filePath(u"muette.mp4"_s);
        if (QProcess::execute(u"ffmpeg"_s, QStringList{u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s} + video.mid(0, 4)
                                               + QStringList{u"-t"_s, u"1"_s, u"-c:v"_s, u"mpeg4"_s, silent})
            == 0) {
            const audio::LoadResult r = audio::load(silent);
            QVERIFY(!r.audio);
            QCOMPARE(r.error, audio::LoadError::NoAudio);
        }
    }

    void examplesHaveTheAnnouncedFrequencies()
    {
        const std::vector<std::pair<audio::Example, double>> expected = {
            {audio::Example::TuningFork, 440.0}, {audio::Example::Flute, 440.0},  {audio::Example::OrganPipe, 261.63},
            {audio::Example::String, 110.0},     {audio::Example::Square, 200.0},
        };
        for (const auto &[example, f0] : expected) {
            const audio::Audio a = audio::generate(example, u"exemple"_s);
            QVERIFY(a.peak() < 1.0f);
            const double found = fundamentalOf(a, audio::Channel::Mix).value_or(0.0);
            QVERIFY2(std::abs(found - f0) < 0.2, qPrintable(u"%1 : %2"_s.arg(int(example)).arg(found)));
        }
        const audio::Audio beats = audio::generate(audio::Example::Beats, u"battements"_s);
        QVERIFY(std::abs(beats.duration() - 4.0) < 1e-9);
    }

    /// Écrit les sons d'exemple dans un dossier pour les écouter, si MEVSONO_EXEMPLES le donne.
    void writeExamples()
    {
        const QString dir = qEnvironmentVariable("MEVSONO_EXEMPLES");
        if (dir.isEmpty())
            QSKIP("MEVSONO_EXEMPLES : dossier où écrire les sons d'exemple");
        QDir().mkpath(dir);
        for (audio::Example example : audio::ALL_EXAMPLES) {
            const audio::Audio a = audio::generate(example, QString::number(int(example)));
            QVERIFY(audio::writeWav(QDir(dir).filePath(a.name + u".wav"_s), int(audio::RATE), {*a.mix}));
        }
    }

    // ------------------------------------------------------------ analyse en fond
    void analyzerComputesTheLatestRequest()
    {
        Analyzer analyzer;
        QSignalSpy spy(&analyzer, &Analyzer::updated);
        const audio::Audio a = audio::generate(audio::Example::Flute, u"flûte"_s);
        AnalysisKey key{1, audio::Channel::Mix, 0, 22050, {Window::Hann, 0}, PeakParams{}};
        QVERIFY(!analyzer.pending());
        analyzer.request(key, a.mix, a.rate);
        QVERIFY(analyzer.pending());
        QTRY_VERIFY(!analyzer.pending());
        QVERIFY(analyzer.result(1));
        QVERIFY(!analyzer.result(2));
        QVERIFY(std::abs(analyzer.result(1)->fundamental.value() - 440.0) < 0.1);
        // extrait trop court : pas de résultat, et plus de calcul en attente
        key.last = 4;
        analyzer.request(key, a.mix, a.rate);
        QTRY_VERIFY(!analyzer.pending());
        QVERIFY(!analyzer.result(1));
        analyzer.clear();
        QVERIFY(!analyzer.pending());
        // un long extrait (une minute, segments de 2¹⁸ échantillons) aussitôt remplacé par un court :
        // seul le dernier compte
        std::vector<float> minute;
        for (int i = 0; i < 20; ++i)
            minute.insert(minute.end(), a.mix->begin(), a.mix->end());
        const auto samples = std::make_shared<const std::vector<float>>(std::move(minute));
        analyzer.request({2, audio::Channel::Mix, 0, samples->size(), {Window::Hann, 0}, PeakParams{}}, samples, a.rate);
        analyzer.request({2, audio::Channel::Mix, 0, 4096, {Window::Hann, 0}, PeakParams{}}, samples, a.rate);
        QTRY_VERIFY(!analyzer.pending());
        QVERIFY(analyzer.result(2));
        QCOMPARE(analyzer.result(2)->spectrum.segmentLen, size_t(4096));
    }

    // ------------------------------------------------------------ écoute
    void copiesTheRangeOnEveryChannelWithFades()
    {
        std::vector<float> samples(10'000);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = (float(i % 100) - 50.0f) / 100.0f;
        Playback p = playback(samples, 1000.0, 3000, 48000.0, 48000.0);
        std::vector<float> out(2 * 2500);
        p.fill(out.data(), 2500, 2);
        std::vector<float> left;
        for (size_t i = 0; i < out.size(); i += 2) {
            QCOMPARE(out[i], out[i + 1]);
            left.push_back(out[i]);
        }
        // fondu d'entrée de 5 ms (240 échantillons), puis le signal lui-même, et un fondu avant la fin
        QCOMPARE(left[0], 0.0f);
        for (size_t i = 1; i < 240; ++i)
            QVERIFY(std::abs(left[i]) <= std::abs(samples[1000 + i]));
        for (size_t i = 240; i < 1760; ++i)
            QCOMPARE(left[i], samples[1000 + i]);
        QVERIFY(std::abs(left[1999]) < 0.01);
        for (size_t i = 2000; i < left.size(); ++i)
            QCOMPARE(left[i], 0.0f);
        QVERIFY(p.shared().done);
        QCOMPARE(p.position(), 3000.0);
    }

    void resamplesToTheOutputRate()
    {
        const auto tone = [](double rate, size_t n) {
            std::vector<float> v(n);
            for (size_t i = 0; i < n; ++i)
                v[i] = float(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * double(i) / rate));
            return v;
        };
        const std::vector<float> samples = tone(44100.0, 44100);
        Playback p = playback(samples, 0.0, samples.size(), 44100.0, 48000.0);
        std::vector<float> out(4800);
        p.fill(out.data(), 4800, 1);
        const std::vector<float> expected = tone(48000.0, 4800);
        float error = 0.0f;
        for (size_t i = 240; i < out.size(); ++i)
            error = std::max(error, std::abs(out[i] - expected[i]));
        QVERIFY2(error < 0.003f, qPrintable(QString::number(error)));
        QVERIFY(std::abs(p.position() - 4410.0) < 1e-6);
    }

    void pauseFadesOutThenFallsSilent()
    {
        const std::vector<float> samples(48000, 0.5f);
        Playback p = playback(samples, 0.0, samples.size(), 48000.0, 48000.0);
        std::vector<float> out(1000);
        p.fill(out.data(), 1000, 1);
        QCOMPARE(out[999], 0.5f);
        p.shared().stop = true;
        p.fill(out.data(), 1000, 1);
        QVERIFY(out[0] > 0.49f && out[120] < 0.26f && out[120] > 0.24f);
        for (size_t i = 240; i < out.size(); ++i)
            QCOMPARE(out[i], 0.0f);
        QVERIFY(p.shared().done);
        QCOMPARE(p.position(), 1240.0);
    }

    void playPauseResumeStop()
    {
        const audio::Audio a = audio::generate(audio::Example::Flute, u"flûte"_s);
        const Samples samples = a.signal(audio::Channel::Mix);
        const Clip whole{1, audio::Channel::Mix, 0, a.len()};
        Player player(true);
        QVERIFY(!player.position() && !player.playing());
        QVERIFY(!player.play(whole, samples, a.rate));
        QVERIFY(player.playing() && player.busy());
        wait(player, [&](const Player &p) { return *p.position() > 0.1 * a.rate; });
        // pause : la position reste fixe, et la lecture reprend là
        player.pause();
        const double paused = player.position().value();
        QVERIFY(player.paused() && paused < 0.5 * a.rate);
        wait(player, [](const Player &p) { return !p.busy(); });
        QCOMPARE(player.position(), paused);
        QVERIFY(!player.play(whole, samples, a.rate));
        wait(player, [&](const Player &p) { return *p.position() > paused + 0.05 * a.rate; });
        // arrêt : plus de position, la lecture suivante part du début
        player.stop();
        QVERIFY(!player.position() && !player.clip());
        QVERIFY(!player.play(whole, samples, a.rate));
        QVERIFY(*player.position() < 0.05 * a.rate);
        // un autre extrait remplace le premier, et la lecture s'arrête seule à sa fin
        const auto first = size_t(a.rate);
        const Clip extract{1, audio::Channel::Mix, first, first + 4410};
        QVERIFY(!player.play(extract, samples, a.rate));
        QCOMPARE(*player.clip(), extract);
        QVERIFY(*player.position() >= a.rate);
        wait(player, [](const Player &p) { return !p.playing(); });
        QVERIFY(!player.position());
    }

    /// Joue une seconde de la flûte sur la vraie sortie audio, si MEVSONO_CARTE_SON est défini.
    void playsOnTheSoundCard()
    {
        if (qEnvironmentVariableIsEmpty("MEVSONO_CARTE_SON"))
            QSKIP("MEVSONO_CARTE_SON : joue un son sur la carte son");
        const audio::Audio a = audio::generate(audio::Example::Flute, u"flûte"_s);
        Player player;
        const Clip clip{1, audio::Channel::Mix, 0, size_t(a.rate)};
        QElapsedTimer clock;
        clock.start();
        const auto error = player.play(clip, a.mix, a.rate);
        QVERIFY2(!error, qPrintable(error ? error->message : QString()));
        // position entendue au fil du temps, comparée à l'horloge
        while (player.playing()) {
            QCoreApplication::processEvents();
            if (const auto e = player.update())
                QFAIL(qPrintable(*e));
            if (const auto at = player.position())
                qInfo("%6.3f s : position %6.3f s", double(clock.nsecsElapsed()) / 1e9, *at / a.rate);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            QVERIFY(clock.elapsed() < 5000);
        }
    }

    // ------------------------------------------------------------ réglages
    void settingsRoundTrip()
    {
        Settings s;
        s.lang = Lang::En;
        s.theme = Qt::ColorScheme::Dark;
        s.expert = true;
        s.channel = audio::Channel::Right;
        s.window = Window::BlackmanHarris;
        s.fftSize = 16384;
        s.db = true;
        s.logFreq = true;
        s.fmax = 0.0;
        s.thresholdDb = -55.5;
        s.harmonics = 12;
        s.snapZero = false;
        s.harmonicMarks = true;
        s.uiZoom = 1.2;
        s.lastDir = u"/home/élève/TP = sons"_s;
        s.windowSize = QSize(1400, 900);
        s.maximized = true;
        QCOMPARE(Settings::parse(s.toText()), s);
        QCOMPARE(Settings::parse(Settings().toText()), Settings());
        // thème du bureau et fenêtre à sa taille normale, tant que l'élève n'a rien choisi
        QCOMPARE(Settings().theme, Qt::ColorScheme::Unknown);
        QVERIFY(!Settings().maximized);
        QCOMPARE(Settings::parse(u"theme = light\n"_s).theme, Qt::ColorScheme::Light);
    }

    void settingsTolerateBadInput()
    {
        const Settings s = Settings::parse(
            u"n'importe quoi\nlanguage = klingon\nwindow = triangle\nfft_size = 1000\nmax_frequency = 3\n"
            u"peak_threshold_db = 5\nharmonics = 999\nui_zoom = 40\nwindow_width = 10\nwindow_height = 600\n  channel = left  \n"_s);
        const Settings d;
        QCOMPARE(s.lang, std::nullopt);
        QCOMPARE(Settings::parse(u"theme = violet\n"_s).theme, Qt::ColorScheme::Unknown);
        QCOMPARE(s.window, d.window);
        QCOMPARE(s.fftSize, d.fftSize);
        QCOMPARE(s.fmax, d.fmax);
        QCOMPARE(s.thresholdDb, d.thresholdDb);
        QCOMPARE(s.harmonics, 40);
        QCOMPARE(s.uiZoom, 1.0);
        QCOMPARE(s.windowSize, std::nullopt);
        QCOMPARE(s.channel, audio::Channel::Left);
    }

    void adminFileOverridesUserFile()
    {
        QTemporaryDir dir;
        const QString user = dir.filePath(u"user.ini"_s), admin = dir.filePath(u"admin.ini"_s);
        const auto write = [](const QString &path, const QByteArray &text) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text);
        };
        write(user, "language = en\nmode = expert\nwindow = hamming\nharmonics = 8\n");
        write(admin, "# imposé par le lycée\nlanguage = fr\nmode = simple\n");
        const Settings s = Settings::load({user, admin});
        QCOMPARE(s.lang, Lang::Fr); // imposés
        QCOMPARE(s.expert, false);
        QCOMPARE(s.window, Window::Hamming); // choix de l'utilisateur conservés
        QCOMPARE(s.harmonics, 8);
        QCOMPARE(Settings::load({dir.filePath(u"absent.ini"_s), {}}), Settings());
    }

    // ------------------------------------------------------------ langues
    void languageCodes()
    {
        QCOMPARE(langFromCode(u"fr_FR.UTF-8"_s), Lang::Fr);
        QCOMPARE(langFromCode(u"en-GB"_s), Lang::En);
        QCOMPARE(langFromCode(u"de_DE"_s), std::nullopt);
        QCOMPARE(langFromCode(u"C"_s), std::nullopt);
        for (Lang lang : ALL_LANGS)
            QCOMPARE(langFromCode(langCode(lang)), lang);
        QCOMPARE(firstTranslated({u"ru"_s, u"it"_s, u"en"_s}), Lang::En);
        QCOMPARE(firstTranslated({}), std::nullopt);
    }

    void numbersFollowTheLanguage()
    {
        QCOMPARE(texts(Lang::En).decimal, u'.');
        QCOMPARE(texts(Lang::Fr).decimal, u',');
        QCOMPARE(texts(Lang::Fr).fftPoints(32768), u"32 768 points"_s);
        QCOMPARE(texts(Lang::En).fftPoints(1048576), u"1,048,576 points"_s);
        QCOMPARE(texts(Lang::En).fftPoints(512), u"512 points"_s);
    }

    // ------------------------------------------------------------ notice
    void writesEveryHelpFileWithVersion()
    {
        QTemporaryDir dir;
        const SettingsFiles files{dir.filePath(u"mevsono.ini"_s), {}};
        // pages refaites à chaque ouverture : dans le dossier temporaire, pas dans celui des réglages
        const QString helpDir = QDir(QDir::tempPath()).filePath(help::DIR_NAME);
        QString error;
        const QString page = help::write(Lang::En, files, &error);
        QCOMPARE(page, QDir(helpDir).filePath(u"en.html"_s));
        QCOMPARE(help::write(Lang::Fr, files, &error), QDir(helpDir).filePath(u"fr.html"_s));
        for (const QString &name : help::files())
            QVERIFY2(QFileInfo(QDir(helpDir).filePath(name)).isFile(), qPrintable(name));
        const QString html = readText(page);
        QVERIFY(html.contains(QStringLiteral(MEVSONO_VERSION)) && !html.contains(u"{{version}}"_s));
        QVERIFY(html.contains(u"help.css"_s));
    }

    /// Chaque image des pages est livrée, et chaque lien interne mène à une partie de la page.
    void helpPagesAreComplete()
    {
        const QStringList files = help::files();
        for (const QString &page : {u"fr.html"_s, u"en.html"_s}) {
            const QString html = readText(u":/help/"_s + page);
            QVERIFY(!html.isEmpty());
            const auto attributes = [&](const QString &prefix) {
                QStringList out;
                for (qsizetype i = html.indexOf(prefix); i >= 0; i = html.indexOf(prefix, i + 1)) {
                    const qsizetype start = i + prefix.size();
                    out << html.mid(start, html.indexOf(u'"', start) - start);
                }
                return out;
            };
            for (const QString &image : attributes(u"src=\""_s))
                QVERIFY2(files.contains(image), qPrintable(page + u" : image "_s + image));
            for (const QString &anchor : attributes(u"href=\"#"_s))
                QVERIFY2(html.contains(u"id=\"%1\""_s.arg(anchor)), qPrintable(page + u" : #"_s + anchor));
            for (const QString &other : attributes(u"href=\""_s)) {
                if (other.endsWith(u".html"_s) || other.endsWith(u".css"_s))
                    QVERIFY2(files.contains(other), qPrintable(page + u" : "_s + other));
            }
            // la notice donne les noms de fichiers du programme
            QVERIFY2(html.contains(u"mevsono.ini"_s), qPrintable(page));
        }
    }

    // ------------------------------------------------------------ calculs de l'interface
    void loudestWindowFindsTheSound()
    {
        std::vector<float> samples(44100 * 3, 0.0f);
        const std::vector<float> tone = tones({{440.0, 0.5}}, 0.5);
        std::ranges::copy(tone, samples.begin() + 44100);
        const double start = loudestWindow(samples, RATE, 0.5);
        QVERIFY2(std::abs(start - 1.0) < 0.011, qPrintable(QString::number(start)));
        QCOMPARE(loudestWindow(std::span(samples).first(100), RATE, 0.5), 0.0);
    }

    void spectrumPointsKeepThePeaks()
    {
        const std::vector<float> x = tones({{1000.0, 0.5}}, 1.0);
        const auto sp = spectrum(x, RATE, {Window::Hann, 0});
        for (bool log : {false, true}) {
            const std::vector<QPointF> pts = spectrumPoints(*sp, {}, 20.0, 20000.0, 400, log);
            QVERIFY(pts.size() <= 400);
            QPointF top;
            for (const QPointF &p : pts) {
                if (p.y() > top.y())
                    top = p;
            }
            QVERIFY(std::abs(top.x() - 1000.0) < 1.0 && std::abs(top.y() - 0.5) < 0.03);
        }
        QVERIFY(spectrumPoints(*sp, {}, 30000.0, 40000.0, 400, false).empty());
    }

    void spectrumPointsReachThePeakTops()
    {
        // son à mi-chemin de deux raies (Δf = 44 100 / 8 192 Hz) : aucune raie n'atteint son amplitude
        const std::vector<float> x = tones({{186.5 * RATE / 8192.0, 0.5}}, 0.1);
        const auto sp = spectrum(x, RATE, {Window::Hann, 0});
        QCOMPARE(sp->fftLen, size_t(8192));
        QVERIFY(sp->maxAmplitude() < 0.49);
        const std::vector<Peak> found = peaks(*sp, Window::Hann, PeakParams{});
        for (size_t columns : {size_t(2000), size_t(50)}) {
            const std::vector<QPointF> pts = spectrumPoints(*sp, found, 20.0, 5000.0, columns, false);
            QVERIFY(std::ranges::is_sorted(pts, {}, &QPointF::x));
            // la courbe passe par chaque pic affiné, et son sommet est l'amplitude du son
            for (const Peak &p : found)
                QVERIFY(std::ranges::find(pts, QPointF(p.freq, p.amplitude)) != pts.end());
            const double top = std::ranges::max(pts, {}, &QPointF::y).y();
            QVERIFY2(std::abs(top - 0.5) < 0.005, qPrintable(QString::number(top)));
        }
    }

    void nearestPeakOnBothAxes()
    {
        const std::vector<Peak> p = {{100.0, 1.0}, {1000.0, 0.5}};
        // 500 Hz : plus près de 100 Hz sur l'axe linéaire, de 1000 Hz sur l'axe logarithmique
        QCOMPARE(nearestPeak(p, 500.0, false)->freq, 100.0);
        QCOMPARE(nearestPeak(p, 500.0, true)->freq, 1000.0);
        QVERIFY(!nearestPeak({}, 500.0, false));
    }

    void logGridMarksAreRound()
    {
        std::vector<double> values;
        for (double m : logGridMarks(1.3, 3.7))
            values.push_back(std::round(std::pow(10.0, m)));
        for (double v : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0})
            QVERIFY2(std::ranges::find(values, v) != values.end(), qPrintable(QString::number(v)));
    }

    void csvExports()
    {
        const Tr &tr = texts(Lang::Fr);
        const std::vector<float> x = tones({{440.0, 0.5}}, 0.1);
        const auto sp = spectrum(x, RATE, {Window::Hann, 1024});
        QString csv = spectrumCsv(tr, *sp);
        QVERIFY2(csv.startsWith(u"f (Hz);A;A (dB)\n0,0000;"_s), qPrintable(csv.left(40)));
        QCOMPARE(csv.count(u'\n'), 1 + 513);
        csv = extractCsv(texts(Lang::En), std::vector<float>{0.25f, -0.5f}, 44100, RATE);
        QCOMPARE(csv, u"t (s),signal\n1.000000,0.250000\n1.000023,-0.500000\n"_s);
        const std::vector<Harmonic> h = {{1, 440.0, Peak{440.0, 0.5}}, {2, 880.0, std::nullopt}};
        const QStringList table = harmonicsTable(tr, h, {440.0, 0.5}).split(u'\n');
        QCOMPARE(table[1], u"1\t440,00\t1,0000\t-6,02\t100,00"_s);
        QCOMPARE(table[2], u"2\t\t\t\t"_s);
    }

    void csvExtensionAddedWhenMissing()
    {
        QTemporaryDir dir;
        QCOMPARE(withCsvExtension(dir.filePath(u"spectre"_s)), dir.filePath(u"spectre.csv"_s));
        QCOMPARE(withCsvExtension(dir.filePath(u"spectre.txt"_s)), dir.filePath(u"spectre.txt"_s));
        QFile old(dir.filePath(u"ancien.csv"_s));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.close();
        QCOMPARE(withCsvExtension(dir.filePath(u"ancien"_s)), dir.filePath(u"ancien"_s));
    }

    void sessionExtractDependsOnTheMode()
    {
        // mode simplifié : les 80 % du milieu du son ; mode expert : 0,5 s de la partie la plus forte
        Session s;
        s.setAudio(audio::generate(audio::Example::Flute, u"flûte"_s));
        QVERIFY(std::abs(s.extract[0] - 0.3) < 1e-9 && std::abs(s.extract[1] - 2.7) < 1e-9);
        // on écoute tout le son pour mesurer la période, l'extrait pour étudier les fréquences
        QCOMPARE(s.tool, Tool::Period);
        QCOMPARE(s.listenChoice(), Listen::Sound);
        s.tool = Tool::Extract;
        QCOMPARE(s.listenChoice(), Listen::Extract);
        QCOMPARE(s.clip()->first, size_t(std::round(0.3 * audio::RATE)));
        s.params.expert = true;
        s.params.window = Window::FlatTop;
        s.setAudio(audio::generate(audio::Example::Flute, u"flûte"_s));
        QVERIFY(std::abs(s.extract[1] - s.extract[0] - 0.5) < 1e-9);
        QCOMPARE(s.loaded->id, quint64(2));
        QCOMPARE(s.clip()->first, size_t(0)); // tout le son
        QCOMPARE(s.spectrumParams().window, Window::FlatTop);
        // en mode simplifié, l'analyse prend les valeurs par défaut
        s.params.expert = false;
        QCOMPARE(s.spectrumParams().window, Window::Hann);
        QCOMPARE(s.channel(), audio::Channel::Mix);
    }
};

QTEST_GUILESS_MAIN(Tests)
#include "tests.moc"
