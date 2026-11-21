// Accès au fichier .mevpix. La description des mesures qu'il contient se trouve dans projet.h.
#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

class QFile;
struct ContenuSeance;

inline constexpr char SIGNATURE_SEANCE[] = "MeVPix/1\n";
inline constexpr qsizetype TAILLE_MAX_ENTETE_SEANCE = 16 * 1024 * 1024;

/// Chemin utilisé par toutes les fenêtres pour verrouiller le même fichier, même via un lien symbolique.
QString chemin_reel_seance(const QString &chemin);

/// Lit depuis la position courante jusqu'à la fin. Le fichier doit être ouvert en lecture.
/// Une empreinte SHA-256 identifie le contenu exact, y compris l'image. Un résultat vide signale une erreur.
QByteArray empreinte_lecture(QFile &fichier);

enum class EtatSauvegarde { Enregistree, Conflit, Occupe, VerrouImpossible, EnteteTropGrande, FichierSpecial, Erreur };

struct ResultatSauvegarde {
    EtatSauvegarde etat;
    QByteArray empreinte;  ///< Version à retenir après une écriture réussie.
    QString erreur;       ///< Détail fourni par le système ou le codeur PNG.
    bool reussie() const { return etat == EtatSauvegarde::Enregistree; }
};

struct VersionFichier {
    /// Empreinte vide si le chemin est libre. std::nullopt signale l'échec de la vérification.
    std::optional<QByteArray> empreinte;
    QString detail;
    EtatSauvegarde causeEchec = EtatSauvegarde::Erreur;
};
VersionFichier empreinte_sur_disque(const QString &chemin);

/// Remplace le fichier seulement s'il correspond à la version attendue.
/// Pour créer un fichier, passer une empreinte vide (valeur par défaut).
ResultatSauvegarde enregistre_seance(const QString &chemin, const ContenuSeance &seance,
                                    const QByteArray &empreinteAttendue = {});
