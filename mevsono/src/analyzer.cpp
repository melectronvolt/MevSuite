#include "analyzer.h"

#include <QDebug>
#include <QMetaObject>

#include <exception>

Analyzer::Analyzer(QObject *parent)
    : QObject(parent)
{
    m_fil = std::thread([this] { traite_les_demandes(); });
}

Analyzer::~Analyzer()
{
    {
        const std::lock_guard verrou(m_mutex);
        m_arret = true;
        m_annulation = true;
    }
    m_reveil.notify_all();
    m_fil.join();
}

void Analyzer::traite_les_demandes()
{
    while (true) {
        Demande demande;
        {
            std::unique_lock verrou(m_mutex);
            m_reveil.wait(verrou, [this] { return m_arret || m_prochaine; });
            if (m_arret)
                return;
            // seule la dernière demande compte
            demande = std::move(*m_prochaine);
            m_prochaine.reset();
            m_annulation = false;
        }
        const AnalysisKey &cle = demande.cle;
        std::optional<dsp::Analysis> analyse;
        try {
            if (demande.signal)
                analyse = dsp::analyse(*demande.signal, cle.first, cle.last, demande.cadence, cle.spectrum, cle.peaks,
                                       &m_annulation);
        } catch (const std::exception &erreur) {
            // Le fil reste disponible, notamment si la mémoire manque pour cet extrait.
            qWarning() << "Échec de l’analyse du signal" << erreur.what();
        }
        // calcul interrompu : une demande plus récente le remplace
        if (m_annulation)
            continue;
        // Le résultat est relevé dans le fil de l'interface ; il est perdu si l'analyseur est détruit.
        QMetaObject::invokeMethod(
            this, [this, cle, analyse = std::move(analyse)]() mutable { sur_reception(cle, std::move(analyse)); },
            Qt::QueuedConnection);
    }
}

void Analyzer::demande(const AnalysisKey &cle, const Samples &signal, double cadence)
{
    if (m_demandee == cle)
        return;
    m_demandee = cle;
    {
        const std::lock_guard verrou(m_mutex);
        m_prochaine = Demande{cle, signal, cadence};
        m_annulation = true;
    }
    m_reveil.notify_one();
}

void Analyzer::sur_reception(const AnalysisKey &cle, std::optional<dsp::Analysis> analyse)
{
    if (m_demandee != cle)
        return;
    m_terminee = cle;
    if (analyse)
        m_resultat.emplace(cle, std::move(*analyse));
    else
        m_resultat.reset();
    emit mis_a_jour();
}

bool Analyzer::en_attente() const
{
    return m_demandee && m_terminee != m_demandee;
}

const dsp::Analysis *Analyzer::resultat(quint64 audio) const
{
    if (!m_resultat || m_resultat->first != m_demandee || m_resultat->first.audio != audio)
        return nullptr;
    return &m_resultat->second;
}

void Analyzer::vide()
{
    // Libérer aussi le travail du son précédent si le nouveau son n'a pas encore d'extrait.
    {
        const std::lock_guard verrou(m_mutex);
        m_prochaine.reset();
        m_annulation = true;
    }
    m_demandee.reset();
    m_terminee.reset();
    m_resultat.reset();
}
