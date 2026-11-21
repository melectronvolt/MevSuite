// Préférences et fichiers de projet « .mevcine ».
//
// La configuration est un fichier de type INI à une seule section `[DEFAULT]`. Un fichier de projet
// reprend cette configuration sous forme de commentaires (« # clé = valeur »), suivie des pointages en
// mètres (en pixels sans étalon), séparés par des tabulations. Un champ « nan » signale une
// coordonnée manquante. Le temps est en secondes.

#pragma once

#include "i18n.h"
#include "vecteur.h"

#include <QByteArray>
#include <QString>

#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace prefs {

/// version du format des fichiers de MeVCine
inline const QString VERSION = QStringLiteral("1.0");
/// extension des fichiers de projet
inline const QString EXTENSION_PROJET = QStringLiteral("mevcine");

class Config {
public:
    /// la configuration par défaut
    Config();

    std::optional<QString> valeur_texte(const QString &cle) const;
    void definit(const QString &cle, const QString &valeur);
    std::optional<qint64> valeur_entiere(const QString &cle) const;
    std::optional<double> valeur_reelle(const QString &cle) const;
    /// une valeur de la forme « (x, y) », ou rien
    std::optional<Vecteur> valeur_vecteur(const QString &cle) const;

    /// vrai si la configuration a été écrite par MeVCine (signature « mevcine » ou « cinemev »)
    bool est_mevcine() const;
    /// vrai si la configuration est lisible : écrite par MeVCine
    bool version_ok() const;
    /// la version du programme qui a écrit la configuration
    std::pair<unsigned, unsigned> version_du_fichier() const;

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
/// le journal des erreurs internes
QString fichier_journal_erreurs();
/// la date et l'heure (UTC) d'un instant compté en secondes depuis 1970, pour le journal des erreurs
QString horodatage(qint64 secondes);

/// lit la configuration sauvegardée, s'il y en a une
std::expected<Config, QString> charge();
/// enregistre la configuration ; renvoie le message d'erreur, vide en cas de succès
QString sauve(const Config &c);

/// vrai si le fichier commence par la signature d'un projet MeVCine
bool est_projet(const QString &chemin);
/// Le texte d'un fichier de projet : la configuration en commentaires, un message, puis les données
QString texte_projet(const Config &config, const QString &message, const QString &donnees);
/// Écrit un projet ou son secours. L'ancien fichier reste lisible si l'écriture échoue.
QString ecrit_projet(const QString &chemin, const QString &texte);
/// Empreinte du contenu exact d'un projet, retours à la ligne compris.
QByteArray empreinte_projet(const QByteArray &contenu);
/// Vérifie que le fichier enregistré existe encore avec ce contenu.
bool projet_identique(const QString &chemin, const QByteArray &empreinte);

struct Projet {
    Config config;
    /// les lignes de données [t, x1, y1, ...]
    std::vector<std::vector<double>> donnees;
    QByteArray empreinte_lue;
};
/// Lit un fichier de projet
std::expected<Projet, QString> lit_projet(const QString &chemin);

/// Les lignes d'un texte, sans le saut de ligne final, ni le retour
/// chariot d'une fin de ligne Windows
QStringList lignes(const QString &texte);

} // namespace prefs
