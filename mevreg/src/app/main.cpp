// MeVReg (version Qt) : tableur de grandeurs, graphes, modélisation et incertitudes pour les
// TP de physique-chimie.

#include "app/about.h"
#include "app/crash.h"
#include "app/mainwindow.h"
#include "app/theme.h"
#include "app/scripting.h"
#include "plot/export.h"
#include "core/resources.h"
#include "python/engine.h"

#include <cstdlib>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QLibraryInfo>
#include <QScreen>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QTranslator>

#include <cstdio>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

/// `mevreg --selftest` : vérifie, sans fenêtre, que le Python intégré démarre et trouve sa
/// bibliothèque standard (versions portables : dossier `python` à côté du programme). Code de
/// sortie 0 si tout va bien.
int autotest(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    assure_ressources_coeur();
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    python::ScriptInput in;
    in.folder = dir.path();
    QFile csv(dir.filePath(u"mesures été.csv"_s));
    if (!csv.open(QIODevice::WriteOnly) || csv.write("I (A);U (V)\n0;0\n0,1;4,7\n0,2;9,4\n") < 0)
        return 1;
    csv.close();
    const auto out = python::Engine::instance().lance_synchrone(uR"py(
import csv, datetime, json, statistics, sys, math, random
import numpy as np, pandas as pd, matplotlib.pyplot as plt, mevreg as mr, fenetre
assert sys.flags.isolated and sys.flags.no_site
df = mr.lire_csv("mesures été.csv")
assert df.shape == (3, 2) and mr.unite("I") == "A"
mr.parametre("gain", 2)
mr.calculer("signal", "gain*U", "V")
assert np.allclose(mr.valeurs("signal"), [0, 9.4, 18.8])
assert abs(mr.modeliser("U", "I", "lineaire").a - 47) < 1e-8
z = np.array([1+2j,3-1j])
assert np.vdot(z,z) == 15 and np.allclose(np.fft.ifft(np.fft.fft(z)), z)
assert np.allclose(np.linalg.solve([[2.,0],[0,3]], [4,9]), [2,3])
x, y = mr.valeurs("I"), mr.valeurs("U")
plt.errorbar(x, y, yerr=[[0.1]*3, [0.2]*3])
plt.xticks([0, 0.2], ["DebutTP", "FinTP"])
plt.savefig("essai.svg")
print("MeVReg", "Python", sys.version.split()[0], "NumPy", np.__version__, "R = 47 ohms")
)py"_s, in);
    std::fputs(out.stdoutText.toUtf8().constData(), stdout);
    if (out.error) {
        std::fprintf(stderr, "%s\n", out.error->traceback.isEmpty() ? qPrintable(out.error->message) : qPrintable(out.error->traceback));
        return 1;
    }
    Seance ws;
    const ViewId script = ws.ajoute_script({});
    const auto applied = scripting::applique_sortie(ws, script, out);
    QString err;
    const auto saved = phy::charge(phy::enregistre(ws, {}), &err);
    if (!saved || applied.savefig.size() != 1) return 1;
    const auto &[gid, name] = applied.savefig.front();
    const auto *graph = saved->seance.graphe(gid);
    if (!graph || !exporter::ecrit_graphe(dir.filePath(name), exporter::Format::Svg, *graph, saved->seance,
                                           QSizeF(640, 400), NumberStyle{}, &err)) return 1;
    QFile svg(dir.filePath(name));
    if (!svg.open(QIODevice::ReadOnly)) return 1;
    const auto bytes = svg.readAll();
    if (!bytes.contains("DebutTP") || !bytes.contains("FinTP")) return 1;
    return 0;
}


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

} // namespace

static int lance_application(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--selftest") == 0)
            return autotest(argc, argv);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(u"MeVSuite"_s);
    // réglages dans MeVSuite/mevreg.ini (`fichier_reglages`) ; pas de nom d'affichage : Qt l'ajouterait à la fin du titre
    // de la fenêtre, qui commence déjà par « MeVReg — »
    QApplication::setApplicationName(u"mevreg"_s);
    QApplication::setApplicationVersion(QStringLiteral(MEVREG_VERSION));
    QApplication::setDesktopFileName(u"mevreg"_s);
    assure_ressources_coeur();
    QApplication::setWindowIcon(QIcon(u":/icons/mevreg.svg"_s));
    charge_police_emoji_appimage();

    // boîtes de dialogue standard de Qt (fichiers, impression) en français
    QTranslator qtTr;
    if (qtTr.load(QLocale(QLocale::French), u"qtbase"_s, u"_"_s, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTr);
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));

    Store store;
    store.prefs = Prefs::charge();
    theme::initialise();
    theme::choisit(store.prefs.theme);
    // relancé après un plantage : la boîte de message seulement
    if (crash::montre_plantage(QApplication::arguments()))
        return 0;
    // journal des erreurs fatales et des plantages, à côté du fichier de réglages
    crash::installe(QFileInfo(fichier_reglages()).dir().filePath(u"mevreg.log"_s));
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
        QTimer::singleShot(0, &win, [&win, chemin = args.at(1)] { win.ouvre_chemin(chemin); });
    }
    QTimer::singleShot(0, &win, [&win] { win.propose_recuperation(true); });
    return QApplication::exec();
}

int main(int argc, char **argv)
{
    const int code = lance_application(argc, argv);
    // Les fenêtres et les documents sont déjà fermés. Un script bloqué dans une
    // extension native ne doit ni retenir le processus ni utiliser des objets détruits.
    if (!python::Engine::instance().arrete_avant_sortie())
        std::_Exit(code);
    return code;
}
