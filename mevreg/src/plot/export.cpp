#include "plot/export.h"

#include "core/workspace.h"

#include <QDate>
#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>
#include <QSvgGenerator>

using namespace Qt::StringLiterals;

namespace exporter {

Format formatOf(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == u"svg")
        return Format::Svg;
    if (ext == u"pdf")
        return Format::Pdf;
    return Format::Png;
}

QSizeF defaultSize(int rows, int cols)
{
    return {760.0 * std::max(cols, 1) * 0.85 + 120.0, 300.0 * std::max(rows, 1) + 200.0};
}

std::vector<PlotData> resolveAll(const GraphView &view, const Document &doc, NumberStyle style)
{
    std::vector<PlotData> out;
    for (int i = 0; i < view.visiblePlots(); ++i)
        out.push_back(resolve(view.plots[i], doc, style));
    return out;
}

QImage graphImage(const GraphView &view, const Document &doc, QSizeF size, double scale, NumberStyle style)
{
    return plotsImage(view, resolveAll(view, doc, style), size, scale, style);
}

QImage plotsImage(const GraphView &view, const std::vector<PlotData> &data, QSizeF size, double scale, NumberStyle style)
{
    QImage img(QSize(static_cast<int>(std::ceil(size.width() * scale)), static_cast<int>(std::ceil(size.height() * scale))), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(scale, scale);
    RenderOptions o;
    o.style = style;
    o.devicePixelRatio = scale;
    renderGraph(p, view, data, size, o);
    p.end();
    return img;
}

std::pair<QString, QString> headerLines(const Meta &m)
{
    QStringList l;
    if (!m.authors.trimmed().isEmpty())
        l << m.authors.trimmed();
    if (!m.klass.trimmed().isEmpty())
        l << u"Classe : "_s + m.klass.trimmed();
    l << QDate::currentDate().toString(u"dd/MM/yyyy"_s);
    return {m.title.trimmed(), l.join(u"  —  "_s)};
}

bool writeGraph(const QString &path, Format format, const GraphView &view, const Workspace &ws, QSizeF size, NumberStyle style, QString *error)
{
    return writePlots(path, format, view, resolveAll(view, ws.doc, style), ws.meta, size, style, error);
}

bool writePlots(const QString &path, Format format, const GraphView &view, const std::vector<PlotData> &data, const Meta &meta, QSizeF size,
                NumberStyle style, QString *error)
{
    RenderOptions o;
    o.style = style;
    switch (format) {
    case Format::Png: {
        const QImage img = plotsImage(view, data, size, 3.0, style);
        if (!img.save(path, "PNG")) {
            if (error)
                *error = u"écriture impossible"_s;
            return false;
        }
        return true;
    }
    case Format::Svg: {
        QSvgGenerator gen;
        gen.setFileName(path);
        gen.setResolution(96);
        gen.setSize(size.toSize());
        gen.setViewBox(QRectF(QPointF(0, 0), size));
        gen.setTitle(view.name);
        gen.setDescription(u"Graphe produit par MeVReg"_s);
        QPainter p;
        if (!p.begin(&gen)) {
            if (error)
                *error = u"écriture impossible"_s;
            return false;
        }
        // police des graphes dès le départ : le fichier ne dépend pas de celle de l'interface
        // (agrandissement), pas même ses attributs par défaut
        p.setFont(plotFont(12.0, false, p.device()));
        o.pixelSnap = false;
        renderGraph(p, view, data, size, o);
        p.end();
        return true;
    }
    case Format::Pdf: {
        const auto [title, line] = headerLines(meta);
        const bool header = !title.isEmpty() || !meta.authors.trimmed().isEmpty() || !meta.klass.trimmed().isEmpty();
        double top = 0.0;
        const double margin = 24.0;
        if (header)
            top = margin + 18.0 * 1.25 + 12.0 * 1.4 + 16.0;
        QPdfWriter pdf(path);
        pdf.setResolution(96);
        pdf.setTitle(view.name);
        pdf.setCreator(u"MeVReg"_s);
        const QSizeF pageSize(size.width(), size.height() + top);
        pdf.setPageSize(QPageSize(pageSize * 72.0 / 96.0, QPageSize::Point, QString(), QPageSize::ExactMatch));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter p;
        if (!p.begin(&pdf)) {
            if (error)
                *error = u"écriture impossible"_s;
            return false;
        }
        p.setFont(plotFont(12.0, false, p.device()));
        p.fillRect(QRectF(QPointF(0, 0), pageSize), Qt::white);
        if (header) {
            double y = margin;
            const QFont tf = plotFont(18.0, true, p.device());
            const QFont lf = plotFont(12.0, false, p.device());
            if (!title.isEmpty()) {
                y += QFontMetricsF(tf, p.device()).ascent();
                p.setFont(tf);
                p.setPen(QColor(25, 25, 25));
                p.drawText(QPointF(margin, y), title);
                y += QFontMetricsF(tf, p.device()).descent() + 6.0;
            }
            y += QFontMetricsF(lf, p.device()).ascent();
            p.setFont(lf);
            p.setPen(QColor(110, 110, 110));
            p.drawText(QPointF(margin, y), line);
            p.setPen(QPen(QColor(210, 210, 210), 0.8));
            p.drawLine(QPointF(margin, top - 8.0), QPointF(pageSize.width() - margin, top - 8.0));
        }
        p.translate(0, top);
        o.pixelSnap = false;
        renderGraph(p, view, data, size, o);
        p.end();
        return true;
    }
    }
    return false;
}

} // namespace exporter
