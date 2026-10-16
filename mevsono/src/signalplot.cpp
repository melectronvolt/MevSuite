#include "signalplot.h"

#include "format.h"
#include "style.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Distance (pixels) en dessous de laquelle on attrape un curseur.
constexpr double GRAB_RADIUS = 8.0;
/// Déplacement (pixels) au-delà duquel un appui devient un glisser, et non plus un clic.
constexpr double CLICK_DISTANCE = 6.0;

} // namespace

SignalPlot::SignalPlot(Session &session, QWidget *parent)
    : PlotWidget(parent)
    , m_session(session)
{
}

void SignalPlot::setPlayhead(std::optional<double> t)
{
    if (t == m_playhead)
        return;
    m_playhead = t;
    update();
}

bool SignalPlot::hasData() const
{
    return m_session.loaded.has_value();
}

void SignalPlot::paintEmpty(QPainter &painter)
{
    QFont font = this->font();
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(font.pointSizeF() * 1.2);
    painter.setFont(font);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(rect().adjusted(20, 10, -20, -10), Qt::AlignCenter | Qt::TextWordWrap, uiText()->emptyHint);
}

std::pair<double, double> SignalPlot::defaultX() const
{
    return {0.0, std::max(m_session.loaded->audio.duration(), 1e-3)};
}

std::pair<double, double> SignalPlot::rangeY() const
{
    const double height = m_session.loaded ? m_session.loaded->peak * 1.1 : 1.0;
    return {-height, height};
}

QString SignalPlot::xAxisLabel() const
{
    return uiText()->axisTime;
}

QString SignalPlot::yAxisLabel() const
{
    return uiText()->axisSignal;
}

QString SignalPlot::hoverText(QPointF data) const
{
    const QChar d = uiText()->decimal;
    return u"t = %1\n%2"_s.arg(fmtTime(data.x(), d), fmtNum(data.y(), 3, d));
}

void SignalPlot::paintData(QPainter &painter, const QRectF &r)
{
    const Loaded &loaded = *m_session.loaded;
    const audio::Channel channel = m_session.channel();
    const std::vector<float> &signal = *loaded.audio.signal(channel);
    const QPalette pal = palette();
    const double s = uiScale();
    const bool extract = m_session.tool == Tool::Extract;

    // extrait analysé (étude des fréquences)
    if (extract) {
        const auto [lo, hi] = sorted(m_session.extract);
        painter.fillRect(QRectF(QPointF(screenX(lo), r.top()), QPointF(screenX(hi), r.bottom())), shadeColor(pal));
    }

    // signal : les échantillons, ou leur minimum et leur maximum par colonne de pixels
    const std::vector<QPointF> points =
        loaded.envelope(channel).points(signal, loaded.audio.rate, viewX0(), viewX1(), size_t(std::max(r.width(), 1.0)));
    QPolygonF line;
    line.reserve(qsizetype(points.size()));
    for (const QPointF &p : points)
        line << QPointF(screenX(p.x()), screenY(p.y()));
    painter.setPen(QPen(signalColor(pal), 1.2 * s));
    drawCurve(painter, line);

    // curseurs et leurs noms, sur un cartouche du fond : lisibles au-dessus du signal
    QFont name = font();
    name.setWeight(TITLE_WEIGHT);
    painter.setFont(name);
    const QFontMetricsF fm(name);
    const QColor tag = faded(pal.color(QPalette::Base), 0.85);
    const auto cursor = [&](double t, const QColor &color, const QString &text, bool top) {
        const double x = screenX(t);
        painter.setPen(QPen(color, 2.0 * s));
        painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        const double y = top ? r.top() + 2.0 : r.bottom() - fm.height() - 2.0;
        const QRectF box(x + 3.0, y, fm.horizontalAdvance(text) + 6.0, fm.height());
        painter.fillRect(box, tag);
        painter.setPen(color);
        painter.drawText(box, Qt::AlignCenter, text);
    };
    // les curseurs de l'activité choisie seulement : A et B, ou t₁ et t₂
    if (extract) {
        cursor(m_session.extract[0], cursorColorA(pal), u"A"_s, true);
        cursor(m_session.extract[1], cursorColorB(pal), u"B"_s, true);
    } else if (const auto &p = m_session.period) {
        cursor((*p)[0], cursorColorT(pal), u"t₁"_s, true);
        cursor((*p)[1], cursorColorT(pal), u"t₂"_s, true);
    }
    if (m_playhead) {
        const double x = screenX(*m_playhead);
        painter.setPen(QPen(COLOR_PLAYHEAD, 1.5 * s));
        painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
}

double SignalPlot::timeAt(double px) const
{
    return std::clamp(dataX(px), 0.0, m_session.loaded->audio.duration());
}

std::optional<Cursors> SignalPlot::pair() const
{
    if (m_session.tool == Tool::Period)
        return m_session.period;
    return m_session.extract;
}

void SignalPlot::setPair(const Cursors &c)
{
    if (m_session.tool == Tool::Period) {
        m_session.period = c;
        emit periodChanged();
    } else {
        m_session.extract = c;
        emit extractChanged();
    }
    update();
}

std::optional<int> SignalPlot::nearCursor(double px) const
{
    const auto p = pair();
    if (!p)
        return std::nullopt;
    std::optional<int> best;
    double bestDistance = GRAB_RADIUS;
    for (int i : {0, 1}) {
        const double dx = std::abs(screenX((*p)[size_t(i)]) - px);
        if (dx <= bestDistance && (!best || dx < bestDistance)) {
            best = i;
            bestDistance = dx;
        }
    }
    return best;
}

void SignalPlot::hoverMoved(std::optional<QPointF> pos)
{
    const bool resize = (m_drag && m_drag->cursor) || (pos && !m_drag && nearCursor(pos->x()));
    if (resize)
        setCursor(Qt::SizeHorCursor);
    else
        unsetCursor();
}

void SignalPlot::leftPressed(QPointF pos)
{
    m_pressedAt = pos;
    m_drag.reset();
}

void SignalPlot::leftMoved(QPointF pos)
{
    if (!m_pressedAt)
        return;
    if (!m_drag) {
        if (std::hypot(pos.x() - m_pressedAt->x(), pos.y() - m_pressedAt->y()) < CLICK_DISTANCE)
            return;
        // glisser près d'un curseur le déplace ; ailleurs, une nouvelle paire part du point d'appui
        std::optional<int> grabbed = nearCursor(m_pressedAt->x());
        // A et B l'un sur l'autre : vers la droite, on tire B ; vers la gauche, A
        const Cursors &e = m_session.extract;
        if (grabbed && m_session.tool == Tool::Extract && std::abs(screenX(e[0]) - screenX(e[1])) < 1.0)
            grabbed = pos.x() > m_pressedAt->x() ? 1 : 0;
        m_drag = Drag{grabbed, timeAt(m_pressedAt->x())};
    }
    const double t = timeAt(pos.x());
    // A reste avant B : un curseur de l'extrait s'arrête sur l'autre, une nouvelle paire met A à gauche
    const bool ordered = m_session.tool == Tool::Extract;
    if (m_drag->cursor) {
        if (auto p = pair()) {
            const auto i = size_t(*m_drag->cursor);
            (*p)[i] = !ordered ? t : i == 0 ? std::min(t, (*p)[1]) : std::max(t, (*p)[0]);
            setPair(*p);
        }
    } else if (ordered) {
        setPair({std::min(m_drag->anchor, t), std::max(m_drag->anchor, t)});
    } else {
        setPair({m_drag->anchor, t});
    }
}

void SignalPlot::leftReleased(QPointF pos)
{
    if (!m_pressedAt)
        return;
    if (m_drag) {
        if (m_drag->cursor)
            snap({*m_drag->cursor});
        else
            snap({0, 1});
        m_drag.reset();
    } else if (auto p = pair()) {
        // simple clic : le curseur le plus proche vient sous la souris
        const double t = timeAt(pos.x());
        const int i = std::abs(t - (*p)[0]) <= std::abs(t - (*p)[1]) ? 0 : 1;
        (*p)[size_t(i)] = t;
        setPair(*p);
        snap({i});
    }
    m_pressedAt.reset();
    // une fois lâchés, le curseur t₁ est le premier dans le temps
    if (auto &p = m_session.period; p && (*p)[0] > (*p)[1]) {
        std::swap((*p)[0], (*p)[1]);
        emit periodChanged();
    }
    hoverMoved(pos);
    update();
}

void SignalPlot::snap(std::initializer_list<int> moved)
{
    if (m_session.tool != Tool::Period || !m_session.active().snapZero || !m_session.period)
        return;
    const Loaded &loaded = *m_session.loaded;
    const std::vector<float> &signal = *loaded.audio.signal(m_session.channel());
    // rayon de recherche : 5 % de la largeur visible, et au moins 16 pixels
    const double visible = viewX1() - viewX0();
    const double radius = std::max(0.05 * visible, visible / std::max(plotRect().width(), 1.0) * GRAB_RADIUS * 2.0);
    Cursors &c = *m_session.period;
    for (int i : moved) {
        if (const auto t = dsp::risingZeroCrossing(signal, loaded.audio.rate, c[size_t(i)], radius))
            c[size_t(i)] = *t;
    }
    emit periodChanged();
}
