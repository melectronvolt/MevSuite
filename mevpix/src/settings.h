// Réglages conservés d'une séance à l'autre, dans un petit fichier texte `clé = valeur`
// modifiable à la main. Une valeur absente ou illisible reprend sa valeur par défaut.
//
// Deux fichiers `mevpix.ini` sont lus, dans cet ordre :
// 1. celui de l'utilisateur (dossier de configuration), où MeVPix enregistre les réglages ;
// 2. celui placé à côté du programme (clé USB, réseau du lycée), s'il existe : ses valeurs
//    l'emportent à chaque lancement et MeVPix ne l'écrit jamais.
//
// Le format est celui de `mevpix.ini` (version Rust) : un fichier se recopie de l'un à l'autre.

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
struct SettingsFiles {
    /// Réglages de l'utilisateur, lus puis enregistrés par MeVPix (vide si introuvable).
    QString user;
    /// Réglages imposés, à côté du programme : lus seulement, ils l'emportent (vide si absent).
    QString admin;

    /// Nécessite une `QCoreApplication` (dossier du programme).
    static SettingsFiles locate();
};

struct Settings {
    /// Vide : langue du système.
    std::optional<Lang> lang;
    /// Thème clair ou sombre (`Unknown` : celui du bureau).
    Qt::ColorScheme theme = Qt::ColorScheme::Unknown;
    Unit unit = Unit::Millimetre;
    Channel channel = Channel::Luminance;
    int band = 5;
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
    static Settings load(const SettingsFiles &files);
    /// Renvoie le message d'erreur, vide en cas de succès.
    QString save(const QString &path) const;
    static Settings parse(const QString &text);
    /// Remplace les réglages présents dans `text` ; les autres restent inchangés.
    void apply(const QString &text);
    QString toText() const;

    bool operator==(const Settings &) const = default;
};
