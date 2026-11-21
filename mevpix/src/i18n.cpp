#include "i18n.h"

#include <QLocale>

using namespace Qt::StringLiterals;

QString code_langue(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return u"fr"_s;
    case Lang::En: return u"en"_s;
    }
    return {};
}

std::optional<Lang> langue_du_code(const QString &code)
{
    QString primary;
    for (QChar c : code) {
        if (c.unicode() > 127 || !c.isLetter())
            break;
        primary += c;
    }
    for (Lang lang : LANGUES) {
        if (primary.compare(code_langue(lang), Qt::CaseInsensitive) == 0)
            return lang;
    }
    return std::nullopt;
}

QString nom_langue(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return u"Français"_s;
    case Lang::En: return u"English"_s;
    }
    return {};
}

std::optional<Lang> premier_traduit(const QStringList &codes)
{
    for (const QString &code : codes) {
        if (const auto lang = langue_du_code(code))
            return lang;
    }
    return std::nullopt;
}

Lang resout_langue(std::optional<Lang> choice)
{
    // Langues d'affichage préférées : variables LANGUAGE, LC_ALL, LC_MESSAGES, LANG sous Linux,
    // langue de l'interface de Windows.
    if (choice)
        return *choice;
    return premier_traduit(QLocale::system().uiLanguages()).value_or(Lang::En);
}

namespace {

const Textes FR = {
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
    .openImage = u"Ouvrir une image…"_s,
    .toolCalibrate = u"Étalonner"_s,
    .toolMeasure = u"Mesurer"_s,
    .toolProfile = u"Profil d'intensité"_s,
    .zoomInImage = u"Agrandir l’image"_s,
    .zoomOutImage = u"Réduire l’image"_s,
    .fitWindow = u"Ajuster à la fenêtre"_s,
    .panImage = {u"Déplacer l’image vers la gauche"_s, u"Déplacer l’image vers la droite"_s,
                 u"Déplacer l’image vers le haut"_s, u"Déplacer l’image vers le bas"_s},
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
    .dialogOpen = u"Ouvrir une image ou une séance MeVPix"_s,
    .images = u"Images et séances MeVPix"_s,
    .allFiles = u"Tous les fichiers"_s,
    .loading = [](const QString &p) { return u"Chargement de « %1 »…"_s.arg(p); },
    .openFailed = [](const QString &p, const QString &e) { return u"Impossible d'ouvrir « %1 » : %2"_s.arg(p, e); },
    .unsupportedFormat = u"Format d’image inconnu. La notice (F1) liste les formats disponibles sur ce poste."_s,
    .incompleteJpeg = u"JPEG incomplet ou endommagé. Recopiez la photo depuis l’original."_s,
    .imageLoaded =
        [](const QString &n, int w, int h) {
            return u"Image « %1 » (%2 × %3 px)."_s.arg(n, QString::number(w), QString::number(h));
        },
    .settingsFailed = [](const QString &e) { return u"Impossible d'enregistrer les réglages : %1"_s.arg(e); },
    .settingsBusy = u"Une autre fenêtre écrit les réglages."_s,
    .folderFailed = [](const QString &d) { return u"impossible de créer le dossier « %1 »"_s.arg(d); },
    .zoomInfo = [](const QString &z) { return u"Zoom : %1 %"_s.arg(z); },
    .zoomTip = u"Les loupes + et − changent le zoom et les flèches déplacent l’image. « Ajuster à la fenêtre » recentre l’image entière."_s,
    .replaceImage =
        [](const QString &n) {
            return u"Enregistrer les modifications de la séance avant d’ouvrir « %1 » ?"_s.arg(n);
        },
    .cancel = u"Annuler"_s,
    .quitWithMeasures = u"Enregistrer les modifications de la séance avant de quitter ?"_s,
    .welcomeDropHint = u"ou glissez une image sur la fenêtre"_s,
    .dropHere = u"Déposez l'image pour l'ouvrir"_s,
    .calibTag = [](const QString &v) { return u"étalon : %1"_s.arg(v); },
    .measureTag = [](int i, const QString &v) { return u"M%1 : %2"_s.arg(QString::number(i), v); },
    .calibHeading = u"Étalonnage"_s,
    .calibStep1 =
        u"1. Tracez un segment sur un objet de longueur connue."_s,
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
    .channels = {u"Gris pondéré (RVB)"_s, u"Rouge"_s, u"Vert"_s, u"Bleu"_s},
    .thickness = u"Épaisseur :"_s,
    .thicknessTip = u"Largeur de la bande utilisée pour calculer l’intensité moyenne."_s,
    .cursors = u"Curseurs"_s,
    .cursorsHint = u"Déplacez A et B sur la coupe ou le graphique. Pour avancer d’un échantillon, sélectionnez A ou B puis utilisez ← ou →."_s,
    .alerteSaturation =
        [](const QString &p) {
            return u"⚠ Pour %1 % des échantillons, au moins un pixel utilisé atteint 254 ou 255 dans une composante du canal choisi."_s.arg(p);
        },
    .axisX = [](const QString &u) { return u"position le long de la coupe (%1)"_s.arg(u); },
    .axisY = u"intensité (0–255)"_s,
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
    .website = u"Site web :"_s,
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
    .saveSession = u"Enregistrer la séance"_s,
    .annotatedImage = u"Image annotée…"_s,
    .invalidSession = u"Séance invalide"_s,
    .regularFileRequired = u"Sélectionnez un fichier ordinaire."_s,
    .referenceSegment = u"Étalon"_s,
    .allMeasurements = u"Toutes les mesures"_s,
    .copyAnnotation = u"Copier"_s,
    .saveSessionAs = u"Enregistrer la séance sous…"_s,
    .discard = u"Abandonner les modifications"_s,
    .pendingScale = u"L’étalonnage a changé. Échelle validée : 1 px = %1."_s,
    .newScale = u"Nouvelle échelle : 1 px = %1."_s,
    .useValidatedScale = u"Garder l’échelle validée"_s,
    .useNewScale = u"Valider la nouvelle échelle"_s,
    .clippedBand = u"Au bord de l’image, la moyenne utilise uniquement la partie visible de la bande."_s,
    .cutAndCursors = u"Coupe et curseurs A/B sur l’image"_s,
    .includeGraph = u"Joindre le graphique et Δx"_s,
    .annotationCopied = u"Image annotée copiée."_s,
    .savedFile = u"Fichier enregistré dans « %1 »."_s,
    .savingFile = u"Enregistrement de « %1 » en cours…"_s,
    .saveFailed = u"Échec de l’enregistrement de « %1 » : %2"_s,
    .depthConverted = u"Les intensités sont converties sur 8 bits par composante (0 à 255)."_s,
    .cutTooShort = u"Éloignez les extrémités de la coupe d’au moins un pixel pour lire le profil."_s,
    .sessionChanged = u"Le fichier « %1 » a été modifié ou déplacé. Vous pouvez enregistrer vos mesures dans une copie."_s,
    .sessionBusy = u"Un autre enregistrement de « %1 » est en cours. Vous pouvez réessayer dans un instant ou enregistrer une copie."_s,
    .saveCopy = u"Enregistrer une copie…"_s,
    .lockFailed = u"Impossible de créer le verrou d’écriture dans ce dossier."_s,
    .sessionHeaderTooLarge = u"Les tracés de la séance dépassent la taille maximale de 16 Mio."_s,
};

const Textes EN = {
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
    .openImage = u"Open an image…"_s,
    .toolCalibrate = u"Calibrate"_s,
    .toolMeasure = u"Measure"_s,
    .toolProfile = u"Intensity profile"_s,
    .zoomInImage = u"Zoom in on the image"_s,
    .zoomOutImage = u"Zoom out of the image"_s,
    .fitWindow = u"Fit to window"_s,
    .panImage = {u"Move the image left"_s, u"Move the image right"_s,
                 u"Move the image up"_s, u"Move the image down"_s},
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
    .dialogOpen = u"Open an image or a MeVPix session"_s,
    .images = u"Images and MeVPix sessions"_s,
    .allFiles = u"All files"_s,
    .loading = [](const QString &p) { return u"Loading “%1”…"_s.arg(p); },
    .openFailed = [](const QString &p, const QString &e) { return u"Cannot open “%1”: %2"_s.arg(p, e); },
    .unsupportedFormat = u"Unknown image format. The guide (F1) lists the formats available on this computer."_s,
    .incompleteJpeg = u"Incomplete or damaged JPEG. Copy the photo again from the original."_s,
    .imageLoaded =
        [](const QString &n, int w, int h) {
            return u"Image “%1” (%2 × %3 px)."_s.arg(n, QString::number(w), QString::number(h));
        },
    .settingsFailed = [](const QString &e) { return u"Cannot save settings: %1"_s.arg(e); },
    .settingsBusy = u"Another window is saving the settings."_s,
    .folderFailed = [](const QString &d) { return u"cannot create the folder “%1”"_s.arg(d); },
    .zoomInfo = [](const QString &z) { return u"Zoom: %1%"_s.arg(z); },
    .zoomTip = u"Use the + and − magnifiers to zoom and the arrows to move the image. “Fit to window” centres the whole image."_s,
    .replaceImage =
        [](const QString &n) {
            return u"Save changes to the current session before opening “%1”?"_s.arg(n);
        },
    .cancel = u"Cancel"_s,
    .quitWithMeasures = u"Save changes to the session before quitting?"_s,
    .welcomeDropHint = u"or drag an image onto the window"_s,
    .dropHere = u"Drop the image to open it"_s,
    .calibTag = [](const QString &v) { return u"reference: %1"_s.arg(v); },
    .measureTag = [](int i, const QString &v) { return u"M%1: %2"_s.arg(QString::number(i), v); },
    .calibHeading = u"Calibration"_s,
    .calibStep1 = u"1. Draw a segment over an object of known length."_s,
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
    .channels = {u"Weighted grey (RGB)"_s, u"Red"_s, u"Green"_s, u"Blue"_s},
    .thickness = u"Width:"_s,
    .thicknessTip = u"Width of the band used to calculate the average intensity."_s,
    .cursors = u"Cursors"_s,
    .cursorsHint = u"Drag A and B on the line or graph. Select A or B, then press ← or → to move one sample."_s,
    .alerteSaturation =
        [](const QString &p) {
            return u"⚠ For %1% of the samples, at least one contributing pixel reaches 254 or 255 in a component of the selected channel."_s.arg(p);
        },
    .axisX = [](const QString &u) { return u"position along the line (%1)"_s.arg(u); },
    .axisY = u"intensity (0–255)"_s,
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
    .website = u"Website:"_s,
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
    .saveSession = u"Save session"_s,
    .annotatedImage = u"Annotated image…"_s,
    .invalidSession = u"Invalid session"_s,
    .regularFileRequired = u"Choose a regular file."_s,
    .referenceSegment = u"Reference"_s,
    .allMeasurements = u"All measurements"_s,
    .copyAnnotation = u"Copy"_s,
    .saveSessionAs = u"Save session as…"_s,
    .discard = u"Discard changes"_s,
    .pendingScale = u"The calibration has changed. Confirmed scale: 1 px = %1."_s,
    .newScale = u"New scale: 1 px = %1."_s,
    .useValidatedScale = u"Keep confirmed scale"_s,
    .useNewScale = u"Confirm new scale"_s,
    .clippedBand = u"At the image edge, the average uses only the visible part of the band."_s,
    .cutAndCursors = u"Profile line and A/B cursors on the image"_s,
    .includeGraph = u"Include graph and Δx"_s,
    .annotationCopied = u"Annotated image copied."_s,
    .savedFile = u"File saved to “%1”."_s,
    .savingFile = u"Saving “%1”…"_s,
    .saveFailed = u"Could not save “%1”: %2"_s,
    .depthConverted = u"Intensities are converted to 8 bits per component (0 to 255)."_s,
    .cutTooShort = u"Move the ends of the line at least one pixel apart to read the profile."_s,
    .sessionChanged = u"The file “%1” has changed or was moved. You can save your measurements in a copy."_s,
    .sessionBusy = u"Another save of “%1” is in progress. You can try again in a moment or save a copy."_s,
    .saveCopy = u"Save a copy…"_s,
    .lockFailed = u"Could not create the write lock in this folder."_s,
    .sessionHeaderTooLarge = u"The session data exceeds the 16 MiB header limit."_s,
};

} // namespace

const Textes &textes(Lang lang)
{
    switch (lang) {
    case Lang::Fr: return FR;
    case Lang::En: return EN;
    }
    return EN;
}
