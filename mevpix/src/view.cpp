#include "view.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

/// Côté maximal de l'image d'affichage. Au-delà, l'image est réduite pour l'écran (les mesures et le
/// profil utilisent toujours les pixels d'origine) : une photo de 100 Mpx occuperait sinon plusieurs
/// centaines de Mo de plus.
constexpr int MAX_DISPLAY_SIDE = 8192;
/// Plus petit côté des réductions successives de l'image d'affichage.
constexpr int MIN_LEVEL_SIDE = 256;
/// Mémoire maximale accordée au décodage d'une image, en Mo (256 par défaut dans Qt : trop peu pour
/// une photo de 100 Mpx).
constexpr int ALLOCATION_LIMIT_MB = 2048;

/// Zoom de l'image (pixels écran par pixel image).
constexpr double ZOOM_MIN = 0.02;
constexpr double ZOOM_MAX = 40.0;

LoadResult unsupported()
{
    LoadResult result;
    result.unsupported = true;
    return result;
}

} // namespace

QPointF LoadedImage::clamp(QPointF p) const
{
    return QPointF(std::clamp(p.x(), 0.0, double(rgb.width())), std::clamp(p.y(), 0.0, double(rgb.height())));
}

LoadResult loadImage(const QString &path)
{
    QImageReader reader(path);
    // Le format est reconnu d'après le contenu, pas d'après l'extension (photo renommée…).
    reader.setDecideFormatFromContent(true);
    // Les photos de téléphone sont souvent stockées tournées, avec l'orientation dans l'EXIF.
    reader.setAutoTransform(true);
    reader.setAllocationLimit(ALLOCATION_LIMIT_MB);
    QImage decoded = reader.read();
    if (decoded.isNull()) {
        if (reader.error() == QImageReader::UnsupportedFormatError)
            return unsupported();
        LoadResult result;
        result.error = reader.errorString();
        return result;
    }
    if (decoded.width() == 0 || decoded.height() == 0)
        return unsupported();

    auto img = std::make_shared<LoadedImage>();
    img->name = QFileInfo(path).fileName();
    img->rgb = decoded.convertToFormat(QImage::Format_RGB888);
    decoded = QImage();

    QImage level = img->rgb.convertToFormat(QImage::Format_RGB32);
    if (level.width() > MAX_DISPLAY_SIDE || level.height() > MAX_DISPLAY_SIDE)
        level = level.scaled(MAX_DISPLAY_SIDE, MAX_DISPLAY_SIDE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img->levels.push_back(level);
    while (std::max(level.width(), level.height()) / 2 >= MIN_LEVEL_SIDE) {
        level = level.scaled(std::max(level.width() / 2, 1), std::max(level.height() / 2, 1), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
        img->levels.push_back(level);
    }
    LoadResult result;
    result.image = std::move(img);
    return result;
}

void ImageView::fit(QSizeF area, QSizeF imgSize)
{
    const double zoom = std::min(area.width() / imgSize.width(), area.height() / imgSize.height()) * 0.98;
    // Zone d'affichage réduite à rien (fenêtre minuscule, interface très agrandie) : zoom valide quand même.
    m_zoom = std::isfinite(zoom) && zoom > 0.0 ? std::clamp(zoom, ZOOM_MIN, ZOOM_MAX) : 1.0;
    const QSizeF free = area - imgSize * m_zoom;
    m_offset = QPointF(free.width(), free.height()) / 2.0;
    m_navigated = false;
}

void ImageView::layout(QSizeF area, QSizeF imgSize)
{
    // Réajustement automatique quand la zone change de taille (fenêtre, panneau du profil).
    if (m_fitRequested || (!m_navigated && area != m_lastSize)) {
        fit(area, imgSize);
        m_fitRequested = false;
    }
    m_lastSize = area;
}

void ImageView::zoomAround(QPointF pointer, double factor)
{
    const QPointF anchor = toImage(pointer);
    m_zoom = std::clamp(m_zoom * factor, ZOOM_MIN, ZOOM_MAX);
    m_offset = pointer - anchor * m_zoom;
    m_navigated = true;
}

void ImageView::pan(QPointF delta)
{
    m_offset += delta;
    m_navigated = true;
}

void ImageView::paintImage(QPainter &painter, QSizeF area, const LoadedImage &img) const
{
    // Partie visible de l'image, en pixels de l'image.
    const QRectF visible = QRectF(toImage(QPointF(0, 0)), toImage(QPointF(area.width(), area.height())))
                               .intersected(QRectF(QPointF(0, 0), img.size()));
    if (visible.isEmpty() || img.levels.empty())
        return;
    // Réduction la plus petite qui garde au moins un pixel d'image par pixel écran.
    size_t k = 0;
    while (k + 1 < img.levels.size() && img.levels[k + 1].width() >= img.size().width() * m_zoom)
        ++k;
    const QImage &level = img.levels[k];
    const double s = level.width() / img.size().width();
    const QRectF source(visible.topLeft() * s, visible.size() * s);
    const QRectF target(toScreen(visible.topLeft()), toScreen(visible.bottomRight()));
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, level, source);
    painter.restore();
}

void paintSegment(QPainter &painter, QPointF a, QPointF b, const QColor &color, bool handles, double scale)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0, 0, 0, 180), 4.0 * scale, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(a, b);
    painter.setPen(QPen(color, 2.0 * scale, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(a, b);
    const double radius = (handles ? 5.0 : 3.0) * scale;
    painter.setPen(QPen(Qt::black, 1.5 * scale));
    painter.setBrush(color);
    for (QPointF p : {a, b})
        painter.drawEllipse(p, radius, radius);
    painter.restore();
}

void paintBand(QPainter &painter, QPointF a, QPointF b, QPointF halfWidth, QColor color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const QPointF points[] = {a + halfWidth, b + halfWidth, b - halfWidth, a - halfWidth};
    painter.drawConvexPolygon(points, 4);
    painter.restore();
}

void paintLabel(QPainter &painter, QPointF pos, const QString &text, const QColor &color, double scale)
{
    painter.save();
    QFont font = painter.font();
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(15.0 * scale))));
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const double margin = 4.0 * scale;
    const QSizeF size(metrics.horizontalAdvance(text) + 2 * margin, metrics.height() + 2 * margin);
    const QRectF rect(QPointF(pos.x() - size.width() / 2, pos.y() - size.height()), size);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 190));
    painter.drawRoundedRect(rect, 4.0 * scale, 4.0 * scale);
    painter.setPen(color);
    painter.drawText(rect, Qt::AlignCenter, text);
    painter.restore();
}

QPointF labelAnchor(const ImageView &view, const Segment &seg, double scale)
{
    const QPointF mid = view.toScreen(seg.pointAt(0.5));
    QPointF n = seg.normal();
    if (n.y() > 0.0)
        n = -n; // toujours au-dessus du segment
    return mid + n * 14.0 * scale;
}
