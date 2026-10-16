#include "core/csv.h"

#include "core/format.h"

#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <map>

#include <cmath>

using namespace Qt::StringLiterals;

namespace csv {

namespace {

bool emptyRow(const QStringList &cells)
{
    return std::all_of(cells.begin(), cells.end(), [](const QString &c) { return c.isEmpty(); });
}

bool isNumericRow(const QStringList &row, Decimal decimal)
{
    bool any = false;
    for (const QString &c : row) {
        if (c.isEmpty())
            continue;
        if (!number(c, decimal))
            return false;
        any = true;
    }
    return any;
}

/// Valeur la plus fréquente (la plus petite en cas d'égalité) ; 0 pour une liste vide.
int mostFrequent(const std::vector<int> &values)
{
    std::map<int, int> count;
    for (int v : values)
        ++count[v];
    int best = 0, most = 0;
    for (const auto &[v, n] : count)
        if (n > most) {
            best = v;
            most = n;
        }
    return best;
}

/// Choisit le séparateur qui donne le plus de lignes au même nombre de cases (au moins deux).
/// À égalité : tabulation, point-virgule, virgule, puis espaces. Une virgule qui couperait des
/// nombres séparés par des espaces (« 0,5 1,2 ») est écartée.
QChar detectSeparator(const QStringList &lines)
{
    QStringList sample;
    for (const QString &l : lines) {
        if (!l.trimmed().isEmpty())
            sample << l;
        if (sample.size() >= 50)
            break;
    }
    static const QRegularExpression spacedDigits(u"\\d\\s+\\d"_s);
    QChar chosen = SPACES;
    qsizetype best = -1;
    for (QChar c : {QChar(u'\t'), QChar(u';'), QChar(u','), SPACES}) {
        std::vector<int> widths;
        qsizetype present = 0, penalty = 0;
        for (const QString &l : sample) {
            const QStringList cells = splitLine(l, c);
            widths.push_back(static_cast<int>(cells.size()));
            if (c == SPACES ? cells.size() > 1 : l.contains(c))
                ++present;
            if (c == u',' && std::any_of(cells.begin(), cells.end(), [](const QString &x) { return x.contains(spacedDigits); }))
                ++penalty;
        }
        if (present * 2 < sample.size())
            continue;
        const int usual = mostFrequent(widths);
        if (usual < 2)
            continue;
        const qsizetype score = std::count(widths.begin(), widths.end(), usual) - penalty;
        if (score > best) {
            best = score;
            chosen = c;
        }
    }
    return chosen;
}

QString unquote(const QString &s)
{
    QString t = s;
    if (t.size() >= 2 && t.startsWith(u'"') && t.endsWith(u'"'))
        t = t.mid(1, t.size() - 2);
    t.replace(u"\"\""_s, u"\""_s);
    return t;
}

QString numberText(double v, bool decimalComma)
{
    if (!std::isfinite(v))
        return {};
    QString s = shortest(v);
    if (decimalComma)
        s.replace(u'.', u',');
    return s;
}

QString stripBrackets(QString unit)
{
    while (!unit.isEmpty() && (unit.startsWith(u'(') || unit.startsWith(u'[')))
        unit.remove(0, 1);
    while (!unit.isEmpty() && (unit.endsWith(u')') || unit.endsWith(u']')))
        unit.chop(1);
    return unit.trimmed();
}

} // namespace

QStringList textLines(const QString &raw)
{
    QString text = raw;
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    // fins de ligne Windows, Unix, ou des anciens Mac (retour chariot seul)
    QStringList lines = text.contains(u'\n') ? text.split(u'\n') : text.split(u'\r');
    for (QString &l : lines)
        if (l.endsWith(u'\r'))
            l.chop(1);
    while (!lines.isEmpty() && lines.back().trimmed().isEmpty())
        lines.removeLast();
    return lines;
}

QStringList splitLine(const QString &line, QChar separator)
{
    if (separator == SPACES) {
        QStringList out;
        for (const QString &c : line.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts))
            out << unquote(c);
        return out.isEmpty() ? QStringList{QString()} : out;
    }
    QStringList out;
    QString cur;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (quoted) {
            if (c != u'"')
                cur += c;
            else if (i + 1 < line.size() && line[i + 1] == u'"')
                cur += line[++i];
            else
                quoted = false;
        } else if (c == u'"' && cur.trimmed().isEmpty()) {
            quoted = true;
            cur.clear();
        } else if (c == separator) {
            out << cur.trimmed();
            cur.clear();
        } else {
            cur += c;
        }
    }
    out << cur.trimmed();
    return out;
}

std::optional<double> number(const QString &cell, Decimal decimal)
{
    if (cell.contains(decimal == Decimal::Comma ? u'.' : u','))
        return std::nullopt;
    return parseNumber(cell);
}

ReadOptions detect(const QStringList &lines, Decimal fallback)
{
    ReadOptions o;
    o.separator = detectSeparator(lines);
    std::vector<QStringList> cells;
    for (const QString &l : lines)
        cells.push_back(splitLine(l, o.separator));
    // séparateur décimal : celui des nombres du fichier (la virgule ne peut pas servir deux fois)
    if (o.separator == u',') {
        o.decimal = Decimal::Point;
    } else {
        static const QRegularExpression comma(u"^[+\\-−]?\\d+,\\d+([eE][+\\-−]?\\d+)?$"_s);
        static const QRegularExpression point(u"^[+\\-−]?\\d*\\.\\d+([eE][+\\-−]?\\d+)?$"_s);
        qsizetype commas = 0, points = 0;
        for (size_t i = 0; i < cells.size() && i < 500; ++i)
            for (const QString &c : cells[i]) {
                const QString t = QString(c).remove(u' ').remove(QChar(0xa0)).remove(QChar(0x202f));
                commas += comma.match(t).hasMatch();
                points += point.match(t).hasMatch();
            }
        o.decimal = commas > points ? Decimal::Comma : points > commas ? Decimal::Point : fallback;
    }
    // première ligne de valeurs : toutes ses cases non vides sont des nombres
    int first = -1;
    for (size_t i = 0; i < cells.size(); ++i)
        if (isNumericRow(cells[i], o.decimal)) {
            first = static_cast<int>(i);
            break;
        }
    if (first < 0)
        return o;
    o.firstLine = first;
    // en-têtes : la dernière ligne non vide avant les valeurs donne les noms, ou l'avant-dernière
    // si la dernière ressemble à une ligne d'unités
    std::vector<int> before;
    for (int i = 0; i < first; ++i)
        if (!emptyRow(cells[static_cast<size_t>(i)]))
            before.push_back(i);
    if (before.size() == 1) {
        o.nameLine = before.front();
    } else if (before.size() >= 2) {
        const QStringList &last = cells[static_cast<size_t>(before.back())];
        const bool units = std::all_of(last.begin(), last.end(), [](const QString &c) { return c.size() <= 8 && !c.contains(u' '); });
        if (units) {
            o.nameLine = before[before.size() - 2];
            o.unitLine = before.back();
        } else {
            o.nameLine = before.back();
        }
    }
    return o;
}

Reading read(const QStringList &lines, const ReadOptions &o)
{
    Reading out;
    const auto cellsOf = [&](int i) { return i >= 0 && i < lines.size() ? splitLine(lines[i], o.separator) : QStringList(); };
    const QStringList names = cellsOf(o.nameLine), units = cellsOf(o.unitLine);
    std::vector<QStringList> data;
    std::vector<int> widths;
    for (int i = std::max(0, o.firstLine); i < lines.size(); ++i) {
        if (i == o.nameLine || i == o.unitLine)
            continue;
        QStringList c = splitLine(lines[i], o.separator);
        const bool empty = emptyRow(c);
        if (empty && o.skipEmpty) {
            ++out.skipped;
            continue;
        }
        if (!empty)
            widths.push_back(static_cast<int>(c.size()));
        out.rows.push_back(i);
        data.push_back(std::move(c));
    }
    out.usualWidth = mostFrequent(widths);
    qsizetype ncols = names.size();
    for (size_t r = 0; r < data.size(); ++r) {
        ncols = std::max(ncols, data[r].size());
        if (!emptyRow(data[r]) && data[r].size() != out.usualWidth) {
            if (out.ragged++ == 0)
                out.firstRagged = out.rows[r];
        }
    }
    const Decimal other = o.decimal == Decimal::Comma ? Decimal::Point : Decimal::Comma;
    for (qsizetype j = 0; j < ncols; ++j) {
        auto [rawName, unit] = splitHeader(names.value(j));
        if (unit.isEmpty())
            unit = stripBrackets(units.value(j));
        Column col{sanitizeName(rawName, u"c%1"_s.arg(j + 1)), unit, {}};
        col.values.reserve(data.size());
        for (size_t r = 0; r < data.size(); ++r) {
            const QString cell = data[r].value(j);
            const auto v = number(cell, o.decimal);
            if (!v && !cell.isEmpty()) {
                if (out.badCells++ == 0)
                    out.firstBad = {out.rows[r], static_cast<int>(j), cell};
                if (number(cell, other))
                    ++out.otherDecimal;
            }
            col.values.push_back(v.value_or(NAN));
        }
        while (!col.values.empty() && std::isnan(col.values.back()))
            col.values.pop_back();
        out.columns.push_back(std::move(col));
    }
    return out;
}

std::vector<QStringList> splitCells(const QString &text)
{
    const QStringList lines = textLines(text);
    const QChar sep = detectSeparator(lines);
    std::vector<QStringList> rows;
    for (const QString &l : lines)
        rows.push_back(splitLine(l, sep));
    while (!rows.empty() && emptyRow(rows.back()))
        rows.pop_back();
    return rows;
}

std::pair<QString, QString> splitHeader(const QString &cell)
{
    const QString c = cell.trimmed();
    for (auto [open, close] : {std::make_pair(u'(', u')'), std::make_pair(u'[', u']')}) {
        if (c.endsWith(close)) {
            const qsizetype i = c.lastIndexOf(open);
            if (i >= 0) {
                const QString name = c.left(i).trimmed();
                const QString unit = c.mid(i + 1, c.size() - i - 2).trimmed();
                if (!name.isEmpty())
                    return {name, unit};
            }
        }
    }
    const qsizetype k = c.indexOf(u" / "_s);
    if (k >= 0)
        return {c.left(k).trimmed(), c.mid(k + 3).trimmed()};
    return {c, QString()};
}

QString sanitizeName(const QString &raw, const QString &fallback)
{
    QString s;
    for (QChar c : raw.trimmed()) {
        if (c.isLetterOrNumber() || c == u'_')
            s += c;
        else if (c == u' ' || c == u'-' || c == u'.')
            s += u'_';
    }
    while (s.contains(u"__"_s))
        s.replace(u"__"_s, u"_"_s);
    while (s.startsWith(u'_'))
        s.remove(0, 1);
    while (s.endsWith(u'_'))
        s.chop(1);
    if (s.isEmpty())
        return fallback;
    if (s[0].isDigit())
        return u"g_"_s + s;
    return s;
}

std::optional<Parsed> parse(const QString &text, QString *error)
{
    const QStringList lines = textLines(text);
    if (lines.isEmpty()) {
        if (error)
            *error = u"aucune donnée"_s;
        return std::nullopt;
    }
    // copier-coller : les rangées sans valeur gardent leur place
    ReadOptions o = detect(lines);
    o.skipEmpty = false;
    Reading r = read(lines, o);
    Parsed out;
    out.ignored = r.badCells;
    // colonnes sans aucune valeur numérique (commentaires…) : ignorées
    for (Column &c : r.columns)
        if (!c.values.empty())
            out.columns.push_back(std::move(c));
    if (out.columns.empty()) {
        if (error)
            *error = u"aucune valeur numérique trouvée"_s;
        return std::nullopt;
    }
    return out;
}

std::vector<QId> import(Document &doc, const Parsed &parsed)
{
    std::vector<QId> ids;
    for (const auto &c : parsed.columns) {
        if (auto id = doc.addExperimental(doc.uniqueName(c.name), c.unit, c.values))
            ids.push_back(*id);
    }
    return ids;
}

QString exportText(const Document &doc, const std::vector<QId> &ids, ExportOptions opts)
{
    std::vector<const Quantity *> qs;
    for (QId id : ids) {
        if (const Quantity *q = doc.get(id))
            qs.push_back(q);
    }
    const QString sep(opts.separator);
    QStringList header, units;
    size_t n = 0;
    for (const Quantity *q : qs) {
        switch (opts.header) {
        case ExportOptions::Header::NameUnit: header << (q->unit.isEmpty() ? q->name : u"%1 (%2)"_s.arg(q->name, q->unit)); break;
        case ExportOptions::Header::TwoLines:
            header << q->name;
            units << q->unit;
            break;
        case ExportOptions::Header::NameOnly: header << q->name; break;
        }
        n = std::max(n, q->values.size());
    }
    // un nom ou une unité qui contient le séparateur (« V, crête »), un guillemet ou un saut de ligne :
    // entre guillemets, comme le veut le format CSV
    const auto field = [&sep](const QString &f) {
        if (!f.contains(sep) && !f.contains(u'"') && !f.contains(u'\n') && !f.contains(u'\r'))
            return f;
        QString q = f;
        q.replace(u'"', u"\"\""_s);
        return u'"' + q + u'"';
    };
    for (QString &h : header)
        h = field(h);
    for (QString &u : units)
        u = field(u);
    QString out = header.join(sep) + u"\r\n"_s;
    if (opts.header == ExportOptions::Header::TwoLines)
        out += units.join(sep) + u"\r\n"_s;
    for (size_t i = 0; i < n; ++i) {
        QStringList row;
        for (const Quantity *q : qs)
            row << numberText(q->value(i), opts.decimalComma);
        out += row.join(sep) + u"\r\n"_s;
    }
    return out;
}

QByteArray encodeText(const QString &text, Encoding encoding)
{
    switch (encoding) {
    case Encoding::Utf8Bom: return QByteArray("\xEF\xBB\xBF") + text.toUtf8();
    case Encoding::Utf8: return text.toUtf8();
    case Encoding::Windows1252: break;
    }
    // Windows-1252 : Latin-1, plus les caractères propres des octets 0x80 à 0x9F ; le reste devient « ? »
    static const char16_t high[32] = {u'€', 0, u'‚', u'ƒ', u'„', u'…', u'†', u'‡', u'ˆ', u'‰', u'Š', u'‹', u'Œ', 0, u'Ž', 0,
                                      0, u'‘', u'’', u'“', u'”', u'•', u'–', u'—', u'˜', u'™', u'š', u'›', u'œ', 0, u'ž', u'Ÿ'};
    QByteArray out;
    out.reserve(text.size());
    for (QChar c : text) {
        const char16_t u = c.unicode();
        char b = '?';
        if (u < 0x80 || (u >= 0xa0 && u <= 0xff)) {
            b = static_cast<char>(u);
        } else {
            for (int k = 0; k < 32; ++k)
                if (high[k] && high[k] == u)
                    b = static_cast<char>(0x80 + k);
        }
        out += b;
    }
    return out;
}

QString decodeText(const QByteArray &bytes)
{
    auto utf8 = QStringDecoder(QStringDecoder::Utf8);
    QString s = utf8(bytes);
    if (!utf8.hasError())
        return s;
    // Windows-1252 : les octets 0x80 à 0x9F ont des caractères propres
    static const char16_t high[32] = {u'€', 0x81, u'‚', u'ƒ', u'„', u'…', u'†', u'‡', u'ˆ', u'‰', u'Š', u'‹', u'Œ', 0x8d, u'Ž', 0x8f,
                                      0x90, u'‘', u'’', u'“', u'”', u'•', u'–', u'—', u'˜', u'™', u'š', u'›', u'œ', 0x9d, u'ž', u'Ÿ'};
    QString out;
    out.reserve(bytes.size());
    for (char ch : bytes) {
        const auto b = static_cast<unsigned char>(ch);
        out += (b >= 0x80 && b <= 0x9f) ? QChar(high[b - 0x80]) : QChar(b);
    }
    return out;
}

} // namespace csv
