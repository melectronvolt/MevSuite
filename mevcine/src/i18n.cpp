#include "i18n.h"

#include <QFile>
#include <QLocale>

#include <atomic>

using namespace Qt::StringLiterals;

namespace i18n {

namespace {

std::atomic<int> g_langue{static_cast<int>(Langue::Francais)};

/// la table des traductions d'une langue, lue une fois pour toutes
const QHash<QString, QString> &table(Langue l)
{
    static const std::array<QHash<QString, QString>, TOUTES_LES_LANGUES.size()> tables = [] {
        std::array<QHash<QString, QString>, TOUTES_LES_LANGUES.size()> t;
        for (Langue l : TOUTES_LES_LANGUES)
            t[static_cast<size_t>(l)] = analyse_po(po(l));
        return t;
    }();
    return tables[static_cast<size_t>(l)];
}

/// le contenu d'une chaîne entre guillemets d'un fichier .po
QString chaine_po(QStringView s)
{
    s = s.trimmed();
    if (s.size() >= 2 && s.startsWith(u'"') && s.endsWith(u'"'))
        s = s.mid(1, s.size() - 2);
    QString res;
    res.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (c != u'\\') {
            res += c;
            continue;
        }
        if (++i >= s.size())
            break;
        switch (s[i].unicode()) {
        case 'n': res += u'\n'; break;
        case 't': res += u'\t'; break;
        case 'r': res += u'\r'; break;
        default: res += s[i];
        }
    }
    return res;
}

} // namespace

QString code(Langue l)
{
    switch (l) {
    case Langue::Francais: return u"fr"_s;
    case Langue::Anglais: return u"en"_s;
    }
    return {};
}

QString nom(Langue l)
{
    switch (l) {
    case Langue::Francais: return u"Français"_s;
    case Langue::Anglais: return u"English"_s;
    }
    return {};
}

std::optional<Langue> depuis_code(const QString &etiquette)
{
    QString code;
    for (QChar c : etiquette.trimmed()) {
        if (c.unicode() > 127 || !c.isLetter())
            break;
        code += c.toLower();
    }
    for (Langue l : TOUTES_LES_LANGUES) {
        if (i18n::code(l) == code)
            return l;
    }
    return std::nullopt;
}

bool virgule_decimale(Langue l)
{
    return l != Langue::Anglais;
}

Langue langue_du_systeme()
{
    // langues d'affichage préférées : variables LANGUAGE, LC_ALL, LC_MESSAGES, LANG sous Linux,
    // langue de l'interface de Windows
    for (const QString &l : QLocale::system().uiLanguages()) {
        if (const auto connue = depuis_code(l))
            return *connue;
    }
    return Langue::Anglais;
}

Langue langue()
{
    return static_cast<Langue>(g_langue.load(std::memory_order_relaxed));
}

void choisit(Langue l)
{
    g_langue.store(static_cast<int>(l), std::memory_order_relaxed);
}

QString po(Langue l)
{
    if (l == Langue::Francais)
        return {};
    QFile f(u":/lang/%1.po"_s.arg(code(l)));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

QString traduit(const char *source)
{
    const QString s = QString::fromUtf8(source);
    const Langue l = langue();
    if (l == Langue::Francais)
        return s;
    return table(l).value(s, s);
}

QString remplit(const QString &modele, const QStringList &args)
{
    QString s;
    s.reserve(modele.size() + 16);
    qsizetype suivant = 0;
    qsizetype pos = 0;
    while (true) {
        const qsizetype debut = modele.indexOf(u'{', pos);
        if (debut < 0)
            break;
        s += QStringView(modele).mid(pos, debut - pos);
        const qsizetype fin = modele.indexOf(u'}', debut + 1);
        if (fin < 0) {
            s += QStringView(modele).mid(debut);
            return s;
        }
        const QStringView interieur = QStringView(modele).mid(debut + 1, fin - debut - 1);
        std::optional<qsizetype> numero;
        if (interieur.isEmpty()) {
            numero = suivant++;
        } else {
            bool ok = false;
            const int n = interieur.toInt(&ok);
            if (ok && n >= 0)
                numero = n;
        }
        if (numero && *numero < args.size())
            s += args[*numero];
        else
            // une accolade qui ne marque pas un argument est gardée
            s += QStringView(modele).mid(debut, fin - debut + 1);
        pos = fin + 1;
    }
    s += QStringView(modele).mid(pos);
    return s;
}

QHash<QString, QString> analyse_po(const QString &texte)
{
    enum class Champ { Aucun, Id, Chaine };
    QHash<QString, QString> table;
    QString id, chaine;
    bool flou = false;
    Champ champ = Champ::Aucun;
    const auto range = [&] {
        if (!id.isEmpty() && !chaine.isEmpty() && !flou)
            table.insert(id, chaine);
        id.clear();
        chaine.clear();
        flou = false;
    };
    for (QStringView ligne : QStringView(texte).split(u'\n')) {
        const QStringView l = ligne.trimmed();
        if (l.startsWith(u"msgid ")) {
            if (champ == Champ::Chaine)
                range();
            id = chaine_po(l.mid(6));
            champ = Champ::Id;
        } else if (l.startsWith(u"msgstr ")) {
            chaine = chaine_po(l.mid(7));
            champ = Champ::Chaine;
        } else if (l.startsWith(u'"')) {
            if (champ == Champ::Id)
                id += chaine_po(l);
            else if (champ == Champ::Chaine)
                chaine += chaine_po(l);
        } else {
            // une ligne vide ou un commentaire : fin de l'entrée
            if (champ == Champ::Chaine)
                range();
            champ = Champ::Aucun;
            if (l.startsWith(u"#,") && l.contains(u"fuzzy"))
                flou = true;
        }
    }
    range();
    return table;
}

} // namespace i18n
