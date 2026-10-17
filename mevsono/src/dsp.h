// Analyse du signal : fenêtres de pondération, spectre d'amplitude (FFT), pics, fondamentale et
// harmoniques, période par autocorrélation, passages par zéro.
//
// Le spectre est un spectre d'amplitude : une sinusoïde d'amplitude A y donne un pic de hauteur A,
// quelle que soit la fenêtre (la pondération est compensée), et quelle que soit sa fréquence entre
// deux raies de la FFT (amplitude et fréquence des pics affinées d'après la réponse de la fenêtre).

#pragma once

#include <QString>

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

/// Échantillons d'un signal, partagés sans copie entre l'interface, l'analyse et la lecture.
using Samples = std::shared_ptr<const std::vector<float>>;

namespace dsp {

/// Fenêtre de pondération appliquée à l'extrait avant la FFT.
enum class Window { Rectangular, Hann, Hamming, Blackman, BlackmanHarris, FlatTop };

inline constexpr std::array<Window, 6> ALL_WINDOWS = {Window::Rectangular, Window::Hann,           Window::Hamming,
                                                      Window::Blackman,    Window::BlackmanHarris, Window::FlatTop};

/// Nom dans le fichier de réglages.
QString windowCode(Window w);
std::optional<Window> windowFromCode(const QString &code);
/// Valeurs de la fenêtre sur `n` échantillons (forme périodique, adaptée à l'analyse spectrale).
std::vector<double> windowCoefficients(Window w, size_t n);
/// Demi-largeur du lobe principal, en raies (sans complétion par des zéros).
double mainLobeBins(Window w);

/// Tailles de FFT proposées ; 0 : automatique (tout l'extrait, complété par des zéros).
inline constexpr std::array<size_t, 10> FFT_SIZES = {0, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144};
/// Plus grand extrait analysé d'un seul tenant en taille automatique (≈ 6 s à 44,1 kHz) : au-delà,
/// les segments sont moyennés.
inline constexpr size_t MAX_AUTO_FFT = size_t(1) << 18;

struct SpectrumParams {
    Window window = Window::Hann;
    /// Taille de la FFT ; 0 : automatique.
    size_t fftSize = 0;

    bool operator==(const SpectrumParams &) const = default;
};

/// Spectre d'amplitude d'un extrait.
struct Spectrum {
    double rate = 0.0;
    /// Taille de la FFT : écart entre deux raies = `rate / fftLen`.
    size_t fftLen = 0;
    /// Nombre d'échantillons de chaque segment analysé (le reste de la FFT est complété par des zéros).
    size_t segmentLen = 0;
    /// Nombre de segments moyennés (1 si l'extrait tient dans la FFT).
    size_t segments = 0;
    /// Amplitude de chaque raie, de 0 à la fréquence de Nyquist.
    std::vector<double> amplitude;

    /// Écart entre deux raies, en hertz.
    double df() const { return rate / double(fftLen); }
    /// Finesse d'analyse : deux fréquences plus proches que cela ne sont pas séparées.
    double resolution() const { return rate / double(segmentLen); }
    double freq(size_t bin) const { return double(bin) * df(); }
    /// Amplitude maximale (composante continue exclue).
    double maxAmplitude() const;
};

/// Spectre d'amplitude de `samples`. La composante continue de chaque segment est retirée. Si l'extrait
/// est plus long que la FFT, il est découpé en segments qui se recouvrent de moitié, et les spectres
/// sont moyennés (méthode de Welch). Le calcul s'interrompt (résultat vide) dès que `cancel` passe à vrai.
std::optional<Spectrum> spectrum(std::span<const float> samples, double rate, const SpectrumParams &params,
                                 const std::atomic<bool> *cancel = nullptr);

/// Pic du spectre, à fréquence et amplitude affinées.
struct Peak {
    double freq = 0.0;
    double amplitude = 0.0;

    bool operator==(const Peak &) const = default;
};

/// Paramètres de la recherche des pics et des harmoniques.
struct PeakParams {
    /// Seuil sous le plus grand pic, en décibels (−40 dB : 1 % de son amplitude).
    double thresholdDb = -40.0;
    /// Fréquences étudiées.
    double fmin = 20.0;
    double fmax = std::numeric_limits<double>::infinity();
    /// Nombre maximal de pics gardés (les plus grands).
    size_t maxPeaks = 40;

    bool operator==(const PeakParams &) const = default;
};

/// Pics du spectre : maxima locaux au-dessus du seuil, affinés d'après la réponse de la fenêtre (le
/// rapport de la raie du maximum à sa plus grande voisine donne la place du sommet entre les raies, puis
/// son amplitude) : justes pour une sinusoïde, où qu'elle tombe entre deux raies. Fenêtre à sommet
/// plat : fréquence par le barycentre du lobe. Triés par fréquence croissante.
std::vector<Peak> peaks(const Spectrum &sp, Window window, const PeakParams &params);

/// Écart toléré entre un pic et la fréquence n × f0 attendue d'un harmonique.
double harmonicTolerance(double f0, double df);

/// Fondamentale des pics : la fréquence f0 dont les multiples n × f0 expliquent le mieux les pics les
/// plus forts (le plus grand pic doit en faire partie). Les fréquences candidates sont les pics et les
/// écarts entre pics voisins (la fondamentale peut manquer, comme au téléphone). À score égal, la plus
/// haute l'emporte : f0/2 explique aussi tous les harmoniques de f0. Le résultat est affiné par
/// moindres carrés sur les harmoniques trouvés.
std::optional<double> fundamental(std::span<const Peak> peaks, double df, double fmin);

/// Harmonique de rang `rank` : fréquence attendue, et pic trouvé près d'elle.
struct Harmonic {
    size_t rank = 0;
    double expected = 0.0;
    std::optional<Peak> peak;

    bool operator==(const Harmonic &) const = default;
};

/// Les `count` premiers harmoniques de `f0` : pour chacun, le plus grand pic dans la tolérance.
std::vector<Harmonic> harmonics(std::span<const Peak> peaks, double f0, double df, size_t count);

/// Période du signal par autocorrélation, entre 1/`fmax` et 1/`fmin` : le premier maximum de
/// l'autocorrélation presque aussi haut que le plus haut (les maxima suivants sont aux multiples de
/// la période). Renvoie la période en secondes.
std::optional<double> periodAutocorrelation(std::span<const float> samples, double rate, double fmin, double fmax);

/// Passage par zéro montant le plus proche de l'instant `t` (secondes), à moins de `radius`
/// secondes, avec interpolation linéaire entre les deux échantillons.
std::optional<double> risingZeroCrossing(std::span<const float> samples, double rate, double t, double radius);

/// Résultat complet de l'analyse d'un extrait (calculé dans un fil de fond).
struct Analysis {
    Spectrum spectrum;
    std::vector<Peak> peaks;
    std::optional<double> fundamental;
    /// Période trouvée par autocorrélation, en secondes.
    std::optional<double> period;
};

/// Analyse de l'extrait `first..last` (indices d'échantillons) de `samples`, interrompue (résultat vide)
/// si `cancel` passe à vrai.
std::optional<Analysis> analyse(std::span<const float> samples, size_t first, size_t last, double rate,
                                const SpectrumParams &spectrumParams, const PeakParams &peakParams,
                                const std::atomic<bool> *cancel = nullptr);

} // namespace dsp
