// MeVCine : étude cinématique de vidéos pour le lycée. Pointage d'objets en mouvement, repère et
// échelle choisis par l'élève, tableau des mesures, export en CSV et en listes Python. Un mode
// Simplifié guide l'élève pas à pas ; un mode Expert donne accès à toutes les fonctions (plusieurs
// objets, suivi automatique, trajectoires, vitesses, grapheur). Le logiciel ne calcule pas d'énergie :
// c'est le travail de l'élève. L'interface est en français et en anglais (i18n.h).
//
// Écrit en C++ avec Qt ; la version précédente, en Rust, est dans Archive/cinemev.
//
// Copyright (C) 2026 Rémi MEVAERE
//
// Composant libre intégré (voir NOTICE et la fenêtre « À propos ») :
// code adapté de pymecavideo, GPL-3.0-or-later,
//   Copyright (C) 2007-2016 Jean-Baptiste Butet <ashashiwa@gmail.com>
//   Copyright (C) 2007-2023 Georges Khaznadar <georgesk@debian.org>
//
// Ce programme est un logiciel libre, sous licence GPL version 3 ou ultérieure.

#include "app/a_propos.h"
#include "app/app.h"
#include "app/theme.h"
#include "debogage.h"
#include "erreurs.h"
#include "i18n.h"
#include "prefs.h"

#include <QApplication>
#include <QIcon>

#include <cstdio>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

void usage()
{
    std::printf("%s\n", qUtf8Printable(trad(
                            "Usage : mevcine [--simple | --expert] [-d (1-3) | --debug=(1-3)] [vidéo | projet]\n"
                            "   lance MeVCine, pour l'étude cinématique de vidéos\n"
                            "   --simple, -s : démarre directement en mode simplifié (pas à pas)\n"
                            "   --expert, -e : démarre directement en mode expert\n"
                            "   -d ou --debug= : messages de débogage plus ou moins bavards (de 1 à 3)\n"
                            "   argument facultatif : une vidéo, ou un projet (.mevcine, .cinemev, .mecavideo)")));
}

struct Arguments {
    QString fichier;
    std::optional<Mode> mode;
};

/// analyse la ligne de commande : le fichier éventuel à ouvrir, et le mode de travail s'il est imposé
Arguments analyse_arguments(const QStringList &args)
{
    Arguments res;
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == u"-s" || a == u"--simple") {
            res.mode = Mode::Simplifie;
            continue;
        }
        if (a == u"-e" || a == u"--expert") {
            res.mode = Mode::Expert;
            continue;
        }
        std::optional<QString> niveau;
        if (a == u"-d" || a == u"--debug") {
            if (i + 1 < args.size())
                niveau = args[++i];
            else
                niveau = QString();
        } else if (a.startsWith(u"--debug="_s)) {
            niveau = a.mid(8);
        } else if (a.startsWith(u"-d"_s) && a.size() > 2) {
            niveau = a.mid(2);
        } else if (a == u"-h" || a == u"--help") {
            usage();
            std::exit(0);
        } else if (a.startsWith(u'-')) {
            usage();
            std::exit(2);
        } else {
            if (res.fichier.isEmpty())
                res.fichier = a;
            continue;
        }
        bool ok = false;
        const int n = niveau->toInt(&ok);
        if (!ok || n < 0 || n > 255) {
            usage();
            std::exit(2);
        }
        NIVEAU_DBG.store(n);
        std::fprintf(stderr, "%s\n", qUtf8Printable(trad("Le débogage a pour verbosité {}", n)));
    }
    return res;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(u"mevcine"_s);
    QApplication::setApplicationVersion(a_propos::VERSION);
    // identifiant de la fenêtre sous Wayland : icône et entrée de menu `mevcine.desktop`
    QGuiApplication::setDesktopFileName(u"mevcine"_s);
    QIcon icone;
    for (int taille : {16, 32, 48, 64, 256})
        icone.addFile(u":/icones/mevcine-%1.png"_s.arg(taille), QSize(taille, taille));
    QApplication::setWindowIcon(icone);

    // la langue choisie avec le bouton du drapeau, sinon celle du système (dès l'aide de la ligne de
    // commande) ; le thème, avant la création de la fenêtre, qui prend sa palette
    const prefs::Config conf = prefs::charge().value_or(prefs::Config());
    i18n::choisit(prefs::langue(conf).value_or(i18n::langue_du_systeme()));
    theme::choisit(theme::depuis_texte(conf.get(u"theme"_s).value_or(QString())));
    // relancé après un plantage : la boîte de dialogue seulement
    if (erreurs::montre_plantage(QApplication::arguments()))
        return 0;
    erreurs::installe();
    const Arguments args = analyse_arguments(QApplication::arguments());

    App fenetre(args.fichier, args.mode);
    fenetre.montre();
    return QApplication::exec();
}
