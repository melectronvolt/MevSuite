#include "app/statspanel.h"

#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/stats.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <numeric>

using namespace Qt::StringLiterals;

std::optional<TraceResultat> trace_histogramme(const Document &doc, QId qid, std::optional<size_t> bins, NumberStyle)
{
    const Grandeur *q = doc.obtient(qid);
    if (!q)
        return std::nullopt;
    const size_t classes = bins ? *bins : stats::classes_par_defaut(q->effectif());
    const auto hist = stats::histogramme(q->values, classes);
    if (!hist || hist->effectifs.empty())
        return std::nullopt;
    const double width = hist->bornes[1] - hist->bornes[0];
    Series s;
    double maxCount = 0;
    for (size_t k = 0; k < hist->effectifs.size(); ++k) {
        s.xs.push_back(std::midpoint(hist->bornes[k], hist->bornes[k + 1]));
        s.ys.push_back(static_cast<double>(hist->effectifs[k]));
        maxCount = std::max(maxCount, s.ys.back());
    }
    s.bars = width;
    s.color = PALETTE[0];
    s.marker = Marker::None;
    TraceResultat h;
    h.view = GraphView::fabrique(0, u"Histogramme"_s);
    Plot &p = h.view.plots[0];
    p.title.text = u"Histogramme de %1"_s.arg(q->name);
    p.y.label = u"Effectif"_s;
    p.y.includeZero = true;
    if (maxCount <= 10)
        p.y.majorStep = 1.0;
    p.y.minorDivisions = 1;
    p.x.gridMajor = false;
    p.legend.visible = false;
    PlotData d;
    d.series.push_back(std::move(s));
    d.xName = q->name;
    d.xUnit = q->unit;
    d.yName = u"Effectif"_s;
    h.data.push_back(std::move(d));
    return h;
}

std::optional<TraceResultat> trace_residus(const Document &doc, const FitSpec &spec)
{
    const auto *x = doc.obtient(spec.x), *y = doc.obtient(spec.y);
    if (!x || !y || !spec.result) return std::nullopt;
    const auto modele = fit::Model::fabrique(spec.kind, x->name, doc.valeurs_parametres());
    if (!modele || modele->parametres().size() != qsizetype(spec.result->params.size())) return std::nullopt;
    auto [xs, ys] = paires_dans_plage(doc, spec.x, spec.y, spec.range);
    PlotData data;
    auto &serie = data.series.emplace_back();
    serie.ys = modele->residus(spec.result->params, xs, ys);
    serie.xs = std::move(xs);
    data.xName = x->name;
    data.xUnit = x->unit;
    data.yName = u"Résidu"_s;
    data.yUnit = y->unit;
    data.overlays.push_back(Overlay{Overlay::HLine});
    auto vue = GraphView::fabrique(0, u"Résidus"_s);
    vue.plots.front().y.includeZero = true;
    vue.plots.front().legend.visible = false;
    return TraceResultat{std::move(vue), {std::move(data)}};
}

void CourbeResultat::definit_trace(std::optional<TraceResultat> plot, const RenderOptions &opts)
{
    m_plot = std::move(plot);
    m_opts = opts;
    update();
}

void CourbeResultat::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    if (!m_plot) {
        p.fillRect(rect(), palette().color(QPalette::Base));
        return;
    }
    RenderOptions o = m_opts;
    o.devicePixelRatio = devicePixelRatioF();
    // agrandissement de l'interface : à l'écran seulement, pas à l'export
    rend_graphe_agrandi(p, m_plot->view, m_plot->data, QSizeF(size()), o, theme::zoom());
}

StatsPanel::StatsPanel(MainWindow &win, QWidget *parent) : QWidget(parent), m_win(win)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);
    auto *barWidget = new QWidget(this);
    auto *bar = new FlowLayout(barWidget, 6);
    bar->addWidget(new QLabel(u"Grandeur"_s, barWidget));
    m_combo = new QComboBox(this);
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    bar->addWidget(m_combo);
    m_autoBins = new QCheckBox(u"classes auto"_s, this);
    m_autoBins->setChecked(true);
    m_autoBins->setToolTip(u"Nombre de classes donné par la règle de Sturges"_s);
    bar->addWidget(m_autoBins);
    m_binCount = new QSpinBox(this);
    m_binCount->setRange(1, 100);
    m_binCount->setSuffix(u" classes"_s);
    m_binCount->setEnabled(false);
    bar->addWidget(m_binCount);
    auto *exp = bouton_outil("export", u"Exporter"_s, u"Exporter l'histogramme"_s, this, false);
    exp->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(exp);
    menu->addAction(icons::obtient("image"), u"Histogramme en PNG…"_s, this, [this] { m_win.exporte_histogramme(exporter::Format::Png, *this); });
    menu->addAction(icons::obtient("vector"), u"Histogramme en SVG…"_s, this, [this] { m_win.exporte_histogramme(exporter::Format::Svg, *this); });
    menu->addAction(icons::obtient("pdf"), u"Histogramme en PDF…"_s, this, [this] { m_win.exporte_histogramme(exporter::Format::Pdf, *this); });
    exp->setMenu(menu);
    bar->addWidget(exp);
    layout->addWidget(barWidget);

    m_summary = new QWidget(this);
    m_grid = new QGridLayout(m_summary);
    m_grid->setContentsMargins(0, 4, 0, 0);
    m_grid->setHorizontalSpacing(18);
    m_grid->setVerticalSpacing(3);
    layout->addWidget(m_summary);
    auto *res = new QHBoxLayout;
    // résultat : graisse et taille normales, comme les valeurs des autres logiciels de la suite
    m_result = new QLabel(this);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    res->addWidget(m_result);
    m_copy = bouton_outil("copy", QString(), u"Copier le résultat"_s, this, false);
    connect(m_copy, &QToolButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_result->text());
        m_win.depot().message(u"Résultat copié"_s);
    });
    res->addWidget(m_copy);
    res->addStretch();
    layout->addLayout(res);
    m_note = etiquette_attenuee(QString(), this);
    layout->addWidget(m_note);
    m_canvas = new CourbeResultat(this);
    theme::definit_taille_min(m_canvas, QSize(240, 180));
    layout->addWidget(m_canvas, 1);

    connect(m_combo, &QComboBox::activated, this, [this](int i) {
        m_quantity = m_combo->itemData(i).toULongLong();
        m_bins.reset();
        rafraichit();
    });
    connect(m_autoBins, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            m_bins.reset();
        else
            m_bins = static_cast<size_t>(m_binCount->value());
        rafraichit();
    });
    connect(m_binCount, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_autoBins->isChecked()) {
            m_bins = static_cast<size_t>(v);
            rafraichit();
        }
    });
    connect(&win.depot(), &Store::espace_travail_change, this, &StatsPanel::rafraichit);
    connect(&win.depot(), &Store::affichage_change, this, &StatsPanel::rafraichit);
    rafraichit();
}

void StatsPanel::definit_grandeur(QId q)
{
    m_quantity = q;
    m_bins.reset();
    rafraichit();
}

void StatsPanel::rafraichit()
{
    Store &store = m_win.depot();
    const Document &doc = store.ws.doc;
    const NumberStyle style = store.style();
    if (!m_quantity || !doc.obtient(*m_quantity)) {
        m_quantity.reset();
        for (const auto &q : doc.quantities) {
            if (q.effectif() > 1) {
                m_quantity = q.id;
                break;
            }
        }
        if (!m_quantity && !doc.quantities.empty())
            m_quantity = doc.quantities.front().id;
    }
    {
        QSignalBlocker b(m_combo);
        m_combo->clear();
        for (const auto &q : doc.quantities)
            m_combo->addItem(q.name, QVariant::fromValue<qulonglong>(q.id));
        if (m_quantity)
            m_combo->setCurrentIndex(m_combo->findData(QVariant::fromValue<qulonglong>(*m_quantity)));
    }
    while (QLayoutItem *it = m_grid->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    m_result->clear();
    m_copy->setVisible(false);
    const Grandeur *q = m_quantity ? doc.obtient(*m_quantity) : nullptr;
    if (!q) {
        m_note->setText(u"Aucune grandeur."_s);
        m_canvas->definit_trace(std::nullopt, {});
        return;
    }
    const auto bilan = stats::resume(q->values);
    const size_t classes = m_bins.value_or(stats::classes_par_defaut(bilan ? bilan->effectif : 0));
    {
        QSignalBlocker b1(m_autoBins), b2(m_binCount);
        m_autoBins->setChecked(!m_bins);
        m_binCount->setEnabled(m_bins.has_value());
        m_binCount->setValue(static_cast<int>(classes));
    }
    const QString unit = unite_lisible(q->unit);
    auto withUnit = [&](double x, int sig) { return unit.isEmpty() ? format_significatif(x, sig, style) : format_significatif(x, sig, style) + u" "_s + unit; };
    if (!bilan) {
        m_note->setText(u"Cette grandeur n'a aucune valeur."_s);
    } else {
        const std::vector<std::pair<QString, QString>> rows = {
            {u"Nombre de valeurs n"_s, QString::number(bilan->effectif)},
            {u"Minimum"_s, withUnit(bilan->minimum, 6)},
            {u"Maximum"_s, withUnit(bilan->maximum, 6)},
            {u"Moyenne"_s, withUnit(bilan->moyenne, 6)},
            {u"Écart-type σ (division par n)"_s, withUnit(bilan->ecartTypePopulation, 4)},
            {u"Écart-type corrigé s (division par n − 1)"_s, withUnit(bilan->ecartTypeEchantillon, 4)},
            {u"Incertitude-type de type A : u = s/√n"_s, withUnit(bilan->uA, 4)},
        };
        const QFont mono = theme::police_fixe();
        for (size_t r = 0; r < rows.size(); ++r) {
            // libellé à la ligne plutôt que rogné quand le panneau est étroit
            auto *name = new QLabel(rows[r].first, m_summary);
            name->setWordWrap(true);
            m_grid->addWidget(name, static_cast<int>(r), 0);
            auto *v = new QLabel(rows[r].second, m_summary);
            v->setFont(mono);
            v->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_grid->addWidget(v, static_cast<int>(r), 1);
        }
        m_grid->setColumnStretch(2, 1);
        if (bilan->effectif >= 2) {
            m_result->setText(u"%1 = %2"_s.arg(q->name, bilan->resultat(style).en_texte(q->unit)));
            m_copy->setVisible(true);
            m_note->setText(u"L’incertitude de la moyenne s’interprète pour des mesures répétées dans les mêmes conditions. La moyenne est arrondie à la même décimale que son incertitude."_s);
        } else {
            m_note->setText(u"Il faut au moins deux valeurs pour estimer une incertitude."_s);
        }
    }
    RenderOptions o;
    o.style = style;
    o.theme = theme::est_sombre() && store.prefs.darkPlots ? PlotTheme::theme_sombre() : PlotTheme::clair();
    m_canvas->definit_trace(trace_histogramme(doc, q->id, classes, style), o);
}
