#include "about.h"
#include "app.h"
#include "crash.h"
#include "i18n.h"
#include "settings.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QScreen>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"mevsono"_s);
    QApplication::setApplicationVersion(about::VERSION);
    // Identifiant de la fenêtre sous Wayland : icône et entrée de menu `mevsono.desktop`.
    QGuiApplication::setDesktopFileName(u"mevsono"_s);
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(u":/icons/mevsono-%1.png"_s.arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);

    const SettingsFiles files = SettingsFiles::locate();
    const Settings settings = Settings::load(files);
    theme::choose(settings.theme);
    const Lang lang = resolveLang(settings.lang);
    // Journal des erreurs fatales, à côté du fichier de réglages de l'utilisateur.
    crash::install(files.user.isEmpty() ? QString() : QFileInfo(files.user).dir().filePath(u"mevsono.log"_s), lang);

    // Son éventuellement passé en argument (« Ouvrir avec… » sous Windows).
    const QStringList args = QApplication::arguments();
    MainWindow window(settings, files, args.size() > 1 ? args[1] : QString());
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
    new Splash(texts(lang), &window);
    return QApplication::exec();
}
