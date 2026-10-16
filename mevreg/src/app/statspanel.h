// Fenêtre Statistiques : résultats d'une série de mesures répétées (moyenne, écarts-types,
// incertitude de type A) et histogramme.

#pragma once

#include "plot/renderer.h"

#include <QWidget>

#include <optional>

class MainWindow;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QToolButton;

/// Histogramme d'une grandeur, prêt à tracer ou à exporter.
struct HistogramPlot {
    GraphView view;
    std::vector<PlotData> data;
};
std::optional<HistogramPlot> histogramPlot(const Document &doc, QId q, std::optional<size_t> bins, NumberStyle style);

class HistogramCanvas : public QWidget {
public:
    explicit HistogramCanvas(QWidget *parent = nullptr) : QWidget(parent) {}
    void setPlot(std::optional<HistogramPlot> plot, const RenderOptions &opts);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    std::optional<HistogramPlot> m_plot;
    RenderOptions m_opts;
};

class StatsPanel : public QWidget {
    Q_OBJECT

public:
    explicit StatsPanel(MainWindow &win, QWidget *parent = nullptr);
    ~StatsPanel() override;

    static StatsPanel *instance();
    void setQuantity(QId q);
    std::optional<QId> quantity() const { return m_quantity; }
    std::optional<size_t> bins() const { return m_bins; }

private:
    void refresh();

    MainWindow &m_win;
    std::optional<QId> m_quantity;
    std::optional<size_t> m_bins;
    QComboBox *m_combo;
    QCheckBox *m_autoBins;
    QSpinBox *m_binCount;
    QWidget *m_summary;
    QGridLayout *m_grid;
    QLabel *m_result;
    QToolButton *m_copy;
    QLabel *m_note;
    HistogramCanvas *m_canvas;
};
