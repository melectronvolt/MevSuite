// Traductions de l'interface. Chaque langue fournit tous les textes : le compilateur refuse une
// traduction incomplète (option -Werror=missing-field-initializers de CMakeLists.txt). Pour ajouter
// une langue : une valeur de `Lang` et une table `Tr` dans i18n.cpp.

#pragma once

#include <QString>
#include <QStringList>

#include <array>
#include <cstddef>
#include <optional>

enum class Lang { Fr, En };

inline constexpr std::array<Lang, 2> ALL_LANGS = {Lang::Fr, Lang::En};

/// Code ISO 639-1, utilisé dans le fichier de réglages.
QString langCode(Lang lang);
/// Reconnaît « fr », « fr_FR.UTF-8 », « en-GB »… (vide pour une langue non traduite).
std::optional<Lang> langFromCode(const QString &code);
/// Nom de la langue dans cette langue, pour le menu.
QString langName(Lang lang);
/// Première langue traduite d'une liste de préférences (« ru », « it », « en » → anglais).
std::optional<Lang> firstTranslated(const QStringList &codes);
/// Langue choisie ou, à défaut, celle du système ; anglais si elle n'est pas traduite.
Lang resolveLang(std::optional<Lang> choice);

/// Nombre de composants principaux affichés dans « À propos » (voir about.cpp).
inline constexpr size_t MAIN_COMPONENT_COUNT = 5;

/// Textes de l'interface dans une langue.
struct Tr {
    /// Séparateur décimal des nombres affichés et des fichiers CSV.
    QChar decimal;

    // Fenêtre, accueil, menus
    QString subtitle;
    QString tagline;
    QString (*version)(const QString &);
    QString menuFile;
    QString menuView;
    QString menuMode;
    QString modeSimple;
    QString modeSimpleTip;
    QString modeExpert;
    QString modeExpertTip;
    QString modeSimpleOn;
    QString modeExpertOn;
    /// Mode affiché à droite de la barre d'état, et sa bulle d'aide.
    QString modeSimpleName;
    QString modeExpertName;
    QString modeStatusTip;
    QString channelMenu;
    QString menuQuit;
    /// Bouton de langue (drapeau de la langue affichée) : bulle d'aide, message après le changement.
    QString switchLanguage;
    QString languageChanged;
    /// Bouton de thème (soleil en clair, lune en sombre) : bulle d'aide selon le thème affiché.
    QString themeToDark;
    QString themeToLight;
    QString menuHelp;
    QString menuManual;
    /// Bouton de la notice (à droite de la barre de menus) et sa bulle d'aide.
    QString menuAbout;
    QString menuAboutQt;
    QString (*helpOpened)(const QString &);
    QString (*helpFailed)(const QString &);

    // Menu Fichier, menu Affichage
    QString openSound;
    /// Noms des sons d'exemple, dans l'ordre de `audio::ALL_EXAMPLES`.
    std::array<QString, 6> exampleNames;
    /// Moyenne des voies, gauche, droite.
    std::array<QString, 3> channelNames;
    QString fitAll;
    QString zoomSelection;
    QString settingsButton;
    QString exportSpectrum;
    QString exportExtract;

    // Barre d'état, chargement
    QString statusStart;
    QString unexpectedError;
    /// (message d'erreur, chemin du journal) : affiché dans une boîte de message avant la fermeture.
    QString (*fatalError)(const QString &, const QString &);
    QString chooseSound;
    QString dialogOpen;
    QString sounds;
    QString allFiles;
    QString (*loading)(const QString &);
    QString (*openFailed)(const QString &, const QString &);
    QString unsupportedFormat;
    QString noAudio;
    /// (nom, « 44,1 kHz · stéréo · 3,2 s »)
    QString (*loaded)(const QString &, const QString &);
    QString truncated;
    QString mono;
    QString stereo;
    QString (*channelsN)(size_t);
    QString (*settingsFailed)(const QString &);
    QString dropHint;
    QString emptyHint;
    /// Page d'accueil : ligne sous les boutons.
    QString welcomeDropHint;

    // Onglets : les deux activités
    QString tabPeriod;
    QString tabPeriodTip;
    QString tabFrequencies;
    QString tabFrequenciesTip;

    // Signal
    QString signalTitle;
    QString signalHint;
    QString signalHintPeriod;
    QString listenSound;
    QString listenExtract;
    /// Écoute en mode simplifié : tout le son (mesure de la période), l'extrait (étude des fréquences).
    QString listenSoundOnly;
    QString listenExtractOnly;
    QString listenTip;
    QString playTip;
    QString pauseTip;
    QString stopTip;
    QString (*playFailed)(const QString &);
    QString noAudioOutput;
    QString periodHeading;
    QString periodHint;
    QString axisTime;
    QString axisSignal;

    // Spectre
    QString spectrumTitle;
    QString spectrumHint;
    QString spectrumHintExpert;
    QString extractHeading;
    QString reticleHint;
    QString f0RightClick;
    QString spectrumEmpty;
    QString extractTooShort;
    QString axisFreq;
    QString axisAmplitude;
    QString axisDb;

    // Panneau des mesures
    QString snapZero;
    QString snapZeroTip;
    QString autocorrelation;
    QString autocorrelationTip;
    QString noPeriod;
    /// (durée, nombre d'échantillons)
    QString (*extractInfo)(const QString &, size_t);
    QString computing;
    /// (taille de la FFT, nom de la fenêtre)
    QString (*fftInfo)(size_t, const QString &);
    /// (nombre de segments, taille d'un segment)
    QString (*segmentsInfo)(size_t, size_t);
    QString dfLabel;
    QString dfTip;
    QString resolutionLabel;
    QString resolutionTip;
    QString fundamentalHeading;
    QString f0Auto;
    QString f0Manual;
    QString f0None;
    QString f0NoPeak;
    QString autoButton;
    QString colRank;
    QString colFreq;
    QString colRatio;
    QString colAmp;
    QString colRel;
    QString copyTable;
    QString tableCopied;
    QString (*setF0)(const QString &);
    QString nearestPeak;
    QString noPeak;
    QString setMarks;
    QString harmonicMarks;
    QString harmonicMarksTip;

    // Réglages
    QString settingsTitle;
    QString setAnalysisHeading;
    QString setWindow;
    /// Dans l'ordre de `dsp::ALL_WINDOWS`.
    std::array<QString, 6> windowNames;
    std::array<QString, 6> windowTips;
    QString setFft;
    QString fftTip;
    QString fftAuto;
    QString (*fftPoints)(size_t);
    QString setThreshold;
    QString thresholdTip;
    QString setHarmonics;
    QString setChannel;
    QString setDisplayHeading;
    QString setAmplitude;
    QString ampLinear;
    QString ampDb;
    QString setFreqAxis;
    QString axisLinear;
    QString axisLog;
    QString setFmax;
    QString fmaxAuto;
    QString defaults;
    QString close;

    // Exports
    QString chooseSave;
    QString dialogExportSpectrum;
    QString dialogExportExtract;
    QString csvSpectrumName;
    QString csvExtractName;
    QString csvFreq;
    QString csvAmplitude;
    QString csvDb;
    QString csvTime;
    QString csvSignal;
    QString (*exported)(const QString &);
    QString (*exportFailed)(const QString &);

    // « À propos »
    QString author;
    QString contact;
    QString ossHeading;
    QString ossIntro;
    /// Rôle des composants principaux, dans l'ordre de `MAIN_COMPONENTS` (about.cpp).
    std::array<QString, MAIN_COMPONENT_COUNT> roles;
    /// Licence de MeVSono et de ses composants, renvoi vers « À propos de Qt ».
    QString licenseNote;
    QString settingsFile;
    QString settingsImposed;
};

const Tr &texts(Lang lang);
