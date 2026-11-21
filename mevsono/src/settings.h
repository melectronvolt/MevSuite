// Réglages conservés d'une séance à l'autre, dans un petit fichier texte `clé = valeur`
// modifiable à la main. Chaque valeur reconnue remplace le réglage lu précédemment.
//
// Deux fichiers `mevsono.ini` sont lus, dans cet ordre :
// 1. celui de l'utilisateur (dossier de configuration), où MeVSono enregistre les réglages ;
// 2. celui de la salle. Sous Linux, /etc/MeVSuite est prioritaire. À défaut, comme sous Windows,
//    MeVSono cherche ce fichier à côté du programme. Il le lit uniquement au démarrage.

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
    /// Fichier de salle retenu au démarrage (vide si absent).
    QString admin;

    /// Nécessite une `QCoreApplication` (dossier du programme).
    static SettingsFiles localise();
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
    /// Adapte les ordonnées du signal et du spectre à la portion affichée.
    bool autoScaleY = true;
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
    static Settings charge(const SettingsFiles &files);
    /// Renvoie le message d'erreur, vide en cas de succès.
    QString enregistre(const QString &path) const;
    static Settings interprete(const QString &text);
    /// Remplace les réglages présents dans `text` ; les autres restent inchangés.
    void applique(const QString &text);
    QString en_texte() const;

    bool operator==(const Settings &) const = default;
};
