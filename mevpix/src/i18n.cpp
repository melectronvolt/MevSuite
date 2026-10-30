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

const Tr FR = {
    .decimal = u',',
    .subtitle = u"Mesurer des longueurs"_s,
    .menuFile = u"Fichier"_s,
    .menuQuit = u"Quitter"_s,
    .switchLanguage = u"Langue : français. Cliquer pour passer en anglais (English)."_s,
    .languageChanged = u"Interface en français."_s,
    .themeToDark = u"Thème clair. Cliquer pour passer au thème sombre."_s,
    .themeToLight = u"Thème sombre. Cliquer pour passer au thème clair."_s,
    .menuHelp = u"Aide"_s,
    .menuAbout = u"À propos de MeVPix…"_s,
    .menuAboutQt = u"À propos de Qt…"_s,
    .menuManual = u"Notice d'utilisation…"_s,
    .helpOpened = [](const QString &p) { return u"Notice ouverte dans le navigateur (%1)."_s.arg(p); },
    .helpFailed = [](const QString &e) { return u"Impossible d'ouvrir la notice : %1"_s.arg(e); },
    .openImage = u"📂 Ouvrir une image…"_s,
    .toolCalibrate = u"📏 Étalonner"_s,
    .toolMeasure = u"📐 Mesurer"_s,
    .toolProfile = u"📈 Profil d'intensité"_s,
    .fitWindow = u"🔍 Ajuster à la fenêtre"_s,
    .scale = [](const QString &s) { return u"Échelle : 1 px = %1"_s.arg(s); },
    .notCalibrated = u"⚠ Non étalonné"_s,
    .statusStart = u"Ouvrez une image pour commencer."_s,
    .unexpectedError = u"Erreur inattendue pendant l'opération."_s,
    .fatalError =
        [](const QString &e, const QString &log) {
            if (log.isEmpty())
                return u"MeVPix a rencontré une erreur imprévue et doit se fermer.\n\n%1\n\nLes détails n'ont pas pu être enregistrés."_s
                    .arg(e);
            return u"MeVPix a rencontré une erreur imprévue et doit se fermer.\n\n%1\n\nLes détails ont été enregistrés dans :\n%2"_s
                .arg(e, log);
        },
    .chooseImage = u"Choisissez une image…"_s,
    .dialogOpen = u"Ouvrir une image"_s,
    .images = u"Images"_s,
    .allFiles = u"Tous les fichiers"_s,
    .loading = [](const QString &p) { return u"Chargement de « %1 »…"_s.arg(p); },
    .openFailed = [](const QString &p, const QString &e) { return u"Impossible d'ouvrir « %1 » : %2"_s.arg(p, e); },
    .unsupportedFormat =
        u"format d'image non pris en charge par cette installation. Convertissez l'image en PNG ou en JPEG (les photos HEIC "
        u"d'iPhone aussi)."_s,
    .imageLoaded =
        [](const QString &n, int w, int h) {
            return u"Image « %1 » (%2 × %3 px). Commencez par tracer l'étalon."_s.arg(n, QString::number(w), QString::number(h));
        },
    .settingsFailed = [](const QString &e) { return u"Impossible d'enregistrer les réglages : %1"_s.arg(e); },
    .folderFailed = [](const QString &d) { return u"impossible de créer le dossier « %1 »"_s.arg(d); },
    .zoomInfo = [](const QString &z) { return u"Zoom : %1 %"_s.arg(z); },
    .zoomTip = u"Molette : zoomer. Bouton « Ajuster à la fenêtre » : revoir toute l'image."_s,
    .replaceImage =
        [](const QString &n) {
            return u"Ouvrir « %1 » ?\nL'étalonnage, les mesures et la coupe de l'image actuelle seront effacés."_s.arg(n);
        },
    .openAnyway = u"Ouvrir la nouvelle image"_s,
    .cancel = u"Annuler"_s,
    .quitWithMeasures = u"Quitter MeVPix ?\nLes mesures et la coupe seront perdues. Pour les garder : « 📋 Copier le tableau », "
                        u"ou « 💾 Exporter le profil (CSV)… »."_s,
    .quitAnyway = u"Quitter"_s,
    .welcomeDropHint = u"ou glissez une image sur la fenêtre"_s,
    .dropHere = u"Déposez l'image pour l'ouvrir"_s,
    .calibTag = [](const QString &v) { return u"étalon : %1"_s.arg(v); },
    .measureTag = [](int i, const QString &v) { return u"M%1 : %2"_s.arg(QString::number(i), v); },
    .calibHeading = u"Étalonnage"_s,
    .calibStep1 =
        u"1. Tracez un segment sur un objet de longueur connue (par exemple 10 carreaux du papier millimétré)."_s,
    .calibStep2 = u"2. Saisissez sa longueur réelle, puis validez."_s,
    .calibDrawn =
        [](const QString &l) { return u"Étalon tracé : %1 px. Saisissez sa longueur réelle puis validez."_s.arg(l); },
    .segmentDrawn = [](const QString &l) { return u"Segment tracé : %1 px"_s.arg(l); },
    .noSegment = u"Aucun segment tracé."_s,
    .realLength = u"Longueur réelle :"_s,
    .example = u"ex. 10"_s,
    .invalidLength = u"Longueur invalide."_s,
    .validate = u"✔ Valider l'étalonnage"_s,
    .calibValidated =
        [](const QString &s) { return u"Étalonnage validé : 1 px = %1. Outil Mesurer activé."_s.arg(s); },
    .currentScale =
        [](const QString &a, const QString &b, const QString &u) {
            return u"Échelle actuelle : 1 px = %1 %3\n(soit %2 px pour 1 %3)"_s.arg(a, b, u);
        },
    .calibChanged = u"⚠ L'étalon, la longueur ou l'unité ont changé depuis la validation : validez à nouveau."_s,
    .measuresHeading = u"Mesures"_s,
    .uncalibratedPx = u"⚠ Non étalonné : les longueurs sont en pixels."_s,
    .measureHint = u"Tracez des segments sur l'image pour mesurer des longueurs."_s,
    .deleteMeasure = u"Supprimer cette mesure"_s,
    .noMeasure = u"Aucune mesure."_s,
    .clearAll = u"Tout effacer"_s,
    .copyMeasures = u"📋 Copier le tableau"_s,
    .measuresCopied = u"Mesures copiées : collez-les dans un tableur ou un document (Ctrl+V)."_s,
    .colMeasure = u"Mesure"_s,
    .colLength = [](const QString &u) { return u"Longueur (%1)"_s.arg(u); },
    .profileHeading = u"Profil d'intensité"_s,
    .profileHint = u"Tracez la ligne de coupe sur l'image, le long de la figure."_s,
    .cutDrawn = u"Ligne de coupe tracée : déplacez les curseurs A et B sur la coupe ou sur le graphique."_s,
    .cutPrompt = u"Tracez une ligne de coupe sur l'image (clic gauche maintenu)."_s,
    .channelLabel = u"Canal :"_s,
    .channels = {u"Luminance (gris)"_s, u"Rouge"_s, u"Vert"_s, u"Bleu"_s},
    .thickness = u"Épaisseur :"_s,
    .thicknessTip = u"Moyenne perpendiculairement à la coupe, pour atténuer le quadrillage et le bruit."_s,
    .cursors = u"Curseurs"_s,
    .cursorsHint = u"Glissez A et B le long de la coupe sur l'image, ou cliquez/glissez sur le graphique."_s,
    .saturated =
        [](const QString &p) {
            return u"⚠ %1 % de la coupe est saturée (valeur 254 ou 255) : l'intensité réelle y est peut-être plus grande que "
                   u"celle affichée."_s
                .arg(p);
        },
    .axisX = [](const QString &u) { return u"position le long de la coupe (%1)"_s.arg(u); },
    .axisY = u"intensité"_s,
    .exportCsv = u"💾 Exporter le profil (CSV)…"_s,
    .chooseSave = u"Choisissez où enregistrer le profil…"_s,
    .replaceFile = [](const QString &n) { return u"« %1 » existe déjà. Le remplacer ?"_s.arg(n); },
    .replaceButton = u"Remplacer"_s,
    .dialogExport = u"Exporter le profil"_s,
    .csvName = u"profil.csv"_s,
    .exported = [](const QString &p) { return u"Profil exporté dans « %1 »."_s.arg(p); },
    .exportFailed = [](const QString &e) { return u"Échec de l'export : %1"_s.arg(e); },
    .tagline = u"Mesurer des longueurs"_s,
    .version = [](const QString &v) { return u"Version %1"_s.arg(v); },
    .author = u"Auteur :"_s,
    .contact = u"Contact :"_s,
    .ossHeading = u"Logiciels libres utilisés"_s,
    .ossIntro = u"MeVPix est écrit en C++ et s'appuie sur les logiciels libres suivants. Merci à leurs auteurs !"_s,
    .roles =
        {
            u"compilateur et bibliothèque standard C++"_s,
            u"interface graphique, lecture des images, boîtes de dialogue"_s,
            u"lecture des images TIFF et WebP"_s,
            u"bibliothèques d'exécution C (version Windows)"_s,
            u"émoji des boutons (version AppImage)"_s,
        },
    .licenseNote =
        u"MeVPix est un logiciel libre, distribué sous licence MIT. Qt est distribué sous licence LGPL version 3 : le bouton « À propos de Qt » en donne la version et les conditions."_s,
    .settingsFile = u"Fichier de réglages :"_s,
    .settingsImposed = u"Réglages imposés (prioritaires) :"_s,
    .close = u"Fermer"_s,
};

const Tr EN = {
    .decimal = u'.',
    .subtitle = u"Measure lengths"_s,
    .menuFile = u"File"_s,
    .menuQuit = u"Quit"_s,
    .switchLanguage = u"Language: English. Click to switch to French (français)."_s,
    .languageChanged = u"Interface in English."_s,
    .themeToDark = u"Light theme. Click to switch to the dark theme."_s,
    .themeToLight = u"Dark theme. Click to switch to the light theme."_s,
    .menuHelp = u"Help"_s,
    .menuAbout = u"About MeVPix…"_s,
    .menuAboutQt = u"About Qt…"_s,
    .menuManual = u"User guide…"_s,
    .helpOpened = [](const QString &p) { return u"Guide opened in the browser (%1)."_s.arg(p); },
    .helpFailed = [](const QString &e) { return u"Cannot open the guide: %1"_s.arg(e); },
    .openImage = u"📂 Open an image…"_s,
    .toolCalibrate = u"📏 Calibrate"_s,
    .toolMeasure = u"📐 Measure"_s,
    .toolProfile = u"📈 Intensity profile"_s,
    .fitWindow = u"🔍 Fit to window"_s,
    .scale = [](const QString &s) { return u"Scale: 1 px = %1"_s.arg(s); },
    .notCalibrated = u"⚠ Not calibrated"_s,
    .statusStart = u"Open an image to get started."_s,
    .unexpectedError = u"Unexpected error during the operation."_s,
    .fatalError =
        [](const QString &e, const QString &log) {
            if (log.isEmpty())
                return u"MeVPix hit an unexpected error and must close.\n\n%1\n\nThe details could not be saved."_s.arg(e);
            return u"MeVPix hit an unexpected error and must close.\n\n%1\n\nDetails were saved to:\n%2"_s.arg(e, log);
        },
    .chooseImage = u"Choose an image…"_s,
    .dialogOpen = u"Open an image"_s,
    .images = u"Images"_s,
    .allFiles = u"All files"_s,
    .loading = [](const QString &p) { return u"Loading “%1”…"_s.arg(p); },
    .openFailed = [](const QString &p, const QString &e) { return u"Cannot open “%1”: %2"_s.arg(p, e); },
    .unsupportedFormat =
        u"image format not supported by this installation. Convert the image to PNG or JPEG (iPhone HEIC photos too)."_s,
    .imageLoaded =
        [](const QString &n, int w, int h) {
            return u"Image “%1” (%2 × %3 px). Start by drawing the reference."_s.arg(n, QString::number(w), QString::number(h));
        },
    .settingsFailed = [](const QString &e) { return u"Cannot save settings: %1"_s.arg(e); },
    .folderFailed = [](const QString &d) { return u"cannot create the folder “%1”"_s.arg(d); },
    .zoomInfo = [](const QString &z) { return u"Zoom: %1%"_s.arg(z); },
    .zoomTip = u"Mouse wheel: zoom. “Fit to window” button: see the whole image again."_s,
    .replaceImage =
        [](const QString &n) {
            return u"Open “%1”?\nThe calibration, measurements and profile line of the current image will be cleared."_s.arg(n);
        },
    .openAnyway = u"Open the new image"_s,
    .cancel = u"Cancel"_s,
    .quitWithMeasures = u"Quit MeVPix?\nThe measurements and the profile line will be lost. To keep them: “📋 Copy the table”, "
                        u"or “💾 Export the profile (CSV)…”."_s,
    .quitAnyway = u"Quit"_s,
    .welcomeDropHint = u"or drag an image onto the window"_s,
    .dropHere = u"Drop the image to open it"_s,
    .calibTag = [](const QString &v) { return u"reference: %1"_s.arg(v); },
    .measureTag = [](int i, const QString &v) { return u"M%1: %2"_s.arg(QString::number(i), v); },
    .calibHeading = u"Calibration"_s,
    .calibStep1 = u"1. Draw a segment over an object of known length (for example 10 squares of graph paper)."_s,
    .calibStep2 = u"2. Enter its real length, then confirm."_s,
    .calibDrawn = [](const QString &l) { return u"Reference drawn: %1 px. Enter its real length, then confirm."_s.arg(l); },
    .segmentDrawn = [](const QString &l) { return u"Segment drawn: %1 px"_s.arg(l); },
    .noSegment = u"No segment drawn."_s,
    .realLength = u"Real length:"_s,
    .example = u"e.g. 10"_s,
    .invalidLength = u"Invalid length."_s,
    .validate = u"✔ Confirm calibration"_s,
    .calibValidated = [](const QString &s) { return u"Calibration confirmed: 1 px = %1. Measure tool selected."_s.arg(s); },
    .currentScale =
        [](const QString &a, const QString &b, const QString &u) {
            return u"Current scale: 1 px = %1 %3\n(i.e. %2 px for 1 %3)"_s.arg(a, b, u);
        },
    .calibChanged = u"⚠ The reference, the length or the unit has changed since the calibration was confirmed: confirm it again."_s,
    .measuresHeading = u"Measurements"_s,
    .uncalibratedPx = u"⚠ Not calibrated: lengths are in pixels."_s,
    .measureHint = u"Draw segments on the image to measure lengths."_s,
    .deleteMeasure = u"Delete this measurement"_s,
    .noMeasure = u"No measurements."_s,
    .clearAll = u"Clear all"_s,
    .copyMeasures = u"📋 Copy the table"_s,
    .measuresCopied = u"Measurements copied: paste them into a spreadsheet or document (Ctrl+V)."_s,
    .colMeasure = u"Measurement"_s,
    .colLength = [](const QString &u) { return u"Length (%1)"_s.arg(u); },
    .profileHeading = u"Intensity profile"_s,
    .profileHint = u"Draw the profile line on the image, along the pattern."_s,
    .cutDrawn = u"Profile line drawn: move cursors A and B along the line or on the graph."_s,
    .cutPrompt = u"Draw a profile line on the image (left-click and drag)."_s,
    .channelLabel = u"Channel:"_s,
    .channels = {u"Luminance (grey)"_s, u"Red"_s, u"Green"_s, u"Blue"_s},
    .thickness = u"Width:"_s,
    .thicknessTip = u"Averages across the line, to smooth out the paper grid and the noise."_s,
    .cursors = u"Cursors"_s,
    .cursorsHint = u"Drag A and B along the line on the image, or click/drag on the graph."_s,
    .saturated =
        [](const QString &p) {
            return u"⚠ %1% of the line is saturated (value 254 or 255): the real intensity there may be higher than shown."_s.arg(p);
        },
    .axisX = [](const QString &u) { return u"position along the line (%1)"_s.arg(u); },
    .axisY = u"intensity"_s,
    .exportCsv = u"💾 Export the profile (CSV)…"_s,
    .chooseSave = u"Choose where to save the profile…"_s,
    .replaceFile = [](const QString &n) { return u"“%1” already exists. Replace it?"_s.arg(n); },
    .replaceButton = u"Replace"_s,
    .dialogExport = u"Export the profile"_s,
    .csvName = u"profile.csv"_s,
    .exported = [](const QString &p) { return u"Profile exported to “%1”."_s.arg(p); },
    .exportFailed = [](const QString &e) { return u"Export failed: %1"_s.arg(e); },
    .tagline = u"Measure lengths"_s,
    .version = [](const QString &v) { return u"Version %1"_s.arg(v); },
    .author = u"Author:"_s,
    .contact = u"Contact:"_s,
    .ossHeading = u"Open-source software used"_s,
    .ossIntro = u"MeVPix is written in C++ and relies on the following open-source software. Many thanks to their authors!"_s,
    .roles =
        {
            u"C++ compiler and standard library"_s,
            u"user interface, image decoding, dialogs"_s,
            u"TIFF and WebP image decoding"_s,
            u"C runtime libraries (Windows version)"_s,
            u"button emoji (AppImage version)"_s,
        },
    .licenseNote =
        u"MeVPix is free software, released under the MIT licence. Qt is released under the LGPL version 3: the “About Qt” button gives its version and licence terms."_s,
    .settingsFile = u"Settings file:"_s,
    .settingsImposed = u"Enforced settings (take priority):"_s,
    .close = u"Close"_s,
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
