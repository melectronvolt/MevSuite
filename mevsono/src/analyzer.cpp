#include "analyzer.h"

#include <QMetaObject>

Analyzer::Analyzer(QObject *parent)
    : QObject(parent)
{
    m_thread = std::thread([this] { run(); });
}

Analyzer::~Analyzer()
{
    {
        const std::lock_guard lock(m_mutex);
        m_quit = true;
        m_cancel = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

void Analyzer::run()
{
    while (true) {
        Request request;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || m_next; });
            if (m_quit)
                return;
            // seule la dernière demande compte
            request = std::move(*m_next);
            m_next.reset();
            m_cancel = false;
        }
        const AnalysisKey &k = request.key;
        std::optional<dsp::Analysis> analysis;
        try {
            analysis = dsp::analyse(*request.samples, k.first, k.last, request.rate, k.spectrum, k.peaks, &m_cancel);
        } catch (...) {
            // mémoire insuffisante pour un extrait démesuré : pas de résultat
        }
        // calcul interrompu : une demande plus récente le remplace
        if (m_cancel)
            continue;
        // Le résultat est relevé dans le fil de l'interface ; il est perdu si l'analyseur est détruit.
        QMetaObject::invokeMethod(
            this, [this, key = request.key, a = std::move(analysis)]() mutable { received(key, std::move(a)); },
            Qt::QueuedConnection);
    }
}

void Analyzer::request(const AnalysisKey &key, const Samples &samples, double rate)
{
    if (m_requested == key)
        return;
    m_requested = key;
    {
        const std::lock_guard lock(m_mutex);
        m_next = Request{key, samples, rate};
        m_cancel = true;
    }
    m_wake.notify_one();
}

void Analyzer::received(const AnalysisKey &key, std::optional<dsp::Analysis> analysis)
{
    m_finished = key;
    if (analysis)
        m_result.emplace(key, std::move(*analysis));
    else if (m_requested == key)
        m_result.reset();
    emit updated();
}

bool Analyzer::pending() const
{
    return m_requested && m_finished != m_requested;
}

const dsp::Analysis *Analyzer::result(quint64 audio) const
{
    return m_result && m_result->first.audio == audio ? &m_result->second : nullptr;
}

void Analyzer::clear()
{
    m_requested.reset();
    m_finished.reset();
    m_result.reset();
}
