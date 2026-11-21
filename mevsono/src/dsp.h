// Analyse du signal : fenêtres de pondération, spectre d'amplitude (FFT), pics, fondamentale et
// harmoniques, période par autocorrélation, passages par zéro.
//
// Pour une sinusoïde isolée, loin de 0 Hz et de la fréquence de Nyquist, on cherche à retrouver
// son amplitude et sa fréquence. On compense la pondération de la fenêtre et on estime la
// position du sommet entre les raies. Deux composantes trop proches peuvent donner un seul pic.

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
QString code_fenetre(Window w);
std::optional<Window> fenetre_du_code(const QString &code);
/// Valeurs de la fenêtre sur `n` échantillons (forme périodique, adaptée à l'analyse spectrale).
std::vector<double> coefficients_fenetre(Window w, size_t n);
/// Demi-largeur du lobe principal, en raies (sans complétion par des zéros).
double raies_lobe_principal(Window w);

/// Tailles de FFT proposées ; 0 : automatique (tout l'extrait, complété par des zéros).
inline constexpr std::array<size_t, 10> FFT_SIZES = {0, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144};
/// Plus grand extrait analysé d'un seul tenant en taille automatique (≈ 6 s à 44,1 kHz) : au-delà,
/// les segments sont moyennés.
inline constexpr size_t MAX_AUTO_FFT = size_t(1) << 18;

struct SpectrumParams {
    Window window = Window::Hann;
    /// 0 choisit la taille automatique. Une taille explicite doit être une puissance de 2, au moins 8.
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
    double pas_frequence() const { return rate / double(fftLen); }
    /// Inverse de la durée du segment ; la séparation effective dépend aussi de la fenêtre.
    double resolution() const { return rate / double(segmentLen); }
    double frequence(size_t bin) const { return double(bin) * pas_frequence(); }
    /// Amplitude maximale (composante continue exclue).
    double amplitude_max() const;
};

/// Spectre d'amplitude de `samples`. La composante continue de chaque segment est retirée. Si l'extrait
/// est plus long que la FFT, il est découpé en segments qui se recouvrent d’au moins la moitié, et les spectres
/// sont moyennés (méthode de Welch). Le calcul s'interrompt (résultat vide) dès que `cancel` passe à vrai.
std::optional<Spectrum> spectre(std::span<const float> samples, double rate, const SpectrumParams &params,
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

/// Repère les maxima locaux et estime leur fréquence et leur amplitude entre deux raies.
/// Pour une fenêtre à sommet plat, la fréquence vient du barycentre du lobe. La bande
/// étudiée et le seuil portent sur les sommets affinés. Le seuil se mesure sous le
/// maximum de tout le spectre (raies et sommets, composante continue exclue).
/// Renvoie les pics par fréquence croissante. Une annulation rend une liste vide.
std::vector<Peak> pics(const Spectrum &sp, Window window, const PeakParams &params,
                       const std::atomic<bool> *cancel = nullptr);

/// Écart toléré entre un pic et la fréquence n × f0 attendue d'un harmonique.
double tolerance_harmonique(double f0, double df);

/// Fréquence dont les multiples expliquent le mieux les pics forts, dont le pic dominant.
/// Celui-ci peut occuper n'importe quel rang de 1 à 40. La répétition du signal est ensuite
/// vérifiée sur les échantillons par `analyse`.
std::optional<double> fondamentale(std::span<const Peak> peaks, double df, double fmin);

/// Harmonique de rang `rank` : fréquence attendue, et pic trouvé près d'elle.
struct Harmonic {
    size_t rank = 0;
    double expected = 0.0;
    std::optional<Peak> peak;

    bool operator==(const Harmonic &) const = default;
};

/// Les `count` premiers rangs de `f0`. Chaque pic est affecté au seul rang le plus proche
/// si l'écart est assez petit. Si plusieurs pics ont ce rang, le plus fort est retenu.
std::vector<Harmonic> harmoniques(std::span<const Peak> peaks, double f0, double df, size_t count);

/// Période du signal par autocorrélation, entre 1/`fmax` et 1/`fmin` : le premier maximum de
/// l'autocorrélation presque aussi haut que le plus haut (les maxima suivants sont aux multiples de
/// la période). Renvoie la période en secondes.
std::optional<double> periode_autocorrelation(std::span<const float> samples, double rate, double fmin, double fmax,
                                              const std::atomic<bool> *cancel = nullptr);

/// Vérifie sur plusieurs portions de l'extrait que le motif revient après `lag` échantillons.
bool periode_stable(std::span<const float> samples, double lag);

/// Vérifie que les pics résolus, triés par fréquence, s'accordent avec la période mesurée en secondes.
bool periode_compatible_avec_pics(double periode, std::span<const Peak> pics, double df);

/// Passage par zéro montant le plus proche de l'instant `t` (secondes), à moins de `radius`
/// secondes, avec interpolation linéaire entre les deux échantillons.
std::optional<double> passage_par_zero_montant(std::span<const float> samples, double rate, double t, double radius);

/// Résultat complet de l'analyse d'un extrait (calculé dans un fil de fond).
struct Analysis {
    /// Maximum de la courbe tracée, sommets affinés compris.
    double amplitude_max() const;
    bool plateaux = false;
    Spectrum spectrum;
    std::vector<Peak> peaks;
    std::optional<double> fundamental;
    /// Période trouvée par autocorrélation, en secondes.
    std::optional<double> period;
};

/// Estime la période par autocorrélation et vérifie sa répétition et celle de la fondamentale spectrale.
void mesure_periodicite(Analysis &analyse, std::span<const float> samples, const PeakParams &pics,
                        const std::atomic<bool> *cancel);

/// Analyse les échantillons de `first` inclus à `last` exclu. Une annulation rend un résultat vide.
std::optional<Analysis> analyse(std::span<const float> samples, size_t first, size_t last, double rate,
                                const SpectrumParams &spectrumParams, const PeakParams &peakParams,
                                const std::atomic<bool> *cancel = nullptr);

} // namespace dsp
