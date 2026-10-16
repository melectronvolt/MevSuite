#include "i18n.h"

#include <QLocale>

using namespace Qt::StringLiterals;

QString langCode(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return u"fr"_s;
    case Lang::En: return u"en"_s;
    }
    return {};
}

std::optional<Lang> langFromCode(const QString &code)
{
    QString primary;
    for (QChar c : code) {
        if (c.unicode() > 127 || !c.isLetter())
            break;
        primary += c;
    }
    for (Lang lang : ALL_LANGS) {
        if (primary.compare(langCode(lang), Qt::CaseInsensitive) == 0)
            return lang;
    }
    return std::nullopt;
}

QString langName(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return u"Français"_s;
    case Lang::En: return u"English"_s;
    }
    return {};
}

std::optional<Lang> firstTranslated(const QStringList &codes)
{
    for (const QString &code : codes) {
        if (const auto lang = langFromCode(code))
            return lang;
    }
    return std::nullopt;
}

Lang resolveLang(std::optional<Lang> choice)
{
    // Langues d'affichage préférées : variables LANGUAGE, LC_ALL, LC_MESSAGES, LANG sous Linux,
    // langue de l'interface de Windows.
    if (choice)
        return *choice;
    return firstTranslated(QLocale::system().uiLanguages()).value_or(Lang::En);
}

namespace {

/// Entier avec un séparateur des milliers : « 32 768 » (espace fine insécable) ou « 32,768 ».
QString grouped(size_t n, const QString &sep)
{
    const QString digits = QString::number(n);
    QString out;
    for (qsizetype i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += sep;
        out += digits[i];
    }
    return out;
}

QString frInt(size_t n)
{
    return grouped(n, u" "_s);
}

QString enInt(size_t n)
{
    return grouped(n, u","_s);
}

const Tr FR = {
    .decimal = u',',

    .subtitle = u"analyse spectrale des sons"_s,
    .tagline = u"Décomposition spectrale des sons"_s,
    .version = [](const QString &v) { return u"Version %1"_s.arg(v); },
    .menuFile = u"Fichier"_s,
    .menuView = u"Affichage"_s,
    .menuMode = u"Mode"_s,
    .modeSimple = u"Simplifié"_s,
    .modeSimpleTip =
        u"Les mesures seulement : l'extrait analysé, les instants t₁ et t₂ sur le signal, le réticule sur le spectre. Affichage fixe, sans réglage."_s,
    .modeExpert = u"Expert"_s,
    .modeExpertTip =
        u"Toutes les options : réglages de l'analyse, dB et axe logarithmique, fondamentale et harmoniques, autocorrélation, aimantation, exports."_s,
    .modeSimpleOn = u"Mode simplifié : les mesures seulement."_s,
    .modeExpertOn = u"Mode expert : toutes les options (menu Affichage, fondamentale et harmoniques…)."_s,
    .modeSimpleName = u"Mode simplifié"_s,
    .modeExpertName = u"Mode expert"_s,
    .modeStatusTip = u"Pour changer de mode : menu Mode."_s,
    .channelMenu = u"Voie étudiée"_s,
    .menuQuit = u"Quitter"_s,
    .switchLanguage = u"Langue : français. Cliquer pour passer en anglais (English)."_s,
    .languageChanged = u"Interface en français."_s,
    .themeToDark = u"Thème clair. Cliquer pour passer au thème sombre."_s,
    .themeToLight = u"Thème sombre. Cliquer pour passer au thème clair."_s,
    .menuHelp = u"Aide"_s,
    .menuManual = u"Notice d'utilisation…"_s,
    .menuAbout = u"À propos de MeVSono…"_s,
    .menuAboutQt = u"À propos de Qt…"_s,
    .helpOpened = [](const QString &p) { return u"Notice ouverte dans le navigateur : %1"_s.arg(p); },
    .helpFailed = [](const QString &e) { return u"Impossible d'ouvrir la notice : %1"_s.arg(e); },

    .openSound = u"📂 Ouvrir un son…"_s,
    .exampleNames =
        {
            u"Diapason (La 440 Hz)"_s,
            u"Flûte (La 440 Hz)"_s,
            u"Tuyau d'orgue bouché (Do 262 Hz)"_s,
            u"Corde pincée (La 110 Hz)"_s,
            u"Signal carré (200 Hz)"_s,
            u"Battements (440 Hz et 444 Hz)"_s,
        },
    .channelNames = {u"moyenne des voies"_s, u"gauche"_s, u"droite"_s},
    .fitAll = u"🔍 Tout voir"_s,
    .zoomSelection = u"Zoom sur l'extrait"_s,
    .settingsButton = u"⚙ Réglages…"_s,
    .exportSpectrum = u"💾 Exporter le spectre (CSV)…"_s,
    .exportExtract = u"💾 Exporter l'extrait du signal (CSV)…"_s,

    .statusStart = u"Ouvrez un son (fichier audio ou vidéo)."_s,
    .unexpectedError = u"Erreur inattendue."_s,
    .fatalError =
        [](const QString &e, const QString &log) {
            return u"MeVSono a rencontré une erreur imprévue et doit se fermer.\n\n%1\n\nLes détails ont été enregistrés dans :\n%2"_s
                .arg(e, log);
        },
    .chooseSound = u"Choisissez un fichier son ou vidéo…"_s,
    .dialogOpen = u"Ouvrir un son"_s,
    .sounds = u"Sons et vidéos"_s,
    .allFiles = u"Tous les fichiers"_s,
    .loading = [](const QString &p) { return u"Lecture de « %1 »…"_s.arg(p); },
    .openFailed = [](const QString &p, const QString &e) { return u"Impossible de lire « %1 » : %2"_s.arg(p, e); },
    .unsupportedFormat =
        u"format non reconnu. Les sons WAV, MP3, OGG, FLAC et M4A et les vidéos MP4, MOV et MKV sont lus directement ; pour les autres, installez ffmpeg."_s,
    .noAudio = u"ce fichier ne contient pas de son."_s,
    .loaded = [](const QString &name, const QString &info) { return u"« %1 » : %2."_s.arg(name, info); },
    .truncated = u"seules les 30 premières minutes sont lues"_s,
    .mono = u"mono"_s,
    .stereo = u"stéréo"_s,
    .channelsN = [](size_t n) { return u"%1 voies"_s.arg(n); },
    .settingsFailed = [](const QString &e) { return u"Réglages non enregistrés : %1"_s.arg(e); },
    .dropHint = u"Déposez le fichier pour l'ouvrir"_s,
    .emptyHint =
        u"Ouvrez un son avec le bouton « 📂 Ouvrir un son… » (Ctrl+O), ou glissez un fichier son ou vidéo sur la fenêtre."_s,
    .welcomeDropHint = u"ou glissez un fichier son ou vidéo sur la fenêtre"_s,

    .tabPeriod = u"Mesure de la période"_s,
    .tabPeriodTip = u"Mesurer sur le signal la durée d'un ou de plusieurs motifs, avec les curseurs t₁ et t₂."_s,
    .tabFrequencies = u"Étude des fréquences"_s,
    .tabFrequenciesTip =
        u"Choisir sur le signal un extrait, entre A et B, et lire son spectre : la fondamentale et les harmoniques."_s,

    .signalTitle = u"Signal"_s,
    .signalHint =
        u"glisser : choisir l'extrait A–B · molette : zoom · clic droit glissé : se déplacer · double-clic : tout voir"_s,
    .signalHintPeriod =
        u"glisser : placer t₁ et t₂ · molette : zoom · clic droit glissé : se déplacer · double-clic : tout voir"_s,
    .listenSound = u"tout le son"_s,
    .listenExtract = u"l'extrait A–B"_s,
    .listenSoundOnly = u"Écouter le son"_s,
    .listenExtractOnly = u"Écouter l'extrait A–B"_s,
    .listenTip = u"Ce que joue la lecture : tout le son, ou seulement l'extrait entre A et B."_s,
    .playTip = u"Écouter (Espace). Après une pause, la lecture reprend là où elle s'était arrêtée."_s,
    .pauseTip = u"Pause (Espace)"_s,
    .stopTip = u"Arrêter : la prochaine lecture repartira du début."_s,
    .playFailed = [](const QString &e) { return u"Impossible d'écouter le son : %1"_s.arg(e); },
    .noAudioOutput = u"aucune sortie audio (haut-parleurs ou casque) n'est disponible."_s,
    .periodHeading = u"Mesure d'une période"_s,
    .periodHint =
        u"Zoomez sur le signal, puis glissez d'un point du motif au même point d'un motif suivant : vous obtenez t₁, t₂ et la durée Δt."_s,
    .axisTime = u"temps (s)"_s,
    .axisSignal = u"signal"_s,

    .spectrumTitle = u"Spectre de l'extrait"_s,
    .spectrumHint =
        u"survol : valeur sous la souris et pic le plus proche · molette : zoom · double-clic : tout voir"_s,
    .spectrumHintExpert =
        u"survol : valeur sous la souris et pic le plus proche · clic droit sur un pic : fondamentale · molette : zoom · double-clic : tout voir"_s,
    .extractHeading = u"Extrait analysé"_s,
    .reticleHint =
        u"Survolez le spectre : le réticule donne la valeur sous la souris, et celle du pic le plus proche (rond plein)."_s,
    .f0RightClick = u"Clic droit sur un pic du spectre : le prendre comme fondamentale."_s,
    .spectrumEmpty = u"Le spectre de l'extrait choisi sur le signal s'affichera ici."_s,
    .extractTooShort = u"Extrait trop court : élargissez-le entre A et B."_s,
    .axisFreq = u"fréquence (Hz)"_s,
    .axisAmplitude = u"amplitude"_s,
    .axisDb = u"amplitude (dB)"_s,

    .snapZero = u"Aimanter t₁ et t₂ aux passages par zéro"_s,
    .snapZeroTip =
        u"Un curseur lâché vient se placer sur le passage par zéro montant le plus proche : idéal pour mesurer une période avec précision."_s,
    .autocorrelation = u"Autocorrélation :"_s,
    .autocorrelationTip = u"Période trouvée par le logiciel, par autocorrélation de l'extrait A–B."_s,
    .noPeriod = u"pas de période nette"_s,
    .extractInfo = [](const QString &t, size_t n) { return u"Extrait : %1 (%2 échantillons)"_s.arg(t, frInt(n)); },
    .computing = u"Calcul du spectre…"_s,
    .fftInfo = [](size_t n, const QString &w) { return u"FFT sur %1 points, fenêtre %2"_s.arg(frInt(n), w); },
    .segmentsInfo = [](size_t k, size_t n) { return u"Moyenne de %1 segments de %2 échantillons"_s.arg(k).arg(frInt(n)); },
    .dfLabel = u"Écart entre raies Δf"_s,
    .dfTip =
        u"Écart entre deux raies calculées par la FFT : fréquence d'échantillonnage / taille de la FFT. La fréquence des pics est affinée par interpolation, bien en dessous."_s,
    .resolutionLabel = u"Finesse de l'analyse"_s,
    .resolutionTip =
        u"Fréquence d'échantillonnage / nombre d'échantillons analysés (≈ 1 / durée de l'extrait) : deux fréquences plus proches ne sont pas séparées. Pour l'améliorer, allongez l'extrait."_s,
    .fundamentalHeading = u"Fondamentale et harmoniques"_s,
    .f0Auto = u"(automatique)"_s,
    .f0Manual = u"(à la main)"_s,
    .f0None = u"Pas de fondamentale trouvée : cliquez sur un pic du spectre pour le mesurer."_s,
    .f0NoPeak = u"Aucun pic à la fréquence choisie."_s,
    .autoButton = u"Détection automatique"_s,
    .colRank = u"Rang"_s,
    .colFreq = u"f (Hz)"_s,
    .colRatio = u"f / f₁"_s,
    .colAmp = u"A (dB)"_s,
    .colRel = u"A / A₁"_s,
    .copyTable = u"📋 Copier le tableau"_s,
    .tableCopied = u"Tableau copié : collez-le dans un tableur ou un document."_s,
    .setF0 = [](const QString &f) { return u"Prendre le pic de %1 comme fondamentale"_s.arg(f); },
    .nearestPeak = u"Pic le plus proche :"_s,
    .noPeak = u"aucun pic repéré"_s,
    .setMarks = u"Repères des harmoniques"_s,
    .harmonicMarks = u"Afficher les repères n × f₁"_s,
    .harmonicMarksTip = u"Traits pointillés roses, numérotés, aux fréquences des harmoniques n × f₁ attendus."_s,

    .settingsTitle = u"Réglages"_s,
    .setAnalysisHeading = u"Analyse spectrale"_s,
    .setWindow = u"Fenêtre de pondération"_s,
    .windowNames = {u"rectangulaire"_s, u"Hann"_s, u"Hamming"_s, u"Blackman"_s, u"Blackman-Harris"_s,
                    u"à sommet plat"_s},
    .windowTips =
        {
            u"Aucune pondération : pics les plus fins, mais forts lobes secondaires (fuites) et amplitudes approximatives."_s,
            u"Le bon choix pour la plupart des sons : pics fins, fréquences et amplitudes précises."_s,
            u"Proche de Hann, avec des lobes secondaires plus bas près du pic mais qui décroissent lentement."_s,
            u"Pics un peu plus larges, lobes secondaires très bas : pour voir des harmoniques faibles."_s,
            u"Lobes secondaires extrêmement bas : pour les harmoniques très faibles à côté d'un pic fort."_s,
            u"Pics larges et plats : la mesure la plus juste des amplitudes."_s,
        },
    .setFft = u"Taille de la FFT"_s,
    .fftTip =
        u"Automatique : tout l'extrait est analysé d'un seul tenant (complété par des zéros). Une taille fixée plus petite que l'extrait découpe l'extrait en segments qui se recouvrent de moitié, et moyenne leurs spectres : le spectre est plus lisse, mais moins fin."_s,
    .fftAuto = u"automatique (tout l'extrait)"_s,
    .fftPoints = [](size_t n) { return u"%1 points"_s.arg(frInt(n)); },
    .setThreshold = u"Seuil de détection des pics"_s,
    .thresholdTip =
        u"Un pic plus faible que le plus grand pic de cette valeur (en dB) n'est pas pris en compte. −40 dB : 1 % de l'amplitude du plus grand pic."_s,
    .setHarmonics = u"Nombre d'harmoniques du tableau"_s,
    .setChannel = u"Voie étudiée (son stéréo)"_s,
    .setDisplayHeading = u"Affichage"_s,
    .setAmplitude = u"Amplitude"_s,
    .ampLinear = u"linéaire"_s,
    .ampDb = u"en décibels (dB)"_s,
    .setFreqAxis = u"Axe des fréquences"_s,
    .axisLinear = u"linéaire"_s,
    .axisLog = u"logarithmique"_s,
    .setFmax = u"Fréquence maximale affichée"_s,
    .fmaxAuto = u"jusqu'à fe / 2"_s,
    .defaults = u"Valeurs par défaut"_s,
    .close = u"Fermer"_s,

    .chooseSave = u"Choisissez où enregistrer le fichier…"_s,
    .dialogExportSpectrum = u"Exporter le spectre"_s,
    .dialogExportExtract = u"Exporter l'extrait du signal"_s,
    .csvSpectrumName = u"spectre.csv"_s,
    .csvExtractName = u"extrait.csv"_s,
    .csvFreq = u"f (Hz)"_s,
    .csvAmplitude = u"A"_s,
    .csvDb = u"A (dB)"_s,
    .csvTime = u"t (s)"_s,
    .csvSignal = u"signal"_s,
    .exported = [](const QString &p) { return u"Fichier enregistré : « %1 »."_s.arg(p); },
    .exportFailed = [](const QString &e) { return u"Échec de l'enregistrement : %1"_s.arg(e); },

    .author = u"Auteur :"_s,
    .contact = u"Contact :"_s,
    .ossHeading = u"Logiciels libres utilisés"_s,
    .ossIntro =
        u"MeVSono est écrit en C++ et s'appuie sur les logiciels libres suivants. Merci à leurs auteurs !"_s,
    .roles =
        {
            u"compilateur C++ et bibliothèque standard"_s,
            u"interface graphique, boîtes de dialogue"_s,
            u"lecture des sons et des vidéos, sortie du son (haut-parleurs, casque)"_s,
            u"décodage des formats audio et vidéo (par Qt Multimedia)"_s,
            u"bibliothèques d'exécution C (version Windows)"_s,
        },
    .licenseNote =
        u"MeVSono est un logiciel libre, distribué sous licence MIT. Qt et Qt Multimedia sont distribués sous licence LGPL version 3, FFmpeg sous licence LGPL version 2.1 ou ultérieure, ou GPL version 2 ou ultérieure selon sa compilation : le bouton « À propos de Qt » donne la version et les conditions de licence de Qt."_s,
    .settingsFile = u"Fichier de réglages :"_s,
    .settingsImposed = u"Réglages imposés (prioritaires) :"_s,
};

const Tr EN = {
    .decimal = u'.',

    .subtitle = u"spectral analysis of sounds"_s,
    .tagline = u"Spectral decomposition of sounds"_s,
    .version = [](const QString &v) { return u"Version %1"_s.arg(v); },
    .menuFile = u"File"_s,
    .menuView = u"View"_s,
    .menuMode = u"Mode"_s,
    .modeSimple = u"Simple"_s,
    .modeSimpleTip =
        u"Measurements only: the analysed extract, times t₁ and t₂ on the signal, the crosshair on the spectrum. Fixed display, no settings."_s,
    .modeExpert = u"Expert"_s,
    .modeExpertTip =
        u"All the options: analysis settings, dB and logarithmic axis, fundamental and harmonics, autocorrelation, snapping, exports."_s,
    .modeSimpleOn = u"Simple mode: measurements only."_s,
    .modeExpertOn = u"Expert mode: all the options (View menu, fundamental and harmonics…)."_s,
    .modeSimpleName = u"Simple mode"_s,
    .modeExpertName = u"Expert mode"_s,
    .modeStatusTip = u"To change the mode: Mode menu."_s,
    .channelMenu = u"Channel analysed"_s,
    .menuQuit = u"Quit"_s,
    .switchLanguage = u"Language: English. Click to switch to French (français)."_s,
    .languageChanged = u"Interface in English."_s,
    .themeToDark = u"Light theme. Click to switch to the dark theme."_s,
    .themeToLight = u"Dark theme. Click to switch to the light theme."_s,
    .menuHelp = u"Help"_s,
    .menuManual = u"User guide…"_s,
    .menuAbout = u"About MeVSono…"_s,
    .menuAboutQt = u"About Qt…"_s,
    .helpOpened = [](const QString &p) { return u"Guide opened in the browser: %1"_s.arg(p); },
    .helpFailed = [](const QString &e) { return u"Cannot open the guide: %1"_s.arg(e); },

    .openSound = u"📂 Open a sound…"_s,
    .exampleNames =
        {
            u"Tuning fork (A 440 Hz)"_s,
            u"Flute (A 440 Hz)"_s,
            u"Stopped organ pipe (C 262 Hz)"_s,
            u"Plucked string (A 110 Hz)"_s,
            u"Square wave (200 Hz)"_s,
            u"Beats (440 Hz and 444 Hz)"_s,
        },
    .channelNames = {u"average of channels"_s, u"left"_s, u"right"_s},
    .fitAll = u"🔍 Show all"_s,
    .zoomSelection = u"Zoom to the extract"_s,
    .settingsButton = u"⚙ Settings…"_s,
    .exportSpectrum = u"💾 Export the spectrum (CSV)…"_s,
    .exportExtract = u"💾 Export the signal extract (CSV)…"_s,

    .statusStart = u"Open a sound (audio or video file)."_s,
    .unexpectedError = u"Unexpected error."_s,
    .fatalError =
        [](const QString &e, const QString &log) {
            return u"MeVSono hit an unexpected error and must close.\n\n%1\n\nDetails were saved to:\n%2"_s.arg(e, log);
        },
    .chooseSound = u"Choose a sound or video file…"_s,
    .dialogOpen = u"Open a sound"_s,
    .sounds = u"Sounds and videos"_s,
    .allFiles = u"All files"_s,
    .loading = [](const QString &p) { return u"Reading “%1”…"_s.arg(p); },
    .openFailed = [](const QString &p, const QString &e) { return u"Cannot read “%1”: %2"_s.arg(p, e); },
    .unsupportedFormat =
        u"unknown format. WAV, MP3, OGG, FLAC and M4A sounds and MP4, MOV and MKV videos are read directly; for other formats, install ffmpeg."_s,
    .noAudio = u"this file contains no sound."_s,
    .loaded = [](const QString &name, const QString &info) { return u"“%1”: %2."_s.arg(name, info); },
    .truncated = u"only the first 30 minutes are read"_s,
    .mono = u"mono"_s,
    .stereo = u"stereo"_s,
    .channelsN = [](size_t n) { return u"%1 channels"_s.arg(n); },
    .settingsFailed = [](const QString &e) { return u"Settings not saved: %1"_s.arg(e); },
    .dropHint = u"Drop the file to open it"_s,
    .emptyHint =
        u"Open a sound with the “📂 Open a sound…” button (Ctrl+O), or drag a sound or video file onto the window."_s,
    .welcomeDropHint = u"or drag a sound or video file onto the window"_s,

    .tabPeriod = u"Period measurement"_s,
    .tabPeriodTip = u"Measure on the signal the duration of one or more patterns, with cursors t₁ and t₂."_s,
    .tabFrequencies = u"Frequency study"_s,
    .tabFrequenciesTip =
        u"Choose an extract of the signal, between A and B, and read its spectrum: the fundamental and the harmonics."_s,

    .signalTitle = u"Signal"_s,
    .signalHint = u"drag: choose the A–B extract · wheel: zoom · right-drag: pan · double-click: show all"_s,
    .signalHintPeriod = u"drag: place t₁ and t₂ · wheel: zoom · right-drag: pan · double-click: show all"_s,
    .listenSound = u"whole sound"_s,
    .listenExtract = u"A–B extract"_s,
    .listenSoundOnly = u"Listen to the sound"_s,
    .listenExtractOnly = u"Listen to the A–B extract"_s,
    .listenTip = u"What is played: the whole sound, or only the extract between A and B."_s,
    .playTip = u"Listen (Space). After a pause, playback resumes where it stopped."_s,
    .pauseTip = u"Pause (Space)"_s,
    .stopTip = u"Stop: the next playback starts from the beginning."_s,
    .playFailed = [](const QString &e) { return u"Cannot play the sound: %1"_s.arg(e); },
    .noAudioOutput = u"no audio output (speakers or headphones) is available."_s,
    .periodHeading = u"Measuring a period"_s,
    .periodHint =
        u"Zoom in on the signal, then drag from a point of one pattern to the same point of a later pattern: you get t₁, t₂ and the duration Δt."_s,
    .axisTime = u"time (s)"_s,
    .axisSignal = u"signal"_s,

    .spectrumTitle = u"Spectrum of the extract"_s,
    .spectrumHint = u"hover: value under the mouse and nearest peak · wheel: zoom · double-click: show all"_s,
    .spectrumHintExpert =
        u"hover: value under the mouse and nearest peak · right-click a peak: fundamental · wheel: zoom · double-click: show all"_s,
    .extractHeading = u"Analysed extract"_s,
    .reticleHint =
        u"Hover over the spectrum: the crosshair gives the value under the mouse, and that of the nearest peak (filled dot)."_s,
    .f0RightClick = u"Right-click a peak of the spectrum to use it as the fundamental."_s,
    .spectrumEmpty = u"The spectrum of the extract chosen on the signal will appear here."_s,
    .extractTooShort = u"Extract too short: widen it between A and B."_s,
    .axisFreq = u"frequency (Hz)"_s,
    .axisAmplitude = u"amplitude"_s,
    .axisDb = u"amplitude (dB)"_s,

    .snapZero = u"Snap t₁ and t₂ to zero crossings"_s,
    .snapZeroTip = u"A released cursor moves to the nearest rising zero crossing: ideal for measuring a period precisely."_s,
    .autocorrelation = u"Autocorrelation:"_s,
    .autocorrelationTip = u"Period found by the software, by autocorrelation of the A–B extract."_s,
    .noPeriod = u"no clear period"_s,
    .extractInfo = [](const QString &t, size_t n) { return u"Extract: %1 (%2 samples)"_s.arg(t, enInt(n)); },
    .computing = u"Computing the spectrum…"_s,
    .fftInfo = [](size_t n, const QString &w) { return u"%1-point FFT, %2 window"_s.arg(enInt(n), w); },
    .segmentsInfo = [](size_t k, size_t n) { return u"Average of %1 segments of %2 samples"_s.arg(k).arg(enInt(n)); },
    .dfLabel = u"Bin spacing Δf"_s,
    .dfTip =
        u"Spacing between two lines computed by the FFT: sampling rate / FFT size. Peak frequencies are refined by interpolation, to much better than that."_s,
    .resolutionLabel = u"Analysis resolution"_s,
    .resolutionTip =
        u"Sampling rate / number of samples analysed (≈ 1 / duration of the extract): two closer frequencies are not separated. To improve it, lengthen the extract."_s,
    .fundamentalHeading = u"Fundamental and harmonics"_s,
    .f0Auto = u"(automatic)"_s,
    .f0Manual = u"(manual)"_s,
    .f0None = u"No fundamental found: click a peak of the spectrum to measure it."_s,
    .f0NoPeak = u"No peak at the chosen frequency."_s,
    .autoButton = u"Automatic detection"_s,
    .colRank = u"Rank"_s,
    .colFreq = u"f (Hz)"_s,
    .colRatio = u"f / f₁"_s,
    .colAmp = u"A (dB)"_s,
    .colRel = u"A / A₁"_s,
    .copyTable = u"📋 Copy the table"_s,
    .tableCopied = u"Table copied: paste it into a spreadsheet or a document."_s,
    .setF0 = [](const QString &f) { return u"Use the %1 peak as the fundamental"_s.arg(f); },
    .nearestPeak = u"Nearest peak:"_s,
    .noPeak = u"no peak detected"_s,
    .setMarks = u"Harmonic marks"_s,
    .harmonicMarks = u"Show the n × f₁ marks"_s,
    .harmonicMarksTip = u"Numbered pink dotted lines at the frequencies n × f₁ of the expected harmonics."_s,

    .settingsTitle = u"Settings"_s,
    .setAnalysisHeading = u"Spectral analysis"_s,
    .setWindow = u"Window function"_s,
    .windowNames = {u"rectangular"_s, u"Hann"_s, u"Hamming"_s, u"Blackman"_s, u"Blackman-Harris"_s, u"flat top"_s},
    .windowTips =
        {
            u"No weighting: the narrowest peaks, but strong side lobes (leakage) and approximate amplitudes."_s,
            u"The right choice for most sounds: narrow peaks, precise frequencies and amplitudes."_s,
            u"Close to Hann, with lower side lobes near the peak, but they decay slowly."_s,
            u"Slightly wider peaks, very low side lobes: to see weak harmonics."_s,
            u"Extremely low side lobes: for very weak harmonics next to a strong peak."_s,
            u"Wide, flat peaks: the most accurate amplitude measurement."_s,
        },
    .setFft = u"FFT size"_s,
    .fftTip =
        u"Automatic: the whole extract is analysed in one go (padded with zeros). A fixed size smaller than the extract splits it into half-overlapping segments and averages their spectra: the spectrum is smoother, but less fine."_s,
    .fftAuto = u"automatic (whole extract)"_s,
    .fftPoints = [](size_t n) { return u"%1 points"_s.arg(enInt(n)); },
    .setThreshold = u"Peak detection threshold"_s,
    .thresholdTip =
        u"A peak weaker than the largest one by more than this amount (in dB) is ignored. −40 dB: 1 % of the amplitude of the largest peak."_s,
    .setHarmonics = u"Number of harmonics in the table"_s,
    .setChannel = u"Channel analysed (stereo sound)"_s,
    .setDisplayHeading = u"Display"_s,
    .setAmplitude = u"Amplitude"_s,
    .ampLinear = u"linear"_s,
    .ampDb = u"in decibels (dB)"_s,
    .setFreqAxis = u"Frequency axis"_s,
    .axisLinear = u"linear"_s,
    .axisLog = u"logarithmic"_s,
    .setFmax = u"Highest frequency shown"_s,
    .fmaxAuto = u"up to fs / 2"_s,
    .defaults = u"Default values"_s,
    .close = u"Close"_s,

    .chooseSave = u"Choose where to save the file…"_s,
    .dialogExportSpectrum = u"Export the spectrum"_s,
    .dialogExportExtract = u"Export the signal extract"_s,
    .csvSpectrumName = u"spectrum.csv"_s,
    .csvExtractName = u"extract.csv"_s,
    .csvFreq = u"f (Hz)"_s,
    .csvAmplitude = u"A"_s,
    .csvDb = u"A (dB)"_s,
    .csvTime = u"t (s)"_s,
    .csvSignal = u"signal"_s,
    .exported = [](const QString &p) { return u"File saved: “%1”."_s.arg(p); },
    .exportFailed = [](const QString &e) { return u"Save failed: %1"_s.arg(e); },

    .author = u"Author:"_s,
    .contact = u"Contact:"_s,
    .ossHeading = u"Open-source software used"_s,
    .ossIntro =
        u"MeVSono is written in C++ and relies on the following open-source software. Many thanks to their authors!"_s,
    .roles =
        {
            u"C++ compiler and standard library"_s,
            u"user interface, dialogs"_s,
            u"sound and video reading, sound output (speakers, headphones)"_s,
            u"audio and video format decoding (through Qt Multimedia)"_s,
            u"C runtime libraries (Windows version)"_s,
        },
    .licenseNote =
        u"MeVSono is free software, released under the MIT licence. Qt and Qt Multimedia are released under the LGPL version 3, FFmpeg under the LGPL version 2.1 or later, or the GPL version 2 or later depending on its build: the “About Qt” button gives the version and licence terms of Qt."_s,
    .settingsFile = u"Settings file:"_s,
    .settingsImposed = u"Enforced settings (take priority):"_s,
};

} // namespace

const Tr &texts(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return FR;
    case Lang::En: return EN;
    }
    return EN;
}
