// MeVReg (version Qt) : tableur de grandeurs, graphes, modélisation et incertitudes pour les
// TP de physique-chimie.

#include "app/about.h"
#include "app/crash.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "core/resources.h"
#include "python/engine.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QScreen>
#include <QTimer>
#include <QTranslator>

#include <cstdio>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

/// `mevreg --selftest` : vérifie, sans fenêtre, que le Python intégré démarre et trouve sa
/// bibliothèque standard (versions portables : dossier `python` à côté du programme). Code de
/// sortie 0 si tout va bien.
int selfTest(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ensureCoreResources();
    const python::ScriptOutput out = python::Engine::instance().runSync(
        u"import json, re, math, random, statistics, datetime, csv, io, os\n"
        "import numpy as np\nimport matplotlib.pyplot as plt\nimport pandas as pd\nimport mevreg as mr\n"
        "import fenetre\n"
        "df = pd.DataFrame({'t': np.linspace(0, 1, 5)})\n"
        "print('MeVReg', mr.__name__, 'Python', __import__('sys').version.split()[0], 'moyenne', df['t'].mean())\n"_s,
        {});
    std::fputs(out.stdoutText.toUtf8().constData(), stdout);
    if (out.error) {
        std::fprintf(stderr, "%s\n", out.error->traceback.isEmpty() ? qPrintable(out.error->message) : qPrintable(out.error->traceback));
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--selftest") == 0)
            return selfTest(argc, argv);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(u"MeVSuite"_s);
    // réglages dans MeVSuite/mevreg.ini (`settingsFile`) ; pas de nom d'affichage : Qt l'ajouterait à la fin du titre
    // de la fenêtre, qui commence déjà par « MeVReg — »
    QApplication::setApplicationName(u"mevreg"_s);
    QApplication::setApplicationVersion(QStringLiteral(MEVREG_VERSION));
    QApplication::setDesktopFileName(u"mevreg"_s);
    ensureCoreResources();
    QApplication::setWindowIcon(QIcon(u":/icons/mevreg.svg"_s));

    // boîtes de dialogue standard de Qt (fichiers, impression) en français
    QTranslator qtTr;
    if (qtTr.load(QLocale(QLocale::French), u"qtbase"_s, u"_"_s, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTr);
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));

    Store store;
    store.prefs = Prefs::load();
    theme::init();
    theme::choose(store.prefs.theme);
    // relancé après un plantage : la boîte de message seulement
    if (crash::showCrash(QApplication::arguments()))
        return 0;
    // journal des erreurs fatales et des plantages, à côté du fichier de réglages
    crash::install(QFileInfo(settingsFile()).dir().filePath(u"mevreg.log"_s));
    MainWindow win(store);
    win.resize(store.prefs.windowSize.value_or(DEFAULT_WINDOW));
    bool maximized = store.prefs.maximized;
    if (const QScreen *screen = win.screen(); screen && !maximized) {
        // Fenêtre restaurée plus grande que l'écran (réglages venus d'un autre poste, vidéoprojecteur
        // en basse définition) : elle est ramenée à l'écran, place gardée pour la barre de titre. Elle
        // n'est agrandie que si l'écran est plus petit que sa taille minimale.
        const QRect available = screen->availableGeometry();
        const QSize room = available.size() - QSize(16, 48);
        maximized = room.width() < MIN_WINDOW.width() || room.height() < MIN_WINDOW.height();
        win.resize(win.size().boundedTo(room));
        win.move(available.center() - win.rect().center());
    }
    if (maximized)
        win.showMaximized();
    else
        win.show();
    new Splash(&win);
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        const QString path = args.at(1);
        QTimer::singleShot(0, &win, [&win, path] { win.openPath(path); });
    }
    return QApplication::exec();
}
