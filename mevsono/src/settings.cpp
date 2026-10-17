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

const QString FILE_NAME = u"mevsono.ini"_s;

std::optional<double> parseDouble(const QString &s)
{
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok && std::isfinite(v) ? std::optional(v) : std::nullopt;
}

/// Entier positif ou nul (comme `parse::<usize>` en Rust : pas de signe « - »).
std::optional<qulonglong> parseCount(const QString &s)
{
    bool ok = false;
    const qulonglong v = s.toULongLong(&ok);
    return ok && !s.startsWith(u'-') ? std::optional(v) : std::nullopt;
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
    const QString old = base.isEmpty() ? QString() : QDir(base).filePath(u"MeVSono"_s);
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() && !qEnvironmentVariable("HOME").isEmpty())
        base = QDir(qEnvironmentVariable("HOME")).filePath(u".config"_s);
    const QString old = base.isEmpty() ? QString() : QDir(base).filePath(u"mevsono"_s);
#endif
    if (old.isEmpty() || !QFileInfo(old).isDir() || QDir(old) == QDir(dir))
        return;
    QDir().mkpath(dir);
    for (const QString &name : {FILE_NAME, u"mevsono.log"_s}) {
        const QString from = QDir(old).filePath(name), to = QDir(dir).filePath(name);
        if (QFileInfo(from).isFile() && !QFileInfo(to).exists())
            QFile::rename(from, to);
    }
    QDir().rmdir(old);
}

QString flagText(bool value)
{
    return value ? u"true"_s : u"false"_s;
}

QString themeCode(Qt::ColorScheme theme)
{
    switch (theme) {
    case Qt::ColorScheme::Light: return u"light"_s;
    case Qt::ColorScheme::Dark: return u"dark"_s;
    default: return u"auto"_s;
    }
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
    const QString admin = QDir(QCoreApplication::applicationDirPath()).filePath(FILE_NAME);
    if (QFileInfo(admin).isFile())
        files.admin = admin;
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
    const auto flag = [](const QString &v, bool old) { return v == u"true" ? true : v == u"false" ? false : old; };
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
            for (const Qt::ColorScheme t : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
                if (value == themeCode(t))
                    theme = t;
            }
        } else if (key == u"mode") {
            expert = value == u"expert" ? true : value == u"simple" ? false : expert;
        } else if (key == u"channel") {
            channel = audio::channelFromCode(value).value_or(channel);
        } else if (key == u"window") {
            window = dsp::windowFromCode(value).value_or(window);
        } else if (key == u"fft_size") {
            const auto size = value == u"auto" ? std::optional<qulonglong>(0) : parseCount(value);
            if (size && std::ranges::find(dsp::FFT_SIZES, size_t(*size)) != dsp::FFT_SIZES.end())
                fftSize = size_t(*size);
        } else if (key == u"amplitude_db") {
            db = flag(value, db);
        } else if (key == u"log_frequency") {
            logFreq = flag(value, logFreq);
        } else if (key == u"max_frequency") {
            const auto f = value == u"auto" ? std::optional(0.0) : parseDouble(value);
            if (f && std::ranges::find(FMAX_CHOICES, *f) != FMAX_CHOICES.end())
                fmax = *f;
        } else if (key == u"peak_threshold_db") {
            if (const auto v = parseDouble(value); v && *v >= -100.0 && *v <= -10.0)
                thresholdDb = *v;
        } else if (key == u"harmonics") {
            if (const auto n = parseCount(value))
                harmonics = int(std::clamp<qulonglong>(*n, 1, 40));
        } else if (key == u"snap_zero") {
            snapZero = flag(value, snapZero);
        } else if (key == u"harmonic_marks") {
            harmonicMarks = flag(value, harmonicMarks);
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
            maximized = flag(value, maximized);
        }
    }
    if (width && height && *width >= 400.0 && *width <= 20000.0 && *height >= 300.0 && *height <= 20000.0)
        windowSize = QSize(int(std::lround(*width)), int(std::lround(*height)));
}

QString Settings::toText() const
{
    QString out = u"# MeVSono — réglages / settings\n"
                  u"# language : auto, fr, en\n"
                  u"# theme : auto, light, dark\n"
                  u"# mode : simple, expert\n"
                  u"# channel : mix, left, right\n"
                  u"# window : rectangular, hann, hamming, blackman, blackman-harris, flat-top\n"
                  u"# fft_size : auto, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144\n"
                  u"# max_frequency : 1000, 2000, 5000, 10000, 15000, 20000, auto\n"_s;
    out += u"language = %1\n"_s.arg(lang ? langCode(*lang) : u"auto"_s);
    out += u"theme = %1\n"_s.arg(themeCode(theme));
    out += u"mode = %1\n"_s.arg(expert ? u"expert"_s : u"simple"_s);
    out += u"channel = %1\n"_s.arg(audio::channelCode(channel));
    out += u"window = %1\n"_s.arg(dsp::windowCode(window));
    out += u"fft_size = %1\n"_s.arg(fftSize == 0 ? u"auto"_s : QString::number(fftSize));
    out += u"amplitude_db = %1\n"_s.arg(flagText(db));
    out += u"log_frequency = %1\n"_s.arg(flagText(logFreq));
    out += u"max_frequency = %1\n"_s.arg(fmax > 0.0 ? shortest(fmax) : u"auto"_s);
    out += u"peak_threshold_db = %1\n"_s.arg(shortest(thresholdDb));
    out += u"harmonics = %1\n"_s.arg(harmonics);
    out += u"snap_zero = %1\n"_s.arg(flagText(snapZero));
    out += u"harmonic_marks = %1\n"_s.arg(flagText(harmonicMarks));
    out += u"ui_zoom = %1\n"_s.arg(shortest(uiZoom));
    out += u"last_dir = %1\n"_s.arg(QDir::toNativeSeparators(lastDir));
    if (windowSize)
        out += u"window_width = %1\nwindow_height = %2\n"_s.arg(windowSize->width()).arg(windowSize->height());
    out += u"maximized = %1\n"_s.arg(flagText(maximized));
    return out;
}
