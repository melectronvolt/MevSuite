#include "spectrumplot.h"

#include "format.h"
#include "style.h"

#include <QMenu>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Plus petite échelle des amplitudes : le spectre d'un silence garde des graduations courtes, qui
/// tiennent dans la marge.
constexpr double MIN_AMPLITUDE = 1e-4;

} // namespace

SpectrumPlot::SpectrumPlot(Session &session, const Analyzer &analyzer, QWidget *parent)
    : PlotWidget(parent)
    , m_session(session)
    , m_analyzer(analyzer)
{
    m_spinner.setInterval(60);
    connect(&m_spinner, &QTimer::timeout, this, [this] {
        m_spinnerAngle = (m_spinnerAngle + 30) % 360;
        update();
    });
}

void SpectrumPlot::analysisChanged()
{
    const bool waiting = m_session.loaded && !analysis() && m_analyzer.pending();
    if (waiting && !m_spinner.isActive())
        m_spinner.start();
    else if (!waiting)
        m_spinner.stop();
    if (!analysis())
        m_hoverPeak.reset();
    update();
}

const dsp::Analysis *SpectrumPlot::analysis() const
{
    return m_session.loaded ? m_analyzer.result(m_session.loaded->id) : nullptr;
}

bool SpectrumPlot::hasData() const
{
    return analysis() != nullptr;
}

void SpectrumPlot::paintEmpty(QPainter &painter)
{
    if (!m_session.loaded) {
        paintMessage(painter, uiText()->spectrumEmpty);
    } else if (m_analyzer.pending()) {
        // calcul en cours : un arc qui tourne
        painter.setRenderHint(QPainter::Antialiasing, true);
        const double side = 28.0 * uiScale();
        const QRectF box(rect().center().x() - side / 2, rect().center().y() - side / 2, side, side);
        painter.setPen(QPen(palette().color(QPalette::PlaceholderText), 3.0 * uiScale(), Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(box, -m_spinnerAngle * 16, 270 * 16);
    } else {
        paintMessage(painter, uiText()->extractTooShort);
    }
}

double SpectrumPlot::toX(double f) const
{
    return logAxis() ? std::log10(std::max(f, 1e-3)) : f;
}

double SpectrumPlot::fromX(double x) const
{
    return logAxis() ? std::pow(10.0, x) : x;
}

double SpectrumPlot::toY(double a) const
{
    if (!decibels())
        return a;
    const double maxA = std::max(analysis() ? analysis()->spectrum.maxAmplitude() : 1.0, MIN_AMPLITUDE);
    return std::max(toDb(a), toDb(maxA) - DB_RANGE);
}

QString SpectrumPlot::amplitudeText(double y) const
{
    const QChar d = uiText()->decimal;
    return decibels() ? fmtDec(y, 1, d) + u" dB"_s : fmtNum(y, 3, d);
}

std::pair<double, double> SpectrumPlot::defaultX() const
{
    const double fmin = logAxis() ? FMIN : 0.0;
    return {toX(fmin), toX(m_session.fmaxDisplay())};
}

std::pair<double, double> SpectrumPlot::rangeY() const
{
    const double maxA = std::max(analysis() ? analysis()->spectrum.maxAmplitude() : 1.0, MIN_AMPLITUDE);
    if (decibels())
        return {toDb(maxA) - DB_RANGE, toDb(maxA) + 6.0};
    return {0.0, maxA * 1.15};
}

QString SpectrumPlot::xAxisLabel() const
{
    return uiText()->axisFreq;
}

QString SpectrumPlot::yAxisLabel() const
{
    return decibels() ? uiText()->axisDb : uiText()->axisAmplitude;
}

std::vector<PlotWidget::Tick> SpectrumPlot::xTicks(double x0, double x1, double pixels) const
{
    if (!logAxis())
        return PlotWidget::xTicks(x0, x1, pixels);
    // axe logarithmique : 1, 2, 5 × 10ⁿ
    std::vector<Tick> ticks;
    for (double m : logGridMarks(x0, x1))
        ticks.push_back({m, fmtFreqShort(std::pow(10.0, m), uiText()->decimal)});
    return ticks;
}

QString SpectrumPlot::hoverText(QPointF data) const
{
    const QChar d = uiText()->decimal;
    const double f = fromX(data.x());
    QString text = u"f = %1\n%2"_s.arg(fmtFreq(f, d), amplitudeText(data.y()));
    if (const auto peak = nearestPeak(analysis()->peaks, f, logAxis())) {
        text += u"\n\n%1\nf = %2\n%3"_s.arg(uiText()->nearestPeak, fmtFreq(peak->freq, d),
                                            amplitudeText(toY(peak->amplitude)));
    }
    return text;
}

void SpectrumPlot::paintData(QPainter &painter, const QRectF &r)
{
    const dsp::Analysis &a = *analysis();
    const Settings active = m_session.active();
    const QPalette pal = palette();
    const double s = uiScale();
    const QColor lineColor = spectrumColor(pal);
    const bool log = logAxis();

    const std::vector<QPointF> points = spectrumPoints(a.spectrum, a.peaks, fromX(viewX0()), fromX(viewX1()),
                                                       size_t(std::max(r.width(), 1.0)), log);
    QPolygonF line;
    line.reserve(qsizetype(points.size()));
    for (const QPointF &p : points)
        line << QPointF(screenX(toX(p.x())), screenY(toY(p.y())));
    painter.setPen(QPen(lineColor, 1.2 * s));
    drawCurve(painter, line);

    // repères des harmoniques n × f₁ (mode expert)
    const std::optional<double> f0 = m_session.manualF0 ? m_session.manualF0 : a.fundamental;
    if (active.expert && active.harmonicMarks && f0) {
        QFont rank = font();
        rank.setWeight(TITLE_WEIGHT);
        painter.setFont(rank);
        const QFontMetricsF fm(rank);
        const QColor harmonic = harmonicColor(pal);
        for (const dsp::Harmonic &h : dsp::harmonics(a.peaks, *f0, a.spectrum.df(), size_t(active.harmonics))) {
            const double x = screenX(toX(h.expected));
            const QColor color = h.peak ? harmonic : faded(harmonic, 0.4);
            QPen dashed(color, 1.0 * s, Qt::CustomDashLine);
            dashed.setDashPattern({3.0, 5.0});
            painter.setPen(dashed);
            painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            painter.setPen(color);
            painter.drawText(QRectF(x - 20.0, r.top() + 1.0, 40.0, fm.height()), Qt::AlignHCenter | Qt::AlignTop,
                             QString::number(h.rank));
        }
    }
    // pics repérés (mode expert)
    if (active.expert) {
        painter.setPen(QPen(lineColor, 1.2 * s));
        painter.setBrush(Qt::NoBrush);
        for (const dsp::Peak &p : a.peaks)
            painter.drawEllipse(QPointF(screenX(toX(p.freq)), screenY(toY(p.amplitude))), 3.0 * s, 3.0 * s);
    }
    // le pic le plus proche de la souris, marqué d'un rond plein
    if (m_hoverPeak) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(cursorColorT(pal));
        painter.drawEllipse(QPointF(screenX(toX(m_hoverPeak->freq)), screenY(toY(m_hoverPeak->amplitude))), 3.5 * s,
                            3.5 * s);
    }
}

void SpectrumPlot::hoverMoved(std::optional<QPointF> pos)
{
    const std::optional<dsp::Peak> before = m_hoverPeak;
    if (pos && analysis() && plotRect().contains(*pos))
        m_hoverPeak = nearestPeak(analysis()->peaks, fromX(dataX(pos->x())), logAxis());
    else
        m_hoverPeak.reset();
    if (m_hoverPeak != before)
        update();
}

void SpectrumPlot::rightClicked(QPointF pos)
{
    if (!m_session.active().expert || !analysis())
        return;
    const QChar d = uiText()->decimal;
    hoverMoved(pos);
    const std::optional<dsp::Peak> peak = m_hoverPeak;
    QMenu menu(this);
    if (peak) {
        connect(menu.addAction(uiText()->setF0(fmtFreq(peak->freq, d))), &QAction::triggered, this,
                [this, f = peak->freq] { emit fundamentalChosen(f); });
    } else {
        menu.addAction(uiText()->noPeak)->setEnabled(false);
    }
    if (m_session.manualF0) {
        connect(menu.addAction(uiText()->autoButton), &QAction::triggered, this,
                [this] { emit fundamentalChosen(std::nullopt); });
    }
    menu.exec(mapToGlobal(pos.toPoint()));
}
