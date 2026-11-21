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

/// Graphe d'un résultat (histogramme ou résidus), prêt à tracer ou à exporter.
struct TraceResultat {
    GraphView view;
    std::vector<PlotData> data;
};
std::optional<TraceResultat> trace_histogramme(const Document &doc, QId q, std::optional<size_t> bins, NumberStyle style);

std::optional<TraceResultat> trace_residus(const Document &doc, const FitSpec &spec);

class CourbeResultat : public QWidget {
public:
    explicit CourbeResultat(QWidget *parent = nullptr) : QWidget(parent) {}
    void definit_trace(std::optional<TraceResultat> plot, const RenderOptions &opts);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    std::optional<TraceResultat> m_plot;
    RenderOptions m_opts;
};

class StatsPanel : public QWidget {
    Q_OBJECT

public:
    explicit StatsPanel(MainWindow &win, QWidget *parent = nullptr);
    void definit_grandeur(QId q);
    std::optional<QId> grandeur_choisie() const { return m_quantity; }
    std::optional<size_t> classes() const { return m_bins; }

private:
    void rafraichit();

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
    CourbeResultat *m_canvas;
};
