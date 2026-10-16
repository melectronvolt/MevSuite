// Réglages conservés d'une séance à l'autre, dans un petit fichier texte `clé = valeur`
// modifiable à la main. Une valeur absente ou illisible reprend sa valeur par défaut.
//
// Deux fichiers `mevsono.ini` sont lus, dans cet ordre :
// 1. celui de l'utilisateur (dossier de configuration), où MeVSono enregistre les réglages ;
// 2. celui placé à côté du programme (clé USB, réseau du lycée), s'il existe : ses valeurs
//    l'emportent à chaque lancement et MeVSono ne l'écrit jamais.
//
// Le format est celui de `mevsono.ini` (version Rust) : un fichier se recopie de l'un à l'autre.

#pragma once

#include "audio.h"
#include "dsp.h"
#include "i18n.h"

#include <QSize>
#include <QString>
#include <Qt>

#include <array>
#include <optional>

/// Agrandissement de l'interface accepté (`Ctrl` + / `Ctrl` −).
inline constexpr double UI_ZOOM_MIN = 0.5;
inline constexpr double UI_ZOOM_MAX = 3.0;
/// Fréquences maximales proposées pour l'affichage du spectre ; 0 : jusqu'à fe/2.
inline constexpr std::array<double, 7> FMAX_CHOICES = {1000.0, 2000.0, 5000.0, 10000.0, 15000.0, 20000.0, 0.0};

/// Emplacement des fichiers de réglages.
struct SettingsFiles {
    /// Réglages de l'utilisateur, lus puis enregistrés par MeVSono (vide si introuvable).
    QString user;
    /// Réglages imposés, à côté du programme : lus seulement, ils l'emportent (vide si absent).
    QString admin;

    /// Nécessite une `QCoreApplication` (dossier du programme).
    static SettingsFiles locate();
};

struct Settings {
    /// Vide : langue du système.
    std::optional<Lang> lang;
    /// Thème clair ou sombre (`Unknown` : celui du bureau, tant que le bouton n'a pas servi).
    Qt::ColorScheme theme = Qt::ColorScheme::Unknown;
    /// Mode expert (toutes les options) ; sinon mode simplifié (les mesures seulement).
    bool expert = false;
    audio::Channel channel = audio::Channel::Mix;
    dsp::Window window = dsp::Window::Hann;
    /// Taille de la FFT ; 0 : automatique.
    size_t fftSize = 0;
    /// Amplitudes en décibels.
    bool db = false;
    /// Axe des fréquences logarithmique.
    bool logFreq = false;
    /// Fréquence maximale affichée ; 0 : jusqu'à fe/2.
    double fmax = 5000.0;
    /// Seuil de détection des pics, sous le plus grand (dB).
    double thresholdDb = -40.0;
    /// Nombre d'harmoniques du tableau.
    int harmonics = 10;
    /// Curseurs t₁ et t₂ aimantés aux passages par zéro montants (par défaut).
    bool snapZero = true;
    /// Repères des harmoniques (n × f₁) sur le spectre.
    bool harmonicMarks = false;
    /// Agrandissement de l'interface (`Ctrl` + / `Ctrl` −).
    double uiZoom = 1.0;
    /// Dossier proposé par les boîtes de dialogue (vide : aucun).
    QString lastDir;
    /// Taille de la fenêtre non agrandie.
    std::optional<QSize> windowSize;
    /// Fenêtre agrandie à l'écran (elle s'ouvre à sa taille normale tant que l'élève ne l'a pas
    /// agrandie lui-même).
    bool maximized = false;

    /// Réglages de l'utilisateur, puis réglages imposés par-dessus.
    static Settings load(const SettingsFiles &files);
    /// Renvoie le message d'erreur, vide en cas de succès.
    QString save(const QString &path) const;
    static Settings parse(const QString &text);
    /// Remplace les réglages présents dans `text` ; les autres restent inchangés.
    void apply(const QString &text);
    QString toText() const;

    bool operator==(const Settings &) const = default;
};
