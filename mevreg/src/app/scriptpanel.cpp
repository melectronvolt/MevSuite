#include "app/scriptpanel.h"

#include "app/help.h"
#include "app/icons.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "plot/renderer.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

using namespace Qt::StringLiterals;

namespace {

const QStringList KEYWORDS = {u"False"_s, u"None"_s,   u"True"_s,    u"and"_s,    u"as"_s,     u"assert"_s, u"break"_s,  u"class"_s, u"continue"_s,
                              u"def"_s,   u"del"_s,    u"elif"_s,    u"else"_s,   u"except"_s, u"finally"_s, u"for"_s,   u"from"_s,  u"global"_s,
                              u"if"_s,    u"import"_s, u"in"_s,      u"is"_s,     u"lambda"_s, u"nonlocal"_s, u"not"_s,  u"or"_s,    u"pass"_s,
                              u"raise"_s, u"return"_s, u"try"_s,     u"while"_s,  u"with"_s,   u"yield"_s};
const QStringList BUILTINS = {u"print"_s, u"range"_s, u"len"_s,  u"abs"_s,  u"min"_s,   u"max"_s,       u"sum"_s,    u"round"_s,
                              u"int"_s,   u"float"_s, u"str"_s,  u"list"_s, u"dict"_s,  u"tuple"_s,     u"enumerate"_s, u"zip"_s,
                              u"sorted"_s, u"isinstance"_s, u"grandeur"_s, u"np"_s, u"plt"_s, u"math"_s, u"fenetre"_s};

/// Blocs ouverts par une chaîne triple : 1 pour """, 2 pour '''.
enum State { Normal = 0, TripleDouble = 1, TripleSingle = 2 };

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t, a.blueF() * (1 - t) + b.blueF() * t);
}

/// Gouttière des numéros de ligne.
class Gutter : public QWidget {
public:
    explicit Gutter(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *e) override { m_editor->paintGutter(e); }

private:
    CodeEditor *m_editor;
};

} // namespace

// --- PythonHighlighter ---------------------------------------------------------------------

PythonHighlighter::PythonHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc)
{
    setDark(theme::isDark());
}

void PythonHighlighter::setDark(bool dark)
{
    auto fmt = [](QColor c, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(c);
        if (bold)
            f.setFontWeight(QFont::DemiBold);
        f.setFontItalic(italic);
        return f;
    };
    if (dark) {
        m_keyword = fmt(QColor(0xc5, 0x86, 0xc0));
        m_builtin = fmt(QColor(0x4e, 0xc9, 0xb0));
        m_string = fmt(QColor(0xce, 0x91, 0x78));
        m_number = fmt(QColor(0xb5, 0xce, 0xa8));
        m_comment = fmt(QColor(0x6a, 0x99, 0x55), false, true);
        m_def = fmt(QColor(0xdc, 0xdc, 0xaa));
    } else {
        m_keyword = fmt(QColor(0x8a, 0x2b, 0xb0));
        m_builtin = fmt(QColor(0x00, 0x70, 0x80));
        m_string = fmt(QColor(0xa3, 0x15, 0x15));
        m_number = fmt(QColor(0x09, 0x86, 0x58));
        m_comment = fmt(QColor(0x00, 0x80, 0x00), false, true);
        m_def = fmt(QColor(0x79, 0x5e, 0x26));
    }
    rehighlight();
}

void PythonHighlighter::highlightBlock(const QString &text)
{
    const int n = static_cast<int>(text.size());
    int i = 0;
    setCurrentBlockState(Normal);
    // suite d'une chaîne triple ouverte plus haut
    if (const int prev = previousBlockState(); prev == TripleDouble || prev == TripleSingle) {
        const QString close = prev == TripleDouble ? u"\"\"\""_s : u"'''"_s;
        const int end = static_cast<int>(text.indexOf(close));
        if (end < 0) {
            setFormat(0, n, m_string);
            setCurrentBlockState(prev);
            return;
        }
        setFormat(0, end + 3, m_string);
        i = end + 3;
    }
    bool afterDef = false;
    while (i < n) {
        const QChar c = text[i];
        if (c == u'#') {
            setFormat(i, n - i, m_comment);
            return;
        }
        if (c == u'"' || c == u'\'') {
            const bool triple = i + 2 < n && text[i + 1] == c && text[i + 2] == c;
            if (triple) {
                const QString close = QString(3, c);
                const int end = static_cast<int>(text.indexOf(close, i + 3));
                if (end < 0) {
                    setFormat(i, n - i, m_string);
                    setCurrentBlockState(c == u'"' ? TripleDouble : TripleSingle);
                    return;
                }
                setFormat(i, end + 3 - i, m_string);
                i = end + 3;
                continue;
            }
            int j = i + 1;
            while (j < n && text[j] != c) {
                if (text[j] == u'\\')
                    ++j;
                ++j;
            }
            j = std::min(j + 1, n);
            // préfixe f, r, b collé à la chaîne
            int start = i;
            if (start > 0 && QStringView(u"frbuFRBU").contains(text[start - 1]) && (start < 2 || !text[start - 2].isLetterOrNumber()))
                --start;
            setFormat(start, j - start, m_string);
            i = j;
            continue;
        }
        if (c.isDigit() || (c == u'.' && i + 1 < n && text[i + 1].isDigit())) {
            if (i > 0 && (text[i - 1].isLetterOrNumber() || text[i - 1] == u'_')) {
                ++i;
                continue;
            }
            int j = i;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == u'.' || ((text[j] == u'-' || text[j] == u'+') && j > i && (text[j - 1] == u'e' || text[j - 1] == u'E'))))
                ++j;
            setFormat(i, j - i, m_number);
            i = j;
            continue;
        }
        if (c.isLetter() || c == u'_') {
            int j = i;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == u'_'))
                ++j;
            const QString word = text.mid(i, j - i);
            const bool attribute = i > 0 && text[i - 1] == u'.';
            if (afterDef) {
                setFormat(i, j - i, m_def);
                afterDef = false;
            } else if (!attribute && KEYWORDS.contains(word)) {
                setFormat(i, j - i, m_keyword);
                afterDef = word == u"def" || word == u"class";
            } else if (!attribute && BUILTINS.contains(word)) {
                setFormat(i, j - i, m_builtin);
            }
            i = j;
            continue;
        }
        ++i;
    }
}

// --- CodeEditor ----------------------------------------------------------------------------

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    setLineWrapMode(QPlainTextEdit::NoWrap);
    m_gutter = new Gutter(this);
    fitFont();
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) {
        if (dy)
            m_gutter->scroll(0, dy);
        else
            m_gutter->update(0, r.y(), m_gutter->width(), r.height());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::updateSelections);
    setViewportMargins(gutterWidth(), 0, 0, 0);
    updateSelections();
}

void CodeEditor::fitFont()
{
    const QFont f = monoFont(13.5 * theme::zoom(), this);
    if (font() == f)
        return;
    setFont(f);
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(u' ') * 4);
    setViewportMargins(gutterWidth(), 0, 0, 0);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

int CodeEditor::gutterWidth() const
{
    const int digits = std::max(3, static_cast<int>(QString::number(std::max(1, blockCount())).size()));
    return theme::px(14) + fontMetrics().horizontalAdvance(u'9') * digits;
}

void CodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void CodeEditor::changeEvent(QEvent *e)
{
    QPlainTextEdit::changeEvent(e);
    if (e->type() == QEvent::PaletteChange)
        updateSelections();
}

void CodeEditor::paintGutter(QPaintEvent *e)
{
    QPainter p(m_gutter);
    const QPalette pal = palette();
    const QColor bg = mix(pal.color(QPalette::Base), pal.color(QPalette::Window), 0.6);
    p.fillRect(e->rect(), bg);
    QColor sep = pal.color(QPalette::Mid);
    sep.setAlphaF(0.6);
    p.setPen(sep);
    p.drawLine(m_gutter->width() - 1, e->rect().top(), m_gutter->width() - 1, e->rect().bottom());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();
    p.setFont(font());
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            const bool error = m_errorLine && *m_errorLine == number + 1;
            QColor c = error ? theme::errorColor() : number == current ? pal.color(QPalette::Text) : theme::weakColor();
            p.setPen(c);
            QFont f = font();
            f.setBold(error);
            p.setFont(f);
            p.drawText(QRect(0, top, m_gutter->width() - theme::px(7), fontMetrics().height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::setErrorLine(std::optional<int> line)
{
    if (m_errorLine == line)
        return;
    m_errorLine = line;
    updateSelections();
    m_gutter->update();
}

void CodeEditor::updateSelections()
{
    QList<QTextEdit::ExtraSelection> sel;
    const QPalette pal = palette();
    {
        QTextEdit::ExtraSelection s;
        QColor c = pal.color(QPalette::Highlight);
        c.setAlphaF(theme::isDark() ? 0.16 : 0.08);
        s.format.setBackground(c);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = textCursor();
        s.cursor.clearSelection();
        sel << s;
    }
    if (m_errorLine) {
        const QTextBlock b = document()->findBlockByNumber(*m_errorLine - 1);
        if (b.isValid()) {
            QTextEdit::ExtraSelection s;
            QColor c = theme::errorColor();
            c.setAlphaF(theme::isDark() ? 0.30 : 0.18);
            s.format.setBackground(c);
            s.format.setProperty(QTextFormat::FullWidthSelection, true);
            s.cursor = QTextCursor(b);
            sel << s;
        }
    }
    setExtraSelections(sel);
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (e->key() == Qt::Key_F5 || (ctrl && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter))) {
        Q_EMIT runRequested();
        return;
    }
    QTextCursor c = textCursor();
    // commenter / décommenter les lignes sélectionnées
    if (ctrl && (e->key() == Qt::Key_Slash || e->key() == Qt::Key_Colon)) {
        QTextBlock first = document()->findBlock(c.selectionStart());
        const QTextBlock last = document()->findBlock(std::max(c.selectionStart(), c.selectionEnd() - (c.hasSelection() ? 1 : 0)));
        bool allCommented = true;
        for (QTextBlock b = first; b.isValid(); b = b.next()) {
            if (!b.text().trimmed().isEmpty() && !b.text().trimmed().startsWith(u'#'))
                allCommented = false;
            if (b == last)
                break;
        }
        c.beginEditBlock();
        for (QTextBlock b = first; b.isValid(); b = b.next()) {
            const QString t = b.text();
            const int lead = std::max(0, static_cast<int>(t.indexOf(QRegularExpression(u"\\S"_s))));
            QTextCursor bc(b);
            if (allCommented) {
                if (t.mid(lead).startsWith(u"# ")) {
                    bc.setPosition(b.position() + lead);
                    bc.setPosition(b.position() + lead + 2, QTextCursor::KeepAnchor);
                    bc.removeSelectedText();
                } else if (t.mid(lead).startsWith(u'#')) {
                    bc.setPosition(b.position() + lead);
                    bc.setPosition(b.position() + lead + 1, QTextCursor::KeepAnchor);
                    bc.removeSelectedText();
                }
            } else if (!t.trimmed().isEmpty()) {
                bc.setPosition(b.position() + lead);
                bc.insertText(u"# "_s);
            }
            if (b == last)
                break;
        }
        c.endEditBlock();
        return;
    }
    if (e->key() == Qt::Key_Tab && !(e->modifiers() & ~Qt::KeypadModifier)) {
        if (c.hasSelection()) {
            // indente toutes les lignes sélectionnées
            QTextBlock b = document()->findBlock(c.selectionStart());
            const QTextBlock last = document()->findBlock(c.selectionEnd() - 1);
            c.beginEditBlock();
            for (; b.isValid(); b = b.next()) {
                QTextCursor(b).insertText(u"    "_s);
                if (b == last)
                    break;
            }
            c.endEditBlock();
        } else {
            const int col = c.positionInBlock();
            c.insertText(QString(4 - col % 4, u' '));
        }
        return;
    }
    if (e->key() == Qt::Key_Backtab) {
        QTextBlock b = document()->findBlock(c.selectionStart());
        const QTextBlock last = document()->findBlock(c.hasSelection() ? c.selectionEnd() - 1 : c.selectionStart());
        c.beginEditBlock();
        for (; b.isValid(); b = b.next()) {
            const QString t = b.text();
            int k = 0;
            while (k < 4 && k < t.size() && t[k] == u' ')
                ++k;
            if (k > 0) {
                QTextCursor bc(b);
                bc.setPosition(b.position() + k, QTextCursor::KeepAnchor);
                bc.removeSelectedText();
            }
            if (b == last)
                break;
        }
        c.endEditBlock();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !e->modifiers()) {
        // garde l'indentation, l'augmente après « : »
        const QString line = c.block().text().left(c.positionInBlock());
        int indent = 0;
        while (indent < line.size() && line[indent] == u' ')
            ++indent;
        QString code = line;
        if (const int hash = static_cast<int>(code.indexOf(u'#')); hash >= 0)
            code = code.left(hash);
        if (code.trimmed().endsWith(u':'))
            indent += 4;
        c.beginEditBlock();
        c.insertText(u"\n"_s + QString(indent, u' '));
        c.endEditBlock();
        ensureCursorVisible();
        return;
    }
    if (e->key() == Qt::Key_Backspace && !c.hasSelection() && !e->modifiers()) {
        const QString before = c.block().text().left(c.positionInBlock());
        if (!before.isEmpty() && before.trimmed().isEmpty()) {
            const int remove = (before.size() - 1) % 4 + 1;
            for (int k = 0; k < remove; ++k)
                c.deletePreviousChar();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}

// --- ScriptPanel ---------------------------------------------------------------------------

ScriptPanel::ScriptPanel(MainWindow &win, ViewId id, QWidget *parent) : QWidget(parent), m_win(win), m_id(id)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto *bar = new QHBoxLayout;
    bar->setSpacing(4);
    m_run = toolButton("play", u"Exécuter"_s, u"Exécuter le script (F5 ou Ctrl+Entrée)"_s, this);
    m_stop = toolButton("stop", u"Arrêter"_s, u"Interrompre le script"_s, this);
    QPalette sp = m_stop->palette();
    sp.setColor(QPalette::ButtonText, theme::errorColor());
    m_stop->setPalette(sp);
    m_busy = new QLabel(this);
    theme::setMinimumWidth(m_busy, 110);
    bar->addWidget(m_run);
    bar->addWidget(m_stop);
    bar->addWidget(m_busy);
    m_linked = new QCheckBox(u"lié"_s, this);
    m_linked->setToolTip(u"Script lié : relancé automatiquement quand une grandeur ou un paramètre qu'il utilise change"_s);
    bar->addWidget(m_linked);
    bar->addStretch();
    m_name = new QLineEdit(this);
    theme::setMaximumWidth(m_name, 180);
    m_name->setToolTip(u"Nom du script"_s);
    bar->addWidget(m_name);
    auto *print = toolButton("print", QString(), u"PDF, impression"_s, this, false);
    print->setPopupMode(QToolButton::InstantPopup);
    auto *pm = new QMenu(print);
    pm->addAction(icons::get("pdf"), u"PDF du script…"_s, this, [this] {
        flush();
        m_win.scriptPdf(m_id, false);
    });
    pm->addAction(icons::get("print"), u"Imprimer…"_s, this, [this] {
        flush();
        m_win.scriptPdf(m_id, true);
    });
    print->setMenu(pm);
    bar->addWidget(print);
    auto *help = toolButton("help", QString(), u"Python dans MeVReg"_s, this, false);
    help->setPopupMode(QToolButton::InstantPopup);
    auto *hm = new QMenu(help);
    auto *ha = new QWidgetAction(hm);
    auto *hl = new QLabel(u"<p><b>Python dans MeVReg</b></p>"
                          "<p>Les grandeurs du document sont des tableaux numpy portant leur nom ; les paramètres sont des nombres.</p>"
                          "<p><code>grandeur(\"nom\", valeurs, \"unité\")</code> crée ou remplace une grandeur du document.</p>"
                          "<p>Les figures matplotlib deviennent des graphes du logiciel, modifiables à la souris.</p>"
                          "<p><b>numpy (np.)</b><br>array, zeros, ones, full, linspace, arange, sin, cos, tan, arcsin, arccos, arctan, arctan2, exp, "
                          "log, log10, sqrt, abs, round, floor, ceil, sum, mean, std, var, min, max, argmin, argmax, median, cumsum, diff, gradient, "
                          "trapezoid, concatenate, append, where, sort, interp, polyfit, polyval, histogram, dot, cross, fft.fft, fft.rfft, fft.fftfreq, "
                          "fft.rfftfreq, random.normal, random.uniform, random.randint, random.seed, linalg.norm…</p>"
                          "<p><b>matplotlib.pyplot (plt.)</b><br>figure, plot (formats 'ro', 'b+-', 'k--'), scatter, errorbar (xerr, yerr), hist, bar, "
                          "title, xlabel, ylabel, legend, grid, xlim, ylim, axhline, axvline, text, subplot, subplots, twinx, savefig, show.</p>"
                          "<p><b>pandas (pd.)</b><br>DataFrame (plusieurs grandeurs ensemble), read_csv (séparateurs devinés), to_csv, "
                          "describe, mean, std, dropna, sort_values, iloc, plot.</p>"
                          "<p><b>mevreg (mr.)</b><br>Tout le logiciel : grandeurs (creer, calculer, modifier, renommer, supprimer, incertitude), "
                          "tableurs, graphes (graphe, courbe, axes, droites, texte), modeliser, fenêtres (ouvrir, cacher, fermer, detacher), "
                          "fichiers (enregistrer, exporter_csv, lire_csv, exporter_graphe). Exemples : menu Python → Exemples.</p>"
                          "<p><b>fenetre</b><br>Fenêtres de dessin en unités physiques : segments, flèches, cercles, textes, cartes d'intensité "
                          "(figures de diffraction, interférences).</p>"
                          "<p style='color:gray'>Un script qui n'utilise que numpy, matplotlib et ces fonctions de pandas marche aussi dans Capytale "
                          "(read_csv : y préciser sep et decimal, que MeVReg devine).</p>"
                          "<p><a href='python'>Toutes les fonctions et leurs paramètres : référence Python</a></p>"_s,
                          hm);
    hl->setWordWrap(true);
    connect(hl, &QLabel::linkActivated, this, [this, hm] {
        hm->close();
        m_win.openHelp(help::PYTHON);
    });
    // largeur fixe, à l'agrandissement de l'interface
    theme::setMinimumWidth(hl, 460);
    theme::setMaximumWidth(hl, 460);
    hl->setMargin(10);
    ha->setDefaultWidget(hl);
    hm->addAction(ha);
    help->setMenu(hm);
    bar->addWidget(help);
    layout->addLayout(bar);

    auto *split = new QSplitter(Qt::Vertical, this);
    m_editor = new CodeEditor(split);
    m_highlighter = new PythonHighlighter(m_editor->document());
    split->addWidget(m_editor);
    auto *console = new QWidget(split);
    auto *cl = new QVBoxLayout(console);
    cl->setContentsMargins(0, 4, 0, 0);
    cl->setSpacing(2);
    auto *head = new QHBoxLayout;
    auto *icon = new QLabel(console);
    icon->setObjectName(u"consoleIcon"_s);
    icon->setPixmap(icons::get("terminal").pixmap(theme::px(QSize(16, 16))));
    head->addWidget(icon);
    auto *title = new QLabel(u"Console"_s, console);
    QFont tf = title->font();
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    head->addWidget(title);
    m_runInfo = weakLabel(QString(), console);
    m_runInfo->setWordWrap(false);
    head->addWidget(m_runInfo);
    head->addStretch();
    cl->addLayout(head);
    m_console = new QTextBrowser(console);
    m_console->setOpenLinks(false);
    m_console->setFont(monoFont(12.5 * theme::zoom(), m_console));
    cl->addWidget(m_console, 1);
    split->addWidget(console);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    split->setSizes({400, 150});
    layout->addWidget(split, 1);

    if (const Script *s = win.store().ws.script(id))
        m_editor->setPlainText(s->code);
    m_codeTimer.setSingleShot(true);
    m_codeTimer.setInterval(300);
    connect(&m_codeTimer, &QTimer::timeout, this, &ScriptPanel::flush);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] { m_codeTimer.start(); });
    connect(m_editor, &CodeEditor::runRequested, this, &ScriptPanel::run);
    connect(m_run, &QToolButton::clicked, this, &ScriptPanel::run);
    connect(m_stop, &QToolButton::clicked, this, [this] { m_win.store().stopScript(m_id); });
    connect(m_linked, &QCheckBox::toggled, this, [this](bool on) {
        m_win.store().edit([&](Workspace &ws) {
            if (Script *s = ws.scriptMut(m_id))
                s->linked = on;
        });
    });
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString n = m_name->text().trimmed();
        const Script *s = m_win.store().ws.script(m_id);
        if (!s || n.isEmpty() || n == s->name) {
            if (s)
                m_name->setText(s->name);
            return;
        }
        m_win.store().edit([&](Workspace &ws) { ws.scriptMut(m_id)->name = n; });
    });
    connect(m_console, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() != u"line")
            return;
        const QTextBlock b = m_editor->document()->findBlockByNumber(url.path().toInt() - 1);
        if (!b.isValid())
            return;
        QTextCursor c(b);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        m_editor->setTextCursor(c);
        m_editor->setFocus();
    });
    m_busyTimer.setInterval(120);
    connect(&m_busyTimer, &QTimer::timeout, this, [this] {
        static const QString frames[] = {u"◐"_s, u"◓"_s, u"◑"_s, u"◒"_s};
        m_busy->setText(frames[m_busyFrame++ % 4] + u" exécution…"_s);
    });
    connect(&win.store(), &Store::workspaceChanged, this, &ScriptPanel::refresh);
    connect(&win.store(), &Store::displayChanged, this, [this] {
        m_highlighter->setDark(theme::isDark());
        // agrandissement de l'interface : polices du code et de la console, icône de la console
        m_editor->fitFont();
        m_console->setFont(monoFont(12.5 * theme::zoom(), m_console));
        if (auto *icon = findChild<QLabel *>(u"consoleIcon"_s))
            icon->setPixmap(icons::get("terminal").pixmap(theme::px(QSize(16, 16))));
        updateConsole();
    });
    connect(&win.store(), &Store::scriptStateChanged, this, [this](ViewId sid) {
        if (sid == m_id) {
            updateRunState();
            updateConsole();
        }
    });
    refresh();
    updateRunState();
    updateConsole();
}

void ScriptPanel::flush()
{
    m_codeTimer.stop();
    m_win.store().setScriptCode(m_id, m_editor->toPlainText());
}

void ScriptPanel::run()
{
    flush();
    Store &store = m_win.store();
    if (store.isRunning(m_id))
        return;
    store.runScript(m_id);
}

void ScriptPanel::refresh()
{
    const Script *s = m_win.store().ws.script(m_id);
    setEnabled(s != nullptr);
    if (!s)
        return;
    if (!m_name->hasFocus() && m_name->text() != s->name)
        m_name->setText(s->name);
    {
        QSignalBlocker b(m_linked);
        m_linked->setChecked(s->linked);
    }
    // code remplacé depuis l'extérieur (ouverture d'un fichier, exemple)
    if (!m_codeTimer.isActive() && s->code != m_editor->toPlainText()) {
        const int pos = m_editor->textCursor().position();
        const int scroll = m_editor->verticalScrollBar()->value();
        m_editor->setPlainText(s->code);
        QTextCursor c = m_editor->textCursor();
        c.setPosition(std::min(pos, static_cast<int>(s->code.size())));
        m_editor->setTextCursor(c);
        m_editor->verticalScrollBar()->setValue(scroll);
        m_codeTimer.stop();
    }
}

void ScriptPanel::updateRunState()
{
    const bool running = m_win.store().isRunning(m_id);
    m_run->setVisible(!running);
    m_stop->setVisible(running);
    m_busy->setVisible(running);
    if (running) {
        m_busyFrame = 0;
        m_busy->setText(u"◐ exécution…"_s);
        m_busyTimer.start();
    } else {
        m_busyTimer.stop();
    }
}

void ScriptPanel::updateConsole()
{
    const Store &store = m_win.store();
    auto it = store.runs.find(m_id);
    const QString err = theme::errorColor().name();
    const QString weak = theme::weakColor().name();
    if (it == store.runs.end() || it->second.runs == 0) {
        m_runInfo->clear();
        m_console->setHtml(u"<p style='color:%1'>La sortie du script (print) et les erreurs s'affichent ici.</p>"_s.arg(weak));
        m_editor->setErrorLine(std::nullopt);
        return;
    }
    const ScriptRun &r = it->second;
    m_runInfo->setText(u"exécution n° %1 en %2 ms"_s.arg(r.runs).arg(qRound(r.durationMs)));
    QString html;
    if (!r.stdoutText.isEmpty()) {
        QString out = r.stdoutText;
        while (out.endsWith(u'\n'))
            out.chop(1);
        html += u"<pre style='margin:0'>%1</pre>"_s.arg(out.toHtmlEscaped());
    }
    if (r.error) {
        const auto &e = *r.error;
        QString headText;
        if (e.line && !e.interrupted)
            headText = u"⚠ <a href='line:%1' style='color:%2'>Ligne %1</a> : %3"_s.arg(*e.line).arg(err, e.message.toHtmlEscaped());
        else
            headText = u"⚠ "_s + e.message.toHtmlEscaped();
        html += u"<p style='color:%1; margin-top:6px'><b>%2</b></p>"_s.arg(err, headText);
        if (!e.interrupted && !e.traceback.trimmed().isEmpty()) {
            QString tb = e.traceback;
            while (tb.endsWith(u'\n'))
                tb.chop(1);
            html += u"<pre style='color:%1; margin:0 0 0 12px; font-size:small'>%2</pre>"_s.arg(err, tb.toHtmlEscaped());
        }
    }
    for (const QString &m : r.messages)
        html += u"<p style='color:%1; margin:2px 0'>→ %2</p>"_s.arg(weak, m.toHtmlEscaped());
    if (html.isEmpty())
        html = u"<p style='color:%1'>Exécution terminée, sans sortie.</p>"_s.arg(weak);
    m_console->setHtml(html);
    m_console->verticalScrollBar()->setValue(m_console->verticalScrollBar()->maximum());
    m_editor->setErrorLine(r.error && !r.error->interrupted ? r.error->line : std::nullopt);
}
