#pragma once

#include <QAudioDevice>
#include <QWidget>

class QComboBox;
class QProcess;
class QLabel;
class QPushButton;
struct Tr;

/// Sélecteurs communs à l'accueil et au menu Audio. Le choix vaut pour la séance.
class AudioDevices : public QWidget {
    Q_OBJECT
public:
    explicit AudioDevices(QWidget *parent = nullptr);
    ~AudioDevices() override;
    QAudioDevice entree() const;
    QAudioDevice sortie() const;
    void retraduit(const Tr &t);
    void actualise();

signals:
    void entree_changee();
    void sortie_changee();

private:
    void surveille_retraits_pulse(const QString &pactl);
    void affiche(QList<QAudioDevice> entrees, QList<QAudioDevice> sorties);
    QProcess *m_inventaire = nullptr;
    QProcess *m_evenements = nullptr;
    bool m_actualisationDemandee = false;
    QComboBox *m_entrees, *m_sorties;
    QLabel *m_labelEntree, *m_labelSortie;
    QPushButton *m_actualiser;
};
