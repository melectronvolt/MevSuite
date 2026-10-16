#include "app/csvimport.h"

#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/csv.h"
#include "core/format.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QPlainTextEdit>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <map>

using namespace Qt::StringLiterals;

namespace csvimport {

namespace {

/// Lignes du fichier montrées telles quelles, valeurs montrées dans l'aperçu.
constexpr int RAW_LINES = 300;
constexpr int PREVIEW_ROWS = 200;
/// Rangées de l'aperçu au-dessus des valeurs.
enum PreviewRow { Include, Name, Unit, FirstValue };

QString plural(qsizetype n)
{
    return n > 1 ? u"s"_s : QString();
}

QString quoted(const QString &s)
{
    return u"« "_s + s + u" »"_s;
}

QString html(const QColor &c, const QString &text)
{
    return u"<span style=\"color:%1\">%2</span>"_s.arg(c.name(), text);
}

QLabel *title(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    return l;
}

class ImportDialog : public QDialog {
public:
    ImportDialog(MainWindow &win, const QString &path, QStringList lines, std::optional<ViewId> table);

private:
    /// Réglages lus dans les champs, lecture refaite, aperçu et bilan revus.
    void reread();
    void showOptions(const csv::ReadOptions &o);
    void fillRaw();
    void fillPreview();
    void updateSummary();
    /// Colonne importée : choix de l'utilisateur, sinon toute colonne qui a des valeurs.
    bool included(int column) const;
    QString nameOf(int column) const;
    QString unitOf(int column) const;
    /// Colonnes à importer, sous leur nom définitif (rendu unique dans le document).
    csv::Parsed selection() const;
    void accept() override;

    MainWindow &m_win;
    QString m_path;
    QStringList m_lines;
    std::optional<ViewId> m_table;
    csv::ReadOptions m_detected, m_options;
    csv::Reading m_reading;
    /// Choix de l'utilisateur par colonne (cocher, renommer), oubliés si les colonnes changent.
    std::map<int, bool> m_include;
    std::map<int, QString> m_names, m_units;
    bool m_filling = false;

    QComboBox *m_separator;
    QComboBox *m_decimal;
    QSpinBox *m_nameLine;
    QSpinBox *m_unitLine;
    QSpinBox *m_firstLine;
    QCheckBox *m_skipEmpty;
    QPushButton *m_reset;
    QTreeWidget *m_raw;
    QTableWidget *m_preview;
    QLabel *m_summary;
    QPushButton *m_ok;
};

ImportDialog::ImportDialog(MainWindow &win, const QString &path, QStringList lines, std::optional<ViewId> table)
    : QDialog(&win), m_win(win), m_path(path), m_lines(std::move(lines)), m_table(table)
{
    setObjectName(u"csvImport"_s);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(u"Importer « %1 »"_s.arg(QFileInfo(path).fileName()));
    const Store &store = win.store();
    m_detected = csv::detect(m_lines, store.prefs.decimalComma ? csv::Decimal::Comma : csv::Decimal::Point);
    m_options = m_detected;

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    layout->addWidget(weakLabel(u"Vérifiez ce qui sera importé avant de valider : les réglages ci-dessous ont été devinés d'après le fichier."_s, this));

    // --- réglages de lecture ---
    layout->addWidget(title(u"Lecture du fichier"_s, this));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(theme::px(10));
    grid->setVerticalSpacing(6);
    m_separator = new QComboBox(this);
    m_separator->setObjectName(u"separator"_s);
    for (const auto &[text, c] : std::initializer_list<std::pair<QString, QChar>>{
             {u"Point-virgule  ;"_s, u';'}, {u"Virgule  ,"_s, u','}, {u"Tabulation"_s, u'\t'}, {u"Espaces"_s, csv::SPACES}})
        m_separator->addItem(c == m_detected.separator ? text + u"   (détecté)"_s : text, QVariant(c));
    m_separator->setToolTip(u"Signe qui sépare les valeurs d'une même ligne"_s);
    m_decimal = new QComboBox(this);
    m_decimal->setObjectName(u"decimal"_s);
    m_decimal->addItem(m_detected.decimal == csv::Decimal::Comma ? u"Virgule : 3,14   (détecté)"_s : u"Virgule : 3,14"_s);
    m_decimal->addItem(m_detected.decimal == csv::Decimal::Point ? u"Point : 3.14   (détecté)"_s : u"Point : 3.14"_s);
    m_decimal->setToolTip(u"Signe des nombres décimaux ; une case écrite avec l'autre signe n'est pas lue (elle apparaît en rouge)"_s);
    auto spin = [this](const QString &name, int min, const QString &none, const QString &tip) {
        auto *s = new QSpinBox(this);
        s->setObjectName(name);
        s->setRange(min, std::max(min, static_cast<int>(m_lines.size())));
        if (!none.isEmpty())
            s->setSpecialValueText(none);
        s->setToolTip(tip);
        return s;
    };
    m_nameLine = spin(u"nameLine"_s, 0, u"aucune"_s, u"Ligne du fichier qui donne les noms des grandeurs (« aucune » : c1, c2…)"_s);
    m_unitLine = spin(u"unitLine"_s, 0, u"aucune"_s, u"Ligne du fichier qui donne les unités ; elles peuvent aussi suivre les noms : « t (s) »"_s);
    m_firstLine = spin(u"firstLine"_s, 1, QString(), u"Les lignes au-dessus (sauf celles des noms et des unités) sont ignorées"_s);
    m_skipEmpty = new QCheckBox(u"Ignorer les lignes vides"_s, this);
    m_skipEmpty->setObjectName(u"skipEmpty"_s);
    m_skipEmpty->setToolTip(u"Sinon, une ligne vide donne une rangée de cases vides"_s);
    auto rowLabel = [this](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };
    grid->addWidget(rowLabel(u"Séparateur des valeurs"_s), 0, 0);
    grid->addWidget(m_separator, 0, 1);
    grid->addWidget(rowLabel(u"Séparateur décimal"_s), 1, 0);
    grid->addWidget(m_decimal, 1, 1);
    grid->addWidget(m_skipEmpty, 2, 1);
    grid->addWidget(rowLabel(u"Ligne des noms"_s), 0, 3);
    grid->addWidget(m_nameLine, 0, 4);
    grid->addWidget(rowLabel(u"Ligne des unités"_s), 1, 3);
    grid->addWidget(m_unitLine, 1, 4);
    grid->addWidget(rowLabel(u"Valeurs à partir de la ligne"_s), 2, 3);
    grid->addWidget(m_firstLine, 2, 4);
    grid->setColumnMinimumWidth(2, theme::px(24));
    grid->setColumnStretch(5, 1);
    m_reset = new QPushButton(u"Réglages détectés"_s, this);
    m_reset->setToolTip(u"Revenir aux réglages devinés d'après le fichier"_s);
    grid->addWidget(m_reset, 2, 6, Qt::AlignRight);
    layout->addLayout(grid);

    // --- le fichier, puis ce qui sera importé ---
    auto *split = new QSplitter(Qt::Vertical, this);
    split->setChildrenCollapsible(false);
    auto *top = new QWidget(split);
    auto *tl = new QVBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(title(u"Le fichier (%1 ligne%2)"_s.arg(m_lines.size()).arg(plural(m_lines.size())), top));
    m_raw = new QTreeWidget(top);
    m_raw->setObjectName(u"raw"_s);
    m_raw->setColumnCount(3);
    m_raw->setHeaderLabels({u"Ligne"_s, u"Lue comme"_s, u"Contenu (⇥ : tabulation)"_s});
    m_raw->setRootIsDecorated(false);
    m_raw->setUniformRowHeights(true);
    m_raw->setSelectionMode(QAbstractItemView::NoSelection);
    m_raw->setFocusPolicy(Qt::NoFocus);
    // colonnes réglables à la main (à la largeur de leur contenu au départ), la dernière prend le reste
    m_raw->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_raw->header()->setStretchLastSection(true);
    tl->addWidget(m_raw);
    auto *bottom = new QWidget(split);
    auto *bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(title(u"Ce qui sera importé"_s, bottom));
    bl->addWidget(weakLabel(u"Décochez une colonne pour ne pas l'importer ; double-clic sur un nom ou une unité pour le changer."_s, bottom));
    m_preview = new QTableWidget(bottom);
    m_preview->setObjectName(u"preview"_s);
    m_preview->setSelectionMode(QAbstractItemView::NoSelection);
    m_preview->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_preview->setWordWrap(false);
    m_preview->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_preview->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_preview->horizontalHeader()->setDefaultSectionSize(theme::px(110));
    bl->addWidget(m_preview, 1);
    split->addWidget(top);
    split->addWidget(bottom);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    layout->addWidget(split, 1);

    m_summary = new QLabel(this);
    m_summary->setObjectName(u"summary"_s);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(u"Importer"_s, QDialogButtonBox::AcceptRole);
    m_ok->setDefault(true);
    buttons->addButton(u"Annuler"_s, QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    showOptions(m_options);
    connect(m_separator, &QComboBox::currentIndexChanged, this, &ImportDialog::reread);
    connect(m_decimal, &QComboBox::currentIndexChanged, this, &ImportDialog::reread);
    for (QSpinBox *s : {m_nameLine, m_unitLine, m_firstLine})
        connect(s, &QSpinBox::valueChanged, this, &ImportDialog::reread);
    connect(m_skipEmpty, &QCheckBox::toggled, this, &ImportDialog::reread);
    connect(m_reset, &QPushButton::clicked, this, [this] {
        m_include.clear();
        m_names.clear();
        m_units.clear();
        showOptions(m_detected);
        reread();
    });
    connect(m_preview, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_filling)
            return;
        const int c = item->column();
        switch (item->row()) {
        case Include: m_include[c] = item->checkState() == Qt::Checked; break;
        case Name: m_names[c] = item->text().trimmed(); break;
        case Unit: m_units[c] = item->text().trimmed(); break;
        default: return;
        }
        // couleurs des colonnes et bilan revus (la lecture ne change pas), une fois le signal
        // passé : l'aperçu est refait, la case qui l'émet aussi
        QMetaObject::invokeMethod(this, [this] {
            m_reset->setEnabled(true);
            fillPreview();
            updateSummary();
        }, Qt::QueuedConnection);
    });
    reread();

    // grande, mais dans l'écran
    QSize size = theme::px(QSize(980, 700));
    if (const QScreen *screen = win.screen())
        size = size.boundedTo(screen->availableGeometry().size() - QSize(40, 80));
    resize(size);
}

void ImportDialog::showOptions(const csv::ReadOptions &o)
{
    m_filling = true;
    for (int i = 0; i < m_separator->count(); ++i)
        if (m_separator->itemData(i).value<QChar>() == o.separator)
            m_separator->setCurrentIndex(i);
    m_decimal->setCurrentIndex(o.decimal == csv::Decimal::Comma ? 0 : 1);
    m_nameLine->setValue(o.nameLine + 1);
    m_unitLine->setValue(o.unitLine + 1);
    m_firstLine->setValue(o.firstLine + 1);
    m_skipEmpty->setChecked(o.skipEmpty);
    m_filling = false;
}

void ImportDialog::reread()
{
    if (m_filling)
        return;
    csv::ReadOptions o;
    o.separator = m_separator->currentData().value<QChar>();
    // la virgule ne peut pas séparer à la fois les valeurs et les décimales
    auto *model = qobject_cast<QStandardItemModel *>(m_decimal->model());
    if (model)
        model->item(0)->setEnabled(o.separator != u',');
    if (o.separator == u',' && m_decimal->currentIndex() == 0) {
        m_filling = true;
        m_decimal->setCurrentIndex(1);
        m_filling = false;
    }
    o.decimal = m_decimal->currentIndex() == 0 ? csv::Decimal::Comma : csv::Decimal::Point;
    o.nameLine = m_nameLine->value() - 1;
    o.unitLine = m_unitLine->value() - 1;
    o.firstLine = m_firstLine->value() - 1;
    o.skipEmpty = m_skipEmpty->isChecked();
    // autres colonnes, ou autres en-têtes : les choix faits colonne par colonne ne valent plus
    if (o.separator != m_options.separator || o.nameLine != m_options.nameLine || o.unitLine != m_options.unitLine) {
        m_include.clear();
        m_names.clear();
        m_units.clear();
    }
    m_options = o;
    m_reading = csv::read(m_lines, m_options);
    m_reset->setEnabled(!(m_options == m_detected) || !m_include.empty() || !m_names.empty() || !m_units.empty());
    fillRaw();
    fillPreview();
    updateSummary();
}

void ImportDialog::fillRaw()
{
    m_raw->clear();
    const QColor weak = theme::weakColor(), accent = theme::accent();
    const int n = std::min(static_cast<int>(m_lines.size()), RAW_LINES);
    QList<QTreeWidgetItem *> items;
    for (int i = 0; i < n; ++i) {
        const QString &line = m_lines[i];
        const QStringList cells = csv::splitLine(line, m_options.separator);
        const bool empty = std::all_of(cells.begin(), cells.end(), [](const QString &c) { return c.isEmpty(); });
        QString role;
        bool header = false, ignored = false;
        if (i == m_options.nameLine) {
            role = u"noms"_s;
            header = true;
        } else if (i == m_options.unitLine) {
            role = u"unités"_s;
            header = true;
        } else if (i < m_options.firstLine) {
            role = u"ignorée"_s;
            ignored = true;
        } else if (empty) {
            role = m_options.skipEmpty ? u"vide, ignorée"_s : u"cases vides"_s;
            ignored = m_options.skipEmpty;
        } else {
            role = u"valeurs"_s;
        }
        auto *item = new QTreeWidgetItem;
        item->setText(0, QString::number(i + 1));
        item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(1, role);
        QString shown = line;
        shown.replace(u'\t', u" ⇥ "_s);
        item->setText(2, shown);
        item->setFont(2, theme::fixedFont());
        if (header) {
            QFont f = item->font(1);
            f.setWeight(QFont::DemiBold);
            item->setFont(1, f);
            item->setForeground(1, accent);
        }
        if (ignored)
            for (int c = 0; c < 3; ++c)
                item->setForeground(c, weak);
        items << item;
    }
    if (m_lines.size() > n) {
        auto *more = new QTreeWidgetItem;
        more->setText(2, u"… %1 ligne%2 de plus"_s.arg(m_lines.size() - n).arg(plural(m_lines.size() - n)));
        more->setForeground(2, weak);
        items << more;
    }
    m_raw->addTopLevelItems(items);
    m_raw->resizeColumnToContents(0);
    m_raw->resizeColumnToContents(1);
}

bool ImportDialog::included(int column) const
{
    if (const auto it = m_include.find(column); it != m_include.end())
        return it->second;
    return !m_reading.columns[static_cast<size_t>(column)].values.empty();
}

QString ImportDialog::nameOf(int column) const
{
    if (const auto it = m_names.find(column); it != m_names.end() && !it->second.isEmpty())
        return csv::sanitizeName(it->second, u"c%1"_s.arg(column + 1));
    return m_reading.columns[static_cast<size_t>(column)].name;
}

QString ImportDialog::unitOf(int column) const
{
    if (const auto it = m_units.find(column); it != m_units.end())
        return it->second;
    return m_reading.columns[static_cast<size_t>(column)].unit;
}

void ImportDialog::fillPreview()
{
    m_filling = true;
    const auto &cols = m_reading.columns;
    const int ncols = static_cast<int>(cols.size());
    const int nrows = static_cast<int>(std::min<size_t>(m_reading.rows.size(), PREVIEW_ROWS));
    const bool more = m_reading.rows.size() > static_cast<size_t>(nrows);
    m_preview->clear();
    m_preview->setColumnCount(ncols);
    m_preview->setRowCount(FirstValue + nrows + (more ? 1 : 0));
    QStringList vlabels{u"Importer"_s, u"Nom"_s, u"Unité"_s};
    for (int r = 0; r < nrows; ++r)
        vlabels << QString::number(m_reading.rows[static_cast<size_t>(r)] + 1);
    if (more)
        vlabels << u"…"_s;
    m_preview->setVerticalHeaderLabels(vlabels);
    m_preview->verticalHeaderItem(Include)->setToolTip(u"Colonnes importées"_s);
    for (int r = FirstValue; r < FirstValue + nrows; ++r)
        m_preview->verticalHeaderItem(r)->setToolTip(u"Ligne %1 du fichier"_s.arg(vlabels[r]));
    QStringList hlabels;
    for (int c = 0; c < ncols; ++c)
        hlabels << u"Colonne %1"_s.arg(c + 1);
    m_preview->setHorizontalHeaderLabels(hlabels);

    const NumberStyle style = m_win.store().style();
    const QColor weak = theme::weakColor(), error = theme::errorColor();
    const QColor text = palette().color(QPalette::Text);
    // valeurs de l'aperçu : cases lues de nouveau, pour montrer le texte d'une case illisible
    std::vector<QStringList> cells;
    cells.reserve(static_cast<size_t>(nrows));
    for (int r = 0; r < nrows; ++r)
        cells.push_back(csv::splitLine(m_lines[m_reading.rows[static_cast<size_t>(r)]], m_options.separator));
    for (int c = 0; c < ncols; ++c) {
        const bool on = included(c);
        const QColor fg = on ? text : weak;
        auto *inc = new QTableWidgetItem(on ? u"oui"_s : u"non"_s);
        inc->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        inc->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        if (cols[static_cast<size_t>(c)].values.empty())
            inc->setToolTip(u"Aucun nombre dans cette colonne"_s);
        m_preview->setItem(Include, c, inc);
        auto *name = new QTableWidgetItem(nameOf(c));
        QFont bold = name->font();
        bold.setWeight(QFont::DemiBold);
        name->setFont(bold);
        name->setForeground(fg);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
        name->setToolTip(u"Nom de la grandeur (double-clic pour le changer)"_s);
        m_preview->setItem(Name, c, name);
        auto *unit = new QTableWidgetItem(unitOf(c));
        unit->setForeground(fg);
        unit->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
        unit->setToolTip(u"Unité (double-clic pour la changer)"_s);
        m_preview->setItem(Unit, c, unit);
        for (int r = 0; r < nrows; ++r) {
            const QString raw = cells[static_cast<size_t>(r)].value(c);
            const auto v = csv::number(raw, m_options.decimal);
            auto *item = new QTableWidgetItem(v ? shortestStyled(*v, style) : raw);
            item->setFlags(Qt::ItemIsEnabled);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (!v && !raw.isEmpty()) {
                item->setForeground(on ? error : weak);
                item->setToolTip(u"« %1 » n'est pas un nombre : la case restera vide"_s.arg(raw));
            } else {
                item->setForeground(fg);
            }
            m_preview->setItem(FirstValue + r, c, item);
        }
        if (more) {
            auto *item = new QTableWidgetItem(u"…"_s);
            item->setFlags(Qt::ItemIsEnabled);
            item->setForeground(weak);
            item->setTextAlignment(Qt::AlignCenter);
            m_preview->setItem(FirstValue + nrows, c, item);
        }
    }
    m_filling = false;
}

csv::Parsed ImportDialog::selection() const
{
    csv::Parsed out;
    for (int c = 0; c < static_cast<int>(m_reading.columns.size()); ++c) {
        if (!included(c))
            continue;
        csv::Column col = m_reading.columns[static_cast<size_t>(c)];
        col.name = nameOf(c);
        col.unit = unitOf(c);
        out.columns.push_back(std::move(col));
    }
    return out;
}

void ImportDialog::updateSummary()
{
    const csv::Parsed sel = selection();
    QStringList lines;
    const QColor warn = theme::warnColor(), ok = theme::successColor(), err = theme::errorColor(), weak = theme::weakColor();
    size_t values = 0;
    for (const csv::Column &c : sel.columns)
        values = std::max(values, c.values.size());
    const bool any = !sel.columns.empty() && values > 0;
    m_ok->setEnabled(any);
    if (!any) {
        lines << html(err, u"✕ Rien à importer : aucune colonne cochée ne contient de nombres. Vérifiez les séparateurs et la première ligne de valeurs."_s);
    } else {
        QString where;
        if (const TableView *t = m_table ? m_win.store().ws.table(*m_table) : nullptr)
            where = u"au tableur "_s + quoted(t->name.toHtmlEscaped());
        else
            where = u"à un nouveau tableur "_s + quoted(QFileInfo(m_path).completeBaseName().toHtmlEscaped());
        lines << html(ok, u"✓ %1 grandeur%2, %3 ligne%4 de valeurs, seront ajoutée%2 %5."_s.arg(sel.columns.size())
                              .arg(plural(static_cast<qsizetype>(sel.columns.size())))
                              .arg(values)
                              .arg(plural(static_cast<qsizetype>(values)))
                              .arg(where));
    }
    // cases illisibles des colonnes importées
    int bad = 0, other = 0;
    csv::BadCell first;
    const csv::Decimal otherDecimal = m_options.decimal == csv::Decimal::Comma ? csv::Decimal::Point : csv::Decimal::Comma;
    for (size_t r = 0; r < m_reading.rows.size(); ++r) {
        const int line = m_reading.rows[r];
        const QStringList cells = csv::splitLine(m_lines[line], m_options.separator);
        for (int c = 0; c < static_cast<int>(m_reading.columns.size()); ++c) {
            const QString cell = cells.value(c);
            if (cell.isEmpty() || !included(c) || csv::number(cell, m_options.decimal))
                continue;
            if (bad++ == 0)
                first = {line, c, cell};
            if (csv::number(cell, otherDecimal))
                ++other;
        }
    }
    if (bad > 0) {
        QString t = u"⚠ %1 case%2 ne %3 pas des nombres (en rouge ci-dessus) : elle%2 resteront vide%2. Première : ligne %4, colonne %5, %6."_s.arg(bad)
                        .arg(plural(bad), bad > 1 ? u"sont"_s : u"est"_s)
                        .arg(first.line + 1)
                        .arg(first.column + 1)
                        .arg(quoted(first.text.toHtmlEscaped()));
        if (bad == 1)
            t.replace(u"elle resteront vide"_s, u"elle restera vide"_s);
        if (other > 0)
            t += u" Avec le séparateur décimal %1, %2 d'entre elles seraient lues."_s.arg(otherDecimal == csv::Decimal::Point ? u"« point »"_s : u"« virgule »"_s).arg(other);
        lines << html(warn, t);
    }
    if (m_reading.ragged > 0 && m_reading.usualWidth > 0)
        lines << html(warn, u"⚠ %1 ligne%2 n'%3 pas %4 case%5 comme les autres (première : ligne %6) : le séparateur des valeurs est peut-être mauvais."_s
                                .arg(m_reading.ragged)
                                .arg(plural(m_reading.ragged), m_reading.ragged > 1 ? u"ont"_s : u"a"_s)
                                .arg(m_reading.usualWidth)
                                .arg(plural(m_reading.usualWidth))
                                .arg(m_reading.firstRagged + 1));
    if (m_reading.columns.size() == 1) {
        const bool others = std::any_of(m_lines.begin(), m_lines.end(), [this](const QString &l) {
            for (QChar c : {QChar(u';'), QChar(u'\t'), QChar(u',')})
                if (c != m_options.separator && l.contains(c))
                    return true;
            return false;
        });
        if (others)
            lines << html(warn, u"⚠ Une seule colonne : vérifiez le séparateur des valeurs."_s);
    }
    // noms déjà pris dans le document : la grandeur importée en prend un autre
    Document doc = m_win.store().ws.doc;
    QStringList renamed;
    for (const csv::Column &c : sel.columns) {
        const QString final = doc.uniqueName(c.name);
        doc.addExperimental(final, c.unit, {});
        if (final != c.name)
            renamed << u"%1 → %2"_s.arg(quoted(c.name.toHtmlEscaped()), quoted(final.toHtmlEscaped()));
    }
    if (!renamed.isEmpty())
        lines << html(warn, u"⚠ Nom%1 déjà pris dans le document, changé%1 : %2."_s.arg(plural(renamed.size()), renamed.join(u", "_s)));
    if (m_reading.skipped > 0)
        lines << html(weak, u"%1 ligne%2 vide%2 ignorée%2."_s.arg(m_reading.skipped).arg(plural(m_reading.skipped)));
    m_summary->setText(lines.join(u"<br>"_s));
}

void ImportDialog::accept()
{
    const csv::Parsed parsed = selection();
    if (parsed.columns.empty())
        return;
    Store &store = m_win.store();
    std::vector<QId> ids;
    ViewId newTable = 0;
    store.edit([&](Workspace &ws) {
        ids = csv::import(ws.doc, parsed);
        if (TableView *t = m_table ? ws.tableMut(*m_table) : nullptr) {
            t->columns.insert(t->columns.end(), ids.begin(), ids.end());
        } else {
            newTable = ws.addTable(ids);
            ws.tableMut(newTable)->name = QFileInfo(m_path).completeBaseName();
        }
    });
    m_win.openTab(TabKey{TabKey::Table, newTable ? newTable : *m_table});
    store.message(u"%1 grandeur%2 importée%2 de %3"_s.arg(ids.size()).arg(plural(static_cast<qsizetype>(ids.size())), QFileInfo(m_path).fileName()),
                  icons::Status::Success);
    QDialog::accept();
}

} // namespace

void exportFile(MainWindow &win, const std::vector<QId> &ids)
{
    Store &store = win.store();
    if (ids.empty()) {
        store.message(u"Aucune grandeur à exporter"_s, icons::Status::Warning);
        return;
    }
    auto *dlg = new QDialog(&win);
    dlg->setObjectName(u"csvExport"_s);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(u"Exporter en CSV"_s);
    auto *layout = new QVBoxLayout(dlg);
    layout->addWidget(weakLabel(u"%1 grandeur%2. Les réglages par défaut conviennent aux tableurs en français (Excel, LibreOffice)."_s
                                    .arg(ids.size())
                                    .arg(plural(static_cast<qsizetype>(ids.size()))),
                                dlg));
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *sep = new QComboBox(dlg);
    sep->setObjectName(u"separator"_s);
    sep->addItem(u"Point-virgule  ;"_s, QVariant(QChar(u';')));
    sep->addItem(u"Virgule  ,"_s, QVariant(QChar(u',')));
    sep->addItem(u"Tabulation"_s, QVariant(QChar(u'\t')));
    auto *dec = new QComboBox(dlg);
    dec->setObjectName(u"decimal"_s);
    dec->addItems({u"Virgule : 3,14"_s, u"Point : 3.14"_s});
    auto *head = new QComboBox(dlg);
    head->setObjectName(u"header"_s);
    head->addItems({u"nom (unité)"_s, u"nom, puis l'unité sur la ligne suivante"_s, u"nom seul"_s});
    auto *enc = new QComboBox(dlg);
    enc->setObjectName(u"encoding"_s);
    enc->addItems({u"UTF-8 (Excel, LibreOffice, Python)"_s, u"UTF-8 sans marque"_s, u"Windows (anciens logiciels)"_s});
    enc->setToolTip(u"Codage des lettres accentuées et des unités (µ, °…)"_s);
    // langue en français : point-virgule et virgule ; sinon virgule et point
    sep->setCurrentIndex(store.prefs.decimalComma ? 0 : 1);
    dec->setCurrentIndex(store.prefs.decimalComma ? 0 : 1);
    form->addRow(u"Séparateur des valeurs"_s, sep);
    form->addRow(u"Séparateur décimal"_s, dec);
    form->addRow(u"En-têtes"_s, head);
    form->addRow(u"Codage"_s, enc);
    layout->addLayout(form);
    layout->addWidget(title(u"Aperçu"_s, dlg));
    auto *preview = new QPlainTextEdit(dlg);
    preview->setObjectName(u"preview"_s);
    preview->setReadOnly(true);
    preview->setFont(theme::fixedFont());
    preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(preview, 1);
    auto *buttons = new QDialogButtonBox(dlg);
    QPushButton *ok = buttons->addButton(u"Exporter…"_s, QDialogButtonBox::AcceptRole);
    ok->setDefault(true);
    buttons->addButton(u"Annuler"_s, QDialogButtonBox::RejectRole);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    layout->addWidget(buttons);
    const auto options = [sep, dec, head] {
        csv::ExportOptions o{sep->currentData().value<QChar>(), dec->currentIndex() == 0};
        o.header = static_cast<csv::ExportOptions::Header>(head->currentIndex());
        return o;
    };
    const auto update = [&store, ids, sep, dec, preview, options] {
        // la virgule ne peut pas séparer à la fois les valeurs et les décimales
        if (auto *model = qobject_cast<QStandardItemModel *>(dec->model()))
            model->item(0)->setEnabled(sep->currentIndex() != 1);
        if (sep->currentIndex() == 1 && dec->currentIndex() == 0)
            dec->setCurrentIndex(1);
        QStringList lines = csv::exportText(store.ws.doc, ids, options()).split(u"\r\n"_s);
        const qsizetype total = lines.size() - 1;
        if (lines.size() > 8)
            lines = lines.mid(0, 8) << u"… (%1 lignes en tout)"_s.arg(total);
        preview->setPlainText(lines.join(u'\n').replace(u'\t', u" ⇥ "_s));
    };
    for (QComboBox *c : {sep, dec, head})
        QObject::connect(c, &QComboBox::currentIndexChanged, dlg, update);
    update();
    QObject::connect(ok, &QPushButton::clicked, dlg, [&win, ids, dlg, enc, options] {
        const QString path = win.askCsvPath();
        if (path.isEmpty())
            return;
        if (win.writeCsv(path, ids, options(), static_cast<csv::Encoding>(enc->currentIndex())))
            dlg->accept();
    });
    dlg->resize(theme::px(QSize(620, 460)));
    dlg->open();
}

void open(MainWindow &win, const QString &path, std::optional<ViewId> table)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        win.store().message(u"Lecture impossible de %1 : %2"_s.arg(QFileInfo(path).fileName(), f.errorString()), icons::Status::Error);
        return;
    }
    QStringList lines = csv::textLines(csv::decodeText(f.readAll()));
    if (lines.isEmpty()) {
        win.store().message(u"%1 est vide"_s.arg(QFileInfo(path).fileName()), icons::Status::Error);
        return;
    }
    auto *dlg = new ImportDialog(win, path, std::move(lines), table);
    dlg->open();
}

} // namespace csvimport
