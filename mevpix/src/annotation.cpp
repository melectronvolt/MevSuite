#include "annotation.h"

#include "plot.h"
#include "style.h"

#include <QApplication>
#include <QColorSpace>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

void annote_photo(QPainter &dessin, const Seance &seance, const Textes &tr, const OptionsAnnotation &options)
{
    const QSize taille = seance.image->rgb.size();
    const double style = std::max(0.4, std::min(taille.width() / 750.0, taille.height() / 280.0));
    QFont police = QApplication::font();
    police.setPixelSize(std::max(1, qRound(13 * style)));
    dessin.setFont(police);
    dessin.setClipRect(QRect(QPoint(), taille));
    const auto etiquette = [&](QPointF position, const QString &texte, QColor couleur, bool nom = false) {
        const QFontMetricsF metriques(dessin.font());
        const double demi = std::min(taille.width() / 2.0, metriques.horizontalAdvance(texte) / 2 + 6 * style);
        position.setX(std::clamp(position.x(), demi, taille.width() - demi));
        position.setY(std::clamp(position.y(), std::min(double(taille.height()), metriques.height() + 8 * style),
                                 double(taille.height())));
        dessine_etiquette(dessin, position, texte, couleur, style, nom);
    };
    const auto trace = [&](const Segment &segment, QColor couleur, const QString &texte) {
        dessine_segment(dessin, segment.a, segment.b, couleur, false, style);
        etiquette(segment.point_en(0.5) - QPointF(0, 6 * style), texte, couleur);
    };
    if (options.etalon && seance.segmentEtalon) {
        const QString longueur = texte_longueur(seance.segmentEtalon->longueur_px(), seance.etalonnage, tr.decimal);
        trace(*seance.segmentEtalon, COLOR_CALIB, tr.calibTag(longueur));
    }
    const auto traceMesure = [&](int numero) {
        const Segment &segment = seance.mesures.at(numero - 1);
        const QString longueur = texte_longueur(segment.longueur_px(), seance.etalonnage, tr.decimal);
        trace(segment, COLOR_MEASURE, tr.measureTag(numero, longueur));
    };
    if (options.mesure == 0) {
        for (int numero = 1; numero <= seance.mesures.size(); ++numero)
            traceMesure(numero);
    } else if (options.mesure > 0 && options.mesure <= seance.mesures.size()) {
        traceMesure(options.mesure);
    }
    if (options.coupe && seance.coupe_mesurable()) {
        const Segment &segment = *seance.coupe;
        QColor bande = COLOR_CUT;
        bande.setAlphaF(0.25f);
        if (seance.largeurBande > 1)
            dessine_bande(dessin, segment.a, segment.b, segment.normale() * (seance.largeurBande / 2.0), bande);
        dessine_segment(dessin, segment.a, segment.b, COLOR_CUT, false, style);
        for (int i : {0, 1}) {
            const QPointF point = segment.point_en(seance.fractionsCurseurs[i]);
            const QColor couleur = i == 0 ? COLOR_A : COLOR_B;
            dessin.setPen(QPen(Qt::black, style));
            dessin.setBrush(couleur);
            dessin.drawEllipse(point, 5 * style, 5 * style);
            etiquette(point + QPointF((i == 0 ? -9 : 9) * style, -6 * style), i == 0 ? u"A"_s : u"B"_s, couleur, true);
        }
    }
}

} // namespace

QImage image_annotee(Seance &seance, const Textes &tr, const OptionsAnnotation &options, QSize limite)
{
    if (!seance.image || seance.image->rgb.isNull())
        return {};
    const QImage &source = seance.image->rgb;
    const bool graphique = options.graphique && seance.coupe_mesurable();
    const int largeur = graphique ? std::max(800, source.width()) : source.width();
    const double echelleGraphe = std::clamp(largeur / 800.0, 1.0, 3.0);
    const int hauteurGraphe = graphique ? qRound(260 * echelleGraphe) : 0;
    const QSize composition(largeur, source.height() + hauteurGraphe);
    const QSize taille = limite.isEmpty() ? composition : composition.scaled(limite, Qt::KeepAspectRatio).expandedTo({1, 1});
    QImage image(taille, QImage::Format_RGB32);
    if (image.isNull())
        return {};
    // Le PNG et le presse-papiers doivent donner le même sens aux couleurs que la photo.
    image.setColorSpace(source.colorSpace());
    const QPalette palette = QApplication::palette();
    image.fill(palette.color(QPalette::Window));
    QPainter dessin(&image);
    dessin.scale(double(taille.width()) / largeur, double(taille.height()) / composition.height());
    dessin.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    dessin.save();
    dessin.translate((largeur - source.width()) / 2, 0);
    // Une photo nommée « mire@2x.png » porte un facteur d'affichage de 2 dans Qt.
    // La photo et les tracés occupent ici le même rectangle, mesuré en pixels d'origine.
    dessin.drawImage(source.rect(), source, source.rect());
    annote_photo(dessin, seance, tr, options);
    dessin.restore();
    if (graphique) {
        GraphiqueProfil graphe(seance);
        graphe.definit_traductions(&tr);
        graphe.resize(qRound(largeur / echelleGraphe), 230);
        dessin.translate(0, source.height());
        dessin.scale(echelleGraphe, echelleGraphe);
        graphe.render(&dessin);
        dessin.setPen(palette.color(QPalette::WindowText));
        const double delta = std::abs(seance.fractionsCurseurs[1] - seance.fractionsCurseurs[0]) * seance.coupe->longueur_px();
        const QString legende = u"%1 · %2 px     Δx = %3"_s.arg(tr.nom_canal(seance.canal)).arg(seance.largeurBande)
                                    .arg(texte_longueur(delta, seance.etalonnage, tr.decimal));
        dessin.drawText(QRect(0, graphe.height(), graphe.width(), 30), Qt::AlignCenter, legende);
    }
    return image;
}
