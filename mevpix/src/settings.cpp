#include "settings.h"

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

const QString FILE_NAME = u"mevpix.ini"_s;

QString unitCode(Unit unit)
{
    switch (unit) {
    case Unit::Micrometre: return u"um"_s;
    case Unit::Millimetre: return u"mm"_s;
    case Unit::Centimetre: return u"cm"_s;
    case Unit::Metre: return u"m"_s;
    }
    return {};
}

QString channelCode(Channel channel)
{
    switch (channel) {
    case Channel::Luminance: return u"luminance"_s;
    case Channel::Red: return u"red"_s;
    case Channel::Green: return u"green"_s;
    case Channel::Blue: return u"blue"_s;
    }
    return {};
}

QString themeCode(Qt::ColorScheme theme)
{
    switch (theme) {
    case Qt::ColorScheme::Light: return u"light"_s;
    case Qt::ColorScheme::Dark: return u"dark"_s;
    case Qt::ColorScheme::Unknown: break;
    }
    return u"auto"_s;
}

std::optional<double> parseDouble(const QString &s)
{
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok && std::isfinite(v) ? std::optional(v) : std::nullopt;
}

/// Dossier commun aux logiciels de la suite : %APPDATA%\\MeVSuite sous Windows,
/// ~/.config/MeVSuite ailleurs ; un fichier de réglages par logiciel, sans autre dossier.
QString configDir()
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
void migrate(const QString &dir)
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

SettingsFiles SettingsFiles::locate()
{
    SettingsFiles files;
    const QString dir = configDir();
    if (!dir.isEmpty()) {
        migrate(dir);
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

Settings Settings::load(const SettingsFiles &files)
{
    Settings s;
    for (const QString &path : {files.user, files.admin}) {
        QFile file(path);
        if (!path.isEmpty() && file.open(QIODevice::ReadOnly | QIODevice::Text))
            s.apply(QString::fromUtf8(file.readAll()));
    }
    return s;
}

QString Settings::save(const QString &path) const
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return texts(resolveLang(lang)).folderFailed(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || !writeAll(file, toText().toUtf8()) || !file.commit())
        return file.errorString();
    return {};
}

Settings Settings::parse(const QString &text)
{
    Settings s;
    s.apply(text);
    return s;
}

void Settings::apply(const QString &text)
{
    std::optional<double> width, height;
    for (const QString &raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        const qsizetype eq = line.indexOf(u'=');
        if (line.startsWith(u'#') || eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == u"language") {
            lang = langFromCode(value);
        } else if (key == u"theme") {
            theme = Qt::ColorScheme::Unknown;
            for (const auto t : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
                if (value == themeCode(t))
                    theme = t;
            }
        } else if (key == u"unit") {
            const auto it = std::ranges::find(ALL_UNITS, value, unitCode);
            if (it != ALL_UNITS.end())
                unit = *it;
        } else if (key == u"channel") {
            const auto it = std::ranges::find(ALL_CHANNELS, value, channelCode);
            if (it != ALL_CHANNELS.end())
                channel = *it;
        } else if (key == u"band") {
            bool ok = false;
            const uint b = value.toUInt(&ok);
            if (ok)
                band = static_cast<int>(std::clamp(b, 1u, 50u));
        } else if (key == u"ui_zoom") {
            if (const auto z = parseDouble(value); z && *z >= UI_ZOOM_MIN && *z <= UI_ZOOM_MAX)
                uiZoom = *z;
        } else if (key == u"last_dir") {
            lastDir = QDir::fromNativeSeparators(value);
        } else if (key == u"window_width") {
            width = parseDouble(value);
        } else if (key == u"window_height") {
            height = parseDouble(value);
        } else if (key == u"maximized") {
            maximized = value == u"true";
        }
    }
    if (width && height && *width >= 400.0 && *width <= 20000.0 && *height >= 300.0 && *height <= 20000.0)
        windowSize = QSize(static_cast<int>(std::lround(*width)), static_cast<int>(std::lround(*height)));
}

QString Settings::toText() const
{
    QString out = u"# MeVPix — réglages / settings\n# language : auto, fr, en\n# theme : auto, light, dark\n"_s;
    out += u"language = %1\n"_s.arg(lang ? langCode(*lang) : u"auto"_s);
    out += u"theme = %1\n"_s.arg(themeCode(theme));
    out += u"unit = %1\n"_s.arg(unitCode(unit));
    out += u"channel = %1\n"_s.arg(channelCode(channel));
    out += u"band = %1\n"_s.arg(band);
    out += u"ui_zoom = %1\n"_s.arg(shortest(uiZoom));
    out += u"last_dir = %1\n"_s.arg(QDir::toNativeSeparators(lastDir));
    if (windowSize)
        out += u"window_width = %1\nwindow_height = %2\n"_s.arg(windowSize->width()).arg(windowSize->height());
    out += u"maximized = %1\n"_s.arg(maximized ? u"true"_s : u"false"_s);
    return out;
}
