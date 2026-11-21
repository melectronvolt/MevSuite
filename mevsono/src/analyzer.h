// Analyse de l'extrait dans un fil de fond : pendant qu'on déplace un curseur, l'interface reste
// fluide, et seule la dernière demande est calculée.

#pragma once

#include "audio.h"
#include "dsp.h"

#include <QObject>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

/// Ce qui détermine le résultat d'une analyse.
struct AnalysisKey {
    /// Numéro du son chargé.
    quint64 audio = 0;
    audio::Channel channel = audio::Channel::Mix;
    /// Indices de l’extrait en échantillons. `first` est inclus et `last` est exclu.
    size_t first = 0;
    size_t last = 0;
    dsp::SpectrumParams spectrum;
    dsp::PeakParams peaks;

    bool operator==(const AnalysisKey &) const = default;
};

class Analyzer : public QObject {
    Q_OBJECT

public:
    explicit Analyzer(QObject *parent = nullptr);
    ~Analyzer() override;

    /// Demande l'analyse, si elle n'est pas déjà demandée.
    void demande(const AnalysisKey &cle, const Samples &signal, double cadence);
    /// Vrai tant que le résultat de la dernière demande n'est pas arrivé.
    bool en_attente() const;
    /// Résultat de la sélection courante, s'il concerne le son `audio`.
    const dsp::Analysis *resultat(quint64 audio) const;
    /// Oublie les demandes et résultats (nouveau son).
    void vide();

signals:
    /// Un résultat est arrivé (ou la dernière demande n'a pas de résultat).
    void mis_a_jour();

private:
    struct Demande {
        AnalysisKey cle;
        Samples signal;
        double cadence = 0.0;
    };

    void traite_les_demandes();
    void sur_reception(const AnalysisKey &cle, std::optional<dsp::Analysis> analyse);

    std::thread m_fil;
    std::mutex m_mutex;
    std::condition_variable m_reveil;
    /// Prochaine demande à calculer (seule la dernière compte) et arrêt du fil.
    std::optional<Demande> m_prochaine;
    bool m_arret = false;
    /// Calcul en cours devenu inutile (demande plus récente, fermeture) : il s'interrompt.
    std::atomic<bool> m_annulation = false;

    std::optional<AnalysisKey> m_demandee;
    /// Dernière demande calculée, avec ou sans résultat (extrait trop court).
    std::optional<AnalysisKey> m_terminee;
    /// Dernier résultat reçu, accessible seulement si la clé correspond à la demande courante.
    std::optional<std::pair<AnalysisKey, dsp::Analysis>> m_resultat;
};
