// Lecture des fichiers son et des pistes audio des vidéos. MeVSono lit directement les WAV.
// Qt Multimedia décode les autres formats avec l'aide du programme ffmpeg si nécessaire.
// L'AAC brut passe en priorité par ffmpeg pour obtenir la même cadence sur toute la prise.

#pragma once

#include "dsp.h"

#include <QAudioFormat>
#include <QString>
#include <QStringList>

#include <array>
#include <atomic>
#include <optional>
#include <vector>

class QIODevice;

namespace audio {

/// Durée maximale lue (au-delà, le reste du fichier est ignoré).
inline constexpr double MAX_SECONDS = 30.0 * 60.0;

/// Extensions proposées par la boîte de dialogue Ouvrir.
QStringList extensions();

/// Voie étudiée d'un son stéréo.
enum class Channel {
    /// Moyenne des voies.
    Mix,
    Left,
    Right,
};

inline constexpr std::array<Channel, 3> ALL_CHANNELS = {Channel::Mix, Channel::Left, Channel::Right};

QString code_canal(Channel c);
std::optional<Channel> canal_du_code(const QString &code);

/// Un son chargé : ses voies, et leur moyenne.
struct Audio {
    /// Nom du fichier affiché.
    QString name;
    /// Fréquence d'échantillonnage en hertz, commune à toutes les voies.
    double rate = 0.0;
    std::vector<Samples> channels;
    Samples mix;
    /// La lecture s'est arrêtée à `MAX_SECONDS`.
    bool truncated = false;
    /// Le décodage s'est arrêté sur une erreur (fichier abîmé) : seul le début du son est là.
    bool damaged = false;
    /// Des échantillons n'étaient pas des nombres finis (fichier abîmé) : ils valent 0.
    bool invalidSamples = false;

    /// Voies de même longueur (les plus longues sont raccourcies) et leur moyenne.
    static Audio fabrique(QString nom, double cadence, std::vector<std::vector<float>> voies, bool dureeLimitee);

    size_t longueur() const { return mix ? mix->size() : 0; }
    double duree() const { return rate > 0.0 ? double(longueur()) / rate : 0.0; }
    /// Signal de la voie demandée (la moyenne pour un son mono).
    const Samples &signal(Channel channel) const;
    /// Plus grande valeur absolue des voies conservées, mesurée lors du chargement.
    float crete() const { return m_crete; }

private:
    float m_crete = 0.0f;
};

enum class LoadError {
    /// Format non reconnu, et pas de ffmpeg pour le convertir.
    Unsupported,
    /// Fichier sans piste audio.
    NoAudio,
    /// Un WAV annonce du son, mais aucune trame complète n'a pu être lue.
    Incomplete,
    /// Autre erreur (fichier introuvable…), décrite par `message`.
    Other,
};

struct LoadResult {
    std::optional<Audio> audio;
    LoadError error = LoadError::Other;
    QString message;
};

/// Lit un WAV depuis un fichier ou un tampon ouverts en lecture et placés au début.
/// La source doit permettre de se déplacer avec seek(). Un format inconnu laisse
/// un autre décodeur essayer. Une lecture interrompue conserve les trames complètes.
std::optional<LoadResult> lit_wav(QIODevice &file, const QString &name, const std::atomic_bool *cancel = nullptr);

/// Convertit la piste audio par défaut (ou la première) en WAV provisoire.
/// Un résultat vide signifie que le convertisseur n'a pas pu être lancé.
std::optional<LoadResult> convertit_avec_ffmpeg(const QString &path, const QString &name,
                                                const std::atomic_bool *cancel = nullptr);

/// Lit un fichier son ou vidéo. Peut être appelé depuis un fil de fond ; `cancel` (fermeture de la
/// fenêtre) interrompt la lecture et la conversion par ffmpeg.
LoadResult charge(const QString &path, const std::atomic_bool *cancel = nullptr);

/// Assemble les trames PCM reçues du microphone, même coupées entre deux lectures.
class Prise {
public:
    explicit Prise(const QAudioFormat &format);
    void ajoute(const QByteArray &octets);
    /// Crête reçue depuis le dernier relevé.
    float releve_crete();
    double duree() const;
    bool pleine() const;
    /// Dernière opération de la prise. Transfère les échantillons reçus au son retourné.
    std::optional<Audio> termine(QString nom);

private:
    QAudioFormat m_format;
    QByteArray m_reste;
    std::vector<std::vector<float>> m_voies;
    size_t m_limite;
    float m_crete = 0;
};

/// Écrit un fichier WAV PCM 16 bits.
bool ecrit_wav(const QString &path, const Audio &son);
bool ecrit_wav(const QString &path, int rate, const std::vector<std::vector<float>> &channels);

} // namespace audio
