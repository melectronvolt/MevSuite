#include "about.h"
#include "app.h"
#include "crash.h"
#include "i18n.h"
#include "settings.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QScreen>

using namespace Qt::StringLiterals;

namespace {

/// AppImage : le Qt joint ne dessine pas les polices émoji en couleur au format COLR (celle de
/// Fedora) ; la petite police émoji en images jointe à côté (packaging/police-emoji.py) les remplace.
/// Absente des autres versions, qui ont le Qt du système (ou de MSYS2).
void charge_police_emoji_appimage()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    const QString chemin = QApplication::applicationDirPath() + u"/../share/mevsuite/mevsuite-emoji.ttf"_s;
    if (!QFileInfo::exists(chemin))
        return;
    const QStringList familles = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(chemin));
    if (!familles.isEmpty())
        QFontDatabase::addApplicationEmojiFontFamily(familles.first());
#endif
}

/// Restaure la taille mémorisée, y compris sur un vidéoprojecteur de résolution différente.
void restaure_fenetre(MainWindow &fenetre, const Settings &reglages)
{
    fenetre.resize(reglages.windowSize.value_or(DEFAULT_WINDOW));
    bool agrandie = reglages.maximized;
    if (const QScreen *ecran = fenetre.screen(); ecran && !agrandie) {
        // Fenêtre restaurée plus grande que l'écran (réglages venus d'un autre poste, vidéoprojecteur
        // en basse définition) : elle est ramenée à l'écran, place gardée pour la barre de titre. Elle
        // n'est agrandie que si l'écran est plus petit que sa taille minimale.
        const QRect zoneDisponible = ecran->availableGeometry();
        const QSize tailleDisponible = zoneDisponible.size() - QSize(16, 48);
        agrandie = tailleDisponible.width() < MIN_WINDOW.width() || tailleDisponible.height() < MIN_WINDOW.height();
        fenetre.resize(fenetre.size().boundedTo(tailleDisponible));
        fenetre.move(zoneDisponible.center() - fenetre.rect().center());
    }
    if (agrandie)
        fenetre.showMaximized();
    else
        fenetre.show();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"mevsono"_s);
    QApplication::setApplicationVersion(about::VERSION);
    // Identifiant de la fenêtre sous Wayland : icône et entrée de menu `mevsono.desktop`.
    QGuiApplication::setDesktopFileName(u"mevsono"_s);
    QIcon icone;
    for (int taille : {16, 32, 48, 64, 128, 256})
        icone.addFile(u":/icons/mevsono-%1.png"_s.arg(taille), QSize(taille, taille));
    QApplication::setWindowIcon(icone);
    charge_police_emoji_appimage();

    const SettingsFiles fichiers = SettingsFiles::localise();
    const Settings reglages = Settings::charge(fichiers);
    theme::choisit(reglages.theme);
    const Lang langue = resout_langue(reglages.lang);
    const QStringList arguments = QApplication::arguments();
    // Relancé après un plantage : la boîte de message seulement.
    if (crash::montre_plantage(arguments, langue))
        return 0;
    // Journal des erreurs fatales et des plantages, à côté du fichier de réglages de l'utilisateur.
    crash::installe(fichiers.user.isEmpty() ? QString() : QFileInfo(fichiers.user).dir().filePath(u"mevsono.log"_s), langue);

    // Son éventuellement passé en argument (« Ouvrir avec… » sous Windows).
    MainWindow fenetre(reglages, fichiers, arguments.value(1));
    restaure_fenetre(fenetre, reglages);
    new Splash(textes(langue), &fenetre);
    return QApplication::exec();
}
