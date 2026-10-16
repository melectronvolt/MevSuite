#include "plot/report.h"

#include <QFontMetricsF>
#include <QImage>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPdfWriter>

using namespace Qt::StringLiterals;

namespace report {

namespace {

constexpr double MARGIN = 56.0;
constexpr double FOOTER = 28.0;

const QColor TEXT(25, 25, 25);
const QColor GRAY(110, 110, 110);
const QColor RULE(210, 210, 210);

/// Appareil de référence à 96 pixels par pouce, pour mesurer le texte pendant la mise en page.
const QPaintDevice *reference()
{
    static QImage img = [] {
        QImage i(4, 4, QImage::Format_ARGB32);
        i.setDotsPerMeterX(3780);
        i.setDotsPerMeterY(3780);
        return i;
    }();
    return &img;
}

QFontMetricsF fm(double size, bool bold)
{
    return QFontMetricsF(plotFont(size, bold, reference()), const_cast<QPaintDevice *>(reference()));
}

double contentWidth()
{
    return PAGE_W - 2 * MARGIN;
}

} // namespace

PageWriter::PageWriter(const QString &title, const QString &infoLine) : m_y(MARGIN)
{
    if (!title.trimmed().isEmpty()) {
        lineText(title.trimmed(), 18.0, true, TEXT);
        m_y += 4.0;
    }
    if (!infoLine.trimmed().isEmpty())
        lineText(infoLine.trimmed(), 11.5, false, GRAY);
    if (!title.trimmed().isEmpty() || !infoLine.trimmed().isEmpty()) {
        m_y += 6.0;
        const double y = m_y;
        push([y](QPainter &p) {
            p.setPen(QPen(RULE, 0.8));
            p.drawLine(QPointF(MARGIN, y), QPointF(PAGE_W - MARGIN, y));
        });
        m_y += 10.0;
    }
}

double PageWriter::width() const
{
    return contentWidth();
}

void PageWriter::push(Op op)
{
    m_ops.push_back(std::move(op));
}

void PageWriter::newPage()
{
    m_pages.push_back(std::move(m_ops));
    m_ops.clear();
    m_y = MARGIN;
}

void PageWriter::ensure(double h)
{
    if (m_y + h > PAGE_H - MARGIN - FOOTER && m_y > MARGIN + 1.0)
        newPage();
}

void PageWriter::lineText(const QString &s, double size, bool bold, const QColor &color)
{
    const QFontMetricsF m = fm(size, bold);
    const double h = m.ascent() + m.descent() + 2.0;
    ensure(h);
    const double base = m_y + m.ascent();
    push([=](QPainter &p) {
        p.setFont(plotFont(size, bold, p.device()));
        p.setPen(color);
        p.drawText(QPointF(MARGIN, base), s);
    });
    m_y += h;
}

void PageWriter::heading(const QString &s)
{
    ensure(60.0);
    m_y += 8.0;
    lineText(s, 14.5, true, TEXT);
    m_y += 4.0;
}

void PageWriter::paragraph(const QString &s, double size)
{
    const QFontMetricsF m = fm(size, false);
    const double maxw = contentWidth();
    for (const QString &raw : s.split(u'\n')) {
        QString line;
        for (const QString &word : raw.split(u' ')) {
            const QString candidate = line.isEmpty() ? word : line + u' ' + word;
            if (m.horizontalAdvance(candidate) > maxw && !line.isEmpty()) {
                lineText(line, size, false, TEXT);
                line = word;
            } else {
                line = candidate;
            }
        }
        lineText(line, size, false, TEXT);
    }
    m_y += 4.0;
}

void PageWriter::table(const QStringList &headers, const std::vector<QStringList> &columns)
{
    if (headers.isEmpty())
        return;
    const double pad = 6.0;
    double size = 10.5;
    auto widths = [&](double sz) {
        const QFontMetricsF mb = fm(sz, true), mr = fm(sz, false);
        std::vector<double> w;
        for (qsizetype k = 0; k < headers.size(); ++k) {
            double c = mb.horizontalAdvance(headers[k]);
            if (static_cast<size_t>(k) < columns.size()) {
                for (const QString &cell : columns[k])
                    c = std::max(c, mr.horizontalAdvance(cell));
            }
            w.push_back(c + 2 * pad);
        }
        return w;
    };
    std::vector<double> w = widths(size);
    double total = 0;
    for (double v : w)
        total += v;
    if (total > contentWidth()) {
        size = std::max(size * contentWidth() / total, 6.5);
        w = widths(size);
        total = 0;
        for (double v : w)
            total += v;
    }
    const double rowH = size * 1.75;
    const double cap = fm(size, false).capHeight();
    size_t nrows = 0;
    for (const auto &c : columns)
        nrows = std::max(nrows, static_cast<size_t>(c.size()));
    const double xEnd = MARGIN + total;
    auto headerRow = [&] {
        const double y = m_y;
        push([=](QPainter &p) {
            p.fillRect(QRectF(MARGIN, y, xEnd - MARGIN, rowH), QColor(233, 237, 242));
            const QFont f = plotFont(size, true, p.device());
            const QFontMetricsF m(f, p.device());
            p.setFont(f);
            p.setPen(TEXT);
            double x = MARGIN;
            for (qsizetype k = 0; k < headers.size(); ++k) {
                const double tw = m.horizontalAdvance(headers[k]);
                p.drawText(QPointF(x + w[k] - pad - tw, y + rowH / 2.0 + cap / 2.0), headers[k]);
                x += w[k];
            }
            p.setPen(QPen(GRAY, 1.0));
            p.drawLine(QPointF(MARGIN, y + rowH), QPointF(xEnd, y + rowH));
        });
        m_y += rowH;
    };
    ensure(rowH * 3.0);
    headerRow();
    for (size_t r = 0; r < nrows; ++r) {
        if (m_y + rowH > PAGE_H - MARGIN - FOOTER) {
            newPage();
            headerRow();
        }
        const double y = m_y;
        QStringList cells;
        for (const auto &c : columns)
            cells << (r < static_cast<size_t>(c.size()) ? c[r] : QString());
        push([=](QPainter &p) {
            if (r % 2 == 1)
                p.fillRect(QRectF(MARGIN, y, xEnd - MARGIN, rowH), QColor(246, 247, 249));
            const QFont f = plotFont(size, false, p.device());
            const QFontMetricsF m(f, p.device());
            p.setFont(f);
            p.setPen(TEXT);
            double x = MARGIN;
            for (qsizetype k = 0; k < cells.size() && static_cast<size_t>(k) < w.size(); ++k) {
                const double tw = m.horizontalAdvance(cells[k]);
                p.drawText(QPointF(x + w[k] - pad - tw, y + rowH / 2.0 + cap / 2.0), cells[k]);
                x += w[k];
            }
        });
        m_y += rowH;
    }
    m_y += 10.0;
}

void PageWriter::code(const QString &src)
{
    const double size = 9.5;
    const double lineH = size * 1.45;
    const QFontMetricsF mono(monoFont(size, reference()), const_cast<QPaintDevice *>(reference()));
    const double charW = std::max(mono.horizontalAdvance(u'0'), 1.0);
    const int maxChars = std::max(10, static_cast<int>((contentWidth() - 16.0) / charW));
    const QStringList lines = src.split(u'\n');
    for (qsizetype n = 0; n < lines.size(); ++n) {
        if (n == lines.size() - 1 && lines[n].isEmpty())
            break;
        QString line = lines[n];
        line.replace(u'\t', u"    "_s);
        QStringList pieces;
        if (line.isEmpty())
            pieces << QString();
        for (qsizetype k = 0; k < line.size(); k += maxChars)
            pieces << line.mid(k, maxChars);
        for (qsizetype k = 0; k < pieces.size(); ++k) {
            ensure(lineH);
            const double base = m_y + mono.ascent();
            const QString piece = pieces[k];
            const QString num = k == 0 ? QString::number(n + 1).rightJustified(3) : QString();
            push([=](QPainter &p) {
                if (!num.isEmpty()) {
                    const QFont nf = monoFont(size * 0.85, p.device());
                    p.setFont(nf);
                    p.setPen(QColor(150, 150, 150));
                    p.drawText(QPointF(MARGIN - 4.0 - QFontMetricsF(nf, p.device()).horizontalAdvance(num), base), num);
                }
                p.setFont(monoFont(size, p.device()));
                p.setPen(TEXT);
                p.drawText(QPointF(MARGIN + 8.0, base), piece);
            });
            m_y += lineH;
        }
    }
    m_y += 8.0;
}

void PageWriter::graph(const GraphView &view, const std::vector<PlotData> &data, double height, NumberStyle style)
{
    ensure(height + 8.0);
    const double y = m_y;
    const double w = contentWidth();
    push([=](QPainter &p) {
        p.save();
        p.translate(MARGIN, y);
        RenderOptions o;
        o.style = style;
        o.pixelSnap = false;
        renderGraph(p, view, data, QSizeF(w, height), o);
        p.restore();
    });
    m_y += height + 10.0;
}

void PageWriter::drawing(const drawing::Drawing &d, double height)
{
    ensure(height + 8.0);
    const double y = m_y;
    const double w = contentWidth();
    push([=](QPainter &p) {
        p.save();
        p.translate(MARGIN, y);
        p.fillRect(QRectF(0, 0, w, height), Qt::white);
        drawing::paint(p, d, QSizeF(w, height));
        p.restore();
    });
    m_y += height + 10.0;
}

int PageWriter::pageCount() const
{
    return static_cast<int>(m_pages.size()) + ((!m_ops.empty() || m_pages.empty()) ? 1 : 0);
}

bool PageWriter::paint(QPagedPaintDevice &device) const
{
    std::vector<std::vector<Op>> pages = m_pages;
    if (!m_ops.empty() || pages.empty())
        pages.push_back(m_ops);
    QPainter p;
    if (!p.begin(&device))
        return false;
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const double scale = std::min(device.width() / PAGE_W, device.height() / PAGE_H);
    const int n = static_cast<int>(pages.size());
    for (int k = 0; k < n; ++k) {
        if (k > 0)
            device.newPage();
        p.save();
        p.scale(scale, scale);
        p.fillRect(QRectF(0, 0, PAGE_W, PAGE_H), Qt::white);
        for (const Op &op : pages[k]) {
            p.save();
            op(p);
            p.restore();
        }
        if (n > 1) {
            const QString label = u"%1 / %2"_s.arg(k + 1).arg(n);
            const QFont f = plotFont(9.5, false, p.device());
            p.setFont(f);
            p.setPen(GRAY);
            p.drawText(QPointF(PAGE_W - MARGIN - QFontMetricsF(f, p.device()).horizontalAdvance(label), PAGE_H - MARGIN / 2.0), label);
        }
        p.restore();
    }
    p.end();
    return true;
}

bool PageWriter::writePdf(const QString &path, const QString &title, QString *error) const
{
    QPdfWriter pdf(path);
    pdf.setResolution(96);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    pdf.setTitle(title);
    pdf.setCreator(u"MeVReg"_s);
    if (!paint(pdf)) {
        if (error)
            *error = u"écriture impossible"_s;
        return false;
    }
    return true;
}

} // namespace report
