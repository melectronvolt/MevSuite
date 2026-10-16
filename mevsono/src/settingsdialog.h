// Fenêtre des réglages (mode expert) : analyse spectrale (fenêtre, taille de la FFT, seuil des pics,
// harmoniques, voie) et affichage (dB, axe logarithmique, repères, fréquence maximale). Les
// changements s'appliquent aussitôt.

#pragma once

#include "i18n.h"
#include "settings.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(Settings &settings, QWidget *parent);

    void retranslate(const Tr &tr);
    /// Reprend les valeurs des réglages (changés ailleurs : menus, valeurs par défaut).
    void load();

signals:
    /// Un réglage a été changé dans la fenêtre.
    void edited();

private:
    void changed();

    Settings &m_settings;
    /// Vrai pendant que `load` remplit les champs : ce ne sont pas des changements de l'utilisateur.
    bool m_loading = false;

    QLabel *m_analysisHeading = nullptr;
    QLabel *m_windowLabel = nullptr;
    QComboBox *m_window = nullptr;
    QLabel *m_fftLabel = nullptr;
    QComboBox *m_fft = nullptr;
    QLabel *m_thresholdLabel = nullptr;
    QDoubleSpinBox *m_threshold = nullptr;
    QLabel *m_harmonicsLabel = nullptr;
    QSpinBox *m_harmonics = nullptr;
    QLabel *m_channelLabel = nullptr;
    QComboBox *m_channel = nullptr;
    QCheckBox *m_snap = nullptr;

    QLabel *m_displayHeading = nullptr;
    QLabel *m_amplitudeLabel = nullptr;
    QRadioButton *m_ampLinear = nullptr;
    QRadioButton *m_ampDb = nullptr;
    QLabel *m_axisLabel = nullptr;
    QRadioButton *m_axisLinear = nullptr;
    QRadioButton *m_axisLog = nullptr;
    QLabel *m_marksLabel = nullptr;
    QCheckBox *m_marks = nullptr;
    QLabel *m_fmaxLabel = nullptr;
    QComboBox *m_fmax = nullptr;

    QPushButton *m_defaults = nullptr;
    QPushButton *m_close = nullptr;
};
