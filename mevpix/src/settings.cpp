#include "settings.h"

#include "savefile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

const QString FILE_NAME = u"mevpix.ini"_s;

QString code_unite(Unite unite)
{
    switch (unite) {
    case Unite::Micrometre: return u"um"_s;
    case Unite::Millimetre: return u"mm"_s;
    case Unite::Centimetre: return u"cm"_s;
    case Unite::Metre: return u"m"_s;
    }
    return {};
}

QString code_canal(Canal canal)
{
    switch (canal) {
    case Canal::GrisPondere: return u"luminance"_s;
    case Canal::Rouge: return u"red"_s;
    case Canal::Vert: return u"green"_s;
    case Canal::Bleu: return u"blue"_s;
    }
    return {};
}

QString code_theme(Qt::ColorScheme theme)
{
    switch (theme) {
    case Qt::ColorScheme::Light: return u"light"_s;
    case Qt::ColorScheme::Dark: return u"dark"_s;
    case Qt::ColorScheme::Unknown: break;
    }
    return u"auto"_s;
}

std::optional<double> lit_reel(const QString &s)
{
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok && std::isfinite(v) ? std::optional(v) : std::nullopt;
}

/// Dossier commun aux logiciels de la suite : %APPDATA%\\MeVSuite sous Windows,
/// ~/.config/MeVSuite ailleurs ; un fichier de réglages par logiciel, sans autre dossier.
QString dossier_config()
{
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    return appData.isEmpty() ? QString() : QDir(appData).filePath(u"MeVSuite"_s);
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) {
        const QString home = qEnvironmentVariable("HOME");
        if (home.isEmpty())
            return {};
        base = QDir(home).filePath(u".config"_s);
    }
    return QDir(base).filePath(u"MeVSuite"_s);
#endif
}

/// Ancien dossier propre au logiciel : ses fichiers passent dans celui de la suite, puis il est
/// retiré s'il est vide.
void migre(const QString &dir)
{
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("APPDATA");
    const QString old = base.isEmpty() ? QString() : QDir(base).filePath(u"MeVPix"_s);
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() && !qEnvironmentVariable("HOME").isEmpty())
        base = QDir(qEnvironmentVariable("HOME")).filePath(u".config"_s);
    const QString old = base.isEmpty() ? QString() : QDir(base).filePath(u"mevpix"_s);
#endif
    if (old.isEmpty() || !QFileInfo(old).isDir() || QDir(old) == QDir(dir))
        return;
    QDir().mkpath(dir);
    for (const QString &name : {FILE_NAME, u"mevpix.log"_s}) {
        const QString from = QDir(old).filePath(name), to = QDir(dir).filePath(name);
        if (QFileInfo(from).isFile() && !QFileInfo(to).exists())
            QFile::rename(from, to);
    }
    QDir().rmdir(old);
}

} // namespace

FichiersReglages FichiersReglages::localise()
{
    FichiersReglages files;
    const QString dir = dossier_config();
    if (!dir.isEmpty()) {
        migre(dir);
        files.user = QDir(dir).filePath(FILE_NAME);
    }
    // réglages imposés (salle informatique, parc) : sous Linux dans /etc/MeVSuite (les paquets
    // mettent le programme dans /usr/bin, une AppImage ne se modifie pas), sinon à côté du programme
    QStringList admin;
#ifndef Q_OS_WIN
    admin << u"/etc/MeVSuite/"_s + FILE_NAME;
#endif
    admin << QDir(QCoreApplication::applicationDirPath()).filePath(FILE_NAME);
    for (const QString &path : admin) {
        if (QFileInfo(path).isFile()) {
            files.admin = path;
            break;
        }
    }
    return files;
}

Reglages Reglages::charge(const FichiersReglages &files)
{
    Reglages reglages;
    for (const QString &chemin : {files.user, files.admin}) {
        QFile fichier(chemin);
        if (chemin.isEmpty() || !QFileInfo(fichier).isFile() || !fichier.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QByteArray contenu = fichier.readAll();
        if (fichier.error() == QFileDevice::NoError)
            reglages.applique(QString::fromUtf8(contenu));
    }
    return reglages;
}

QString Reglages::enregistre(const QString &path, const Reglages *avant) const
{
    const Textes &t = textes(resout_langue(lang));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return t.folderFailed(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    // Le verrou couvre la relecture et le remplacement. Deux fenêtres peuvent ainsi
    // modifier, par exemple, l'une la langue et l'autre la taille de la fenêtre.
    QLockFile verrou(path + u".lock"_s);
    if (!verrou.tryLock(250))
        return verrou.error() == QLockFile::LockFailedError ? t.settingsBusy : t.lockFailed;

    const QFileInfo cible(path);
    if (cible.exists() && !cible.isFile())
        return t.regularFileRequired;
    Reglages aEcrire = *this;
    if (avant) {
        Reglages surDisque;
        QFile original(path);
        if (original.exists()) {
            if (!original.open(QIODevice::ReadOnly | QIODevice::Text))
                return original.errorString();
            surDisque.applique(QString::fromUtf8(original.readAll()));
            if (original.error() != QFileDevice::NoError)
                return original.errorString();
        }
        // La référence est l'état de la fenêtre, qui peut inclure des réglages administrateur.
        // Pour chaque champ resté identique, garder la valeur personnelle relue sur disque.
        const auto conserveSiInchange = [&](auto champ) {
            if (this->*champ == avant->*champ)
                aEcrire.*champ = surDisque.*champ;
        };
        conserveSiInchange(&Reglages::lang);
        conserveSiInchange(&Reglages::theme);
        conserveSiInchange(&Reglages::unite);
        conserveSiInchange(&Reglages::canal);
        conserveSiInchange(&Reglages::largeurBande);
        conserveSiInchange(&Reglages::uiZoom);
        conserveSiInchange(&Reglages::lastDir);
        conserveSiInchange(&Reglages::windowSize);
        conserveSiInchange(&Reglages::maximized);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || !ecrit_tout(file, aEcrire.en_texte().toUtf8()) || !file.commit())
        return file.errorString();
    return {};
}

Reglages Reglages::analyse(const QString &text)
{
    Reglages s;
    s.applique(text);
    return s;
}

void Reglages::applique(const QString &texte)
{
    std::optional<double> largeur, hauteur;
    for (const QString &ligneBrute : texte.split(u'\n')) {
        const QString ligne = ligneBrute.trimmed();
        const qsizetype separateur = ligne.indexOf(u'=');
        if (ligne.startsWith(u'#') || separateur < 0)
            continue;
        const QString cle = ligne.left(separateur).trimmed();
        const QString valeur = ligne.mid(separateur + 1).trimmed();
        if (cle == u"language") {
            if (valeur == u"auto")
                lang.reset();
            else if (const auto langue = langue_du_code(valeur))
                lang = langue;
        } else if (cle == u"theme") {
            for (const auto choix : {Qt::ColorScheme::Unknown, Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
                if (valeur == code_theme(choix))
                    theme = choix;
            }
        } else if (cle == u"unit") {
            const auto uniteTrouvee = std::ranges::find(UNITES, valeur, code_unite);
            if (uniteTrouvee != UNITES.end())
                unite = *uniteTrouvee;
        } else if (cle == u"channel") {
            const auto canalTrouve = std::ranges::find(CANAUX, valeur, code_canal);
            if (canalTrouve != CANAUX.end())
                canal = *canalTrouve;
        } else if (cle == u"band") {
            bool valide = false;
            const uint epaisseur = valeur.toUInt(&valide);
            if (valide)
                largeurBande = static_cast<int>(std::clamp(epaisseur, 1u, 50u));
        } else if (cle == u"ui_zoom") {
            if (const auto zoom = lit_reel(valeur); zoom && *zoom >= UI_ZOOM_MIN && *zoom <= UI_ZOOM_MAX)
                uiZoom = *zoom;
        } else if (cle == u"last_dir") {
            lastDir = QDir::fromNativeSeparators(valeur);
        } else if (cle == u"window_width") {
            largeur = lit_reel(valeur);
        } else if (cle == u"window_height") {
            hauteur = lit_reel(valeur);
        } else if (cle == u"maximized" && (valeur == u"true" || valeur == u"false")) {
            maximized = valeur == u"true";
        }
    }
    if (largeur && hauteur && *largeur >= 400 && *largeur <= 20000 && *hauteur >= 300 && *hauteur <= 20000)
        windowSize = QSize(static_cast<int>(std::lround(*largeur)), static_cast<int>(std::lround(*hauteur)));
}

QString Reglages::en_texte() const
{
    QString out = u"# MeVPix — réglages / settings\n# language : auto, fr, en\n# theme : auto, light, dark\n"_s;
    out += u"language = %1\n"_s.arg(lang ? code_langue(*lang) : u"auto"_s);
    out += u"theme = %1\n"_s.arg(code_theme(theme));
    out += u"unit = %1\n"_s.arg(code_unite(unite));
    out += u"channel = %1\n"_s.arg(code_canal(canal));
    out += u"band = %1\n"_s.arg(largeurBande);
    out += u"ui_zoom = %1\n"_s.arg(plus_court(uiZoom));
    out += u"last_dir = %1\n"_s.arg(QDir::toNativeSeparators(lastDir));
    if (windowSize)
        out += u"window_width = %1\nwindow_height = %2\n"_s.arg(windowSize->width()).arg(windowSize->height());
    out += u"maximized = %1\n"_s.arg(maximized ? u"true"_s : u"false"_s);
    return out;
}
