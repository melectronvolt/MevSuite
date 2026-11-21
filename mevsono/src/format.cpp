#include "format.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Écriture en virgule fixe, correctement arrondie.
QString virgule_fixe(double v, int decimals)
{
    std::array<char, 1100> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, decimals);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

QString avec_separateur_decimal(QString s, QChar dec)
{
    if (dec != u'.')
        s.replace(u'.', dec);
    return s;
}

// L’exposant se lit après l’arrondi. Ainsi 9,9996 à quatre chiffres devient 1,000 × 10¹.
int exposant_decimal(const QString &scientifique)
{
    return QStringView(scientifique).sliced(scientifique.indexOf(u'e') + 1).toInt();
}

} // namespace

QString texte_nombre(double valeur, int chiffres, QChar separateur)
{
    if (valeur == 0.0 || !std::isfinite(valeur))
        return u"0"_s;
    const QString scientifique = QString::number(std::abs(valeur), 'e', chiffres - 1);
    const int decimales = chiffres - 1 - exposant_decimal(scientifique);
    if (decimales >= 0)
        return texte_decimal(valeur, decimales, separateur);

    // Exemple : 1,23 × 10⁴ donne les trois chiffres « 123 » suivis de deux zéros.
    QString entier = scientifique.first(scientifique.indexOf(u'e')).remove(u'.');
    entier += QString(-decimales, u'0');
    return valeur < 0 ? u'−' + entier : entier;
}

int decimales_significatives(double valeur, int chiffres)
{
    if (valeur == 0.0 || !std::isfinite(valeur))
        return 0;
    const QString scientifique = QString::number(std::abs(valeur), 'e', chiffres - 1);
    return std::max(0, chiffres - 1 - exposant_decimal(scientifique));
}

QString texte_decimal(double v, int decimals, QChar dec)
{
    QString s = virgule_fixe(v, decimals);
    // « -0,0 » s'écrit « 0,0 »
    if (s.startsWith(u'-') && std::all_of(s.begin() + 1, s.end(), [](QChar c) { return c == u'0' || c == u'.'; }))
        s.remove(0, 1);
    if (s.startsWith(u'-'))
        s.replace(0, 1, u"−"_s);
    return avec_separateur_decimal(s, dec);
}

QString nombre_csv(double v, int decimals, QChar dec)
{
    return avec_separateur_decimal(virgule_fixe(v, decimals), dec);
}

QString plus_court(double v)
{
    std::array<char, 1100> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

QString texte_temps(double t, QChar dec)
{
    if (std::abs(t) < 1.0)
        return texte_nombre(t * 1000.0, 4, dec) + u" ms"_s;
    return texte_nombre(t, 5, dec) + u" s"_s;
}

QString texte_frequence(double f, QChar dec)
{
    const int decimals = std::abs(f) < 100.0 ? 2 : std::abs(f) < 10'000.0 ? 1 : 0;
    return texte_decimal(f, decimals, dec) + u" Hz"_s;
}

QString texte_position(double t, double rate, QChar dec)
{
    const int digits = std::clamp(int(std::ceil(std::log10(std::max(rate, 1.0)))), 4, 9);
    return texte_decimal(t, digits, dec) + u" s"_s;
}

QString texte_duree(double t, QChar dec)
{
    if (t < 1.0)
        return texte_nombre(t * 1000.0, 3, dec) + u" ms"_s;
    if (t < 60.0)
        return texte_nombre(t, 3, dec) + u" s"_s;
    const auto s = static_cast<long long>(std::llround(t));
    return u"%1 min %2 s"_s.arg(s / 60).arg(s % 60, 2, 10, QChar(u'0'));
}

QString texte_frequence_court(double f, QChar dec)
{
    const auto sansZerosFinaux = [dec](double v) {
        QString s = texte_nombre(v, 3, dec);
        if (s.contains(dec)) {
            while (s.endsWith(u'0'))
                s.chop(1);
            while (s.endsWith(dec))
                s.chop(1);
        }
        return s;
    };
    if (f >= 1000.0)
        return sansZerosFinaux(f / 1000.0) + u" kHz"_s;
    return sansZerosFinaux(f) + u" Hz"_s;
}

double en_decibels(double a)
{
    return 20.0 * std::log10(std::max(a, 1e-9));
}
