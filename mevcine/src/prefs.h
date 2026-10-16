// Préférences et fichiers de projet « .mevcine ».
//
// La configuration est un fichier de type INI à une seule section `[DEFAULT]`. Un fichier de projet
// reprend cette configuration sous forme de commentaires (« # clé = valeur »), suivie des pointages en
// mètre, séparés par des tabulations. C'est aussi le format des anciens fichiers « .mecavideo », que
// MeVCine sait rouvrir. Les formats sont ceux de MeVCine (version Rust) : configuration et projets
// passent de l'une à l'autre.

#pragma once

#include "i18n.h"
#include "vecteur.h"

#include <QString>

#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace prefs {

/// version du format des fichiers de MeVCine
inline const QString VERSION = QStringLiteral("1.0");
/// version minimale d'un ancien fichier .mecavideo lisible
inline constexpr std::pair<unsigned, unsigned> VERSION_MIN = {7, 3};
/// extension des fichiers de projet
inline const QString EXTENSION_PROJET = QStringLiteral("mevcine");

class Config {
public:
    /// la configuration par défaut
    Config();

    std::optional<QString> get(const QString &cle) const;
    void set(const QString &cle, const QString &valeur);
    std::optional<qint64> get_int(const QString &cle) const;
    std::optional<double> get_float(const QString &cle) const;
    /// une valeur de la forme « (x, y) », ou rien
    std::optional<Vecteur> get_vecteur(const QString &cle) const;

    /// vrai si la configuration a été écrite par MeVCine, ou par CineMev (son ancien nom)
    bool est_mevcine() const;
    /// vrai si la configuration est lisible : écrite par MeVCine, ou ancien fichier .mecavideo assez
    /// récent
    bool version_ok() const;
    /// la version du programme qui a écrit la configuration
    std::pair<unsigned, unsigned> version() const;

    /// complète la configuration à partir d'un texte au format INI
    void lit_ini(const QString &texte);
    /// la configuration au format INI, comme l'écrit configparser
    QString ecrit_ini() const;

    /// retire une entrée (réglages du poste, dans un fichier de projet)
    void retire(const QString &cle);

private:
    std::vector<std::pair<QString, QString>> m_entrees;
};

/// la langue choisie avec le bouton du drapeau, si elle l'a été
std::optional<Langue> langue(const Config &c);

/// répertoire de configuration de l'application ; la variable d'environnement MEVCINE_CONF permet
/// d'en choisir un autre
QString dossier_conf();
QString fichier_conf();
/// la sauvegarde automatique du travail en cours (un projet MeVCine), pour le retrouver après un
/// plantage ou une fermeture malencontreuse
QString fichier_secours();
/// le journal des erreurs internes
QString fichier_journal_erreurs();
/// la date et l'heure (UTC) d'un instant compté en secondes depuis 1970, pour le journal des erreurs
QString horodatage(qint64 secondes);

/// lit la configuration sauvegardée, s'il y en a une
std::expected<Config, QString> charge();
/// enregistre la configuration ; renvoie le message d'erreur, vide en cas de succès
QString sauve(const Config &c);

/// vrai si le fichier commence par la signature d'un projet MeVCine, d'un projet .cinemev (ancien nom
/// de MeVCine) ou d'un ancien projet .mecavideo
bool est_projet(const QString &chemin);
/// Le texte d'un fichier de projet : la configuration en commentaires, un message, puis les données
QString texte_projet(const Config &config, const QString &message, const QString &donnees);
/// Écrit la sauvegarde automatique, sans jamais laisser un fichier incomplet ; renvoie le message
/// d'erreur, vide en cas de succès
QString ecrit_secours(const QString &texte);

struct Projet {
    Config config;
    /// les lignes de données [t, x1, y1, ...]
    std::vector<std::vector<double>> donnees;
};
/// Lit un fichier de projet
std::expected<Projet, QString> lit_projet(const QString &chemin);

/// Les lignes d'un texte, comme les découpe Rust : sans le saut de ligne final, ni le retour
/// chariot d'une fin de ligne Windows
QStringList lignes(const QString &texte);

} // namespace prefs
