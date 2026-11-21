// Traductions de l'interface. Chaque langue fournit tous les textes : le compilateur refuse une
// traduction incomplète (option -Werror=missing-field-initializers de CMakeLists.txt). Pour ajouter
// une langue : une valeur de `Lang` et une table `Textes` dans i18n.cpp.

#pragma once

#include "profile.h"

#include <QString>
#include <QStringList>

#include <array>
#include <optional>

enum class Lang { Fr, En };

inline constexpr std::array<Lang, 2> LANGUES = {Lang::Fr, Lang::En};

/// Code ISO 639-1, utilisé dans le fichier de réglages.
QString code_langue(Lang lang);
/// Reconnaît « fr », « fr_FR.UTF-8 », « en-GB »… (vide pour une langue non traduite).
std::optional<Lang> langue_du_code(const QString &code);
/// Nom de la langue dans cette langue, pour le menu.
QString nom_langue(Lang lang);
/// Première langue traduite d'une liste de préférences (« ru », « it », « en » → anglais).
std::optional<Lang> premier_traduit(const QStringList &codes);
/// Langue choisie ou, à défaut, celle du système ; anglais si elle n'est pas traduite.
Lang resout_langue(std::optional<Lang> choice);

/// Nombre de composants principaux affichés dans « À propos » (voir about.cpp).
inline constexpr size_t MAIN_COMPONENT_COUNT = 5;

/// Textes de l'interface dans une langue.
struct Textes {
    /// Séparateur décimal des nombres affichés et du CSV.
    QChar decimal;

    // Fenêtre et menus
    QString subtitle;
    QString menuFile;
    QString menuQuit;
    /// Bouton de langue (drapeau de la langue affichée) : bulle d'aide, message après le changement.
    QString switchLanguage;
    QString languageChanged;
    /// Bouton de thème (soleil en clair, lune en sombre) : bulle d'aide selon le thème affiché.
    QString themeToDark;
    QString themeToLight;
    QString menuHelp;
    QString menuAbout;
    QString menuAboutQt;
    /// Notice accessible depuis le menu Aide ou par F1. Les deux messages rendent compte de son ouverture.
    QString menuManual;
    QString (*helpOpened)(const QString &);
    QString (*helpFailed)(const QString &);

    // Barre d'outils
    QString openImage;
    QString toolCalibrate;
    QString toolMeasure;
    QString toolProfile;
    QString zoomInImage;
    QString zoomOutImage;
    QString fitWindow;
    /// Infobulles des flèches de l'image, dans l'ordre gauche, droite, haut, bas.
    std::array<QString, 4> panImage;
    QString (*scale)(const QString &);
    QString notCalibrated;

    // Barre d'état, chargement
    QString statusStart;
    QString unexpectedError;
    /// (message d'erreur, chemin du journal) : affiché dans une boîte de message avant la fermeture.
    QString (*fatalError)(const QString &, const QString &);
    QString chooseImage;
    QString dialogOpen;
    QString images;
    QString allFiles;
    QString (*loading)(const QString &);
    QString (*openFailed)(const QString &, const QString &);
    /// Raison donnée dans `openFailed` pour un format d'image non reconnu.
    QString unsupportedFormat;
    QString incompleteJpeg;
    QString (*imageLoaded)(const QString &, int, int);
    QString (*settingsFailed)(const QString &);
    QString settingsBusy;
    /// Dossier qui n'a pas pu être créé (réglages, notice).
    QString (*folderFailed)(const QString &);
    /// Zoom de l'image en pourcentage, à droite de la barre d'état, et sa bulle d'aide.
    QString (*zoomInfo)(const QString &);
    QString zoomTip;
    /// Confirmation avant de remplacer une image qui porte un étalonnage ou des mesures.
    QString (*replaceImage)(const QString &);
    QString cancel;
    /// Confirmation avant de fermer une séance modifiée.
    QString quitWithMeasures;

    // Page d'accueil (aucune image ouverte), zone image
    /// Ligne discrète sous le bouton Ouvrir.
    QString welcomeDropHint;
    /// Voile affiché pendant qu'on glisse un fichier au-dessus de la fenêtre.
    QString dropHere;
    QString (*calibTag)(const QString &);
    QString (*measureTag)(int, const QString &);

    // Étalonnage
    QString calibHeading;
    QString calibStep1;
    QString calibStep2;
    QString (*calibDrawn)(const QString &);
    QString (*segmentDrawn)(const QString &);
    QString noSegment;
    QString realLength;
    QString example;
    QString invalidLength;
    QString validate;
    QString (*calibValidated)(const QString &);
    /// (mm par px, px par mm, unité)
    QString (*currentScale)(const QString &, const QString &, const QString &);
    QString calibChanged;

    // Mesures
    QString measuresHeading;
    QString uncalibratedPx;
    QString measureHint;
    QString deleteMeasure;
    QString noMeasure;
    QString clearAll;
    QString copyMeasures;
    QString measuresCopied;
    /// Titres des colonnes du tableau copié : mesure, longueur (unité).
    QString colMeasure;
    QString (*colLength)(const QString &);

    // Profil d'intensité
    QString profileHeading;
    QString profileHint;
    QString cutDrawn;
    QString cutPrompt;
    QString channelLabel;
    /// Dans l'ordre de `CANAUX`.
    std::array<QString, 4> channels;
    QString thickness;
    QString thicknessTip;
    QString cursors;
    QString cursorsHint;
    QString (*alerteSaturation)(const QString &);
    QString (*axisX)(const QString &);
    QString axisY;
    QString exportCsv;
    QString chooseSave;
    /// Le fichier complété de « .csv » existe déjà : le remplacer ?
    QString (*replaceFile)(const QString &);
    QString replaceButton;
    QString dialogExport;
    QString csvName;
    QString (*exported)(const QString &);
    QString (*exportFailed)(const QString &);

    // Accueil et « À propos »
    QString tagline;
    QString (*version)(const QString &);
    QString author;
    QString contact;
    QString website;
    QString ossHeading;
    QString ossIntro;
    /// Rôle des composants principaux, dans l'ordre de `MAIN_COMPONENTS` (about.cpp).
    std::array<QString, MAIN_COMPONENT_COUNT> roles;
    /// Licence de MeVPix et renvoi vers « À propos de Qt ».
    QString licenseNote;
    QString settingsFile;
    QString settingsImposed;
    QString close;

    QString nom_canal(Canal ch) const { return channels[static_cast<size_t>(ch)]; }
    QString saveSession;
    QString annotatedImage;
    QString invalidSession;
    QString regularFileRequired;
    QString referenceSegment;
    QString allMeasurements;
    QString copyAnnotation;
    QString saveSessionAs;
    QString discard;
    QString pendingScale;
    QString newScale;
    QString useValidatedScale;
    QString useNewScale;
    QString clippedBand;
    QString cutAndCursors;
    QString includeGraph;
    QString annotationCopied;
    QString savedFile;
    QString savingFile;
    QString saveFailed;
    QString depthConverted;
    QString cutTooShort;
    QString sessionChanged;
    QString sessionBusy;
    QString saveCopy;
    QString lockFailed;
    QString sessionHeaderTooLarge;
};

const Textes &textes(Lang lang);
