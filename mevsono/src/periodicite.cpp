// Mesure de la durée du motif qui se répète dans un son et placement des curseurs de période.
#include "dsp.h"

#include "fft.h"
#include "interpolation.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dsp {

namespace {

/// Plus long extrait pris pour l'autocorrélation.
constexpr size_t MAX_AUTOCORR = size_t(1) << 16;
/// Nombre de positions de mesure par échantillon, pour situer le sommet de corrélation.
constexpr size_t SUBDIVISIONS = 4;

constexpr size_t TAILLE_BLOC = 1024;
constexpr size_t NB_ZONES = 64;

struct PortionsSonores {
    std::span<const float> pourAutocorrelation;
    std::vector<size_t> positions;
};

/// Choisit ensemble la grande portion servant à estimer la période et les petites
/// portions servant à la vérifier. Une note brève peut tomber entre deux silences.
PortionsSonores repere_portions(std::span<const float> samples, const std::atomic<bool> *cancel)
{
    PortionsSonores portions{samples.first(std::min(samples.size(), MAX_AUTOCORR)), {}};
    if (samples.size() <= MAX_AUTOCORR) {
        const size_t largeur = std::min(samples.size(), TAILLE_BLOC);
        for (size_t i = 0; i < samples.size(); i += TAILLE_BLOC)
            portions.positions.push_back(std::min(i, samples.size() - largeur));
        return portions;
    }

    portions.positions.resize(NB_ZONES);
    std::array<double, NB_ZONES> energies;
    energies.fill(-1);
    double maximum = -1;
    const size_t dernier = samples.size() - MAX_AUTOCORR;
    for (size_t debut = 0; ; debut = std::min(debut + MAX_AUTOCORR, dernier)) {
        if (cancel && *cancel)
            return {};
        double sommeTotale = 0, carresTotaux = 0;
        for (size_t i = debut; i < debut + MAX_AUTOCORR; i += TAILLE_BLOC) {
            double somme = 0, carres = 0;
            for (double x : samples.subspan(i, TAILLE_BLOC)) {
                somme += x;
                carres += x * x;
            }
            // Dans chaque zone, garder le bloc qui varie le plus autour de sa moyenne.
            const auto zone = size_t(std::llround(double(i) * (NB_ZONES - 1) / double(samples.size() - TAILLE_BLOC)));
            const double energie = std::max(0.0, carres - somme * somme / TAILLE_BLOC);
            if (energie > energies[zone]) {
                energies[zone] = energie;
                portions.positions[zone] = i;
            }
            sommeTotale += somme;
            carresTotaux += carres;
        }
        const double energie = std::max(0.0, carresTotaux - sommeTotale * sommeTotale / MAX_AUTOCORR);
        if (energie > maximum) {
            maximum = energie;
            portions.pourAutocorrelation = samples.subspan(debut, MAX_AUTOCORR);
        }
        if (debut == dernier)
            break;
    }
    return portions;
}

struct Comparaison {
    double produit;
    double energieX;
    double energieY;
};

/// Compare une portion à sa copie décalée. Soustraire les moyennes élimine le
/// décalage du zéro du microphone.
Comparaison compare_portion(std::span<const float> samples, size_t debut, size_t taille, double lag, bool filtrer)
{
    const size_t shift = size_t(lag);
    const double fraction = lag - double(shift);
    double sx = 0, sy = 0, xx = 0, yy = 0, xy = 0;
    for (size_t i = debut; i < debut + taille; ++i) {
        const double x = samples[i];
        const double y = filtrer ? echantillon_filtre(samples, double(i) + lag, 1.0)
                                 : std::lerp(double(samples[i + shift]), double(samples[i + shift + 1]), fraction);
        sx += x;
        sy += y;
        xx += x * x;
        yy += y * y;
        xy += x * y;
    }
    return {xy - sx * sy / double(taille), std::max(0.0, xx - sx * sx / double(taille)),
            std::max(0.0, yy - sy * sy / double(taille))};
}

bool motif_retrouve(std::span<const float> samples, std::span<const size_t> positions, double periode, int cycles,
                    double seuil, bool verifieInversions)
{
    const double lag = cycles * periode;
    if (!(lag >= 2 && lag * 2 <= double(samples.size())))
        return false;
    const size_t shift = size_t(lag);
    const double fraction = lag - double(shift);
    const bool filtrer = periode < 8 && fraction > 0.001 && fraction < 0.999;
    const size_t pairs = samples.size() - shift - 1;
    const size_t block = std::min(pairs, TAILLE_BLOC);
    // Deux tons proches changent de phase au creux du battement. Comparer de
    // courtes portions de part et d'autre du creux révèle leur opposition, même
    // quand l'extrait est trop court pour séparer les deux pics dans le spectre.
    const size_t largeur = verifieInversions ? std::clamp<size_t>(size_t(2 * periode), 32, TAILLE_BLOC) : TAILLE_BLOC;
    double cross = 0, energyX = 0, energyY = 0;
    double energieMax = 0, energieEnOpposition = 0;
    for (size_t position : positions) {
        // Placer les deux lectures de part et d'autre du bloc retenu, même en bord de fichier.
        const size_t start = std::clamp(position, shift / 2, pairs - block + shift / 2) - shift / 2;
        for (size_t i = start; i < start + block; i += largeur) {
            const size_t taille = std::min(largeur, start + block - i);
            const auto c = compare_portion(samples, i, taille, lag, filtrer);
            cross += c.produit;
            energyX += c.energieX;
            energyY += c.energieY;
            if (verifieInversions && taille == largeur) {
                energieMax = std::max({energieMax, c.energieX, c.energieY});
                if (c.produit < -0.7 * std::sqrt(c.energieX * c.energieY))
                    energieEnOpposition = std::max(energieEnOpposition, std::min(c.energieX, c.energieY));
            }
        }
    }
    // Corrélation normalisée : tolère une enveloppe variable (note jouée sur un instrument).
    // Les deux portions doivent atteindre 5 % du niveau maximal pour qu'une
    // opposition compte. Cela écarte les fluctuations dans le silence de la prise.
    return energyX > 0 && energyY > 0 && cross > seuil * std::sqrt(energyX * energyY)
        && energieEnOpposition <= (0.05 * 0.05) * energieMax;
}

/// Produit la courbe de ressemblance du bloc avec lui-même à différents décalages.
std::vector<fft::Complex> correlation_interpolee(std::span<const float> samples, const std::atomic<bool> *cancel)
{
    const size_t n = samples.size();
    const double m = std::accumulate(samples.begin(), samples.end(), 0.0) / double(n);
    const size_t len = fft::puissance_de_deux_suivante(2 * n);
    std::vector<fft::Complex> buffer(len);
    for (size_t i = 0; i < n; ++i)
        buffer[i] = {double(samples[i]) - m, 0.0};
    fft::Plan(len).directe(buffer);
    if (cancel && *cancel)
        return {};

    // La transformée inverse des puissances donne la corrélation pour chaque décalage.
    // Insérer des zéros entre fréquences positives et négatives permet de la lire au
    // quart d'échantillon. Sur une note aiguë, son premier sommet peut tomber entre
    // deux échantillons alors que le sommet situé deux périodes plus loin tombe juste.
    std::vector<fft::Complex> correlation(SUBDIVISIONS * len);
    correlation[0] = std::norm(buffer[0]);
    for (size_t k = 1; k <= len / 2; ++k) {
        double puissance = std::norm(buffer[k]);
        if (k == len / 2)
            puissance /= 2; // La raie de Nyquist d'origine se partage entre +fe/2 et −fe/2.
        correlation[k] = correlation[correlation.size() - k] = puissance;
    }
    fft::Plan(correlation.size()).inverse(correlation);
    return correlation;
}

bool repetition_confirmee(std::span<const float> samples, std::span<const size_t> positions, double lag, bool picIsole)
{
    // Sur une note assez longue, vérifier aussi la répétition après huit cycles.
    // Un glissando lent peut encore être très corrélé au cycle voisin.
    // Avec quelques échantillons par cycle, une droite entre deux valeurs peut
    // déformer le motif. On reconstruit alors sa valeur au décalage voulu.
    return motif_retrouve(samples, positions, lag, 1, 0.75, false)
        && (double(samples.size()) < 24 * lag || motif_retrouve(samples, positions, lag, 8, 0.55, picIsole));
}

std::optional<double> periode_du_bloc(std::span<const float> bloc, double rate, double fmin, double fmax,
                                      const std::atomic<bool> *cancel)
{
    if (bloc.size() < 16 || !(rate > 0 && fmin > 0 && fmax >= fmin) || (cancel && *cancel))
        return std::nullopt;
    const size_t n = bloc.size();
    const auto correlation = correlation_interpolee(bloc, cancel);
    if (correlation.empty() || (cancel && *cancel))
        return std::nullopt;
    const double r0 = correlation[0].real();
    if (!(r0 > 0.0))
        return std::nullopt;
    const double retardMin = std::max(2.0, rate / fmax);
    const double retardMax = std::min(double(n) / 2, rate / fmin);
    if (retardMin > retardMax)
        return std::nullopt;
    const size_t lagMin = size_t(std::floor(SUBDIVISIONS * retardMin));
    const size_t lagMax = size_t(std::ceil(SUBDIVISIONS * retardMax));
    // Un décalage laisse une portion de signal plus courte en commun. On en tient
    // compte pour pouvoir comparer la hauteur du premier sommet à celle des suivants.
    const auto r = [&](size_t indice) {
        const double retard = double(indice) / SUBDIVISIONS;
        return correlation[indice].real() / r0 * double(n) / (double(n) - retard);
    };
    std::vector<size_t> localMax;
    for (size_t l = lagMin; l <= lagMax; ++l) {
        if (r(l) > r(l - 1) && r(l) >= r(l + 1) && r(l) > 0.3)
            localMax.push_back(l);
    }
    double highest = -std::numeric_limits<double>::infinity();
    for (size_t l : localMax)
        highest = std::max(highest, r(l));
    const auto found = std::ranges::find_if(localMax, [&](size_t l) { return r(l) >= 0.9 * highest; });
    if (found == localMax.end())
        return std::nullopt;
    const size_t lag = *found;
    const double a = r(lag - 1), b = r(lag), c = r(lag + 1);
    const double denom = a - 2.0 * b + c;
    const double delta = denom < 0.0 ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
    return std::clamp((double(lag) + delta) / SUBDIVISIONS, retardMin, retardMax) / rate;
}

} // namespace

bool periode_stable(std::span<const float> samples, double lag)
{
    return repetition_confirmee(samples, repere_portions(samples, nullptr).positions, lag, true);
}

std::optional<double> periode_autocorrelation(std::span<const float> samples, double rate, double fmin, double fmax,
                                              const std::atomic<bool> *cancel)
{
    return periode_du_bloc(repere_portions(samples, cancel).pourAutocorrelation, rate, fmin, fmax, cancel);
}

void mesure_periodicite(Analysis &analyse, std::span<const float> samples, const PeakParams &pics,
                        const std::atomic<bool> *cancel)
{
    const auto portions = repere_portions(samples, cancel);
    const double rate = analyse.spectrum.rate;
    analyse.period = periode_du_bloc(portions.pourAutocorrelation, rate, pics.fmin, pics.fmax, cancel);
    if (cancel && *cancel)
        return;
    const bool picIsole = analyse.peaks.size() == 1;
    if (analyse.fundamental && !repetition_confirmee(samples, portions.positions, rate / *analyse.fundamental, picIsole))
        analyse.fundamental.reset();
    if (analyse.period && (!periode_compatible_avec_pics(*analyse.period, analyse.peaks, analyse.spectrum.pas_frequence())
                           || !repetition_confirmee(samples, portions.positions, rate * *analyse.period, picIsole)))
        analyse.period.reset();
}

std::optional<double> passage_par_zero_montant(std::span<const float> signal, double cadence, double instant, double rayon)
{
    if (signal.size() < 2 || !(cadence > 0.0) || !std::isfinite(cadence) ||
        !std::isfinite(instant) || !std::isfinite(rayon) || rayon < 0.0)
        return std::nullopt;

    // Aux bords de la recherche, le passage peut tomber entre deux échantillons
    // dont l'un est hors de la fenêtre. Garder les deux pour l'interpolation.
    const double debut = instant - rayon, fin = instant + rayon;
    const double dernierIndice = double(signal.size() - 1);
    const size_t premier = size_t(std::clamp(std::floor(debut * cadence), 0.0, dernierIndice));
    // Si la borne droite coïncide avec un échantillon nul, lire également
    // l'échantillon suivant pour savoir si le passage est montant.
    const size_t dernierSegment = size_t(std::clamp(std::floor(fin * cadence), 0.0, dernierIndice));
    const size_t dernier = std::min(dernierSegment + 1, signal.size() - 1);
    std::optional<double> plusProche;
    for (size_t i = premier; i < dernier; ++i) {
        const double avant = signal[i], apres = signal[i + 1];
        if (avant <= 0.0 && apres > 0.0) {
            const double fraction = -avant / (apres - avant);
            const double passage = (double(i) + fraction) / cadence;
            if (passage >= debut && passage <= fin &&
                (!plusProche || std::abs(passage - instant) < std::abs(*plusProche - instant)))
                plusProche = passage;
        }
    }
    return plusProche;
}

} // namespace dsp
