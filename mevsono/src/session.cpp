#include "session.h"

#include "format.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

/// Indice d'échantillon d'un instant (secondes), arrondi et borné à `len`.
size_t indice_echantillon(double t, double rate, size_t len)
{
    const double x = std::round(t * rate);
    if (!(x > 0.0))
        return 0;
    return x >= double(len) ? len : size_t(x);
}

/// Séparateur des colonnes d'un CSV : point-virgule avec la virgule décimale, sinon virgule.
QChar separateur_csv(QChar dec)
{
    return dec == u',' ? u';' : u',';
}

audio::Channel voie_etudiee(const audio::Audio &son, const Settings &reglages)
{
    return reglages.expert && son.channels.size() >= 2 ? reglages.channel : audio::Channel::Mix;
}

} // namespace

ApercuSon ApercuSon::prepare(const audio::Audio &son, const Settings &reglages)
{
    ApercuSon apercu;
    apercu.expert = reglages.expert;
    apercu.voie = voie_etudiee(son, reglages);
    const auto &signal = *son.signal(apercu.voie);
    apercu.enveloppe = std::make_shared<const Envelope>(signal);
    const double duree = son.duree();
    if (reglages.expert) {
        const double longueur = std::min(EXPERT_EXTRACT, duree);
        const double debut = fenetre_la_plus_forte(signal, son.rate, longueur);
        apercu.extrait = {debut, debut + longueur};
    } else {
        const double marge = (1.0 - SIMPLE_EXTRACT) * duree / 2.0;
        apercu.extrait = {marge, duree - marge};
    }
    return apercu;
}

bool ApercuSon::correspond(const audio::Audio &son, const Settings &reglages) const
{
    return expert == reglages.expert && voie == voie_etudiee(son, reglages);
}

Loaded::Loaded(audio::Audio son, quint64 numero, ApercuSon apercu)
    : audio(std::move(son)), id(numero)
{
    m_envelopes[size_t(apercu.voie)] = std::move(apercu.enveloppe);
}

const Envelope &Loaded::enveloppe(audio::Channel channel) const
{
    auto &slot = m_envelopes[size_t(channel)];
    if (!slot)
        slot = std::make_shared<const Envelope>(*audio.signal(channel));
    return *slot;
}

void Session::installe_son(audio::Audio audio)
{
    auto apercu = ApercuSon::prepare(audio, params);
    installe_son(std::move(audio), std::move(apercu));
}

void Session::installe_son(audio::Audio audio, ApercuSon apercu)
{
    extract = apercu.extrait;
    period.reset();
    manualF0.reset();
    loaded.emplace(std::move(audio), nextId++, std::move(apercu));
}

Settings Session::reglages_actifs() const
{
    if (params.expert)
        return params;
    Settings s;
    s.lang = params.lang;
    s.expert = false;
    return s;
}

audio::Channel Session::canal() const
{
    return loaded ? voie_etudiee(loaded->audio, params) : audio::Channel::Mix;
}

std::optional<std::pair<size_t, size_t>> Session::intervalle_extrait() const
{
    if (!loaded)
        return std::nullopt;
    const auto [a, b] = trie(extract);
    const size_t len = loaded->audio.longueur();
    return std::pair(indice_echantillon(a, loaded->audio.rate, len), indice_echantillon(b, loaded->audio.rate, len));
}

dsp::SpectrumParams Session::parametres_spectre() const
{
    const Settings p = reglages_actifs();
    return {p.window, p.fftSize};
}

std::optional<double> Session::fondamentale(const dsp::Analysis *analyse) const
{
    if (manualF0)
        return manualF0;
    return analyse ? analyse->fundamental : std::nullopt;
}

std::vector<dsp::Harmonic> Session::harmoniques(const dsp::Analysis *analyse) const
{
    const auto f0 = fondamentale(analyse);
    if (!analyse || !f0)
        return {};
    return dsp::harmoniques(analyse->peaks, *f0, analyse->spectrum.pas_frequence(), size_t(reglages_actifs().harmonics));
}

std::optional<AnalysisKey> Session::cle_analyse() const
{
    const auto range = intervalle_extrait();
    if (!range)
        return std::nullopt;
    dsp::PeakParams peaks;
    peaks.thresholdDb = reglages_actifs().thresholdDb;
    peaks.fmin = FMIN;
    return AnalysisKey{loaded->id, canal(), range->first, range->second, parametres_spectre(), peaks};
}

double Session::fmax_affichee() const
{
    const double nyquist = loaded ? loaded->audio.rate / 2.0 : 22050.0;
    const double fmax = reglages_actifs().fmax;
    return fmax > 0.0 ? std::min(fmax, nyquist) : nyquist;
}

Listen Session::choix_ecoute() const
{
    if (params.expert)
        return listen;
    return tool == Tool::Period ? Listen::Sound : Listen::Extract;
}

std::optional<Clip> Session::extrait_en_lecture() const
{
    if (!loaded)
        return std::nullopt;
    if (choix_ecoute() == Listen::Sound)
        return Clip{loaded->id, canal(), 0, loaded->audio.longueur()};
    const auto range = intervalle_extrait();
    if (!range)
        return std::nullopt;
    return Clip{loaded->id, canal(), range->first, range->second};
}

std::pair<double, double> trie(const Cursors &c)
{
    return c[0] <= c[1] ? std::pair(c[0], c[1]) : std::pair(c[1], c[0]);
}

double fenetre_la_plus_forte(std::span<const float> signal, double cadence, double duree)
{
    if (!(cadence > 0.0) || !std::isfinite(cadence))
        return 0.0;
    const size_t longueur = indice_echantillon(duree, cadence, signal.size());
    if (longueur == 0 || longueur == signal.size())
        return 0.0;

    const auto energie = [&](size_t debut, size_t nombre) {
        double somme = 0.0;
        for (double valeur : signal.subspan(debut, nombre))
            somme += valeur * valeur;
        return somme;
    };
    const size_t pas = std::max<size_t>(1, indice_echantillon(0.010, cadence, signal.size()));
    const size_t dernierDebut = signal.size() - longueur;
    size_t meilleurDebut = 0;
    double energieCourante = energie(0, longueur);
    double maximum = energieCourante;
    for (size_t debut = 0; debut < dernierDebut;) {
        // La dernière fenêtre atteint toujours la fin du son. Entre deux
        // fenêtres, seuls les échantillons entrants ou sortants changent la somme.
        const size_t suivant = debut + std::min(pas, dernierDebut - debut);
        const size_t nombre = std::min(suivant - debut, longueur);
        energieCourante += energie(suivant + longueur - nombre, nombre) - energie(debut, nombre);
        if (energieCourante >= maximum) {
            maximum = energieCourante;
            meilleurDebut = suivant;
        }
        debut = suivant;
    }
    return double(meilleurDebut) / cadence;
}

std::vector<QPointF> points_spectre(const dsp::Spectrum &sp, std::span<const dsp::Peak> peaks, double f0, double f1,
                                    size_t columns, bool log)
{
    if (columns == 0 || !std::isfinite(f0) || !std::isfinite(f1) || f1 <= f0 || (log && f0 <= 0.0))
        return {};
    const double df = sp.pas_frequence();
    const size_t len = sp.amplitude.size();
    const auto toIndex = [len](double x) { return !(x > 0.0) ? size_t(0) : x >= double(len) ? len : size_t(x); };
    const size_t first = std::min(std::max<size_t>(toIndex(std::floor(std::max(f0, 0.0) / df)), log ? 1 : 0), len);
    const size_t last = std::min(toIndex(std::ceil(std::max(f1, 0.0) / df)) + 2, len);
    if (last <= first)
        return {};
    const size_t count = last - first;
    std::vector<QPointF> out;
    if (columns >= (count + 1) / 2) {
        out.reserve(count + peaks.size());
        for (size_t k = first; k < last; ++k)
            out.emplace_back(sp.frequence(k), sp.amplitude[k]);
    } else {
        out.reserve(columns + peaks.size());
        const double logLo = log ? std::log10(f0) : 0.0;
        const double largeur = log ? std::log10(f1) - logLo : f1 - f0;
        if (!(largeur > 0.0))
            return {};
        const auto columnOf = [&](size_t k) {
            const double f = std::max(sp.frequence(k), 1e-9);
            const double x = (log ? std::log10(f) - logLo : f - f0) / largeur;
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
                    out.emplace_back(sp.frequence(current->second), sp.amplitude[current->second]);
                current = std::pair(c, k);
            }
        }
        if (current)
            out.emplace_back(sp.frequence(current->second), sp.amplitude[current->second]);
    }
    // sommet affiné de chaque pic : souvent entre deux raies, plus haut qu'elles ; la courbe passe
    // ainsi par le rond qui marque le pic
    const auto bins = std::ptrdiff_t(out.size());
    // Un sommet affiné peut précéder la raie gardée dans la première colonne visible.
    const double lo = std::min(f0, out.front().x()), hi = std::max(f1, out.back().x());
    for (const dsp::Peak &p : peaks) {
        if (p.freq >= lo && p.freq <= hi)
            out.emplace_back(p.freq, p.amplitude);
    }
    std::inplace_merge(out.begin(), out.begin() + bins, out.end(),
                       [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    return out;
}

std::optional<dsp::Peak> pic_le_plus_proche(std::span<const dsp::Peak> peaks, double f, bool log)
{
    const auto axis = [log](double x) { return log ? std::log10(std::max(x, 1e-3)) : x; };
    std::optional<dsp::Peak> best;
    for (const dsp::Peak &p : peaks) {
        if (!best || std::abs(axis(p.freq) - axis(f)) < std::abs(axis(best->freq) - axis(f)))
            best = p;
    }
    return best;
}

std::vector<double> graduations_log(double lo, double hi)
{
    std::vector<double> marks;
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi < lo)
        return marks;
    // Les fréquences sont représentées par des doubles. Se limiter aux
    // décennies représentables avant de convertir les bornes en entiers.
    lo = std::max(lo, std::log10(std::numeric_limits<double>::denorm_min()));
    hi = std::min(hi, std::log10(std::numeric_limits<double>::max()));
    if (hi < lo)
        return marks;
    for (int decade = int(std::floor(lo)); decade <= int(std::floor(hi)); ++decade) {
        for (double facteur : {1.0, 2.0, 5.0}) {
            const double abscisse = decade + std::log10(facteur);
            if (abscisse >= lo && abscisse <= hi)
                marks.push_back(abscisse);
        }
    }
    return marks;
}

QString infos_son(const Tr &tr, const audio::Audio &audio)
{
    const QChar d = tr.decimal;
    const size_t n = audio.channels.size();
    const QString channels = n == 1 ? tr.mono : n == 2 ? tr.stereo : tr.channelsN(n);
    return u"%1 kHz · %2 · %3"_s.arg(texte_nombre(audio.rate / 1000.0, 3, d), channels, texte_duree(audio.duree(), d));
}

QString texte_tableau_harmoniques(const Tr &tr, std::span<const dsp::Harmonic> harmonics)
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
            out += u"%1\t%2\t%3\t%4\t%5\n"_s.arg(QString::number(h.rank), nombre_csv(p->freq, 2, d), nombre_csv(p->freq / f1, 4, d),
                                                 nombre_csv(en_decibels(p->amplitude), 2, d),
                                                 first ? nombre_csv(100.0 * p->amplitude / first->amplitude, 2, d) : QString());
        } else {
            out += u"%1\t\t\t\t\n"_s.arg(h.rank);
        }
    }
    return out;
}

QString csv_spectre(const Tr &tr, const dsp::Spectrum &sp, size_t first, size_t count)
{
    const QChar d = tr.decimal;
    const QChar s = separateur_csv(d);
    QString out;
    if (first == 0)
        out = tr.csvFreq + s + tr.csvAmplitude + s + tr.csvDb + u'\n';
    first = std::min(first, sp.amplitude.size());
    count = std::min(count, sp.amplitude.size() - first);
    out.reserve(qsizetype(count) * 32);
    for (size_t k = first; k < first + count; ++k) {
        const double a = sp.amplitude[k];
        out += nombre_csv(sp.frequence(k), 4, d) + s + nombre_csv(a, 8, d) + s + nombre_csv(en_decibels(a), 2, d) + u'\n';
    }
    return out;
}

QString csv_extrait(const Tr &tr, std::span<const float> samples, size_t start, double rate, bool entete)
{
    const QChar d = tr.decimal;
    const QChar s = separateur_csv(d);
    QString out;
    if (entete)
        out = tr.csvTime + s + tr.csvSignal + u'\n';
    out.reserve(qsizetype(samples.size()) * 22);
    for (size_t i = 0; i < samples.size(); ++i)
        out += nombre_csv(double(start + i) / rate, 6, d) + s + nombre_csv(double(samples[i]), 6, d) + u'\n';
    return out;
}

QString avec_extension_csv(const QString &path)
{
    // « .csv » ajouté au nom tapé sans extension ; si ce fichier existe déjà, le programme demande
    // avant de le remplacer (le dialogue a confirmé le nom tapé, pas celui-ci)
    return QFileInfo(path).suffix().isEmpty() ? path + u".csv"_s : path;
}
