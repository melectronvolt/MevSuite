#include "settingsdialog.h"

#include "format.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

QLabel *heading(QWidget *parent)
{
    auto *label = new QLabel(parent);
    QFont font = label->font();
    font.setBold(true);
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(font.pointSizeF() * 1.2);
    label->setFont(font);
    return label;
}

QWidget *pairOf(QWidget *parent, QRadioButton *first, QRadioButton *second)
{
    auto *box = new QWidget(parent);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(first);
    row->addWidget(second);
    row->addStretch();
    auto *group = new QButtonGroup(box);
    group->addButton(first);
    group->addButton(second);
    return box;
}

template <typename T, size_t N>
int indexOf(const std::array<T, N> &values, const T &value)
{
    const auto it = std::ranges::find(values, value);
    return it == values.end() ? 0 : int(it - values.begin());
}

} // namespace

SettingsDialog::SettingsDialog(Settings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    auto *layout = new QVBoxLayout(this);

    m_analysisHeading = heading(this);
    layout->addWidget(m_analysisHeading);
    auto *analysis = new QGridLayout;
    analysis->setHorizontalSpacing(16);
    m_windowLabel = new QLabel(this);
    m_window = new QComboBox(this);
    for (size_t i = 0; i < dsp::ALL_WINDOWS.size(); ++i)
        m_window->addItem(QString());
    m_fftLabel = new QLabel(this);
    m_fft = new QComboBox(this);
    for (size_t i = 0; i < dsp::FFT_SIZES.size(); ++i)
        m_fft->addItem(QString());
    m_thresholdLabel = new QLabel(this);
    m_threshold = new QDoubleSpinBox(this);
    m_threshold->setRange(-100.0, -10.0);
    m_threshold->setSingleStep(0.5);
    m_threshold->setDecimals(1);
    m_threshold->setSuffix(u" dB"_s);
    m_harmonicsLabel = new QLabel(this);
    m_harmonics = new QSpinBox(this);
    m_harmonics->setRange(1, 40);
    m_channelLabel = new QLabel(this);
    m_channel = new QComboBox(this);
    for (size_t i = 0; i < audio::ALL_CHANNELS.size(); ++i)
        m_channel->addItem(QString());
    const std::pair<QLabel *, QWidget *> analysisRows[] = {{m_windowLabel, m_window},
                                                           {m_fftLabel, m_fft},
                                                           {m_thresholdLabel, m_threshold},
                                                           {m_harmonicsLabel, m_harmonics},
                                                           {m_channelLabel, m_channel}};
    int row = 0;
    for (const auto &[label, field] : analysisRows) {
        analysis->addWidget(label, row, 0);
        analysis->addWidget(field, row++, 1, Qt::AlignLeft);
    }
    layout->addLayout(analysis);
    m_snap = new QCheckBox(this);
    layout->addWidget(m_snap);
    layout->addSpacing(8);

    m_displayHeading = heading(this);
    layout->addWidget(m_displayHeading);
    auto *display = new QGridLayout;
    display->setHorizontalSpacing(16);
    m_amplitudeLabel = new QLabel(this);
    m_ampLinear = new QRadioButton(this);
    m_ampDb = new QRadioButton(this);
    m_axisLabel = new QLabel(this);
    m_axisLinear = new QRadioButton(this);
    m_axisLog = new QRadioButton(this);
    m_marksLabel = new QLabel(this);
    m_marks = new QCheckBox(this);
    m_fmaxLabel = new QLabel(this);
    m_fmax = new QComboBox(this);
    for (size_t i = 0; i < FMAX_CHOICES.size(); ++i)
        m_fmax->addItem(QString());
    const std::pair<QLabel *, QWidget *> displayRows[] = {{m_amplitudeLabel, pairOf(this, m_ampLinear, m_ampDb)},
                                                          {m_axisLabel, pairOf(this, m_axisLinear, m_axisLog)},
                                                          {m_marksLabel, m_marks},
                                                          {m_fmaxLabel, m_fmax}};
    row = 0;
    for (const auto &[label, field] : displayRows) {
        display->addWidget(label, row, 0);
        display->addWidget(field, row++, 1, Qt::AlignLeft);
    }
    layout->addLayout(display);
    layout->addSpacing(8);
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    auto *buttons = new QHBoxLayout;
    m_defaults = new QPushButton(this);
    m_close = new QPushButton(this);
    buttons->addWidget(m_defaults);
    buttons->addWidget(m_close);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    const auto onChange = [this] { changed(); };
    connect(m_window, &QComboBox::currentIndexChanged, this, onChange);
    connect(m_fft, &QComboBox::currentIndexChanged, this, onChange);
    connect(m_threshold, &QDoubleSpinBox::valueChanged, this, onChange);
    connect(m_harmonics, &QSpinBox::valueChanged, this, onChange);
    connect(m_channel, &QComboBox::currentIndexChanged, this, onChange);
    connect(m_snap, &QCheckBox::toggled, this, onChange);
    connect(m_ampDb, &QRadioButton::toggled, this, onChange);
    connect(m_axisLog, &QRadioButton::toggled, this, onChange);
    connect(m_marks, &QCheckBox::toggled, this, onChange);
    connect(m_fmax, &QComboBox::currentIndexChanged, this, onChange);
    connect(m_defaults, &QPushButton::clicked, this, [this] {
        // les choix qui ne relèvent pas de l'analyse ni de l'affichage restent
        const Settings old = m_settings;
        m_settings = Settings();
        m_settings.lang = old.lang;
        m_settings.theme = old.theme;
        m_settings.expert = old.expert;
        m_settings.lastDir = old.lastDir;
        m_settings.uiZoom = old.uiZoom;
        m_settings.windowSize = old.windowSize;
        m_settings.maximized = old.maximized;
        load();
        emit edited();
    });
    connect(m_close, &QPushButton::clicked, this, &QDialog::close);
}

void SettingsDialog::retranslate(const Tr &tr)
{
    const bool loading = std::exchange(m_loading, true);
    setWindowTitle(tr.settingsTitle);
    m_analysisHeading->setText(tr.setAnalysisHeading);
    m_windowLabel->setText(tr.setWindow);
    for (size_t i = 0; i < dsp::ALL_WINDOWS.size(); ++i) {
        m_window->setItemText(int(i), tr.windowNames[i]);
        m_window->setItemData(int(i), tr.windowTips[i], Qt::ToolTipRole);
    }
    m_fftLabel->setText(tr.setFft);
    m_fftLabel->setToolTip(tr.fftTip);
    m_fft->setToolTip(tr.fftTip);
    for (size_t i = 0; i < dsp::FFT_SIZES.size(); ++i)
        m_fft->setItemText(int(i), dsp::FFT_SIZES[i] == 0 ? tr.fftAuto : tr.fftPoints(dsp::FFT_SIZES[i]));
    m_thresholdLabel->setText(tr.setThreshold);
    m_thresholdLabel->setToolTip(tr.thresholdTip);
    m_threshold->setToolTip(tr.thresholdTip);
    m_threshold->setLocale(tr.decimal == u',' ? QLocale(QLocale::French) : QLocale::c());
    m_harmonicsLabel->setText(tr.setHarmonics);
    m_channelLabel->setText(tr.setChannel);
    for (size_t i = 0; i < audio::ALL_CHANNELS.size(); ++i)
        m_channel->setItemText(int(i), tr.channelNames[i]);
    m_snap->setText(tr.snapZero);
    m_snap->setToolTip(tr.snapZeroTip);
    m_displayHeading->setText(tr.setDisplayHeading);
    m_amplitudeLabel->setText(tr.setAmplitude);
    m_ampLinear->setText(tr.ampLinear);
    m_ampDb->setText(tr.ampDb);
    m_axisLabel->setText(tr.setFreqAxis);
    m_axisLinear->setText(tr.axisLinear);
    m_axisLog->setText(tr.axisLog);
    m_marksLabel->setText(tr.setMarks);
    m_marks->setText(tr.harmonicMarks);
    m_marks->setToolTip(tr.harmonicMarksTip);
    m_fmaxLabel->setText(tr.setFmax);
    for (size_t i = 0; i < FMAX_CHOICES.size(); ++i)
        m_fmax->setItemText(int(i), FMAX_CHOICES[i] > 0.0 ? fmtFreqShort(FMAX_CHOICES[i], tr.decimal) : tr.fmaxAuto);
    m_defaults->setText(tr.defaults);
    m_close->setText(tr.close);
    m_loading = loading;
}

void SettingsDialog::load()
{
    const bool loading = std::exchange(m_loading, true);
    const Settings &s = m_settings;
    m_window->setCurrentIndex(indexOf(dsp::ALL_WINDOWS, s.window));
    m_fft->setCurrentIndex(indexOf(dsp::FFT_SIZES, s.fftSize));
    m_threshold->setValue(s.thresholdDb);
    m_harmonics->setValue(s.harmonics);
    m_channel->setCurrentIndex(indexOf(audio::ALL_CHANNELS, s.channel));
    m_snap->setChecked(s.snapZero);
    (s.db ? m_ampDb : m_ampLinear)->setChecked(true);
    (s.logFreq ? m_axisLog : m_axisLinear)->setChecked(true);
    m_marks->setChecked(s.harmonicMarks);
    m_fmax->setCurrentIndex(indexOf(FMAX_CHOICES, s.fmax));
    m_loading = loading;
}

void SettingsDialog::changed()
{
    if (m_loading)
        return;
    Settings &s = m_settings;
    s.window = dsp::ALL_WINDOWS[size_t(std::max(m_window->currentIndex(), 0))];
    s.fftSize = dsp::FFT_SIZES[size_t(std::max(m_fft->currentIndex(), 0))];
    s.thresholdDb = m_threshold->value();
    s.harmonics = m_harmonics->value();
    s.channel = audio::ALL_CHANNELS[size_t(std::max(m_channel->currentIndex(), 0))];
    s.snapZero = m_snap->isChecked();
    s.db = m_ampDb->isChecked();
    s.logFreq = m_axisLog->isChecked();
    s.harmonicMarks = m_marks->isChecked();
    s.fmax = FMAX_CHOICES[size_t(std::max(m_fmax->currentIndex(), 0))];
    emit edited();
}
