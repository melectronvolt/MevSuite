#include "detect.h"

#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

namespace detect {

namespace {

/// nombre de candidats de la recherche grossière affinés en pleine résolution
constexpr size_t CANDIDATS = 3;

/// image au format RGB888, pour un accès direct aux lignes
QImage rvb(const QImage &img)
{
    return img.format() == QImage::Format_RGB888 ? img : img.convertToFormat(QImage::Format_RGB888);
}

/// Une zone rectangulaire d'une image RVB, sans copie
struct Zone {
    const QImage *img;
    int x, y, w, h;

    /// la zone de `img` entre les coins (x1, y1) et (x2, y2) exclus
    Zone(const QImage &img, int x1, int y1, int x2, int y2)
        : img(&img)
        , x(x1)
        , y(y1)
        , w(std::max(x2 - x1, 0))
        , h(std::max(y2 - y1, 0))
    {
    }

    /// la ligne `j` de la zone, sous forme d'octets RVB
    const uint8_t *ligne(int j) const { return img->constScanLine(y + j) + 3 * x; }
};

/// une position (coin haut-gauche, dans l'image) et l'écart du motif à cet endroit
struct Candidat {
    int x, y;
    uint64_t ecart;
};

/// ordre des candidats : l'écart, puis la position (à écart égal, le premier rencontré, comme OpenCV)
bool avant(const Candidat &a, const Candidat &b)
{
    return std::tie(a.ecart, a.y, a.x) < std::tie(b.ecart, b.y, b.x);
}

/// somme des carrés des différences de deux suites d'octets
inline uint64_t sqdiff(const uint8_t *a, const uint8_t *b, int n)
{
    uint32_t s = 0;
    for (int k = 0; k < n; ++k) {
        const int d = int(a[k]) - int(b[k]);
        s += static_cast<uint32_t>(d * d);
    }
    return s;
}

/// Recherche le motif dans la zone ; pour chaque ligne de la zone, renvoie la meilleure position
/// (coin haut-gauche, dans l'image) et son écart.
std::vector<Candidat> meilleurs_par_ligne(const Zone &zone, const QImage &motif)
{
    const int mw = motif.width(), mh = motif.height();
    if (mw == 0 || mh == 0 || mw > zone.w || mh > zone.h)
        return {};
    std::vector<int> lignes(zone.h - mh + 1);
    std::iota(lignes.begin(), lignes.end(), 0);
    return QtConcurrent::blockingMapped<std::vector<Candidat>>(lignes, [&zone, &motif, mw, mh](int y) {
        Candidat meilleur{zone.x, zone.y + y, std::numeric_limits<uint64_t>::max()};
        for (int x = 0; x <= zone.w - mw; ++x) {
            uint64_t somme = 0;
            for (int j = 0; j < mh; ++j) {
                somme += sqdiff(zone.ligne(y + j) + 3 * x, motif.constScanLine(j), 3 * mw);
                // abandon précoce : on ne fera pas mieux
                if (somme >= meilleur.ecart)
                    break;
            }
            if (somme < meilleur.ecart)
                meilleur = {zone.x + x, zone.y + y, somme};
        }
        return meilleur;
    });
}

/// la meilleure position du motif dans la zone, et son écart
std::optional<Candidat> match_template(const Zone &zone, const QImage &motif)
{
    const auto lignes = meilleurs_par_ligne(zone, motif);
    if (lignes.empty())
        return std::nullopt;
    return *std::min_element(lignes.begin(), lignes.end(), avant);
}

struct Reduites {
    QImage image, motif;
    int f;
};

/// Recherche le motif entre les coins (x1, y1) et (x2, y2) de l'image ; renvoie la meilleure position
/// (coin haut-gauche) et son écart. `reduites` : l'image et le motif réduits d'un facteur `f`, pour
/// une recherche grossière suivie d'un affinage.
std::optional<Candidat> cherche(const QImage &motif, const QImage &image, const std::optional<Reduites> &reduites,
                                int x1, int y1, int x2, int y2)
{
    if (!reduites)
        return match_template(Zone(image, x1, y1, x2, y2), motif);
    const int f = reduites->f;
    const QImage &image_r = reduites->image, &motif_r = reduites->motif;
    const int mw = motif.width(), mh = motif.height();
    const Zone zone_r(image_r, x1 / f, y1 / f, std::min(x2 / f, image_r.width()), std::min(y2 / f, image_r.height()));
    // les meilleurs candidats, assez éloignés les uns des autres
    auto lignes = meilleurs_par_ligne(zone_r, motif_r);
    std::sort(lignes.begin(), lignes.end(), avant);
    const int distance_min = std::max(motif_r.width(), motif_r.height()) / 2;
    std::vector<Candidat> candidats;
    for (const Candidat &c : lignes) {
        const bool loin = std::all_of(candidats.begin(), candidats.end(), [&](const Candidat &d) {
            return std::max(std::abs(d.x - c.x), std::abs(d.y - c.y)) > distance_min;
        });
        if (loin) {
            candidats.push_back(c);
            if (candidats.size() == CANDIDATS)
                break;
        }
    }
    // affinage en pleine résolution, à 2 pixels réduits près
    std::optional<Candidat> meilleur;
    for (const Candidat &c : candidats) {
        const int x = c.x * f, y = c.y * f;
        const Zone zone(image, std::max(x - 2 * f, x1), std::max(y - 2 * f, y1), std::min(x + 2 * f + mw, x2),
                        std::min(y + 2 * f + mh, y2));
        const auto trouve = match_template(zone, motif);
        if (trouve && (!meilleur || avant(*trouve, *meilleur)))
            meilleur = trouve;
    }
    return meilleur;
}

} // namespace

QImage reduit(const QImage &source, int f)
{
    const QImage img = rvb(source);
    const int w = img.width() / f, h = img.height() / f;
    QImage res(w, h, QImage::Format_RGB888);
    std::vector<int> lignes(h);
    std::iota(lignes.begin(), lignes.end(), 0);
    // chaque ligne est écrite par un seul fil d'exécution : `bits()` est appelé avant, une fois
    uchar *bits = res.bits();
    const qsizetype pas = res.bytesPerLine();
    QtConcurrent::blockingMap(lignes, [&](int y) {
        uchar *ligne = bits + y * pas;
        for (int x = 0; x < w; ++x) {
            uint32_t somme[3] = {0, 0, 0};
            for (int dy = 0; dy < f; ++dy) {
                const uchar *px = img.constScanLine(y * f + dy) + 3 * x * f;
                for (int k = 0; k < f; ++k) {
                    for (int c = 0; c < 3; ++c)
                        somme[c] += px[3 * k + c];
                }
            }
            for (int c = 0; c < 3; ++c)
                ligne[3 * x + c] = static_cast<uchar>(somme[c] / static_cast<uint32_t>(f * f));
        }
    });
    return res;
}

int facteur(const QImage &image, const QImage &motif)
{
    const int grand = std::max(image.width(), image.height());
    const int petit_motif = std::min(motif.width(), motif.height());
    int f = 1;
    while (f < 4 && grand / (2 * f) >= 640 && petit_motif / (2 * f) >= 12)
        f *= 2;
    return f;
}

std::optional<QPoint> detect_part(const QImage &motif_source, const QImage &image_source, std::optional<QPoint> zone_proche)
{
    const QImage motif = rvb(motif_source), image = rvb(image_source);
    const int iw = image.width(), ih = image.height();
    const int mw = motif.width(), mh = motif.height();
    const int f = facteur(image, motif);
    std::optional<Reduites> reduites;
    if (f > 1)
        reduites = Reduites{reduit(image, f), reduit(motif, f), f};
    if (zone_proche) {
        // un rayon assez petit par rapport à la taille de l'image, pour une première recherche dans un
        // extrait de l'image
        const int r = (iw + ih) / 10;
        const int cx = zone_proche->x() + mw / 2, cy = zone_proche->y() + mh / 2;
        const int x1 = std::max(cx - r, 0), y1 = std::max(cy - r, 0);
        const int x2 = std::min(cx + r, iw), y2 = std::min(cy + r, ih);
        if (x2 > x1 + mw && y2 > y1 + mh) {
            if (const auto c = cherche(motif, image, reduites, x1, y1, x2, y2)) {
                // si l'écart est assez faible, on considère qu'on a trouvé
                const uint64_t tolerance = 120 * static_cast<uint64_t>(mw) * mh;
                if (c->ecart < tolerance)
                    return QPoint(c->x, c->y);
            }
        }
    }
    // sinon on cherche dans toute l'image
    if (const auto c = cherche(motif, image, reduites, 0, 0, iw, ih))
        return QPoint(c->x, c->y);
    return std::nullopt;
}

std::optional<QImage> decoupe(const QImage &image, int x, int y, int w, int h)
{
    x = std::clamp(x, 0, image.width());
    y = std::clamp(y, 0, image.height());
    w = std::min(w, image.width() - x);
    h = std::min(h, image.height() - y);
    if (w <= 0 || h <= 0)
        return std::nullopt;
    return rvb(image).copy(x, y, w, h);
}

} // namespace detect
