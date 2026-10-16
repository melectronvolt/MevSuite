#include "hdr.h"

#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;

namespace {

/// luminance du blanc de référence, en cd/m²
constexpr float BLANC = 203.0f;
/// luminance de crête de l'écran de référence pour HLG, en cd/m²
constexpr float CRETE_HLG = 1000.0f;
/// courbe de Möbius : linéaire jusqu'au seuil, puis compression jusqu'à la crête (en multiples du
/// blanc de référence), comme `tonemap=mobius:param=0.6:peak=4.9` de ffmpeg
constexpr float MOBIUS_SEUIL = 0.6f;

/// passage des primaires BT.2020 aux primaires BT.709, en lumière linéaire
constexpr float BT2020_VERS_BT709[3][3] = {
    {1.660491f, -0.587641f, -0.072850f},
    {-0.124550f, 1.132900f, -0.008349f},
    {-0.018151f, -0.100579f, 1.118730f},
};

/// courbe EOTF de PQ (SMPTE ST 2084) : luminance en cd/m²
float pq_eotf(float e)
{
    constexpr float M1 = 2610.0f / 16384.0f;
    constexpr float M2 = 2523.0f / 4096.0f * 128.0f;
    constexpr float C1 = 3424.0f / 4096.0f;
    constexpr float C2 = 2413.0f / 4096.0f * 32.0f;
    constexpr float C3 = 2392.0f / 4096.0f * 32.0f;
    const float p = std::pow(std::max(e, 0.0f), 1.0f / M2);
    return 10000.0f * std::pow(std::max(p - C1, 0.0f) / (C2 - C3 * p), 1.0f / M1);
}

/// inverse de l'OETF de HLG (UIT-R BT.2100) : lumière de la scène, de 0 à 1
float hlg_inverse_oetf(float e)
{
    constexpr float A = 0.17883277f;
    constexpr float B = 1.0f - 4.0f * A;
    const float c = 0.5f - A * std::log(4.0f * A);
    e = std::clamp(e, 0.0f, 1.0f);
    return e <= 0.5f ? e * e / 3.0f : (std::exp((e - c) / A) + B) / 12.0f;
}

/// inverse de l'EOTF de BT.1886 (écran de référence SDR, gamma 2,4)
float bt1886_inverse(float l)
{
    return std::pow(std::max(l, 0.0f), 1.0f / 2.4f);
}

constexpr float MAX16 = 65535.0f;

} // namespace

const float MOBIUS_CRETE = CRETE_HLG / BLANC;

std::optional<Transfert> transfert_depuis_ffprobe(const QString &nom)
{
    if (nom == u"arib-std-b67")
        return Transfert::Hlg;
    if (nom == u"smpte2084")
        return Transfert::Pq;
    return std::nullopt;
}

float mobius(float x)
{
    const float j = MOBIUS_SEUIL, crete = MOBIUS_CRETE;
    if (x <= j)
        return x;
    const float a = -j * j * (crete - 1.0f) / (j * j - 2.0f * j + crete);
    const float b = (j * j - 2.0f * j * crete + crete) / std::max(crete - 1.0f, 1e-6f);
    return (b * b + 2.0f * b * j + j * j) / (b - a) * (x + a) / (x + b);
}

ConversionHdr::ConversionHdr(Transfert transfert)
    : m_transfert(transfert)
    , m_lineaire(65536)
    , m_oetf(65536)
{
    for (int i = 0; i <= 65535; ++i) {
        const float e = i / MAX16;
        m_lineaire[i] = transfert == Transfert::Pq ? pq_eotf(e) / BLANC : hlg_inverse_oetf(e);
        m_oetf[i] = static_cast<uint8_t>(std::round(std::clamp(bt1886_inverse(e), 0.0f, 1.0f) * 255.0f));
    }
}

std::array<uint8_t, 3> ConversionHdr::pixel(const uint16_t *px) const
{
    float c[3];
    for (int k = 0; k < 3; ++k)
        c[k] = m_lineaire[px[k]];
    if (m_transfert == Transfert::Hlg) {
        // OOTF de HLG pour un écran de 1000 cd/m² (gamma du système 1,2)
        const float ys = 0.2627f * c[0] + 0.6780f * c[1] + 0.0593f * c[2];
        const float k = ys > 0.0f ? CRETE_HLG / BLANC * std::pow(ys, 0.2f) : 0.0f;
        for (float &v : c)
            v *= k;
    }
    float rgb[3];
    for (int k = 0; k < 3; ++k) {
        const float *ligne = BT2020_VERS_BT709[k];
        rgb[k] = ligne[0] * c[0] + ligne[1] * c[1] + ligne[2] * c[2];
    }
    const float sig = std::max({rgb[0], rgb[1], rgb[2], 1e-6f});
    const float facteur = mobius(sig) / sig;
    std::array<uint8_t, 3> res;
    for (int k = 0; k < 3; ++k) {
        const float v = std::clamp(rgb[k] * facteur, 0.0f, 1.0f);
        res[k] = m_oetf[static_cast<size_t>(std::round(v * MAX16))];
    }
    return res;
}

QImage ConversionHdr::convertit(int largeur, int hauteur, const std::vector<uint16_t> &donnees) const
{
    QImage sortie(largeur, hauteur, QImage::Format_RGB888);
    std::vector<int> lignes(hauteur);
    std::iota(lignes.begin(), lignes.end(), 0);
    // chaque ligne est écrite par un seul fil d'exécution : `bits()` est appelé avant, une fois
    uchar *bits = sortie.bits();
    const qsizetype pas = sortie.bytesPerLine();
    QtConcurrent::blockingMap(lignes, [&](int y) {
        uchar *ligne = bits + y * pas;
        const uint16_t *source = donnees.data() + static_cast<size_t>(y) * largeur * 3;
        for (int x = 0; x < largeur; ++x) {
            const auto px = pixel(source + 3 * x);
            std::copy(px.begin(), px.end(), ligne + 3 * x);
        }
    });
    return sortie;
}
