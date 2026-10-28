#include "video.h"

#include "chemins.h"
#include "i18n.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// taille maximale du cache d'images, en octets
constexpr qsizetype CACHE_MAX = qsizetype(512) * 1024 * 1024;
/// attente maximale d'un programme de ffmpeg (ms) : au-delà, la vidéo est jugée illisible plutôt
/// que de figer la fenêtre sans fin (fichier abîmé, support très lent)
constexpr int ATTENTE_MAX = 120000;
/// attente maximale d'une image décodée (ms)
constexpr int ATTENTE_IMAGE = 60000;
/// au-delà de ce nombre d'images d'avance, on relance ffmpeg plutôt que de décoder toutes les images
/// intermédiaires
constexpr int SAUT_MAX = 25;
/// écart relatif à l'intervalle habituel entre deux trames au-delà duquel on considère que la
/// cadence est variable
constexpr double TOLERANCE_CADENCE = 0.25;
/// durée d'une tranche d'attente pendant une ouverture annulable (ms)
constexpr int TRANCHE = 50;

/// pendant `VideoReader::ouvre`, dans le fil qui ouvre la vidéo : l'indicateur qui l'interrompt
thread_local const std::atomic_bool *t_annule = nullptr;

/// Attend la fin du programme (`fin`) ou des données à lire, au plus `ms` millisecondes ; faux si le
/// délai est dépassé. Pendant une ouverture annulable, l'attente se fait par tranches courtes : une
/// annulation l'arrête aussitôt (faux aussi).
bool attend(QProcess &p, bool fin, int ms)
{
    const auto attente = [&](int t) { return fin ? p.waitForFinished(t) : p.waitForReadyRead(t); };
    if (!t_annule)
        return attente(ms);
    QElapsedTimer chrono;
    chrono.start();
    while (!t_annule->load()) {
        const qint64 reste = ms < 0 ? TRANCHE : ms - chrono.elapsed();
        if (reste <= 0)
            return false;
        if (attente(static_cast<int>(std::min<qint64>(TRANCHE, reste))))
            return true;
        if (p.state() == QProcess::NotRunning)
            return false;
    }
    return false;
}

/// message d'erreur quand un programme de ffmpeg ne peut pas être lancé
QString erreur_lancement(const QString &programme, const QProcess &p)
{
    const QString chemin = chemins::outil(programme);
    const bool introuvable = !QFileInfo(chemin).isFile() && QStandardPaths::findExecutable(chemin).isEmpty();
    if (p.error() == QProcess::FailedToStart && introuvable)
        return trad("le programme {} est introuvable : ffmpeg doit être installé pour lire les vidéos", programme);
    return trad("impossible de lancer {} : {}", programme, p.errorString());
}

/// Lance un programme de ffmpeg, sans entrée standard. Sous Windows, Qt le lance sans fenêtre de
/// console (l'application n'en a pas).
std::unique_ptr<QProcess> lance(const QString &programme, const QStringList &args)
{
    auto p = std::make_unique<QProcess>();
    p->setStandardInputFile(QProcess::nullDevice());
    p->start(chemins::outil(programme), args);
    return p;
}

/// lit exactement `n` octets ; faux si le flux se termine avant
bool lit_exact(const Lecture &lecture, char *dest, qint64 n)
{
    while (n > 0) {
        const qint64 lu = lecture(dest, n);
        if (lu <= 0)
            return false;
        dest += lu;
        n -= lu;
    }
    return true;
}

/// lit un entier décimal de l'en-tête d'un fichier PPM
std::expected<unsigned, QString> lit_entier(const Lecture &lecture)
{
    char octet = 0;
    std::optional<unsigned> valeur;
    while (true) {
        if (lecture(&octet, 1) <= 0) {
            if (valeur)
                return *valeur;
            return std::unexpected(trad("fin du flux vidéo"));
        }
        if (octet >= '0' && octet <= '9') {
            valeur = valeur.value_or(0) * 10 + static_cast<unsigned>(octet - '0');
        } else if (octet == '#') {
            // commentaire jusqu'à la fin de la ligne
            while (lecture(&octet, 1) == 1 && octet != '\n') {
            }
            if (valeur)
                return *valeur;
        } else if (octet == ' ' || octet == '\t' || octet == '\n' || octet == '\r' || octet == '\f'
                   || octet == '\v') {
            if (valeur)
                return *valeur;
        } else {
            return std::unexpected(trad("en-tête PPM invalide"));
        }
    }
}

/// un entier écrit en texte par ffprobe (« 16 »)
std::optional<long long> entier(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    bool ok = false;
    const long long n = v.toString().toLongLong(&ok);
    return ok && n >= 0 ? std::optional(n) : std::nullopt;
}

std::optional<double> flottant(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    bool ok = false;
    const double x = v.toString().toDouble(&ok);
    return ok ? std::optional(x) : std::nullopt;
}

QStringList ffprobe()
{
    return {u"-v"_s, u"error"_s, u"-select_streams"_s, u"V:0"_s};
}

QString ffprobe_ne_repond_pas()
{
    return trad("ffprobe ne répond pas : la vidéo est peut-être abîmée, ou sur un support trop lent");
}

std::expected<QJsonObject, QString> lance_ffprobe(const QString &chemin, bool compte_paquets)
{
    QStringList args = ffprobe();
    if (compte_paquets)
        args << u"-count_packets"_s;
    args << u"-show_entries"_s
         << u"stream=codec_name,width,height,r_frame_rate,avg_frame_rate,time_base,nb_frames,nb_read_packets,"
            "duration,color_transfer:format=duration:format_tags"_s
         << u"-of"_s << u"json"_s << chemin;
    auto p = lance(u"ffprobe"_s, args);
    if (!p->waitForStarted(-1))
        return std::unexpected(erreur_lancement(u"ffprobe"_s, *p));
    if (!attend(*p, true, ATTENTE_MAX)) {
        p->kill();
        p->waitForFinished(5000);
        return std::unexpected(ffprobe_ne_repond_pas());
    }
    if (p->exitStatus() != QProcess::NormalExit || p->exitCode() != 0)
        return std::unexpected(QString::fromUtf8(p->readAllStandardError()).trimmed());
    QJsonParseError erreur;
    const QJsonDocument doc = QJsonDocument::fromJson(p->readAllStandardOutput(), &erreur);
    if (!doc.isObject())
        return std::unexpected(erreur.errorString());
    return doc.object();
}

/// dates de présentation des trames d'une vidéo, en seconde, dans l'ordre, d'après les paquets du
/// flux (sans décoder) ; vide si elles sont inconnues. Trop long : une erreur (supposer la cadence
/// régulière fausserait en silence les dates d'une vidéo à cadence variable).
std::expected<std::vector<double>, QString> dates_des_trames(const QString &chemin)
{
    auto p = lance(u"ffprobe"_s, ffprobe() << u"-show_entries"_s << u"packet=pts_time,size,flags"_s << u"-of"_s
                                            << u"csv=p=0"_s << chemin);
    if (!p->waitForStarted(-1))
        return std::vector<double>();
    if (!attend(*p, true, ATTENTE_MAX)) {
        p->kill();
        p->waitForFinished(5000);
        return std::unexpected(ffprobe_ne_repond_pas());
    }
    if (p->exitStatus() != QProcess::NormalExit || p->exitCode() != 0)
        return std::vector<double>();
    std::vector<double> dates;
    const QString sortie = QString::fromUtf8(p->readAllStandardOutput());
    for (QStringView ligne : QStringView(sortie).split(u'\n')) {
        // les champs viennent dans l'ordre de ffprobe : date, taille, drapeaux
        const auto champs = ligne.trimmed().split(u',');
        if (champs.size() != 3)
            continue;
        const QStringView pts = champs[0].trimmed(), taille = champs[1].trimmed(), drapeaux = champs[2].trimmed();
        // paquets à ignorer (listes d'édition des fichiers MP4...), paquets vides (images perdues des
        // AVI), qui ne donnent pas de trame
        if (drapeaux.contains(u'D') || taille == u"0")
            continue;
        bool ok = false;
        const double t = pts.toDouble(&ok);
        // une seule date inconnue rend les autres inutilisables
        if (!ok || !std::isfinite(t))
            return std::vector<double>();
        dates.push_back(t);
    }
    std::sort(dates.begin(), dates.end());
    // deux trames à la même date : une seule est affichée
    dates.erase(std::unique(dates.begin(), dates.end(), [](double a, double b) { return std::abs(b - a) < 1e-6; }),
                dates.end());
    return dates;
}

/// récupère les métadonnées d'une vidéo à l'aide de ffprobe
std::expected<VideoInfo, QString> sonde(const QString &chemin)
{
    const auto json = lance_ffprobe(chemin, false);
    if (!json)
        return std::unexpected(json.error().isEmpty() ? trad("ce fichier n'est pas une vidéo") : json.error());
    const QJsonArray streams = (*json)[u"streams"_s].toArray();
    if (streams.isEmpty() || !streams[0].isObject())
        return std::unexpected(trad("ce fichier ne contient pas de vidéo"));
    const QJsonObject flux = streams[0].toObject();
    const QJsonObject format = (*json)[u"format"_s].toObject();
    VideoInfo info;
    info.fps = rationnel(flux[u"avg_frame_rate"_s].toString())
                   .or_else([&] { return rationnel(flux[u"r_frame_rate"_s].toString()); })
                   .value_or(25.0);
    std::optional<double> duree = flottant(flux[u"duration"_s]);
    if (!duree)
        duree = flottant(format[u"duration"_s]);
    auto dates_lues = dates_des_trames(chemin);
    if (!dates_lues)
        return std::unexpected(dates_lues.error());
    info.dates = std::move(*dates_lues);
    const auto intervalle = cadence_variable(info.dates);
    info.cadence_variable = intervalle.has_value();
    // la cadence habituelle, et non la moyenne, faussée par les images perdues : c'est celle de la
    // caméra
    if (intervalle)
        info.fps = 1.0 / *intervalle;
    // cadence presque régulière (téléphones : 40 puis 44 ms…) : les dates réelles des trames, plus
    // justes qu'une cadence moyenne
    info.dates_reelles = info.cadence_variable
        || cadence_presque_reguliere(info.dates, rationnel(flux[u"time_base"_s].toString()).value_or(0.0));
    const auto &dates = info.dates;
    long long nb_images = 0;
    if (info.dates_reelles) {
        nb_images = static_cast<long long>(dates.size());
    } else if (dates.size() > 1) {
        nb_images = std::llround((dates.back() - dates.front()) * info.fps) + 1;
    } else if (const auto n = entier(flux[u"nb_frames"_s]); n && *n > 0) {
        nb_images = *n;
    } else {
        // certains conteneurs (ogv, webm...) n'indiquent pas le nombre d'images : on compte les
        // paquets, sans décoder
        std::optional<long long> compte;
        if (const auto j = lance_ffprobe(chemin, true)) {
            const QJsonArray s = (*j)[u"streams"_s].toArray();
            if (!s.isEmpty())
                compte = entier(s[0].toObject()[u"nb_read_packets"_s]);
        }
        if (compte && *compte > 0)
            nb_images = *compte;
        else if (duree)
            nb_images = std::llround(*duree * info.fps);
    }
    if (nb_images <= 0)
        return std::unexpected(trad("impossible de déterminer le nombre d'images"));
    info.nb_images = static_cast<int>(nb_images);
    // les téléphones Android notent la cadence de prise de vue des vidéos au ralenti ou en accéléré
    const QJsonValue capture = format[u"tags"_s].toObject()[u"com.android.capture.fps"_s];
    if (capture.isString()) {
        bool ok = false;
        const double c = capture.toString().trimmed().toDouble(&ok);
        if (ok && std::isfinite(c) && c > 0.0 && std::abs(c / info.fps - 1.0) > 0.1)
            info.fps_capture = c;
    }
    info.hdr = transfert_depuis_ffprobe(flux[u"color_transfer"_s].toString());
    info.largeur = flux[u"width"_s].toInt();
    info.hauteur = flux[u"height"_s].toInt();
    info.codec = flux[u"codec_name"_s].toString();
    return info;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// VideoInfo
// ---------------------------------------------------------------------------------------------

int VideoInfo::trame(int i) const
{
    if (dates.empty())
        return i;
    const int n = static_cast<int>(dates.size());
    if (dates_reelles)
        return std::min(i, n - 1);
    const double t = dates[0] + (i + 0.5) / fps;
    const int k = static_cast<int>(std::upper_bound(dates.begin(), dates.end(), t) - dates.begin());
    return std::min(std::max(k, 1), n) - 1;
}

int VideoInfo::trame_de_date(double t) const
{
    if (dates.empty())
        return static_cast<int>(std::max(std::round((t - t0) * fps), 0.0));
    const int k = static_cast<int>(std::lower_bound(dates.begin(), dates.end(), t) - dates.begin());
    const int n = static_cast<int>(dates.size());
    if (k == 0)
        return 0;
    if (k == n || t - dates[k - 1] < dates[k] - t)
        return k - 1;
    return k;
}

std::optional<std::vector<double>> VideoInfo::instants() const
{
    if (dates.size() < 2 || !dates_reelles)
        return std::nullopt;
    std::vector<double> res;
    res.reserve(dates.size());
    for (double d : dates)
        res.push_back((d - dates[0]) * fps);
    return res;
}

// ---------------------------------------------------------------------------------------------
// fonctions de lecture
// ---------------------------------------------------------------------------------------------

std::expected<QImage, QString> lit_ppm(const Lecture &lecture, const ConversionHdr *hdr)
{
    char magique[2];
    if (!lit_exact(lecture, magique, 2))
        return std::unexpected(trad("fin du flux vidéo"));
    if (magique[0] != 'P' || magique[1] != '6')
        return std::unexpected(trad("le flux de ffmpeg n'est pas au format PPM"));
    const auto largeur = lit_entier(lecture);
    if (!largeur)
        return std::unexpected(largeur.error());
    const auto hauteur = lit_entier(lecture);
    if (!hauteur)
        return std::unexpected(hauteur.error());
    const auto maxval = lit_entier(lecture);
    if (!maxval)
        return std::unexpected(maxval.error());
    const int w = static_cast<int>(*largeur), h = static_cast<int>(*hauteur);
    if (w <= 0 || h <= 0 || *largeur > 1u << 16 || *hauteur > 1u << 16 || !(*maxval == 255 || *maxval == 65535))
        return std::unexpected(trad("format PPM non pris en charge"));
    if (*maxval == 65535) {
        // composantes de 16 bits, poids fort en premier
        const size_t n = static_cast<size_t>(w) * h * 3;
        std::vector<uint8_t> octets(2 * n);
        if (!lit_exact(lecture, reinterpret_cast<char *>(octets.data()), static_cast<qint64>(octets.size())))
            return std::unexpected(trad("image PPM tronquée"));
        std::vector<uint16_t> mots(n);
        for (size_t k = 0; k < n; ++k)
            mots[k] = static_cast<uint16_t>(octets[2 * k] << 8 | octets[2 * k + 1]);
        if (hdr)
            return hdr->convertit(w, h, mots);
        QImage img(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) {
            uchar *ligne = img.scanLine(y);
            for (int k = 0; k < 3 * w; ++k)
                ligne[k] = static_cast<uchar>(mots[static_cast<size_t>(y) * 3 * w + k] >> 8);
        }
        return img;
    }
    QImage img(w, h, QImage::Format_RGB888);
    if (img.isNull())
        return std::unexpected(trad("format PPM non pris en charge"));
    for (int y = 0; y < h; ++y) {
        if (!lit_exact(lecture, reinterpret_cast<char *>(img.scanLine(y)), 3 * qint64(w)))
            return std::unexpected(trad("image PPM tronquée"));
    }
    return img;
}

std::optional<double> lit_date(const LectureLigne &ligne)
{
    while (const auto l = ligne()) {
        if (!l->contains("showinfo") || !l->contains(" n:"))
            continue;
        const qsizetype i = l->indexOf("pts_time:");
        if (i < 0)
            return std::nullopt;
        const QByteArray reste = l->mid(i + 9).trimmed();
        const qsizetype fin = reste.indexOf(' ');
        bool ok = false;
        const double t = (fin < 0 ? reste : reste.left(fin)).toDouble(&ok);
        return ok ? std::optional(t) : std::nullopt;
    }
    return std::nullopt;
}

std::optional<double> rationnel(const QString &s)
{
    double v = 0.0;
    bool ok = false;
    if (const qsizetype barre = s.indexOf(u'/'); barre >= 0) {
        bool okd = false;
        const double d = s.mid(barre + 1).toDouble(&okd);
        if (!okd || d == 0.0)
            return std::nullopt;
        v = s.left(barre).toDouble(&ok) / d;
    } else {
        v = s.toDouble(&ok);
    }
    return ok && std::isfinite(v) && v > 0.0 ? std::optional(v) : std::nullopt;
}

std::optional<double> cadence_variable(const std::vector<double> &dates)
{
    if (dates.size() < 3)
        return std::nullopt;
    std::vector<double> ecarts;
    for (size_t k = 1; k < dates.size(); ++k)
        ecarts.push_back(dates[k] - dates[k - 1]);
    std::vector<double> tries = ecarts;
    std::sort(tries.begin(), tries.end());
    const double median = tries[tries.size() / 2];
    const auto habituel = [&](double e) { return std::abs(e - median) <= TOLERANCE_CADENCE * median; };
    if (median <= 0.0 || std::all_of(ecarts.begin(), ecarts.end(), habituel))
        return std::nullopt;
    // la moyenne des intervalles habituels, plus précise que la médiane (les dates sont arrondies à
    // la microseconde)
    double somme = 0.0;
    int n = 0;
    for (double e : ecarts) {
        if (habituel(e)) {
            somme += e;
            ++n;
        }
    }
    return somme / n;
}

bool cadence_presque_reguliere(const std::vector<double> &dates, double pas)
{
    const size_t n = dates.size();
    if (n < 3)
        return false;
    // droite des moindres carrés : date = moyenne + (k - k moyen) × intervalle
    const double k_moyen = (n - 1) / 2.0;
    double d_moyenne = 0.0;
    for (double d : dates)
        d_moyenne += d;
    d_moyenne /= double(n);
    double num = 0.0, den = 0.0;
    for (size_t k = 0; k < n; ++k) {
        num += (double(k) - k_moyen) * (dates[k] - d_moyenne);
        den += (double(k) - k_moyen) * (double(k) - k_moyen);
    }
    const double intervalle = num / den;
    if (!(intervalle > 0.0))
        return false;
    // les dates sont arrondies à la base de temps du conteneur, puis à la microseconde par ffprobe
    const double seuil = 0.02 * intervalle + std::max(pas, 1e-6);
    for (size_t k = 0; k < n; ++k) {
        if (std::abs(dates[k] - (d_moyenne + (double(k) - k_moyen) * intervalle)) > seuil)
            return true;
    }
    return false;
}

QString nom_codec(const QString &codec)
{
    static const std::pair<QString, QString> NOMS[] = {
        {u"h264"_s, u"H.264 (AVC)"_s},  {u"hevc"_s, u"HEVC (H.265)"_s}, {u"av1"_s, u"AV1"_s},
        {u"vp8"_s, u"VP8"_s},           {u"vp9"_s, u"VP9"_s},           {u"prores"_s, u"Apple ProRes"_s},
        {u"apv"_s, u"APV (Samsung)"_s}, {u"mpeg2video"_s, u"MPEG-2"_s}, {u"mjpeg"_s, u"Motion JPEG"_s},
        {u"vvc"_s, u"VVC (H.266)"_s},
    };
    for (const auto &[c, nom] : NOMS) {
        if (c == codec)
            return nom;
    }
    if (codec == u"mpeg4")
        return trad("MPEG-4 partie 2");
    if (codec.isEmpty())
        return trad("inconnu");
    return codec.toUpper();
}

QImage tourne(const QImage &img, int rotation)
{
    rotation = ((rotation % 360) + 360) % 360;
    if (rotation == 0 || img.isNull())
        return img;
    const QImage src = img.format() == QImage::Format_RGB888 ? img : img.convertToFormat(QImage::Format_RGB888);
    const int w = src.width(), h = src.height();
    QImage out = rotation == 180 ? QImage(w, h, QImage::Format_RGB888) : QImage(h, w, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        const uchar *ligne = src.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            int xo = x, yo = y;
            switch (rotation) {
            case 90: xo = h - 1 - y; yo = x; break;   // sens horaire
            case 180: xo = w - 1 - x; yo = h - 1 - y; break;
            case 270: xo = y; yo = w - 1 - x; break;
            }
            std::memcpy(out.scanLine(yo) + 3 * xo, ligne + 3 * x, 3);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// VideoReader
// ---------------------------------------------------------------------------------------------

/// un processus ffmpeg qui décode les trames, dans l'ordre
struct VideoReader::Flux {
    std::unique_ptr<QProcess> processus;
    /// la prochaine trame (à partir de 0) qui sortira du tube
    int prochain = 0;

    ~Flux()
    {
        processus->kill();
        processus->waitForFinished(-1);
    }

    /// lecture de la sortie standard : les images au format PPM
    qint64 lit(char *dest, qint64 n)
    {
        QProcess &p = *processus;
        p.setReadChannel(QProcess::StandardOutput);
        while (p.bytesAvailable() == 0) {
            if (!attend(p, false, ATTENTE_IMAGE))
                break;
        }
        return p.read(dest, n);
    }

    /// une ligne de la sortie d'erreur, où le filtre showinfo écrit les dates
    std::optional<QByteArray> ligne()
    {
        QProcess &p = *processus;
        p.setReadChannel(QProcess::StandardError);
        std::optional<QByteArray> l;
        while (!p.canReadLine()) {
            if (!attend(p, false, ATTENTE_IMAGE))
                break;
        }
        if (p.canReadLine())
            l = p.readLine();
        else if (const QByteArray reste = p.readAll(); !reste.isEmpty())
            l = reste;
        p.setReadChannel(QProcess::StandardOutput);
        return l;
    }

    /// arrête ffmpeg et renvoie son dernier message d'erreur, pour compléter l'erreur `e`
    QString erreur(const QString &e)
    {
        processus->kill();
        processus->waitForFinished(-1);
        const QString journal = QString::fromUtf8(processus->readAllStandardError());
        const QStringList lignes = journal.split(u'\n');
        for (auto it = lignes.rbegin(); it != lignes.rend(); ++it) {
            const QString l = it->trimmed();
            if (!l.isEmpty() && !l.contains(u"showinfo"_s))
                return u"%1 (%2)"_s.arg(e, l);
        }
        return e;
    }
};

VideoReader::VideoReader(const QString &chemin, VideoInfo info_)
    : info(std::move(info_))
    , m_chemin(chemin)
{
    if (info.hdr)
        m_hdr = std::make_shared<ConversionHdr>(*info.hdr);
}

VideoReader::~VideoReader() = default;

std::expected<std::unique_ptr<VideoReader>, QString> VideoReader::ouvre(const QString &chemin,
                                                                      const std::atomic_bool *annule)
{
    // l'indicateur vaut pour toutes les attentes de ce fil pendant l'ouverture
    const std::atomic_bool *avant = std::exchange(t_annule, annule);
    const auto retablit = qScopeGuard([avant] { t_annule = avant; });
    auto meta = sonde(chemin);
    if (!meta)
        return std::unexpected(meta.error());
    std::unique_ptr<VideoReader> lecteur(new VideoReader(chemin, std::move(*meta)));
    // on décode la première trame : ses dimensions sont celles des images (ffmpeg applique de
    // lui-même une éventuelle rotation des métadonnées), et sa date est l'origine des temps
    auto lance = lecteur->lance_ffmpeg(0);
    if (!lance)
        return std::unexpected(lance.error());
    std::unique_ptr<Flux> flux = std::move(*lance);
    const auto premiere = lit_ppm([&](char *d, qint64 n) { return flux->lit(d, n); }, lecteur->m_hdr.get());
    if (!premiere)
        return std::unexpected(
            trad("La vidéo est au format {}, que ffmpeg n'a pas su décoder : {}.\n\n"
                 "Utilisez la version de MeVCine livrée avec son propre ffmpeg (dossier « ffmpeg » "
                 "à côté du programme), ou installez une version complète de ffmpeg.",
                 nom_codec(lecteur->info.codec), flux->erreur(premiere.error())));
    if (const auto t = lit_date([&] { return flux->ligne(); })) {
        VideoInfo &info = lecteur->info;
        // les dates des paquets doivent être celles des trames décodées ; sinon (B-frames
        // « empaquetées » des AVI...), on suppose les trames régulières à partir de la première
        if (!info.dates.empty() && std::abs(*t - info.dates[0]) > 0.5 / info.fps) {
            info.dates.clear();
            info.cadence_variable = false;
            info.dates_reelles = false;
        }
        info.t0 = *t;
    }
    flux->prochain = 1;
    lecteur->info.largeur = premiere->width();
    lecteur->info.hauteur = premiere->height();
    lecteur->met_en_cache(0, *premiere);
    lecteur->m_flux = std::move(flux);
    return lecteur;
}

void VideoReader::confie_au_fil(QThread *fil)
{
    if (m_flux)
        m_flux->processus->moveToThread(fil);
}

void VideoReader::set_rotation(int rotation)
{
    rotation = ((rotation % 360) + 360) % 360;
    if (rotation != m_rotation) {
        m_rotation = rotation;
        vide_cache();
    }
}

QSize VideoReader::dimensions() const
{
    if (m_rotation % 180 == 90)
        return {info.hauteur, info.largeur};
    return {info.largeur, info.hauteur};
}

void VideoReader::vide_cache()
{
    m_cache.clear();
    m_ordre_cache.clear();
    m_taille_cache = 0;
}

void VideoReader::oublie_cache_et_flux()
{
    vide_cache();
    m_flux.reset();
}

void VideoReader::sans_conversion_hdr()
{
    m_hdr.reset();
}

void VideoReader::met_en_cache(int trame, const QImage &img)
{
    if (m_cache.contains(trame))
        return;
    m_taille_cache += img.sizeInBytes();
    m_cache.emplace(trame, img);
    m_ordre_cache.push_back(trame);
    while (m_taille_cache > CACHE_MAX && !m_ordre_cache.empty()) {
        const int vieux = m_ordre_cache.front();
        m_ordre_cache.pop_front();
        if (const auto it = m_cache.find(vieux); it != m_cache.end()) {
            m_taille_cache -= it->second.sizeInBytes();
            m_cache.erase(it);
        }
    }
}

std::expected<QImage, QString> VideoReader::image(int index)
{
    // le cache et le flux de ffmpeg sont indexés par trames
    const int i = info.trame(std::max(index, 1) - 1);
    if (const auto it = m_cache.find(i); it != m_cache.end())
        return it->second;
    const bool relance = !m_flux || i < m_flux->prochain || i - m_flux->prochain > SAUT_MAX;
    if (relance) {
        m_flux.reset();
        auto flux = lance_ffmpeg(i);
        if (!flux)
            return std::unexpected(flux.error());
        m_flux = std::move(*flux);
    }
    while (true) {
        Flux &flux = *m_flux;
        const auto brute = lit_ppm([&](char *d, qint64 n) { return flux.lit(d, n); }, m_hdr.get());
        if (!brute) {
            const QString e = flux.erreur(brute.error());
            m_flux.reset();
            return std::unexpected(e);
        }
        // la trame est identifiée par sa date ; à défaut, on compte
        const auto t = lit_date([&] { return flux.ligne(); });
        const int n = t ? info.trame_de_date(*t) : flux.prochain;
        flux.prochain = n + 1;
        const QImage img = tourne(*brute, m_rotation);
        met_en_cache(n, img);
        if (n >= i)
            return img;
    }
}

std::expected<std::unique_ptr<VideoReader::Flux>, QString> VideoReader::lance_ffmpeg(int i) const
{
    QStringList args{u"-nostdin"_s, u"-hide_banner"_s, u"-nostats"_s, u"-v"_s, u"info"_s};
    // ffmpeg arrondit la date demandée selon la base de temps du flux : on démarre un peu avant, et
    // les trames en trop sont reconnues à leur date. Avant la première trame, on ne cherche pas :
    // ffmpeg commencerait le décodage sur une trame qui n'est pas une trame clé.
    double t, debut;
    if (!info.dates.empty()) {
        t = info.dates[std::min<size_t>(i, info.dates.size() - 1)] - 1.5 / info.fps;
        debut = info.dates.front();
    } else {
        t = info.t0 + (i - 1.5) / info.fps;
        debut = info.t0;
    }
    if (i > 0 && t > debut)
        // dates absolues, sans décalage par la date de début du fichier
        args << u"-seek_timestamp"_s << u"1"_s << u"-ss"_s << QString::fromStdString(std::format("{:.6f}", t));
    // -copyts : les dates écrites par showinfo restent celles du fichier
    args << u"-copyts"_s << u"-i"_s << m_chemin;
    args << u"-map"_s << u"0:V:0"_s << u"-an"_s << u"-sn"_s << u"-dn"_s << u"-vf"_s << u"showinfo"_s << u"-fps_mode"_s
         << u"passthrough"_s;
    // vidéo HDR : 16 bits par composante, pour la conversion en SDR
    const QString format = m_hdr ? u"rgb48be"_s : u"rgb24"_s;
    args << u"-f"_s << u"image2pipe"_s << u"-c:v"_s << u"ppm"_s << u"-pix_fmt"_s << format << u"-"_s;
    auto flux = std::make_unique<Flux>();
    flux->processus = lance(u"ffmpeg"_s, args);
    if (!flux->processus->waitForStarted(-1))
        return std::unexpected(erreur_lancement(u"ffmpeg"_s, *flux->processus));
    flux->prochain = i;
    return flux;
}
