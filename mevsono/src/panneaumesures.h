// Colonne de résultats à gauche des graphiques. La fenêtre lui confie les valeurs à
// afficher et reçoit un signal quand une action de l'utilisateur modifie le travail en cours.
#pragma once

#include "dsp.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
struct Session;
struct Tr;

class PanneauMesures : public QWidget {
    Q_OBJECT

public:
    explicit PanneauMesures(QWidget *parent);
    void retraduit(const Tr &t);
    /// Une analyse peut manquer au premier affichage ou si l'extrait est trop court.
    void affiche(const Session &session, const dsp::Analysis *analyse, bool enAttente,
                 const std::optional<dsp::Peak> &pic, const QString &comparaison, const Tr &t);

signals:
    void intervalle_demande();
    void aimantation_changee(bool active);
    void detection_automatique_demandee();
    void copie_demandee();

private:
    void construit_periode();
    void construit_frequences();
    void construit_resultats_experts();
    void affiche_curseurs(const Session &session, double rate, const Tr &t);
    void affiche_autocorrelation(const dsp::Analysis *analyse, bool expert, const Tr &t);
    void affiche_extrait(const Session &session, double rate, const dsp::Analysis *analyse, bool enAttente,
                         const std::optional<dsp::Peak> &pic, const QString &comparaison, const Tr &t);
    void affiche_resultats_experts(const Session &session, const dsp::Analysis &analyse, bool enAttente, const Tr &t);
    void remplit_tableau(const std::vector<dsp::Harmonic> &harmonics, QChar d);

    /// Une section par activité : mesure de la période, étude des fréquences.
    QWidget *m_periodSection = nullptr;
    QWidget *m_frequencySection = nullptr;
    QLabel *m_periodHeading = nullptr;
    QWidget *m_periodBox = nullptr;
    /// Noms des curseurs (t₁, t₂, A, B), de leur couleur, qui suit le thème.
    QLabel *m_t1Name = nullptr;
    QLabel *m_t2Name = nullptr;
    QLabel *m_aName = nullptr;
    QLabel *m_bName = nullptr;
    QLabel *m_t1Value = nullptr;
    QLabel *m_t2Value = nullptr;
    QLabel *m_deltaValue = nullptr;
    QLabel *m_periodHint = nullptr;
    QCheckBox *m_snapCheck = nullptr;
    QPushButton *m_analyseIntervalle = nullptr;
    QLabel *m_autocorrPeriode = nullptr;

    QLabel *m_extractHeading = nullptr;
    QLabel *m_aValue = nullptr;
    QLabel *m_bValue = nullptr;
    QLabel *m_extractInfo = nullptr;
    QLabel *m_qualite = nullptr;
    QLabel *m_autocorrValue = nullptr;
    QWidget *m_computingBox = nullptr;
    QLabel *m_computing = nullptr;
    QLabel *m_tooShort = nullptr;
    QLabel *m_reticleHint = nullptr;

    QWidget *m_expertBox = nullptr;
    QLabel *m_dfLabel = nullptr;
    QLabel *m_dfValue = nullptr;
    QLabel *m_resolutionLabel = nullptr;
    QLabel *m_resolutionValue = nullptr;
    QLabel *m_fundamentalHeading = nullptr;
    QLabel *m_f0Value = nullptr;
    QWidget *m_f0ManualBox = nullptr;
    QLabel *m_f0Manual = nullptr;
    QPushButton *m_autoButton = nullptr;
    QLabel *m_f0Auto = nullptr;
    QLabel *m_f0None = nullptr;
    QTableWidget *m_harmonicsTable = nullptr;
    QPushButton *m_copyTable = nullptr;
    QLabel *m_f0NoPeak = nullptr;
};
