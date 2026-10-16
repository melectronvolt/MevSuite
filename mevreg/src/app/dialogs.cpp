#include "app/dialogs.h"

#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/scriptpanel.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/csv.h"
#include "core/fft.h"
#include "core/numeric.h"
#include "core/pyexport.h"
#include "core/series.h"
#include "core/titration.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/// Unité d'un quotient a/b, écrite simplement.
QString unitRatio(const QString &a, const QString &b)
{
    const QString x = a.trimmed(), y = b.trimmed();
    if (x.isEmpty() && y.isEmpty())
        return {};
    if (y.isEmpty())
        return x;
    if (x.isEmpty())
        return y + u"^-1"_s;
    return x + u'/' + y;
}

QString unitProduct(const QString &a, const QString &b)
{
    const QString x = a.trimmed(), y = b.trimmed();
    if (x.isEmpty())
        return y;
    if (y.isEmpty())
        return x;
    return x + u'.' + y;
}

/// Virgule décimale saisie dans une formule (« 0,05 ») : remplacée par le point, sans toucher
/// aux virgules qui séparent les arguments d'une fonction.
QString formulaText(const QString &s)
{
    static const QRegularExpression re(u"(\\d),(\\d)"_s);
    QString t = s.trimmed();
    t.replace(re, u"\\1.\\2"_s);
    return t;
}

QLabel *errorLabel(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setWordWrap(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, theme::errorColor());
    l->setPalette(p);
    l->hide();
    return l;
}

void showError(QLabel *l, const QString &text)
{
    l->setText(text.isEmpty() ? QString() : u"⚠ "_s + text);
    l->setVisible(!text.isEmpty());
}

QLabel *warnLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, theme::warnColor());
    l->setPalette(p);
    return l;
}

QLabel *strongLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont f = l->font();
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    return l;
}

/// Boîte de dialogue : contenu, message d'erreur, boutons.
struct Box {
    QDialog *dlg;
    QVBoxLayout *layout;
    QLabel *error;
    QDialogButtonBox *buttons;
    QPushButton *ok = nullptr;

    Box(MainWindow &win, const QString &title, const QString &okText, int minWidth = 380)
    {
        dlg = new QDialog(&win);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(title);
        theme::setMinimumWidth(dlg, minWidth);
        layout = new QVBoxLayout(dlg);
        layout->setSpacing(8);
        error = errorLabel(dlg);
        buttons = new QDialogButtonBox(dlg);
        if (!okText.isEmpty()) {
            ok = buttons->addButton(okText, QDialogButtonBox::AcceptRole);
            ok->setDefault(true);
        }
        QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    }
    QFormLayout *form()
    {
        auto *f = new QFormLayout;
        f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addLayout(f);
        return f;
    }
    void cancel(const QString &text = u"Annuler"_s) { buttons->addButton(text, QDialogButtonBox::RejectRole); }
    /// `apply` renvoie un message d'erreur, vide si l'opération a réussi (la boîte se ferme alors).
    void onOk(std::function<QString()> apply)
    {
        QLabel *err = error;
        QDialog *d = dlg;
        QObject::connect(ok, &QPushButton::clicked, dlg, [apply, err, d] {
            const QString e = apply();
            showError(err, e);
            if (e.isEmpty())
                d->accept();
        });
    }
    void show()
    {
        layout->addWidget(error);
        layout->addWidget(buttons);
        dlg->open();
    }
};

/// Champs d'une suite de valeurs : « de a à b par pas de h » ou « en n valeurs », avec aperçu.
class FillFields : public QWidget {
public:
    FillFields(NumberStyle style, QWidget *parent) : QWidget(parent), m_style(style)
    {
        auto *g = new QGridLayout(this);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(8);
        m_start = new NumberEdit(this);
        m_end = new NumberEdit(this);
        m_step = new NumberEdit(this);
        for (NumberEdit *e : {m_start, m_end, m_step})
            e->setStyle(style);
        m_start->setValue(0);
        m_end->setValue(10);
        m_step->setValue(1);
        m_count = new QSpinBox(this);
        m_count->setRange(1, static_cast<int>(series::MAX_VALUES));
        m_count->setValue(11);
        m_byStep = new QRadioButton(u"par pas de"_s, this);
        m_byCount = new QRadioButton(u"en"_s, this);
        m_byStep->setChecked(true);
        g->addWidget(new QLabel(u"de"_s, this), 0, 0, Qt::AlignRight);
        auto *range = new QHBoxLayout;
        range->addWidget(m_start);
        range->addWidget(new QLabel(u"à"_s, this));
        range->addWidget(m_end);
        range->addStretch();
        g->addLayout(range, 0, 1);
        g->addWidget(m_byStep, 1, 0);
        auto *sr = new QHBoxLayout;
        sr->addWidget(m_step);
        sr->addStretch();
        g->addLayout(sr, 1, 1);
        g->addWidget(m_byCount, 2, 0);
        auto *cr = new QHBoxLayout;
        cr->addWidget(m_count);
        cr->addWidget(new QLabel(u"valeurs"_s, this));
        cr->addStretch();
        g->addLayout(cr, 2, 1);
        m_preview = weakLabel(QString(), this);
        g->addWidget(m_preview, 3, 0, 1, 2);
        auto update = [this] { refresh(); };
        for (NumberEdit *e : {m_start, m_end, m_step})
            connect(e, &NumberEdit::valueEdited, this, update);
        connect(m_count, &QSpinBox::valueChanged, this, update);
        connect(m_byStep, &QRadioButton::toggled, this, update);
        refresh();
    }

    series::Result values() const
    {
        if (m_byStep->isChecked())
            return series::byStep(m_start->value(), m_end->value(), m_step->value());
        return series::byCount(m_start->value(), m_end->value(), static_cast<size_t>(m_count->value()));
    }
    std::function<void()> changed;

private:
    void refresh()
    {
        m_step->setEnabled(m_byStep->isChecked());
        m_count->setEnabled(!m_byStep->isChecked());
        const series::Result r = values();
        QPalette p = m_preview->palette();
        if (!r.ok()) {
            m_preview->setText(r.error);
            p.setColor(QPalette::WindowText, theme::errorColor());
        } else {
            const auto &v = r.values;
            auto f = [&](double x) { return formatSig(x, 6, m_style); };
            QString shown;
            if (v.size() <= 5) {
                QStringList l;
                for (double x : v)
                    l << f(x);
                shown = l.join(u" ; "_s);
            } else {
                shown = u"%1 ; %2 ; %3 ; … ; %4"_s.arg(f(v[0]), f(v[1]), f(v[2]), f(v.back()));
            }
            m_preview->setText(u"%1 valeur%2 : %3"_s.arg(v.size()).arg(v.size() > 1 ? u"s"_s : QString(), shown));
            p.setColor(QPalette::WindowText, theme::weakColor());
        }
        m_preview->setPalette(p);
        if (changed)
            changed();
    }

    NumberStyle m_style;
    NumberEdit *m_start, *m_end, *m_step;
    QSpinBox *m_count;
    QRadioButton *m_byStep, *m_byCount;
    QLabel *m_preview;
};

/// Affiche une grandeur dans un tableur : celui donné, sinon le premier.
void showInTable(Workspace &ws, QId q, std::optional<ViewId> table = std::nullopt)
{
    TableView *t = table ? ws.tableMut(*table) : nullptr;
    if (!t && !ws.tables.empty())
        t = &ws.tables.front();
    if (t && std::find(t->columns.begin(), t->columns.end(), q) == t->columns.end())
        t->columns.push_back(q);
}

/// Grandeurs proposées par défaut : les deux premières colonnes du premier tableur.
std::pair<std::optional<QId>, std::optional<QId>> defaultPair(const Workspace &ws)
{
    std::vector<QId> ids;
    if (!ws.tables.empty())
        ids = ws.tables.front().columns;
    for (const auto &q : ws.doc.quantities)
        if (std::find(ids.begin(), ids.end(), q.id) == ids.end())
            ids.push_back(q.id);
    // les incertitudes ne sont pas des grandeurs qu'on dérive ou qu'on trace
    std::erase_if(ids, [&](QId id) { return !ws.doc.get(id) || ws.doc.uncertaintyOwner(id); });
    std::optional<QId> x, y;
    if (!ids.empty())
        x = ids[0];
    if (ids.size() > 1)
        y = ids[1];
    return {x, y};
}

QComboBox *graphCombo(const Workspace &ws, bool allowNew, QWidget *parent)
{
    auto *c = new QComboBox(parent);
    if (allowNew)
        c->addItem(u"nouveau graphe"_s, QVariant::fromValue<qulonglong>(0));
    for (const auto &g : ws.graphs)
        c->addItem(g.name, QVariant::fromValue<qulonglong>(g.id));
    return c;
}

void warnScripts(MainWindow &win, const QString &old)
{
    for (const QString &s : win.store().ws.scriptsUsing(old))
        win.store().message(u"Le script « %1 » utilise encore l'ancien nom « %2 » : pensez à le modifier"_s.arg(s, old), icons::Status::Warning);
}

QLineEdit *unitEdit(const QString &unit, QFormLayout *form, QWidget *parent)
{
    auto *e = new QLineEdit(unit, parent);
    e->setPlaceholderText(u"m/s, m.s-1, mol.L-1…"_s);
    auto *preview = weakLabel(QString(), parent);
    auto upd = [e, preview] {
        const QString t = e->text().trimmed();
        preview->setText(t.isEmpty() ? QString() : u"Affichage : "_s + prettyUnit(t));
        preview->setVisible(!t.isEmpty());
    };
    QObject::connect(e, &QLineEdit::textChanged, preview, upd);
    upd();
    form->addRow(u"Unité"_s, e);
    form->addRow(QString(), preview);
    return e;
}

QLineEdit *formulaEdit(const QString &text, const QString &placeholder, QWidget *parent)
{
    auto *e = new QLineEdit(text, parent);
    e->setFont(theme::fixedFont());
    e->setPlaceholderText(placeholder);
    theme::setMinimumWidth(e, 260);
    return e;
}

} // namespace

namespace dialogs {

void newQuantity(MainWindow &win, bool calculated, std::optional<ViewId> table)
{
    Store &store = win.store();
    Box box(win, calculated ? u"Nouvelle grandeur calculée"_s : u"Nouvelle grandeur"_s, u"Créer"_s);
    QFormLayout *f = box.form();
    auto *name = new QLineEdit(box.dlg);
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = unitEdit(QString(), f, box.dlg);
    QLineEdit *formula = nullptr;
    if (calculated) {
        formula = formulaEdit(QString(), u"0.5*m*v^2"_s, box.dlg);
        f->addRow(u"Formule"_s, formula);
        box.layout->addWidget(weakLabel(u"Opérations + − * / ^, fonctions sqrt, exp, ln, log, sin, cos… ; deriv(y, x) et integ(y, x) pour dériver et intégrer. Le point décimal est toujours le point."_s, box.dlg));
    }
    FillFields *fill = nullptr;
    QCheckBox *withFill = nullptr;
    if (!calculated) {
        withFill = new QCheckBox(u"Remplir avec une suite de valeurs"_s, box.dlg);
        box.layout->addWidget(withFill);
        fill = new FillFields(store.style(), box.dlg);
        fill->setVisible(false);
        fill->setContentsMargins(theme::px(18), 0, 0, 0);
        box.layout->addWidget(fill);
        QDialog *d = box.dlg;
        QObject::connect(withFill, &QCheckBox::toggled, fill, [fill, d](bool on) {
            fill->setVisible(on);
            d->adjustSize();
        });
    }
    box.cancel();
    box.onOk([&win, calculated, table, name, unit, formula, fill, withFill]() -> QString {
        Store &st = win.store();
        const QString n = name->text().trimmed(), u = unit->text().trimmed();
        if (const QString e = st.ws.doc.checkName(n); !e.isEmpty())
            return e;
        if (calculated) {
            const QString fx = formulaText(formula->text());
            if (fx.isEmpty())
                return u"Saisissez la formule de la grandeur."_s;
            // essai sur une copie : pas de grandeur créée si la formule est fausse
            Document test = st.ws.doc;
            auto r = test.addCalculated(n, u, fx);
            if (!r)
                return r.error;
            if (const Quantity *q = test.get(*r); q && !q->error.isEmpty())
                return q->error;
            st.edit([&](Workspace &ws) {
                if (auto id = ws.doc.addCalculated(n, u, fx))
                    showInTable(ws, *id, table);
            });
            return {};
        }
        std::vector<double> values;
        if (withFill->isChecked()) {
            const series::Result r = fill->values();
            if (!r.ok())
                return r.error;
            values = r.values;
        }
        QString error;
        st.edit([&](Workspace &ws) {
            auto id = ws.doc.addExperimental(n, u, std::move(values));
            if (!id)
                error = id.error;
            else
                showInTable(ws, *id, table);
        });
        return error;
    });
    box.show();
}

void newParameter(MainWindow &win)
{
    Box box(win, u"Nouveau paramètre"_s, u"Créer"_s);
    QFormLayout *f = box.form();
    auto *name = new QLineEdit(win.store().ws.doc.uniqueName(u"k"_s), box.dlg);
    name->selectAll();
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = unitEdit(QString(), f, box.dlg);
    auto *value = new NumberEdit(box.dlg);
    value->setStyle(win.store().style());
    value->setValue(1.0);
    f->addRow(u"Valeur"_s, value);
    box.layout->addWidget(weakLabel(u"Le paramètre se règle ensuite au curseur dans la fenêtre Calculs ; les formules et les scripts qui l'utilisent suivent."_s, box.dlg));
    box.cancel();
    box.onOk([&win, name, unit, value]() -> QString {
        QString error;
        win.store().edit([&](Workspace &ws) {
            auto r = ws.doc.addParameter(name->text().trimmed(), unit->text().trimmed(), value->value());
            if (!r)
                error = r.error;
        });
        if (error.isEmpty())
            win.openTab(TabKey{TabKey::Calculations});
        return error;
    });
    box.show();
}

void newUncertainty(MainWindow &win, QId of)
{
    Store &store = win.store();
    const Quantity *q = store.ws.doc.get(of);
    if (!q)
        return;
    const QString name = q->name;
    Box box(win, u"Incertitude"_s, u"Créer"_s);
    box.layout->addWidget(new QLabel(u"Incertitude-type u(%1), rattachée à « %1 » :"_s.arg(name), box.dlg));
    if (q->uncertainty) {
        box.layout->addWidget(warnLabel(u"« %1 » est actuellement l'incertitude de %2 : la nouvelle la remplacera."_s.arg(store.ws.doc.name(*q->uncertainty), name), box.dlg));
    }
    auto *row = new QHBoxLayout;
    auto *lbl = new QLabel(u"u_%1 ="_s.arg(name), box.dlg);
    lbl->setFont(theme::fixedFont());
    row->addWidget(lbl);
    auto *formula = formulaEdit(QString(), u"0.05   ou   0.02*%1 + 0.01"_s.arg(name), box.dlg);
    row->addWidget(formula, 1);
    box.layout->addLayout(row);
    box.layout->addWidget(weakLabel(u"Une constante s'applique à toutes les valeurs ; une formule peut dépendre de la grandeur "
                                    "(par exemple 0.02*%1 + 0.01 pour « 2 % + 1 digit »). Pour une graduation de pas Δ : Δ/sqrt(12) ; "
                                    "pour une tolérance ±a du constructeur : a/sqrt(3)."_s
                                        .arg(name),
                                    box.dlg));
    auto *bars = new QCheckBox(u"Afficher les barres d'incertitude sur les graphes"_s, box.dlg);
    bars->setChecked(true);
    box.layout->addWidget(bars);
    box.cancel();
    box.onOk([&win, of, formula, bars]() -> QString {
        Store &st = win.store();
        const QString fx = formulaText(formula->text());
        if (fx.isEmpty())
            return u"Saisissez la valeur ou la formule de l'incertitude."_s;
        Document test = st.ws.doc;
        auto r = test.createUncertainty(of, fx);
        if (!r)
            return r.error;
        if (const Quantity *u = test.get(*r); u && !u->error.isEmpty())
            return u->error;
        const bool showBars = bars->isChecked();
        st.edit([&](Workspace &ws) {
            auto id = ws.doc.createUncertainty(of, fx);
            if (!id)
                return;
            // la colonne u(x) prend place à droite de x dans les tableurs qui l'affichent
            bool shown = false;
            for (auto &t : ws.tables) {
                auto it = std::find(t.columns.begin(), t.columns.end(), of);
                if (it != t.columns.end()) {
                    t.columns.insert(it + 1, *id);
                    shown = true;
                }
            }
            if (!shown)
                showInTable(ws, *id);
            if (showBars) {
                for (auto &g : ws.graphs) {
                    for (auto &p : g.plots) {
                        for (auto &c : p.curves) {
                            if (c.y == of && c.errors == ErrorBars::None)
                                c.errors = ErrorBars::Vertical;
                            else if (c.x == of && (c.errors == ErrorBars::None || c.errors == ErrorBars::Vertical))
                                c.errors = ErrorBars::Both;
                        }
                    }
                }
            }
        });
        return {};
    });
    box.show();
    formula->setFocus();
}

void fill(MainWindow &win, QId qid)
{
    Store &store = win.store();
    const Quantity *q = store.ws.doc.get(qid);
    if (!q || q->isCalculated())
        return;
    Box box(win, u"Suite de valeurs"_s, u"Remplir"_s);
    box.layout->addWidget(strongLabel(u"Remplir « %1 »"_s.arg(q->name), box.dlg));
    auto *fields = new FillFields(store.style(), box.dlg);
    box.layout->addWidget(fields);
    if (const size_t n = q->count(); n > 0)
        box.layout->addWidget(warnLabel(u"Les %1 valeurs actuelles seront remplacées (Ctrl+Z pour revenir en arrière)."_s.arg(n), box.dlg));
    QPushButton *ok = box.ok;
    fields->changed = [fields, ok] { ok->setEnabled(fields->values().ok()); };
    fields->changed();
    box.cancel();
    box.onOk([&win, qid, fields]() -> QString {
        const series::Result r = fields->values();
        if (!r.ok())
            return r.error;
        QString error;
        win.store().edit([&](Workspace &ws) { error = ws.doc.setValues(qid, r.values); });
        return error;
    });
    box.show();
}

void confirmDelete(MainWindow &win, QId qid)
{
    Store &store = win.store();
    const Quantity *q = store.ws.doc.get(qid);
    if (!q)
        return;
    Box box(win, u"Supprimer une grandeur"_s, u"Supprimer"_s);
    box.ok->setIcon(icons::get("trash"));
    QPalette p = box.ok->palette();
    p.setColor(QPalette::ButtonText, theme::errorColor());
    box.ok->setPalette(p);
    box.ok->setDefault(false);
    box.ok->setAutoDefault(false);
    box.layout->addWidget(new QLabel(u"Supprimer définitivement la grandeur « %1 » ?"_s.arg(q->name), box.dlg));
    const auto usages = store.ws.usages(qid);
    if (!usages.empty()) {
        QStringList items;
        for (const auto &u : usages)
            items << u"<li>%1</li>"_s.arg(u.text().toHtmlEscaped());
        auto *l = new QLabel(u"Elle est utilisée dans :<ul style='margin-top:2px'>%1</ul>"_s.arg(items.join(QString())), box.dlg);
        l->setTextFormat(Qt::RichText);
        box.layout->addWidget(l);
        box.layout->addWidget(weakLabel(u"Les courbes qui l'utilisent seront retirées ; les formules qui la citent seront en erreur. Ctrl+Z annule la suppression."_s, box.dlg));
    }
    box.cancel();
    // « Annuler » par défaut : Entrée ne supprime rien
    for (QAbstractButton *b : box.buttons->buttons())
        if (box.buttons->buttonRole(b) == QDialogButtonBox::RejectRole)
            if (auto *pb = qobject_cast<QPushButton *>(b))
                pb->setDefault(true);
    box.onOk([&win, qid]() -> QString {
        win.store().edit([&](Workspace &ws) { ws.deleteQuantity(qid); });
        return {};
    });
    box.show();
}

void editQuantity(MainWindow &win, QId qid)
{
    Store &store = win.store();
    const Quantity *q = store.ws.doc.get(qid);
    if (!q)
        return;
    Box box(win, u"Grandeur « %1 »"_s.arg(q->name), u"Valider"_s);
    QFormLayout *f = box.form();
    auto *name = new QLineEdit(q->name, box.dlg);
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = unitEdit(q->unit, f, box.dlg);
    QLineEdit *formula = nullptr;
    if (q->formula) {
        formula = formulaEdit(*q->formula, QString(), box.dlg);
        f->addRow(u"Formule"_s, formula);
    }
    if (const Quantity *owner = store.ws.doc.uncertaintyOwner(qid))
        box.layout->addWidget(weakLabel(u"Incertitude-type de « %1 »."_s.arg(owner->name), box.dlg));
    box.cancel();
    box.onOk([&win, qid, name, unit, formula]() -> QString {
        Store &st = win.store();
        const Quantity *cur = st.ws.doc.get(qid);
        if (!cur)
            return {};
        const QString old = cur->name;
        // tout est essayé sur une copie : rien ne change si une partie échoue
        Workspace w = st.ws;
        const QString n = name->text().trimmed();
        bool renamed = false;
        if (n != old) {
            if (const QString e = w.renameQuantity(qid, n); !e.isEmpty())
                return e;
            renamed = true;
        }
        w.doc.setUnit(qid, unit->text().trimmed());
        if (formula) {
            if (const QString e = w.doc.setFormula(qid, formulaText(formula->text())); !e.isEmpty())
                return e;
            if (const Quantity *nq = w.doc.get(qid); nq && !nq->error.isEmpty())
                return u"formule : "_s + nq->error;
        }
        st.edit([&](Workspace &ws) { ws = std::move(w); });
        if (renamed)
            warnScripts(win, old);
        return {};
    });
    box.show();
}

void newColumn(MainWindow &win, ViewId table)
{
    Store &store = win.store();
    Box box(win, u"Nouvelle grandeur"_s, u"Créer"_s);
    QFormLayout *f = box.form();
    auto *name = new QLineEdit(box.dlg);
    name->setPlaceholderText(u"V, t, pH…"_s);
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = unitEdit(QString(), f, box.dlg);
    auto *withFill = new QCheckBox(u"Remplir avec une suite de valeurs"_s, box.dlg);
    box.layout->addWidget(withFill);
    auto *fill = new FillFields(store.style(), box.dlg);
    fill->setVisible(false);
    fill->setContentsMargins(theme::px(18), 0, 0, 0);
    box.layout->addWidget(fill);
    QDialog *d = box.dlg;
    QObject::connect(withFill, &QCheckBox::toggled, fill, [fill, d](bool on) {
        fill->setVisible(on);
        d->adjustSize();
    });
    box.layout->addWidget(weakLabel(u"Pour une grandeur calculée par une formule : bouton « f(x) » du tableur."_s, box.dlg));
    box.cancel();
    box.onOk([&win, table, name, unit, fill, withFill]() -> QString {
        std::vector<double> values;
        if (withFill->isChecked()) {
            const series::Result r = fill->values();
            if (!r.ok())
                return r.error;
            values = r.values;
        }
        QString error;
        win.store().edit([&](Workspace &ws) {
            auto id = ws.doc.addExperimental(name->text().trimmed(), unit->text().trimmed(), std::move(values));
            if (!id) {
                error = id.error;
                return;
            }
            if (TableView *t = ws.tableMut(table))
                t->columns.push_back(*id);
            else
                showInTable(ws, *id);
        });
        return error;
    });
    box.show();
}

void derivative(MainWindow &win, bool second)
{
    Store &store = win.store();
    Box box(win, second ? u"Dérivée seconde"_s : u"Dérivée"_s, u"Calculer"_s);
    QFormLayout *f = box.form();
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    const auto [dx, dy] = defaultPair(store.ws);
    x->setCurrent(dx);
    y->setCurrent(dy);
    f->addRow(u"Dériver"_s, y);
    f->addRow(u"par rapport à"_s, x);
    auto *name = new QLineEdit(box.dlg);
    f->addRow(u"Nom"_s, name);
    auto hint = [&store, x, y, name, second] {
        const QString yn = y->current() ? store.ws.doc.name(*y->current()) : QString();
        const QString xn = x->current() ? store.ws.doc.name(*x->current()) : QString();
        name->setPlaceholderText(second ? u"d2%1_d%22"_s.arg(yn, xn) : u"d%1_d%2"_s.arg(yn, xn));
    };
    QObject::connect(x, &QuantityCombo::picked, name, hint);
    QObject::connect(y, &QuantityCombo::picked, name, hint);
    hint();
    auto *graph = new QCheckBox(u"Tracer dans un nouveau graphe"_s, box.dlg);
    graph->setChecked(true);
    box.layout->addWidget(graph);
    box.layout->addWidget(weakLabel(u"Différences centrées, pas variable accepté. La dérivée est une grandeur calculée : elle suit les mesures."_s, box.dlg));
    box.cancel();
    box.onOk([&win, x, y, name, graph, second]() -> QString {
        Store &st = win.store();
        const auto xi = x->current(), yi = y->current();
        if (!xi || !yi || *xi == *yi)
            return u"Choisissez deux grandeurs différentes."_s;
        const Quantity xq = *st.ws.doc.get(*xi), yq = *st.ws.doc.get(*yi);
        QString base = name->text().trimmed();
        if (base.isEmpty())
            base = second ? u"d2%1_d%22"_s.arg(yq.name, xq.name) : u"d%1_d%2"_s.arg(yq.name, xq.name);
        const QString nm = st.ws.doc.uniqueName(csv::sanitizeName(base, u"d"_s));
        QString formula, unit;
        if (second) {
            formula = u"deriv(deriv(%1, %2), %2)"_s.arg(yq.name, xq.name);
            unit = unitRatio(yq.unit, xq.unit.trimmed().isEmpty() ? QString() : xq.unit.trimmed() + u"^2"_s);
        } else {
            formula = u"deriv(%1, %2)"_s.arg(yq.name, xq.name);
            unit = unitRatio(yq.unit, xq.unit);
        }
        QString error;
        std::optional<QId> created;
        st.edit([&](Workspace &ws) {
            auto r = ws.doc.addCalculated(nm, unit, formula);
            if (!r) {
                error = r.error;
                return;
            }
            created = *r;
            showInTable(ws, *r);
        });
        if (!created)
            return error;
        if (graph->isChecked())
            win.newGraph(std::pair(xq.id, *created));
        st.message(u"%1 = %2"_s.arg(nm, formula));
        return {};
    });
    box.show();
}

void integral(MainWindow &win)
{
    Store &store = win.store();
    const NumberStyle style = store.style();
    Box box(win, u"Intégrale"_s, u"Créer la primitive"_s);
    QFormLayout *f = box.form();
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    const auto [dx, dy] = defaultPair(store.ws);
    x->setCurrent(dx);
    y->setCurrent(dy);
    f->addRow(u"Intégrer"_s, y);
    f->addRow(u"par rapport à"_s, x);
    auto *name = new QLineEdit(box.dlg);
    f->addRow(u"Nom de la primitive"_s, name);
    // aire entre deux bornes, mise à jour à chaque réglage
    auto *areaBox = new QWidget(box.dlg);
    auto *al = new QVBoxLayout(areaBox);
    al->setContentsMargins(0, 4, 0, 0);
    auto *row = new QHBoxLayout;
    auto *a = new NumberEdit(areaBox);
    auto *b = new NumberEdit(areaBox);
    a->setStyle(style);
    b->setStyle(style);
    row->addWidget(new QLabel(u"Aire entre"_s, areaBox));
    row->addWidget(a);
    row->addWidget(new QLabel(u"et"_s, areaBox));
    row->addWidget(b);
    row->addStretch();
    al->addLayout(row);
    auto *area = strongLabel(QString(), areaBox);
    al->addWidget(area);
    box.layout->addWidget(areaBox);
    auto bounds = [&store, x, a, b] {
        if (!x->current())
            return;
        if (auto r = numeric::finiteRange(store.ws.doc.values(*x->current()))) {
            a->setValue(r->first);
            b->setValue(r->second);
        }
    };
    auto compute = [&store, x, y, a, b, area, name, style] {
        const Quantity *xq = x->current() ? store.ws.doc.get(*x->current()) : nullptr;
        const Quantity *yq = y->current() ? store.ws.doc.get(*y->current()) : nullptr;
        name->setPlaceholderText(yq ? u"int_"_s + yq->name : QString());
        if (!xq || !yq) {
            area->clear();
            return;
        }
        const double s = numeric::areaBetween(yq->values, xq->values, a->value(), b->value());
        const QString unit = prettyUnit(unitProduct(yq->unit, xq->unit));
        area->setText((u"Aire = %1 %2"_s.arg(formatSig(s, 5, style), unit)).trimmed());
    };
    QObject::connect(x, &QuantityCombo::picked, area, [bounds, compute] {
        bounds();
        compute();
    });
    QObject::connect(y, &QuantityCombo::picked, area, compute);
    QObject::connect(a, &NumberEdit::valueEdited, area, compute);
    QObject::connect(b, &NumberEdit::valueEdited, area, compute);
    bounds();
    compute();
    box.layout->addWidget(weakLabel(u"La primitive (méthode des trapèzes) est nulle au premier point."_s, box.dlg));
    box.cancel(u"Fermer"_s);
    box.onOk([&win, x, y, name]() -> QString {
        Store &st = win.store();
        const auto xi = x->current(), yi = y->current();
        if (!xi || !yi)
            return u"Choisissez les deux grandeurs."_s;
        const Quantity xq = *st.ws.doc.get(*xi), yq = *st.ws.doc.get(*yi);
        QString base = name->text().trimmed();
        if (base.isEmpty())
            base = u"int_"_s + yq.name;
        const QString nm = st.ws.doc.uniqueName(csv::sanitizeName(base, u"I"_s));
        QString error;
        st.edit([&](Workspace &ws) {
            auto r = ws.doc.addCalculated(nm, unitProduct(yq.unit, xq.unit), u"integ(%1, %2)"_s.arg(yq.name, xq.name));
            if (!r)
                error = r.error;
            else
                showInTable(ws, *r);
        });
        if (error.isEmpty())
            st.message(u"Primitive %1 créée"_s.arg(nm));
        return error;
    });
    box.show();
}

void spectrum(MainWindow &win)
{
    Store &store = win.store();
    const NumberStyle style = store.style();
    Box box(win, u"Analyse de Fourier"_s, u"Calculer le spectre"_s);
    QFormLayout *f = box.form();
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    const auto [dx, dy] = defaultPair(store.ws);
    x->setCurrent(dx);
    y->setCurrent(dy);
    f->addRow(u"Signal"_s, y);
    f->addRow(u"Temps"_s, x);
    auto *rect = new QRadioButton(fft::windowLabel(fft::Window::Rectangular), box.dlg);
    auto *hann = new QRadioButton(fft::windowLabel(fft::Window::Hann), box.dlg);
    hann->setToolTip(u"Réduit les « fuites » quand la durée n'est pas un nombre entier de périodes"_s);
    rect->setChecked(true);
    auto *wrow = new QHBoxLayout;
    wrow->addWidget(rect);
    wrow->addWidget(hann);
    wrow->addStretch();
    f->addRow(u"Fenêtre"_s, wrow);
    auto *pad = new QComboBox(box.dlg);
    for (int k : {1, 2, 4, 8, 16})
        pad->addItem(u"× %1"_s.arg(k), k);
    pad->setToolTip(u"Ajoute des zéros au signal : spectre plus finement échantillonné (pics mieux dessinés)"_s);
    f->addRow(u"Complétion par des zéros"_s, pad);
    auto *info = weakLabel(QString(), box.dlg);
    box.layout->addWidget(info);
    auto *uneven = warnLabel(QString(), box.dlg);
    box.layout->addWidget(uneven);
    auto *resample = new QCheckBox(u"Rééchantillonner à pas constant (interpolation linéaire)"_s, box.dlg);
    box.layout->addWidget(resample);
    auto update = [&store, x, info, uneven, resample, style] {
        const Quantity *xq = x->current() ? store.ws.doc.get(*x->current()) : nullptr;
        const auto si = xq ? numeric::stepInfo(xq->values) : std::nullopt;
        if (!si) {
            info->clear();
            uneven->hide();
            resample->hide();
            return;
        }
        const auto [dt, dev] = *si;
        const QString unit = prettyUnit(xq->unit);
        const QString funit = xq->unit.trimmed() == u"s" ? u"Hz"_s : prettyUnit(unitRatio(QString(), xq->unit));
        info->setText(u"Pas d'échantillonnage : %1 %2 ; fréquence d'échantillonnage : %3 %4 ; %5 points"_s.arg(formatSig(dt, 4, style), unit,
                                                                                                                 formatSig(1.0 / dt, 4, style), funit)
                          .arg(xq->count()));
        const bool bad = dev > 0.01;
        uneven->setText(u"⚠ Pas non constant (écart jusqu'à %1 %)."_s.arg(formatFixed(dev * 100.0, 1, style)));
        uneven->setVisible(bad);
        resample->setVisible(bad);
    };
    QObject::connect(x, &QuantityCombo::picked, info, update);
    update();
    box.cancel();
    box.onOk([&win, x, y, hann, pad, resample]() -> QString {
        Store &st = win.store();
        const auto xi = x->current(), yi = y->current();
        if (!xi || !yi || *xi == *yi)
            return u"Choisissez le signal et le temps."_s;
        const Quantity xq = *st.ws.doc.get(*xi), yq = *st.ws.doc.get(*yi);
        std::vector<double> ts = xq.values, vs = yq.values;
        if (resample->isVisible() && resample->isChecked()) {
            const size_t n = static_cast<size_t>(std::count_if(xq.values.begin(), xq.values.end(), [](double v) { return std::isfinite(v); }));
            std::tie(ts, vs) = numeric::resampleUniform(xq.values, yq.values, n);
        }
        fft::SpectrumOptions opts;
        opts.window = hann->isChecked() ? fft::Window::Hann : fft::Window::Rectangular;
        opts.zeroPadding = pad->currentData().toInt();
        QString error;
        const auto s = fft::amplitudeSpectrum(ts, vs, opts, &error);
        if (!s)
            return error;
        ViewId gid = 0;
        st.edit([&](Workspace &ws) {
            const QString fu = xq.unit.trimmed() == u"s" ? u"Hz"_s : unitRatio(QString(), xq.unit);
            const QString fname = ws.doc.uniqueName(u"f"_s);
            const QString aname = ws.doc.uniqueName(csv::sanitizeName(u"A_"_s + yq.name, u"A"_s));
            const QId fq = *ws.doc.addExperimental(fname, fu, s->freq);
            const QId aq = *ws.doc.addExperimental(aname, yq.unit, s->amplitude);
            const ViewId tid = ws.addTable({fq, aq});
            ws.tableMut(tid)->name = u"Spectre de %1"_s.arg(yq.name);
            gid = ws.addGraph();
            GraphView *g = ws.graphMut(gid);
            g->name = u"Spectre de %1"_s.arg(yq.name);
            Plot &p = g->plots[0];
            p.title.text = u"Spectre en amplitude de %1"_s.arg(yq.name);
            p.y.includeZero = true;
            p.x.includeZero = true;
            CurveSpec &c = p.addCurve(fq, aq);
            c.marker = Marker::None;
            c.line = LineKind::Straight;
            c.lineWidth = 1.2;
        });
        win.openTab(TabKey{TabKey::Graph, gid});
        return {};
    });
    box.show();
}

void tangents(MainWindow &win, std::optional<ViewId> graphId, int plot)
{
    Store &store = win.store();
    const NumberStyle style = store.style();
    if (store.ws.graphs.empty()) {
        store.message(u"Créez d'abord un graphe contenant la courbe de dosage."_s, icons::Status::Warning);
        return;
    }
    Box box(win, u"Méthode des tangentes"_s, u"Tracer sur le graphe"_s);
    QFormLayout *f = box.form();
    auto *graph = graphCombo(store.ws, false, box.dlg);
    if (graphId)
        graph->setCurrentIndex(std::max(0, graph->findData(QVariant::fromValue<qulonglong>(*graphId))));
    f->addRow(u"Graphe"_s, graph);
    auto *curve = new QComboBox(box.dlg);
    f->addRow(u"Courbe"_s, curve);
    auto *result = strongLabel(QString(), box.dlg);
    box.layout->addWidget(result);
    auto *err = errorLabel(box.dlg);
    box.layout->addWidget(err);
    box.layout->addWidget(weakLabel(u"Les deux tangentes parallèles et la médiane sont tracées sur le graphe ; glissez un point de contact pour les ajuster."_s, box.dlg));
    auto plotOf = [&store, graph, plot]() -> std::pair<const GraphView *, int> {
        const GraphView *g = store.ws.graph(graph->currentData().toULongLong());
        if (!g)
            return {nullptr, 0};
        return {g, std::clamp(plot, 0, std::max(g->visiblePlots() - 1, 0))};
    };
    auto preview = [&store, plotOf, curve, result, err, style] {
        result->clear();
        showError(err, QString());
        const auto [g, pi] = plotOf();
        if (!g || curve->currentIndex() < 0 || static_cast<size_t>(pi) >= g->plots.size())
            return;
        const auto &cs = g->plots[pi].curves;
        if (static_cast<size_t>(curve->currentIndex()) >= cs.size())
            return;
        const CurveSpec c = cs[curve->currentIndex()];
        QString e;
        auto tc = titration::Curve::make(store.ws.doc.values(c.x), store.ws.doc.values(c.y), &e);
        std::optional<titration::Tangents> t;
        if (tc)
            t = tc->tangents(tc->defaultContact(), &e);
        if (!t) {
            showError(err, e);
            return;
        }
        result->setText(u"Équivalence : %1 = %2 ; %3 = %4"_s.arg(store.ws.doc.name(c.x), formatSig(t->equivalence.first, 4, style), store.ws.doc.name(c.y),
                                                                  formatSig(t->equivalence.second, 3, style)));
    };
    auto fillCurves = [&store, plotOf, curve, preview] {
        curve->clear();
        const auto [g, pi] = plotOf();
        if (g && static_cast<size_t>(pi) < g->plots.size())
            for (const auto &c : g->plots[pi].curves)
                curve->addItem(u"%1(%2)"_s.arg(store.ws.doc.name(c.y), store.ws.doc.name(c.x)));
        preview();
    };
    QObject::connect(graph, &QComboBox::activated, curve, fillCurves);
    QObject::connect(curve, &QComboBox::activated, curve, preview);
    fillCurves();
    box.cancel();
    box.onOk([&win, plotOf, curve]() -> QString {
        const auto [g, pi] = plotOf();
        if (!g || curve->currentIndex() < 0)
            return u"Ce graphe ne contient aucune courbe."_s;
        const ViewId gid = g->id;
        const int ci = curve->currentIndex();
        win.store().edit([&](Workspace &ws) {
            Plot &p = ws.graphMut(gid)->plots[pi];
            const CurveSpec c = p.curves[ci];
            std::erase_if(p.tangents, [&](const TangentSpec &t) { return t.x == c.x && t.y == c.y; });
            TangentSpec t;
            t.x = c.x;
            t.y = c.y;
            t.color = PALETTE[2];
            t.side = c.side;
            p.tangents.push_back(t);
        });
        win.openTab(TabKey{TabKey::Graph, gid});
        return {};
    });
    box.show();
}

void derivativeMethod(MainWindow &win)
{
    Store &store = win.store();
    const NumberStyle style = store.style();
    Box box(win, u"Méthode de la dérivée"_s, u"Appliquer"_s);
    QFormLayout *f = box.form();
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    // le pH s'il existe, en fonction du volume
    auto [dx, dy] = defaultPair(store.ws);
    if (const Quantity *ph = store.ws.doc.byName(u"pH"_s)) {
        dy = ph->id;
        if (dx == ph->id)
            dx.reset();
        for (const auto &q : store.ws.doc.quantities)
            if (!dx && q.id != ph->id)
                dx = q.id;
    }
    x->setCurrent(dx);
    y->setCurrent(dy);
    f->addRow(u"pH (ou grandeur dosée)"_s, y);
    f->addRow(u"Volume versé"_s, x);
    auto *graph = graphCombo(store.ws, true, box.dlg);
    f->addRow(u"Graphe"_s, graph);
    // le graphe qui trace déjà le pH en fonction du volume
    for (const auto &g : store.ws.graphs)
        for (const auto &c : g.plots.empty() ? std::vector<CurveSpec>() : g.plots.front().curves)
            if (dx && dy && c.x == *dx && c.y == *dy)
                graph->setCurrentIndex(graph->findData(QVariant::fromValue<qulonglong>(g.id)));
    auto *result = strongLabel(QString(), box.dlg);
    box.layout->addWidget(result);
    auto *err = errorLabel(box.dlg);
    box.layout->addWidget(err);
    box.layout->addWidget(weakLabel(u"Crée la dérivée, la trace sur l'axe de droite et marque l'équivalence par une droite verticale."_s, box.dlg));
    auto preview = [&store, x, y, result, err, style] {
        result->clear();
        showError(err, QString());
        const Quantity *xq = x->current() ? store.ws.doc.get(*x->current()) : nullptr;
        const Quantity *yq = y->current() ? store.ws.doc.get(*y->current()) : nullptr;
        if (!xq || !yq)
            return;
        QString e;
        if (auto m = titration::derivativeMaximum(xq->values, yq->values, &e))
            result->setText(u"Maximum de |d%1/d%2| en %2 = %3 %4"_s.arg(yq->name, xq->name, formatSig(m->first, 4, style), prettyUnit(xq->unit)).trimmed());
        else
            showError(err, e);
    };
    QObject::connect(x, &QuantityCombo::picked, result, preview);
    QObject::connect(y, &QuantityCombo::picked, result, preview);
    preview();
    box.cancel();
    box.onOk([&win, x, y, graph, style]() -> QString {
        Store &st = win.store();
        const auto xi = x->current(), yi = y->current();
        if (!xi || !yi || *xi == *yi)
            return u"Choisissez deux grandeurs différentes."_s;
        const Quantity xq = *st.ws.doc.get(*xi), yq = *st.ws.doc.get(*yi);
        QString e;
        const auto m = titration::derivativeMaximum(xq.values, yq.values, &e);
        if (!m)
            return e;
        const double ve = m->first;
        const ViewId chosen = graph->currentData().toULongLong();
        ViewId gid = 0;
        st.edit([&](Workspace &ws) {
            const QString dname = csv::sanitizeName(u"d%1_d%2"_s.arg(yq.name, xq.name), u"d"_s);
            QId dq;
            if (const Quantity *q = ws.doc.byName(dname)) {
                dq = q->id;
            } else {
                dq = *ws.doc.addCalculated(ws.doc.uniqueName(dname), unitRatio(yq.unit, xq.unit), u"deriv(%1, %2)"_s.arg(yq.name, xq.name));
                showInTable(ws, dq);
            }
            gid = chosen && ws.graph(chosen) ? chosen : 0;
            if (!gid) {
                gid = ws.addGraph();
                ws.graphMut(gid)->plots[0].addCurve(xq.id, yq.id);
            }
            Plot &p = ws.graphMut(gid)->plots[0];
            if (std::none_of(p.curves.begin(), p.curves.end(), [&](const CurveSpec &c) { return c.y == dq; })) {
                CurveSpec &c = p.addCurve(xq.id, dq);
                c.side = YSide::Right;
                c.marker = Marker::None;
                c.line = LineKind::Straight;
                c.dash = Dash::Dashed;
            }
            Annotation a;
            a.kind = Annotation::VLine;
            a.x = ve;
            a.color = PALETTE[3];
            a.label = (u"%1 = %2 %3"_s.arg(xq.name, formatSig(ve, 4, style), prettyUnit(xq.unit))).trimmed();
            a.dash = Dash::Dashed;
            p.annotations.push_back(a);
        });
        win.openTab(TabKey{TabKey::Graph, gid});
        return {};
    });
    box.show();
}

void meta(MainWindow &win)
{
    const Meta m = win.store().ws.meta;
    Box box(win, u"Informations du TP"_s, u"Valider"_s, 440);
    box.layout->addWidget(weakLabel(u"Ces informations figurent en tête des documents PDF exportés et imprimés."_s, box.dlg));
    QFormLayout *f = box.form();
    auto *title = new QLineEdit(m.title, box.dlg);
    auto *authors = new QLineEdit(m.authors, box.dlg);
    auto *klass = new QLineEdit(m.klass, box.dlg);
    auto *notes = new QPlainTextEdit(m.notes, box.dlg);
    theme::setFixedHeight(notes, 90);
    title->setPlaceholderText(u"Dosage de l'acide éthanoïque"_s);
    authors->setPlaceholderText(u"Prénom Nom, Prénom Nom"_s);
    f->addRow(u"Titre du TP"_s, title);
    f->addRow(u"Noms"_s, authors);
    f->addRow(u"Classe"_s, klass);
    f->addRow(u"Notes"_s, notes);
    box.cancel();
    box.onOk([&win, title, authors, klass, notes]() -> QString {
        Meta n{title->text(), authors->text(), klass->text(), notes->toPlainText()};
        if (!(n == win.store().ws.meta))
            win.store().edit([&](Workspace &ws) { ws.meta = n; });
        return {};
    });
    box.show();
}

void preferences(MainWindow &win)
{
    Store &store = win.store();
    Box box(win, u"Préférences"_s, QString(), 440);
    // le thème clair ou sombre se choisit avec le bouton soleil / lune de la barre d'outils ; la
    // virgule ou le point décimal suit la langue
    auto *darkPlots = new QCheckBox(u"Graphes sur fond sombre avec le thème sombre"_s, box.dlg);
    darkPlots->setChecked(store.prefs.darkPlots);
    darkPlots->setToolTip(u"Les exports et les impressions restent toujours sur fond blanc"_s);
    box.layout->addWidget(darkPlots);
    auto *confirm = new QCheckBox(u"Demander avant de fermer la fenêtre d'un tableur, d'un graphe ou d'un script"_s, box.dlg);
    confirm->setChecked(store.prefs.confirmClose);
    confirm->setToolTip(u"Ils restent dans le document : menu Fenêtres pour les rouvrir"_s);
    box.layout->addWidget(confirm);
    QFormLayout *f = box.form();
    auto *sig = new QSpinBox(box.dlg);
    sig->setRange(2, 15);
    sig->setValue(store.prefs.sigDigits);
    f->addRow(u"Chiffres significatifs des tableurs"_s, sig);
    auto apply = [&store, darkPlots, confirm, sig] {
        store.prefs.darkPlots = darkPlots->isChecked();
        store.prefs.confirmClose = confirm->isChecked();
        store.prefs.sigDigits = sig->value();
        store.prefsChanged();
    };
    QObject::connect(darkPlots, &QCheckBox::toggled, box.dlg, apply);
    QObject::connect(confirm, &QCheckBox::toggled, box.dlg, apply);
    QObject::connect(sig, &QSpinBox::valueChanged, box.dlg, apply);
    box.cancel(u"Fermer"_s);
    box.show();
}

void pythonExport(MainWindow &win)
{
    const QString text = pyexport::exportScript(win.store().ws);
    Box box(win, u"Exporter vers Python"_s, QString(), 680);
    box.dlg->resize(theme::px(QSize(760, 600)));
    box.layout->addWidget(weakLabel(u"Script numpy + matplotlib reproduisant les données, les calculs et les graphes (compatible Capytale)."_s, box.dlg));
    auto *edit = new CodeEditor(box.dlg);
    edit->setPlainText(text);
    edit->setReadOnly(true);
    new PythonHighlighter(edit->document());
    box.layout->addWidget(edit, 1);
    auto *copy = box.buttons->addButton(u"Copier"_s, QDialogButtonBox::ActionRole);
    copy->setIcon(icons::get("copy"));
    QObject::connect(copy, &QPushButton::clicked, box.dlg, [&win, text] {
        QGuiApplication::clipboard()->setText(text);
        win.store().message(u"Script copié"_s);
    });
    auto *save = box.buttons->addButton(u"Enregistrer…"_s, QDialogButtonBox::ActionRole);
    save->setIcon(icons::get("save"));
    QDialog *d = box.dlg;
    QObject::connect(save, &QPushButton::clicked, box.dlg, [&win, text, d] {
        const QString base = win.currentPath().isEmpty() ? u"tp.py"_s : QFileInfo(win.currentPath()).completeBaseName() + u".py"_s;
        QString path = QFileDialog::getSaveFileName(d, u"Enregistrer le script Python"_s, base, u"Script Python (*.py)"_s);
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += u".py"_s;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.toUtf8()) < 0) {
            win.store().message(u"Écriture impossible : %1"_s.arg(file.errorString()), icons::Status::Error);
            return;
        }
        win.store().message(u"Script enregistré : %1"_s.arg(QFileInfo(path).fileName()));
    });
    box.cancel(u"Fermer"_s);
    box.show();
}

} // namespace dialogs
