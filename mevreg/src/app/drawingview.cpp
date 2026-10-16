#include "app/drawingview.h"

#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "plot/renderer.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

DrawingCanvas::DrawingCanvas(MainWindow &win, const QString &name, QWidget *parent) : QWidget(parent), m_win(win), m_name(name)
{
    setMouseTracking(true);
    theme::setMinimumSize(this, QSize(160, 120));
    connect(&win.store(), &Store::scriptApplied, this, [this] { update(); });
    connect(&win.store(), &Store::displayChanged, this, [this] { update(); });
}

void DrawingCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    // une feuille blanche, comme à l'export
    p.fillRect(rect(), Qt::white);
    const Store &store = m_win.store();
    auto it = store.drawings.find(m_name);
    if (it == store.drawings.end()) {
        m_view.reset();
        p.setPen(QColor(110, 110, 110));
        p.setFont(plotFont(13.0 * theme::zoom(), false, this));
        const int m = theme::px(20);
        p.drawText(rect().adjusted(m, m, -m, -m), Qt::AlignCenter | Qt::TextWordWrap,
                   u"Ce dessin est produit par un script Python : exécutez le script pour l'afficher."_s);
        return;
    }
    const drawing::Drawing &d = it->second;
    // agrandissement de l'interface : le dessin (règle, traits, textes) est agrandi à l'écran,
    // pas à l'export
    const double zoom = theme::zoom();
    p.save();
    p.scale(zoom, zoom);
    m_view = drawing::paint(p, d, QSizeF(size()) / zoom);
    p.restore();
    if (!m_hover || !m_view->frame().contains(*m_hover / zoom))
        return;
    // coordonnées sous le pointeur, en unités physiques
    const QPointF xy = m_view->fromPx(*m_hover / zoom);
    const NumberStyle style = store.style();
    const QString unit = prettyUnit(d.desc.unit);
    const QString text = u"x = %1 %3    y = %2 %3"_s.arg(formatSig(xy.x(), 4, style), formatSig(xy.y(), 4, style), unit).trimmed();
    // lecture des coordonnées : elle suit l'agrandissement de l'interface (pas le dessin, comme à
    // l'export)
    const double z = theme::zoom();
    const QFont f = plotFont(12.0 * z, false, this);
    p.setFont(f);
    const QFontMetricsF fm(f, this);
    const QRectF box(8.0 * z, height() - 8.0 * z - fm.height() - 6.0 * z, fm.horizontalAdvance(text) + 12.0 * z, fm.height() + 6.0 * z);
    p.setPen(QPen(QColor(160, 160, 160), 1.0));
    p.setBrush(QColor(255, 255, 255, 230));
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 3 * z, 3 * z);
    p.setPen(QColor(25, 25, 25));
    p.drawText(box, Qt::AlignCenter, text);
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = e->position();
    update();
}

void DrawingCanvas::leaveEvent(QEvent *)
{
    m_hover.reset();
    update();
}

DrawingView::DrawingView(MainWindow &win, const QString &name, QWidget *parent) : QWidget(parent), m_win(win), m_name(name)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto *bar = new QHBoxLayout;
    auto *copy = toolButton("copy", u"Copier l'image"_s, QString(), this);
    connect(copy, &QToolButton::clicked, this, [this] { m_win.copyDrawing(m_name); });
    bar->addWidget(copy);
    auto *exp = toolButton("export", u"Exporter"_s, QString(), this);
    exp->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(exp);
    menu->addAction(u"Image PNG…"_s, this, [this] { m_win.exportDrawing(m_name, exporter::Format::Png); });
    menu->addAction(u"Image vectorielle SVG…"_s, this, [this] { m_win.exportDrawing(m_name, exporter::Format::Svg); });
    menu->addAction(icons::get("pdf"), u"Document PDF…"_s, this, [this] { m_win.exportDrawing(m_name, exporter::Format::Pdf); });
    exp->setMenu(menu);
    bar->addWidget(exp);
    bar->addStretch();
    layout->addLayout(bar);
    m_canvas = new DrawingCanvas(win, name, this);
    layout->addWidget(m_canvas, 1);
}
