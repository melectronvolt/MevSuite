#include "view.h"
#include "fichier_seance.h"
#include "jpeg.h"

#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QFontMetricsF>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace {

/// Côté maximal de l'image d'affichage. Au-delà, l'image est réduite pour l'écran (les mesures et le
/// profil utilisent toujours les pixels d'origine) : une photo de 100 Mpx occuperait sinon plusieurs
/// centaines de Mo de plus.
constexpr int MAX_DISPLAY_SIDE = 8192;
/// La pyramide s’arrête lorsque son côté le plus long ne peut plus être réduit à 256 pixels.
constexpr int MIN_LEVEL_SIDE = 256;
/// Mémoire maximale accordée au décodage d'une image, en Mo (256 par défaut dans Qt : trop peu pour
/// une photo de 100 Mpx).
constexpr int ALLOCATION_LIMIT_MB = 2048;

/// Zoom de l'image (pixels écran par pixel image).
constexpr double ZOOM_MIN = 0.02;
constexpr double ZOOM_MAX = 40.0;

ResultatChargement non_pris_en_charge()
{
    ResultatChargement result;
    result.typeErreur = ResultatChargement::Erreur::FormatInconnu;
    return result;
}

/// Construit seulement les résolutions plus petites que la photo. Garder aussi une copie RVB32
/// à pleine taille coûterait quatre octets supplémentaires par pixel (96 Mo pour 24 mégapixels).
void prepare_apercus(ImageChargee &photo, QImage niveau)
{
    while (std::max(niveau.width(), niveau.height()) >= 2 * MIN_LEVEL_SIDE) {
        QSize cible(std::max(1, niveau.width() / 2), std::max(1, niveau.height() / 2));
        if (std::max(niveau.width(), niveau.height()) > MAX_DISPLAY_SIDE)
            cible = niveau.size().scaled(MAX_DISPLAY_SIDE, MAX_DISPLAY_SIDE, Qt::KeepAspectRatio).expandedTo({1, 1});
        niveau = niveau.scaled(cible, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_RGB32);
        if (niveau.isNull())
            break; // La photo d'origine reste disponible si la mémoire manque pour un aperçu.
        photo.apercus.push_back(niveau);
    }
}

} // namespace

QPointF ImageChargee::borne(QPointF p) const
{
    return QPointF(std::clamp(p.x(), 0.0, double(rgb.width())), std::clamp(p.y(), 0.0, double(rgb.height())));
}

QStringList formats_image()
{
    QStringList formats;
    for (const auto &format : QImageReader::supportedImageFormats())
        formats << QString::fromLatin1(format).toLower();
    formats.removeDuplicates();
    formats.sort();
    return formats;
}

ResultatChargement charge_image(const QString &chemin)
{
    const QFileInfo source(chemin);
    ResultatChargement resultat;
    if (source.exists() && !source.isFile()) {
        // L'ouverture d'un tube nommé pourrait attendre indéfiniment un émetteur.
        resultat.typeErreur = ResultatChargement::Erreur::FichierSpecial;
        return resultat;
    }
    QFile fichier(chemin);
    if (!fichier.open(QIODevice::ReadOnly)) {
        resultat.error = fichier.errorString();
        return resultat;
    }
    const QByteArray debut = fichier.peek(sizeof(SIGNATURE_SEANCE) - 1);
    // Ce réglage appartient à Qt pour tout le processus, y compris lorsque plusieurs photos se chargent.
    static std::once_flag premiereLecture;
    std::call_once(premiereLecture, &QImageReader::setAllocationLimit, ALLOCATION_LIMIT_MB);
    QImageReader lecteur(&fichier);
    if (debut.startsWith("\xff\xd8")) {
        // Certains décodeurs complètent en gris les lignes manquantes d'un transfert interrompu.
        if (!jpeg_complet(fichier)) {
            if (fichier.error() == QFileDevice::NoError)
                resultat.typeErreur = ResultatChargement::Erreur::JpegIncomplet;
            else
                resultat.error = fichier.errorString();
            return resultat;
        }
        if (!fichier.seek(0)) {
            resultat.error = fichier.errorString();
            return resultat;
        }
        lecteur.setFormat("JPEG");
    } else if (debut.startsWith("\x89PNG\r\n\x1a\n")) {
        lecteur.setFormat("PNG");
    } else if (debut == SIGNATURE_SEANCE) {
        // Garder la version du fichier effectivement ouvert, même si son nom est repris
        // par une autre fenêtre pendant le décodage.
        resultat.empreinteSeance = empreinte_lecture(fichier);
        if (resultat.empreinteSeance.isEmpty() || !fichier.seek(0)) {
            resultat.error = fichier.errorString();
            return resultat;
        }
        fichier.readLine();
        const QByteArray ligne = fichier.readLine(TAILLE_MAX_ENTETE_SEANCE);
        resultat.seance = QJsonDocument::fromJson(ligne).object();
        if (resultat.seance.isEmpty() || !ligne.endsWith('\n')) {
            resultat.error = QStringLiteral("MeVPix : JSON");
            return resultat;
        }
        lecteur.setFormat("PNG");
    } else {
        // Le type MIME est lu dans le contenu, y compris pour une image renommée.
        // Éviter de charger tous les plugins quand Qt reconnaît déjà ce format.
        const auto type = QMimeDatabase().mimeTypeForData(&fichier);
        const QByteArray format = type.preferredSuffix().toLatin1();
        QImageReader sonde(&fichier, format);
        sonde.setAutoDetectImageFormat(false);
        const bool reconnu = type.name().startsWith(QLatin1String("image/")) && !format.isEmpty() && sonde.canRead();
        if (!fichier.seek(0)) {
            resultat.error = fichier.errorString();
            return resultat;
        }
        if (reconnu)
            lecteur.setFormat(format);
        else
            lecteur.setDecideFormatFromContent(true);
    }
    // Les photos de téléphone sont souvent stockées tournées, avec l'orientation dans l'EXIF.
    lecteur.setAutoTransform(true);
    QImage imageDecodee = lecteur.read();
    if (fichier.error() != QFileDevice::NoError) {
        resultat.error = fichier.errorString();
        return resultat;
    }
    if (imageDecodee.isNull()) {
        if (lecteur.error() == QImageReader::UnsupportedFormatError)
            return non_pris_en_charge();
        resultat.error = lecteur.errorString();
        return resultat;
    }
    if (imageDecodee.width() == 0 || imageDecodee.height() == 0)
        return non_pris_en_charge();

    auto photo = std::make_shared<ImageChargee>();
    photo->name = resultat.seance.value("image").toString(source.fileName());
    const auto format = imageDecodee.pixelFormat();
    photo->bitsSource = std::max({8, int(format.redSize()), int(format.greenSize()), int(format.blueSize()),
                                std::clamp(resultat.seance.value("bitsSource").toInt(), 0, 64)});
    // Les JPEG sont souvent déjà décodés en RVB32, format que Qt réduit rapidement.
    const bool sourceRvb32 = imageDecodee.format() == QImage::Format_RGB32;
    if (sourceRvb32)
        prepare_apercus(*photo, imageDecodee);
    photo->rgb = std::move(imageDecodee).convertToFormat(QImage::Format_RGB888);
    if (photo->rgb.isNull()) {
        resultat.error = QStringLiteral("RGB888");
        return resultat;
    }

    if (!sourceRvb32)
        prepare_apercus(*photo, photo->rgb);
    resultat.image = std::move(photo);
    return resultat;
}

void VueImage::ajuste(QSizeF area, QSizeF imgSize)
{
    const double zoom = std::min(area.width() / imgSize.width(), area.height() / imgSize.height()) * 0.98;
    // Zone d'affichage réduite à rien (fenêtre minuscule, interface très agrandie) : zoom valide quand même.
    m_zoom = std::isfinite(zoom) && zoom > 0.0 ? std::min(zoom, ZOOM_MAX) : 1.0;
    m_zoomMini = std::min(m_zoom, ZOOM_MIN);
    const QSizeF free = area - imgSize * m_zoom;
    m_origineEcran = QPointF(free.width(), free.height()) / 2.0;
    m_vueManuelle = false;
}

void VueImage::dispose(QSizeF area, QSizeF imgSize)
{
    // Réajustement automatique quand la zone change de taille (fenêtre, panneau du profil).
    if (m_ajustementDemande || (!m_vueManuelle && area != m_derniereTaille)) {
        ajuste(area, imgSize);
        m_ajustementDemande = false;
    }
    m_derniereTaille = area;
}

void VueImage::zoome_autour(QPointF pointer, double factor)
{
    const QPointF anchor = vers_image(pointer);
    m_zoom = std::clamp(m_zoom * factor, m_zoomMini, ZOOM_MAX);
    m_origineEcran = pointer - anchor * m_zoom;
    m_vueManuelle = true;
}

void VueImage::deplace_vue(QPointF delta)
{
    m_origineEcran += delta;
    m_vueManuelle = true;
}

void VueImage::dessine_image(QPainter &painter, QSizeF area, const ImageChargee &img) const
{
    // Partie visible de l'image, en pixels de l'image.
    const QRectF visible = QRectF(vers_image(QPointF(0, 0)), vers_image(QPointF(area.width(), area.height())))
                               .intersected(QRectF(QPointF(0, 0), img.taille()));
    if (visible.isEmpty())
        return;
    // Réduction la plus petite qui garde au moins un pixel d'image par pixel écran.
    const double zoom = m_zoom * painter.device()->devicePixelRatioF();
    const QImage *level = &img.rgb;
    for (const QImage &reduction : img.apercus) {
        if (reduction.width() < img.rgb.width() * zoom || reduction.height() < img.rgb.height() * zoom)
            break;
        level = &reduction;
    }
    const double sx = double(level->width()) / img.rgb.width();
    const double sy = double(level->height()) / img.rgb.height();
    const QRectF source(visible.x() * sx, visible.y() * sy, visible.width() * sx, visible.height() * sy);
    const QRectF target(vers_ecran(visible.topLeft()), vers_ecran(visible.bottomRight()));
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 1.0);
    painter.drawImage(target, *level, source);
    painter.restore();
}

void dessine_segment(QPainter &painter, QPointF a, QPointF b, const QColor &color, bool handles, double scale)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0, 0, 0, 180), 4.0 * scale, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(a, b);
    painter.setPen(QPen(color, 2.0 * scale, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(a, b);
    const double radius = (handles ? 5.0 : 3.0) * scale;
    painter.setPen(QPen(Qt::black, 1.5 * scale));
    painter.setBrush(color);
    for (QPointF p : {a, b})
        painter.drawEllipse(p, radius, radius);
    painter.restore();
}

void dessine_bande(QPainter &painter, QPointF a, QPointF b, QPointF halfWidth, QColor color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const QPointF points[] = {a + halfWidth, b + halfWidth, b - halfWidth, a - halfWidth};
    painter.drawConvexPolygon(points, 4);
    painter.restore();
}

void dessine_etiquette(QPainter &painter, QPointF pos, const QString &text, const QColor &color, double scale, bool name)
{
    painter.save();
    // police de l'interface (elle suit l'agrandissement) ; nom d'un curseur en demi-gras
    QFont font = painter.font();
    if (name)
        font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const double margin = 4.0 * scale;
    const QSizeF size(metrics.horizontalAdvance(text) + 2 * margin, metrics.height() + 2 * margin);
    const QRectF rect(QPointF(pos.x() - size.width() / 2, pos.y() - size.height()), size);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 190));
    painter.drawRoundedRect(rect, 4.0 * scale, 4.0 * scale);
    painter.setPen(color);
    painter.drawText(rect, Qt::AlignCenter, text);
    painter.restore();
}

QPointF ancre_etiquette(const VueImage &view, const Segment &seg, double scale)
{
    const QPointF mid = view.vers_ecran(seg.point_en(0.5));
    QPointF n = seg.normale();
    if (n.y() > 0.0)
        n = -n; // toujours au-dessus du segment
    return mid + n * 14.0 * scale;
}
