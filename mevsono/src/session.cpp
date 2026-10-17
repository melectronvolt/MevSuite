#include "session.h"

#include "format.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Indice d'échantillon d'un instant (secondes), arrondi et borné à `len`.
size_t sampleIndex(double t, double rate, size_t len)
{
    const double x = std::round(t * rate);
    if (!(x > 0.0))
        return 0;
    return x >= double(len) ? len : size_t(x);
}

/// Séparateur des colonnes d'un CSV : point-virgule avec la virgule décimale, sinon virgule.
QChar csvSeparator(QChar dec)
{
    return dec == u',' ? u';' : u',';
}

} // namespace

const Envelope &Loaded::envelope(audio::Channel channel) const
{
    auto &slot = m_envelopes[size_t(channel)];
    if (!slot)
        slot = std::make_shared<const Envelope>(*audio.signal(channel));
    return *slot;
}

void Session::setAudio(audio::Audio audio)
{
    const quint64 id = nextId++;
    const double duration = audio.duration();
    if (params.expert) {
        const double length = std::min(EXPERT_EXTRACT, duration);
        const double start = loudestWindow(*audio.signal(audio::Channel::Mix), audio.rate, length);
        extract = {start, start + length};
    } else {
        const double margin = (1.0 - SIMPLE_EXTRACT) / 2.0 * duration;
        extract = {margin, duration - margin};
    }
    period.reset();
    manualF0.reset();
    const double peak = std::max(double(audio.peak()), 1e-6);
    Loaded l;
    l.audio = std::move(audio);
    l.id = id;
    l.peak = peak;
    loaded = std::move(l);
}

Settings Session::active() const
{
    if (params.expert)
        return params;
    Settings s;
    s.lang = params.lang;
    s.expert = false;
    return s;
}

audio::Channel Session::channel() const
{
    if (loaded && loaded->audio.channels.size() >= 2)
        return active().channel;
    return audio::Channel::Mix;
}

std::optional<std::pair<size_t, size_t>> Session::extractRange() const
{
    if (!loaded)
        return std::nullopt;
    const auto [a, b] = sorted(extract);
    const size_t len = loaded->audio.len();
    return std::pair(sampleIndex(a, loaded->audio.rate, len), sampleIndex(b, loaded->audio.rate, len));
}

dsp::SpectrumParams Session::spectrumParams() const
{
    const Settings p = active();
    return {p.window, p.fftSize};
}

std::optional<AnalysisKey> Session::analysisKey() const
{
    const auto range = extractRange();
    if (!range)
        return std::nullopt;
    dsp::PeakParams peaks;
    peaks.thresholdDb = active().thresholdDb;
    peaks.fmin = FMIN;
    return AnalysisKey{loaded->id, channel(), range->first, range->second, spectrumParams(), peaks};
}

double Session::fmaxDisplay() const
{
    const double nyquist = loaded ? loaded->audio.rate / 2.0 : 22050.0;
    const double fmax = active().fmax;
    return fmax > 0.0 ? std::min(fmax, nyquist) : nyquist;
}

Listen Session::listenChoice() const
{
    if (params.expert)
        return listen;
    return tool == Tool::Period ? Listen::Sound : Listen::Extract;
}

std::optional<Clip> Session::clip() const
{
    if (!loaded)
        return std::nullopt;
    if (listenChoice() == Listen::Sound)
        return Clip{loaded->id, channel(), 0, loaded->audio.len()};
    const auto range = extractRange();
    if (!range)
        return std::nullopt;
    return Clip{loaded->id, channel(), range->first, range->second};
}

std::pair<double, double> sorted(const Cursors &c)
{
    return c[0] <= c[1] ? std::pair(c[0], c[1]) : std::pair(c[1], c[0]);
}

double loudestWindow(std::span<const float> samples, double rate, double length)
{
    const auto n = size_t(std::max(length * rate, 0.0));
    const size_t len = samples.size();
    if (n == 0 || n >= len)
        return 0.0;
    const size_t step = std::max<size_t>(size_t(0.01 * rate), 1);
    const size_t count = (len - n) / step + 1;
    // Sommes cumulées des carrés, notées seulement aux débuts et aux fins des portions essayées.
    std::vector<double> atStart(count), atEnd(count);
    double prefix = 0.0;
    size_t nextStart = 0, nextEnd = 0;
    for (size_t i = 0; i <= len; ++i) {
        while (nextStart < count && nextStart * step == i)
            atStart[nextStart++] = prefix;
        while (nextEnd < count && nextEnd * step + n == i)
            atEnd[nextEnd++] = prefix;
        if (i < len)
            prefix += double(samples[i]) * double(samples[i]);
    }
    size_t best = 0;
    for (size_t k = 1; k < count; ++k) {
        // à égalité, la dernière portion l'emporte (comme `max_by` en Rust)
        if (atEnd[k] - atStart[k] >= atEnd[best] - atStart[best])
            best = k;
    }
    return double(best * step) / rate;
}

std::vector<QPointF> spectrumPoints(const dsp::Spectrum &sp, std::span<const dsp::Peak> peaks, double f0, double f1,
                                    size_t columns, bool log)
{
    const double df = sp.df();
    const size_t len = sp.amplitude.size();
    const auto toIndex = [len](double x) { return !(x > 0.0) ? size_t(0) : x >= double(len) ? len : size_t(x); };
    const size_t first = std::min(std::max<size_t>(toIndex(std::floor(std::max(f0, 0.0) / df)), log ? 1 : 0), len);
    const size_t last = std::min(toIndex(std::ceil(std::max(f1, 0.0) / df)) + 2, len);
    if (last <= first)
        return {};
    const size_t count = last - first;
    std::vector<QPointF> out;
    if (count <= 2 * std::max<size_t>(columns, 1)) {
        out.reserve(count + peaks.size());
        for (size_t k = first; k < last; ++k)
            out.emplace_back(sp.freq(k), sp.amplitude[k]);
    } else {
        out.reserve(columns + peaks.size());
        const double logLo = std::log10(std::max(f0, 1e-9));
        const auto columnOf = [&](size_t k) {
            const double f = std::max(sp.freq(k), 1e-9);
            const double x = log ? (std::log10(f) - logLo) / (std::log10(f1) - logLo) : (f - f0) / (f1 - f0);
            return size_t(std::clamp(x * double(columns), 0.0, double(columns) - 1.0));
        };
        // (colonne, raie la plus haute de la colonne)
        std::optional<std::pair<size_t, size_t>> current;
        for (size_t k = first; k < last; ++k) {
            const size_t c = columnOf(k);
            if (current && current->first == c) {
                if (sp.amplitude[k] > sp.amplitude[current->second])
                    current->second = k;
            } else {
                if (current)
                    out.emplace_back(sp.freq(current->second), sp.amplitude[current->second]);
                current = std::pair(c, k);
            }
        }
        if (current)
            out.emplace_back(sp.freq(current->second), sp.amplitude[current->second]);
    }
    // sommet affiné de chaque pic : souvent entre deux raies, plus haut qu'elles ; la courbe passe
    // ainsi par le rond qui marque le pic
    const auto bins = std::ptrdiff_t(out.size());
    const double lo = out.front().x(), hi = out.back().x();
    for (const dsp::Peak &p : peaks) {
        if (p.freq > lo && p.freq < hi)
            out.emplace_back(p.freq, p.amplitude);
    }
    std::inplace_merge(out.begin(), out.begin() + bins, out.end(),
                       [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    return out;
}

std::optional<dsp::Peak> nearestPeak(std::span<const dsp::Peak> peaks, double f, bool log)
{
    const auto axis = [log](double x) { return log ? std::log10(std::max(x, 1e-3)) : x; };
    std::optional<dsp::Peak> best;
    for (const dsp::Peak &p : peaks) {
        if (!best || std::abs(axis(p.freq) - axis(f)) < std::abs(axis(best->freq) - axis(f)))
            best = p;
    }
    return best;
}

std::vector<double> logGridMarks(double lo, double hi)
{
    std::vector<double> marks;
    for (int decade = int(std::floor(lo)) - 1; decade <= int(std::ceil(hi)); ++decade) {
        for (double k : {1.0, 2.0, 5.0}) {
            const double value = std::log10(k * std::pow(10.0, decade));
            if (value >= lo && value <= hi)
                marks.push_back(value);
        }
    }
    return marks;
}

QString soundInfo(const Tr &tr, const audio::Audio &audio)
{
    const QChar d = tr.decimal;
    const size_t n = audio.channels.size();
    const QString channels = n == 1 ? tr.mono : n == 2 ? tr.stereo : tr.channelsN(n);
    return u"%1 kHz · %2 · %3"_s.arg(fmtNum(audio.rate / 1000.0, 3, d), channels, fmtDuration(audio.duration(), d));
}

QString harmonicsTable(const Tr &tr, std::span<const dsp::Harmonic> harmonics)
{
    const QChar d = tr.decimal;
    // l'amplitude relative est en pour cent (le signe % est dans les cases du tableau affiché)
    QString out = QStringList{tr.colRank, tr.colFreq, tr.colRatio, tr.colAmp, tr.colRel + u" (%)"_s}.join(u'\t') + u'\n';
    if (harmonics.empty())
        return out;
    // rapport à la fondamentale : son pic s'il y en a un, sinon sa fréquence attendue (fondamentale
    // absente) ; amplitude relative seulement avec le pic de la fondamentale
    const std::optional<dsp::Peak> &first = harmonics.front().peak;
    const double f1 = first ? first->freq : harmonics.front().expected;
    for (const dsp::Harmonic &h : harmonics) {
        if (const auto &p = h.peak) {
            out += u"%1\t%2\t%3\t%4\t%5\n"_s.arg(QString::number(h.rank), csvNum(p->freq, 2, d), csvNum(p->freq / f1, 4, d),
                                                 csvNum(toDb(p->amplitude), 2, d),
                                                 first ? csvNum(100.0 * p->amplitude / first->amplitude, 2, d) : QString());
        } else {
            out += u"%1\t\t\t\t\n"_s.arg(h.rank);
        }
    }
    return out;
}

QString spectrumCsv(const Tr &tr, const dsp::Spectrum &sp)
{
    const QChar d = tr.decimal;
    const QChar s = csvSeparator(d);
    QString out = tr.csvFreq + s + tr.csvAmplitude + s + tr.csvDb + u'\n';
    out.reserve(qsizetype(sp.amplitude.size()) * 32);
    for (size_t k = 0; k < sp.amplitude.size(); ++k) {
        const double a = sp.amplitude[k];
        out += csvNum(sp.freq(k), 4, d) + s + csvNum(a, 8, d) + s + csvNum(toDb(a), 2, d) + u'\n';
    }
    return out;
}

QString extractCsv(const Tr &tr, std::span<const float> samples, size_t start, double rate)
{
    const QChar d = tr.decimal;
    const QChar s = csvSeparator(d);
    QString out = tr.csvTime + s + tr.csvSignal + u'\n';
    out.reserve(qsizetype(samples.size()) * 22);
    for (size_t i = 0; i < samples.size(); ++i)
        out += csvNum(double(start + i) / rate, 6, d) + s + csvNum(double(samples[i]), 6, d) + u'\n';
    return out;
}

QString withCsvExtension(const QString &path)
{
    // « .csv » ajouté au nom tapé sans extension ; si ce fichier existe déjà, le programme demande
    // avant de le remplacer (le dialogue a confirmé le nom tapé, pas celui-ci)
    return QFileInfo(path).suffix().isEmpty() ? path + u".csv"_s : path;
}
