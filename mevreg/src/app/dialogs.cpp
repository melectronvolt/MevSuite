#include "app/dialogs.h"

#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/savefile.h"
#include "app/scriptpanel.h"
#include "app/theme.h"
#include "app/tableview.h"
#include "app/widgets.h"
#include "core/csv.h"
#include "core/expr.h"
#include "core/fft.h"
#include "core/numeric.h"
#include "core/pyexport.h"
#include "core/series.h"
#include "core/titration.h"
#include "plot/resolve.h"

#include <QAbstractSpinBox>
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
#include <QSaveFile>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/// Unité d'un quotient a/b, écrite simplement.
QString rapport_unites(const QString &a, const QString &b)
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

std::optional<double> secondes_par_unite(const QString &unite)
{
    const QString u = unite.trimmed();
    if (u == u"s") return 1.0;
    if (u == u"ms") return 1e-3;
    if (u == u"µs" || u == u"μs" || u == u"us") return 1e-6;
    if (u == u"ns") return 1e-9;
    if (u == u"min") return 60.0;
    if (u == u"h") return 3600.0;
    return {};
}

QString produit_unites(const QString &a, const QString &b)
{
    const QString x = a.trimmed(), y = b.trimmed();
    if (x.isEmpty())
        return y;
    if (y.isEmpty())
        return x;
    return x + u'.' + y;
}

QLabel *etiquette_erreur(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setWordWrap(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, theme::couleur_erreur());
    l->setPalette(p);
    l->hide();
    return l;
}

void montre_erreur(QLabel *l, const QString &text)
{
    l->setText(text.isEmpty() ? QString() : u"⚠ "_s + text);
    l->setVisible(!text.isEmpty());
    // la boîte grandit pour le message (qui passe à la ligne) : sans cela, Qt prenait la place aux
    // champs, écrasés ; elle ne rapetisse pas (l'utilisateur a pu l'agrandir)
    QWidget *w = l->window();
    if (text.isEmpty() || !w || !w->isVisible() || !w->layout())
        return;
    QLayout *layout = w->layout();
    layout->activate();
    const int need = std::max(layout->totalMinimumSize().height(),
                              layout->hasHeightForWidth() ? layout->totalHeightForWidth(w->width()) : layout->totalSizeHint().height());
    if (w->height() < need)
        w->resize(w->width(), need);
}

QLabel *etiquette_attention(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, theme::couleur_attention());
    l->setPalette(p);
    return l;
}

QLabel *etiquette_forte(const QString &text, QWidget *parent)
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

    Box(MainWindow &win, const QString &title, const QString &okText, int minWidth = 380, const char *icone = "check")
    {
        dlg = new QDialog(&win);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(title);
        theme::definit_largeur_min(dlg, minWidth);
        layout = new QVBoxLayout(dlg);
        layout->setSpacing(8);
        error = etiquette_erreur(dlg);
        buttons = new QDialogButtonBox(dlg);
        if (!okText.isEmpty()) {
            ok = buttons->addButton(okText, QDialogButtonBox::AcceptRole);
            ok->setIcon(icons::obtient(icone));
            ok->setDefault(true);
        }
        QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    }
    QFormLayout *formulaire()
    {
        auto *f = new QFormLayout;
        f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addLayout(f);
        return f;
    }
    void annule(const QString &text = u"Annuler"_s) { buttons->addButton(text, QDialogButtonBox::RejectRole)->setIcon(icons::obtient("close")); }
    /// `apply` renvoie un message d'erreur, vide si l'opération a réussi (la boîte se ferme alors).
    void sur_ok(std::function<QString()> apply)
    {
        QLabel *err = error;
        QDialog *d = dlg;
        QObject::connect(ok, &QPushButton::clicked, dlg, [apply, err, d] {
            const QString e = apply();
            if (e.isEmpty()) {
                d->accept();
                return;
            }
            montre_erreur(err, e);
        });
    }
    void montre()
    {
        layout->addWidget(error);
        layout->addWidget(buttons);
        // le message d'erreur s'efface dès que l'on corrige un champ (il ne doit pas parler d'une
        // saisie qui n'est plus là)
        QLabel *err = error;
        QDialog *d = dlg;
        auto clear = [err, d] {
            if (err->isVisible()) {
                montre_erreur(err, QString());
                d->adjustSize();
            }
        };
        for (QLineEdit *e : dlg->findChildren<QLineEdit *>())
            QObject::connect(e, &QLineEdit::textEdited, dlg, clear);
        for (QComboBox *c : dlg->findChildren<QComboBox *>())
            QObject::connect(c, &QComboBox::currentIndexChanged, dlg, clear);
        for (QAbstractSpinBox *sp : dlg->findChildren<QAbstractSpinBox *>())
            QObject::connect(sp, &QAbstractSpinBox::editingFinished, dlg, clear);
        for (QAbstractButton *b : dlg->findChildren<QCheckBox *>())
            QObject::connect(b, &QAbstractButton::toggled, dlg, clear);
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
            e->definit_style(style);
        m_start->definit_valeur(0);
        m_end->definit_valeur(10);
        m_step->definit_valeur(1);
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
        m_preview = etiquette_attenuee(QString(), this);
        g->addWidget(m_preview, 3, 0, 1, 2);
        auto update = [this] { rafraichit(); };
        for (NumberEdit *e : {m_start, m_end, m_step})
            connect(e, &NumberEdit::valeur_modifiee, this, update);
        connect(m_count, &QSpinBox::valueChanged, this, update);
        connect(m_byStep, &QRadioButton::toggled, this, update);
        rafraichit();
    }

    series::Result valeurs() const
    {
        if (m_byStep->isChecked())
            return series::par_pas(m_start->valeur(), m_end->valeur(), m_step->valeur());
        return series::par_nombre(m_start->valeur(), m_end->valeur(), static_cast<size_t>(m_count->value()));
    }
    std::function<void()> changed;

private:
    void rafraichit()
    {
        m_step->setEnabled(m_byStep->isChecked());
        m_count->setEnabled(!m_byStep->isChecked());
        const series::Result r = valeurs();
        QPalette p = m_preview->palette();
        if (!r.ok()) {
            m_preview->setText(r.error);
            p.setColor(QPalette::WindowText, theme::couleur_erreur());
        } else {
            const auto &v = r.values;
            auto f = [&](double x) { return format_significatif(x, 6, m_style); };
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
            p.setColor(QPalette::WindowText, theme::couleur_attenuee());
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
void montre_dans_tableur(Seance &ws, QId q, std::optional<ViewId> table = std::nullopt)
{
    TableView *t = table ? ws.tableur_modifiable(*table) : nullptr;
    if (!t && !ws.tables.empty())
        t = &ws.tables.front();
    if (t && std::find(t->columns.begin(), t->columns.end(), q) == t->columns.end())
        t->columns.push_back(q);
}

/// Garde la grandeur calculée avec ses mesures lorsque le document contient plusieurs tableurs.
void montre_avec_mesures(Seance &ws, QId resultat, QId x, QId y)
{
    std::optional<ViewId> table;
    for (const auto &t : ws.tables) {
        if (std::find(t.columns.begin(), t.columns.end(), y) == t.columns.end()) continue;
        if (!table) table = t.id;
        if (std::find(t.columns.begin(), t.columns.end(), x) != t.columns.end()) {
            table = t.id;
            break;
        }
    }
    montre_dans_tableur(ws, resultat, table);
}

/// Grandeurs proposées par défaut : les deux premières colonnes du premier tableur.
std::pair<std::optional<QId>, std::optional<QId>> paire_par_defaut(const Seance &ws, std::optional<dialogs::Axes> axes = std::nullopt)
{
    if (axes && ws.doc.obtient(axes->first) && ws.doc.obtient(axes->second))
        return {axes->first, axes->second};
    std::vector<QId> ids;
    if (!ws.tables.empty())
        ids = ws.tables.front().columns;
    for (const auto &q : ws.doc.quantities)
        if (std::find(ids.begin(), ids.end(), q.id) == ids.end())
            ids.push_back(q.id);
    // les incertitudes ne sont pas des grandeurs qu'on dérive ou qu'on trace
    std::erase_if(ids, [&](QId id) { return !ws.doc.obtient(id) || ws.doc.proprietaire_incertitude(id); });
    std::optional<QId> x, y;
    if (!ids.empty())
        x = ids[0];
    if (ids.size() > 1)
        y = ids[1];
    return {x, y};
}

QComboBox *liste_graphes(const Seance &ws, bool allowNew, QWidget *parent)
{
    auto *c = new QComboBox(parent);
    if (allowNew)
        c->addItem(u"nouveau graphe"_s, QVariant::fromValue<qulonglong>(0));
    for (const auto &g : ws.graphs)
        c->addItem(g.name, QVariant::fromValue<qulonglong>(g.id));
    return c;
}

void avertit_scripts(MainWindow &win, const QString &old)
{
    for (const QString &s : win.depot().ws.scripts_utilisant(old))
        win.depot().message(u"Le script « %1 » utilise encore l'ancien nom « %2 » : pensez à le modifier"_s.arg(s, old), icons::Status::Warning);
}

QLineEdit *champ_unite(const QString &unit, QFormLayout *form, QWidget *parent)
{
    auto *e = new QLineEdit(unit, parent);
    e->setPlaceholderText(u"m/s, m.s-1, mol.L-1…"_s);
    auto *preview = etiquette_attenuee(QString(), parent);
    auto upd = [e, preview] {
        const QString t = e->text().trimmed();
        preview->setText(t.isEmpty() ? QString() : u"Affichage : "_s + unite_lisible(t));
        preview->setVisible(!t.isEmpty());
    };
    QObject::connect(e, &QLineEdit::textChanged, preview, upd);
    upd();
    form->addRow(u"Unité"_s, e);
    form->addRow(QString(), preview);
    return e;
}

QLineEdit *champ_formule(const QString &text, const QString &placeholder, QWidget *parent)
{
    auto *e = new QLineEdit(text, parent);
    e->setFont(theme::police_fixe());
    e->setPlaceholderText(placeholder);
    theme::definit_largeur_min(e, 260);
    return e;
}

} // namespace

namespace dialogs {

QString texte_formule(const QString &s)
{
    // fonctions dont les arguments sont séparés par une virgule : min(2,3) garde sa virgule ;
    // ailleurs, une virgule entre deux chiffres est une virgule décimale : sin(0,5), 0,05*x
    static const QStringList multi = {u"min"_s, u"max"_s, u"pow"_s, u"atan2"_s, u"deriv"_s, u"integ"_s};
    const QString t = s.trimmed();
    QString out;
    out.reserve(t.size());
    std::vector<bool> calls; // pour chaque parenthèse ouverte : appel d'une fonction à plusieurs arguments
    for (qsizetype i = 0; i < t.size(); ++i) {
        const QChar c = t[i];
        if (c == u'(') {
            qsizetype end = out.size();
            while (end > 0 && out[end - 1].isSpace())
                --end;
            qsizetype start = end;
            while (start > 0 && expr::est_car_identifiant(out[start - 1]))
                --start;
            calls.push_back(multi.contains(out.mid(start, end - start)));
        } else if (c == u')') {
            if (!calls.empty())
                calls.pop_back();
        } else if (c == u',' && i > 0 && i + 1 < t.size() && t[i - 1].isDigit() && t[i + 1].isDigit()
                   && (calls.empty() || !calls.back())) {
            out += u'.';
            continue;
        }
        out += c;
    }
    return out;
}

void nouvelle_grandeur(MainWindow &win, bool calculated, std::optional<ViewId> table)
{
    Store &store = win.depot();
    Box box(win, calculated ? u"Nouvelle grandeur calculée"_s : u"Nouvelle grandeur"_s, u"Créer"_s, 380, "plus");
    QFormLayout *f = box.formulaire();
    auto *name = new QLineEdit(box.dlg);
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = champ_unite(QString(), f, box.dlg);
    QLineEdit *formula = nullptr;
    if (calculated) {
        formula = champ_formule(QString(), u"0.5*m*v^2"_s, box.dlg);
        f->addRow(u"Formule"_s, formula);
        box.layout->addWidget(etiquette_attenuee(u"Opérations + − * / ^, fonctions sqrt, exp, ln, log, sin, cos… ; deriv(y, x) et integ(y, x) pour dériver et intégrer. Nombres décimaux : avec un point ou une virgule (0.05 ou 0,05)."_s, box.dlg));
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
    box.annule();
    box.sur_ok([&win, calculated, table, name, unit, formula, fill, withFill]() -> QString {
        Store &st = win.depot();
        const QString n = name->text().trimmed(), u = unit->text().trimmed();
        if (const QString e = st.ws.doc.verifie_nom(n); !e.isEmpty())
            return e;
        if (calculated) {
            const QString fx = texte_formule(formula->text());
            if (fx.isEmpty())
                return u"Saisissez la formule de la grandeur."_s;
            // essai sur une copie : pas de grandeur créée si la formule est fausse
            Document test = st.ws.doc;
            auto r = test.ajoute_calculee(n, u, fx);
            if (!r)
                return r.error;
            if (const Grandeur *q = test.obtient(*r); q && !q->error.isEmpty())
                return q->error;
            st.edite([&](Seance &ws) {
                if (auto id = ws.doc.ajoute_calculee(n, u, fx))
                    montre_dans_tableur(ws, *id, table);
            });
            return {};
        }
        std::vector<double> values;
        if (withFill->isChecked()) {
            const series::Result r = fill->valeurs();
            if (!r.ok())
                return r.error;
            values = r.values;
        }
        QString error;
        st.edite([&](Seance &ws) {
            auto id = ws.doc.ajoute_experimentale(n, u, std::move(values));
            if (!id)
                error = id.error;
            else
                montre_dans_tableur(ws, *id, table);
        });
        return error;
    });
    box.montre();
}

void nouveau_parametre(MainWindow &win)
{
    Box box(win, u"Nouveau paramètre"_s, u"Créer"_s, 380, "plus");
    QFormLayout *f = box.formulaire();
    auto *name = new QLineEdit(win.depot().ws.doc.nom_unique(u"k"_s), box.dlg);
    name->selectAll();
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = champ_unite(QString(), f, box.dlg);
    auto *value = new NumberEdit(box.dlg);
    value->definit_style(win.depot().style());
    value->definit_valeur(1.0);
    f->addRow(u"Valeur"_s, value);
    box.layout->addWidget(etiquette_attenuee(u"Le paramètre se règle ensuite au curseur dans la fenêtre Calculs ; les formules et les scripts qui l'utilisent suivent."_s, box.dlg));
    box.annule();
    box.sur_ok([&win, name, unit, value]() -> QString {
        QString error;
        win.depot().edite([&](Seance &ws) {
            auto r = ws.doc.ajoute_parametre(name->text().trimmed(), unit->text().trimmed(), value->valeur());
            if (!r)
                error = r.error;
        });
        if (error.isEmpty())
            win.ouvre_onglet(TabKey{TabKey::Calculations});
        return error;
    });
    box.montre();
}

void nouvelle_incertitude(MainWindow &win, QId of)
{
    Store &store = win.depot();
    const Grandeur *q = store.ws.doc.obtient(of);
    if (!q)
        return;
    const QString name = q->name;
    Box box(win, u"Incertitude"_s, u"Créer"_s, 380, "plus");
    box.layout->addWidget(new QLabel(u"Incertitude-type u(%1), rattachée à « %1 » :"_s.arg(name), box.dlg));
    if (q->uncertainty) {
        box.layout->addWidget(etiquette_attention(u"« %1 » est actuellement l'incertitude de %2 : la nouvelle la remplacera."_s.arg(store.ws.doc.nom(*q->uncertainty), name), box.dlg));
    }
    auto *methode = new QComboBox(box.dlg);
    methode->addItems({u"Incertitude-type connue ou formule"_s, u"Lecture sur une graduation"_s, u"Tolérance ±a du constructeur"_s});
    box.formulaire()->addRow(u"Origine de l’incertitude"_s, methode);
    auto *row = new QHBoxLayout;
    auto *lbl = new QLabel(u"u_%1 ="_s.arg(name), box.dlg);
    lbl->setFont(theme::police_fixe());
    row->addWidget(lbl);
    auto *formula = champ_formule(QString(), u"0.05   ou   (0.02*abs(%1) + 0.01)/sqrt(3)"_s.arg(name), box.dlg);
    row->addWidget(formula, 1);
    box.layout->addLayout(row);
    auto *aide = etiquette_attenuee(u"Saisissez l’incertitude-type dans l’unité de la grandeur. Une constante s’applique à toutes les lignes."_s, box.dlg);
    box.layout->addWidget(aide);
    QObject::connect(methode, &QComboBox::currentIndexChanged, box.dlg, [=](int index) {
        lbl->setText(index == 1 ? u"Pas de graduation Δ ="_s : index == 2 ? u"Tolérance a ="_s : u"u_%1 ="_s.arg(name));
        formula->setPlaceholderText(index == 0 ? u"Valeur ou formule"_s : u"Valeur positive dans l’unité de la grandeur"_s);
        if (index == 1)
            aide->setText(u"Entrez l’écart entre deux graduations. Pour une lecture à la graduation la plus proche, MeVReg calcule u = Δ/√12 (distribution uniforme)."_s);
        else if (index == 2)
            aide->setText(u"Entrez la demi-largeur a de l’intervalle ±a. Pour une distribution uniforme, MeVReg calcule u = a/√3."_s);
        else
            aide->setText(u"Saisissez directement l’incertitude-type dans l’unité de la grandeur, ou une formule qui la calcule."_s);
    });
    auto *bars = new QCheckBox(u"Afficher les barres d'incertitude sur les graphes"_s, box.dlg);
    bars->setChecked(true);
    box.layout->addWidget(bars);
    box.annule();
    box.sur_ok([&win, of, formula, bars, methode]() -> QString {
        Store &st = win.depot();
        QString fx = texte_formule(formula->text());
        if (fx.isEmpty())
            return u"Saisissez la valeur ou la formule de l'incertitude."_s;
        if (methode->currentIndex() != 0)
            fx = u"(%1)/sqrt(%2)"_s.arg(fx).arg(methode->currentIndex() == 1 ? 12 : 3);
        Document test = st.ws.doc;
        auto r = test.cree_incertitude(of, fx);
        if (!r)
            return r.error;
        if (const Grandeur *u = test.obtient(*r); u && !u->error.isEmpty())
            return u->error;
        const bool showBars = bars->isChecked();
        st.edite([&](Seance &ws) {
            auto id = ws.doc.cree_incertitude(of, fx);
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
                montre_dans_tableur(ws, *id);
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
    box.montre();
    formula->setFocus();
}

void remplit(MainWindow &win, QId qid)
{
    Store &store = win.depot();
    const Grandeur *q = store.ws.doc.obtient(qid);
    if (!q || q->est_calculee())
        return;
    Box box(win, u"Suite de valeurs"_s, u"Remplir"_s, 380, "numbers");
    box.layout->addWidget(etiquette_forte(u"Remplir « %1 »"_s.arg(q->name), box.dlg));
    auto *fields = new FillFields(store.style(), box.dlg);
    box.layout->addWidget(fields);
    if (const size_t n = q->effectif(); n > 0)
        box.layout->addWidget(etiquette_attention(u"Les %1 valeurs actuelles seront remplacées (Ctrl+Z pour revenir en arrière)."_s.arg(n), box.dlg));
    QPushButton *ok = box.ok;
    fields->changed = [fields, ok] { ok->setEnabled(fields->valeurs().ok()); };
    fields->changed();
    box.annule();
    box.sur_ok([&win, qid, fields]() -> QString {
        const series::Result r = fields->valeurs();
        if (!r.ok())
            return r.error;
        QString error;
        win.depot().edite([&](Seance &ws) { error = ws.doc.definit_valeurs(qid, r.values); });
        return error;
    });
    box.montre();
}

void confirme_suppression(MainWindow &win, QId qid)
{
    Store &store = win.depot();
    const Grandeur *q = store.ws.doc.obtient(qid);
    if (!q)
        return;
    Box box(win, u"Supprimer une grandeur"_s, u"Supprimer"_s, 380, "trash");
    box.ok->setIcon(icons::obtient("trash"));
    QPalette p = box.ok->palette();
    p.setColor(QPalette::ButtonText, theme::couleur_erreur());
    box.ok->setPalette(p);
    box.ok->setDefault(false);
    box.ok->setAutoDefault(false);
    box.layout->addWidget(new QLabel(u"Supprimer définitivement la grandeur « %1 » ?"_s.arg(q->name), box.dlg));
    const auto usages = store.ws.usages(qid);
    if (!usages.empty()) {
        QStringList items;
        for (const auto &u : usages)
            items << u"<li>%1</li>"_s.arg(u.texte().toHtmlEscaped());
        auto *l = new QLabel(u"Elle est utilisée dans :<ul style='margin-top:2px'>%1</ul>"_s.arg(items.join(QString())), box.dlg);
        l->setTextFormat(Qt::RichText);
        box.layout->addWidget(l);
        box.layout->addWidget(etiquette_attenuee(u"Les courbes qui l'utilisent seront retirées ; les formules qui la citent seront en erreur. Ctrl+Z annule la suppression."_s, box.dlg));
    }
    box.annule();
    // « Annuler » par défaut : Entrée ne supprime rien
    for (QAbstractButton *b : box.buttons->buttons())
        if (box.buttons->buttonRole(b) == QDialogButtonBox::RejectRole)
            if (auto *pb = qobject_cast<QPushButton *>(b))
                pb->setDefault(true);
    box.sur_ok([&win, qid]() -> QString {
        win.depot().edite([&](Seance &ws) { ws.supprime_grandeur(qid); });
        return {};
    });
    box.montre();
}

void modifie_grandeur(MainWindow &win, QId qid, bool convertir)
{
    Store &store = win.depot();
    const Grandeur *q = store.ws.doc.obtient(qid);
    if (!q)
        return;
    Box box(win, (convertir ? u"Convertir « %1 » en grandeur calculée"_s : u"Grandeur « %1 »"_s).arg(q->name),
            convertir ? u"Convertir"_s : u"Valider"_s);
    if (convertir) box.ok->setIcon(icons::obtient("function"));
    QFormLayout *f = box.formulaire();
    auto *name = new QLineEdit(q->name, box.dlg);
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = champ_unite(q->unit, f, box.dlg);
    auto *formula = champ_formule(q->formula.value_or(QString()), u"Exemple : 2*t + 1"_s, box.dlg);
    formula->setObjectName(u"formuleGrandeur"_s);
    f->addRow(q->formula || convertir ? u"Formule"_s : u"Formule (facultative)"_s, formula);
    if (!q->formula)
        box.layout->addWidget(etiquette_attenuee(u"Une formule remplace les mesures de toute la colonne. Ctrl+Z permet de les retrouver."_s, box.dlg));
    if (const Grandeur *owner = store.ws.doc.proprietaire_incertitude(qid))
        box.layout->addWidget(etiquette_attenuee(u"Incertitude-type de « %1 »."_s.arg(owner->name), box.dlg));
    box.annule();
    box.sur_ok([&win, qid, name, unit, formula, convertir]() -> QString {
        Store &st = win.depot();
        const Grandeur *cur = st.ws.doc.obtient(qid);
        if (!cur)
            return {};
        const QString old = cur->name;
        // tout est essayé sur une copie : rien ne change si une partie échoue
        Seance w = st.ws;
        const QString n = name->text().trimmed();
        bool renamed = false;
        if (n != old) {
            if (const QString e = w.renomme_grandeur(qid, n); !e.isEmpty())
                return e;
            renamed = true;
        }
        w.doc.definit_unite(qid, unit->text().trimmed());
        const QString expression = texte_formule(formula->text());
        if (expression.isEmpty() && (convertir || cur->formula))
            return u"Saisissez une formule pour calculer cette grandeur."_s;
        if (!expression.isEmpty()) {
            if (const QString e = w.doc.definit_formule(qid, expression); !e.isEmpty())
                return e;
            if (const Grandeur *nq = w.doc.obtient(qid); nq && !nq->error.isEmpty())
                return u"formule : "_s + nq->error;
        }
        st.edite([&](Seance &ws) { ws = std::move(w); });
        if (renamed)
            avertit_scripts(win, old);
        return {};
    });
    box.montre();
    if (convertir) formula->setFocus();
}

void tableur_guide(MainWindow &win)
{
    Box box(win, u"Nouveau tableur de mesures"_s, u"Créer le tableur"_s, 380, "table");
    auto *form = box.formulaire();
    auto *x = new QLineEdit(box.dlg), *ux = new QLineEdit(box.dlg);
    auto *y = new QLineEdit(box.dlg), *uy = new QLineEdit(box.dlg);
    x->setPlaceholderText(u"Par exemple t"_s);
    ux->setPlaceholderText(u"s"_s);
    y->setPlaceholderText(u"Par exemple z"_s);
    uy->setPlaceholderText(u"m"_s);
    form->addRow(u"Première grandeur"_s, x);
    form->addRow(u"Unité"_s, ux);
    form->addRow(u"Deuxième grandeur"_s, y);
    form->addRow(u"Unité"_s, uy);
    box.annule();
    box.sur_ok([&win, x, ux, y, uy] {
        auto &store = win.depot();
        const QString nx = x->text().trimmed(), ny = y->text().trimmed();
        for (const auto &nom : {nx, ny}) {
            const QString erreur = store.ws.doc.verifie_nom(nom);
            if (!erreur.isEmpty())
                return erreur;
        }
        if (nx == ny)
            return u"Donnez un nom différent à chaque grandeur."_s;
        ViewId table = 0;
        store.edite([&](Seance &ws) {
            const QId a = *ws.doc.ajoute_experimentale(nx, ux->text(), {});
            const QId b = *ws.doc.ajoute_experimentale(ny, uy->text(), {});
            table = ws.ajoute_tableur({a, b});
        });
        win.ouvre_onglet({TabKey::Table, table});
        return QString();
    });
    box.montre();
}

void nouvelle_colonne(MainWindow &win, ViewId table)
{
    Store &store = win.depot();
    Box box(win, u"Nouvelle grandeur"_s, u"Créer"_s, 380, "plus");
    QFormLayout *f = box.formulaire();
    auto *name = new QLineEdit(box.dlg);
    name->setPlaceholderText(u"V, t, pH…"_s);
    f->addRow(u"Nom"_s, name);
    QLineEdit *unit = champ_unite(QString(), f, box.dlg);
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
    box.layout->addWidget(etiquette_attenuee(u"Pour une grandeur calculée par une formule : bouton « Calculée » du tableur."_s, box.dlg));
    box.annule();
    box.sur_ok([&win, table, name, unit, fill, withFill]() -> QString {
        std::vector<double> values;
        if (withFill->isChecked()) {
            const series::Result r = fill->valeurs();
            if (!r.ok())
                return r.error;
            values = r.values;
        }
        QString error;
        win.depot().edite([&](Seance &ws) {
            auto id = ws.doc.ajoute_experimentale(name->text().trimmed(), unit->text().trimmed(), std::move(values));
            if (!id) {
                error = id.error;
                return;
            }
            if (TableView *t = ws.tableur_modifiable(table))
                t->columns.push_back(*id);
            else
                montre_dans_tableur(ws, *id);
        });
        return error;
    });
    box.montre();
}

void derivee(MainWindow &win, bool second, std::optional<Axes> axes)
{
    Store &store = win.depot();
    Box box(win, u"Calculer une dérivée"_s, u"Calculer"_s, 380, "derivative");
    QFormLayout *f = box.formulaire();
    auto *ordre = new QComboBox(box.dlg);
    ordre->setObjectName(u"ordreDerivee"_s);
    ordre->addItems({u"Dérivée première"_s, u"Dérivée seconde"_s});
    ordre->setCurrentIndex(second ? 1 : 0);
    f->addRow(u"Ordre"_s, ordre);
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    const auto [dx, dy] = paire_par_defaut(store.ws, axes);
    x->definit_actuel(dx);
    y->definit_actuel(dy);
    f->addRow(u"Dériver"_s, y);
    f->addRow(u"par rapport à"_s, x);
    auto *name = new QLineEdit(box.dlg);
    f->addRow(u"Nom"_s, name);
    auto hint = [&store, x, y, name, ordre] {
        const QString yn = y->actuel() ? store.ws.doc.nom(*y->actuel()) : QString();
        const QString xn = x->actuel() ? store.ws.doc.nom(*x->actuel()) : QString();
        name->setPlaceholderText(ordre->currentIndex() == 1 ? (u"d2%1_d%2"_s.arg(yn, xn) + u"2"_s) : u"d%1_d%2"_s.arg(yn, xn));
    };
    QObject::connect(x, &QuantityCombo::choisi, name, hint);
    QObject::connect(y, &QuantityCombo::choisi, name, hint);
    QObject::connect(ordre, &QComboBox::currentIndexChanged, name, hint);
    hint();
    auto *graph = new QCheckBox(u"Tracer dans un nouveau graphe"_s, box.dlg);
    graph->setChecked(true);
    box.layout->addWidget(graph);
    box.layout->addWidget(etiquette_attenuee(u"À l’intérieur de la série, la pente utilise les mesures voisines. Aux extrémités, elle est estimée d’un seul côté et est moins précise."_s, box.dlg));
    box.annule();
    box.sur_ok([&win, x, y, name, graph, ordre]() -> QString {
        const bool second = ordre->currentIndex() == 1;
        Store &st = win.depot();
        const auto xi = x->actuel(), yi = y->actuel();
        if (!xi || !yi || *xi == *yi)
            return u"Choisissez deux grandeurs différentes."_s;
        const auto *sourceX = st.ws.doc.obtient(*xi), *sourceY = st.ws.doc.obtient(*yi);
        if (!sourceX || !sourceY) return u"Ces grandeurs ne sont plus disponibles."_s;
        if (numeric::indices_valides(sourceY->values, sourceX->values).size() < (second ? 3u : 2u))
            return second ? u"Il faut au moins trois couples de valeurs pour une dérivée seconde."_s
                          : u"Il faut au moins deux couples de valeurs pour dériver."_s;
        const auto pentes = numeric::gradient(sourceY->values, sourceX->values);
        if (std::none_of(pentes.begin(), pentes.end(), [](double v) { return std::isfinite(v); }))
            return u"Il faut des abscisses distinctes pour calculer une pente."_s;
        QString base = name->text().trimmed();
        if (base.isEmpty())
            base = second ? (u"d2%1_d%2"_s.arg(sourceY->name, sourceX->name) + u"2"_s) : u"d%1_d%2"_s.arg(sourceY->name, sourceX->name);
        const QString nm = st.ws.doc.nom_unique(csv::nettoie_nom(base, u"d"_s));
        QString formula, unit;
        if (second) {
            formula = u"deriv(deriv(%1, %2), %2)"_s.arg(sourceY->name, sourceX->name);
            unit = rapport_unites(sourceY->unit, sourceX->unit.trimmed().isEmpty() ? QString() : sourceX->unit.trimmed() + u"^2"_s);
        } else {
            formula = u"deriv(%1, %2)"_s.arg(sourceY->name, sourceX->name);
            unit = rapport_unites(sourceY->unit, sourceX->unit);
        }
        QString error;
        std::optional<QId> created;
        st.edite([&](Seance &ws) {
            auto r = ws.doc.ajoute_calculee(nm, unit, formula);
            if (!r) {
                error = r.error;
                return;
            }
            created = *r;
            montre_avec_mesures(ws, *r, *xi, *yi);
        });
        if (!created)
            return error;
        if (graph->isChecked())
            win.nouveau_graphe(std::pair(*xi, *created));
        st.message(u"%1 = %2"_s.arg(nm, formula));
        return {};
    });
    box.montre();
}

void integrale(MainWindow &win, std::optional<Axes> axes)
{
    Store &store = win.depot();
    const NumberStyle style = store.style();
    Box box(win, u"Intégrale"_s, u"Créer la primitive"_s, 380, "integral");
    QFormLayout *f = box.formulaire();
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    const auto [dx, dy] = paire_par_defaut(store.ws, axes);
    x->definit_actuel(dx);
    y->definit_actuel(dy);
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
    a->definit_style(style);
    b->definit_style(style);
    row->addWidget(new QLabel(u"Aire entre"_s, areaBox));
    row->addWidget(a);
    row->addWidget(new QLabel(u"et"_s, areaBox));
    row->addWidget(b);
    row->addStretch();
    al->addLayout(row);
    auto *area = etiquette_forte(QString(), areaBox);
    al->addWidget(area);
    box.layout->addWidget(areaBox);
    auto bounds = [&store, x, a, b] {
        if (!x->actuel())
            return;
        if (auto r = numeric::plage_finie(store.ws.doc.valeurs(*x->actuel()))) {
            a->definit_valeur(r->first);
            b->definit_valeur(r->second);
        }
    };
    auto compute = [&store, x, y, a, b, area, name, style] {
        const Grandeur *xq = x->actuel() ? store.ws.doc.obtient(*x->actuel()) : nullptr;
        const Grandeur *yq = y->actuel() ? store.ws.doc.obtient(*y->actuel()) : nullptr;
        name->setPlaceholderText(yq ? u"int_"_s + yq->name : QString());
        if (!xq || !yq) {
            area->clear();
            return;
        }
        const double s = numeric::aire_entre(yq->values, xq->values, a->valeur(), b->valeur());
        const QString unit = unite_lisible(produit_unites(yq->unit, xq->unit));
        area->setText((u"Aire = %1 %2"_s.arg(format_significatif(s, 5, style), unit)).trimmed());
    };
    QObject::connect(x, &QuantityCombo::choisi, area, [bounds, compute] {
        bounds();
        compute();
    });
    QObject::connect(y, &QuantityCombo::choisi, area, compute);
    QObject::connect(a, &NumberEdit::valeur_modifiee, area, compute);
    QObject::connect(b, &NumberEdit::valeur_modifiee, area, compute);
    bounds();
    compute();
    box.layout->addWidget(etiquette_attenuee(u"La primitive (méthode des trapèzes) est nulle au premier point."_s, box.dlg));
    box.annule(u"Fermer"_s);
    box.sur_ok([&win, x, y, name]() -> QString {
        Store &st = win.depot();
        const auto xi = x->actuel(), yi = y->actuel();
        if (!xi || !yi)
            return u"Choisissez les deux grandeurs."_s;
        const auto *sourceX = st.ws.doc.obtient(*xi), *sourceY = st.ws.doc.obtient(*yi);
        if (!sourceX || !sourceY) return u"Ces grandeurs ne sont plus disponibles."_s;
        QString base = name->text().trimmed();
        if (base.isEmpty())
            base = u"int_"_s + sourceY->name;
        const QString nm = st.ws.doc.nom_unique(csv::nettoie_nom(base, u"I"_s));
        const QString unite = produit_unites(sourceY->unit, sourceX->unit);
        const QString formule = u"integ(%1, %2)"_s.arg(sourceY->name, sourceX->name);
        QString error;
        st.edite([&](Seance &ws) {
            auto r = ws.doc.ajoute_calculee(nm, unite, formule);
            if (!r)
                error = r.error;
            else
                montre_avec_mesures(ws, *r, *xi, *yi);
        });
        if (error.isEmpty())
            st.message(u"Primitive %1 créée"_s.arg(nm));
        return error;
    });
    box.montre();
}

void spectre(MainWindow &win)
{
    Store &store = win.depot();
    const NumberStyle style = store.style();
    Box box(win, u"Analyse de Fourier"_s, u"Calculer le spectre"_s, 380, "wave");
    QFormLayout *f = box.formulaire();
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    const auto [dx, dy] = paire_par_defaut(store.ws);
    x->definit_actuel(dx);
    y->definit_actuel(dy);
    f->addRow(u"Signal"_s, y);
    f->addRow(u"Temps"_s, x);
    auto *rect = new QRadioButton(fft::nom_fenetre(fft::Window::Rectangular), box.dlg);
    auto *hann = new QRadioButton(fft::nom_fenetre(fft::Window::Hann), box.dlg);
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
    auto *info = etiquette_attenuee(QString(), box.dlg);
    box.layout->addWidget(info);
    auto *uneven = etiquette_attention(QString(), box.dlg);
    box.layout->addWidget(uneven);
    auto *resample = new QCheckBox(u"Rééchantillonner à pas constant (interpolation linéaire)"_s, box.dlg);
    box.layout->addWidget(resample);
    auto update = [&store, x, y, info, uneven, resample, style] {
        const Grandeur *xq = x->actuel() ? store.ws.doc.obtient(*x->actuel()) : nullptr;
        const Grandeur *yq = y->actuel() ? store.ws.doc.obtient(*y->actuel()) : nullptr;
        const auto temps = xq && yq ? numeric::paires_triees(xq->values, yq->values).first : std::vector<double>{};
        const auto si = numeric::infos_pas(temps);
        if (!si) {
            info->clear();
            uneven->hide();
            resample->hide();
            return;
        }
        const auto [dt, dev] = *si;
        const QString unit = unite_lisible(xq->unit);
        const auto secondes = secondes_par_unite(xq->unit);
        const QString funit = secondes ? u"Hz"_s : unite_lisible(rapport_unites(QString(), xq->unit));
        info->setText(u"Pas d'échantillonnage : %1 %2 ; fréquence d'échantillonnage : %3 %4 ; %5 points"_s.arg(format_significatif(dt, 4, style), unit,
                                                                                                                 format_significatif(1.0 / (dt * secondes.value_or(1.0)), 4, style), funit)
                          .arg(temps.size()));
        const bool bad = dev > 0.01;
        uneven->setText(u"⚠ Pas non constant (écart jusqu'à %1 %)."_s.arg(format_fixe(dev * 100.0, 1, style)));
        if (bad && temps.size() < std::max(xq->values.size(), yq->values.size())) {
            size_t ligne = 0;
            while (std::isfinite(xq->valeur(ligne)) && std::isfinite(yq->valeur(ligne)))
                ++ligne;
            uneven->setText(uneven->text() + u" Première mesure incomplète à la ligne %1. Corrigez la case ou choisissez l’interpolation."_s.arg(ligne + 1));
        }
        uneven->setVisible(bad);
        resample->setVisible(bad);
    };
    QObject::connect(x, &QuantityCombo::choisi, info, update);
    QObject::connect(y, &QuantityCombo::choisi, info, update);
    update();
    box.annule();
    box.sur_ok([&win, x, y, hann, pad, resample]() -> QString {
        Store &st = win.depot();
        const auto xi = x->actuel(), yi = y->actuel();
        if (!xi || !yi || *xi == *yi)
            return u"Choisissez le signal et le temps."_s;
        const auto *temps = st.ws.doc.obtient(*xi);
        const auto *signal = st.ws.doc.obtient(*yi);
        if (!temps || !signal)
            return u"Ces grandeurs ne sont plus disponibles."_s;

        const auto secondes = secondes_par_unite(temps->unit);
        const QString uniteFrequence = secondes ? u"Hz"_s : rapport_unites(QString(), temps->unit);
        const QString nomSignal = signal->name, uniteSignal = signal->unit;
        const bool reechantillonner = resample->isVisible() && resample->isChecked();
        // Utiliser directement l'acquisition. Seule une conversion d'unité
        // ou une demande de rééchantillonnage nécessite un tableau de travail.
        std::vector<double> tempsConvertis, signalReechantillonne;
        if (reechantillonner) {
            std::tie(tempsConvertis, signalReechantillonne) =
                numeric::reechantillonne_uniforme(temps->values, signal->values, temps->effectif());
        }
        const bool convertir = secondes && *secondes != 1.0;
        if (convertir) {
            if (!reechantillonner)
                tempsConvertis = temps->values;
            for (double &instant : tempsConvertis)
                instant *= *secondes;
        }
        fft::SpectrumOptions opts;
        opts.window = hann->isChecked() ? fft::Window::Hann : fft::Window::Rectangular;
        opts.zeroPadding = pad->currentData().toInt();
        QString error;
        auto spectre = fft::spectre_amplitude(reechantillonner || convertir ? tempsConvertis : temps->values,
                                             reechantillonner ? signalReechantillonne : signal->values, opts, &error);
        if (!spectre)
            return error;
        ViewId gid = 0;
        st.edite([&](Seance &ws) {
            const QString fname = ws.doc.nom_unique(u"f"_s);
            const QString aname = ws.doc.nom_unique(csv::nettoie_nom(u"A_"_s + nomSignal, u"A"_s));
            const QId fq = *ws.doc.ajoute_experimentale(fname, uniteFrequence, std::move(spectre->freq));
            const QId aq = *ws.doc.ajoute_experimentale(aname, uniteSignal, std::move(spectre->amplitude));
            const ViewId tid = ws.ajoute_tableur({fq, aq});
            ws.tableur_modifiable(tid)->name = u"Spectre de %1"_s.arg(nomSignal);
            gid = ws.ajoute_graphe();
            GraphView *g = ws.graphe_modifiable(gid);
            g->name = u"Spectre de %1"_s.arg(nomSignal);
            Plot &p = g->plots[0];
            p.title.text = u"Spectre en amplitude de %1"_s.arg(nomSignal);
            p.y.includeZero = true;
            p.x.includeZero = true;
            CurveSpec &c = p.ajoute_courbe(fq, aq);
            c.marker = Marker::None;
            c.line = LineKind::Straight;
            c.lineWidth = 1.2;
        });
        win.ouvre_onglet(TabKey{TabKey::Graph, gid});
        return {};
    });
    box.montre();
}

std::vector<std::pair<CourbeTracee, QString>> courbes_visibles(const GraphView &graphe, const Document &doc)
{
    std::vector<std::pair<CourbeTracee, QString>> courbes;
    for (int i = 0; i < graphe.traces_visibles(); ++i) {
        for (const auto &c : graphe.plots[i].curves) {
            if (!c.visible || !doc.obtient(c.x) || !doc.obtient(c.y)) continue;
            QString nom = u"%1 en fonction de %2"_s.arg(doc.nom(c.y), doc.nom(c.x));
            if (graphe.traces_visibles() > 1) nom = u"Panneau %1 · %2"_s.arg(i + 1).arg(nom);
            if (c.side == YSide::Right) nom += u" (axe droit)"_s;
            courbes.emplace_back(CourbeTracee{i, c.x, c.y, c.side}, nom);
        }
    }
    return courbes;
}

void tangentes(MainWindow &win, std::optional<ViewId> graphId, CourbeTracee cible)
{
    Store &store = win.depot();
    if (store.ws.graphs.empty()) {
        store.message(u"Tracez d’abord la courbe sur laquelle construire les tangentes."_s, icons::Status::Warning);
        return;
    }
    Box box(win, u"Méthode des tangentes parallèles"_s, u"Tracer sur le graphe"_s, 380, "tangent");
    auto *form = box.formulaire();
    auto *graph = liste_graphes(store.ws, false, box.dlg);
    if (graphId) graph->setCurrentIndex(std::max(0, graph->findData(QVariant::fromValue<qulonglong>(*graphId))));
    form->addRow(u"Graphe"_s, graph);
    auto *curve = new QComboBox(box.dlg);
    form->addRow(u"Courbe"_s, curve);
    auto *result = etiquette_forte({}, box.dlg);
    box.layout->addWidget(result);
    auto *erreurCourbe = etiquette_erreur(box.dlg);
    box.layout->addWidget(erreurCourbe);
    box.layout->addWidget(etiquette_attenuee(u"Sur une courbe en S, les deux tangentes de même pente encadrent la zone de plus forte variation. La médiane est la parallèle à mi-distance. Vous pouvez déplacer les points de contact sur le graphe."_s, box.dlg));
    auto evalue = [&store, graph, curve](TangentSpec &t, QString &erreur) -> std::optional<titration::Tangents> {
        const auto cible = curve->currentData().value<CourbeTracee>();
        const auto *g = store.ws.graphe(graph->currentData().toULongLong());
        if (g && cible.x && cible.panneau >= 0 && cible.panneau < g->traces_visibles()) {
            for (const auto &c : g->plots[cible.panneau].curves) {
                if (!c.visible || c.x != cible.x || c.y != cible.y || c.side != cible.cote) continue;
                t.x = c.x;
                t.y = c.y;
                t.side = c.side;
                if (const auto r = calcule_tangentes(t, store.ws.doc, &erreur)) return r->second;
                return std::nullopt;
            }
        }
        erreur = u"Choisissez une courbe visible sur le graphe."_s;
        return std::nullopt;
    };
    auto preview = [&store, evalue, result, err = erreurCourbe, ok = box.ok] {
        TangentSpec t;
        QString erreur;
        const auto r = evalue(t, erreur);
        ok->setEnabled(r.has_value());
        montre_erreur(err, erreur);
        result->clear();
        if (!r) return;
        const auto &doc = store.ws.doc;
        auto coordonnee = [&](QId id, double valeur) {
            return u"%1 = %2 %3"_s.arg(doc.nom(id), format_significatif(valeur, 4, store.style()), unite_lisible(doc.obtient(id)->unit)).trimmed();
        };
        result->setText(u"Intersection de la médiane avec la courbe\n%1, %2"_s
                        .arg(coordonnee(t.x, r->equivalence.first), coordonnee(t.y, r->equivalence.second)));
    };
    auto remplit = [&store, graph, graphId, cible, curve, preview] {
        const QVariant avant = curve->currentData();
        QSignalBlocker bloque(curve);
        curve->clear();
        const auto *g = store.ws.graphe(graph->currentData().toULongLong());
        if (g) {
            int defaut = -1;
            for (const auto &[c, nom] : courbes_visibles(*g, store.ws.doc)) {
                if (g->id == graphId && c.panneau == cible.panneau && (c == cible || defaut < 0))
                    defaut = curve->count();
                curve->addItem(nom, QVariant::fromValue(c));
            }
            const int i = curve->findData(avant);
            if (i >= 0 || defaut >= 0) curve->setCurrentIndex(i >= 0 ? i : defaut);
        }
        preview();
    };
    QObject::connect(graph, &QComboBox::currentIndexChanged, curve, remplit);
    QObject::connect(curve, &QComboBox::currentIndexChanged, result, preview);
    QObject::connect(&store, &Store::espace_travail_change, curve, remplit);
    remplit();
    box.annule();
    box.sur_ok([&win, graph, curve, evalue]() -> QString {
        TangentSpec t;
        QString erreur;
        if (!evalue(t, erreur)) return erreur;
        const ViewId gid = graph->currentData().toULongLong();
        const int pi = curve->currentData().value<CourbeTracee>().panneau;
        win.depot().edite([&](Seance &ws) {
            Plot &p = ws.graphe_modifiable(gid)->plots[pi];
            std::erase_if(p.tangents, [&](const TangentSpec &autre) { return autre.x == t.x && autre.y == t.y && autre.side == t.side; });
            p.tangents.push_back(t);
        });
        win.ouvre_onglet(TabKey{TabKey::Graph, gid});
        return {};
    });
    box.montre();
}

void methode_derivee(MainWindow &win, std::optional<ViewId> graphe, CourbeTracee cible)
{
    Store &store = win.depot();
    const NumberStyle style = store.style();
    Box box(win, u"Maximum de pente en valeur absolue"_s, u"Appliquer"_s);
    QFormLayout *f = box.formulaire();
    auto *y = new QuantityCombo(store, false, box.dlg);
    auto *x = new QuantityCombo(store, false, box.dlg);
    const auto [dx, dy] = paire_par_defaut(store.ws, Axes{cible.x, cible.y});
    x->definit_actuel(dx);
    y->definit_actuel(dy);
    f->addRow(u"Ordonnée"_s, y);
    f->addRow(u"Abscisse"_s, x);
    auto *graph = liste_graphes(store.ws, true, box.dlg);
    f->addRow(u"Graphe"_s, graph);
    if (graphe && store.ws.graphe(*graphe)) {
        graph->setCurrentIndex(graph->findData(QVariant::fromValue<qulonglong>(*graphe)));
    } else {
        for (const auto &g : store.ws.graphs) {
            if (!dx || !dy || g.plots.empty()) continue;
            if (std::any_of(g.plots.front().curves.begin(), g.plots.front().curves.end(), [&](const auto &c) { return c.x == *dx && c.y == *dy; })) {
                graph->setCurrentIndex(graph->findData(QVariant::fromValue<qulonglong>(g.id)));
                break;
            }
        }
    }
    auto *result = etiquette_forte(QString(), box.dlg);
    box.layout->addWidget(result);
    auto *err = etiquette_erreur(box.dlg);
    box.layout->addWidget(err);
    box.layout->addWidget(etiquette_attenuee(u"Trace la dérivée sur l’autre axe des ordonnées et marque l’abscisse où sa valeur absolue présente un maximum intérieur."_s, box.dlg));
    auto preview = [&store, x, y, result, err, style, ok = box.ok] {
        ok->setEnabled(false);
        result->clear();
        montre_erreur(err, QString());
        const Grandeur *xq = x->actuel() ? store.ws.doc.obtient(*x->actuel()) : nullptr;
        const Grandeur *yq = y->actuel() ? store.ws.doc.obtient(*y->actuel()) : nullptr;
        if (!xq || !yq || xq == yq) {
            montre_erreur(err, u"Choisissez deux grandeurs différentes."_s);
            return;
        }
        QString e;
        if (auto m = titration::maximum_derivee(xq->values, yq->values, &e)) {
            ok->setEnabled(true);
            result->setText(u"Maximum de |d%1/d%2| en %2 = %3 %4"_s.arg(yq->name, xq->name, format_significatif(m->first, 4, style), unite_lisible(xq->unit)).trimmed());
        } else {
            montre_erreur(err, e);
        }
    };
    QObject::connect(x, &QuantityCombo::choisi, result, preview);
    QObject::connect(y, &QuantityCombo::choisi, result, preview);
    QObject::connect(&store, &Store::espace_travail_change, result, preview);
    preview();
    box.annule();
    box.sur_ok([&win, x, y, graph, graphe, cible]() -> QString {
        Store &st = win.depot();
        const auto xi = x->actuel(), yi = y->actuel();
        if (!xi || !yi || *xi == *yi)
            return u"Choisissez deux grandeurs différentes."_s;
        const auto *sourceX = st.ws.doc.obtient(*xi), *sourceY = st.ws.doc.obtient(*yi);
        if (!sourceX || !sourceY) return u"Ces grandeurs ne sont plus disponibles."_s;
        QString e;
        const auto m = titration::maximum_derivee(sourceX->values, sourceY->values, &e);
        if (!m)
            return e;
        const ViewId chosen = graph->currentData().toULongLong();
        ViewId gid = 0;
        const QString nomDerivee = csv::nettoie_nom(u"d%1_d%2"_s.arg(sourceY->name, sourceX->name), u"d"_s);
        const QString formule = u"deriv(%1, %2)"_s.arg(sourceY->name, sourceX->name);
        const QString unite = rapport_unites(sourceY->unit, sourceX->unit);
        st.edite([&](Seance &ws) {
            const auto &grandeurs = ws.doc.quantities;
            const auto trouve = std::find_if(grandeurs.begin(), grandeurs.end(), [&](const Grandeur &q) {
                return q.formula == formule && q.unit == unite;
            });
            QId dq;
            if (trouve != grandeurs.end()) {
                dq = trouve->id;
            } else {
                dq = *ws.doc.ajoute_calculee(ws.doc.nom_unique(nomDerivee), unite, formule);
                montre_avec_mesures(ws, dq, *xi, *yi);
            }
            gid = chosen && ws.graphe(chosen) ? chosen : 0;
            if (!gid) {
                gid = ws.ajoute_graphe();
                ws.graphe_modifiable(gid)->plots[0].ajoute_courbe(*xi, *yi);
            }
            auto *g = ws.graphe_modifiable(gid);
            const int pi = gid == graphe && cible.panneau >= 0 && cible.panneau < g->traces_visibles() ? cible.panneau : 0;
            Plot &p = g->plots[pi];
            YSide source = YSide::Left;
            for (const auto &c : p.curves) {
                if (!c.visible || c.x != *xi || c.y != *yi) continue;
                source = c.side;
                if (gid != graphe || *xi != cible.x || *yi != cible.y || source == cible.cote) break;
            }
            const YSide oppose = source == YSide::Left ? YSide::Right : YSide::Left;
            const auto existante = std::find_if(p.curves.begin(), p.curves.end(), [&](const CurveSpec &c) {
                return c.x == *xi && c.y == dq && c.side == oppose;
            });
            CurveSpec &c = existante == p.curves.end() ? p.ajoute_courbe(*xi, dq) : *existante;
            c.side = oppose;
            c.marker = Marker::None;
            c.line = LineKind::Straight;
            c.dash = Dash::Dashed;
            c.dosage = *yi;
        });
        win.ouvre_onglet(TabKey{TabKey::Graph, gid});
        return {};
    });
    box.montre();
}

void meta(MainWindow &win)
{
    const Meta m = win.depot().ws.meta;
    Box box(win, u"Informations du document"_s, u"Valider"_s, 440);
    box.layout->addWidget(etiquette_attenuee(u"Ces informations figurent en tête des documents PDF exportés et imprimés."_s, box.dlg));
    QFormLayout *f = box.formulaire();
    auto *title = new QLineEdit(m.title, box.dlg);
    auto *authors = new QLineEdit(m.authors, box.dlg);
    auto *klass = new QLineEdit(m.klass, box.dlg);
    auto *notes = new QPlainTextEdit(m.notes, box.dlg);
    theme::definit_hauteur_fixe(notes, 90);
    title->setPlaceholderText(u"Titre de votre étude"_s);
    authors->setPlaceholderText(u"Noms des auteurs"_s);
    f->addRow(u"Titre"_s, title);
    f->addRow(u"Auteurs"_s, authors);
    f->addRow(u"Groupe ou classe"_s, klass);
    f->addRow(u"Notes"_s, notes);
    box.annule();
    box.sur_ok([&win, title, authors, klass, notes]() -> QString {
        Meta n{title->text(), authors->text(), klass->text(), notes->toPlainText()};
        if (!(n == win.depot().ws.meta))
            win.depot().edite([&](Seance &ws) { ws.meta = n; });
        return {};
    });
    box.montre();
}

void preferences(MainWindow &win)
{
    Store &store = win.depot();
    Box box(win, u"Préférences"_s, QString(), 440);
    // le thème clair ou sombre se choisit avec le bouton soleil / lune de la barre de menus ; la
    // virgule ou le point décimal suit la langue
    auto *darkPlots = new QCheckBox(u"Graphes sur fond sombre avec le thème sombre"_s, box.dlg);
    darkPlots->setChecked(store.prefs.darkPlots);
    darkPlots->setToolTip(u"Les exports et les impressions restent toujours sur fond blanc"_s);
    box.layout->addWidget(darkPlots);
    auto *confirm = new QCheckBox(u"Demander s'il faut cacher ou fermer un tableur, un graphe, un script ou un dessin"_s, box.dlg);
    confirm->setChecked(store.prefs.confirmClose);
    confirm->setToolTip(u"Case cochée : choisir entre cacher la fenêtre (elle reste dans le menu Fenêtres) et la fermer "
                        "(elle est retirée du document). Case décochée : la fenêtre est cachée."_s);
    box.layout->addWidget(confirm);
    QFormLayout *f = box.formulaire();
    auto *sig = new QSpinBox(box.dlg);
    sig->setRange(2, 15);
    sig->setValue(store.prefs.sigDigits);
    f->addRow(u"Chiffres significatifs des tableurs"_s, sig);
    auto reglage = [&](const QString &nom, const QString &titre, int min, int max, int valeur, const QString &suffixe) {
        auto *champ = new QSpinBox(box.dlg);
        champ->setObjectName(nom);
        champ->setRange(min, max);
        champ->setValue(valeur);
        champ->setSuffix(suffixe);
        f->addRow(titre, champ);
        return champ;
    };
    auto *zoom = reglage(u"zoomInterface"_s, u"Agrandissement de l’interface"_s, qRound(100 * UI_ZOOM_MIN),
                         qRound(100 * UI_ZOOM_MAX), qRound(100 * store.prefs.uiZoom), u" %"_s);
    zoom->setSingleStep(10);
    zoom->setToolTip(u"L’agrandissement affiché s’adapte à la largeur de la fenêtre."_s);
    auto *texte = reglage(u"texteTableur"_s, u"Taille du texte du tableur"_s, Prefs::texteMin, Prefs::texteMax, store.prefs.texteTableur, u" %"_s);
    texte->setSingleStep(5);
    auto *ligne = reglage(u"hauteurLigne"_s, u"Hauteur minimale des lignes"_s, Prefs::ligneMin, Prefs::ligneMax, store.prefs.hauteurLigne, u" px"_s);
    auto *apercu = new QLabel(u"12,50 mL"_s, box.dlg);
    apercu->setAlignment(Qt::AlignCenter);
    apercu->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
    f->addRow(u"Aperçu d’une cellule"_s, apercu);
    auto actualise = [&store, apercu] {
        const QFont police = TableGrid::police_lecture(store.prefs);
        apercu->setFont(police);
        apercu->setFixedHeight(TableGrid::hauteur_lecture(store.prefs, police));
    };
    actualise();
    QObject::connect(&store, &Store::affichage_change, apercu, actualise);
    QObject::connect(zoom, &QSpinBox::valueChanged, box.dlg, [&win](int v) { win.change_agrandissement(v / 100.0); });
    auto apply = [&store, darkPlots, confirm, sig, texte, ligne] {
        store.prefs.texteTableur = texte->value();
        store.prefs.hauteurLigne = ligne->value();
        store.prefs.darkPlots = darkPlots->isChecked();
        store.prefs.confirmClose = confirm->isChecked();
        store.prefs.sigDigits = sig->value();
        store.preferences_modifiees();
    };
    QObject::connect(darkPlots, &QCheckBox::toggled, box.dlg, apply);
    QObject::connect(confirm, &QCheckBox::toggled, box.dlg, apply);
    for (auto *champ : {sig, texte, ligne})
        QObject::connect(champ, &QSpinBox::valueChanged, box.dlg, apply);
    box.annule(u"Fermer"_s);
    box.montre();
}

void export_python(MainWindow &win)
{
    win.transmet_saisie_scripts();
    const QString text = pyexport::exporte_script(win.depot().ws);
    Box box(win, u"Exporter vers Python"_s, QString(), 680);
    box.dlg->resize(theme::px(QSize(760, 600)));
    box.layout->addWidget(etiquette_attenuee(u"Script numpy + matplotlib reproduisant les données, les calculs et les graphes (compatible Capytale)."_s, box.dlg));
    auto *edit = new CodeEditor(box.dlg);
    edit->setPlainText(text);
    edit->setReadOnly(true);
    new PythonHighlighter(edit->document());
    box.layout->addWidget(edit, 1);
    auto *copy = box.buttons->addButton(u"Copier"_s, QDialogButtonBox::ActionRole);
    copy->setIcon(icons::obtient("copy"));
    QObject::connect(copy, &QPushButton::clicked, box.dlg, [&win, text] {
        QGuiApplication::clipboard()->setText(text);
        win.depot().message(u"Script copié"_s);
    });
    auto *save = box.buttons->addButton(u"Enregistrer…"_s, QDialogButtonBox::ActionRole);
    save->setIcon(icons::obtient("save"));
    QDialog *d = box.dlg;
    QObject::connect(save, &QPushButton::clicked, box.dlg, [&win, text, d] {
        const QString base = win.chemin_actuel().isEmpty() ? u"mesures.py"_s : QFileInfo(win.chemin_actuel()).completeBaseName() + u".py"_s;
        QString path = QFileDialog::getSaveFileName(d, u"Enregistrer le script Python"_s, base, u"Script Python (*.py)"_s);
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += u".py"_s;
        const QByteArray bytes = text.toUtf8();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !ecrit_tout(file, bytes) || !file.commit()) {
            win.depot().message(u"Écriture impossible : %1"_s.arg(file.errorString()), icons::Status::Error);
            return;
        }
        win.depot().message(u"Script enregistré : %1"_s.arg(QFileInfo(path).fileName()));
    });
    box.annule(u"Fermer"_s);
    box.montre();
}

} // namespace dialogs
