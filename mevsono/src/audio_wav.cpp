// Dans un WAV, un bloc décrit le codage et un autre contient les trames audio.
// Une trame réunit les échantillons de toutes les voies à un même instant.
#include "audio.h"
#include "savefile.h"

#include <QDataStream>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace audio {

namespace {

constexpr quint16 WAVE_PCM = 1;
constexpr quint16 WAVE_FLOAT = 3;
constexpr quint16 WAVE_EXTENSIBLE = 0xFFFE;

/// Le PCM entier est ramené à l'échelle de −1 à 1. Les fichiers flottants gardent leur amplitude.
using DecodeurEchantillon = float (*)(const char *);

DecodeurEchantillon decodeur(quint16 codage, int bitsParEchantillon)
{
    if (codage == WAVE_PCM) {
        switch (bitsParEchantillon) {
        case 8: return [](const char *p) { return (float(quint8(p[0])) - 128.0f) / 128.0f; };
        case 16: return [](const char *p) { return float(qFromLittleEndian<qint16>(p)) / 32768.0f; };
        case 24:
            return [](const char *p) {
                // trois octets, placés dans les poids forts d'un entier de 32 bits
                const quint32 mot = (quint32(quint8(p[0])) << 8) | (quint32(quint8(p[1])) << 16)
                    | (quint32(quint8(p[2])) << 24);
                return float(double(qint32(mot)) / 2147483648.0);
            };
        case 32: return [](const char *p) { return float(double(qFromLittleEndian<qint32>(p)) / 2147483648.0); };
        }
    } else if (codage == WAVE_FLOAT) {
        switch (bitsParEchantillon) {
        case 32: return [](const char *p) { return std::bit_cast<float>(qFromLittleEndian<quint32>(p)); };
        case 64: return [](const char *p) { return float(std::bit_cast<double>(qFromLittleEndian<quint64>(p))); };
        }
    }
    return nullptr;
}

/// Une lecture peut s'arrêter au milieu d'une trame stéréo. Rassembler ses deux
/// morceaux avant de décoder les échantillons. Un bloc court indique une fin ou une erreur.
QByteArray lit_bloc(QIODevice &fichier, qint64 taille, const std::atomic_bool *annulation, bool *erreurLecture = nullptr)
{
    QByteArray bloc(taille, Qt::Uninitialized);
    qint64 lus = 0;
    while (lus < taille && !(annulation && *annulation)) {
        const qint64 n = fichier.read(bloc.data() + lus, taille - lus);
        if (n <= 0) {
            if (erreurLecture)
                *erreurLecture = n < 0;
            break;
        }
        lus += n;
    }
    bloc.truncate(lus);
    return bloc;
}

/// Vrai si le fichier, de `debut` à la fin, n'est qu'une suite de blocs RIFF (identifiant de quatre
/// caractères, taille qui mène au bloc suivant puis à la fin du fichier) : des métadonnées LIST ou
/// id3, et non des échantillons. Le fichier revient ensuite à `debut`.
bool que_des_blocs(QIODevice &fichier, qint64 debut, const std::atomic_bool *annulation)
{
    const qint64 fin = fichier.size();
    qint64 pos = debut;
    bool ok = pos + 8 <= fin;
    while (ok && pos + 8 <= fin) {
        if (annulation && *annulation)
            break;
        fichier.seek(pos);
        const QByteArray entete = lit_bloc(fichier, 8, annulation);
        ok = entete.size() == 8 && std::all_of(entete.begin(), entete.begin() + 4, [](char c) { return c >= 0x20 && c <= 0x7e; });
        const quint32 taille = ok ? qFromLittleEndian<quint32>(entete.constData() + 4) : 0;
        pos += 8 + qint64(taille) + (taille % 2);
    }
    fichier.seek(debut);
    // l'octet de bourrage d'un dernier bloc de taille impaire manque parfois
    return ok && (pos == fin || pos == fin + 1);
}

struct EnteteWav {
    quint16 codage = 0;
    int nombreVoies = 0;
    int bitsParEchantillon = 0;
    int octetsParTrame = 0;
    quint32 cadence = 0;
    quint32 tailleAnnoncee = 0;
};

/// Lit le bloc « fmt » et place le fichier au premier échantillon du bloc « data ».
std::optional<EnteteWav> lit_entete(QIODevice &fichier, const std::atomic_bool *annulation)
{
    const QByteArray riff = lit_bloc(fichier, 12, annulation);
    if (riff.size() != 12 || !riff.startsWith("RIFF") || !riff.endsWith("WAVE"))
        return std::nullopt;

    EnteteWav format;
    while (!(annulation && *annulation)) {
        const QByteArray entete = lit_bloc(fichier, 8, annulation);
        if (entete.size() != 8)
            return std::nullopt;
        const quint32 taille = qFromLittleEndian<quint32>(entete.constData() + 4);
        if (entete.startsWith("data")) {
            format.tailleAnnoncee = taille;
            return format;
        }
        // Les blocs de longueur impaire sont suivis d'un octet de bourrage.
        const qint64 longueurBloc = qint64(taille) + (taille % 2);
        if (longueurBloc > fichier.size() - fichier.pos())
            return std::nullopt;
        const qint64 suivant = fichier.pos() + longueurBloc;
        if (entete.startsWith("fmt ")) {
            const QByteArray fmt = lit_bloc(fichier, std::min<quint32>(taille, 40), annulation);
            if (fmt.size() < 16)
                return std::nullopt;
            format.codage = qFromLittleEndian<quint16>(fmt.constData());
            format.nombreVoies = qFromLittleEndian<quint16>(fmt.constData() + 2);
            format.cadence = qFromLittleEndian<quint32>(fmt.constData() + 4);
            format.octetsParTrame = qFromLittleEndian<quint16>(fmt.constData() + 12);
            format.bitsParEchantillon = qFromLittleEndian<quint16>(fmt.constData() + 14);
            if (format.codage == WAVE_EXTENSIBLE) {
                // L'identifiant du sous-format occupe 16 octets. Les deux premiers
                // suffisent à départager PCM et flottant une fois la famille reconnue.
                static const QByteArray finIdentifiant = QByteArray::fromHex("000000001000800000aa00389b71");
                if (fmt.size() < 40 || !fmt.endsWith(finIdentifiant))
                    return std::nullopt;
                const quint16 tailleExtension = qFromLittleEndian<quint16>(fmt.constData() + 16);
                if (tailleExtension < 22 || tailleExtension > taille - 18)
                    return std::nullopt;
                format.codage = qFromLittleEndian<quint16>(fmt.constData() + 24);
            }
        }
        if (!fichier.seek(suivant))
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::optional<LoadResult> lit_wav(QIODevice &fichier, const QString &nom, const std::atomic_bool *annulation)
{
    const auto entete = lit_entete(fichier, annulation);
    if (annulation && *annulation)
        return LoadResult{std::nullopt, LoadError::Other, {}};
    if (!entete)
        return std::nullopt;
    const int nombreVoies = entete->nombreVoies;
    const int octetsParTrame = entete->octetsParTrame;
    const int octetsParEchantillon = entete->bitsParEchantillon / 8;
    const DecodeurEchantillon decodeEchantillon = decodeur(entete->codage, entete->bitsParEchantillon);
    if (!decodeEchantillon || nombreVoies < 1 || entete->cadence == 0 ||
        octetsParTrame < nombreVoies * octetsParEchantillon)
        return std::nullopt;

    // le bloc des données seulement : des métadonnées (LIST, id3…) suivent souvent le son. Taille
    // absente (fichier écrit en continu) : jusqu'à la fin du fichier, sauf si ce qui suit n'est que des
    // blocs de métadonnées (le bloc des données est alors vide). Taille plus grande que le fichier :
    // enregistrement coupé, signalé.
    const qint64 debutDonnees = fichier.pos();
    qint64 octetsDisponibles = std::max<qint64>(0, fichier.size() - debutDonnees);
    bool incompletSurDisque = false;
    const quint32 tailleAnnoncee = entete->tailleAnnoncee;
    if (tailleAnnoncee == 0 && que_des_blocs(fichier, debutDonnees, annulation))
        return LoadResult{std::nullopt, LoadError::NoAudio, {}};
    const bool tailleProvisoire = tailleAnnoncee == 0 || tailleAnnoncee == 0x7FFFFFFFu || tailleAnnoncee == 0xFFFFFFFFu;
    if (tailleProvisoire) {
        // Un bloc RIFF de longueur impaire peut finir par un octet de bourrage.
        // Tout autre reste appartient à une trame que le fichier n'a pas achevée.
        const qint64 reste = octetsDisponibles % octetsParTrame;
        const bool bourragePossible = reste == 1 && octetsDisponibles % 2 == 0;
        incompletSurDisque = reste != 0 && !bourragePossible;
    } else {
        incompletSurDisque = qint64(tailleAnnoncee) > octetsDisponibles || tailleAnnoncee % octetsParTrame != 0;
        octetsDisponibles = std::min(octetsDisponibles, qint64(tailleAnnoncee));
    }
    if (annulation && *annulation)
        return LoadResult{std::nullopt, LoadError::Other, {}};
    const qint64 tramesDisponibles = octetsDisponibles / octetsParTrame;
    const qint64 tramesACharger = std::min(tramesDisponibles, qint64(MAX_SECONDS * entete->cadence));
    // En stéréo, une seule copie suffit tant que gauche et droite sont identiques.
    std::vector<std::vector<float>> voies(nombreVoies == 2 ? 1 : size_t(nombreVoies));
    for (auto &voie : voies)
        voie.reserve(size_t(tramesACharger));
    const qint64 tramesParBloc = (128 * 1024) / octetsParTrame;
    qint64 tramesLues = 0;
    bool erreurLecture = false;
    while (tramesLues < tramesACharger) {
        const qint64 tramesDemandees = std::min(tramesParBloc, tramesACharger - tramesLues);
        const QByteArray bloc = lit_bloc(fichier, tramesDemandees * octetsParTrame, annulation, &erreurLecture);
        if (annulation && *annulation)
            return LoadResult{std::nullopt, LoadError::Other, {}};
        const qint64 tramesRecues = bloc.size() / octetsParTrame;
        qint64 indice = 0;
        if (nombreVoies == 2 && voies.size() == 1) {
            for (; indice < tramesRecues; ++indice) {
                const char *trame = bloc.constData() + indice * octetsParTrame;
                if (std::memcmp(trame, trame + octetsParEchantillon, size_t(octetsParEchantillon)) != 0) {
                    auto &droite = voies.emplace_back();
                    droite.reserve(size_t(tramesACharger));
                    droite.assign(voies[0].begin(), voies[0].end());
                    break;
                }
                voies[0].push_back(decodeEchantillon(trame));
            }
        }
        for (; indice < tramesRecues; ++indice) {
            const char *trame = bloc.constData() + indice * octetsParTrame;
            for (int c = 0; c < nombreVoies; ++c)
                voies[size_t(c)].push_back(decodeEchantillon(trame + c * octetsParEchantillon));
        }
        tramesLues += tramesRecues;
        if (tramesRecues < tramesDemandees)
            break;
    }
    const bool incomplet = incompletSurDisque || tramesLues < tramesACharger;
    if (tramesLues == 0 && erreurLecture)
        return LoadResult{std::nullopt, LoadError::Other, fichier.errorString()};
    if (tramesLues == 0)
        return LoadResult{std::nullopt, incomplet ? LoadError::Incomplete : LoadError::NoAudio, {}};
    // Une coupure pendant la lecture d'un fichier de plus de 30 minutes peut
    // arriver bien avant la limite. Seules les trames réellement reçues comptent.
    const bool limiteAtteinte = tramesDisponibles > tramesACharger && tramesLues == tramesACharger;
    const bool voiesIdentiques = nombreVoies == 2 && voies.size() == 1;
    Audio audio = Audio::fabrique(nom, double(entete->cadence), std::move(voies), limiteAtteinte);
    if (voiesIdentiques)
        audio.channels.push_back(audio.mix);
    audio.damaged = incomplet && !limiteAtteinte;
    return LoadResult{std::move(audio), LoadError::Other, {}};
}

namespace {
bool ecrit_pcm(const QString &chemin, int cadence, const std::vector<std::span<const float>> &voies)
{
    if (voies.empty() || voies.size() > 2 || cadence <= 0)
        return false;
    const size_t n = voies.front().size();
    const auto nombreVoies = quint16(voies.size());
    if (n > (UINT32_MAX - 36) / (2u * nombreVoies) ||
        std::ranges::any_of(voies, [n](auto voie) { return voie.size() != n; }))
        return false;
    const quint64 octetsParSeconde = quint64(cadence) * nombreVoies * 2;
    if (octetsParSeconde > UINT32_MAX)
        return false;
    const auto octetsDonnees = quint32(n * nombreVoies * 2);
    QSaveFile fichier(chemin);
    if (!fichier.open(QIODevice::WriteOnly))
        return false;
    QDataStream entete(&fichier);
    entete.setByteOrder(QDataStream::LittleEndian);
    entete.writeRawData("RIFF", 4);
    entete << quint32(36 + octetsDonnees);
    entete.writeRawData("WAVEfmt ", 8);
    entete << quint32(16) << quint16(WAVE_PCM) << nombreVoies << quint32(cadence) << quint32(octetsParSeconde)
           << quint16(nombreVoies * 2) << quint16(16);
    entete.writeRawData("data", 4);
    entete << octetsDonnees;
    if (entete.status() != QDataStream::Ok)
        return false;
    // Encoder plusieurs trames avant chaque écriture sur le disque.
    constexpr size_t tramesParBloc = 16384;
    QByteArray bloc(qsizetype(tramesParBloc * nombreVoies * 2), Qt::Uninitialized);
    for (size_t debut = 0; debut < n; debut += tramesParBloc) {
        const size_t fin = std::min(debut + tramesParBloc, n);
        bloc.resize(qsizetype((fin - debut) * nombreVoies * 2));
        char *destination = bloc.data();
        for (size_t i = debut; i < fin; ++i) {
            for (const auto &voie : voies) {
                // −1 correspond à −32768. La plus grande valeur positive est +32767.
                const float amplitude = std::isfinite(voie[i]) ? std::clamp(voie[i], -1.0f, 1.0f) : 0.0f;
                const auto entier = qint16(std::min(std::lround(amplitude * 32768.0f), 32767L));
                qToLittleEndian(entier, destination);
                destination += 2;
            }
        }
        if (!ecrit_tout(fichier, bloc))
            return false;
    }
    return fichier.commit();
}
} // namespace

bool ecrit_wav(const QString &chemin, const Audio &son)
{
    std::vector<std::span<const float>> voies;
    for (const auto &voie : son.channels)
        voies.emplace_back(*voie);
    if (!(son.rate > 0 && son.rate <= std::numeric_limits<int>::max()))
        return false;
    return ecrit_pcm(chemin, int(son.rate), voies);
}

bool ecrit_wav(const QString &chemin, int cadence, const std::vector<std::vector<float>> &voies)
{
    std::vector<std::span<const float>> vues(voies.begin(), voies.end());
    return ecrit_pcm(chemin, cadence, vues);
}

} // namespace audio
