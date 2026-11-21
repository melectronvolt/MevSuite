#include "profile.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Valeur à partir de laquelle un pixel est considéré comme saturé (écrêté).
constexpr int SATURATION = 254;

/// Contribution d'un pixel RVB888 à la moyenne bilinéaire. Les intensités sont les niveaux
/// numériques du fichier (0 à 255). La luminance affichée est la somme pondérée des trois canaux.
double contribution_pixel(const uchar *pixel, Canal canal, double poids, bool &sature)
{
    if (poids == 0)
        return 0;
    const int rouge = pixel[0], vert = pixel[1], bleu = pixel[2];
    const auto pondere = [&](double niveau, int maximum) {
        if (poids > 1e-9 && maximum >= SATURATION)
            sature = true;
        return poids * niveau;
    };
    switch (canal) {
    case Canal::GrisPondere:
        // Un laser rouge peut écrêter le rouge même si le niveau de gris reste faible.
        return pondere(0.299 * rouge + 0.587 * vert + 0.114 * bleu, std::max({rouge, vert, bleu}));
    case Canal::Rouge: return pondere(rouge, rouge);
    case Canal::Vert: return pondere(vert, vert);
    case Canal::Bleu: return pondere(bleu, bleu);
    }
    return 0;
}

/// Vue en lecture seule des pixels. Deux lignes consécutives peuvent être séparées par du
/// remplissage. On relève leur pas et les dimensions une seule fois pour toute la coupe.
struct GrilleRvb {
    const uchar *pixels;
    qsizetype octetsParLigne;
    QSize taille;

    double interpole(QPointF position, Canal canal, bool &sature) const;
};

/// Chaque pixel (colonne, ligne) a son centre en (colonne + 0,5 ; ligne + 0,5).
/// On interpole entre les quatre centres voisins. Au bord, la valeur du dernier centre se prolonge.
double GrilleRvb::interpole(QPointF position, Canal canal, bool &sature) const
{
    const double x = std::clamp(position.x() - 0.5, 0.0, double(taille.width() - 1));
    const double y = std::clamp(position.y() - 0.5, 0.0, double(taille.height() - 1));
    const int gauche = static_cast<int>(x), haut = static_cast<int>(y);
    const double fractionX = x - gauche, fractionY = y - haut;
    const uchar *ligneHaute = pixels + haut * octetsParLigne;
    const uchar *pixelGauche = ligneHaute + qsizetype(3) * gauche;
    if (fractionX == 0 && fractionY == 0)
        return contribution_pixel(pixelGauche, canal, 1.0, sature);

    const int droite = std::min(gauche + 1, taille.width() - 1);
    const int bas = std::min(haut + 1, taille.height() - 1);
    const uchar *ligneBasse = pixels + bas * octetsParLigne;
    return contribution_pixel(pixelGauche, canal, (1 - fractionX) * (1 - fractionY), sature)
         + contribution_pixel(ligneHaute + qsizetype(3) * droite, canal, fractionX * (1 - fractionY), sature)
         + contribution_pixel(ligneBasse + qsizetype(3) * gauche, canal, (1 - fractionX) * fractionY, sature)
         + contribution_pixel(ligneBasse + qsizetype(3) * droite, canal, fractionX * fractionY, sature);
}

} // namespace

double Profil::longueur_px() const
{
    return pasPx * double(intensites.empty() ? 0 : intensites.size() - 1);
}

double Profil::valeur_en(double t) const
{
    if (intensites.empty())
        return 0.0;
    const size_t last = intensites.size() - 1;
    const double indice = std::clamp(t, 0.0, 1.0) * double(last);
    const auto k = static_cast<size_t>(indice);
    return std::lerp(intensites[k], intensites[std::min(k + 1, last)], indice - double(k));
}

Profil echantillonne_profil(const QImage &image, const Segment &coupe, int largeurBande, Canal canal)
{
    Q_ASSERT(image.format() == QImage::Format_RGB888 && !image.isNull());
    const GrilleRvb grille{image.constBits(), image.bytesPerLine(), image.size()};
    const double longueurPx = coupe.longueur_px();
    // Inclure les deux extrémités. Par exemple, une coupe de 10 px donne 11 échantillons.
    const int intervalles = std::max(static_cast<int>(std::ceil(longueurPx)), 1);
    const int largeur = std::max(largeurBande, 1);
    const QPointF normale = coupe.normale();
    std::vector<QPointF> decalages;
    decalages.reserve(largeur);
    for (int ligne = 0; ligne < largeur; ++ligne)
        decalages.push_back(normale * (ligne - (largeur - 1) / 2.0));

    Profil profil;
    profil.pasPx = longueurPx / intervalles;
    profil.intensites.reserve(intervalles + 1);
    for (int k = 0; k <= intervalles; ++k) {
        const QPointF centre = coupe.point_en(double(k) / intervalles);
        bool sature = false;
        double somme = 0;
        int pointsDansImage = 0;
        for (QPointF decalage : decalages) {
            const QPointF point = centre + decalage;
            if (point.x() < 0 || point.y() < 0 || point.x() > grille.taille.width() || point.y() > grille.taille.height())
                continue;
            somme += grille.interpole(point, canal, sature);
            ++pointsDansImage;
        }
        if (pointsDansImage < largeur)
            ++profil.tronques;
        // Au coin de la photo, tous les points d'une bande paire peuvent tomber dehors.
        const double intensite = pointsDansImage > 0 ? somme / pointsDansImage
                                                    : grille.interpole(centre, canal, sature);
        profil.intensites.push_back(intensite);
        if (sature)
            ++profil.echantillonsSatures;
    }
    return profil;
}

QString en_csv(const Profil &profil, const std::optional<Etalonnage> &etalonnage, QChar decimale)
{
    const QString unite = etalonnage ? symbole_unite(etalonnage->unite) : u"px"_s;
    const double unitesParPixel = etalonnage ? etalonnage->unites_par_pixel() : 1.0;
    const QChar separateur = decimale == u',' ? u';' : u',';
    QString tableau = u"x (%1)%2I\n"_s.arg(unite, separateur);
    for (size_t k = 0; k < profil.intensites.size(); ++k) {
        const double abscisse = double(k) * profil.pasPx * unitesParPixel;
        // Garder la précision des abscisses pour les calculs ultérieurs dans le tableur.
        tableau += avec_separateur_decimal(QString::number(abscisse, 'g', 15), decimale);
        tableau += separateur;
        tableau += texte_decimal(profil.intensites[k], 2, decimale);
        tableau += u'\n';
    }
    return tableau;
}

QString avec_extension_csv(const QString &path)
{
    // « .csv » ajouté au nom tapé sans extension ; si ce fichier existe déjà, le programme demande
    // avant de le remplacer (le dialogue a confirmé le nom tapé, pas celui-ci)
    return QFileInfo(path).suffix().isEmpty() ? path + u".csv"_s : path;
}
