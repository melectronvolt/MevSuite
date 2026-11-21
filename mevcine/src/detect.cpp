#include "detect.h"

#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

namespace detect {
namespace {

struct Sonde {
    int x, y, r, v, b;
};

Sonde sonde(const QImage &image, int x, int y)
{
    const uchar *p = image.constScanLine(y) + 3 * x;
    return {x, y, p[0], p[1], p[2]};
}

int luminosite(const Sonde &s) { return (s.r + 2 * s.v + s.b) / 4; }

/// Neuf paires de repères réparties sur l'objet pour le balayage, puis une grille plus dense pour le contrôle.
struct Patron {
    QImage image;
    std::array<Sonde, 18> reperes{};
    std::vector<Sonde> grille;
    std::array<double, 3> moyenne{};
    double variance = 0;

    explicit Patron(const QImage &source = {}, int limite_grille = 48) : image(source)
    {
        if (image.isNull())
            return;
        const int w = image.width(), h = image.height();
        const int sx = (w + limite_grille - 1) / limite_grille, sy = (h + limite_grille - 1) / limite_grille;
        for (int y = sy / 2; y < h; y += sy)
            for (int x = sx / 2; x < w; x += sx) {
                const Sonde s = sonde(image, x, y);
                grille.push_back(s);
                moyenne[0] += s.r;
                moyenne[1] += s.v;
                moyenne[2] += s.b;
                variance += s.r * s.r + s.v * s.v + s.b * s.b;
            }
        for (double &m : moyenne)
            m /= grille.size();
        variance = std::max(0.0, variance / grille.size() -
                                     std::inner_product(moyenne.begin(), moyenne.end(), moyenne.begin(), 0.0));
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                const int x0 = i * w / 3, y0 = j * h / 3;
                int meilleur = -1;
                Sonde &repere = reperes[j * 3 + i];
                Sonde &voisin = reperes[9 + j * 3 + i];
                repere = voisin = sonde(image, (2 * i + 1) * w / 6, (2 * j + 1) * h / 6);
                // Choisir un détail contrasté dans chaque case et conserver le centre si elle est unie.
                for (int y = y0; y < (j + 1) * h / 3; y += sy)
                    for (int x = x0; x < (i + 1) * w / 3; x += sx) {
                        const Sonde s = sonde(image, x, y);
                        const Sonde droite = sonde(image, std::min(x + sx, w - 1), y);
                        const Sonde bas = sonde(image, x, std::min(y + sy, h - 1));
                        const auto ecart = [&](const Sonde &v) {
                            return std::abs(v.r - s.r) + std::abs(v.v - s.v) + std::abs(v.b - s.b);
                        };
                        const int dx = ecart(droite), dy = ecart(bas);
                        const int contraste = std::max(dx, dy);
                        if (contraste > 0 && contraste > meilleur) {
                            meilleur = contraste;
                            repere = s;
                            voisin = dx >= dy ? droite : bas;
                        }
                    }
            }
    }
};

struct Candidat {
    QPoint coin;
    int cout = std::numeric_limits<int>::max();
    double distance = 0;
};

int cout_rapide(const uchar *origine, const std::array<qsizetype, 18> &decalages, const Patron &patron, int plafond)
{
    const Sonde &s = patron.reperes[4];
    const uchar *milieu = origine + decalages[4];
    const int lumiere = std::clamp((milieu[0] + 2 * milieu[1] + milieu[2]) / 4 - luminosite(s), -65, 65);
    int cout = 3 * std::abs(lumiere);
    for (size_t i = 0; i < decalages.size() && cout <= plafond; ++i) {
        const uchar *p = origine + decalages[i];
        const Sonde &ref = patron.reperes[i];
        cout += std::abs(p[0] - ref.r - lumiere) + std::abs(p[1] - ref.v - lumiere) + std::abs(p[2] - ref.b - lumiere);
    }
    return cout;
}

double distance(QPoint a, std::optional<QPoint> b)
{
    if (!b)
        return 0;
    const QPointF d = QPointF(a) - *b;
    return QPointF::dotProduct(d, d);
}

/// Un minimum par tuile, y compris lorsque deux objets identiques sont sur la même ligne.
std::vector<Candidat> minima(const Patron &patron, const QImage &image, QRect zone, std::optional<QPoint> prevision)
{
    const int tuile = std::clamp(std::min(patron.image.width(), patron.image.height()) / 3, 5, 24);
    std::array<qsizetype, 18> decalages;
    for (size_t i = 0; i < decalages.size(); ++i)
        decalages[i] = patron.reperes[i].y * image.bytesPerLine() + 3 * patron.reperes[i].x;
    const int nx = (zone.width() + tuile - 1) / tuile, ny = (zone.height() + tuile - 1) / tuile;
    std::vector<Candidat> resultats(nx * ny);
    const auto ligne = [&](int j) {
        for (int i = 0; i < nx; ++i) {
            Candidat &c = resultats[j * nx + i];
            const int gauche = zone.left() + i * tuile, haut = zone.top() + j * tuile;
            for (int y = haut; y <= std::min(haut + tuile - 1, zone.bottom()); ++y) {
                const uchar *p = image.constScanLine(y) + 3 * gauche;
                for (int x = gauche; x <= std::min(gauche + tuile - 1, zone.right()); ++x, p += 3) {
                    const int cout = cout_rapide(p, decalages, patron, c.cout);
                    if (cout <= c.cout) {
                        const double d = distance({x, y}, prevision);
                        if (cout < c.cout || d < c.distance)
                            c = {{x, y}, cout, d};
                    }
                }
            }
        }
    };
    if (qint64(zone.width()) * zone.height() < 40000) {
        for (int j = 0; j < ny; ++j)
            ligne(j);
    } else {
        std::vector<int> lignes(ny);
        std::iota(lignes.begin(), lignes.end(), 0);
        QtConcurrent::blockingMap(lignes, ligne);
    }
    std::sort(resultats.begin(), resultats.end(), [](const Candidat &a, const Candidat &b) {
        return std::tuple(a.cout, a.distance, a.coin.y(), a.coin.x()) <
               std::tuple(b.cout, b.distance, b.coin.y(), b.coin.x());
    });
    std::vector<Candidat> distincts;
    for (const Candidat &c : resultats) {
        if (std::none_of(distincts.begin(), distincts.end(),
                         [&](const Candidat &v) { return distance(c.coin, v.coin) < tuile * tuile; })) {
            distincts.push_back(c);
            if (distincts.size() == 8)
                break;
        }
    }
    return distincts;
}

struct Accord {
    QPoint coin;
    double cout;
    bool a_verifier;
};

/// Une forme unie doit aussi se détacher de ce qui l'entoure (une couleur de fond ne localise rien).
bool bord_visible(const QImage &image, QRect cadre, const std::array<double, 3> &couleur)
{
    const int d = std::max(2, std::min(cadre.width(), cadre.height()) / 4);
    const QRect tour = cadre.adjusted(-d, -d, d, d);
    std::array<int, 4> contrastes{};
    for (int i = 0; i < 5; ++i) {
        const int x = cadre.left() + i * (cadre.width() - 1) / 4;
        const int y = cadre.top() + i * (cadre.height() - 1) / 4;
        const std::array<QPoint, 4> bords = {QPoint(x, tour.top()), QPoint(x, tour.bottom()), QPoint(tour.left(), y),
                                             QPoint(tour.right(), y)};
        for (size_t k = 0; k < bords.size(); ++k) {
            if (!image.rect().contains(bords[k]))
                continue;
            const Sonde s = sonde(image, bords[k].x(), bords[k].y());
            if (std::max({std::abs(s.r - couleur[0]), std::abs(s.v - couleur[1]), std::abs(s.b - couleur[2])}) > 20)
                ++contrastes[k];
        }
    }
    return std::count_if(contrastes.begin(), contrastes.end(), [](int n) { return n >= 2; }) >= 2;
}

/// Corrélation centrée par canal et écart photométrique, évalués indépendamment de la prédiction.
std::optional<Accord> verifie(const Patron &patron, const QImage &image, QPoint coin)
{
    std::array<double, 3> somme{};
    double carres = 0, produit = 0, ecart = 0;
    for (const Sonde &a : patron.grille) {
        const Sonde b = sonde(image, coin.x() + a.x, coin.y() + a.y);
        somme[0] += b.r;
        somme[1] += b.v;
        somme[2] += b.b;
        carres += b.r * b.r + b.v * b.v + b.b * b.b;
        produit += a.r * b.r + a.v * b.v + a.b * b.b;
        const int dr = a.r - b.r, dv = a.v - b.v, db = a.b - b.b;
        ecart += dr * dr + dv * dv + db * db;
    }
    const double n = patron.grille.size();
    const double erreur = std::sqrt(ecart / (3 * n));
    for (double &s : somme)
        s /= n;
    if (patron.variance < 27) {
        if (erreur > 26 || !bord_visible(image, QRect(coin, patron.image.size()), somme))
            return {};
        return Accord{coin, erreur, erreur > 10};
    }
    const double variance = carres / n - std::inner_product(somme.begin(), somme.end(), somme.begin(), 0.0);
    if (variance < 3)
        return {};
    const double covariance = produit / n - std::inner_product(somme.begin(), somme.end(), patron.moyenne.begin(), 0.0);
    const double correlation = std::clamp(covariance / std::sqrt(patron.variance * variance), -1.0, 1.0);
    if (correlation < 0.59 || erreur > 95 || (erreur > 62 && correlation < 0.89))
        return {};
    return Accord{coin, 60 * (1 - correlation) + 0.16 * erreur, correlation < 0.88 || erreur > 32};
}

QRect positions(const QImage &image, const Patron &patron)
{
    return {0, 0, image.width() - patron.image.width() + 1, image.height() - patron.image.height() + 1};
}

QRect voisinage(QPoint centre, int rayon) { return QRect(centre - QPoint(rayon, rayon), centre + QPoint(rayon, rayon)); }

/// Sur un aplat, des contours de plusieurs côtés distinguent l'intérieur du mobile du fond à côté de lui.
bool detail_entoure(const QImage &motif, QPoint centre)
{
    const Sonde milieu = sonde(motif, centre.x(), centre.y());
    int directions = 0;
    for (const QPoint pas : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
        for (QPoint p = centre + pas; motif.rect().contains(p); p += pas) {
            const Sonde s = sonde(motif, p.x(), p.y());
            if (std::max({std::abs(s.r - milieu.r), std::abs(s.v - milieu.v), std::abs(s.b - milieu.b)}) >= 24) {
                ++directions;
                break;
            }
        }
    }
    return directions >= 3;
}

Observation explore(const Patron &fin, const QImage &image, const Patron &petit, const QImage &reduite, int f, QRect zone,
                    std::optional<QPoint> prevision, bool vitesse_connue)
{
    if (zone.isEmpty())
        return {};
    const QRect petite = QRect(QPoint(zone.left() / f, zone.top() / f), QPoint(zone.right() / f, zone.bottom() / f))
                             .intersected(positions(reduite, petit));
    if (petite.isEmpty())
        return {};
    std::vector<Accord> accords;
    for (const Candidat &c : minima(petit, reduite, petite, prevision ? std::optional(*prevision / f) : std::nullopt)) {
        const QRect autour = voisinage(c.coin * f, f == 1 ? 1 : 2 * f).intersected(zone);
        std::optional<Accord> meilleur;
        for (int y = autour.top(); y <= autour.bottom(); ++y)
            for (int x = autour.left(); x <= autour.right(); ++x) {
                const auto a = verifie(fin, image, {x, y});
                if (a &&
                    (!meilleur || a->cout < meilleur->cout ||
                     (a->cout == meilleur->cout && distance(a->coin, prevision) < distance(meilleur->coin, prevision))))
                    meilleur = a;
            }
        if (!meilleur)
            continue;
        // Plusieurs positions peuvent avoir les mêmes couleurs aux repères. Recentrer chaque bassin
        // sur le minimum de la grille dense, jusqu’au pixel en pleine résolution.
        for (int pas = std::clamp(std::min(fin.image.width(), fin.image.height()) / 8, 2, 8);;
             pas = std::max(1, pas / 2)) {
            for (int tour = 0; tour < 4; ++tour) {
                const QPoint origine = meilleur->coin;
                for (int dy : {-pas, 0, pas})
                    for (int dx : {-pas, 0, pas}) {
                        const QPoint voisin = origine + QPoint(dx, dy);
                        if ((!dx && !dy) || !zone.contains(voisin))
                            continue;
                        const auto a = verifie(fin, image, voisin);
                        if (a && a->cout < meilleur->cout)
                            meilleur = a;
                    }
                if (origine == meilleur->coin)
                    break;
            }
            if (pas == 1)
                break;
        }
        accords.push_back(*meilleur);
    }
    if (accords.empty())
        return {};
    const int cote = std::min(fin.image.width(), fin.image.height());
    const auto rang = [&](const Accord &a) {
        return a.cout + std::min(2.0, std::sqrt(distance(a.coin, prevision)) / std::max(3, cote / 2));
    };
    const auto &meilleur = *std::min_element(accords.begin(), accords.end(),
                                             [&](const Accord &a, const Accord &b) { return rang(a) < rang(b); });
    const int separation = std::max(3, cote / 3);
    const bool ambigu = std::any_of(accords.begin(), accords.end(), [&](const Accord &a) {
        if (distance(a.coin, meilleur.coin) < separation * separation || a.cout > meilleur.cout + 1.4)
            return false;
        // Deux apparences indiscernables : seule une trajectoire déjà établie peut les départager.
        return !vitesse_connue || std::sqrt(distance(a.coin, prevision)) <=
                                      std::sqrt(distance(meilleur.coin, prevision)) + std::max(4, cote / 2);
    });
    return {meilleur.coin, meilleur.a_verifier || ambigu};
}

} // namespace

struct Motif {
    std::array<Patron, 3> niveaux;
    explicit Motif(const QImage &image)
    {
        niveaux[0] = Patron(image.convertToFormat(QImage::Format_RGB888));
        for (int f : {2, 4})
            if (std::min(image.width(), image.height()) / f >= 12)
                niveaux[f / 2] = Patron(reduit(image, f));
    }
};

Image::Image(const QImage &source) { m_niveaux[0] = source.convertToFormat(QImage::Format_RGB888); }

const QImage &Image::niveau(int f)
{
    QImage &image = m_niveaux[f / 2];
    if (f > 1 && image.isNull())
        image = reduit(m_niveaux[0], f);
    return image;
}

Piste::Piste(const QImage &motif, std::optional<QPoint> coin, double date)
    : m_motif(std::make_shared<Motif>(motif)), m_dernier(coin), m_date(date)
{
}

QSize Piste::taille() const { return m_motif->niveaux[0].image.size(); }

Observation Piste::recherche(Image &source, double date) const
{
    const QImage &image = source.niveau(1);
    const Patron &motif = m_motif->niveaux[0];
    const QRect limites = positions(image, motif);
    if (image.isNull() || motif.image.isNull() || limites.isEmpty())
        return {};
    std::optional<QPoint> prevision = m_dernier;
    if (prevision && m_intervalle > 0 && date > m_date)
        *prevision += (m_vitesse * std::min(date - m_date, 4 * m_intervalle)).toPoint();
    Observation locale;
    if (prevision) {
        const int base = std::clamp(std::min(taille().width(), taille().height()) / 2, 16, 60);
        const QRect zone = voisinage(*prevision, base * (1 + std::min(m_manques, 2))).intersected(limites);
        locale = explore(motif, image, motif, image, 1, zone, prevision, m_intervalle > 0 && m_manques < 3);
        if (zone == limites)
            return locale;
        if (locale.coin && !locale.a_verifier && zone.contains(voisinage(*locale.coin, 2).intersected(limites)))
            return locale;
    }
    const int f = facteur(image, motif.image);
    const Observation globale = explore(motif, image, m_motif->niveaux[f / 2], source.niveau(f), f, limites, prevision,
                                        m_intervalle > 0 && m_manques < 3);
    return globale.coin ? globale : locale;
}

void Piste::enregistre(Observation o, double date)
{
    if (!o.coin || o.a_verifier) {
        m_manques = std::min(m_manques + 1, 3);
        return;
    }
    if (m_dernier && date > m_date) {
        m_intervalle = date - m_date;
        m_vitesse = (QPointF(*o.coin) - *m_dernier) / m_intervalle;
    }
    m_dernier = o.coin;
    m_date = date;
    m_manques = 0;
}

std::optional<QPoint> localise_motif(const QImage &motif, const QImage &image, std::optional<QPoint> coin, bool *douteux)
{
    Image trame(image);
    const Observation o = Piste(motif, coin, 0).recherche(trame, 1);
    if (douteux)
        *douteux = o.a_verifier;
    return o.coin;
}

struct ExempleDetail {
    QSize dimensions;
    QPointF decalage; // du coin du motif jusqu'au point exact de l'élève
    Patron patron;
    QImage contexte;
    QPoint coin_source; // dans le contexte, pour le contrôle en sens inverse
    int rayon;
};

namespace {

bool dans_image(const QImage &image, QPointF p)
{
    return p.x() >= 0 && p.y() >= 0 && p.x() < image.width() && p.y() < image.height();
}

// Ajustement local par les gradients des deux images. Chaque canal peut changer de luminosité.
QPointF decalage_fin(const Patron &patron, const QImage &image, QPoint coin)
{
    const QImage &motif = patron.image;
    double xx = 0, xy = 0, yy = 0, xr = 0, yr = 0;
    std::array<double, 3> somme_x{}, somme_y{}, somme_r{};
    int n = 0;
    for (const Sonde &s : patron.grille) {
        if (s.x < 1 || s.y < 1 || s.x + 1 >= motif.width() || s.y + 1 >= motif.height())
            continue;
        const uchar *a = motif.constScanLine(s.y) + 3 * s.x;
        const uchar *b = image.constScanLine(coin.y() + s.y) + 3 * (coin.x() + s.x);
        const auto pa = motif.bytesPerLine(), pb = image.bytesPerLine();
        for (int c = 0; c < 3; ++c) {
            const double gx = (a[c + 3] - a[c - 3] + b[c + 3] - b[c - 3]) * 0.25;
            const double gy = (a[c + pa] - a[c - pa] + b[c + pb] - b[c - pb]) * 0.25;
            const double r = a[c] - b[c];
            xx += gx * gx;
            xy += gx * gy;
            yy += gy * gy;
            xr += gx * r;
            yr += gy * r;
            somme_x[c] += gx;
            somme_y[c] += gy;
            somme_r[c] += r;
        }
        ++n;
    }
    if (!n)
        return {};
    for (int c = 0; c < 3; ++c) {
        xx -= somme_x[c] * somme_x[c] / n;
        xy -= somme_x[c] * somme_y[c] / n;
        yy -= somme_y[c] * somme_y[c] / n;
        xr -= somme_x[c] * somme_r[c] / n;
        yr -= somme_y[c] * somme_r[c] / n;
    }
    const double det = xx * yy - xy * xy;
    // Un bord rectiligne ne renseigne pas le déplacement dans les deux directions.
    if (det <= 1e-5 * (xx + yy) * (xx + yy))
        return {};
    const QPointF pas((yy * xr - xy * yr) / det, (xx * yr - xy * xr) / det);
    return std::abs(pas.x()) <= 0.75 && std::abs(pas.y()) <= 0.75 ? pas : QPointF();
}

std::optional<QPointF> recherche_exemple(const ExempleDetail &exemple, const QImage &image, QPointF derniere)
{
    if (image.size() != exemple.dimensions)
        return {};
    const Patron &patron = exemple.patron;
    const QPoint attendu = (derniere - exemple.decalage).toPoint();
    const int r = exemple.rayon;
    const QRect voisin = QRect(attendu, patron.image.size()).adjusted(-r, -r, r, r).intersected(image.rect());
    const QImage extrait = image.copy(voisin).convertToFormat(QImage::Format_RGB888);
    const auto accord = explore(patron, extrait, patron, extrait, 1, positions(extrait, patron), attendu - voisin.topLeft(), false);
    if (!accord.coin || accord.a_verifier)
        return {};

    const Patron retour(extrait.copy(QRect(*accord.coin, patron.image.size())),
                        std::max(patron.image.width(), patron.image.height()));
    const QImage &avant = exemple.contexte;
    const auto inverse = explore(retour, avant, retour, avant, 1, positions(avant, retour), exemple.coin_source, false);
    if (!inverse.coin || inverse.a_verifier || distance(*inverse.coin, exemple.coin_source) > 4)
        return {};
    return QPointF(voisin.topLeft() + *accord.coin) + exemple.decalage + decalage_fin(patron, extrait, *accord.coin);
}

} // namespace

ReferenceDetail prepare_detail(const QImage &source, QPointF point)
{
    if (!dans_image(source, point))
        return {};
    const QPoint pixel(static_cast<int>(point.x()), static_cast<int>(point.y()));
    // Un petit motif préserve le détail choisi. On l'élargit sur une zone unie, par exemple au centre d'une bille.
    for (int rayon : {8, 16, 28}) {
        const QRect cadre = voisinage(pixel, rayon).intersected(source.rect());
        // Tous les pixels du détail comptent, y compris le dernier pixel du contour d'un mobile uni.
        Patron patron(source.copy(cadre).convertToFormat(QImage::Format_RGB888), 2 * rayon + 1);
        if (patron.variance < 100 || (rayon != 8 && !detail_entoure(patron.image, pixel - cadre.topLeft())))
            continue;
        const int r = std::clamp(std::min(source.width(), source.height()) / 10, 40, 96);
        const QRect voisin = cadre.adjusted(-r, -r, r, r).intersected(source.rect());
        return std::make_shared<ExempleDetail>(source.size(), point - cadre.topLeft(), std::move(patron),
            source.copy(voisin).convertToFormat(QImage::Format_RGB888), cadre.topLeft() - voisin.topLeft(), r);
    }
    return {};
}

std::optional<QPointF> retrouve_detail(std::span<const ReferenceDetail> exemples, const QImage &image, QPointF derniere)
{
    if (!dans_image(image, derniere))
        return {};
    std::vector<QPointF> accords;
    for (const auto &exemple : exemples) {
        if (!exemple)
            continue;
        const auto p = recherche_exemple(*exemple, image, derniere);
        if (!p || !dans_image(image, *p))
            continue;
        // Toutes les propositions reconnues doivent désigner le même détail à trois pixels près.
        if (std::any_of(accords.begin(), accords.end(), [&](QPointF q) {
                return QPointF::dotProduct(q - *p, q - *p) > 9.0;
            }))
            return {};
        accords.push_back(*p);
    }
    // Le dernier pointage explicite a priorité quand les exemples concordent.
    return accords.empty() ? std::nullopt : std::optional(accords.back());
}

std::optional<QPointF> retrouve_detail(const QImage &precedente, const QImage &suivante, QPointF point)
{
    const std::array exemples{prepare_detail(precedente, point)};
    return retrouve_detail(exemples, suivante, point);
}

QImage reduit(const QImage &image, int f)
{
    const QImage source = image.convertToFormat(QImage::Format_RGB888);
    if (f <= 1 || source.isNull())
        return source;
    const QSize taille(source.width() / f, source.height() / f);
    if (taille.isEmpty())
        return {};
    QImage sortie(taille, QImage::Format_RGB888);
    for (int y = 0; y < taille.height(); ++y) {
        uchar *dest = sortie.scanLine(y);
        for (int x = 0; x < taille.width(); ++x, dest += 3) {
            std::array<qint64, 3> somme{};
            for (int dy = 0; dy < f; ++dy) {
                const uchar *p = source.constScanLine(y * f + dy) + 3 * x * f;
                for (int dx = 0; dx < f; ++dx, p += 3)
                    for (int c = 0; c < 3; ++c)
                        somme[c] += p[c];
            }
            for (int c = 0; c < 3; ++c)
                dest[c] = static_cast<uchar>(somme[c] / (qint64(f) * f));
        }
    }
    return sortie;
}

int facteur(const QImage &image, const QImage &motif)
{
    int f = 1;
    while (f < 4 && std::max(image.width(), image.height()) / (2 * f) >= 640 &&
           std::min(motif.width(), motif.height()) / (2 * f) >= 12)
        f *= 2;
    return f;
}

std::optional<QImage> decoupe(const QImage &image, int x, int y, int w, int h)
{
    const QRect cadre = QRect(x, y, w, h).intersected(image.rect());
    if (cadre.isEmpty())
        return {};
    return image.copy(cadre).convertToFormat(QImage::Format_RGB888);
}

} // namespace detect
