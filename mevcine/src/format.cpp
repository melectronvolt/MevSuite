#include "format.h"

#include "i18n.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

using namespace Qt::StringLiterals;

namespace {

/// les écritures des unités acceptées à la fin d'une longueur, dans les langues de MeVCine
const std::array<std::pair<QString, Unite>, 23> UNITES = {{
    {u"m"_s, Unite::Metre},
    {u"mètre"_s, Unite::Metre},
    {u"mètres"_s, Unite::Metre},
    {u"metre"_s, Unite::Metre},
    {u"metres"_s, Unite::Metre},
    {u"meter"_s, Unite::Metre},
    {u"meters"_s, Unite::Metre},
    {u"Meter"_s, Unite::Metre},
    {u"metro"_s, Unite::Metre},
    {u"metros"_s, Unite::Metre},
    {u"metri"_s, Unite::Metre},
    {u"cm"_s, Unite::Centimetre},
    {u"centimètre"_s, Unite::Centimetre},
    {u"centimètres"_s, Unite::Centimetre},
    {u"centimetre"_s, Unite::Centimetre},
    {u"centimetres"_s, Unite::Centimetre},
    {u"centimeter"_s, Unite::Centimetre},
    {u"centimeters"_s, Unite::Centimetre},
    {u"mm"_s, Unite::Millimetre},
    {u"millimètre"_s, Unite::Millimetre},
    {u"millimètres"_s, Unite::Millimetre},
    {u"millimeter"_s, Unite::Millimetre},
    {u"millimeters"_s, Unite::Millimetre},
}};

bool chiffres_ok(QStringView t)
{
    return std::all_of(t.begin(), t.end(), [](QChar c) { return c >= u'0' && c <= u'9'; });
}

/// la fin commune des messages d'erreur : comment écrire une longueur
QString exemples()
{
    return trad("Exemples : 0,85 ou 8,5E-1 ou 1E-10");
}

/// un nombre sans espace ni unité, éventuellement avec une puissance de 10
std::optional<double> lit_nombre(QString s)
{
    s.replace(u"**"_s, u"^"_s);
    if (const qsizetype i = s.indexOf(u"10^"_s); i >= 0) {
        const QString exposant = s.mid(i + 3);
        QStringView chiffres = exposant;
        if (chiffres.startsWith(u'+') || chiffres.startsWith(u'-'))
            chiffres = chiffres.mid(1);
        if (chiffres.isEmpty() || !chiffres_ok(chiffres))
            return std::nullopt;
        bool ok = false;
        const int e = exposant.toInt(&ok);
        if (!ok)
            return std::nullopt;
        const QString avant = s.left(i);
        double mantisse = 1.0;
        if (!avant.isEmpty()) {
            static const QString SIGNES = u"×xX*·⋅"_s;
            if (!SIGNES.contains(avant.back()))
                return std::nullopt;
            const auto m = parse_float(avant.chopped(1));
            if (!m)
                return std::nullopt;
            mantisse = *m;
        }
        return mantisse * std::pow(10.0, e);
    }
    // « E-10 » sans mantisse : 1E-10
    if (s.startsWith(u'e') || s.startsWith(u'E'))
        s.prepend(u'1');
    return parse_float(s);
}

} // namespace

QString fmt_g(double v, int p)
{
    return QString::fromStdString(std::format("{:.{}g}", v, std::max(p, 1)));
}

std::optional<double> parse_float(const QString &saisie)
{
    QString s = saisie.trimmed();
    s.replace(u',', u'.');
    if (s.isEmpty() || std::any_of(s.begin(), s.end(), [](QChar c) { return c.isSpace(); }))
        return std::nullopt;
    QStringView corps = s;
    if (corps.startsWith(u'+') || corps.startsWith(u'-'))
        corps = corps.mid(1);
    QStringView mantisse = corps;
    std::optional<QStringView> exposant;
    for (qsizetype i = 0; i < corps.size(); ++i) {
        if (corps[i] == u'e' || corps[i] == u'E') {
            mantisse = corps.left(i);
            exposant = corps.mid(i + 1);
            break;
        }
    }
    bool mantisse_ok;
    if (const qsizetype point = mantisse.indexOf(u'.'); point >= 0) {
        const QStringView a = mantisse.left(point), b = mantisse.mid(point + 1);
        mantisse_ok = chiffres_ok(a) && chiffres_ok(b) && !(a.isEmpty() && b.isEmpty());
    } else {
        mantisse_ok = !mantisse.isEmpty() && chiffres_ok(mantisse);
    }
    bool exposant_ok = true;
    if (exposant) {
        QStringView e = *exposant;
        if (e.startsWith(u'+') || e.startsWith(u'-'))
            e = e.mid(1);
        exposant_ok = !e.isEmpty() && chiffres_ok(e);
    }
    if (!mantisse_ok || !exposant_ok)
        return std::nullopt;
    // QString::toDouble ne dépend pas de la langue du système (point décimal)
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok && !std::isinf(v))
        return std::nullopt;
    return v;
}

QChar separateur_decimal()
{
    return i18n::virgule_decimale(i18n::langue()) ? u',' : u'.';
}

QString nombre_local(double v, int p)
{
    // ni « -0 », ni « -0,0 » : un zéro est un zéro
    if (std::abs(v) < 1e-300)
        v = 0.0;
    return fmt_g(v, p).replace(u'.', separateur_decimal());
}

QString texte_reel(double v)
{
    return QString::fromStdString(std::format("{}", v));
}

QString fmt_py(double v)
{
    const QString s = fmt_g(v, 6);
    const bool flottant = std::any_of(s.begin(), s.end(), [](QChar c) {
        return c == u'.' || c == u'e' || c == u'n' || c == u'i';
    });
    return flottant ? s : s + u".0"_s;
}

QString symbole(Unite u)
{
    switch (u) {
    case Unite::Millimetre: return u"mm"_s;
    case Unite::Centimetre: return u"cm"_s;
    case Unite::Metre: return u"m"_s;
    }
    return {};
}

double en_metre(Unite u)
{
    switch (u) {
    case Unite::Millimetre: return 1e-3;
    case Unite::Centimetre: return 1e-2;
    case Unite::Metre: return 1.0;
    }
    return 1.0;
}

std::optional<Unite> unite_depuis_symbole(const QString &s)
{
    for (Unite u : TOUTES_LES_UNITES) {
        if (symbole(u) == s.trimmed())
            return u;
    }
    return std::nullopt;
}

QString texte_longueur(double metres, Unite u)
{
    return nombre_local(metres / en_metre(u), 4) + u' ' + symbole(u);
}

std::expected<Longueur, QString> parse_longueur(const QString &saisie, Unite unite)
{
    QString s;
    for (QChar c : saisie) {
        if (!c.isSpace())
            s += c;
    }
    if (s.isEmpty())
        return std::unexpected(trad("Indiquez la longueur de l'étalon. {}", exemples()));
    // l'unité éventuelle : les lettres de la fin
    qsizetype debut_unite = s.size();
    while (debut_unite > 0 && s[debut_unite - 1].isLetter())
        --debut_unite;
    QString nombre = s.left(debut_unite), ecrite = s.mid(debut_unite);
    // « 1E » : la lettre E n'est pas une unité, mais un exposant inachevé
    if (ecrite.compare(u"e"_s, Qt::CaseInsensitive) == 0) {
        nombre = s;
        ecrite.clear();
    }
    if (!ecrite.isEmpty()) {
        const auto u = std::find_if(UNITES.begin(), UNITES.end(), [&](const auto &x) { return x.first == ecrite; });
        if (u == UNITES.end())
            return std::unexpected(
                trad("« {} » : l'unité doit être le millimètre (mm), le centimètre (cm) ou le mètre (m). {}",
                     saisie.trimmed(), exemples()));
        unite = u->second;
    }
    const auto valeur = lit_nombre(nombre);
    if (!valeur)
        return std::unexpected(trad("« {} » n'est pas un nombre. {}", saisie.trimmed(), exemples()));
    if (!(std::isfinite(*valeur) && *valeur > 0.0))
        return std::unexpected(trad("La longueur doit être un nombre positif, non nul."));
    return Longueur{*valeur, unite};
}
