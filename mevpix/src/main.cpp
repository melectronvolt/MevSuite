#include "about.h"
#include "app.h"
#include "crash.h"
#include "i18n.h"
#include "settings.h"
#include "theme.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QImageWriter>
#include <QPainter>
#include <QScreen>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

/// AppImage : le Qt joint ne dessine pas les polices émoji en couleur au format COLR (celle de
/// Fedora) ; la petite police émoji en images jointe à côté (packaging/police-emoji.py) les remplace.
/// Absente des autres versions, qui ont le Qt du système (ou de MSYS2).
void charge_police_emoji_appimage()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    const QString path = QApplication::applicationDirPath() + u"/../share/mevsuite/mevsuite-emoji.ttf"_s;
    if (!QFileInfo::exists(path))
        return;
    const QStringList families = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(path));
    if (!families.isEmpty())
        QFontDatabase::addApplicationEmojiFontFamily(families.first());
#endif
}

/// Vérifie les greffons du paquet avec deux bandes de couleur créées pour cet essai.
bool verifie_formats()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return false;
    QImage portrait(24, 48, QImage::Format_RGB32);
    portrait.fill(Qt::red);
    QPainter peintre(&portrait);
    peintre.fillRect(0, 24, 24, 24, Qt::blue);
    peintre.end();
    const auto relit = [&](const QString &path) {
        const auto photo = charge_image(path);
        return photo.image && photo.image->rgb.size() == portrait.size()
            && photo.image->rgb.pixelColor(12, 8).red() > 180 && photo.image->rgb.pixelColor(12, 40).blue() > 180;
    };

    // JPEG couché ; le segment APP1 contient un seul champ EXIF, Orientation = 6 (rotation de 90°).
    QBuffer jpeg;
    if (!jpeg.open(QIODevice::ReadWrite) || !portrait.transformed(QTransform().rotate(-90)).save(&jpeg, "JPEG"))
        return false;
    QByteArray donnees = jpeg.data();
    donnees.insert(2, QByteArray::fromHex("ffe100224578696600004d4d002a00000008000101120003000000010006000000000000"));
    QFile fichier(dir.filePath(u"rotation.jpg"_s));
    if (!fichier.open(QIODevice::WriteOnly) || fichier.write(donnees) != donnees.size())
        return false;
    fichier.close();
    if (!relit(fichier.fileName()))
        return false;

    // Le lecteur reconnaît aussi le format par son contenu, même si l'extension est erronée.
    const QString renomme = dir.filePath(u"renomme.heic"_s);
    if (!portrait.save(renomme, "PNG") || !relit(renomme))
        return false;
    for (const QByteArray &format : QImageWriter::supportedImageFormats()) {
        if (format == "heic" || format == "heif") {
            const QString path = dir.filePath(u"mire."_s + QString::fromLatin1(format));
            return portrait.save(path, format.constData()) && relit(path);
        }
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (app.arguments().contains(u"--selftest-formats"_s))
        return verifie_formats() ? 0 : 1;
    QApplication::setApplicationName(u"mevpix"_s);
    QApplication::setApplicationVersion(about::VERSION);
    // Identifiant de la fenêtre sous Wayland : icône et entrée de menu `mevpix.desktop`.
    QGuiApplication::setDesktopFileName(u"mevpix"_s);
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(u":/icons/mevpix-%1.png"_s.arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);
    charge_police_emoji_appimage();

    const FichiersReglages files = FichiersReglages::localise();
    const Reglages settings = Reglages::charge(files);
    // Avant la création des fenêtres, qui prennent la palette du thème.
    theme::choisit(settings.theme);
    const Lang lang = resout_langue(settings.lang);
    // Relancé après un plantage : la boîte de message seulement.
    if (crash::montre_plantage(QApplication::arguments(), lang))
        return 0;
    // Journal des erreurs fatales et des plantages, à côté du fichier de réglages de l'utilisateur.
    crash::installe(files.user.isEmpty() ? QString() : QFileInfo(files.user).dir().filePath(u"mevpix.log"_s), lang);

    // Image éventuellement passée en argument (« Ouvrir avec… » sous Windows).
    const QStringList args = QApplication::arguments();
    FenetrePrincipale window(settings, files, args.size() > 1 ? args[1] : QString());
    window.resize(settings.windowSize.value_or(DEFAULT_WINDOW));
    bool maximized = settings.maximized;
    if (const QScreen *screen = window.screen(); screen && !maximized) {
        // Fenêtre restaurée plus grande que l'écran (réglages venus d'un autre poste, vidéoprojecteur
        // en basse définition) : elle est ramenée à l'écran, place gardée pour la barre de titre. Elle
        // n'est agrandie que si l'écran est plus petit que sa taille minimale.
        const QRect available = screen->availableGeometry();
        const QSize room = available.size() - QSize(16, 48);
        maximized = room.width() < MIN_WINDOW.width() || room.height() < MIN_WINDOW.height();
        window.resize(window.size().boundedTo(room));
        window.move(available.center() - window.rect().center());
    }
    if (maximized)
        window.showMaximized();
    else
        window.show();
    new EcranDemarrage(textes(lang), &window);
    return QApplication::exec();
}
