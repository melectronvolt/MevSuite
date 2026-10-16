#include "plot.h"

#include "format.h"
#include "style.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Pas « rond » (1, 2 ou 5 × 10ⁿ) pour environ `count` graduations sur `range`.
double niceStep(double range, double count)
{
    const double raw = range / std::max(count, 1.0);
    if (!(raw > 0.0) || !std::isfinite(raw))
        return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    return (norm <= 1.0 ? 1.0 : norm <= 2.0 ? 2.0 : norm <= 5.0 ? 5.0 : 10.0) * mag;
}

int stepDecimals(double step)
{
    return std::max(0, -static_cast<int>(std::floor(std::log10(step) + 1e-9)));
}

/// Largeurs extrêmes de la vue : en deçà, les nombres n'ont plus de sens ; au-delà, plus rien n'est visible.
constexpr double MIN_SPAN = 1e-9;
constexpr double MAX_SPAN = 1e9;

} // namespace

PlotWidget::PlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(100);
    setFocusPolicy(Qt::NoFocus);
}

void PlotWidget::setTr(const Tr *tr)
{
    m_tr = tr;
    update();
}

void PlotWidget::setUiScale(double scale)
{
    m_uiScale = scale;
    update();
}

void PlotWidget::resetView()
{
    m_needsReset = true;
    update();
}

void PlotWidget::setViewX(double x0, double x1)
{
    if (!(x1 > x0))
        return;
    m_x0 = x0;
    m_x1 = x1;
    m_needsReset = false;
    update();
}

void PlotWidget::ensureView()
{
    if (!m_needsReset)
        return;
    const auto [x0, x1] = defaultX();
    m_x0 = x0;
    m_x1 = x1 > x0 ? x1 : x0 + 1.0;
    m_needsReset = false;
}

QRectF PlotWidget::plotRect() const
{
    const QFontMetricsF fm(font());
    const double left = fm.horizontalAdvance(u"−000,00"_s) + fm.height() + 14.0;
    // à droite, la moitié de la dernière graduation de l'axe horizontal (« 5000 » sous le bord)
    const double right = std::max(14.0, fm.horizontalAdvance(u"00000"_s) / 2 + 6.0);
    const double bottom = 2.0 * fm.height() + 10.0;
    return QRectF(left, 8.0, std::max(width() - left - right, 10.0), std::max(height() - bottom - 8.0, 10.0));
}

double PlotWidget::screenX(double x) const
{
    const QRectF r = plotRect();
    return r.left() + (x - m_x0) / (m_x1 - m_x0) * r.width();
}

double PlotWidget::dataX(double px) const
{
    const QRectF r = plotRect();
    return m_x0 + (px - r.left()) / r.width() * (m_x1 - m_x0);
}

double PlotWidget::screenY(double y) const
{
    const QRectF r = plotRect();
    const auto [y0, y1] = rangeY();
    return r.bottom() - (y - y0) / (y1 - y0) * r.height();
}

double PlotWidget::dataY(double py) const
{
    const QRectF r = plotRect();
    const auto [y0, y1] = rangeY();
    return y0 + (r.bottom() - py) / r.height() * (y1 - y0);
}

std::vector<PlotWidget::Tick> PlotWidget::niceTicks(double lo, double hi, double count) const
{
    std::vector<Tick> ticks;
    const double step = niceStep(hi - lo, count);
    const int decimals = stepDecimals(step);
    const QChar d = m_tr ? m_tr->decimal : u'.';
    for (auto k = static_cast<long long>(std::ceil(lo / step)); k * step <= hi && ticks.size() < 200; ++k) {
        const double v = double(k) * step;
        ticks.push_back({v, fmtDec(k == 0 ? 0.0 : v, decimals, d)});
    }
    return ticks;
}

std::vector<PlotWidget::Tick> PlotWidget::xTicks(double x0, double x1, double pixels) const
{
    const QFontMetricsF fm(font());
    return niceTicks(x0, x1, pixels / (fm.horizontalAdvance(u"0000000"_s) + 10.0));
}

void PlotWidget::paintMessage(QPainter &painter, const QString &text, double scale)
{
    QFont font = this->font();
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(font.pointSizeF() * scale);
    painter.setFont(font);
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, text);
}

void PlotWidget::drawCurve(QPainter &painter, const QPolygonF &line)
{
    // extrémités carrées (celles du stylo par défaut) : les segments se chevauchent aux sommets, sans
    // trou ; des extrémités rondes coûteraient dix fois plus
    QList<QLineF> segments;
    segments.reserve(std::max<qsizetype>(line.size() - 1, 0));
    for (qsizetype i = 0; i + 1 < line.size(); ++i)
        segments << QLineF(line[i], line[i + 1]);
    painter.drawLines(segments);
}

void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QPalette pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Window));
    if (!m_tr)
        return;
    if (!hasData()) {
        paintEmpty(painter);
        return;
    }
    ensureView();
    const auto [y0, y1] = rangeY();
    const QRectF r = plotRect();
    const QFontMetricsF fm(font());
    QColor grid = pal.color(QPalette::Text);
    grid.setAlpha(30);
    const QColor text = pal.color(QPalette::Text);
    painter.fillRect(r, pal.color(QPalette::Base));

    // Graduations et grille.
    double lastLabelRight = -1e9;
    for (const Tick &tick : xTicks(m_x0, m_x1, r.width())) {
        const double sx = screenX(tick.value);
        if (sx < r.left() - 0.5 || sx > r.right() + 0.5)
            continue;
        painter.setPen(grid);
        painter.drawLine(QPointF(sx, r.top()), QPointF(sx, r.bottom()));
        // étiquettes qui se chevaucheraient (graduations logarithmiques serrées) : la première seulement
        const double w = fm.horizontalAdvance(tick.label);
        // une étiquette plus longue que la marge reste entière, ramenée contre le bord
        const double labelLeft = std::min(sx - w / 2, width() - w - 2.0);
        if (tick.label.isEmpty() || labelLeft < lastLabelRight + 6.0)
            continue;
        lastLabelRight = labelLeft + w;
        painter.setPen(text);
        painter.drawText(QRectF(labelLeft - 2.0, r.bottom() + 2.0, w + 4.0, fm.height()), Qt::AlignCenter, tick.label);
    }
    for (const Tick &tick : niceTicks(y0, y1, r.height() / (2.5 * fm.height()))) {
        const double sy = screenY(tick.value);
        painter.setPen(grid);
        painter.drawLine(QPointF(r.left(), sy), QPointF(r.right(), sy));
        painter.setPen(text);
        painter.drawText(QRectF(0.0, sy - fm.height() / 2, r.left() - 4.0, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         tick.label);
    }

    // Titres des axes.
    painter.drawText(QRectF(r.left(), height() - fm.height() - 4.0, r.width(), fm.height()), Qt::AlignHCenter,
                     xAxisLabel());
    painter.save();
    painter.translate(2.0, r.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-r.height() / 2, 0.0, r.height(), fm.height()), Qt::AlignHCenter, yAxisLabel());
    painter.restore();

    // Courbes et repères, limités à la zone du graphique.
    painter.save();
    painter.setClipRect(r);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintData(painter, r);
    painter.restore();

    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(r);

    // Réticule : valeur sous la souris.
    if (m_hover && r.contains(*m_hover)) {
        const QPointF data(dataX(m_hover->x()), dataY(m_hover->y()));
        const QString label = hoverText(data);
        QPen dashed(faded(text, 0.6), 1.0, Qt::DashLine);
        painter.setPen(dashed);
        painter.drawLine(QPointF(m_hover->x(), r.top()), QPointF(m_hover->x(), r.bottom()));
        painter.drawLine(QPointF(r.left(), m_hover->y()), QPointF(r.right(), m_hover->y()));
        const QRectF box = fm.boundingRect(QRectF(0, 0, 600, 400), Qt::AlignLeft, label).adjusted(-5, -4, 5, 4);
        QPointF at = *m_hover + QPointF(14.0, -box.height() - 8.0);
        // près d'un bord, la boîte passe de l'autre côté du pointeur
        if (at.x() + box.width() > r.right())
            at.setX(m_hover->x() - box.width() - 14.0);
        if (at.y() < r.top())
            at.setY(m_hover->y() + 14.0);
        const QRectF placed(at, box.size());
        painter.setPen(pal.color(QPalette::Mid));
        painter.setBrush(faded(pal.color(QPalette::ToolTipBase), 0.92));
        painter.drawRoundedRect(placed, 4, 4);
        painter.setPen(pal.color(QPalette::ToolTipText));
        painter.drawText(placed.adjusted(5, 4, -5, -4), Qt::AlignLeft, label);
    }
}

void PlotWidget::panPixels(double pixels)
{
    const double perPixel = (m_x1 - m_x0) / std::max(plotRect().width(), 1.0);
    m_x0 -= pixels * perPixel;
    m_x1 -= pixels * perPixel;
    update();
}

void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    // seule la zone des courbes réagit, pas les graduations ni les titres des axes
    if (!hasData() || !plotRect().contains(event->position()))
        return;
    if (event->button() == Qt::LeftButton) {
        leftPressed(event->position());
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panFrom = event->position();
        m_panButton = event->button();
        m_panDistance = 0.0;
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_hover = event->position();
    if (hasData()) {
        hoverMoved(m_hover);
        if (m_panFrom && (event->buttons() & m_panButton)) {
            const QPointF delta = event->position() - *m_panFrom;
            m_panDistance += std::abs(delta.x()) + std::abs(delta.y());
            m_panFrom = event->position();
            panPixels(delta.x());
        }
        if (event->buttons() & Qt::LeftButton)
            leftMoved(event->position());
    }
    update();
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!hasData())
        return;
    if (event->button() == Qt::LeftButton) {
        leftReleased(event->position());
    } else if (m_panFrom && event->button() == m_panButton) {
        if (m_panButton == Qt::RightButton && m_panDistance < 4.0)
            rightClicked(event->position());
        m_panFrom.reset();
    }
    update();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && hasData())
        resetView();
}

void PlotWidget::wheelEvent(QWheelEvent *event)
{
    if (!hasData())
        return;
    ensureView();
    const QPoint angle = event->angleDelta();
    const QPoint pixels = event->pixelDelta();
    const QPoint raw = pixels.isNull() ? angle * 50 / 120 : pixels;
    // Selon le système, Maj + molette arrive en défilement vertical ou horizontal.
    const double delta = raw.y() != 0 ? raw.y() : raw.x();
    if (event->modifiers() & Qt::ShiftModifier || (raw.y() == 0 && raw.x() != 0)) {
        panPixels(delta);
    } else {
        const double span = m_x1 - m_x0;
        const double newSpan = std::clamp(span / std::exp(delta * 0.002), MIN_SPAN, MAX_SPAN);
        const double anchor = dataX(event->position().x());
        m_x0 = anchor - (anchor - m_x0) * newSpan / span;
        m_x1 = m_x0 + newSpan;
        update();
    }
    event->accept();
}

void PlotWidget::leaveEvent(QEvent *)
{
    m_hover.reset();
    hoverMoved(std::nullopt);
    update();
}
