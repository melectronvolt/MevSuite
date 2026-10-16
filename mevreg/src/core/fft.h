// Analyse de Fourier : transformée discrète (toutes tailles) et spectre en amplitude.

#pragma once

#include <QString>

#include <complex>
#include <optional>
#include <vector>

namespace fft {

using Complex = std::complex<double>;

/// Transformée de Fourier discrète, sur place (sens direct, ou inverse non normalisée).
/// Puissances de deux : algorithme de Cooley-Tukey ; autres tailles : algorithme de Bluestein.
void transform(std::vector<Complex> &data, bool inverse);

enum class Window { Rectangular, Hann };

QString windowLabel(Window w);

struct SpectrumOptions {
    Window window = Window::Rectangular;
    /// Longueur totale = n × facteur (complétion par des zéros), facteur ≥ 1.
    int zeroPadding = 1;
    /// Écart relatif toléré entre les pas d'échantillonnage.
    double stepTolerance = 0.01;
};

struct Spectrum {
    std::vector<double> freq;
    std::vector<double> amplitude;
    double dt = 0.0;
    size_t n = 0;
};

/// Spectre en amplitude normalisé : une sinusoïde d'amplitude A donne un pic de hauteur A.
/// En cas d'erreur, `nullopt` et le message dans `error`.
std::optional<Spectrum> amplitudeSpectrum(const std::vector<double> &t, const std::vector<double> &y, const SpectrumOptions &opts, QString *error);
/// Spectre d'un signal échantillonné à pas constant `dt`.
Spectrum spectrumUniform(const std::vector<double> &y, double dt, Window window, int zeroPadding);
/// Fréquence du pic principal (hors composante continue), avec interpolation parabolique.
std::optional<double> dominantFrequency(const std::vector<double> &t, const std::vector<double> &y);

} // namespace fft
