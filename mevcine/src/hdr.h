// Conversion des images HDR des téléphones (HLG, PQ ; couleurs primaires BT.2020) en images
// ordinaires (SDR, BT.709), pour l'affichage et le suivi automatique. Sans cela, elles paraissent
// grises et délavées.
//
// C'est la méthode de la chaîne de filtres de ffmpeg
// `zscale=t=linear:npl=203, zscale=p=bt709, tonemap=mobius, zscale=t=bt709`, faite ici pour
// fonctionner avec n'importe quelle version de ffmpeg :
// - linéarisation ; le blanc de référence est à 203 cd/m² (recommandation UIT-R BT.2408) : 75 % du
//   signal HLG, 58 % du signal PQ ;
// - passage aux couleurs primaires BT.709 ;
// - compression des hautes lumières (courbe de Möbius), appliquée à la composante la plus forte pour
//   ne pas altérer les teintes ;
// - codage pour un écran ordinaire (BT.1886, gamma 2,4) : comme zscale, on convertit la lumière
//   affichée par l'écran HDR en lumière affichée par l'écran SDR.

#pragma once

#include <QImage>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

/// fonction de transfert d'une vidéo HDR
enum class Transfert {
    /// Hybrid Log-Gamma (iPhone, la plupart des téléphones)
    Hlg,
    /// Perceptual Quantizer (vidéos HDR10)
    Pq,
};

/// d'après le nom donné par ffprobe (champ color_transfer)
std::optional<Transfert> transfert_depuis_ffprobe(const QString &nom);

/// la courbe de Möbius de ffmpeg (vf_tonemap.c)
float mobius(float x);

/// crête de la courbe de Möbius, en multiples du blanc de référence
extern const float MOBIUS_CRETE;

/// Une conversion HDR → SDR, avec ses tables précalculées
class ConversionHdr {
public:
    explicit ConversionHdr(Transfert transfert);

    /// convertit un pixel HDR (R'G'B' sur 16 bits, primaires BT.2020)
    std::array<uint8_t, 3> pixel(const uint16_t *px) const;

    /// convertit une image HDR : `donnees` contient les composantes R'G'B' de chaque pixel, sur
    /// 16 bits
    QImage convertit(int largeur, int hauteur, const std::vector<uint16_t> &donnees) const;

private:
    Transfert m_transfert;
    /// valeur codée sur 16 bits → lumière linéaire (PQ : relative au blanc de référence ; HLG :
    /// lumière de la scène, de 0 à 1)
    std::vector<float> m_lineaire;
    /// lumière affichée de 0 à 1, sur 16 bits → valeur SDR sur 8 bits
    std::vector<uint8_t> m_oetf;
};
