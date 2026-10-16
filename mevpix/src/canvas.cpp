#include "canvas.h"

#include "style.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Distance (pixels écran) en dessous de laquelle on attrape l'extrémité d'un segment.
constexpr double HANDLE_RADIUS = 9.0;
/// Longueur minimale (pixels écran) d'un nouveau segment, pour ignorer les simples clics.
constexpr double MIN_SEGMENT = 4.0;

} // namespace

ImageCanvas::ImageCanvas(Session &session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(100, 100);
}

void ImageCanvas::setTr(const Tr *tr)
{
    m_tr = tr;
    update();
}

void ImageCanvas::setUiScale(double scale)
{
    m_scale = scale;
    update();
}

void ImageCanvas::requestFit()
{
    m_view.requestFit();
    update();
}

void ImageCanvas::cancelDrag()
{
    m_drag.reset();
    m_preview.reset();
    update();
}

void ImageCanvas::layoutView()
{
    if (m_session.image)
        m_view.layout(QSizeF(size()), m_session.image->size());
}

std::optional<ImageCanvas::Drag> ImageCanvas::grabAt(QPointF pos) const
{
    std::optional<Drag> best;
    double bestDistance = HANDLE_RADIUS * m_scale;
    const auto consider = [&](const Drag &drag, QPointF p) {
        const double d = length(m_view.toScreen(p) - pos);
        if (d < bestDistance || (!best && d <= bestDistance)) {
            best = drag;
            bestDistance = d;
        }
    };
    for (const auto &[target, seg] : m_session.editable()) {
        consider(Drag{Drag::Handle, {}, target, false, 0}, seg.a);
        consider(Drag{Drag::Handle, {}, target, true, 0}, seg.b);
    }
    if (m_session.tool == Tool::Profile && m_session.cut) {
        for (int i : {0, 1})
            consider(Drag{Drag::Cursor, {}, Target{Target::Cut}, false, i}, m_session.cut->pointAt(m_session.cursorT[i]));
    }
    return best;
}

void ImageCanvas::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    m_lastPointer = pos;
    if (!m_session.image)
        return;
    layoutView();
    if (event->button() == Qt::LeftButton) {
        m_drag = grabAt(pos);
        if (!m_drag)
            m_drag = Drag{Drag::New, m_session.image->clamp(m_view.toImage(pos))};
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panning = true;
    }
    updateCursorShape(pos);
}

void ImageCanvas::dragTo(QPointF pos, bool shift)
{
    if (!m_drag || !m_session.image)
        return;
    const QPointF p = m_session.image->clamp(m_view.toImage(pos));
    switch (m_drag->kind) {
    case Drag::New:
        m_preview = Segment{m_drag->start, shift ? snapAxis(m_drag->start, p) : p};
        break;
    case Drag::Handle:
        if (Segment *seg = m_session.segment(m_drag->target)) {
            const QPointF fixed = m_drag->endB ? seg->a : seg->b;
            const QPointF moved = shift ? snapAxis(fixed, p) : p;
            (m_drag->endB ? seg->b : seg->a) = moved;
            emit edited();
        }
        break;
    case Drag::Cursor:
        if (m_session.cut) {
            m_session.cursorT[m_drag->cursor] = m_session.cut->project(p);
            emit edited();
        }
        break;
    }
    update();
}

void ImageCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (m_panning) {
        m_view.pan(pos - m_lastPointer);
        update();
    } else if (event->buttons() & Qt::LeftButton) {
        dragTo(pos, event->modifiers() & Qt::ShiftModifier);
    }
    m_lastPointer = pos;
    updateCursorShape(pos);
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_drag) {
        // Un segment plus court qu'un pixel de l'image (fort zoom) ne mesure rien et ne peut pas
        // servir d'étalon : il est ignoré comme un simple clic.
        const std::optional<Segment> seg = std::exchange(m_preview, std::nullopt);
        m_drag.reset();
        if (seg && seg->lenPx() * m_view.zoom() >= MIN_SEGMENT && seg->lenPx() >= 1.0)
            emit segmentDrawn(*seg);
        update();
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panning = false;
    }
    updateCursorShape(event->position());
}

void ImageCanvas::keyPressEvent(QKeyEvent *event)
{
    // Maj enfoncée ou relâchée pendant le tracé : le segment suit sans attendre un mouvement de souris.
    if (event->key() == Qt::Key_Shift && m_drag)
        dragTo(m_lastPointer, true);
    else
        QWidget::keyPressEvent(event);
}

void ImageCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift && m_drag)
        dragTo(m_lastPointer, false);
    else
        QWidget::keyReleaseEvent(event);
}

void ImageCanvas::wheelEvent(QWheelEvent *event)
{
    if (!m_session.image)
        return;
    // Molette : zoom centré sur la souris (un cran ≈ 10 %) ; pavé tactile : défilement fin.
    const double delta =
        event->pixelDelta().isNull() ? event->angleDelta().y() / 120.0 * 50.0 : double(event->pixelDelta().y());
    if (delta != 0.0) {
        layoutView();
        m_view.zoomAround(event->position(), std::exp(delta * 0.002));
        update();
    }
    event->accept();
}

bool ImageCanvas::event(QEvent *event)
{
    // Pincement sur un pavé tactile : zoom centré sur les doigts.
    if (event->type() == QEvent::NativeGesture && m_session.image) {
        const auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            layoutView();
            m_view.zoomAround(gesture->position(), 1.0 + gesture->value());
            update();
            return true;
        }
    }
    return QWidget::event(event);
}

void ImageCanvas::updateCursorShape(QPointF pos)
{
    if (!m_session.image) {
        unsetCursor();
        return;
    }
    if (m_drag && m_drag->kind != Drag::New)
        setCursor(Qt::ClosedHandCursor);
    else if (m_panning)
        setCursor(Qt::SizeAllCursor);
    else if (!m_drag && grabAt(pos))
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(Qt::CrossCursor);
}

void ImageCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // Fond clair en thème clair, sombre en thème sombre ; un peu différent de celui des panneaux pour
    // que les bords de l'image restent visibles.
    const bool dark = isDark(palette());
    painter.fillRect(rect(), dark ? QColor(20, 20, 22) : QColor(222, 223, 226));

    if (m_session.image) {
        layoutView();
        m_view.paintImage(painter, QSizeF(size()), *m_session.image);
        paintOverlays(painter);
        if (m_view.zoom() != m_reportedZoom) {
            m_reportedZoom = m_view.zoom();
            emit zoomChanged(m_reportedZoom);
        }
    }
    // Sans image, la fenêtre montre la page d'accueil à la place de la zone image.
}

void ImageCanvas::paintOverlays(QPainter &painter) const
{
    if (!m_tr)
        return;
    const Tr &tr = *m_tr;
    const QChar d = tr.decimal;
    const auto &calib = m_session.calibration;
    const auto screen = [&](QPointF p) { return m_view.toScreen(p); };
    const double s = m_scale;

    // Chaque outil n'affiche que ses segments : l'étalon (jaune) avec Étalonner, les mesures (bleu)
    // avec Mesurer, la ligne de coupe (rose) avec Profil d'intensité.
    if (const auto &seg = m_session.calibSeg; seg && m_session.tool == Tool::Calibrate) {
        paintSegment(painter, screen(seg->a), screen(seg->b), COLOR_CALIB, true, s);
        const QString value = calib && calib->matches(Calibration{calib->realLen, seg->lenPx(), calib->unit})
                                  ? fmtNum(calib->realLen, 3, d) + u' ' + unitSymbol(calib->unit)
                                  : fmtNum(seg->lenPx(), 4, d) + u" px"_s;
        paintLabel(painter, labelAnchor(m_view, *seg, s), tr.calibTag(value), COLOR_CALIB, s);
    }

    for (size_t i = 0; m_session.tool == Tool::Measure && i < m_session.measures.size(); ++i) {
        const Segment &seg = m_session.measures[i];
        paintSegment(painter, screen(seg.a), screen(seg.b), COLOR_MEASURE, true, s);
        const QString text = tr.measureTag(static_cast<int>(i) + 1, fmtLen(seg.lenPx(), calib, d));
        paintLabel(painter, labelAnchor(m_view, seg, s), text, COLOR_MEASURE, s);
    }

    if (m_session.tool == Tool::Profile && m_session.cut) {
        const Segment &seg = *m_session.cut;
        if (m_session.band > 1) {
            const QPointF half = seg.normal() * (m_session.band / 2.0) * m_view.zoom();
            QColor band = COLOR_CUT;
            band.setAlphaF(0.25f);
            paintBand(painter, screen(seg.a), screen(seg.b), half, band);
        }
        paintSegment(painter, screen(seg.a), screen(seg.b), COLOR_CUT, true, s);
        const std::pair<QString, QColor> cursors[] = {{u"A"_s, COLOR_A}, {u"B"_s, COLOR_B}};
        for (int i : {0, 1}) {
            const QPointF p = screen(seg.pointAt(m_session.cursorT[i]));
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(Qt::black, 1.5 * s));
            painter.setBrush(cursors[i].second);
            painter.drawEllipse(p, 6.0 * s, 6.0 * s);
            painter.restore();
            paintLabel(painter, p + QPointF(0.0, -10.0 * s), cursors[i].first, cursors[i].second, s, true);
        }
    }

    if (const auto &seg = m_preview) {
        const QColor color = m_session.tool == Tool::Calibrate ? COLOR_CALIB
                           : m_session.tool == Tool::Measure   ? COLOR_MEASURE
                                                               : COLOR_CUT;
        paintSegment(painter, screen(seg->a), screen(seg->b), color, false, s);
        const QString text = m_session.tool == Tool::Calibrate ? fmtNum(seg->lenPx(), 4, d) + u" px"_s
                                                               : fmtLen(seg->lenPx(), calib, d);
        paintLabel(painter, labelAnchor(m_view, *seg, s), text, color, s);
    }
}
