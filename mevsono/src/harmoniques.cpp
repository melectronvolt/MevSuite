// Une corde qui vibre à 100 Hz peut produire des pics à 200 et 500 Hz.
// Ces deux pics correspondent aux rangs 2 et 5, même si le rang 1 est absent du spectre.
#include "dsp.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {

constexpr size_t DERNIER_RANG = 40;
// Les rangs retenus doivent expliquer au moins deux fois la puissance laissée
// de côté. Deux tons aussi forts se partagent la puissance à égalité, tandis
// qu'un léger vibrato répartit une part moindre autour de la note principale.
constexpr double PART_MINIMALE = 2.0 / 3.0;

double puissance_des_pics(std::span<const Peak> pics)
{
    double somme = 0;
    for (const Peak &pic : pics)
        somme += pic.amplitude * pic.amplitude;
    return somme;
}

double puissance_expliquee(std::span<const Peak> pics, double frequence, double df)
{
    double somme = 0;
    for (const Harmonic &rang : harmoniques(pics, frequence, df, DERNIER_RANG)) {
        if (rang.peak)
            somme += rang.peak->amplitude * rang.peak->amplitude;
    }
    return somme;
}

} // namespace

double tolerance_harmonique(double f0, double df)
{
    return std::max(0.03 * f0, 1.5 * df);
}

std::vector<Harmonic> harmoniques(std::span<const Peak> peaks, double f0, double df, size_t count)
{
    std::vector<Harmonic> rangs;
    if (!(f0 > 0))
        return rangs;
    rangs.reserve(count);
    for (size_t rang = 1; rang <= count; ++rang)
        rangs.push_back({rang, double(rang) * f0, std::nullopt});

    // Un pic va au multiple entier le plus proche. Même si la FFT est peu précise,
    // il ne pourra ainsi remplir qu'une ligne du tableau.
    const double tolerance = tolerance_harmonique(f0, df);
    for (const Peak &pic : peaks) {
        const double rang = std::round(pic.freq / f0);
        if (!(rang >= 1 && rang <= double(count)))
            continue;
        Harmonic &ligne = rangs[size_t(rang) - 1];
        if (std::abs(pic.freq - ligne.expected) <= tolerance &&
            (!ligne.peak || pic.amplitude > ligne.peak->amplitude))
            ligne.peak = pic;
    }
    return rangs;
}

std::optional<double> fondamentale(std::span<const Peak> peaks, double df, double fmin)
{
    if (peaks.empty())
        return std::nullopt;
    std::vector<Peak> forts(peaks.begin(), peaks.end());
    std::stable_sort(forts.begin(), forts.end(), [](const Peak &a, const Peak &b) { return a.amplitude > b.amplitude; });
    if (forts.size() > 24)
        forts.resize(24);
    const double dominante = forts.front().freq;
    std::ranges::sort(forts, {}, &Peak::freq);

    // Le pic dominant peut lui-même être un harmonique. S'il est à 500 Hz, les
    // hypothèses comprennent 500, 250, 500/3, 125 et 100 Hz. Les autres pics et
    // leurs écarts donnent aussi des points de départ pour les instruments peu justes.
    std::vector<double> candidates;
    for (size_t rang = 1; rang <= DERNIER_RANG && dominante / rang >= fmin; ++rang)
        candidates.push_back(dominante / rang);
    for (size_t i = 0; i < forts.size(); ++i) {
        candidates.push_back(forts[i].freq);
        if (i > 0)
            candidates.push_back(forts[i].freq - forts[i - 1].freq);
    }

    struct Hypothese {
        double frequence;
        double puissanceExpliquee;
    };
    std::vector<Hypothese> hypotheses;
    double meilleurScore = 0;
    for (const double f : candidates) {
        if (!(f >= fmin && f > 0))
            continue;
        const double rangDominant = std::round(dominante / f);
        if (rangDominant < 1 || rangDominant > DERNIER_RANG ||
            std::abs(dominante - rangDominant * f) > tolerance_harmonique(f, df))
            continue;
        // Additionner les puissances. Les petits lobes autour d'un son pur ont peu
        // d'énergie et ne suffisent pas à faire passer ce son pour un harmonique grave.
        const double score = puissance_expliquee(forts, f, df);
        hypotheses.push_back({f, score});
        meilleurScore = std::max(meilleurScore, score);
    }
    // Une fréquence et sa moitié peuvent expliquer les mêmes pics. Parmi les
    // hypothèses suffisamment étayées et proches du meilleur score (à 3 % près),
    // la plus haute donne la période la plus courte.
    const double seuil = std::max(PART_MINIMALE * puissance_des_pics(forts), 0.97 * meilleurScore);
    double retenue = 0;
    for (const auto &h : hypotheses) {
        if (h.puissanceExpliquee > 0 && h.puissanceExpliquee >= seuil)
            retenue = std::max(retenue, h.frequence);
    }
    if (retenue == 0)
        return std::nullopt;
    // Ajuster f pour que rang × f passe au plus près des pics mesurés.
    double numerateur = 0, denominateur = 0;
    for (const Harmonic &h : harmoniques(forts, retenue, df, DERNIER_RANG)) {
        if (h.peak) {
            numerateur += h.peak->amplitude * h.rank * h.peak->freq;
            denominateur += h.peak->amplitude * h.rank * h.rank;
        }
    }
    return numerateur / denominateur;
}

bool periode_compatible_avec_pics(double periode, std::span<const Peak> pics, double df)
{
    if (!(periode > 0) || !std::isfinite(periode))
        return false;
    const double frequence = 1 / periode;
    // L'autocorrélation peut mesurer une période même si la FFT sépare mal les
    // pics. Seuls les pics dans les rangs étudiés permettent de la contredire.
    const auto fin = std::ranges::lower_bound(pics, (DERNIER_RANG + 0.5) * frequence, {}, &Peak::freq);
    const auto utiles = pics.first(size_t(fin - pics.begin()));
    return utiles.size() < 2 || puissance_expliquee(utiles, frequence, df) >= PART_MINIMALE * puissance_des_pics(utiles);
}

} // namespace dsp
