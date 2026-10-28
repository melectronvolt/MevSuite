#include "app/calculationspanel.h"

#include "app/dialogs.h"
#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/expr.h"

#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Curseur d'un paramètre : positions entières converties en valeurs de min à max.
struct SliderMap {
    double min, max, step;
    int steps() const
    {
        if (step > 0.0) {
            const double n = (max - min) / step;
            if (n >= 1.0 && n <= 20000.0)
                return static_cast<int>(std::lround(n));
        }
        return 1000;
    }
    double value(int k) const
    {
        const int n = steps();
        if (step > 0.0 && std::abs((max - min) / step - n) < 1e-6)
            return min + k * step;
        return min + (max - min) * k / n;
    }
    int position(double v) const
    {
        const int n = steps();
        if (max == min)
            return 0;
        return std::clamp(static_cast<int>(std::lround((v - min) / (max - min) * n)), 0, n);
    }
};

QLabel *statusIcon(QWidget *parent)
{
    auto *l = new QLabel(parent);
    theme::setFixedSize(l, QSize(18, 18));
    return l;
}

void setStatus(QLabel *l, const QString &error, const QColor &ok)
{
    const char *name = error.isEmpty() ? "check" : "warning";
    QPixmap pm = icons::get(name).pixmap(theme::px(QSize(16, 16)));
    QPixmap tinted(pm.size());
    tinted.setDevicePixelRatio(pm.devicePixelRatio());
    tinted.fill(Qt::transparent);
    QPainter p(&tinted);
    p.drawPixmap(0, 0, pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(tinted.rect(), error.isEmpty() ? ok : theme::errorColor());
    p.end();
    l->setPixmap(tinted);
    l->setToolTip(error.isEmpty() ? u"Formule valide"_s : error);
}

} // namespace

CalculationsPanel::CalculationsPanel(MainWindow &win, QWidget *parent) : QWidget(parent), m_win(win)
{
    theme::setMinimumWidth(this, 270);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new PanelScroll(this);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget(scroll);
    scroll->setWidget(content);
    // fond transparent : celui du panneau, peint par sa fenêtre ancrée
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // paramètres
    auto *ph = new QHBoxLayout;
    ph->addWidget(heading(u"Paramètres"_s, content));
    ph->addStretch();
    auto *addParam = toolButton("sliders", u"Paramètre"_s, u"Constante réglable par un curseur (m, g, k…)"_s, content);
    connect(addParam, &QToolButton::clicked, this, [this] { dialogs::newParameter(m_win); });
    ph->addWidget(addParam);
    layout->addLayout(ph);
    m_params = new QWidget(content);
    m_paramsLayout = new QVBoxLayout(m_params);
    m_paramsLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_params);

    // grandeurs calculées
    layout->addSpacing(8);
    layout->addWidget(heading(u"Grandeurs calculées"_s, content));
    layout->addWidget(weakLabel(u"Une ligne par grandeur : nom = expression. Entrée pour valider."_s, content));
    auto *nh = new QHBoxLayout;
    m_newFormula = new QLineEdit(content);
    m_newFormula->setPlaceholderText(u"Ec = 0.5*m*v^2"_s);
    QFont mono(u"DejaVu Sans Mono"_s);
    mono.setStyleHint(QFont::Monospace);
    m_newFormula->setFont(mono);
    nh->addWidget(m_newFormula, 1);
    auto *add = new QPushButton(icons::get("plus"), u"Ajouter"_s, content);
    nh->addWidget(add);
    layout->addLayout(nh);
    m_newError = new QLabel(content);
    m_newError->setWordWrap(true);
    m_newError->hide();
    layout->addWidget(m_newError);
    connect(add, &QPushButton::clicked, this, &CalculationsPanel::addFormula);
    connect(m_newFormula, &QLineEdit::returnPressed, this, &CalculationsPanel::addFormula);
    m_formulas = new QWidget(content);
    m_formulasLayout = new QVBoxLayout(m_formulas);
    m_formulasLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_formulas);

    // aide sur les fonctions
    layout->addSpacing(8);
    auto *helpButton = new QToolButton(content);
    helpButton->setText(u"Fonctions disponibles"_s);
    helpButton->setIcon(icons::get("help"));
    helpButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    helpButton->setCheckable(true);
    helpButton->setAutoRaise(true);
    layout->addWidget(helpButton);
    QString html = u"<table cellspacing='0' cellpadding='3'>"_s;
    for (const auto &f : expr::functions())
        html += u"<tr><td><code>%1</code></td><td>%2</td></tr>"_s.arg(QString::fromUtf16(f.signature).toHtmlEscaped(), QString::fromUtf16(f.description).toHtmlEscaped());
    html += u"<tr><td><code>pi</code></td><td>π = 3,14159…</td></tr><tr><td><code>^ ou **</code></td><td>puissance : v^2</td></tr></table>"
            "<p>Le séparateur décimal est le point : 0.5*m*v^2. Les angles sont en radians.</p>"_s;
    auto *helpText = new QLabel(html, content);
    helpText->setWordWrap(true);
    helpText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    helpText->hide();
    layout->addWidget(helpText);
    connect(helpButton, &QToolButton::toggled, helpText, &QWidget::setVisible);
    layout->addStretch();

    connect(&m_win.store(), &Store::workspaceChanged, this, &CalculationsPanel::refresh);
    connect(&m_win.store(), &Store::displayChanged, this, &CalculationsPanel::rebuild);
    rebuild();
}

QString CalculationsPanel::signature() const
{
    QString s;
    const Document &d = m_win.store().ws.doc;
    for (const auto &p : d.parameters)
        s += u"p%1;"_s.arg(p.id);
    for (const auto &q : d.quantities) {
        if (q.isCalculated())
            s += u"q%1;"_s.arg(q.id);
    }
    return s;
}

void CalculationsPanel::refresh()
{
    const QString sig = signature();
    if (sig != m_signature)
        rebuild();
    else
        for (const auto &f : m_sync)
            f();
}

void CalculationsPanel::rebuild()
{
    m_signature = signature();
    m_sync.clear();
    for (QWidget *w : {m_params, m_formulas}) {
        QLayout *l = w->layout();
        while (QLayoutItem *item = l->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }
    Store &store = m_win.store();
    const Document &doc = store.ws.doc;
    const NumberStyle style = store.style();

    if (doc.parameters.empty())
        m_paramsLayout->addWidget(weakLabel(u"Aucun paramètre. Un paramètre est une constante (masse, g, k…) utilisable dans les formules et réglable par curseur."_s, m_params));
    for (const auto &p0 : doc.parameters) {
        const ParamId id = p0.id;
        auto *frame = new QFrame(m_params);
        frame->setFrameShape(QFrame::StyledPanel);
        auto *row = new QHBoxLayout(frame);
        row->setContentsMargins(6, 4, 6, 4);
        auto *name = new QLabel(p0.name, frame);
        QFont bf = name->font();
        bf.setBold(true);
        name->setFont(bf);
        row->addWidget(name);
        const SliderMap map{std::min(p0.min, p0.max), std::max(p0.min, p0.max), p0.step};
        auto *slider = new QSlider(Qt::Horizontal, frame);
        slider->setRange(0, map.steps());
        slider->setValue(map.position(p0.value));
        theme::setMinimumWidth(slider, 90);
        row->addWidget(slider, 1);
        auto *edit = new NumberEdit(frame);
        edit->setStyle(style);
        edit->setValue(p0.value);
        if (p0.step > 0)
            edit->setStep(p0.step);
        theme::setMaximumWidth(edit, 100);
        row->addWidget(edit);
        auto *unit = new QLabel(prettyUnit(p0.unit), frame);
        row->addWidget(unit);
        auto mapOf = [&store, id] {
            const Parameter *p = store.ws.doc.param(id);
            return p ? SliderMap{std::min(p->min, p->max), std::max(p->min, p->max), p->step} : SliderMap{0, 1, 0};
        };
        connect(slider, &QSlider::valueChanged, this, [&store, id, mapOf](int k) {
            store.edit([&](Workspace &ws) { ws.doc.setParamValue(id, mapOf().value(k)); });
        });
        connect(edit, &NumberEdit::valueEdited, this, [&store, id](double v) { store.edit([&](Workspace &ws) { ws.doc.setParamValue(id, v); }); });
        // réglages : nom, unité, bornes, pas
        auto *gear = new QToolButton(frame);
        gear->setIcon(icons::get("gear"));
        gear->setAutoRaise(true);
        gear->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(gear);
        auto *form = new QWidget(menu);
        auto *fl = new QFormLayout(form);
        auto *nameEdit = new QLineEdit(p0.name, form);
        auto *unitEdit = new QLineEdit(p0.unit, form);
        auto *minEdit = new NumberEdit(form);
        auto *maxEdit = new NumberEdit(form);
        auto *stepEdit = new NumberEdit(form);
        for (auto *e : {minEdit, maxEdit, stepEdit})
            e->setStyle(style);
        minEdit->setValue(p0.min);
        maxEdit->setValue(p0.max);
        stepEdit->setValue(p0.step);
        fl->addRow(u"Nom"_s, nameEdit);
        fl->addRow(u"Unité"_s, unitEdit);
        fl->addRow(u"Minimum"_s, minEdit);
        fl->addRow(u"Maximum"_s, maxEdit);
        fl->addRow(u"Pas"_s, stepEdit);
        auto *wa = new QWidgetAction(menu);
        wa->setDefaultWidget(form);
        menu->addAction(wa);
        menu->addSeparator();
        menu->addAction(icons::get("trash"), u"Supprimer le paramètre"_s, this, [&store, id] { store.edit([&](Workspace &ws) { ws.doc.removeParameter(id); }); });
        gear->setMenu(menu);
        row->addWidget(gear);
        connect(nameEdit, &QLineEdit::editingFinished, this, [&store, id, nameEdit] {
            const Parameter *p = store.ws.doc.param(id);
            if (!p || nameEdit->text().trimmed() == p->name)
                return;
            QString err;
            store.edit([&](Workspace &ws) { err = ws.doc.renameParameter(id, nameEdit->text()).error; });
            if (!err.isEmpty())
                store.message(err, icons::Status::Error);
        });
        connect(unitEdit, &QLineEdit::editingFinished, this, [&store, id, unitEdit] {
            store.edit([&](Workspace &ws) {
                if (Parameter *p = ws.doc.paramMut(id))
                    p->unit = unitEdit->text().trimmed();
            });
        });
        auto bound = [&store, id](int which) {
            return [&store, id, which](double v) {
                store.edit([&](Workspace &ws) {
                    if (Parameter *p = ws.doc.paramMut(id)) {
                        if (which == 0)
                            p->min = v;
                        else if (which == 1)
                            p->max = v;
                        else
                            p->step = std::max(v, 0.0);
                    }
                });
            };
        };
        connect(minEdit, &NumberEdit::valueEdited, this, bound(0));
        connect(maxEdit, &NumberEdit::valueEdited, this, bound(1));
        connect(stepEdit, &NumberEdit::valueEdited, this, bound(2));
        m_sync.push_back([&store, id, slider, edit, name, unit, mapOf, style] {
            const Parameter *p = store.ws.doc.param(id);
            if (!p)
                return;
            QSignalBlocker b1(slider), b2(edit);
            const SliderMap m = mapOf();
            if (!slider->isSliderDown()) {
                slider->setRange(0, m.steps());
                slider->setValue(m.position(p->value));
            }
            edit->setStep(p->step > 0 ? std::optional<double>(p->step) : std::nullopt);
            edit->setValue(p->value);
            name->setText(p->name);
            unit->setText(prettyUnit(p->unit));
        });
        m_paramsLayout->addWidget(frame);
    }

    std::vector<const Quantity *> calc;
    for (const auto &q : doc.quantities) {
        if (q.isCalculated())
            calc.push_back(&q);
    }
    if (calc.empty())
        m_formulasLayout->addWidget(weakLabel(u"Aucune grandeur calculée."_s, m_formulas));
    QFont mono(u"DejaVu Sans Mono"_s);
    mono.setStyleHint(QFont::Monospace);
    for (const Quantity *q : calc) {
        const QId id = q->id;
        auto *rowW = new QWidget(m_formulas);
        auto *grid = new QGridLayout(rowW);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        const QColor color = theme::kindColor(true, doc.uncertaintyOwner(id) != nullptr);
        auto *name = new QLabel(q->name + u" ="_s, rowW);
        name->setFont(mono);
        QPalette pal = name->palette();
        pal.setColor(QPalette::WindowText, color);
        name->setPalette(pal);
        grid->addWidget(name, 0, 0);
        auto *formula = new QLineEdit(*q->formula, rowW);
        formula->setFont(mono);
        grid->addWidget(formula, 0, 1);
        auto *unit = new QLineEdit(q->unit, rowW);
        unit->setPlaceholderText(u"unité"_s);
        theme::setMaximumWidth(unit, 70);
        grid->addWidget(unit, 0, 2);
        auto *status = statusIcon(rowW);
        setStatus(status, q->error, color);
        grid->addWidget(status, 0, 3);
        auto *del = new QToolButton(rowW);
        del->setIcon(icons::get("trash"));
        del->setAutoRaise(true);
        del->setToolTip(u"Supprimer…"_s);
        grid->addWidget(del, 0, 4);
        auto *error = new QLabel(q->error, rowW);
        error->setWordWrap(true);
        QPalette ep = error->palette();
        ep.setColor(QPalette::WindowText, theme::errorColor());
        error->setPalette(ep);
        error->setVisible(!q->error.isEmpty());
        grid->addWidget(error, 1, 1, 1, 4);
        grid->setColumnStretch(1, 1);
        connect(formula, &QLineEdit::editingFinished, this, [&store, id, formula] {
            // virgule décimale acceptée, comme dans les boîtes de dialogue
            const QString fx = dialogs::formulaText(formula->text());
            const Quantity *qq = store.ws.doc.get(id);
            if (qq && qq->formula && fx != *qq->formula)
                store.edit([&](Workspace &ws) { ws.doc.setFormula(id, fx); });
        });
        connect(unit, &QLineEdit::editingFinished, this, [&store, id, unit] {
            const Quantity *qq = store.ws.doc.get(id);
            if (qq && unit->text().trimmed() != qq->unit)
                store.edit([&](Workspace &ws) { ws.doc.setUnit(id, unit->text()); });
        });
        connect(del, &QToolButton::clicked, this, [this, id] { dialogs::confirmDelete(m_win, id); });
        m_sync.push_back([&store, id, formula, unit, status, error, color] {
            const Quantity *qq = store.ws.doc.get(id);
            if (!qq || !qq->formula)
                return;
            // formule changée ailleurs (renommage, annulation), sauf pendant la saisie
            if (!formula->hasFocus() && formula->text() != *qq->formula)
                formula->setText(*qq->formula);
            if (!unit->hasFocus() && unit->text() != qq->unit)
                unit->setText(qq->unit);
            setStatus(status, qq->error, color);
            error->setText(qq->error);
            error->setVisible(!qq->error.isEmpty());
        });
        m_formulasLayout->addWidget(rowW);
    }
}

void CalculationsPanel::addFormula()
{
    const QString text = m_newFormula->text().trimmed();
    if (text.isEmpty())
        return;
    Store &store = m_win.store();
    const auto def = expr::splitDefinition(text);
    QString err;
    if (!def) {
        err = u"écrire « nom = expression », par exemple v = deriv(x, t)"_s;
    } else {
        store.edit([&](Workspace &ws) {
            auto id = ws.doc.addCalculated(def->first, QString(), def->second);
            if (!id) {
                err = id.error;
                return;
            }
            // la nouvelle grandeur apparaît dans le premier tableur
            if (!ws.tables.empty())
                ws.tables.front().columns.push_back(*id);
        });
    }
    if (err.isEmpty()) {
        m_newFormula->clear();
        m_newError->hide();
    } else {
        m_newError->setText(err);
        QPalette p = m_newError->palette();
        p.setColor(QPalette::WindowText, theme::errorColor());
        m_newError->setPalette(p);
        m_newError->show();
    }
}
