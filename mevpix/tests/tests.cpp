// Tests du code sans interface, repris des tests de la version Rust.

#include "help.h"
#include "i18n.h"
#include "model.h"
#include "profile.h"
#include "session.h"
#include "settings.h"
#include "view.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

bool near(double a, double b, double eps = 1e-6)
{
    return std::abs(a - b) < eps;
}

/// Image dont chaque pixel est donné par `f(x, y)`.
QImage makeImage(int w, int h, const std::function<QColor(int, int)> &f)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            img.setPixelColor(x, y, f(x, y));
    }
    return img;
}

/// Image dont la composante rouge vaut x (dégradé horizontal), vert = y, bleu = 0 (modulo 256).
QImage gradient(int w, int h)
{
    return makeImage(w, h, [](int x, int y) { return QColor(x & 0xff, y & 0xff, 0); });
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

} // namespace

class Tests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ model
    void segmentGeometry()
    {
        const Segment s{{0, 0}, {3, 4}};
        QCOMPARE(s.lenPx(), 5.0);
        QCOMPARE(s.pointAt(0.5), QPointF(1.5, 2.0));
        const QPointF n = s.normal();
        QVERIFY(near(length(n), 1.0));
        QVERIFY(near(QPointF::dotProduct(n, s.b - s.a), 0.0));
        QVERIFY(near(s.project(QPointF(1.5, 2.0) + n * 3.0), 0.5));
        QCOMPARE(s.project(QPointF(-10, 0)), 0.0);
        QCOMPARE(s.project(QPointF(30, 40)), 1.0);
    }

    void snapping()
    {
        QCOMPARE(snapAxis(QPointF(0, 0), QPointF(10, 2)), QPointF(10, 0));
        QCOMPARE(snapAxis(QPointF(0, 0), QPointF(-1, 7)), QPointF(0, 7));
    }

    void calibration()
    {
        // 10 mm sur 250 px → 0,04 mm/px
        const auto c = Calibration::make(250.0, 10.0, Unit::Millimetre);
        QVERIFY(c);
        QVERIFY(near(c->unitsPerPx(), 0.04, 1e-12));
        QVERIFY(near(c->toReal(500.0), 20.0, 1e-9));
        QCOMPARE(fmtLen(311.0, c, u','), u"12,4 mm"_s);
        QCOMPARE(fmtLen(311.0, c, u'.'), u"12.4 mm"_s);
        QCOMPARE(fmtLen(311.0, std::nullopt, u','), u"311 px"_s);
        QVERIFY(!Calibration::make(0.5, 10.0, Unit::Millimetre));
        QVERIFY(!Calibration::make(100.0, 0.0, Unit::Millimetre));
        QVERIFY(!Calibration::make(100.0, std::nan(""), Unit::Millimetre));
    }

    void calibrationMatches()
    {
        const auto c = *Calibration::make(250.0, 10.0, Unit::Millimetre);
        QVERIFY(c.matches(*Calibration::make(250.3, 10.0, Unit::Millimetre)));
        QVERIFY(!c.matches(*Calibration::make(251.0, 10.0, Unit::Millimetre)));
        QVERIFY(!c.matches(*Calibration::make(250.0, 20.0, Unit::Millimetre)));
        QVERIFY(!c.matches(*Calibration::make(250.0, 10.0, Unit::Centimetre)));
        // étalon court : 1 px porté à 1,4 px change l'échelle de 40 % : à revalider
        const auto shortRef = *Calibration::make(1.0, 1.0, Unit::Millimetre);
        QVERIFY(!shortRef.matches(*Calibration::make(1.4, 1.0, Unit::Millimetre)));
    }

    void measuresAsTable()
    {
        const auto c = Calibration::make(250.0, 10.0, Unit::Millimetre);
        QCOMPARE(measuresTable({311.0, 50.0}, c, u',', {u"Mesure"_s, u"Longueur (mm)"_s}),
                 u"Mesure\tLongueur (mm)\nM1\t12,4\nM2\t2,00\n"_s);
        QCOMPARE(measuresTable({311.0}, std::nullopt, u'.', {u"M"_s, u"L (px)"_s}), u"M\tL (px)\nM1\t311\n"_s);
        QCOMPARE(measuresTable({}, std::nullopt, u',', {u"M"_s, u"L"_s}), u"M\tL\n"_s);
    }

    void numberFormatting()
    {
        QCOMPARE(fmtNum(12.437, 3, u','), u"12,4"_s);
        QCOMPARE(fmtNum(0.04234, 3, u','), u"0,0423"_s);
        QCOMPARE(fmtNum(0.04234, 3, u'.'), u"0.0423"_s);
        QCOMPARE(fmtNum(1234.0, 3, u','), u"1234"_s);
        QCOMPARE(fmtNum(0.0, 3, u','), u"0"_s);
        QCOMPARE(fmtNum(-2.5, 3, u','), u"-2,50"_s);
        QCOMPARE(sigDecimals(6.98, 3), 2);
        QCOMPARE(fmtDec(3.4891, 2, u','), u"3,49"_s);
        QCOMPARE(fmtDec(3.4891, 2, u'.'), u"3.49"_s);
        // L'arrondi change la puissance de 10 : toujours 3 chiffres significatifs.
        QCOMPARE(fmtNum(9.996, 3, u','), u"10,0"_s);
        QCOMPARE(fmtNum(0.09996, 3, u','), u"0,100"_s);
        QCOMPARE(fmtNum(-9.996, 3, u','), u"-10,0"_s);
        QCOMPARE(fmtNum(999.7, 3, u','), u"1000"_s);
        QCOMPARE(fmtNum(9.94, 3, u','), u"9,94"_s);
        QCOMPARE(sigDecimals(9.996, 3), 1);
        QCOMPARE(shortest(0.1), u"0.1"_s);
        QCOMPARE(shortest(2.0), u"2"_s);
        QCOMPARE(shortest(0.0000012), u"0.0000012"_s);
    }

    void numberParsing()
    {
        QCOMPARE(parseNum(u" 2,5 "_s), std::optional(2.5));
        QCOMPARE(parseNum(u"10"_s), std::optional(10.0));
        QCOMPARE(parseNum(u"+3"_s), std::optional(3.0));
        QCOMPARE(parseNum(u"abc"_s), std::nullopt);
        QCOMPARE(parseNum(u"inf"_s), std::nullopt);
        QCOMPARE(parseNum(u""_s), std::nullopt);
        QCOMPARE(parseNum(u"10 mm"_s), std::nullopt);
    }

    // ------------------------------------------------------------ profile
    void horizontalProfileFollowsGradient()
    {
        const QImage img = gradient(100, 50);
        // Le long des centres de pixels de la ligne 10, de la colonne 10 à la colonne 60.
        const Segment seg{{10.5, 10.5}, {60.5, 10.5}};
        const Profile p = sampleProfile(img, seg, 1, Channel::Red);
        QCOMPARE(p.values.size(), size_t(51));
        QVERIFY(near(p.stepPx, 1.0));
        for (size_t k = 0; k < p.values.size(); ++k)
            QVERIFY2(near(p.values[k], double(10 + k), 1e-3), qPrintable(u"k=%1 v=%2"_s.arg(k).arg(p.values[k])));
        QCOMPARE(p.saturated, 0);
    }

    void obliqueProfileInterpolates()
    {
        const QImage img = gradient(100, 100);
        const Segment seg{{10.5, 20.5}, {40.5, 60.5}}; // longueur 50
        const Profile p = sampleProfile(img, seg, 1, Channel::Green);
        QCOMPARE(p.values.size(), size_t(51));
        QVERIFY(near(p.values[0], 20.0, 1e-3));
        QVERIFY(near(p.values[25], 40.0, 1e-3));
        QVERIFY(near(p.values[50], 60.0, 1e-3));
    }

    void bandAveragesPerpendicular()
    {
        // Rayures horizontales : lignes paires noires, impaires blanches.
        const QImage img = makeImage(50, 50, [](int, int y) { return y % 2 == 0 ? QColor(0, 0, 0) : QColor(200, 200, 200); });
        const Segment seg{{5.5, 20.5}, {45.5, 20.5}};
        const Profile thin = sampleProfile(img, seg, 1, Channel::Red);
        QVERIFY(std::ranges::all_of(thin.values, [](double v) { return v < 1e-3; }));
        const Profile wide = sampleProfile(img, seg, 10, Channel::Red);
        QVERIFY(std::ranges::all_of(wide.values, [](double v) { return near(v, 100.0, 1e-3); }));
    }

    void luminanceAndSaturation()
    {
        // 5 pixels rouges saturés, 5 blancs, 10 gris.
        const QImage img = makeImage(20, 1, [](int x, int) {
            return x < 5 ? QColor(255, 0, 0) : x < 10 ? QColor(255, 255, 255) : QColor(100, 100, 100);
        });
        const Segment seg{{0.5, 0.5}, {19.5, 0.5}};
        const Profile lum = sampleProfile(img, seg, 1, Channel::Luminance);
        QVERIFY(near(lum.values[0], 0.299 * 255.0, 1e-3));
        QVERIFY(near(lum.values[19], 100.0, 1e-3));
        // Le rouge écrêté compte aussi : la luminance réelle y est plus grande que 0,299 × 255.
        QCOMPARE(lum.saturated, 10);
        QCOMPARE(sampleProfile(img, seg, 1, Channel::Red).saturated, 10);
        QCOMPARE(sampleProfile(img, seg, 1, Channel::Green).saturated, 5); // seulement le blanc
    }

    void saturationAnywhereInBand()
    {
        // Ligne 12 blanche, le reste gris : la coupe passe en ligne 10, la bande de 5 px touche la ligne 12.
        const QImage img =
            makeImage(30, 30, [](int, int y) { return y == 12 ? QColor(255, 255, 255) : QColor(100, 100, 100); });
        const Segment seg{{5.5, 10.5}, {25.5, 10.5}};
        QCOMPARE(sampleProfile(img, seg, 1, Channel::Luminance).saturated, 0);
        QCOMPARE(sampleProfile(img, seg, 5, Channel::Luminance).saturated, 21);
    }

    void saturationOfInterpolatedNeighbours()
    {
        // Colonne 0 rouge écrêtée, colonne 1 noire ; en x = 1,1 le pixel le plus proche est noir, mais
        // la valeur interpolée prend 40 % du rouge : la coupe est signalée saturée.
        const QImage img = makeImage(2, 20, [](int x, int) { return x == 0 ? QColor(255, 0, 0) : QColor(0, 0, 0); });
        const Segment seg{{1.1, 0.5}, {1.1, 19.5}};
        const Profile red = sampleProfile(img, seg, 1, Channel::Red);
        QVERIFY(near(red.values[0], 102.0, 1e-3));
        QCOMPARE(red.saturated, 20);
        QCOMPARE(sampleProfile(img, Segment{{1.5, 0.5}, {1.5, 19.5}}, 1, Channel::Red).saturated, 0);
    }

    void valueAtPicksNearestSample()
    {
        const Profile p{1.0, {0.0, 10.0, 20.0}, 0};
        QCOMPARE(p.valueAt(0.0), 0.0);
        QCOMPARE(p.valueAt(0.6), 10.0);
        QCOMPARE(p.valueAt(2.0), 20.0);
        QCOMPARE(p.lenPx(), 2.0);
    }

    void csvFormat()
    {
        const Profile p{2.0, {12.0, 34.567}, 0};
        QCOMPARE(toCsv(p, std::nullopt, u','), u"x (px);I\n0;12,00\n2;34,57\n"_s);
        const auto c = Calibration::make(100.0, 5.0, Unit::Millimetre);
        QCOMPARE(toCsv(p, c, u','), u"x (mm);I\n0;12,00\n0,1;34,57\n"_s);
        QCOMPARE(toCsv(p, c, u'.'), u"x (mm),I\n0,12.00\n0.1,34.57\n"_s);
    }

    void csvExtensionAddedWhenMissing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(withCsvExtension(dir.filePath(u"profil"_s)), dir.filePath(u"profil.csv"_s));
        QCOMPARE(withCsvExtension(dir.filePath(u"profil.csv"_s)), dir.filePath(u"profil.csv"_s));
        QCOMPARE(withCsvExtension(dir.filePath(u"profil.txt"_s)), dir.filePath(u"profil.txt"_s));
        // un fichier existant garde son extension (le programme demande avant de le remplacer)
        writeFile(dir.filePath(u"ancien.csv"_s), "x");
        QCOMPARE(withCsvExtension(dir.filePath(u"ancien"_s)), dir.filePath(u"ancien.csv"_s));
    }

    // ------------------------------------------------------------ settings
    void settingsRoundTrip()
    {
        Settings s;
        s.lang = Lang::En;
        s.theme = Qt::ColorScheme::Dark;
        s.unit = Unit::Micrometre;
        s.channel = Channel::Red;
        s.band = 12;
        s.uiZoom = 1.2;
        s.lastDir = u"/home/élève/TP = diffraction"_s;
        s.windowSize = QSize(1400, 900);
        s.maximized = true;
        QVERIFY(Settings::parse(s.toText()) == s);
        QVERIFY(Settings::parse(Settings().toText()) == Settings());
        QVERIFY(s.toText().contains(u"ui_zoom = 1.2\n"_s));
        QVERIFY(s.toText().contains(u"theme = dark\n"_s));
        QVERIFY(Settings().toText().contains(u"theme = auto\n"_s));
        QCOMPARE(Settings::parse(u"theme = light\n"_s).theme, Qt::ColorScheme::Light);
    }

    void settingsTolerateBadInput()
    {
        const Settings s = Settings::parse(u"n'importe quoi\nlanguage = klingon\ntheme = rose\nband = 999\nunit = pouce\nui_zoom = 40\n"
                                           u"window_width = 10\nwindow_height = 600\n  channel = blue  \n"_s);
        QCOMPARE(s.lang, std::nullopt);
        QCOMPARE(s.theme, Qt::ColorScheme::Unknown);
        QCOMPARE(s.band, 50);
        QCOMPARE(s.unit, Unit::Millimetre);
        QCOMPARE(s.uiZoom, 1.0);
        QCOMPARE(s.windowSize, std::nullopt);
        QCOMPARE(s.channel, Channel::Blue);
    }

    void adminFileOverridesUserFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString user = dir.filePath(u"user.ini"_s), admin = dir.filePath(u"admin.ini"_s);
        writeFile(user, "language = en\nunit = cm\nband = 12\n");
        writeFile(admin, "# imposé par le lycée\nlanguage = fr\n");

        const Settings s = Settings::load({user, admin});
        QCOMPARE(s.lang, std::optional(Lang::Fr)); // imposé
        QCOMPARE(s.unit, Unit::Centimetre);        // choix de l'utilisateur conservé
        QCOMPARE(s.band, 12);

        QCOMPARE(Settings::load({user, QString()}).lang, std::optional(Lang::En));
        QVERIFY(Settings::load({dir.filePath(u"absent.ini"_s), QString()}) == Settings());
    }

    void settingsSaveCreatesDirectory()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"a/b/mevpix.ini"_s);
        Settings s;
        s.band = 7;
        QCOMPARE(s.save(path), QString());
        QCOMPARE(Settings::load({path, QString()}).band, 7);
    }

    // ------------------------------------------------------------ i18n
    void languageCodes()
    {
        QCOMPARE(langFromCode(u"fr_FR.UTF-8"_s), std::optional(Lang::Fr));
        QCOMPARE(langFromCode(u"en-GB"_s), std::optional(Lang::En));
        QCOMPARE(langFromCode(u"FR"_s), std::optional(Lang::Fr));
        QCOMPARE(langFromCode(u"de_DE"_s), std::nullopt);
        QCOMPARE(langFromCode(u"C"_s), std::nullopt);
        QCOMPARE(langFromCode(u"auto"_s), std::nullopt);
        QCOMPARE(langFromCode(u"ru_RU"_s), std::nullopt);
        for (Lang lang : ALL_LANGS)
            QCOMPARE(langFromCode(langCode(lang)), std::optional(lang));
        // Plusieurs langues préférées : la première traduite l'emporte.
        QCOMPARE(firstTranslated({u"ru"_s, u"it"_s, u"en"_s}), std::optional(Lang::En));
        QCOMPARE(firstTranslated({}), std::nullopt);
        QCOMPARE(resolveLang(Lang::Fr), Lang::Fr);
    }

    void numberConventions()
    {
        QCOMPARE(texts(Lang::En).decimal, u'.');
        for (Lang lang : ALL_LANGS) {
            if (lang != Lang::En)
                QCOMPARE(texts(lang).decimal, u',');
        }
    }

    // ------------------------------------------------------------ notice
    void helpWritesEveryFileWithVersion()
    {
        QTemporaryDir dir;
        const SettingsFiles files{dir.filePath(u"mevpix.ini"_s), QString()};
        // pages refaites à chaque ouverture : dans le dossier temporaire, pas dans celui des réglages
        const QString helpDir = QDir(QDir::tempPath()).filePath(help::DIR_NAME);
        QString error;
        const QString page = help::write(Lang::En, files, &error);
        QCOMPARE(error, QString());
        QCOMPARE(page, QDir(helpDir).filePath(u"en.html"_s));
        QCOMPARE(help::write(Lang::Fr, files, &error), QDir(helpDir).filePath(u"fr.html"_s));
        for (const QString &name : help::files())
            QVERIFY2(QFile::exists(QDir(helpDir).filePath(name)), qPrintable(name));
        QFile file(page);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString html = QString::fromUtf8(file.readAll());
        QVERIFY(html.contains(QStringLiteral(MEVPIX_VERSION)) && !html.contains(u"{{version}}"_s));
        QVERIFY(html.contains(u"help.css"_s) && html.contains(u"fenetre-en.jpg"_s));
    }

    /// Chaque page de la notice : images, liens et ancres existent, et la page n'utilise que les images
    /// de sa langue.
    void helpLinksAndImagesExist()
    {
        QTemporaryDir dir;
        QString error;
        const QString page = help::write(Lang::Fr, SettingsFiles{dir.filePath(u"mevpix.ini"_s), QString()}, &error);
        QVERIFY2(!page.isEmpty(), qPrintable(error));
        const QDir site = QFileInfo(page).dir();
        const QRegularExpression attribute(u"(?:src|href)=\"([^\"]+)\""_s);
        for (Lang lang : ALL_LANGS) {
            QFile file(site.filePath(langCode(lang) + u".html"_s));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString html = QString::fromUtf8(file.readAll());
            for (const auto &match : attribute.globalMatch(html)) {
                const QString target = match.captured(1);
                if (target.startsWith(u"#"_s))
                    QVERIFY2(html.contains(u"id=\"%1\""_s.arg(target.mid(1))), qPrintable(target));
                else if (!target.startsWith(u"mailto:"_s) && !target.startsWith(u"http"_s))
                    QVERIFY2(QFile::exists(site.filePath(target)), qPrintable(target));
                if (target.endsWith(u".jpg"_s))
                    QVERIFY2(target.endsWith(u"-"_s + langCode(lang) + u".jpg"_s), qPrintable(target));
            }
        }
    }

    // ------------------------------------------------------------ images et état
    void loadsImageAndRejectsOtherFiles()
    {
        QTemporaryDir dir;
        const QString png = dir.filePath(u"degrade.png"_s);
        QVERIFY(gradient(300, 200).save(png));
        const LoadResult ok = loadImage(png);
        QVERIFY(ok.image);
        QCOMPARE(ok.image->name, u"degrade.png"_s);
        QCOMPARE(ok.image->rgb.format(), QImage::Format_RGB888);
        QCOMPARE(ok.image->rgb.size(), QSize(300, 200));
        QCOMPARE(ok.image->rgb.pixelColor(42, 17), QColor(42, 17, 0));
        QVERIFY(!ok.image->levels.empty());

        const QString text = dir.filePath(u"pas-une-image.png"_s);
        writeFile(text, "ceci n'est pas une image");
        const LoadResult bad = loadImage(text);
        QVERIFY(!bad.image);
        QVERIFY(bad.unsupported);
    }

    void sessionCalibrationState()
    {
        Session s;
        s.calibSeg = Segment{{0, 0}, {250, 0}};
        s.calibInput = u"10"_s;
        const auto c = s.calibFromInputs();
        QVERIFY(c && near(c->unitsPerPx(), 0.04));
        QVERIFY(!s.calibPending());
        s.calibration = c;
        QVERIFY(!s.calibPending());
        s.calibInput = u"20"_s; // longueur changée sans valider
        QVERIFY(s.calibPending());
        s.calibInput = u"10"_s;
        s.calibUnit = Unit::Centimetre; // unité changée sans valider
        QVERIFY(s.calibPending());
        QVERIFY(s.hasWork());
    }

    void cutAloneIsWork()
    {
        // une coupe tracée sans étalon ni mesure est aussi du travail à protéger
        Session s;
        QVERIFY(!s.hasWork());
        s.cut = Segment{{10.5, 40.5}, {110.5, 40.5}};
        QVERIFY(s.hasWork());
    }
};

QTEST_GUILESS_MAIN(Tests)
#include "tests.moc"
