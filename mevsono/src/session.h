// État du travail (son chargé, extrait analysé, curseurs de mesure, réglages) et calculs de
// l'interface qui ne dépendent pas des widgets : extrait choisi à l'ouverture, points tracés du
// spectre, pic le plus proche, graduations logarithmiques, exports CSV et tableau des harmoniques.

#pragma once

#include "analyzer.h"
#include "audio.h"
#include "dsp.h"
#include "i18n.h"
#include "player.h"
#include "settings.h"
#include "wave.h"

#include <QPointF>
#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/// Plus basse fréquence étudiée (pics, fondamentale, axe logarithmique).
inline constexpr double FMIN = 20.0;
/// Étendue de l'axe des amplitudes en décibels, sous le plus grand pic.
inline constexpr double DB_RANGE = 100.0;
/// Extrait choisi à l'ouverture d'un son : en mode simplifié, cette part du son, au milieu ; en mode
/// expert, cette durée (secondes) sur la partie la plus forte.
inline constexpr double SIMPLE_EXTRACT = 0.8;
inline constexpr double EXPERT_EXTRACT = 0.5;

/// Activité, choisie par les onglets : elle décide de ce que montrent le signal et le panneau, et de
/// ce que fait la souris sur le signal.
enum class Tool {
    /// Mesure de la période (curseurs t₁ et t₂) ; pas de spectre.
    Period,
    /// Étude des fréquences : choisir l'extrait analysé (curseurs A et B) et lire son spectre.
    Extract,
};

/// Ce que joue la lecture.
enum class Listen {
    /// Tout le son.
    Sound,
    /// L'extrait entre A et B.
    Extract,
};

/// Deux curseurs, en secondes.
using Cursors = std::array<double, 2>;

/// Premier tracé et position de A et B. Pour un fichier, le lecteur les prépare en arrière-plan.
struct ApercuSon {
    bool expert = false;
    audio::Channel voie = audio::Channel::Mix;
    Cursors extrait{};
    std::shared_ptr<const Envelope> enveloppe;

    static ApercuSon prepare(const audio::Audio &son, const Settings &reglages);
    bool correspond(const audio::Audio &son, const Settings &reglages) const;
};

/// Son chargé. Les autres voies reçoivent leur enveloppe quand on les affiche pour la première fois.
struct Loaded {
    Loaded(audio::Audio son, quint64 numero, ApercuSon apercu);
    audio::Audio audio;
    quint64 id = 0;

    const Envelope &enveloppe(audio::Channel channel) const;

private:
    mutable std::array<std::shared_ptr<const Envelope>, 3> m_envelopes;
};

struct Session {
    std::optional<Loaded> loaded;
    quint64 nextId = 1;
    Tool tool = Tool::Period;
    /// Extrait analysé : curseurs A et B, A toujours avant B.
    Cursors extract = {0.0, 0.0};
    /// Curseurs t₁ et t₂ de mesure d'une période.
    std::optional<Cursors> period;
    /// Fondamentale choisie à la main (sinon, celle détectée ; mode expert).
    std::optional<double> manualF0;
    /// Écoute : tout le son ou l'extrait (mode expert).
    Listen listen = Listen::Sound;
    /// Réglages de l'analyse et de l'affichage, tels que l'utilisateur les a choisis.
    Settings params;

    /// Nouveau son : l'extrait couvre l'essentiel du son (mode simplifié), ou une demi-seconde de sa
    /// partie la plus forte (mode expert).
    void installe_son(audio::Audio audio);
    void installe_son(audio::Audio audio, ApercuSon apercu);
    /// Réglages d'analyse et d'affichage en vigueur : ceux de l'utilisateur en mode expert ; en mode
    /// simplifié, les valeurs par défaut (affichage fixe, sans option à régler).
    Settings reglages_actifs() const;
    /// Voie étudiée (la moyenne pour un son mono).
    audio::Channel canal() const;
    /// Extrait entre A et B, en indices d'échantillons. Le premier est inclus, le dernier exclu.
    std::optional<std::pair<size_t, size_t>> intervalle_extrait() const;
    dsp::SpectrumParams parametres_spectre() const;
    /// Fréquence choisie à la main ou, à défaut, celle détectée dans l'extrait.
    std::optional<double> fondamentale(const dsp::Analysis *analyse) const;
    /// Rangs communs au tableau, au presse-papiers et aux repères du spectre.
    std::vector<dsp::Harmonic> harmoniques(const dsp::Analysis *analyse) const;
    /// Ce qui détermine l'analyse de l'extrait.
    std::optional<AnalysisKey> cle_analyse() const;
    /// Fréquence maximale du spectre affiché.
    double fmax_affichee() const;
    /// Ce qu'on écoute : au choix en mode expert ; en mode simplifié, tout le son pour mesurer la
    /// période, l'extrait pour étudier les fréquences.
    Listen choix_ecoute() const;
    /// Ce que joue la lecture : tout le son ou l'extrait, sur la voie étudiée.
    std::optional<Clip> extrait_en_lecture() const;
};

/// Les deux curseurs dans l'ordre croissant.
std::pair<double, double> trie(const Cursors &c);

/// Début, en secondes, de la portion dont la moyenne quadratique est la plus forte.
/// Les débuts sont espacés de 10 ms. La dernière portion atteint la fin du signal.
double fenetre_la_plus_forte(std::span<const float> signal, double cadence, double duree);

/// Points du spectre visibles entre `f0` et `f1` (fréquence, amplitude) : chaque raie s'il y en a
/// peu, sinon la plus haute de chaque colonne de pixels (les pics restent visibles), et le sommet
/// affiné de chaque pic de `peaks` (triés par fréquence), qu'aucune raie n'atteint en général.
std::vector<QPointF> points_spectre(const dsp::Spectrum &sp, std::span<const dsp::Peak> peaks, double f0, double f1,
                                    size_t columns, bool log);

/// Pic repéré le plus proche de la fréquence `f`, en distance sur l'axe affiché (logarithmique ou non).
std::optional<dsp::Peak> pic_le_plus_proche(std::span<const dsp::Peak> peaks, double f, bool log);

/// Graduations d'un axe logarithmique entre `lo` et `hi` (abscisse = log10 de la fréquence) :
/// 1, 2, 5 × 10ⁿ. Renvoie leurs abscisses.
std::vector<double> graduations_log(double lo, double hi);

/// « 44,1 kHz · stéréo · 3,20 s »
QString infos_son(const Tr &tr, const audio::Audio &audio);

/// Tableau des harmoniques, colonnes séparées par des tabulations, à coller dans un tableur ; rapports
/// à la fondamentale (rang 1), même si elle n'a pas de pic.
QString texte_tableau_harmoniques(const Tr &tr, std::span<const dsp::Harmonic> harmonics);
/// Spectre (fréquence, amplitude, amplitude en dB) au format CSV.
QString csv_spectre(const Tr &tr, const dsp::Spectrum &sp, size_t first = 0, size_t count = std::dynamic_extent);
/// Échantillons de l'extrait (instant, valeur) au format CSV ; `start` : indice du premier.
QString csv_extrait(const Tr &tr, std::span<const float> samples, size_t start, double rate, bool entete = true);
/// Ajoute `.csv` à un nom saisi sans extension, sauf si cela écraserait un fichier existant.
QString avec_extension_csv(const QString &path);
