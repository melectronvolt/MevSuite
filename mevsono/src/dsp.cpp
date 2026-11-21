#include "dsp.h"

#include "fft.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>

using namespace Qt::StringLiterals;

namespace dsp {

namespace {

/// Conversion d'un réel positif en indice : 0 pour un négatif ou un NaN,
/// borné pour un très grand nombre (ou l'infini).
size_t indice_borne(double x, size_t maximum)
{
    if (!(x > 0.0))
        return 0;
    return x >= double(maximum) ? maximum : size_t(x);
}

/// Coefficients d'une fenêtre « somme de cosinus » : w(i) = Σ (−1)^k a_k cos(2π k i / n).
std::vector<double> termes_cosinus(Window w)
{
    switch (w) {
    case Window::Rectangular: return {1.0};
    case Window::Hann: return {0.5, 0.5};
    case Window::Hamming: return {0.54, 0.46};
    case Window::Blackman: return {0.42, 0.5, 0.08};
    case Window::BlackmanHarris: return {0.35875, 0.48829, 0.14128, 0.01168};
    case Window::FlatTop: return {0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368};
    }
    return {1.0};
}

/// Noyau de Dirichlet : Σ e^(−2iπνn) pour n de 0 à `len` − 1 (périodique en ν, de période 1).
std::complex<double> dirichlet(double nu, double len)
{
    nu -= std::round(nu);
    const double s = std::sin(std::numbers::pi * nu);
    if (std::abs(s) < 1e-12)
        return len;
    // Le rapport des sinus peut être négatif : std::polar exige un module positif ou nul.
    return std::sin(std::numbers::pi * nu * len) / s * std::polar(1.0, -std::numbers::pi * nu * (len - 1.0));
}

/// Amplitude relative d'une sinusoïde décalée de `delta` raies de la raie mesurée, pour la fenêtre
/// `terms` de `len` échantillons et une FFT de `fftLen` points : |W(δ / fftLen)| / W(0), où W est le
/// spectre de la fenêtre (somme de noyaux de Dirichlet décalés).
double gain_fenetre(const std::vector<double> &terms, double delta, size_t len, size_t fftLen)
{
    const double n = double(len), nu = delta / double(fftLen);
    std::complex<double> w;
    for (size_t k = 0; k < terms.size(); ++k) {
        const double c = (k % 2 == 0 ? terms[k] : -terms[k]) / 2.0;
        w += c * (dirichlet(nu - double(k) / n, n) + dirichlet(nu + double(k) / n, n));
    }
    return std::abs(w) / (n * terms[0]);
}

} // namespace

QString code_fenetre(Window w)
{
    switch (w) {
    case Window::Rectangular: return u"rectangular"_s;
    case Window::Hann: return u"hann"_s;
    case Window::Hamming: return u"hamming"_s;
    case Window::Blackman: return u"blackman"_s;
    case Window::BlackmanHarris: return u"blackman-harris"_s;
    case Window::FlatTop: return u"flat-top"_s;
    }
    return {};
}

std::optional<Window> fenetre_du_code(const QString &code)
{
    for (Window w : ALL_WINDOWS) {
        if (code_fenetre(w) == code)
            return w;
    }
    return std::nullopt;
}

std::vector<double> coefficients_fenetre(Window w, size_t n)
{
    const std::vector<double> terms = termes_cosinus(w);
    std::vector<double> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double x = 2.0 * std::numbers::pi * double(i) / double(n);
        double v = 0.0;
        for (size_t k = 0; k < terms.size(); ++k)
            v += (k % 2 == 0 ? terms[k] : -terms[k]) * std::cos(double(k) * x);
        out[i] = v;
    }
    return out;
}

double raies_lobe_principal(Window w)
{
    switch (w) {
    case Window::Rectangular: return 1.0;
    case Window::Hann:
    case Window::Hamming: return 2.0;
    case Window::Blackman: return 3.0;
    case Window::BlackmanHarris: return 4.0;
    case Window::FlatTop: return 5.0;
    }
    return 1.0;
}

double Spectrum::amplitude_max() const
{
    double m = 0.0;
    for (size_t k = 1; k < amplitude.size(); ++k)
        m = std::max(m, amplitude[k]);
    return m;
}

std::optional<Spectrum> spectre(std::span<const float> signal, double cadence, const SpectrumParams &params,
                                const std::atomic<bool> *annulation)
{
    if (signal.size() < 8 || !(cadence > 0.0) || !std::isfinite(cadence) || (annulation && *annulation))
        return std::nullopt;
    if (params.fftSize != 0 && (params.fftSize < 8 || !std::has_single_bit(params.fftSize)))
        return std::nullopt;
    const size_t longueurSegment = std::min(signal.size(), params.fftSize == 0 ? MAX_AUTO_FFT : params.fftSize);
    const size_t tailleFFT = params.fftSize == 0 ? fft::puissance_de_deux_suivante(longueurSegment) : params.fftSize;

    // Répartir les fenêtres entre les deux bords, avec au moins 50 % de recouvrement.
    const size_t pasMax = longueurSegment / 2;
    const size_t dernierDebut = signal.size() - longueurSegment;
    const size_t intervalles = (dernierDebut + pasMax - 1) / pasMax;
    const auto fenetre = coefficients_fenetre(params.window, longueurSegment);
    const double sommeFenetre = std::accumulate(fenetre.begin(), fenetre.end(), 0.0);
    const fft::Plan plan(tailleFFT);
    std::vector<double> puissances(tailleFFT / 2 + 1, 0.0);
    std::vector<fft::Complex> tampon(tailleFFT);
    for (size_t numero = 0; numero <= intervalles; ++numero) {
        if (annulation && *annulation)
            return std::nullopt;
        const size_t debut = intervalles ? size_t(std::llround(double(dernierDebut) * numero / intervalles)) : 0;
        const auto segment = signal.subspan(debut, longueurSegment);
        const double moyenne = std::accumulate(segment.begin(), segment.end(), 0.0) / double(longueurSegment);
        for (size_t i = 0; i < longueurSegment; ++i)
            tampon[i] = (double(segment[i]) - moyenne) * fenetre[i];
        std::fill(tampon.begin() + longueurSegment, tampon.end(), fft::Complex{});
        plan.directe(tampon);
        for (size_t k = 0; k < puissances.size(); ++k)
            puissances[k] += std::norm(tampon[k]);
    }

    Spectrum resultat;
    resultat.rate = cadence;
    resultat.fftLen = tailleFFT;
    resultat.segmentLen = longueurSegment;
    resultat.segments = intervalles + 1;
    resultat.amplitude.resize(puissances.size());
    for (size_t k = 0; k < puissances.size(); ++k) {
        const double amplitude = std::sqrt(puissances[k] / double(resultat.segments)) / sommeFenetre;
        // Une sinusoïde se partage entre fréquences positives et négatives,
        // sauf pour la composante continue et la raie de Nyquist (fe / 2).
        resultat.amplitude[k] = (k == 0 || k == tailleFFT / 2) ? amplitude : 2.0 * amplitude;
    }
    return resultat;
}

namespace {

/// La FFT mesure l'amplitude sur une grille régulière. Estimer la position du
/// sommet entre deux raies permet de corriger aussi sa hauteur.
Peak affine_pic(const Spectrum &spectre, size_t raie, size_t gauche, size_t droite,
                Window fenetre, const std::vector<double> &termes)
{
    const auto &amplitude = spectre.amplitude;
    const auto gain = [&](double decalage) {
        return gain_fenetre(termes, decalage, spectre.segmentLen, spectre.fftLen);
    };
    const double pasHz = spectre.pas_frequence();
    double centre;
    double decalage;
    if (fenetre == Window::FlatTop) {
        // Le sommet est presque horizontal. Le barycentre des puissances
        // sur le lobe principal situe mieux sa fréquence que deux raies voisines.
        double puissance = 0.0, moment = 0.0;
        for (size_t i = gauche; i <= droite; ++i) {
            const double poids = amplitude[i] * amplitude[i];
            puissance += poids;
            moment += poids * double(i);
        }
        centre = moment / puissance;
        decalage = std::clamp(centre - double(raie), -0.5, 0.5);
    } else {
        // À mesure que le sommet se rapproche d'une demi-raie, la hauteur
        // de la voisine tend vers celle de la raie centrale. Chercher le
        // décalage qui reproduit le rapport observé avec cette fenêtre.
        const bool versDroite = amplitude[raie + 1] >= amplitude[raie - 1];
        const size_t voisine = versDroite ? raie + 1 : raie - 1;
        const double rapport = amplitude[voisine] / amplitude[raie];
        const double precisionEnRaies = std::min(0.0001, 0.0001 / pasHz);
        double bas = 0.0, haut = 0.5;
        for (int essai = 0; essai < 50 && haut - bas > precisionEnRaies; ++essai) {
            const double milieu = (bas + haut) / 2.0;
            if (gain(1.0 - milieu) < rapport * gain(milieu))
                bas = milieu;
            else
                haut = milieu;
        }
        // Si la dichotomie garde zéro dans son intervalle final, le sommet
        // coïncide avec la raie à la précision du calcul.
        decalage = bas == 0.0 ? 0.0 : (versDroite ? 0.5 : -0.5) * (bas + haut);
        centre = double(raie) + decalage;
    }
    return {centre * pasHz, amplitude[raie] / gain(decalage)};
}

} // namespace

std::vector<Peak> pics(const Spectrum &spectre, Window fenetre, const PeakParams &params, const std::atomic<bool> *cancel)
{
    const auto &amplitude = spectre.amplitude;
    if (amplitude.size() < 3 || spectre.segmentLen == 0 || spectre.fftLen == 0 || params.maxPeaks == 0)
        return {};
    const double remplissage = double(spectre.fftLen) / double(spectre.segmentLen);
    const size_t nyquist = amplitude.size() - 1;
    const size_t rayon = std::max<size_t>(1, indice_borne(std::round(raies_lobe_principal(fenetre) * remplissage), nyquist));
    const auto termes = termes_cosinus(fenetre);
    const double seuilRelatif = std::pow(10.0, params.thresholdDb / 20.0);
    std::vector<Peak> trouves;
    // Le seuil se réfère au son entier. Même hors de la bande de fréquences
    // étudiée, un son fort ne doit pas faire apparaître son bruit de fond comme un pic.
    // La dernière raie (Nyquist) n'a pas deux voisines pour l'interpolation.
    // Sa hauteur brute entre elle aussi dans le calcul de ce maximum.
    double maximum = spectre.amplitude_max();
    if (maximum <= 0.0)
        return {};
    for (size_t k = 1; k < nyquist; ++k) {
        if (cancel && *cancel)
            return {};
        // Entre deux raies, toutes les fenêtres proposées conservent au moins
        // la moitié de l'amplitude. En dessous de cette limite, même le sommet
        // corrigé restera sous le seuil d'un pic déjà mesuré.
        if (amplitude[k] <= 0.0 || amplitude[k] < 0.5 * maximum * seuilRelatif)
            continue;
        // Tester les voisines immédiates avant de parcourir un lobe qui peut
        // occuper des milliers de raies lorsque la FFT est complétée par des zéros.
        if ((k > 1 && amplitude[k - 1] >= amplitude[k]) || amplitude[k + 1] > amplitude[k])
            continue;
        const size_t gauche = k > rayon ? k - rayon : 1;
        const size_t droite = std::min(k + rayon, nyquist);
        const auto sommet = std::max_element(amplitude.begin() + gauche, amplitude.begin() + droite + 1);
        if (size_t(sommet - amplitude.begin()) != k)
            continue;
        const Peak pic = affine_pic(spectre, k, gauche, droite, fenetre, termes);
        maximum = std::max(maximum, pic.amplitude);
        if (pic.freq >= params.fmin && pic.freq <= params.fmax)
            trouves.push_back(pic);
    }
    const double seuil = maximum * seuilRelatif;
    std::erase_if(trouves, [seuil](const Peak &pic) { return pic.amplitude < seuil; });
    if (trouves.size() > params.maxPeaks) {
        std::stable_sort(trouves.begin(), trouves.end(), [](const Peak &a, const Peak &b) { return a.amplitude > b.amplitude; });
        trouves.resize(params.maxPeaks);
    }
    std::ranges::stable_sort(trouves, {}, &Peak::freq);
    return trouves;
}

double Analysis::amplitude_max() const
{
    double maximum = spectrum.amplitude_max();
    for (const auto &peak : peaks)
        maximum = std::max(maximum, peak.amplitude);
    return maximum;
}

std::optional<Analysis> analyse(std::span<const float> samples, size_t first, size_t last, double rate,
                                const SpectrumParams &spectrumParams, const PeakParams &peakParams,
                                const std::atomic<bool> *cancel)
{
    if (first > last || last > samples.size())
        return std::nullopt;
    const std::span<const float> extract = samples.subspan(first, last - first);
    auto sp = spectre(extract, rate, spectrumParams, cancel);
    if (!sp)
        return std::nullopt;
    PeakParams params = peakParams;
    params.fmax = std::min(peakParams.fmax, rate / 2.0);
    Analysis a;
    // Trois échantillons consécutifs au même niveau, près du maximum numérique.
    for (size_t i = 2; i < extract.size(); ++i) {
        const float sommet = extract[i];
        if (std::abs(sommet) >= 0.998f && extract[i - 1] == sommet && extract[i - 2] == sommet) {
            a.plateaux = true;
            break;
        }
    }
    a.peaks = pics(*sp, spectrumParams.window, params, cancel);
    if (cancel && *cancel)
        return std::nullopt;
    a.fundamental = fondamentale(a.peaks, sp->pas_frequence(), peakParams.fmin);
    a.spectrum = std::move(*sp);
    mesure_periodicite(a, extract, params, cancel);
    if (cancel && *cancel)
        return std::nullopt;
    return a;
}

} // namespace dsp
