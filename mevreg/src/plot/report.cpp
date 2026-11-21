#include "plot/report.h"
#include "plot/export.h"

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
    return QFontMetricsF(police_trace(size, bold, reference()), const_cast<QPaintDevice *>(reference()));
}

double largeur_contenu()
{
    return PAGE_W - 2 * MARGIN;
}

} // namespace

PageWriter::PageWriter(const QString &title, const QString &infoLine) : m_y(MARGIN)
{
    if (!title.trimmed().isEmpty()) {
        texte_ligne(title.trimmed(), 18.0, true, TEXT);
        m_y += 4.0;
    }
    if (!infoLine.trimmed().isEmpty())
        texte_ligne(infoLine.trimmed(), 11.5, false, GRAY);
    if (!title.trimmed().isEmpty() || !infoLine.trimmed().isEmpty()) {
        m_y += 6.0;
        const double y = m_y;
        empile([y](QPainter &p) {
            p.setPen(QPen(RULE, 0.8));
            p.drawLine(QPointF(MARGIN, y), QPointF(PAGE_W - MARGIN, y));
        });
        m_y += 10.0;
    }
}

double PageWriter::largeur() const
{
    return largeur_contenu();
}

void PageWriter::empile(Op op)
{
    m_ops.push_back(std::move(op));
}

void PageWriter::nouvelle_page()
{
    m_pages.push_back(std::move(m_ops));
    m_ops.clear();
    m_y = MARGIN;
}

void PageWriter::assure(double h)
{
    if (m_y + h > PAGE_H - MARGIN - FOOTER && m_y > MARGIN + 1.0)
        nouvelle_page();
}

void PageWriter::texte_ligne(const QString &s, double size, bool bold, const QColor &color)
{
    const QFontMetricsF m = fm(size, bold);
    const double h = m.ascent() + m.descent() + 2.0;
    assure(h);
    const double base = m_y + m.ascent();
    empile([=](QPainter &p) {
        p.setFont(police_trace(size, bold, p.device()));
        p.setPen(color);
        p.drawText(QPointF(MARGIN, base), s);
    });
    m_y += h;
}

void PageWriter::titre_section(const QString &s)
{
    assure(60.0);
    m_y += 8.0;
    texte_ligne(s, 14.5, true, TEXT);
    m_y += 4.0;
}

void PageWriter::paragraphe(const QString &s, double size)
{
    const QFontMetricsF m = fm(size, false);
    const double maxw = largeur_contenu();
    for (const QString &raw : s.split(u'\n')) {
        QString line;
        for (const QString &word : raw.split(u' ')) {
            const QString candidate = line.isEmpty() ? word : line + u' ' + word;
            if (m.horizontalAdvance(candidate) > maxw && !line.isEmpty()) {
                texte_ligne(line, size, false, TEXT);
                line = word;
            } else {
                line = candidate;
            }
        }
        texte_ligne(line, size, false, TEXT);
    }
    m_y += 4.0;
}

void PageWriter::tableur(const QStringList &headers, const std::vector<QStringList> &columns)
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
    if (total > largeur_contenu()) {
        size = std::max(size * largeur_contenu() / total, 6.5);
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
        empile([=](QPainter &p) {
            p.fillRect(QRectF(MARGIN, y, xEnd - MARGIN, rowH), QColor(233, 237, 242));
            const QFont f = police_trace(size, true, p.device());
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
    assure(rowH * 3.0);
    headerRow();
    for (size_t r = 0; r < nrows; ++r) {
        if (m_y + rowH > PAGE_H - MARGIN - FOOTER) {
            nouvelle_page();
            headerRow();
        }
        const double y = m_y;
        QStringList cells;
        for (const auto &c : columns)
            cells << (r < static_cast<size_t>(c.size()) ? c[r] : QString());
        empile([=](QPainter &p) {
            if (r % 2 == 1)
                p.fillRect(QRectF(MARGIN, y, xEnd - MARGIN, rowH), QColor(246, 247, 249));
            const QFont f = police_trace(size, false, p.device());
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
    const QFontMetricsF mono(police_mono(size, reference()), const_cast<QPaintDevice *>(reference()));
    const double charW = std::max(mono.horizontalAdvance(u'0'), 1.0);
    const int maxChars = std::max(10, static_cast<int>((largeur_contenu() - 16.0) / charW));
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
            assure(lineH);
            const double base = m_y + mono.ascent();
            const QString &piece = pieces.at(k);
            const QString num = k == 0 ? QString::number(n + 1).rightJustified(3) : QString();
            empile([=](QPainter &p) {
                if (!num.isEmpty()) {
                    const QFont nf = police_mono(size * 0.85, p.device());
                    p.setFont(nf);
                    p.setPen(QColor(150, 150, 150));
                    p.drawText(QPointF(MARGIN - 4.0 - QFontMetricsF(nf, p.device()).horizontalAdvance(num), base), num);
                }
                p.setFont(police_mono(size, p.device()));
                p.setPen(TEXT);
                p.drawText(QPointF(MARGIN + 8.0, base), piece);
            });
            m_y += lineH;
        }
    }
    m_y += 8.0;
}

void PageWriter::graphe(const GraphView &view, const std::vector<PlotData> &data, double height, NumberStyle style)
{
    assure(height + 8.0);
    const double y = m_y;
    const double w = largeur_contenu();
    empile([=](QPainter &p) {
        p.save();
        p.translate(MARGIN, y);
        RenderOptions o;
        o.style = style;
        o.pixelSnap = false;
        rend_graphe(p, view, data, QSizeF(w, height), o);
        p.restore();
    });
    m_y += height + 10.0;
}

void PageWriter::dessin_rapport(const drawing::Drawing &d, double height)
{
    assure(height + 8.0);
    const double y = m_y;
    const double w = largeur_contenu();
    empile([=](QPainter &p) {
        p.save();
        p.translate(MARGIN, y);
        p.fillRect(QRectF(0, 0, w, height), Qt::white);
        drawing::paint(p, d, QSizeF(w, height));
        p.restore();
    });
    m_y += height + 10.0;
}

int PageWriter::nombre_pages() const
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
        if (k > 0 && !device.newPage())
            return false;
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
            const QFont f = police_trace(9.5, false, p.device());
            p.setFont(f);
            p.setPen(GRAY);
            p.drawText(QPointF(PAGE_W - MARGIN - QFontMetricsF(f, p.device()).horizontalAdvance(label), PAGE_H - MARGIN / 2.0), label);
        }
        p.restore();
    }
    return p.end();
}

bool PageWriter::ecrit_pdf(const QString &path, const QString &title, QString *error) const
{
    return exporter::ecrit_fichier(path, [&](QIODevice &file) {
        QPdfWriter pdf(&file);
        pdf.setResolution(96);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        pdf.setTitle(title);
        pdf.setCreator(u"MeVReg"_s);
        return paint(pdf);
    }, error);
}

} // namespace report
