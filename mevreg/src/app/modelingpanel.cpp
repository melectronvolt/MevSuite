#include "app/modelingpanel.h"

#include "app/icons.h"
#include "app/graphwidget.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/numeric.h"
#include "plot/renderer.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

ModelingPanel *g_instance = nullptr;

/// Allure de chaque modèle, pour sa vignette (x et y entre 0 et 1).
double thumbValue(fit::ModelKind::Type t, double x)
{
    using K = fit::ModelKind;
    const double tau = 2.0 * M_PI;
    switch (t) {
    case K::Constant: return 0.5;
    case K::Linear: return 0.9 * x;
    case K::Affine: return 0.7 * x + 0.2;
    case K::Polynomial: return 0.5 + 1.6 * (x - 0.5) * (x - 0.2) * (x - 0.85) * 4.0;
    case K::Parabola: return 4.0 * (x - 0.5) * (x - 0.5);
    case K::Power: return std::pow(x, 2.5);
    case K::ExpX: return std::exp(3.0 * x) / 20.0;
    case K::ExpInvX: return std::exp(-0.4 / std::max(x, 0.05));
    case K::Charge: return 1.0 - std::exp(-x / 0.2);
    case K::Discharge: return std::exp(-x / 0.2);
    case K::Ln:
    case K::Log10: return std::log(std::max(x, 0.02)) / 4.0 + 1.0;
    case K::Sine: return 0.5 + 0.4 * std::sin(tau * x / 0.45);
    case K::Cosine: return 0.5 + 0.4 * std::cos(tau * x / 0.45);
    case K::DampedSine: return 0.5 + 0.45 * std::exp(-x / 0.35) * std::sin(tau * x / 0.2);
    case K::DampedCosine: return 0.5 + 0.45 * std::exp(-x / 0.35) * std::cos(tau * x / 0.2);
    case K::User: return 0.5 + 0.3 * std::sin(6.0 * x) * x;
    }
    return 0.5;
}

QString shortLabel(const fit::ModelKind &k)
{
    using K = fit::ModelKind;
    switch (k.type) {
    case K::Charge: return u"Charge"_s;
    case K::Discharge: return u"Décharge"_s;
    case K::ExpX: return u"Exp. en x"_s;
    case K::ExpInvX: return u"Exp. en 1/x"_s;
    case K::User: return u"Utilisateur"_s;
    default: return k.label();
    }
}

/// Grille de vignettes des modèles : clic pour choisir.
class ModelThumbs : public QWidget {
public:
    /// Vignette et espacement, à 100 % : ils suivent l'agrandissement de l'interface.
    static int W() { return theme::px(96); }
    static int H() { return theme::px(72); }
    static int GAP() { return theme::px(6); }

    ModelThumbs(std::optional<fit::ModelKind::Type> selected, std::function<void(const fit::ModelKind &)> onPick, QWidget *parent)
        : QWidget(parent), m_selected(selected), m_onPick(std::move(onPick)), m_catalog(fit::ModelKind::catalog())
    {
        setMouseTracking(true);
        QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override
    {
        const int rows = (static_cast<int>(m_catalog.size()) + perRow(w) - 1) / perRow(w);
        return rows * (H() + GAP()) - GAP() + 2;
    }
    QSize sizeHint() const override { return {4 * (W() + GAP()), heightForWidth(4 * (W() + GAP()))}; }
    QSize minimumSizeHint() const override { return {2 * (W() + GAP()), heightForWidth(2 * (W() + GAP()))}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        const QColor accent = theme::accent();
        const double z = theme::zoom();
        for (size_t i = 0; i < m_catalog.size(); ++i) {
            const fit::ModelKind &k = m_catalog[i];
            const QRectF r = cell(static_cast<int>(i)).adjusted(0.5, 0.5, -0.5, -0.5);
            const bool sel = m_selected && *m_selected == k.type;
            const bool hover = m_hover == static_cast<int>(i);
            QColor bg = pal.color(QPalette::Base);
            if (sel) {
                bg = accent;
                bg.setAlphaF(theme::isDark() ? 0.32 : 0.16);
            } else if (hover) {
                bg = pal.color(QPalette::Highlight);
                bg.setAlphaF(0.10);
            }
            QColor border = sel ? accent : pal.color(QPalette::Mid);
            p.setPen(QPen(border, (sel ? 1.6 : 1.0) * z));
            p.setBrush(bg);
            p.drawRoundedRect(r, 5 * z, 5 * z);
            // axes et courbe
            const QRectF plot(r.left() + 9 * z, r.top() + 7 * z, r.width() - 18 * z, 38 * z);
            QColor axis = pal.color(QPalette::WindowText);
            axis.setAlphaF(0.45);
            p.setPen(QPen(axis, 0.9 * z));
            p.drawLine(plot.bottomLeft(), plot.bottomRight());
            p.drawLine(plot.bottomLeft(), plot.topLeft());
            QPainterPath path;
            for (int j = 0; j <= 60; ++j) {
                const double x = j / 60.0;
                const double y = std::clamp(thumbValue(k.type, x), -0.05, 1.05);
                const QPointF pt(plot.left() + x * plot.width(), plot.bottom() - y * plot.height());
                if (j == 0)
                    path.moveTo(pt);
                else
                    path.lineTo(pt);
            }
            p.setPen(QPen(accent, 1.8 * z, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() * 0.85);
            f.setWeight(sel ? QFont::DemiBold : QFont::Normal);
            p.setFont(f);
            p.setPen(pal.color(QPalette::WindowText));
            p.drawText(QRectF(r.left() + 2 * z, r.bottom() - 22 * z, r.width() - 4 * z, 20 * z), Qt::AlignCenter, shortLabel(k));
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = at(e->position().toPoint());
        if (h != m_hover) {
            m_hover = h;
            setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const int i = at(e->position().toPoint());
        if (i >= 0 && e->button() == Qt::LeftButton)
            m_onPick(m_catalog[i]);
    }
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(e);
            const int i = at(he->pos());
            if (i >= 0)
                QToolTip::showText(he->globalPos(), u"<b>%1</b><br>%2"_s.arg(m_catalog[i].label(), m_catalog[i].templ().toHtmlEscaped()), this, cell(i));
            else
                QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

private:
    int perRow(int w) const { return std::max(2, (w + GAP()) / (W() + GAP())); }
    QRect cell(int i) const
    {
        const int n = perRow(width());
        return {(i % n) * (W() + GAP()), (i / n) * (H() + GAP()), W(), H()};
    }
    int at(QPoint p) const
    {
        for (size_t i = 0; i < m_catalog.size(); ++i)
            if (cell(static_cast<int>(i)).contains(p))
                return static_cast<int>(i);
        return -1;
    }

    std::optional<fit::ModelKind::Type> m_selected;
    std::function<void(const fit::ModelKind &)> m_onPick;
    std::vector<fit::ModelKind> m_catalog;
    int m_hover = -1;
};

QLabel *strong(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    return l;
}

QLabel *mono(const QString &text, QWidget *parent, bool bold = false)
{
    auto *l = new QLabel(text, parent);
    QFont f = theme::fixedFont();
    f.setBold(bold);
    l->setFont(f);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QLabel *colored(const QString &text, const QColor &c, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, c);
    l->setPalette(p);
    return l;
}

QWidget *hbox(QWidget *parent, std::initializer_list<QWidget *> widgets)
{
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    for (QWidget *x : widgets)
        l->addWidget(x);
    l->addStretch();
    return w;
}

QString paramText(const fit::FitResult &r, size_t i, NumberStyle style)
{
    const double u = i < r.uncertainties.size() ? r.uncertainties[i] : NAN;
    if (std::isfinite(u) && u > 0.0)
        return roundMeasure(r.params[i], u, 2, style).toText(QString());
    return formatSig(r.params[i], 6, style);
}

} // namespace

ModelingPanel::ModelingPanel(MainWindow &win, QWidget *parent) : QWidget(parent), m_win(win)
{
    g_instance = this;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_scroll = new PanelScroll(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->viewport()->setAutoFillBackground(false);
    layout->addWidget(m_scroll);
    m_rebuild.setSingleShot(true);
    connect(&m_rebuild, &QTimer::timeout, this, &ModelingPanel::tryRebuild);
    m_exprTimer.setSingleShot(true);
    m_exprTimer.setInterval(450);
    connect(&m_exprTimer, &QTimer::timeout, this, &ModelingPanel::applyUserExpr);
    connect(&win.store(), &Store::workspaceChanged, this, &ModelingPanel::scheduleRebuild);
    connect(&win.store(), &Store::displayChanged, this, &ModelingPanel::scheduleRebuild);
    rebuild();
}

ModelingPanel::~ModelingPanel()
{
    if (g_instance == this)
        g_instance = nullptr;
}

ModelingPanel *ModelingPanel::instance()
{
    return g_instance;
}

void ModelingPanel::target(ViewId graph, int plot, std::optional<int> fit)
{
    const GraphView *g = m_win.store().ws.graph(graph);
    if (!g)
        return;
    m_graph = graph;
    m_plot = std::clamp(plot, 0, std::max(g->visiblePlots() - 1, 0));
    m_curve = 0;
    m_fit.reset();
    m_new = fit && *fit < 0;
    m_gridOpen = m_new;
    if (fit && *fit >= 0 && static_cast<size_t>(m_plot) < g->plots.size()) {
        const Plot &p = g->plots[m_plot];
        if (static_cast<size_t>(*fit) < p.fits.size()) {
            const FitSpec &f = p.fits[*fit];
            for (size_t k = 0; k < p.curves.size(); ++k) {
                if (p.curves[k].x == f.x && p.curves[k].y == f.y) {
                    m_curve = static_cast<int>(k);
                    break;
                }
            }
            m_fit = *fit;
        }
    }
    rebuild();
}

bool ModelingPanel::applyRange(ViewId graph, int plot, double a, double b)
{
    if (!isVisible() || m_graph != graph || m_plot != plot || !m_fit)
        return false;
    editFit([&](FitSpec &f) { f.range = std::pair(a, b); });
    return true;
}

std::optional<int> ModelingPanel::shownFit(ViewId graph, int plot) const
{
    if (!isVisible() || m_graph != graph || m_plot != plot)
        return std::nullopt;
    return m_fit;
}

void ModelingPanel::scheduleRebuild()
{
    m_rebuild.start(0);
}

void ModelingPanel::tryRebuild()
{
    if (live::busy(m_page)) {
        m_rebuild.start(250);
        return;
    }
    rebuild();
}

void ModelingPanel::applyUserExpr()
{
    m_exprTimer.stop();
    const QString e = m_userExpr;
    editFit([&](FitSpec &f) {
        if (f.kind.type == fit::ModelKind::User && f.kind.expression != e) {
            f.kind.expression = e;
            f.initial.clear();
        }
    });
}

void ModelingPanel::editFit(const std::function<void(FitSpec &)> &f)
{
    if (!m_graph || !m_fit)
        return;
    const ViewId gid = *m_graph;
    const int pi = m_plot, fi = *m_fit;
    m_win.store().edit([&](Workspace &ws) {
        GraphView *g = ws.graphMut(gid);
        if (!g || static_cast<size_t>(pi) >= g->plots.size() || static_cast<size_t>(fi) >= g->plots[pi].fits.size())
            return;
        f(g->plots[pi].fits[fi]);
    });
}

bool ModelingPanel::validate()
{
    const Workspace &ws = m_win.store().ws;
    if (ws.graphs.empty())
        return false;
    if (!m_graph || !ws.graph(*m_graph)) {
        m_graph = ws.graphs.front().id;
        m_plot = 0;
        m_curve = 0;
        m_fit.reset();
        m_new = false;
    }
    const GraphView *g = ws.graph(*m_graph);
    if (m_plot >= g->visiblePlots() || static_cast<size_t>(m_plot) >= g->plots.size())
        m_plot = 0;
    if (g->plots.empty())
        return false;
    const Plot &plot = g->plots[m_plot];
    if (plot.curves.empty())
        return true;
    if (static_cast<size_t>(m_curve) >= plot.curves.size())
        m_curve = 0;
    const CurveSpec &c = plot.curves[m_curve];
    auto belongs = [&](int k) { return k >= 0 && static_cast<size_t>(k) < plot.fits.size() && plot.fits[k].x == c.x && plot.fits[k].y == c.y; };
    if (m_fit && !belongs(*m_fit))
        m_fit.reset();
    if (!m_fit && !m_new) {
        for (int k = static_cast<int>(plot.fits.size()) - 1; k >= 0; --k) {
            if (belongs(k)) {
                m_fit = k;
                break;
            }
        }
        if (!m_fit)
            m_gridOpen = true;
    }
    return true;
}

void ModelingPanel::rebuild()
{
    // barres de plage du graphe affiché : redessinées
    if (m_graph)
        if (GraphPanel *gp = m_win.graphPanel(*m_graph))
            gp->canvas()->update();
    m_rebuild.stop();
    const int scrollPos = m_scroll->verticalScrollBar()->value();
    const live::Focus focus = live::remember(m_page);
    auto *page = new QWidget;
    m_page = page;
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(10, 8, 10, 10);
    l->setSpacing(6);
    if (!validate())
        l->addWidget(weakLabel(u"Créez d'abord un graphe contenant la courbe à modéliser."_s, page));
    else
        build(l);
    l->addStretch();
    m_scroll->setWidget(page);
    // fond transparent : celui du panneau, peint par sa fenêtre ancrée
    page->setAutoFillBackground(false);
    live::track(page);
    keepScroll(m_scroll, scrollPos);
    live::restore(page, focus);
}

void ModelingPanel::choose(const fit::ModelKind &picked)
{
    Store &store = m_win.store();
    const GraphView *g = store.ws.graph(*m_graph);
    const Plot &plot = g->plots[m_plot];
    const CurveSpec curve = plot.curves[m_curve];
    fit::ModelKind k = picked;
    const QString xname = store.ws.doc.name(curve.x);
    if (k.type == fit::ModelKind::Polynomial)
        k.degree = std::clamp(m_degree, 1, 10);
    if (k.type == fit::ModelKind::User) {
        if (m_userExpr.trimmed().isEmpty())
            m_userExpr = u"a*%1 + b"_s.arg(xname);
        k.expression = m_userExpr;
    }
    m_gridOpen = false;
    if (m_fit) {
        editFit([&](FitSpec &f) {
            f.fixed = k.defaultFixed();
            f.initial.clear();
            f.kind = k;
        });
        return;
    }
    std::optional<std::pair<double, double>> range;
    if (auto it = store.selectedRange.find(*m_graph); it != store.selectedRange.end() && std::get<0>(it->second) == m_plot)
        range = std::pair(std::get<1>(it->second), std::get<2>(it->second));
    const ViewId gid = *m_graph;
    const int pi = m_plot;
    int index = -1;
    m_new = false;
    store.edit([&](Workspace &ws) {
        Plot &p = ws.graphMut(gid)->plots[pi];
        FitSpec spec = FitSpec::make(k, curve.x, curve.y, p.nextColor());
        spec.side = curve.side;
        spec.range = range;
        p.fits.push_back(spec);
        index = static_cast<int>(p.fits.size()) - 1;
        m_fit = index;
    });
}

void ModelingPanel::build(QVBoxLayout *l)
{
    Store &store = m_win.store();
    const Workspace &ws = store.ws;
    const NumberStyle style = store.style();
    const GraphView *g = ws.graph(*m_graph);
    const PlotTheme th = theme::isDark() && store.prefs.darkPlots ? PlotTheme::darkTheme() : PlotTheme::light();

    // --- graphe, courbe ---
    auto *top = new QWidget(m_page);
    auto *tl = new QGridLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setHorizontalSpacing(8);
    auto *graphs = new QComboBox(top);
    for (const auto &gv : ws.graphs)
        graphs->addItem(gv.name, QVariant::fromValue<qulonglong>(gv.id));
    graphs->setCurrentIndex(graphs->findData(QVariant::fromValue<qulonglong>(*m_graph)));
    connect(graphs, &QComboBox::activated, this, [this, graphs](int i) {
        m_graph = graphs->itemData(i).toULongLong();
        m_plot = 0;
        m_curve = 0;
        m_fit.reset();
        m_new = false;
        scheduleRebuild();
    });
    tl->addWidget(new QLabel(u"Graphe"_s, top), 0, 0);
    auto *gr = new QHBoxLayout;
    gr->addWidget(graphs, 1);
    if (g->visiblePlots() > 1) {
        auto *plots = new QComboBox(top);
        for (int k = 0; k < g->visiblePlots(); ++k)
            plots->addItem(u"n° %1"_s.arg(k + 1));
        plots->setCurrentIndex(m_plot);
        connect(plots, &QComboBox::activated, this, [this](int i) {
            m_plot = i;
            m_curve = 0;
            m_fit.reset();
            m_new = false;
            scheduleRebuild();
        });
        gr->addWidget(plots);
    }
    tl->addLayout(gr, 0, 1);
    const Plot &plot = g->plots[m_plot];
    if (plot.curves.empty()) {
        l->addWidget(top);
        l->addWidget(weakLabel(u"Ce graphe ne contient aucune courbe : glissez-y une grandeur depuis un tableur ou la fenêtre Grandeurs."_s, m_page));
        return;
    }
    const CurveSpec curve = plot.curves[m_curve];
    auto *curves = new QComboBox(top);
    for (const auto &c : plot.curves)
        curves->addItem(u"%1(%2)"_s.arg(ws.doc.name(c.y), ws.doc.name(c.x)));
    curves->setCurrentIndex(m_curve);
    connect(curves, &QComboBox::activated, this, [this](int i) {
        m_curve = i;
        m_fit.reset();
        m_new = false;
        scheduleRebuild();
    });
    tl->addWidget(new QLabel(u"Courbe"_s, top), 1, 0);
    tl->addWidget(curves, 1, 1);
    tl->setColumnStretch(1, 1);
    l->addWidget(top);

    // --- modèles existants de la courbe ---
    auto *fitsBar = new QWidget(m_page);
    auto *flow = new FlowLayout(fitsBar);
    for (size_t k = 0; k < plot.fits.size(); ++k) {
        const FitSpec &f = plot.fits[k];
        if (f.x != curve.x || f.y != curve.y)
            continue;
        auto *b = new QToolButton(fitsBar);
        b->setText(u"●  "_s + f.kind.label());
        b->setCheckable(true);
        b->setChecked(m_fit == static_cast<int>(k));
        b->setAutoRaise(true);
        QPalette p = b->palette();
        p.setColor(QPalette::ButtonText, th.adapt(f.color));
        b->setPalette(p);
        connect(b, &QToolButton::clicked, this, [this, k] {
            m_fit = static_cast<int>(k);
            m_new = false;
            m_gridOpen = false;
            scheduleRebuild();
        });
        flow->addWidget(b);
    }
    auto *nb = new QToolButton(fitsBar);
    nb->setIcon(icons::get("plus"));
    nb->setText(u"Nouveau modèle"_s);
    nb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    nb->setCheckable(true);
    nb->setChecked(!m_fit);
    nb->setAutoRaise(true);
    connect(nb, &QToolButton::clicked, this, [this] {
        m_fit.reset();
        m_new = true;
        m_gridOpen = true;
        scheduleRebuild();
    });
    flow->addWidget(nb);
    l->addWidget(fitsBar);
    auto *sep = new QFrame(m_page);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    l->addWidget(sep);

    // --- grille de vignettes ---
    const FitSpec *spec = m_fit ? &plot.fits[*m_fit] : nullptr;
    std::optional<fit::ModelKind::Type> current;
    if (spec)
        current = spec->kind.type;
    auto *gridHead = new QToolButton(m_page);
    gridHead->setText(spec ? u"Modèle : %1"_s.arg(spec->kind.label()) : u"Modèle"_s);
    gridHead->setArrowType(m_gridOpen ? Qt::DownArrow : Qt::RightArrow);
    gridHead->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    gridHead->setAutoRaise(true);
    QFont hf = gridHead->font();
    hf.setWeight(QFont::DemiBold);
    gridHead->setFont(hf);
    gridHead->setEnabled(spec != nullptr);
    connect(gridHead, &QToolButton::clicked, this, [this] {
        m_gridOpen = !m_gridOpen;
        scheduleRebuild();
    });
    l->addWidget(gridHead);
    if (m_gridOpen || !spec) {
        auto *thumbs = new ModelThumbs(current, [this](const fit::ModelKind &k) { QTimer::singleShot(0, this, [this, k] { choose(k); }); }, m_page);
        l->addWidget(thumbs);
    }
    if (!spec) {
        l->addSpacing(4);
        l->addWidget(weakLabel(u"Cliquez sur une vignette pour ajuster ce modèle à la courbe %1(%2)."_s.arg(ws.doc.name(curve.y), ws.doc.name(curve.x)), m_page));
        return;
    }
    const QString xname = ws.doc.name(curve.x), yname = ws.doc.name(curve.y);
    const Quantity *yq = ws.doc.get(curve.y);
    const QString yunit = yq ? prettyUnit(yq->unit) : QString();

    // --- options propres au modèle ---
    if (spec->kind.type == fit::ModelKind::Polynomial) {
        auto *deg = new QSpinBox(m_page);
        deg->setRange(1, 10);
        deg->setValue(spec->kind.degree);
        deg->setKeyboardTracking(false);
        connect(deg, &QSpinBox::valueChanged, this, [this](int d) {
            m_degree = d;
            editFit([&](FitSpec &f) {
                f.kind.degree = d;
                f.initial.clear();
            });
        });
        l->addWidget(hbox(m_page, {new QLabel(u"Degré"_s, m_page), deg}));
        l->addWidget(mono(spec->kind.templ(), m_page));
    } else if (spec->kind.type == fit::ModelKind::User) {
        auto *expr = new QLineEdit(spec->kind.expression, m_page);
        expr->setFont(theme::fixedFont());
        expr->setPlaceholderText(u"a*exp(-%1/tau) + c"_s.arg(xname));
        theme::setMinimumWidth(expr, 220);
        m_userExpr = spec->kind.expression;
        connect(expr, &QLineEdit::textEdited, this, [this, expr](const QString &t) {
            m_userExpr = t;
            m_exprTimer.start();
            // l'ajustement suit la saisie : la page peut se reconstruire sans attendre la validation
            expr->setModified(false);
        });
        connect(expr, &QLineEdit::editingFinished, this, &ModelingPanel::applyUserExpr);
        auto *row = new QWidget(m_page);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->addWidget(mono(yname + u" ="_s, row));
        rl->addWidget(expr, 1);
        l->addWidget(row);
        l->addWidget(weakLabel(u"Variable : %1. Les autres noms (hors paramètres du document) sont ajustés."_s.arg(xname), m_page));
    } else {
        l->addWidget(mono(spec->kind.templ(), m_page));
    }

    // --- plage ---
    {
        auto *row = new QWidget(m_page);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->addWidget(new QLabel(u"Points :"_s, row));
        auto *all = new QRadioButton(u"tous"_s, row);
        auto *range = new QRadioButton(u"plage"_s, row);
        all->setChecked(!spec->range);
        range->setChecked(spec->range.has_value());
        auto *group = new QButtonGroup(row);
        group->addButton(all);
        group->addButton(range);
        connect(all, &QRadioButton::clicked, this, [this] { editFit([](FitSpec &f) { f.range.reset(); }); });
        const QId xid = curve.x;
        connect(range, &QRadioButton::clicked, this, [this, xid] {
            const auto r = numeric::finiteRange(m_win.store().ws.doc.values(xid));
            editFit([&](FitSpec &f) {
                if (!f.range)
                    f.range = r;
            });
        });
        rl->addWidget(all);
        rl->addWidget(range);
        if (spec->range) {
            auto *a = new NumberEdit(row);
            auto *b = new NumberEdit(row);
            a->setStyle(style);
            b->setStyle(style);
            a->setValue(spec->range->first);
            b->setValue(spec->range->second);
            connect(a, &NumberEdit::valueEdited, this, [this](double v) { editFit([&](FitSpec &f) { if (f.range) f.range->first = v; }); });
            connect(b, &NumberEdit::valueEdited, this, [this](double v) { editFit([&](FitSpec &f) { if (f.range) f.range->second = v; }); });
            rl->addWidget(a);
            rl->addWidget(new QLabel(u"≤ %1 ≤"_s.arg(xname), row));
            rl->addWidget(b);
        }
        rl->addStretch();
        l->addWidget(row);
        if (auto it = store.selectedRange.find(*m_graph); it != store.selectedRange.end() && std::get<0>(it->second) == m_plot) {
            const auto [pi, a, b] = it->second;
            auto *use = new QPushButton(icons::get("range"), u"Sélection du graphe (%1 à %2)"_s.arg(formatSig(a, 4, style), formatSig(b, 4, style)), m_page);
            use->setToolTip(u"Plage tracée avec l'outil Plage du graphe"_s);
            connect(use, &QPushButton::clicked, this, [this, a, b] { editFit([&](FitSpec &f) { f.range = std::pair(a, b); }); });
            l->addWidget(use, 0, Qt::AlignLeft);
        } else if (spec->range) {
            l->addWidget(weakLabel(u"Sur le graphe, glissez les deux barres verticales pour choisir la plage."_s, m_page));
        } else {
            l->addWidget(weakLabel(u"« plage » : deux barres verticales apparaissent sur le graphe, à glisser."_s, m_page));
        }
    }

    // --- paramètres ---
    l->addSpacing(4);
    l->addWidget(strong(u"Paramètres"_s, m_page));
    QString modelError;
    const auto model = fit::Model::make(spec->kind, xname, ws.doc.paramValues(), &modelError);
    const QStringList names = model ? model->params() : QStringList();
    if (!model)
        l->addWidget(colored(modelError, theme::errorColor(), m_page));
    const fit::FitResult *res = spec->result && spec->paramNames == names ? &*spec->result : nullptr;
    if (!names.isEmpty()) {
        auto *grid = new QWidget(m_page);
        auto *gl = new QGridLayout(grid);
        gl->setContentsMargins(0, 0, 0, 0);
        gl->setHorizontalSpacing(12);
        gl->setVerticalSpacing(3);
        const QStringList heads = {u"Nom"_s, u"Fixé"_s, spec->kind.isLinear() ? u"Valeur fixée"_s : u"Valeur fixée ou initiale"_s, u"Résultat"_s};
        for (int c = 0; c < heads.size(); ++c) {
            auto *h = new QLabel(heads[c], grid);
            QPalette p = h->palette();
            p.setColor(QPalette::WindowText, theme::weakColor());
            h->setPalette(p);
            gl->addWidget(h, 0, c);
        }
        for (int i = 0; i < names.size(); ++i) {
            const QString n = names[i];
            const int row = i + 1;
            gl->addWidget(mono(n, grid, true), row, 0);
            auto fixedIt = std::find_if(spec->fixed.begin(), spec->fixed.end(), [&](const auto &p) { return p.first == n; });
            const bool isFixed = fixedIt != spec->fixed.end();
            auto *fix = new QCheckBox(grid);
            fix->setChecked(isFixed);
            fix->setToolTip(u"Fixer ce paramètre à la valeur indiquée"_s);
            const double resVal = res ? res->params[i] : NAN;
            connect(fix, &QCheckBox::toggled, this, [this, n, resVal](bool on) {
                editFit([&](FitSpec &f) {
                    std::erase_if(f.fixed, [&](const auto &p) { return p.first == n; });
                    if (on)
                        f.fixed.emplace_back(n, std::isfinite(resVal) ? resVal : 0.0);
                });
            });
            gl->addWidget(fix, row, 1, Qt::AlignCenter);
            if (isFixed) {
                auto *v = new NumberEdit(grid);
                v->setStyle(style);
                v->setValue(fixedIt->second);
                connect(v, &NumberEdit::valueEdited, this, [this, n](double x) {
                    editFit([&](FitSpec &f) {
                        for (auto &p : f.fixed)
                            if (p.first == n)
                                p.second = x;
                    });
                });
                gl->addWidget(v, row, 2);
            } else if (!spec->kind.isLinear()) {
                auto initIt = std::find_if(spec->initial.begin(), spec->initial.end(), [&](const auto &p) { return p.first == n; });
                const bool has = initIt != spec->initial.end();
                auto *cell = new QWidget(grid);
                auto *cl = new QHBoxLayout(cell);
                cl->setContentsMargins(0, 0, 0, 0);
                auto *imp = new QCheckBox(cell);
                imp->setChecked(has);
                imp->setToolTip(u"Imposer la valeur de départ de l'ajustement"_s);
                connect(imp, &QCheckBox::toggled, this, [this, n, resVal](bool on) {
                    editFit([&](FitSpec &f) {
                        std::erase_if(f.initial, [&](const auto &p) { return p.first == n; });
                        if (on)
                            f.initial.emplace_back(n, std::isfinite(resVal) ? resVal : 1.0);
                    });
                });
                cl->addWidget(imp);
                if (has) {
                    auto *v = new NumberEdit(cell);
                    v->setStyle(style);
                    v->setValue(initIt->second);
                    connect(v, &NumberEdit::valueEdited, this, [this, n](double x) {
                        editFit([&](FitSpec &f) {
                            for (auto &p : f.initial)
                                if (p.first == n)
                                    p.second = x;
                        });
                    });
                    cl->addWidget(v, 1);
                } else {
                    cl->addWidget(weakLabel(u"auto"_s, cell), 1);
                }
                gl->addWidget(cell, row, 2);
            } else {
                gl->addWidget(weakLabel(u"—"_s, grid), row, 2);
            }
            gl->addWidget(mono(res ? paramText(*res, i, style) : QString(), grid), row, 3);
        }
        gl->setColumnStretch(2, 1);
        gl->setColumnStretch(3, 1);
        l->addWidget(grid);
    }

    // --- résultats ---
    l->addSpacing(4);
    if (spec->result) {
        const fit::FitResult &r = *spec->result;
        if (model && model->params().size() == static_cast<qsizetype>(r.params.size())) {
            auto *eq = mono(model->display(r.params, xname, yname, style), m_page, true);
            eq->setWordWrap(true);
            l->addWidget(eq);
        }
        auto *q = new QWidget(m_page);
        auto *ql = new QGridLayout(q);
        ql->setContentsMargins(0, 0, 0, 0);
        ql->setHorizontalSpacing(12);
        auto *r2 = new QLabel(u"R²"_s, q);
        r2->setToolTip(u"Coefficient de détermination : plus il est proche de 1, mieux le modèle décrit les points"_s);
        ql->addWidget(r2, 0, 0);
        ql->addWidget(mono(formatSig(r.r2, 6, style), q), 0, 1);
        ql->addWidget(new QLabel(u"Écart-type des résidus"_s, q), 1, 0);
        ql->addWidget(mono((formatSig(r.residualStd, 3, style) + u" "_s + yunit).trimmed(), q), 1, 1);
        ql->addWidget(new QLabel(u"Points utilisés"_s, q), 2, 0);
        ql->addWidget(mono(QString::number(r.n), q), 2, 1);
        ql->setColumnStretch(2, 1);
        l->addWidget(q);
        if (!r.warning.isEmpty())
            l->addWidget(colored(u"⚠ "_s + r.warning, theme::warnColor(), m_page));
    } else if (!spec->resultError.isEmpty()) {
        l->addWidget(colored(u"⚠ "_s + spec->resultError, theme::errorColor(), m_page));
    }

    // --- couleur, affichage, actions ---
    l->addSpacing(4);
    auto *color = new ColorButton(m_page);
    color->setColor(spec->color);
    connect(color, &ColorButton::colorPicked, this, [this](Color c) { editFit([&](FitSpec &f) { f.color = c; }); });
    auto *shown = new QCheckBox(u"affiché"_s, m_page);
    shown->setChecked(spec->visible);
    connect(shown, &QCheckBox::toggled, this, [this](bool on) { editFit([&](FitSpec &f) { f.visible = on; }); });
    l->addWidget(hbox(m_page, {new QLabel(u"Couleur"_s, m_page), color, shown}));
    // nom dans la légende
    auto *name = new QLineEdit(spec->label.value_or(QString()), m_page);
    name->setObjectName(u"fitLabel"_s);
    name->setPlaceholderText(u"Modèle : "_s + spec->kind.label().toLower());
    name->setToolTip(u"Nom du modèle dans la légende du graphe (vide : nom du modèle)"_s);
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
        const QString t = name->text().trimmed();
        editFit([&](FitSpec &f) {
            if (t.isEmpty())
                f.label.reset();
            else
                f.label = t;
        });
    });
    l->addWidget(hbox(m_page, {new QLabel(u"Légende"_s, m_page), name}));
    // étendue de la courbe : la fonction se prolonge sur tout le graphe, ou s'arrête
    {
        auto *row = new QWidget(m_page);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->addWidget(new QLabel(u"Courbe tracée :"_s, row));
        auto *whole = new QRadioButton(u"sur tout le graphe"_s, row);
        auto *points = new QRadioButton(u"sur les points"_s, row);
        auto *custom = new QRadioButton(u"de"_s, row);
        whole->setObjectName(u"drawGraph"_s);
        points->setObjectName(u"drawPoints"_s);
        custom->setObjectName(u"drawCustom"_s);
        whole->setToolTip(u"La courbe du modèle se prolonge avant et après les points, jusqu'aux bords du graphe (elle suit le zoom)"_s);
        points->setToolTip(u"La courbe s'arrête aux points utilisés pour l'ajustement"_s);
        auto *group = new QButtonGroup(row);
        for (QRadioButton *b : {whole, points, custom})
            group->addButton(b);
        whole->setChecked(spec->draw == FitDraw::Graph);
        points->setChecked(spec->draw == FitDraw::Points);
        custom->setChecked(spec->draw == FitDraw::Custom);
        connect(whole, &QRadioButton::clicked, this, [this] { editFit([](FitSpec &f) { f.draw = FitDraw::Graph; }); });
        connect(points, &QRadioButton::clicked, this, [this] { editFit([](FitSpec &f) { f.draw = FitDraw::Points; }); });
        const QId xid = curve.x;
        connect(custom, &QRadioButton::clicked, this, [this, xid] {
            const auto r = numeric::finiteRange(m_win.store().ws.doc.values(xid));
            editFit([&](FitSpec &f) {
                // de départ : la plage des points, élargie d'un quart de chaque côté
                if (f.draw != FitDraw::Custom && r) {
                    const double d = (r->second - r->first) / 4.0;
                    f.drawRange = {r->first - d, r->second + d};
                }
                f.draw = FitDraw::Custom;
            });
        });
        rl->addWidget(whole);
        rl->addWidget(points);
        rl->addWidget(custom);
        if (spec->draw == FitDraw::Custom) {
            auto *a = new NumberEdit(row);
            auto *b = new NumberEdit(row);
            a->setStyle(style);
            b->setStyle(style);
            a->setValue(spec->drawRange.first);
            b->setValue(spec->drawRange.second);
            connect(a, &NumberEdit::valueEdited, this, [this](double v) { editFit([&](FitSpec &f) { f.drawRange.first = v; }); });
            connect(b, &NumberEdit::valueEdited, this, [this](double v) { editFit([&](FitSpec &f) { f.drawRange.second = v; }); });
            rl->addWidget(a);
            rl->addWidget(new QLabel(u"à"_s, row));
            rl->addWidget(b);
        }
        rl->addStretch();
        l->addWidget(row);
        if (spec->draw == FitDraw::Custom)
            l->addWidget(weakLabel(u"Sur le graphe, glissez les barres en pointillés (losanges) pour choisir l'étendue de la courbe."_s, m_page));
    }

    auto *actions = new QWidget(m_page);
    auto *al = new FlowLayout(actions);
    if (spec->result && model) {
        auto *make = new QPushButton(icons::get("function"), u"Créer la grandeur modèle"_s, actions);
        make->setToolTip(u"Grandeur calculée donnant les valeurs du modèle"_s);
        const QString formula = model->formula(spec->result->params, xname);
        const QString unit = yq ? yq->unit : QString();
        connect(make, &QPushButton::clicked, this, [this, formula, unit, yname] {
            Store &st = m_win.store();
            const QString name = st.ws.doc.uniqueName(yname + u"_mod"_s);
            QString error;
            st.edit([&](Workspace &w) {
                auto r = w.doc.addCalculated(name, unit, formula);
                if (!r) {
                    error = r.error;
                    return;
                }
                if (!w.tables.empty())
                    w.tables.front().columns.push_back(*r);
            });
            if (error.isEmpty())
                st.message(u"Grandeur %1 créée"_s.arg(name));
            else
                st.message(error, icons::Status::Error);
        });
        al->addWidget(make);
        auto *copy = new QPushButton(icons::get("copy"), u"Copier les résultats"_s, actions);
        connect(copy, &QPushButton::clicked, this, [this] {
            QGuiApplication::clipboard()->setText(resultsText());
            m_win.store().message(u"Résultats copiés"_s);
        });
        al->addWidget(copy);
    }
    auto *del = new QPushButton(icons::get("trash"), u"Supprimer ce modèle"_s, actions);
    QPalette dp = del->palette();
    dp.setColor(QPalette::ButtonText, theme::errorColor());
    del->setPalette(dp);
    connect(del, &QPushButton::clicked, this, [this] {
        if (!m_graph || !m_fit)
            return;
        const ViewId gid = *m_graph;
        const int pi = m_plot, fi = *m_fit;
        m_fit.reset();
        m_win.store().edit([&](Workspace &w) {
            GraphView *gv = w.graphMut(gid);
            if (gv && static_cast<size_t>(pi) < gv->plots.size() && static_cast<size_t>(fi) < gv->plots[pi].fits.size())
                gv->plots[pi].fits.erase(gv->plots[pi].fits.begin() + fi);
        });
    });
    al->addWidget(del);
    l->addWidget(actions);
}

QString ModelingPanel::resultsText() const
{
    const Store &store = const_cast<MainWindow &>(m_win).store();
    const GraphView *g = m_graph ? store.ws.graph(*m_graph) : nullptr;
    if (!g || !m_fit)
        return {};
    const FitSpec &spec = g->plots[m_plot].fits[*m_fit];
    if (!spec.result)
        return {};
    const NumberStyle style = store.style();
    const fit::FitResult &r = *spec.result;
    const QString xname = store.ws.doc.name(spec.x), yname = store.ws.doc.name(spec.y);
    QString s;
    if (auto model = fit::Model::make(spec.kind, xname, store.ws.doc.paramValues())) {
        s += u"Modèle %1 : %2\n"_s.arg(spec.kind.label().toLower(), model->display(r.params, xname, yname, style));
        for (int i = 0; i < model->params().size() && static_cast<size_t>(i) < r.params.size(); ++i) {
            const double u = static_cast<size_t>(i) < r.uncertainties.size() ? r.uncertainties[i] : NAN;
            const QString &name = model->params()[i];
            const bool fixed = std::any_of(spec.fixed.begin(), spec.fixed.end(), [&](const auto &f) { return f.first == name; });
            if (fixed)
                s += u"%1 = %2 (fixé)\n"_s.arg(name, formatSig(r.params[i], 6, style));
            else if (std::isfinite(u) && u > 0.0)
                s += u"%1 = %2\n"_s.arg(name, roundMeasure(r.params[i], u, 2, style).toText(QString()));
            else if (u == 0.0)
                s += u"%1 = %2 (incertitude nulle)\n"_s.arg(name, formatSig(r.params[i], 6, style));
            else
                // deux points pour deux paramètres, paramètres liés : pas d'incertitude
                s += u"%1 = %2 (incertitude non calculable)\n"_s.arg(name, formatSig(r.params[i], 6, style));
        }
    }
    s += u"R² = %1\n"_s.arg(formatSig(r.r2, 6, style));
    return s;
}
