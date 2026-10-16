// Graphique du spectre de l'extrait : amplitudes (linéaires ou en dB), axe des fréquences linéaire ou
// logarithmique, pics repérés et repères des harmoniques (mode expert). Le réticule donne la valeur
// sous la souris et celle du pic le plus proche (rond plein). Mode expert : clic droit sur un pic pour
// le prendre comme fondamentale.

#pragma once

#include "analyzer.h"
#include "plot.h"
#include "session.h"

#include <QTimer>

#include <optional>

class SpectrumPlot : public PlotWidget {
    Q_OBJECT

public:
    SpectrumPlot(Session &session, const Analyzer &analyzer, QWidget *parent = nullptr);

    /// Pic le plus proche de la souris.
    std::optional<dsp::Peak> hoverPeak() const { return m_hoverPeak; }
    /// À appeler quand l'analyse ou les réglages ont changé (repères, calcul en cours).
    void analysisChanged();

signals:
    /// Fondamentale choisie dans le menu du clic droit ; vide : détection automatique.
    void fundamentalChosen(std::optional<double> f0);

protected:
    bool hasData() const override;
    void paintEmpty(QPainter &painter) override;
    std::pair<double, double> defaultX() const override;
    std::pair<double, double> rangeY() const override;
    QString xAxisLabel() const override;
    QString yAxisLabel() const override;
    std::vector<Tick> xTicks(double x0, double x1, double pixels) const override;
    QString hoverText(QPointF data) const override;
    void paintData(QPainter &painter, const QRectF &r) override;
    void hoverMoved(std::optional<QPointF> pos) override;
    void rightClicked(QPointF pos) override;

private:
    const dsp::Analysis *analysis() const;
    bool logAxis() const { return m_session.active().logFreq; }
    bool decibels() const { return m_session.active().db; }
    /// Abscisse d'une fréquence (log10 de la fréquence sur l'axe logarithmique), et inversement.
    double toX(double f) const;
    double fromX(double x) const;
    /// Ordonnée d'une amplitude (en dB, bornée à `DB_RANGE` sous le plus grand pic, ou linéaire).
    double toY(double a) const;
    /// Amplitude affichée : « −12,3 dB » ou « 0,251 ».
    QString amplitudeText(double y) const;

    Session &m_session;
    const Analyzer &m_analyzer;
    std::optional<dsp::Peak> m_hoverPeak;
    /// Animation de l'attente pendant le calcul.
    QTimer m_spinner;
    int m_spinnerAngle = 0;
};
