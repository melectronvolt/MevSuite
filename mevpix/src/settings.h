// Réglages conservés d'une séance à l'autre, dans un petit fichier texte `clé = valeur`
// modifiable à la main. Une valeur absente ou illisible conserve le réglage déjà lu
// ou, à défaut, le réglage initial du programme.
//
// Deux fichiers `mevpix.ini` sont lus, dans cet ordre :
// 1. celui de l'utilisateur (dossier de configuration), où MeVPix enregistre les réglages ;
// 2. les réglages du lycée, d'abord recherchés sous /etc/MeVSuite sur Linux, puis à côté
//    du programme. Les valeurs du premier fichier trouvé l'emportent à chaque lancement.

#pragma once

#include "i18n.h"
#include "model.h"
#include "profile.h"

#include <QSize>
#include <QString>

#include <optional>

/// Agrandissement de l'interface accepté (`Ctrl` + / `Ctrl` −).
inline constexpr double UI_ZOOM_MIN = 0.5;
inline constexpr double UI_ZOOM_MAX = 3.0;

/// Emplacement des fichiers de réglages.
struct FichiersReglages {
    /// Réglages de l'utilisateur, lus puis enregistrés par MeVPix (vide si introuvable).
    QString user;
    /// Réglages du lycée, lus seulement et prioritaires au démarrage (vide si absents).
    QString admin;

    /// Nécessite une `QCoreApplication` (dossier du programme).
    static FichiersReglages localise();
};

struct Reglages {
    /// Vide : langue du système.
    std::optional<Lang> lang;
    /// Thème clair ou sombre (`Unknown` : celui du bureau).
    Qt::ColorScheme theme = Qt::ColorScheme::Unknown;
    Unite unite = Unite::Millimetre;
    Canal canal = Canal::GrisPondere;
    int largeurBande = 5;
    /// Agrandissement de l'interface (`Ctrl` + / `Ctrl` −).
    double uiZoom = 1.0;
    /// Dossier proposé par les boîtes de dialogue (vide : aucun).
    QString lastDir;
    /// Taille de la fenêtre non agrandie.
    std::optional<QSize> windowSize;
    /// Fenêtre agrandie à l'écran (elle s'ouvre à sa taille normale tant que l'élève ne l'a pas
    /// agrandie lui-même).
    bool maximized = false;

    /// Réglages de l'utilisateur, puis réglages imposés par-dessus.
    static Reglages charge(const FichiersReglages &files);
    /// Avec un état précédent, écrit uniquement les changements de cette fenêtre dans le fichier
    /// utilisateur. Sans cet état, écrit tous les réglages. Renvoie un message en cas d'échec.
    QString enregistre(const QString &path, const Reglages *avant = nullptr) const;
    static Reglages analyse(const QString &text);
    /// Remplace les réglages présents dans `text` ; les autres restent inchangés.
    void applique(const QString &texte);
    QString en_texte() const;

    bool operator==(const Reglages &) const = default;
};
