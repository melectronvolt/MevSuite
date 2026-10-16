#include "app/graphsettings.h"

#include "app/dialogs.h"
#include "app/graphwidget.h"
#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "plot/resolve.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/// Section repliable : titre cliquable, contenu en dessous.
class Collapsible : public QWidget {
public:
    Collapsible(const QString &title, const QColor &color, bool open, QWidget *parent) : QWidget(parent)
    {
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(2);
        m_head = new QToolButton(this);
        m_head->setText(title);
        m_head->setCheckable(true);
        m_head->setChecked(open);
        m_head->setAutoRaise(true);
        m_head->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_head->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        m_head->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont f = m_head->font();
        f.setWeight(QFont::DemiBold);
        m_head->setFont(f);
        if (color.isValid()) {
            QPalette p = m_head->palette();
            p.setColor(QPalette::ButtonText, color);
            m_head->setPalette(p);
        }
        l->addWidget(m_head);
        m_body = new QFrame(this);
        m_body->setFrameShape(QFrame::StyledPanel);
        m_body->setVisible(open);
        l->addWidget(m_body);
        connect(m_head, &QToolButton::toggled, this, [this](bool on) {
            m_body->setVisible(on);
            m_head->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
            if (onToggle)
                onToggle(on);
        });
    }
    QFrame *body() const { return m_body; }
    std::function<void(bool)> onToggle;

private:
    QToolButton *m_head;
    QFrame *m_body;
};

QIcon markerIcon(Marker m)
{
    // assez de pixels pour l'agrandissement de l'interface
    const double ratio = 2.0 * std::max(theme::zoom(), 1.0);
    QPixmap pm((QSizeF(22, 16) * ratio).toSize());
    pm.setDevicePixelRatio(ratio);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    drawMarker(p, m, QPointF(11, 8), 9, QApplication::palette().color(QPalette::ButtonText), QApplication::palette().color(QPalette::Base));
    return QIcon(pm);
}

QIcon dashIcon(Dash d)
{
    // assez de pixels pour l'agrandissement de l'interface
    const double ratio = 2.0 * std::max(theme::zoom(), 1.0);
    QPixmap pm((QSizeF(30, 12) * ratio).toSize());
    pm.setDevicePixelRatio(ratio);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QApplication::palette().color(QPalette::ButtonText), 1.6);
    const auto pat = dashPattern(d);
    if (!pat.empty()) {
        QList<qreal> q;
        for (double v : pat)
            q << v;
        pen.setDashPattern(q);
        pen.setCapStyle(Qt::FlatCap);
    }
    p.setPen(pen);
    p.drawLine(QPointF(1, 6), QPointF(29, 6));
    return QIcon(pm);
}

QIcon errorsIcon(ErrorBars e)
{
    // assez de pixels pour l'agrandissement de l'interface
    const double ratio = 2.0 * std::max(theme::zoom(), 1.0);
    QPixmap pm((QSizeF(22, 18) * ratio).toSize());
    pm.setDevicePixelRatio(ratio);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QColor c = QApplication::palette().color(QPalette::ButtonText);
    QPen pen(c, 1);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    switch (e) {
    case ErrorBars::None:
        break;
    case ErrorBars::Vertical:
        p.drawLine(QPointF(11.5, 2.5), QPointF(11.5, 15.5));
        p.drawLine(QPointF(8.5, 2.5), QPointF(14.5, 2.5));
        p.drawLine(QPointF(8.5, 15.5), QPointF(14.5, 15.5));
        break;
    case ErrorBars::Both:
        p.drawLine(QPointF(11.5, 2.5), QPointF(11.5, 15.5));
        p.drawLine(QPointF(9.5, 2.5), QPointF(13.5, 2.5));
        p.drawLine(QPointF(9.5, 15.5), QPointF(13.5, 15.5));
        p.drawLine(QPointF(3.5, 9.5), QPointF(19.5, 9.5));
        p.drawLine(QPointF(3.5, 7.5), QPointF(3.5, 11.5));
        p.drawLine(QPointF(19.5, 7.5), QPointF(19.5, 11.5));
        break;
    case ErrorBars::Rectangles: {
        QColor f = c;
        f.setAlphaF(0.2);
        p.setBrush(f);
        p.drawRect(QRectF(4.5, 4.5, 14, 10));
        break;
    }
    }
    p.setRenderHint(QPainter::Antialiasing);
    drawMarker(p, Marker::Disc, QPointF(11.5, 9.5), 5, c, Qt::white);
    return QIcon(pm);
}

QDoubleSpinBox *sizeSpin(double value, double min, double max, double step, const QString &suffix, QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(1);
    s->setValue(value);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    theme::setMaximumWidth(s, 90);
    return s;
}

QWidget *row(QWidget *parent, std::initializer_list<QWidget *> widgets, bool stretch = true)
{
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    for (QWidget *x : widgets)
        l->addWidget(x);
    if (stretch)
        l->addStretch();
    return w;
}

QFormLayout *form(QWidget *parent)
{
    auto *f = new QFormLayout(parent);
    f->setContentsMargins(8, 6, 8, 6);
    f->setHorizontalSpacing(10);
    f->setVerticalSpacing(5);
    f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return f;
}

} // namespace

GraphSettings::GraphSettings(MainWindow &win, ViewId id, QWidget *parent) : QWidget(parent), m_win(win), m_id(id)
{
    theme::setMinimumWidth(this, 280);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 0, 0, 0);
    auto *head = new QHBoxLayout;
    head->addWidget(heading(u"Réglages"_s, this));
    m_plotCombo = new QComboBox(this);
    head->addWidget(m_plotCombo);
    head->addStretch();
    // fermer les réglages (comme le bouton Réglages de la barre du graphe)
    auto *close = toolButton("close", QString(), u"Fermer les réglages"_s, this, false);
    close->setObjectName(u"closeSettings"_s);
    connect(close, &QToolButton::clicked, this, &GraphSettings::closeRequested);
    head->addWidget(close);
    layout->addLayout(head);
    // sections : boutons qui passent à la ligne quand la place manque
    auto *bar = new QWidget(this);
    auto *flow = new FlowLayout(bar, 2);
    m_sections = new QButtonGroup(this);
    int k = 0;
    for (const char *t : {"Courbes", "Axes", "Titre et légende", "Modèles", "Annotations"}) {
        auto *b = new QToolButton(bar);
        b->setText(QString::fromUtf8(t));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_sections->addButton(b, k++);
        flow->addWidget(b);
    }
    m_sections->button(0)->setChecked(true);
    layout->addWidget(bar);
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    m_scroll = new PanelScroll(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_scroll, 1);
    m_rebuild.setSingleShot(true);
    connect(&m_rebuild, &QTimer::timeout, this, &GraphSettings::tryRebuild);
    connect(m_sections, &QButtonGroup::idClicked, this, [this] {
        m_focusAxis = -1;
        rebuild();
    });
    connect(m_plotCombo, &QComboBox::activated, this, [this](int k) {
        m_win.store().selectedPlot[m_id] = k;
        rebuild();
    });
    connect(&win.store(), &Store::workspaceChanged, this, &GraphSettings::scheduleRebuild);
    connect(&win.store(), &Store::displayChanged, this, &GraphSettings::scheduleRebuild);
    rebuild();
}

int GraphSettings::plotIndex() const
{
    const GraphView *g = m_win.store().ws.graph(m_id);
    if (!g)
        return 0;
    auto it = m_win.store().selectedPlot.find(m_id);
    const int k = it == m_win.store().selectedPlot.end() ? 0 : it->second;
    return std::clamp(k, 0, std::max(g->visiblePlots() - 1, 0));
}

void GraphSettings::selectPlot(int plot)
{
    Q_UNUSED(plot);
    scheduleRebuild();
}

void GraphSettings::showSection(SettingsSection section, int axis)
{
    m_focusAxis = axis;
    m_sections->button(static_cast<int>(section))->setChecked(true);
    rebuild();
}

void GraphSettings::scheduleRebuild()
{
    if (!isVisible()) {
        m_stale = true;
        return;
    }
    m_rebuild.start(0);
}

void GraphSettings::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_stale)
        m_rebuild.start(0);
}

void GraphSettings::tryRebuild()
{
    // pas pendant une saisie, ni pendant qu'une liste déroulante ou un menu est ouvert
    if (live::busy(m_page)) {
        m_rebuild.start(250);
        return;
    }
    rebuild();
}

void GraphSettings::editPlot(const std::function<void(Plot &)> &f)
{
    const int pi = plotIndex();
    m_win.store().edit([&](Workspace &ws) {
        GraphView *g = ws.graphMut(m_id);
        if (g && static_cast<size_t>(pi) < g->plots.size())
            f(g->plots[pi]);
    });
}

void GraphSettings::rebuild()
{
    const GraphView *g = m_win.store().ws.graph(m_id);
    if (!g)
        return;
    {
        QSignalBlocker b(m_plotCombo);
        m_plotCombo->clear();
        for (int k = 0; k < g->visiblePlots(); ++k)
            m_plotCombo->addItem(u"Graphe n° %1"_s.arg(k + 1));
        m_plotCombo->setCurrentIndex(plotIndex());
        m_plotCombo->setVisible(g->visiblePlots() > 1);
    }
    m_stale = false;
    m_rebuild.stop();
    const int scrollPos = m_scroll->verticalScrollBar()->value();
    const live::Focus focus = live::remember(m_page);
    auto *page = new QWidget;
    m_page = page;
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(4, 4, 8, 8);
    l->setSpacing(6);
    switch (static_cast<SettingsSection>(std::max(0, m_sections->checkedId()))) {
    case SettingsSection::Curves: buildCurves(l); break;
    case SettingsSection::Axes: buildAxes(l); break;
    case SettingsSection::Title: buildTitle(l); break;
    case SettingsSection::Models: buildModels(l); break;
    case SettingsSection::Annotations: buildAnnotations(l); break;
    }
    l->addStretch();
    m_scroll->setWidget(page);
    live::track(page);
    keepScroll(m_scroll, scrollPos);
    live::restore(page, focus);
}

void GraphSettings::buildCurves(QVBoxLayout *l)
{
    Store &store = m_win.store();
    const GraphView *g = store.ws.graph(m_id);
    const int pi = plotIndex();
    const Plot &plot = g->plots[pi];
    const Document &doc = store.ws.doc;
    const size_t n = plot.curves.size();
    if (m_expanded.size() != n)
        m_expanded.assign(n, n <= 2);
    const PlotTheme th = theme::isDark() && store.prefs.darkPlots ? PlotTheme::darkTheme() : PlotTheme::light();
    for (size_t k = 0; k < n; ++k) {
        const CurveSpec c = plot.curves[k];
        const Quantity *yq = doc.get(c.y);
        const Quantity *xq = doc.get(c.x);
        const QString title = u"●  %1(%2)"_s.arg(yq ? yq->name : u"?"_s, xq ? xq->name : u"?"_s);
        auto *box = new Collapsible(title, th.adapt(c.color), m_expanded[k], m_page);
        box->onToggle = [this, k](bool on) {
            if (k < m_expanded.size())
                m_expanded[k] = on;
        };
        auto *f = form(box->body());
        auto set = [this, k](const std::function<void(CurveSpec &)> &fn) {
            editPlot([&](Plot &p) {
                if (k < p.curves.size())
                    fn(p.curves[k]);
            });
        };
        auto *xc = new QuantityCombo(store, false, box->body());
        xc->setCurrent(c.x);
        auto *yc = new QuantityCombo(store, false, box->body());
        yc->setCurrent(c.y);
        connect(xc, &QuantityCombo::picked, this, [set](std::optional<QId> q) {
            if (q)
                set([&](CurveSpec &cs) { cs.x = *q; });
        });
        connect(yc, &QuantityCombo::picked, this, [set](std::optional<QId> q) {
            if (q)
                set([&](CurveSpec &cs) { cs.y = *q; });
        });
        f->addRow(u"Abscisse"_s, xc);
        f->addRow(u"Ordonnée"_s, yc);
        auto *side = new QComboBox(box->body());
        side->addItems({u"gauche"_s, u"droite"_s});
        side->setCurrentIndex(c.side == YSide::Right ? 1 : 0);
        connect(side, &QComboBox::activated, this, [set](int i) { set([&](CurveSpec &cs) { cs.side = i == 1 ? YSide::Right : YSide::Left; }); });
        f->addRow(u"Axe"_s, side);
        auto *color = new ColorButton(box->body());
        color->setColor(c.color);
        connect(color, &ColorButton::colorPicked, this, [set](Color col) { set([&](CurveSpec &cs) { cs.color = col; }); });
        f->addRow(u"Couleur"_s, color);
        auto *marker = new QComboBox(box->body());
        for (Marker m : ALL_MARKERS)
            marker->addItem(markerIcon(m), markerLabel(m));
        marker->setCurrentIndex(static_cast<int>(c.marker));
        connect(marker, &QComboBox::activated, this, [set](int i) { set([&](CurveSpec &cs) { cs.marker = ALL_MARKERS[i]; }); });
        auto *msize = sizeSpin(c.markerSize, 2, 20, 0.5, u" px"_s, box->body());
        connect(msize, &QDoubleSpinBox::valueChanged, this, [set](double v) { set([&](CurveSpec &cs) { cs.markerSize = v; }); });
        f->addRow(u"Marqueur"_s, row(box->body(), {marker, msize}));
        auto *line = new QComboBox(box->body());
        for (LineKind lk : ALL_LINES)
            line->addItem(lineLabel(lk));
        line->setItemData(2, u"Courbe douce passant par tous les points (spline cubique)"_s, Qt::ToolTipRole);
        line->setItemData(3, u"Courbe passant par tous les points, sans oscillation entre deux points"_s, Qt::ToolTipRole);
        line->setCurrentIndex(static_cast<int>(c.line));
        connect(line, &QComboBox::activated, this, [set](int i) { set([&](CurveSpec &cs) { cs.line = ALL_LINES[i]; }); });
        auto *lw = sizeSpin(c.lineWidth, 0.3, 6, 0.1, u" px"_s, box->body());
        lw->setVisible(c.line != LineKind::None);
        connect(lw, &QDoubleSpinBox::valueChanged, this, [set](double v) { set([&](CurveSpec &cs) { cs.lineWidth = v; }); });
        f->addRow(u"Trait"_s, row(box->body(), {line, lw}));
        if (c.line != LineKind::None) {
            auto *dash = new QComboBox(box->body());
            dash->setIconSize(theme::px(QSize(30, 12)));
            for (Dash d : ALL_DASHES)
                dash->addItem(dashIcon(d), dashLabel(d));
            dash->setCurrentIndex(static_cast<int>(c.dash));
            connect(dash, &QComboBox::activated, this, [set](int i) { set([&](CurveSpec &cs) { cs.dash = ALL_DASHES[i]; }); });
            f->addRow(u"Style"_s, dash);
        }
        // incertitudes
        auto *err = new QComboBox(box->body());
        err->setIconSize(theme::px(QSize(22, 18)));
        for (ErrorBars e : ALL_ERRORS)
            err->addItem(errorsIcon(e), errorsLabel(e));
        err->setCurrentIndex(static_cast<int>(c.errors));
        connect(err, &QComboBox::activated, this, [set](int i) { set([&](CurveSpec &cs) { cs.errors = ALL_ERRORS[i]; }); });
        f->addRow(u"Incertitudes"_s, err);
        const bool sides = c.errXLow || c.errXHigh || c.errYLow || c.errYHigh;
        const bool hasUy = (yq && yq->uncertainty) || c.errYLow || c.errYHigh, hasUx = (xq && xq->uncertainty) || c.errXLow || c.errXHigh;
        if (c.errors != ErrorBars::None) {
            auto *ew = sizeSpin(c.errorWidth, 0.3, 4, 0.1, u" px"_s, box->body());
            ew->setToolTip(u"Épaisseur des barres d'incertitude"_s);
            connect(ew, &QDoubleSpinBox::valueChanged, this, [set](double v) { set([&](CurveSpec &cs) { cs.errorWidth = v; }); });
            if (c.errors != ErrorBars::Rectangles) {
                f->addRow(u"Barres"_s, row(box->body(), {ew}));
                auto *capAuto = new QCheckBox(u"auto"_s, box->body());
                capAuto->setChecked(!c.capSize);
                capAuto->setToolTip(u"Embouts proportionnés au marqueur"_s);
                auto *cap = sizeSpin(c.capSize.value_or(std::clamp(c.markerSize * 0.5, 2.5, 6.0)), 0, 15, 0.5, u" px"_s, box->body());
                cap->setEnabled(c.capSize.has_value());
                cap->setToolTip(u"Demi-largeur des embouts (0 : barres sans embout)"_s);
                connect(capAuto, &QCheckBox::toggled, this, [set, cap](bool on) {
                    set([&](CurveSpec &cs) {
                        if (on)
                            cs.capSize.reset();
                        else
                            cs.capSize = cap->value();
                    });
                });
                connect(cap, &QDoubleSpinBox::valueChanged, this, [set](double v) { set([&](CurveSpec &cs) { cs.capSize = v; }); });
                f->addRow(u"Embouts"_s, row(box->body(), {capAuto, cap}));
            } else {
                f->addRow(u"Contour"_s, row(box->body(), {ew}));
            }
            const bool needX = c.errors != ErrorBars::Vertical;
            // une grandeur pour chaque côté : barres ou rectangles dissymétriques (sinon, de part
            // et d'autre, l'incertitude rattachée à la grandeur)
            auto *bySide = new QCheckBox(u"une grandeur pour chaque côté"_s, box->body());
            bySide->setObjectName(u"errorSides"_s);
            bySide->setChecked(sides);
            bySide->setToolTip(u"Choisir une grandeur existante pour le haut, le bas, la gauche et la droite (incertitudes différentes de part et d'autre)"_s);
            connect(bySide, &QCheckBox::toggled, this, [this, k, set](bool on) {
                if (on) {
                    // les listes apparaissent ; rien ne change tant qu'aucune grandeur n'est choisie
                    m_errorSides.insert(k);
                    rebuild();
                    return;
                }
                m_errorSides.erase(k);
                set([&](CurveSpec &cs) { cs.errXLow = cs.errXHigh = cs.errYLow = cs.errYHigh = std::nullopt; });
            });
            f->addRow(QString(), bySide);
            if (sides || m_errorSides.count(k)) {
                auto sideCombo = [&](std::optional<QId> current, const Quantity *of, std::optional<QId> CurveSpec::*member, const QString &name) {
                    auto *combo = new QuantityCombo(store, true, box->body());
                    combo->setObjectName(name);
                    // premier choix : l'incertitude rattachée à la grandeur, s'il y en a une
                    combo->setItemText(0, of && of->uncertainty ? u"%1 (rattachée)"_s.arg(doc.name(*of->uncertainty)) : u"aucune"_s);
                    combo->setCurrent(current);
                    connect(combo, &QuantityCombo::picked, this, [set, member](std::optional<QId> id) { set([&](CurveSpec &cs) { cs.*member = id; }); });
                    return combo;
                };
                f->addRow(u"y vers le haut"_s, sideCombo(c.errYHigh, yq, &CurveSpec::errYHigh, u"errorYHigh"_s));
                f->addRow(u"y vers le bas"_s, sideCombo(c.errYLow, yq, &CurveSpec::errYLow, u"errorYLow"_s));
                if (needX) {
                    f->addRow(u"x vers la droite"_s, sideCombo(c.errXHigh, xq, &CurveSpec::errXHigh, u"errorXHigh"_s));
                    f->addRow(u"x vers la gauche"_s, sideCombo(c.errXLow, xq, &CurveSpec::errXLow, u"errorXLow"_s));
                }
            }
            if (!hasUy || (needX && !hasUx)) {
                QString what;
                if (!hasUy && yq)
                    what = u"u(%1)"_s.arg(yq->name);
                if (needX && !hasUx && xq)
                    what += (what.isEmpty() ? QString() : u" et "_s) + u"u(%1)"_s.arg(xq->name);
                f->addRow(QString(), weakLabel(u"Aucune incertitude rattachée pour %1 : créez-la ci-dessous ou par le menu de la grandeur."_s.arg(what), box->body()));
                auto *make = new QWidget(box->body());
                auto *ml = new QHBoxLayout(make);
                ml->setContentsMargins(0, 0, 0, 0);
                if (!hasUy && yq) {
                    auto *b = new QPushButton(icons::get("plusminus"), u"u(%1)…"_s.arg(yq->name), make);
                    const QId qid = yq->id;
                    connect(b, &QPushButton::clicked, this, [this, qid] { dialogs::newUncertainty(m_win, qid); });
                    ml->addWidget(b);
                }
                if (needX && !hasUx && xq) {
                    auto *b = new QPushButton(icons::get("plusminus"), u"u(%1)…"_s.arg(xq->name), make);
                    const QId qid = xq->id;
                    connect(b, &QPushButton::clicked, this, [this, qid] { dialogs::newUncertainty(m_win, qid); });
                    ml->addWidget(b);
                }
                ml->addStretch();
                f->addRow(QString(), make);
            }
        } else if (hasUy || hasUx) {
            f->addRow(QString(), weakLabel(u"Incertitude rattachée : choisissez « Barres » pour l'afficher."_s, box->body()));
        }
        // légende
        auto *inLegend = new QCheckBox(u"dans la légende"_s, box->body());
        inLegend->setChecked(c.inLegend);
        connect(inLegend, &QCheckBox::toggled, this, [set](bool on) { set([&](CurveSpec &cs) { cs.inLegend = on; }); });
        auto *label = new QLineEdit(c.label.value_or(QString()), box->body());
        label->setPlaceholderText(yq ? yq->name : QString());
        connect(label, &QLineEdit::editingFinished, this, [set, label] {
            const QString t = label->text();
            set([&](CurveSpec &cs) {
                if (t.isEmpty())
                    cs.label.reset();
                else
                    cs.label = t;
            });
        });
        f->addRow(u"Légende"_s, inLegend);
        f->addRow(QString(), label);
        // visibilité et ordre
        auto *visible = new QCheckBox(u"visible"_s, box->body());
        visible->setChecked(c.visible);
        connect(visible, &QCheckBox::toggled, this, [set](bool on) { set([&](CurveSpec &cs) { cs.visible = on; }); });
        auto *up = new QToolButton(box->body());
        up->setArrowType(Qt::UpArrow);
        up->setAutoRaise(true);
        up->setToolTip(u"Monter"_s);
        up->setEnabled(k > 0);
        auto *down = new QToolButton(box->body());
        down->setArrowType(Qt::DownArrow);
        down->setAutoRaise(true);
        down->setToolTip(u"Descendre"_s);
        down->setEnabled(k + 1 < n);
        auto *remove = new QToolButton(box->body());
        remove->setIcon(icons::get("trash"));
        remove->setText(u"Retirer"_s);
        remove->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        remove->setAutoRaise(true);
        remove->setToolTip(u"Retirer la courbe du graphe, avec ses modèles et sa méthode des tangentes (la grandeur reste)"_s);
        connect(up, &QToolButton::clicked, this, [this, k] { editPlot([&](Plot &p) { std::swap(p.curves[k], p.curves[k - 1]); }); });
        connect(down, &QToolButton::clicked, this, [this, k] { editPlot([&](Plot &p) { std::swap(p.curves[k], p.curves[k + 1]); }); });
        connect(remove, &QToolButton::clicked, this, [this, k] {
            m_expanded.clear();
            editPlot([&](Plot &p) { p.removeCurve(k); });
        });
        f->addRow(row(box->body(), {visible, up, down, remove}));
        // mise en évidence de la courbe sur le graphe pendant le survol
        box->setAttribute(Qt::WA_Hover);
        box->installEventFilter(this);
        box->setProperty("curve", static_cast<int>(k));
        l->addWidget(box);
    }
    // nouvelle courbe
    l->addSpacing(6);
    l->addWidget(heading(u"Ajouter une courbe"_s, m_page));
    auto *yc = new QuantityCombo(store, false, m_page);
    auto *xc = new QuantityCombo(store, false, m_page);
    std::optional<QId> firstX = plot.curves.empty() ? std::nullopt : std::optional<QId>(plot.curves.front().x);
    if (!firstX && store.pendingAbscissa.count(m_id))
        firstX = store.pendingAbscissa.at(m_id);
    if (!firstX && !doc.quantities.empty())
        firstX = doc.quantities.front().id;
    xc->setCurrent(firstX);
    for (const auto &q : doc.quantities) {
        if (q.id != firstX) {
            yc->setCurrent(q.id);
            break;
        }
    }
    auto *add = new QPushButton(icons::get("plus"), u"Ajouter"_s, m_page);
    add->setEnabled(doc.quantities.size() >= 1);
    connect(add, &QPushButton::clicked, this, [this, xc, yc] {
        const auto x = xc->current(), y = yc->current();
        if (!x || !y)
            return;
        m_expanded.clear();
        editPlot([&](Plot &p) { p.addCurve(*x, *y); });
    });
    l->addWidget(row(m_page, {yc, new QLabel(u"en fonction de"_s, m_page), xc}));
    l->addWidget(add, 0, Qt::AlignLeft);
    l->addWidget(weakLabel(u"Astuce : glissez une grandeur sur le graphe pour l'ajouter ; sur l'axe des abscisses pour changer d'abscisse."_s, m_page));
}

void GraphSettings::buildAxes(QVBoxLayout *l)
{
    Store &store = m_win.store();
    const GraphView *g = store.ws.graph(m_id);
    const int pi = plotIndex();
    const Plot &plot = g->plots[pi];
    const PlotData data = resolve(plot, store.ws.doc, store.style());
    const bool right = plot.usesRightAxis();
    struct Entry {
        int index;
        QString title;
        const AxisSpec *spec;
        QString name, unit;
    };
    std::vector<Entry> entries = {{0, u"Abscisses"_s, &plot.x, data.xName, data.xUnit}, {1, u"Ordonnées (gauche)"_s, &plot.y, data.yName, data.yUnit}};
    if (right)
        entries.push_back({2, u"Ordonnées (droite)"_s, &plot.y2, data.y2Name, data.y2Unit});
    const NumberStyle style = store.style();
    for (const Entry &e : entries) {
        const bool open = m_focusAxis >= 0 ? m_focusAxis == e.index : e.index == 0;
        auto *box = new Collapsible(e.title, QColor(), open, m_page);
        auto *f = form(box->body());
        const AxisSpec a = *e.spec;
        const int which = e.index;
        auto set = [this, which](const std::function<void(AxisSpec &)> &fn) {
            editPlot([&](Plot &p) { fn(which == 0 ? p.x : which == 1 ? p.y : p.y2); });
        };
        // légende et unité
        auto *labelAuto = new QCheckBox(u"auto"_s, box->body());
        labelAuto->setChecked(!a.label);
        auto *label = new QLineEdit(a.label.value_or(e.name), box->body());
        label->setEnabled(a.label.has_value());
        connect(labelAuto, &QCheckBox::toggled, this, [set, name = e.name](bool on) {
            set([&](AxisSpec &s) {
                if (on)
                    s.label.reset();
                else
                    s.label = name;
            });
        });
        connect(label, &QLineEdit::editingFinished, this, [set, label] { set([&](AxisSpec &s) { s.label = label->text(); }); });
        f->addRow(u"Légende"_s, row(box->body(), {labelAuto, label}, false));
        auto *unitAuto = new QCheckBox(u"auto"_s, box->body());
        unitAuto->setChecked(!a.unit);
        auto *unit = new QLineEdit(a.unit.value_or(e.unit), box->body());
        unit->setPlaceholderText(u"m.s-1"_s);
        unit->setEnabled(a.unit.has_value());
        unit->setToolTip(u"Affichée : "_s + prettyUnit(a.unit.value_or(e.unit)));
        connect(unitAuto, &QCheckBox::toggled, this, [set, u = e.unit](bool on) {
            set([&](AxisSpec &s) {
                if (on)
                    s.unit.reset();
                else
                    s.unit = u;
            });
        });
        connect(unit, &QLineEdit::editingFinished, this, [set, unit] { set([&](AxisSpec &s) { s.unit = unit->text(); }); });
        f->addRow(u"Unité"_s, row(box->body(), {unitAuto, unit}, false));
        // échelle
        auto *autoScale = new QCheckBox(u"automatique"_s, box->body());
        autoScale->setChecked(a.autoScale);
        connect(autoScale, &QCheckBox::toggled, this, [set](bool on) { set([&](AxisSpec &s) { s.autoScale = on; }); });
        f->addRow(u"Échelle"_s, autoScale);
        auto *mn = new NumberEdit(box->body());
        auto *mx = new NumberEdit(box->body());
        mn->setStyle(style);
        mx->setStyle(style);
        // bornes affichées : celles du graphe à l'écran si l'échelle est automatique
        double shownMin = a.min, shownMax = a.max;
        if (GraphPanel *gp = m_win.graphPanel(m_id); gp && a.autoScale && static_cast<size_t>(pi) < gp->canvas()->layouts().size()) {
            const PlotLayout &lay = gp->canvas()->layouts()[pi];
            const AxisMap *m = which == 0 ? &lay.x : which == 1 ? &lay.y : (lay.y2 ? &*lay.y2 : nullptr);
            if (m) {
                shownMin = m->min;
                shownMax = m->max;
            }
        }
        mn->setValue(shownMin);
        mx->setValue(shownMax);
        connect(mn, &NumberEdit::valueEdited, this, [set, shownMax](double v) {
            set([&](AxisSpec &s) {
                if (s.autoScale)
                    s.max = shownMax;
                s.min = v;
                s.autoScale = false;
            });
        });
        connect(mx, &NumberEdit::valueEdited, this, [set, shownMin](double v) {
            set([&](AxisSpec &s) {
                if (s.autoScale)
                    s.min = shownMin;
                s.max = v;
                s.autoScale = false;
            });
        });
        f->addRow(QString(), row(box->body(), {new QLabel(u"de"_s, box->body()), mn, new QLabel(u"à"_s, box->body()), mx}));
        auto *log = new QCheckBox(u"logarithmique"_s, box->body());
        log->setChecked(a.log);
        connect(log, &QCheckBox::toggled, this, [set](bool on) { set([&](AxisSpec &s) { s.log = on; }); });
        auto *zero = new QCheckBox(u"inclure 0"_s, box->body());
        zero->setChecked(a.includeZero);
        connect(zero, &QCheckBox::toggled, this, [set](bool on) { set([&](AxisSpec &s) { s.includeZero = on; }); });
        f->addRow(QString(), row(box->body(), {log, zero}));
        // graduations
        auto *stepAuto = new QCheckBox(u"pas auto"_s, box->body());
        stepAuto->setChecked(!a.majorStep);
        auto *step = new NumberEdit(box->body());
        step->setStyle(style);
        step->setValue(a.majorStep.value_or(std::max(std::abs(shownMax - shownMin) / 5.0, 1e-12)));
        step->setEnabled(a.majorStep.has_value());
        connect(stepAuto, &QCheckBox::toggled, this, [set, step](bool on) {
            set([&](AxisSpec &s) {
                if (on)
                    s.majorStep.reset();
                else
                    s.majorStep = std::max(std::abs(step->value()), 1e-300);
            });
        });
        connect(step, &NumberEdit::valueEdited, this, [set](double v) { set([&](AxisSpec &s) { s.majorStep = std::max(std::abs(v), 1e-300); }); });
        f->addRow(u"Graduations"_s, row(box->body(), {stepAuto, step}));
        auto *divAuto = new QCheckBox(u"subdivisions auto"_s, box->body());
        divAuto->setChecked(!a.minorDivisions);
        auto *div = new QSpinBox(box->body());
        div->setRange(1, 10);
        div->setValue(a.minorDivisions.value_or(5));
        div->setEnabled(a.minorDivisions.has_value());
        connect(divAuto, &QCheckBox::toggled, this, [set, div](bool on) {
            set([&](AxisSpec &s) {
                if (on)
                    s.minorDivisions.reset();
                else
                    s.minorDivisions = div->value();
            });
        });
        connect(div, &QSpinBox::valueChanged, this, [set](int v) { set([&](AxisSpec &s) { s.minorDivisions = v; }); });
        f->addRow(QString(), row(box->body(), {divAuto, div}));
        auto *gmaj = new QCheckBox(u"principal"_s, box->body());
        gmaj->setChecked(a.gridMajor);
        connect(gmaj, &QCheckBox::toggled, this, [set](bool on) { set([&](AxisSpec &s) { s.gridMajor = on; }); });
        auto *gmin = new QCheckBox(u"secondaire"_s, box->body());
        gmin->setChecked(a.gridMinor);
        connect(gmin, &QCheckBox::toggled, this, [set](bool on) { set([&](AxisSpec &s) { s.gridMinor = on; }); });
        f->addRow(u"Quadrillage"_s, row(box->body(), {gmaj, gmin}));
        // format des nombres
        auto *fmt = new QComboBox(box->body());
        fmt->addItems({u"automatique"_s, u"décimales fixes"_s, u"scientifique"_s});
        fmt->setCurrentIndex(static_cast<int>(a.format.kind));
        auto *dec = new QSpinBox(box->body());
        dec->setRange(0, 8);
        dec->setSuffix(u" déc."_s);
        dec->setValue(a.format.decimals);
        dec->setVisible(a.format.kind != NumFormat::Auto);
        connect(fmt, &QComboBox::activated, this, [set, dec](int i) { set([&](AxisSpec &s) { s.format = NumFormat{static_cast<NumFormat::Kind>(i), dec->value()}; }); });
        connect(dec, &QSpinBox::valueChanged, this, [set](int v) { set([&](AxisSpec &s) { s.format.decimals = v; }); });
        f->addRow(u"Nombres"_s, row(box->body(), {fmt, dec}));
        auto *ls = sizeSpin(a.labelSize, 6, 30, 0.5, QString(), box->body());
        ls->setPrefix(u"légende "_s);
        theme::setMaximumWidth(ls, 130);
        connect(ls, &QDoubleSpinBox::valueChanged, this, [set](double v) { set([&](AxisSpec &s) { s.labelSize = v; }); });
        auto *ts = sizeSpin(a.tickSize, 6, 30, 0.5, QString(), box->body());
        ts->setPrefix(u"nombres "_s);
        theme::setMaximumWidth(ts, 130);
        connect(ts, &QDoubleSpinBox::valueChanged, this, [set](double v) { set([&](AxisSpec &s) { s.tickSize = v; }); });
        f->addRow(u"Tailles"_s, row(box->body(), {ls, ts}));
        l->addWidget(box);
    }
    if (!right)
        l->addWidget(weakLabel(u"L'axe de droite apparaît dès qu'une courbe lui est rattachée (réglages de la courbe)."_s, m_page));
}

void GraphSettings::buildTitle(QVBoxLayout *l)
{
    Store &store = m_win.store();
    const Plot &plot = store.ws.graph(m_id)->plots[plotIndex()];
    l->addWidget(heading(u"Titre"_s, m_page));
    auto *tb = new QWidget(m_page);
    auto *f = form(tb);
    auto *text = new QLineEdit(plot.title.text, tb);
    text->setPlaceholderText(u"Titre du graphe"_s);
    connect(text, &QLineEdit::editingFinished, this, [this, text] { editPlot([&](Plot &p) { p.title.text = text->text(); }); });
    f->addRow(u"Texte"_s, text);
    auto *visible = new QCheckBox(u"visible"_s, tb);
    visible->setChecked(plot.title.visible);
    connect(visible, &QCheckBox::toggled, this, [this](bool on) { editPlot([&](Plot &p) { p.title.visible = on; }); });
    auto *bold = new QCheckBox(u"gras"_s, tb);
    bold->setChecked(plot.title.bold);
    connect(bold, &QCheckBox::toggled, this, [this](bool on) { editPlot([&](Plot &p) { p.title.bold = on; }); });
    f->addRow(u"Affichage"_s, row(tb, {visible, bold}));
    auto *align = new QComboBox(tb);
    align->addItems({u"à gauche"_s, u"centré"_s, u"à droite"_s});
    align->setCurrentIndex(static_cast<int>(plot.title.align));
    connect(align, &QComboBox::activated, this, [this](int i) { editPlot([&](Plot &p) { p.title.align = static_cast<HAlign>(i); }); });
    f->addRow(u"Position"_s, align);
    auto *size = sizeSpin(plot.title.size, 8, 36, 0.5, QString(), tb);
    connect(size, &QDoubleSpinBox::valueChanged, this, [this](double v) { editPlot([&](Plot &p) { p.title.size = v; }); });
    f->addRow(u"Taille"_s, size);
    l->addWidget(tb);
    l->addSpacing(8);
    l->addWidget(heading(u"Légende des courbes"_s, m_page));
    auto *lb = new QWidget(m_page);
    auto *lf = form(lb);
    auto *lv = new QCheckBox(u"visible"_s, lb);
    lv->setChecked(plot.legend.visible);
    connect(lv, &QCheckBox::toggled, this, [this](bool on) { editPlot([&](Plot &p) { p.legend.visible = on; }); });
    lf->addRow(u"Affichage"_s, lv);
    auto *pos = new QComboBox(lb);
    for (LegendPos lp : ALL_LEGEND_POS)
        pos->addItem(legendPosLabel(lp));
    pos->setCurrentIndex(static_cast<int>(plot.legend.pos));
    connect(pos, &QComboBox::activated, this, [this](int i) { editPlot([&](Plot &p) { p.legend.pos = ALL_LEGEND_POS[i]; }); });
    lf->addRow(u"Position"_s, pos);
    auto *ls = sizeSpin(plot.legend.size, 6, 24, 0.5, QString(), lb);
    connect(ls, &QDoubleSpinBox::valueChanged, this, [this](double v) { editPlot([&](Plot &p) { p.legend.size = v; }); });
    lf->addRow(u"Taille"_s, ls);
    l->addWidget(lb);
    l->addWidget(weakLabel(u"Le texte de chaque entrée se règle avec la courbe (onglet Courbes)."_s, m_page));
}

void GraphSettings::buildModels(QVBoxLayout *l)
{
    Store &store = m_win.store();
    const int pi = plotIndex();
    const Plot &plot = store.ws.graph(m_id)->plots[pi];
    const NumberStyle style = store.style();
    const PlotTheme th = theme::isDark() && store.prefs.darkPlots ? PlotTheme::darkTheme() : PlotTheme::light();
    if (plot.fits.empty())
        l->addWidget(weakLabel(u"Aucun modèle sur ce graphe."_s, m_page));
    for (size_t k = 0; k < plot.fits.size(); ++k) {
        const FitSpec &f = plot.fits[k];
        auto *w = new QWidget(m_page);
        auto *wl = new QVBoxLayout(w);
        wl->setContentsMargins(0, 0, 0, 0);
        auto *visible = new QCheckBox(u"%1 (%2/%3)"_s.arg(f.kind.label(), store.ws.doc.name(f.y), store.ws.doc.name(f.x)), w);
        visible->setChecked(f.visible);
        QPalette pal = visible->palette();
        pal.setColor(QPalette::WindowText, th.adapt(f.color));
        visible->setPalette(pal);
        connect(visible, &QCheckBox::toggled, this, [this, k](bool on) { editPlot([&](Plot &p) { p.fits[k].visible = on; }); });
        wl->addWidget(visible);
        const QString summary = f.result ? u"R² = "_s + formatSig(f.result->r2, 5, style) : f.resultError;
        auto *edit = new QToolButton(w);
        edit->setIcon(icons::get("pencil"));
        edit->setAutoRaise(true);
        edit->setToolTip(u"Ouvrir dans la fenêtre Modélisation"_s);
        connect(edit, &QToolButton::clicked, this, [this, pi, k] { m_win.openModeling(m_id, pi, static_cast<int>(k)); });
        auto *del = new QToolButton(w);
        del->setIcon(icons::get("trash"));
        del->setAutoRaise(true);
        del->setToolTip(u"Supprimer le modèle"_s);
        connect(del, &QToolButton::clicked, this, [this, k] { editPlot([&](Plot &p) { p.fits.erase(p.fits.begin() + static_cast<std::ptrdiff_t>(k)); }); });
        wl->addWidget(row(w, {weakLabel(summary, w), edit, del}));
        l->addWidget(w);
    }
    auto *add = new QPushButton(icons::get("plus"), u"Nouveau modèle…"_s, m_page);
    connect(add, &QPushButton::clicked, this, [this, pi] { m_win.openModeling(m_id, pi, -1); });
    l->addWidget(add, 0, Qt::AlignLeft);
    if (!plot.tangents.empty()) {
        l->addSpacing(8);
        l->addWidget(heading(u"Méthode des tangentes"_s, m_page));
    }
    for (size_t k = 0; k < plot.tangents.size(); ++k) {
        const TangentSpec &t = plot.tangents[k];
        // couleur, nom, verrou, contact automatique, suppression
        auto *color = new ColorButton(m_page);
        color->setColor(t.color);
        color->setToolTip(u"Couleur des tangentes"_s);
        connect(color, &ColorButton::colorPicked, this, [this, k](Color c) { editPlot([&](Plot &p) { p.tangents[k].color = c; }); });
        auto *name = new QLabel(u"%1(%2)"_s.arg(store.ws.doc.name(t.y), store.ws.doc.name(t.x)), m_page);
        QPalette pal = name->palette();
        pal.setColor(QPalette::WindowText, th.adapt(t.color));
        name->setPalette(pal);
        auto *lock = new QToolButton(m_page);
        lock->setObjectName(u"tangentLock"_s);
        lock->setCheckable(true);
        lock->setChecked(t.locked);
        lock->setAutoRaise(true);
        lock->setIcon(icons::get(t.locked ? "lock" : "unlock"));
        lock->setToolTip(t.locked ? u"Verrouillées : les points de contact ne se glissent plus (cliquer pour libérer)"_s
                                  : u"Libres : les points de contact se glissent sur le graphe (cliquer pour verrouiller)"_s);
        connect(lock, &QToolButton::toggled, this, [this, k](bool on) { editPlot([&](Plot &p) { p.tangents[k].locked = on; }); });
        auto *reset = new QToolButton(m_page);
        reset->setIcon(icons::get("undo"));
        reset->setAutoRaise(true);
        reset->setToolTip(u"Point de contact automatique"_s);
        connect(reset, &QToolButton::clicked, this, [this, k] { editPlot([&](Plot &p) { p.tangents[k].contact.reset(); }); });
        auto *del = new QToolButton(m_page);
        del->setObjectName(u"tangentDelete"_s);
        del->setIcon(icons::get("trash"));
        del->setAutoRaise(true);
        del->setToolTip(u"Supprimer la méthode des tangentes"_s);
        connect(del, &QToolButton::clicked, this, [this, k] { editPlot([&](Plot &p) { p.tangents.erase(p.tangents.begin() + static_cast<std::ptrdiff_t>(k)); }); });
        l->addWidget(row(m_page, {color, name, lock, reset, del}));
        QString err;
        if (auto res = computeTangents(t, store.ws.doc, &err)) {
            const auto &e = res->second.equivalence;
            l->addWidget(new QLabel(u"Équivalence : %1 = %2 ; %3 = %4"_s.arg(store.ws.doc.name(t.x), formatSig(e.first, 4, style), store.ws.doc.name(t.y),
                                                                             formatSig(e.second, 3, style)),
                                    m_page));
            if (!t.locked)
                l->addWidget(weakLabel(u"Glissez un point de contact sur le graphe pour déplacer les tangentes."_s, m_page));
        } else {
            auto *el = new QLabel(err, m_page);
            el->setWordWrap(true);
            QPalette ep = el->palette();
            ep.setColor(QPalette::WindowText, theme::errorColor());
            el->setPalette(ep);
            l->addWidget(el);
        }
    }
}

void GraphSettings::buildAnnotations(QVBoxLayout *l)
{
    Store &store = m_win.store();
    const Plot &plot = store.ws.graph(m_id)->plots[plotIndex()];
    const NumberStyle style = store.style();
    auto *bar = new QWidget(m_page);
    auto *flow = new FlowLayout(bar);
    // nouvelle annotation au milieu de ce qui est affiché
    const GraphCanvas *canvas = nullptr;
    if (const GraphPanel *gp = m_win.graphPanel(m_id))
        canvas = gp->canvas();
    const int pi = plotIndex();
    auto middle = [canvas, pi](double fx, double fy) -> std::pair<double, double> {
        if (!canvas || static_cast<size_t>(pi) >= canvas->layouts().size())
            return {fx, fy};
        const PlotLayout &pl = canvas->layouts()[static_cast<size_t>(pi)];
        return {pl.x.fromPx(pl.area.left() + pl.area.width() * fx), pl.y.fromPx(pl.area.bottom() - pl.area.height() * fy)};
    };
    auto add = [this, &flow, bar](const QString &text, const char *icon, const std::function<Annotation()> &make) {
        auto *b = new QPushButton(icons::get(icon), text, bar);
        flow->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, make] { editPlot([&](Plot &p) { p.annotations.push_back(make()); }); });
    };
    add(u"Droite verticale"_s, "plus", [middle] {
        Annotation a;
        a.kind = Annotation::VLine;
        a.x = middle(0.5, 0.5).first;
        return a;
    });
    add(u"Droite horizontale"_s, "plus", [middle] {
        Annotation a;
        a.kind = Annotation::HLine;
        a.y = middle(0.5, 0.5).second;
        return a;
    });
    add(u"Droite oblique"_s, "slope", [middle] {
        Annotation a;
        a.kind = Annotation::Line;
        std::tie(a.x, a.y) = middle(0.25, 0.3);
        std::tie(a.x2, a.y2) = middle(0.75, 0.7);
        a.dash = Dash::Solid;
        return a;
    });
    add(u"Texte"_s, "plus", [middle] {
        Annotation a;
        a.kind = Annotation::Text;
        std::tie(a.x, a.y) = middle(0.4, 0.5);
        a.text = u"Texte"_s;
        a.color = colors::TEXT;
        a.dash = Dash::Solid;
        return a;
    });
    l->addWidget(bar);
    l->addWidget(weakLabel(u"Sur le graphe, glissez une droite ou un texte pour le déplacer ; une droite oblique s'incline par ses "
                           u"poignées (les ronds). Le cadenas l'empêche de bouger. Clic droit sur le graphe → « Ajouter ici »."_s,
                           m_page));
    for (size_t k = 0; k < plot.annotations.size(); ++k) {
        const Annotation a = plot.annotations[k];
        auto set = [this, k](const std::function<void(Annotation &)> &fn) {
            editPlot([&](Plot &p) {
                if (k < p.annotations.size())
                    fn(p.annotations[k]);
            });
        };
        auto *w = new QFrame(m_page);
        w->setFrameShape(QFrame::StyledPanel);
        auto *rows = new QVBoxLayout(w);
        rows->setContentsMargins(4, 3, 4, 3);
        rows->setSpacing(3);
        // première ligne : couleur (et taille d'un texte), nature, trait, verrou, suppression
        auto *top = new QHBoxLayout;
        top->setSpacing(4);
        auto *color = new ColorButton(w);
        color->setColor(a.color);
        connect(color, &ColorButton::colorPicked, this, [set](Color c) { set([&](Annotation &x) { x.color = c; }); });
        top->addWidget(color);
        if (a.kind == Annotation::Text) {
            auto *size = sizeSpin(a.size, 6, 72, 1, u" px"_s, w);
            size->setToolTip(u"Taille du texte"_s);
            connect(size, &QDoubleSpinBox::valueChanged, this, [set](double v) { set([&](Annotation &x) { x.size = v; }); });
            top->addWidget(size);
        }
        static const char *const kinds[] = {"Droite verticale", "Droite horizontale", "Texte", "Droite oblique"};
        top->addWidget(new QLabel(QString::fromUtf8(kinds[a.kind]), w));
        top->addStretch(1);
        if (a.kind != Annotation::Text) {
            auto *dash = new QComboBox(w);
            dash->setIconSize(theme::px(QSize(30, 12)));
            for (Dash d : ALL_DASHES)
                dash->addItem(dashIcon(d), QString());
            dash->setCurrentIndex(static_cast<int>(a.dash));
            dash->setToolTip(u"Trait"_s);
            connect(dash, &QComboBox::activated, this, [set](int i) { set([&](Annotation &x) { x.dash = ALL_DASHES[i]; }); });
            top->addWidget(dash);
        }
        auto *lock = new QToolButton(w);
        lock->setObjectName(u"annotationLock"_s);
        lock->setCheckable(true);
        lock->setChecked(a.locked);
        lock->setAutoRaise(true);
        lock->setIcon(icons::get(a.locked ? "lock" : "unlock"));
        lock->setToolTip(a.locked ? u"Verrouillée : la souris ne la déplace pas (cliquer pour la libérer)"_s
                                  : u"Libre : elle se déplace à la souris (cliquer pour la verrouiller)"_s);
        connect(lock, &QToolButton::toggled, this, [set](bool on) { set([&](Annotation &x) { x.locked = on; }); });
        top->addWidget(lock);
        auto *del = new QToolButton(w);
        del->setIcon(icons::get("trash"));
        del->setAutoRaise(true);
        del->setToolTip(u"Supprimer"_s);
        connect(del, &QToolButton::clicked, this, [this, k] { editPlot([&](Plot &p) { p.annotations.erase(p.annotations.begin() + static_cast<std::ptrdiff_t>(k)); }); });
        top->addWidget(del);
        rows->addLayout(top);
        // seconde ligne : position, texte, étiquette
        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(4);
        auto num = [&](double value, const std::function<void(Annotation &, double)> &apply) {
            auto *e = new NumberEdit(w);
            e->setStyle(style);
            e->setValue(value);
            connect(e, &NumberEdit::valueEdited, this, [set, apply](double nv) { set([&](Annotation &x) { apply(x, nv); }); });
            return e;
        };
        auto labelEdit = [&] {
            auto *label = new QLineEdit(a.label, w);
            label->setPlaceholderText(u"étiquette"_s);
            connect(label, &QLineEdit::editingFinished, this, [set, label] { set([&](Annotation &x) { x.label = label->text(); }); });
            return label;
        };
        switch (a.kind) {
        case Annotation::VLine:
            bottom->addWidget(new QLabel(u"x ="_s, w));
            bottom->addWidget(num(a.x, [](Annotation &x, double v) { x.x = v; }));
            bottom->addWidget(labelEdit(), 1);
            break;
        case Annotation::HLine:
            bottom->addWidget(new QLabel(u"y ="_s, w));
            bottom->addWidget(num(a.y, [](Annotation &x, double v) { x.y = v; }));
            bottom->addStretch();
            break;
        case Annotation::Text: {
            auto *text = new QLineEdit(a.text, w);
            connect(text, &QLineEdit::editingFinished, this, [set, text] { set([&](Annotation &x) { x.text = text->text(); }); });
            bottom->addWidget(text, 1);
            bottom->addWidget(new QLabel(u"en"_s, w));
            bottom->addWidget(num(a.x, [](Annotation &x, double v) { x.x = v; }));
            bottom->addWidget(num(a.y, [](Annotation &x, double v) { x.y = v; }));
            break;
        }
        case Annotation::Line: {
            // les deux points, puis l'équation
            auto *grid = new QGridLayout;
            grid->setHorizontalSpacing(4);
            grid->setVerticalSpacing(3);
            grid->addWidget(new QLabel(u"de"_s, w), 0, 0, Qt::AlignRight);
            grid->addWidget(num(a.x, [](Annotation &x, double v) { x.x = v; }), 0, 1);
            grid->addWidget(num(a.y, [](Annotation &x, double v) { x.y = v; }), 0, 2);
            grid->addWidget(new QLabel(u"à"_s, w), 1, 0, Qt::AlignRight);
            grid->addWidget(num(a.x2, [](Annotation &x, double v) { x.x2 = v; }), 1, 1);
            grid->addWidget(num(a.y2, [](Annotation &x, double v) { x.y2 = v; }), 1, 2);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 1);
            rows->addLayout(grid);
            QString eq = u"verticale"_s;
            if (a.x2 != a.x) {
                const double slope = (a.y2 - a.y) / (a.x2 - a.x), b = a.y - slope * a.x;
                eq = u"y = %1·x %2 %3"_s.arg(formatSig(slope, 4, style), b < 0 ? u"−"_s : u"+"_s, formatSig(std::abs(b), 4, style));
            }
            bottom->addWidget(weakLabel(eq, w), 1);
            bottom->addWidget(labelEdit(), 1);
            break;
        }
        }
        rows->addLayout(bottom);
        l->addWidget(w);
    }
}
