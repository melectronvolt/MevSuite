#include "settings.h"

#include "format.h"
#include "savefile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

const QString NOM_FICHIER = u"mevsono.ini"_s;

std::optional<double> lit_reel(const QString &s)
{
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok && std::isfinite(v) ? std::optional(v) : std::nullopt;
}

/// Entier positif ou nul (pas de signe « - »).
std::optional<qulonglong> lit_compte(const QString &s)
{
    bool ok = false;
    const qulonglong v = s.toULongLong(&ok);
    return ok && !s.startsWith(u'-') ? std::optional(v) : std::nullopt;
}

/// Dossier de configuration qui contient les réglages de toute la suite et ceux des anciennes versions.
QString racine_configuration()
{
#ifdef Q_OS_WIN
    return qEnvironmentVariable("APPDATA");
#else
    const QString dossier = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (QDir::isAbsolutePath(dossier))
        return dossier;
    const QString personnel = qEnvironmentVariable("HOME");
    return personnel.isEmpty() ? QString() : QDir(personnel).filePath(u".config"_s);
#endif
}

/// Déplace les anciens fichiers lorsque leur destination est encore libre.
void migre(const QDir &racine, const QDir &destination)
{
#ifdef Q_OS_WIN
    const QString ancien = racine.filePath(u"MeVSono"_s);
#else
    const QString ancien = racine.filePath(u"mevsono"_s);
#endif
    if (!QFileInfo(ancien).isDir() || QDir(ancien) == destination)
        return;
    QDir().mkpath(destination.path());
    for (const QString &nom : {NOM_FICHIER, u"mevsono.log"_s}) {
        const QString source = QDir(ancien).filePath(nom);
        const QString cible = destination.filePath(nom);
        if (QFileInfo(source).isFile() && !QFileInfo::exists(cible))
            QFile::rename(source, cible);
    }
    QDir().rmdir(ancien); // rmdir conserve un dossier qui contient encore des fichiers.
}

bool lit_booleen(const QString &texte, bool valeurActuelle)
{
    if (texte == u"true")
        return true;
    if (texte == u"false")
        return false;
    return valeurActuelle;
}

QString texte_booleen(bool value)
{
    return value ? u"true"_s : u"false"_s;
}

QString code_theme(Qt::ColorScheme theme)
{
    switch (theme) {
    case Qt::ColorScheme::Light: return u"light"_s;
    case Qt::ColorScheme::Dark: return u"dark"_s;
    default: return u"auto"_s;
    }
}

} // namespace

SettingsFiles SettingsFiles::localise()
{
    SettingsFiles files;
    const QString racine = racine_configuration();
    if (!racine.isEmpty()) {
        const QDir dossier(QDir(racine).filePath(u"MeVSuite"_s));
        migre(QDir(racine), dossier);
        files.user = dossier.filePath(NOM_FICHIER);
    }
    // réglages imposés (salle informatique, parc) : sous Linux dans /etc/MeVSuite (les paquets
    // mettent le programme dans /usr/bin, une AppImage ne se modifie pas), sinon à côté du programme
    QStringList admin;
#ifndef Q_OS_WIN
    admin << u"/etc/MeVSuite/"_s + NOM_FICHIER;
#endif
    admin << QDir(QCoreApplication::applicationDirPath()).filePath(NOM_FICHIER);
    for (const QString &path : admin) {
        if (QFileInfo(path).isFile()) {
            files.admin = path;
            break;
        }
    }
    return files;
}

Settings Settings::charge(const SettingsFiles &files)
{
    Settings s;
    for (const QString &path : {files.user, files.admin}) {
        QFile file(path);
        if (!path.isEmpty() && file.open(QIODevice::ReadOnly | QIODevice::Text))
            s.applique(QString::fromUtf8(file.readAll()));
    }
    return s;
}

QString Settings::enregistre(const QString &path) const
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return textes(resout_langue(lang)).folderFailed(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || !ecrit_tout(file, en_texte().toUtf8()) || !file.commit())
        return file.errorString();
    return {};
}

Settings Settings::interprete(const QString &text)
{
    Settings s;
    s.applique(text);
    return s;
}

void Settings::applique(const QString &texte)
{
    // Un fichier de salle peut imposer une seule dimension d’une fenêtre déjà mémorisée.
    QSize taille = windowSize.value_or(QSize());
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
        } else if (cle == u"mode") {
            if (valeur == u"expert")
                expert = true;
            else if (valeur == u"simple")
                expert = false;
        } else if (cle == u"channel") {
            channel = audio::canal_du_code(valeur).value_or(channel);
        } else if (cle == u"window") {
            window = dsp::fenetre_du_code(valeur).value_or(window);
        } else if (cle == u"fft_size") {
            const auto points = valeur == u"auto" ? std::optional<qulonglong>(0) : lit_compte(valeur);
            if (points && std::ranges::find(dsp::FFT_SIZES, *points) != dsp::FFT_SIZES.end())
                fftSize = size_t(*points);
        } else if (cle == u"amplitude_db") {
            db = lit_booleen(valeur, db);
        } else if (cle == u"log_frequency") {
            logFreq = lit_booleen(valeur, logFreq);
        } else if (cle == u"auto_scale_y") {
            autoScaleY = lit_booleen(valeur, autoScaleY);
        } else if (cle == u"max_frequency") {
            const auto f = valeur == u"auto" ? std::optional(0.0) : lit_reel(valeur);
            if (f && std::ranges::find(FMAX_CHOICES, *f) != FMAX_CHOICES.end())
                fmax = *f;
        } else if (cle == u"peak_threshold_db") {
            if (const auto v = lit_reel(valeur); v && *v >= -100.0 && *v <= -10.0)
                thresholdDb = *v;
        } else if (cle == u"harmonics") {
            if (const auto n = lit_compte(valeur))
                harmonics = int(std::clamp<qulonglong>(*n, 1, 40));
        } else if (cle == u"snap_zero") {
            snapZero = lit_booleen(valeur, snapZero);
        } else if (cle == u"harmonic_marks") {
            harmonicMarks = lit_booleen(valeur, harmonicMarks);
        } else if (cle == u"ui_zoom") {
            if (const auto z = lit_reel(valeur); z && *z >= UI_ZOOM_MIN && *z <= UI_ZOOM_MAX)
                uiZoom = *z;
        } else if (cle == u"last_dir") {
            lastDir = QDir::fromNativeSeparators(valeur);
        } else if (cle == u"window_width") {
            if (const auto largeur = lit_reel(valeur); largeur && *largeur >= 400 && *largeur <= 20000)
                taille.setWidth(int(std::lround(*largeur)));
        } else if (cle == u"window_height") {
            if (const auto hauteur = lit_reel(valeur); hauteur && *hauteur >= 300 && *hauteur <= 20000)
                taille.setHeight(int(std::lround(*hauteur)));
        } else if (cle == u"maximized") {
            maximized = lit_booleen(valeur, maximized);
        }
    }
    if (taille.isValid())
        windowSize = taille;
}

QString Settings::en_texte() const
{
    QString out = u"# MeVSono — réglages / settings\n"
                  u"# language : auto, fr, en\n"
                  u"# theme : auto, light, dark\n"
                  u"# mode : simple, expert\n"
                  u"# channel : mix, left, right\n"
                  u"# window : rectangular, hann, hamming, blackman, blackman-harris, flat-top\n"
                  u"# fft_size : auto, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144\n"
                  u"# max_frequency : 1000, 2000, 5000, 10000, 15000, 20000, auto\n"_s;
    out += u"language = %1\n"_s.arg(lang ? code_langue(*lang) : u"auto"_s);
    out += u"theme = %1\n"_s.arg(code_theme(theme));
    out += u"mode = %1\n"_s.arg(expert ? u"expert"_s : u"simple"_s);
    out += u"channel = %1\n"_s.arg(audio::code_canal(channel));
    out += u"window = %1\n"_s.arg(dsp::code_fenetre(window));
    out += u"fft_size = %1\n"_s.arg(fftSize == 0 ? u"auto"_s : QString::number(fftSize));
    out += u"amplitude_db = %1\n"_s.arg(texte_booleen(db));
    out += u"log_frequency = %1\n"_s.arg(texte_booleen(logFreq));
    out += u"auto_scale_y = %1\n"_s.arg(texte_booleen(autoScaleY));
    out += u"max_frequency = %1\n"_s.arg(fmax > 0.0 ? plus_court(fmax) : u"auto"_s);
    out += u"peak_threshold_db = %1\n"_s.arg(plus_court(thresholdDb));
    out += u"harmonics = %1\n"_s.arg(harmonics);
    out += u"snap_zero = %1\n"_s.arg(texte_booleen(snapZero));
    out += u"harmonic_marks = %1\n"_s.arg(texte_booleen(harmonicMarks));
    out += u"ui_zoom = %1\n"_s.arg(plus_court(uiZoom));
    out += u"last_dir = %1\n"_s.arg(QDir::toNativeSeparators(lastDir));
    if (windowSize)
        out += u"window_width = %1\nwindow_height = %2\n"_s.arg(windowSize->width()).arg(windowSize->height());
    out += u"maximized = %1\n"_s.arg(texte_booleen(maximized));
    return out;
}
