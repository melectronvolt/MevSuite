#include "app/widgets.h"

#include "app/icons.h"
#include "app/store.h"
#include "app/theme.h"

#include <QApplication>
#include <QColorDialog>
#include <QGridLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStylePainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QStyleOptionToolButton>
#include <QWidgetAction>

#include <cmath>

using namespace Qt::StringLiterals;

// --- NumberEdit -------------------------------------------------------------------------------

NumberEdit::NumberEdit(QWidget *parent) : QAbstractSpinBox(parent)
{
    setAccelerated(true);
    setKeyboardTracking(false);
    setCorrectionMode(QAbstractSpinBox::CorrectToPreviousValue);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    theme::setMinimumWidth(this, 70);
    lineEdit()->setText(text(m_value));
    connect(this, &QAbstractSpinBox::editingFinished, this, &NumberEdit::commit);
}

QString NumberEdit::text(double v) const
{
    return formatSig(v, 6, m_style) + m_suffix;
}

void NumberEdit::setValue(double v)
{
    m_value = v;
    if (!lineEdit()->hasFocus() || !lineEdit()->isModified())
        lineEdit()->setText(text(v));
}

void NumberEdit::setStyle(NumberStyle s)
{
    m_style = s;
    lineEdit()->setText(text(m_value));
}

void NumberEdit::setRange(double min, double max)
{
    m_min = min;
    m_max = max;
}

void NumberEdit::setSuffix(const QString &s)
{
    m_suffix = s;
    lineEdit()->setText(text(m_value));
}

void NumberEdit::commit()
{
    QString t = lineEdit()->text();
    if (!m_suffix.isEmpty() && t.endsWith(m_suffix))
        t.chop(m_suffix.size());
    const auto v = parseNumber(t);
    lineEdit()->setModified(false);
    if (!v) {
        lineEdit()->setText(text(m_value));
        return;
    }
    const double c = std::clamp(*v, m_min, m_max);
    lineEdit()->setText(text(c));
    if (c != m_value) {
        m_value = c;
        Q_EMIT valueEdited(c);
    }
}

void NumberEdit::stepBy(int steps)
{
    const double step = m_step ? *m_step : std::max(std::abs(m_value) * 0.01, 1e-6);
    const double v = std::clamp(m_value + steps * step, m_min, m_max);
    if (v != m_value) {
        m_value = v;
        lineEdit()->setText(text(v));
        Q_EMIT valueEdited(v);
    }
}

QValidator::State NumberEdit::validate(QString &input, int &) const
{
    QString t = input;
    if (!m_suffix.isEmpty() && t.endsWith(m_suffix))
        t.chop(m_suffix.size());
    if (parseNumber(t))
        return QValidator::Acceptable;
    return QValidator::Intermediate;
}

QAbstractSpinBox::StepEnabled NumberEdit::stepEnabled() const
{
    return StepUpEnabled | StepDownEnabled;
}

// --- QuantityCombo ----------------------------------------------------------------------------

QuantityCombo::QuantityCombo(Store &store, bool allowNone, QWidget *parent) : QComboBox(parent), m_store(store), m_allowNone(allowNone)
{
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    setMinimumContentsLength(6);
    refresh();
    connect(this, &QComboBox::activated, this, [this] { Q_EMIT picked(current()); });
}

std::optional<QId> QuantityCombo::current() const
{
    const QVariant v = currentData();
    if (!v.isValid() || v.toULongLong() == 0)
        return std::nullopt;
    return static_cast<QId>(v.toULongLong());
}

void QuantityCombo::setCurrent(std::optional<QId> id)
{
    const int k = findData(QVariant::fromValue<qulonglong>(id.value_or(0)));
    if (k >= 0)
        setCurrentIndex(k);
    else if (m_allowNone)
        setCurrentIndex(0);
}

void QuantityCombo::refresh()
{
    const auto keep = current();
    QSignalBlocker block(this);
    clear();
    if (m_allowNone)
        addItem(u"—"_s, QVariant::fromValue<qulonglong>(0));
    for (const auto &q : m_store.ws.doc.quantities)
        addItem(q.name, QVariant::fromValue<qulonglong>(q.id));
    setCurrent(keep);
}

// --- ColorButton ------------------------------------------------------------------------------

ColorButton::ColorButton(QWidget *parent) : QToolButton(parent)
{
    setPopupMode(QToolButton::InstantPopup);
    theme::setFixedSize(this, QSize(34, 24));
    setToolTip(u"Couleur"_s);
    auto *menu = new QMenu(this);
    auto *panel = new QWidget(menu);
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setSpacing(4);
    std::vector<Color> swatches(PALETTE.begin(), PALETTE.end());
    for (Color c : {colors::BLACK, Color::rgb(90, 90, 90), Color::rgb(160, 160, 160), Color::rgb(0, 0, 255), Color::rgb(255, 0, 0),
                    Color::rgb(0, 128, 0), Color::rgb(255, 165, 0), Color::rgb(128, 0, 128), Color::rgb(0, 128, 128), Color::rgb(165, 42, 42)})
        swatches.push_back(c);
    for (size_t k = 0; k < swatches.size(); ++k) {
        auto *b = new QToolButton(panel);
        theme::setFixedSize(b, QSize(22, 22));
        b->setAutoRaise(true);
        b->setStyleSheet(u"QToolButton { background: %1; border: 1px solid rgba(0,0,0,60); border-radius: 3px; }"
                         "QToolButton:hover { border: 2px solid palette(highlight); }"_s.arg(swatches[k].hex()));
        const Color c = swatches[k];
        connect(b, &QToolButton::clicked, this, [this, c, menu] {
            menu->close();
            setColor(c);
            Q_EMIT colorPicked(c);
        });
        grid->addWidget(b, static_cast<int>(k / 5), static_cast<int>(k % 5));
    }
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(panel);
    menu->addAction(action);
    menu->addSeparator();
    menu->addAction(u"Autre couleur…"_s, this, [this] {
        const QColor c = QColorDialog::getColor(m_color.q(), this, u"Couleur"_s);
        if (c.isValid()) {
            setColor(Color::from(c));
            Q_EMIT colorPicked(m_color);
        }
    });
    setMenu(menu);
}

void ColorButton::setColor(Color c)
{
    m_color = c;
    update();
}

void MenuToolButton::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.features &= ~QStyleOptionToolButton::HasMenu;
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
}

MenuToolButton *menuButton(const char *icon, const QString &text, const QString &tip, QMenu *menu, QWidget *parent)
{
    auto *b = new MenuToolButton(parent);
    b->setIcon(icons::get(icon));
    theme::setIconSize(b, QSize(18, 18));
    b->setText(text + u" ▾"_s);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setPopupMode(QToolButton::InstantPopup);
    menu->setParent(b, menu->windowFlags());
    b->setMenu(menu);
    return b;
}

void ColorButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QStyleOptionToolButton opt;
    initStyleOption(&opt);
    opt.features &= ~QStyleOptionToolButton::HasMenu;
    style()->drawPrimitive(QStyle::PE_PanelButtonTool, &opt, &p, this);
    p.setRenderHint(QPainter::Antialiasing);
    const double z = theme::zoom();
    const QRectF r = QRectF(rect()).adjusted(6 * z, 5 * z, -6 * z, -5 * z);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(m_color.q());
    p.drawRoundedRect(r, 3 * z, 3 * z);
}

// --- FlowLayout -------------------------------------------------------------------------------

FlowLayout::FlowLayout(QWidget *parent, int spacing) : QLayout(parent), m_spacing(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.push_back(item);
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return index >= 0 && index < count() ? m_items[index] : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= count())
        return nullptr;
    QLayoutItem *item = m_items[index];
    m_items.erase(m_items.begin() + index);
    return item;
}

int FlowLayout::heightForWidth(int width) const
{
    return arrange(QRect(0, 0, width, 0), false);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, true);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::arrange(const QRect &rect, bool apply) const
{
    const QMargins m = contentsMargins();
    const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), lineHeight = 0;
    // éléments de la ligne en cours, centrés verticalement une fois la ligne complète
    std::vector<std::pair<QLayoutItem *, QRect>> line;
    auto flush = [&] {
        if (apply)
            for (auto &[item, g] : line)
                item->setGeometry(g.translated(0, (lineHeight - g.height()) / 2));
        line.clear();
    };
    for (QLayoutItem *item : m_items) {
        if (item->widget() && !item->widget()->isVisibleTo(item->widget()->parentWidget()))
            continue;
        const QSize hint = item->sizeHint();
        int next = x + hint.width() + m_spacing;
        if (next - m_spacing > r.right() + 1 && lineHeight > 0) {
            flush();
            x = r.x();
            y += lineHeight + m_spacing;
            next = x + hint.width() + m_spacing;
            lineHeight = 0;
        }
        line.emplace_back(item, QRect(QPoint(x, y), hint));
        x = next;
        lineHeight = std::max(lineHeight, hint.height());
    }
    flush();
    return y + lineHeight - rect.y() + m.bottom();
}

// --- textes et boutons ------------------------------------------------------------------------

QLabel *weakLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    // indication : couleur discrète, taille normale
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, theme::weakColor());
    l->setPalette(pal);
    return l;
}

QLabel *heading(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    // titre : demi-gras, taille normale, comme dans les autres logiciels de la suite
    QFont f = l->font();
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    return l;
}

QToolButton *toolButton(const char *icon, const QString &text, const QString &tip, QWidget *parent, bool showText)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icons::get(icon));
    theme::setIconSize(b, QSize(18, 18));
    if (showText && !text.isEmpty()) {
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    } else {
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    b->setToolTip(tip.isEmpty() ? text : tip);
    b->setAutoRaise(true);
    return b;
}

namespace dnd {

QMimeData *quantityMime(QId id)
{
    return quantitiesMime({id});
}

QMimeData *quantitiesMime(const std::vector<QId> &ids)
{
    // identifiants séparés par des virgules
    QByteArrayList parts;
    for (QId id : ids)
        parts << QByteArray::number(static_cast<qulonglong>(id));
    auto *m = new QMimeData;
    m->setData(QString::fromLatin1(MIME), parts.join(','));
    return m;
}

std::vector<QId> quantitiesFrom(const QMimeData *mime)
{
    std::vector<QId> out;
    if (!mime || !mime->hasFormat(QString::fromLatin1(MIME)))
        return out;
    for (const QByteArray &part : mime->data(QString::fromLatin1(MIME)).split(',')) {
        bool ok = false;
        const qulonglong id = part.trimmed().toULongLong(&ok);
        if (!ok || id == 0)
            return {};
        out.push_back(static_cast<QId>(id));
    }
    return out;
}

std::optional<QId> quantityFrom(const QMimeData *mime)
{
    const std::vector<QId> ids = quantitiesFrom(mime);
    if (ids.empty())
        return std::nullopt;
    return ids.front();
}

} // namespace dnd

namespace live {

namespace {

QLineEdit *editOf(QWidget *w)
{
    if (auto *le = qobject_cast<QLineEdit *>(w))
        return le;
    if (auto *sb = qobject_cast<QAbstractSpinBox *>(w))
        return sb->findChild<QLineEdit *>();
    if (auto *cb = qobject_cast<QComboBox *>(w))
        return cb->lineEdit();
    return nullptr;
}

/// Widget qui porte le focus dans la page : un champ de saisie plutôt que sa zone de texte interne.
QWidget *focusIn(const QWidget *page)
{
    QWidget *f = QApplication::focusWidget();
    if (!page || !f || !page->isAncestorOf(f))
        return nullptr;
    if (auto *sb = qobject_cast<QAbstractSpinBox *>(f->parentWidget()); sb && qobject_cast<QLineEdit *>(f))
        return sb;
    return f;
}

} // namespace

bool busy(const QWidget *page)
{
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
        return true;
    QWidget *f = focusIn(page);
    if (!f)
        return false;
    const QLineEdit *le = editOf(f);
    return le && le->isModified();
}

void track(QWidget *page)
{
    for (QLineEdit *le : page->findChildren<QLineEdit *>()) {
        if (qobject_cast<QAbstractSpinBox *>(le->parentWidget()))
            continue;
        QObject::connect(le, &QLineEdit::editingFinished, le, [le] { le->setModified(false); });
    }
    for (QAbstractSpinBox *sb : page->findChildren<QAbstractSpinBox *>()) {
        if (QLineEdit *le = sb->findChild<QLineEdit *>())
            QObject::connect(sb, &QAbstractSpinBox::editingFinished, le, [le] { le->setModified(false); });
    }
}

Focus remember(const QWidget *page)
{
    QWidget *f = focusIn(page);
    if (!f)
        return {};
    Focus m;
    m.cls = f->metaObject()->className();
    const auto all = page->findChildren<QWidget *>();
    int k = 0;
    for (QWidget *w : all) {
        if (w == f) {
            m.index = k;
            break;
        }
        if (m.cls == w->metaObject()->className())
            ++k;
    }
    if (const QLineEdit *le = editOf(f))
        m.cursor = le->cursorPosition();
    return m;
}

void restore(QWidget *page, const Focus &f)
{
    if (f.index < 0)
        return;
    int k = 0;
    for (QWidget *w : page->findChildren<QWidget *>()) {
        if (f.cls != w->metaObject()->className())
            continue;
        if (k++ == f.index) {
            if (!w->isEnabled() || !w->isVisibleTo(page))
                return;
            w->setFocus(Qt::OtherFocusReason);
            if (QLineEdit *le = editOf(w); le && f.cursor >= 0) {
                le->deselect();
                le->setCursorPosition(std::min(f.cursor, static_cast<int>(le->text().size())));
            }
            return;
        }
    }
}

} // namespace live

PanelScroll::PanelScroll(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

QSize PanelScroll::minimumSizeHint() const
{
    const int content = widget() ? widget()->minimumSizeHint().width() : 0;
    return {content + verticalScrollBar()->sizeHint().width() + 2 * frameWidth(), QScrollArea::minimumSizeHint().height()};
}

bool PanelScroll::eventFilter(QObject *watched, QEvent *event)
{
    // contenu reconstruit ou agrandi : la largeur minimale du panneau suit
    if (watched == widget() && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show))
        updateGeometry();
    return QScrollArea::eventFilter(watched, event);
}

void keepScroll(QScrollArea *area, int pos)
{
    QScrollBar *bar = area->verticalScrollBar();
    if (QWidget *w = area->widget()) {
        w->resize(area->viewport()->width(), w->heightForWidth(area->viewport()->width()) > 0 ? w->heightForWidth(area->viewport()->width()) : w->sizeHint().height());
        bar->setRange(0, std::max(0, w->height() - area->viewport()->height()));
    }
    bar->setValue(pos);
    QTimer::singleShot(0, area, [area, pos] { area->verticalScrollBar()->setValue(pos); });
}
