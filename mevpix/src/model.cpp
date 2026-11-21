#include "model.h"

#include <algorithm>
#include <charconv>
#include <cmath>

using namespace Qt::StringLiterals;

double longueur(QPointF v)
{
    return std::hypot(v.x(), v.y());
}

double Segment::longueur_px() const
{
    return longueur(b - a);
}

QPointF Segment::point_en(double t) const
{
    return a + (b - a) * t;
}

double Segment::projette(QPointF p) const
{
    const QPointF d = b - a;
    const double lenSq = QPointF::dotProduct(d, d);
    return lenSq > 0.0 ? std::clamp(QPointF::dotProduct(p - a, d) / lenSq, 0.0, 1.0) : 0.0;
}

QPointF Segment::normale() const
{
    const QPointF d = b - a;
    const double len = longueur(d);
    return len > 0.0 ? QPointF(-d.y(), d.x()) / len : QPointF();
}

QPointF aligne_sur_axe(QPointF a, QPointF b)
{
    const QPointF d = b - a;
    return std::abs(d.x()) >= std::abs(d.y()) ? QPointF(b.x(), a.y()) : QPointF(a.x(), b.y());
}

QString symbole_unite(Unite unite)
{
    switch (unite) {
    case Unite::Micrometre: return u"µm"_s;
    case Unite::Millimetre: return u"mm"_s;
    case Unite::Centimetre: return u"cm"_s;
    case Unite::Metre: return u"m"_s;
    }
    return {};
}

std::optional<Etalonnage> Etalonnage::fabrique(double referencePx, double longueurReelle, Unite unite,
                                                double diagonalePx)
{
    if (!std::isfinite(referencePx) || referencePx < Segment::LONGUEUR_MIN_PX
        || !std::isfinite(longueurReelle) || longueurReelle <= 0)
        return std::nullopt;
    const double unitesParPixel = longueurReelle / referencePx;
    if (unitesParPixel == 0 || !std::isfinite(1.0 / unitesParPixel) || !std::isfinite(diagonalePx * unitesParPixel))
        return std::nullopt;
    return Etalonnage{longueurReelle, referencePx, unite};
}

bool Etalonnage::correspond(const Etalonnage &other) const
{
    // tolérance relative (0,2 % de la longueur de l'étalon) : un simple clic sur une poignée ne
    // demande pas de revalider, mais un déplacement d'un étalon court se voit (un demi-pixel de
    // tolérance fixe changeait l'échelle d'un étalon de quelques pixels sans le dire)
    const double tolerancePx = 0.002 * std::max(referencePx, other.referencePx);
    return std::abs(referencePx - other.referencePx) <= tolerancePx
        && longueurReelle == other.longueurReelle && unite == other.unite;
}

namespace {

/// Écriture en virgule fixe, correctement arrondie.
QString virgule_fixe(double v, int decimals)
{
    std::array<char, 1024> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed, decimals);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

} // namespace

QString plus_court(double v)
{
    std::array<char, 1024> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed);
    return ec == std::errc() ? QString::fromLatin1(buf.data(), end - buf.data()) : u"0"_s;
}

bool notation_scientifique(double valeur)
{
    const double module = std::abs(valeur);
    return module >= 1e9 || (module > 0.0 && module < 1e-6);
}

QString texte_nombre(double v, int sig, QChar dec)
{
    if (v == 0.0 || !std::isfinite(v))
        return u"0"_s;
    if (notation_scientifique(v))
        return avec_separateur_decimal(QString::number(v, 'e', sig - 1), dec);
    return texte_decimal(v, decimales_significatives(v, sig), dec);
}

int decimales_significatives(double v, int sig)
{
    if (v == 0.0 || !std::isfinite(v))
        return 0;
    const int magnitude = static_cast<int>(std::floor(std::log10(std::abs(v))));
    const int decimals = std::max(sig - 1 - magnitude, 0);
    // L'arrondi peut faire passer à la puissance de 10 suivante (9,996 → 10,00) : une décimale de moins.
    const double rounded = virgule_fixe(std::abs(v), decimals).toDouble();
    if (decimals > 0 && rounded > 0.0 && std::floor(std::log10(rounded)) > magnitude)
        return decimals - 1;
    return decimals;
}

QString texte_decimal(double v, int decimals, QChar dec)
{
    return avec_separateur_decimal(virgule_fixe(v, decimals), dec);
}

QString avec_separateur_decimal(QString s, QChar dec)
{
    if (dec != u'.')
        s.replace(u'.', dec);
    return s;
}

QString texte_longueur(double px, const std::optional<Etalonnage> &calib, QChar dec)
{
    if (calib)
        return texte_nombre(calib->en_reel(px), 3, dec) + u' ' + symbole_unite(calib->unite);
    return texte_nombre(px, 3, dec) + u" px"_s;
}

QString tableau_mesures(const std::vector<double> &lengthsPx, const std::optional<Etalonnage> &calib, QChar dec,
                      const std::array<QString, 2> &header)
{
    QString out = header[0] + u'\t' + header[1] + u'\n';
    for (size_t i = 0; i < lengthsPx.size(); ++i) {
        const double px = lengthsPx[i];
        const QString value = calib ? texte_nombre(calib->en_reel(px), 3, dec) : texte_nombre(px, 3, dec);
        out += u"M%1\t%2\n"_s.arg(i + 1).arg(value);
    }
    return out;
}

std::optional<double> lit_nombre(const QString &s)
{
    QByteArray text = s.trimmed().replace(u',', u'.').toUtf8();
    // Un signe « + » est accepté devant le nombre.
    if (text.startsWith('+') && text.size() > 1 && text[1] != '-')
        text.remove(0, 1);
    double v = 0.0;
    const char *end = text.constData() + text.size();
    const auto [ptr, ec] = std::from_chars(text.constData(), end, v);
    if (text.isEmpty() || ec != std::errc() || ptr != end || !std::isfinite(v))
        return std::nullopt;
    return v;
}
