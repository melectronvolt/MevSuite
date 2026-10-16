#include "plot.h"

#include "style.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Intensités affichées (de 0 à 255, avec une marge de 5 %).
constexpr double Y_MIN = -12.75;
constexpr double Y_MAX = 267.75;

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

} // namespace

ProfilePlot::ProfilePlot(Session &session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    setMouseTracking(true);
    setMinimumHeight(120);
}

void ProfilePlot::setTr(const Tr *tr)
{
    m_tr = tr;
    update();
}

void ProfilePlot::resetView()
{
    setView(-0.05, 1.05);
}

void ProfilePlot::setView(double t0, double t1)
{
    m_t0 = t0;
    m_t1 = t1 > t0 ? t1 : t0 + 1e-6;
    update();
}

QRectF ProfilePlot::plotRect() const
{
    const QFontMetricsF fm(font());
    const double left = fm.horizontalAdvance(u"255"_s) + fm.height() + 16.0;
    // à droite, la moitié de la dernière graduation de l'axe horizontal (« 31,25 » sous le bord)
    const double right = std::max(14.0, fm.horizontalAdvance(u"00000"_s) / 2 + 6.0);
    const double bottom = 2.0 * fm.height() + 12.0;
    return QRectF(left, 8.0, std::max(width() - left - right, 10.0), std::max(height() - bottom - 8.0, 10.0));
}

double ProfilePlot::fractionAt(double px) const
{
    const QRectF r = plotRect();
    return m_t0 + (px - r.left()) / r.width() * (m_t1 - m_t0);
}

double ProfilePlot::screenX(double fraction) const
{
    const QRectF r = plotRect();
    return r.left() + (fraction - m_t0) / (m_t1 - m_t0) * r.width();
}

double ProfilePlot::screenY(double intensity) const
{
    const QRectF r = plotRect();
    return r.bottom() - (intensity - Y_MIN) / (Y_MAX - Y_MIN) * r.height();
}

void ProfilePlot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QPalette pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Window));
    if (!m_tr)
        return;
    const Tr &tr = *m_tr;
    const QChar d = tr.decimal;

    const Profile *profile = m_session.profile();
    if (!profile) {
        // indication, en couleur discrète
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap, tr.cutPrompt);
        return;
    }
    const auto [unit, perPx] = m_session.unitScale();
    const double len = profile->lenPx() * perPx;
    const QRectF r = plotRect();
    const QFontMetricsF fm(font());
    QColor grid = pal.color(QPalette::Text);
    grid.setAlpha(30);

    painter.fillRect(r, pal.color(QPalette::Base));

    // Graduations et grille. Abscisses en unité de longueur, ordonnées en intensité.
    painter.setPen(pal.color(QPalette::Text));
    if (len > 0.0) {
        const double x0 = m_t0 * len, x1 = m_t1 * len;
        const double step = niceStep(x1 - x0, r.width() / (fm.horizontalAdvance(u"0000000"_s) + 10.0));
        const int decimals = stepDecimals(step);
        double lastLabelRight = -1e9;
        // graduation au bord droit gardée malgré les arrondis
        for (auto k = static_cast<long long>(std::ceil(x0 / step - 1e-9)); k * step <= x1 + 1e-9 * step; ++k) {
            const double x = k * step;
            const double sx = screenX(x / len);
            painter.setPen(grid);
            painter.drawLine(QPointF(sx, r.top()), QPointF(sx, r.bottom()));
            painter.setPen(pal.color(QPalette::Text));
            const QString label = fmtDec(k == 0 ? 0.0 : x, decimals, d);
            // une étiquette plus longue que la marge reste entière, ramenée contre le bord ; celles
            // qui se chevaucheraient : la première seulement
            const double w = fm.horizontalAdvance(label);
            const double labelLeft = std::min(sx - w / 2, width() - w - 2.0);
            if (labelLeft < lastLabelRight + 6.0)
                continue;
            lastLabelRight = labelLeft + w;
            painter.drawText(QRectF(labelLeft - 2.0, r.bottom() + 2.0, w + 4.0, fm.height()),
                             Qt::AlignHCenter | Qt::AlignTop, label);
        }
    }
    const double yStep = niceStep(Y_MAX - Y_MIN, r.height() / (2.5 * fm.height()));
    for (auto k = static_cast<long long>(std::ceil(Y_MIN / yStep)); k * yStep <= Y_MAX; ++k) {
        const double y = k * yStep;
        const double sy = screenY(y);
        painter.setPen(grid);
        painter.drawLine(QPointF(r.left(), sy), QPointF(r.right(), sy));
        painter.setPen(pal.color(QPalette::Text));
        painter.drawText(QRectF(0.0, sy - fm.height() / 2, r.left() - 4.0, fm.height()),
                         Qt::AlignRight | Qt::AlignVCenter, fmtDec(y, stepDecimals(yStep), d));
    }

    // Titres des axes.
    painter.drawText(QRectF(r.left(), height() - fm.height() - 4.0, r.width(), fm.height()), Qt::AlignHCenter,
                     tr.axisX(unit));
    painter.save();
    painter.translate(4.0, r.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-r.height() / 2, 0.0, r.height(), fm.height()), Qt::AlignHCenter, tr.axisY);
    painter.restore();

    // Courbe et curseurs, limités à la zone du graphique.
    painter.save();
    painter.setClipRect(r);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const size_t n = profile->values.size();
    QPolygonF line;
    line.reserve(static_cast<qsizetype>(n));
    for (size_t k = 0; k < n; ++k)
        line << QPointF(screenX(n > 1 ? double(k) / double(n - 1) : 0.0), screenY(profile->values[k]));
    painter.setPen(QPen(isDark(pal) ? QColor(230, 230, 230) : QColor(30, 30, 30), 1.5));
    painter.drawPolyline(line);
    // curseurs et leurs noms, sur un cartouche du fond : lisibles au-dessus de la courbe
    QFont nameFont = font();
    nameFont.setWeight(TITLE_WEIGHT);
    painter.setFont(nameFont);
    const QFontMetricsF nameMetrics(nameFont);
    QColor tag = pal.color(QPalette::Base);
    tag.setAlphaF(0.85f);
    const QColor colors[] = {cursorColorA(pal), cursorColorB(pal)};
    const QString names[] = {u"A"_s, u"B"_s};
    for (int i : {0, 1}) {
        const double sx = screenX(m_session.cursorT[i]);
        painter.setPen(QPen(colors[i], 2.0));
        painter.drawLine(QPointF(sx, r.top()), QPointF(sx, r.bottom()));
        // à droite du trait, à gauche près du bord
        const double w = nameMetrics.horizontalAdvance(names[i]) + 6.0;
        const double left = sx + 3.0 + w > r.right() ? sx - 3.0 - w : sx + 3.0;
        const QRectF box(left, r.top() + 2.0, w, nameMetrics.height());
        painter.fillRect(box, tag);
        painter.setPen(colors[i]);
        painter.drawText(box, Qt::AlignCenter, names[i]);
    }
    painter.restore();

    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(r);

    // Position du pointeur.
    if (m_hover && r.contains(*m_hover)) {
        const double x = fractionAt(m_hover->x()) * len;
        const double y = Y_MIN + (r.bottom() - m_hover->y()) / r.height() * (Y_MAX - Y_MIN);
        const QString text = u"x = %1 %2\nI = %3"_s.arg(fmtNum(x, 4, d), unit, QString::number(std::lround(y)));
        QPen dashed(pal.color(QPalette::Text), 1.0, Qt::DashLine);
        painter.setPen(dashed);
        painter.drawLine(QPointF(m_hover->x(), r.top()), QPointF(m_hover->x(), r.bottom()));
        painter.drawLine(QPointF(r.left(), m_hover->y()), QPointF(r.right(), m_hover->y()));
        const QRectF box = fm.boundingRect(QRectF(0, 0, 400, 100), Qt::AlignLeft, text).adjusted(-4, -3, 4, 3);
        QPointF at = *m_hover + QPointF(12.0, -box.height() - 6.0);
        at.setX(std::min(at.x(), r.right() - box.width()));
        at.setY(std::max(at.y(), r.top()));
        const QRectF placed(at, box.size());
        painter.setPen(pal.color(QPalette::Mid));
        painter.setBrush(pal.color(QPalette::ToolTipBase));
        painter.drawRoundedRect(placed, 3, 3);
        painter.setPen(pal.color(QPalette::ToolTipText));
        painter.drawText(placed.adjusted(4, 3, -4, -3), Qt::AlignLeft, text);
    }
}

void ProfilePlot::moveCursor(QPointF pos)
{
    const double t = std::clamp(fractionAt(pos.x()), 0.0, 1.0);
    // Clic : le curseur le plus proche ; glisser : le même jusqu'au relâchement.
    const int i = m_draggedCursor.value_or(
        std::abs(t - m_session.cursorT[0]) <= std::abs(t - m_session.cursorT[1]) ? 0 : 1);
    m_draggedCursor = i;
    m_session.cursorT[i] = t;
    emit cursorsMoved();
    update();
}

void ProfilePlot::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_session.profile() && plotRect().contains(event->position()))
        moveCursor(event->position());
}

void ProfilePlot::mouseMoveEvent(QMouseEvent *event)
{
    m_hover = event->position();
    if (m_draggedCursor && (event->buttons() & Qt::LeftButton))
        moveCursor(event->position());
    else
        update();
}

void ProfilePlot::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_draggedCursor.reset();
}

void ProfilePlot::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        resetView();
}

void ProfilePlot::wheelEvent(QWheelEvent *event)
{
    if (!m_session.profile())
        return;
    const QPoint angle = event->angleDelta();
    const QPoint pixels = event->pixelDelta();
    const QPoint raw = pixels.isNull() ? angle * 50 / 120 : pixels;
    // Selon le système, Maj + molette arrive en défilement vertical ou horizontal.
    const double delta = raw.y() != 0 ? raw.y() : raw.x();
    const double span = m_t1 - m_t0;
    if (event->modifiers() & Qt::ShiftModifier || (raw.y() == 0 && raw.x() != 0)) {
        // Déplacement horizontal.
        const double shift = -delta / plotRect().width() * span;
        m_t0 += shift;
        m_t1 += shift;
    } else {
        // Zoom horizontal autour de la souris, comme sur l'image.
        const double factor = std::exp(delta * 0.002);
        const double newSpan = std::clamp(span / factor, 1e-6, 100.0);
        const double anchor = fractionAt(event->position().x());
        m_t0 = anchor - (anchor - m_t0) * newSpan / span;
        m_t1 = m_t0 + newSpan;
    }
    update();
    event->accept();
}

void ProfilePlot::leaveEvent(QEvent *)
{
    m_hover.reset();
    update();
}
