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
    /// Extrait : indices d'échantillons `first..last`.
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
    void request(const AnalysisKey &key, const Samples &samples, double rate);
    /// Vrai tant que le résultat de la dernière demande n'est pas arrivé.
    bool pending() const;
    /// Dernier résultat, s'il concerne le son `audio`.
    const dsp::Analysis *result(quint64 audio) const;
    /// Oublie les demandes et résultats (nouveau son).
    void clear();

signals:
    /// Un résultat est arrivé (ou la dernière demande n'a pas de résultat).
    void updated();

private:
    struct Request {
        AnalysisKey key;
        Samples samples;
        double rate = 0.0;
    };

    void run();
    void received(const AnalysisKey &key, std::optional<dsp::Analysis> analysis);

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    /// Prochaine demande à calculer (seule la dernière compte) et arrêt du fil.
    std::optional<Request> m_next;
    bool m_quit = false;
    /// Calcul en cours devenu inutile (demande plus récente, fermeture) : il s'interrompt.
    std::atomic<bool> m_cancel = false;

    std::optional<AnalysisKey> m_requested;
    /// Dernière demande calculée, avec ou sans résultat (extrait trop court).
    std::optional<AnalysisKey> m_finished;
    /// Dernier résultat reçu (il peut correspondre à une demande précédente).
    std::optional<std::pair<AnalysisKey, dsp::Analysis>> m_result;
};
