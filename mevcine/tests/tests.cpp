// Tests du code sans interface.

#include "calculs.h"
#include "chemins.h"
#include "detect.h"
#include "export.h"
#include "format.h"
#include "hdr.h"
#include "i18n.h"
#include "outils.h"
#include "pointage.h"
#include "prefs.h"
#include "seance.h"
#include "video.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <functional>
#include <tuple>

namespace {

bool proche(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) < eps;
}

/// lecture d'un texte en mémoire, comme la sortie d'un programme
Lecture lecture_de(QByteArray &donnees, qsizetype &pos)
{
    return [&donnees, &pos](char *dest, qint64 n) -> qint64 {
        const qint64 k = std::min<qint64>(n, donnees.size() - pos);
        std::memcpy(dest, donnees.constData() + pos, k);
        pos += k;
        return k;
    };
}

/// une image de bruit pseudo-aléatoire reproductible
QImage bruit(int w, int h, uint32_t graine)
{
    QImage img(w, h, QImage::Format_RGB888);
    uint32_t etat = graine;
    for (int y = 0; y < h; ++y) {
        uchar *l = img.scanLine(y);
        for (int x = 0; x < 3 * w; ++x) {
            etat = etat * 1103515245u + 12345u;
            l[x] = static_cast<uchar>(etat >> 16);
        }
    }
    return img;
}

/// une grande image sans détails fins, comme une image de vidéo, avec un objet (un disque dégradé)
/// au coin haut-gauche (ox, oy)
QImage grande_image(int w, int h, QPoint o, int rayon)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        uchar *l = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const double dx = x - double(o.x() + rayon), dy = y - double(o.y() + rayon);
            const double d = std::sqrt(dx * dx + dy * dy) / rayon;
            uchar *px = l + 3 * x;
            if (d < 1.0) {
                const auto v = static_cast<uchar>(255.0 * (1.0 - d));
                px[0] = v;
                px[1] = static_cast<uchar>(255 - v / 2);
                px[2] = 40;
            } else {
                const double fond = 120.0 + 60.0 * std::sin(x / 97.0) * std::cos(y / 61.0) + 30.0 * std::sin((x + y) / 233.0);
                px[0] = static_cast<uchar>(fond);
                px[1] = static_cast<uchar>(fond * 0.8);
                px[2] = static_cast<uchar>(255.0 - fond);
            }
        }
    }
    return img;
}

/// écart moyen entre deux images, par composante (de 0 à 255)
double ecart_moyen(const QImage &a, const QImage &b)
{
    double somme = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar *p = a.constScanLine(y), *q = b.constScanLine(y);
        for (int x = 0; x < 3 * a.width(); ++x)
            somme += std::abs(int(p[x]) - int(q[x]));
    }
    return somme / (3.0 * a.width() * a.height());
}

/// code 16 bits d'une valeur de 0 à 1
uint16_t code(double e)
{
    return static_cast<uint16_t>(std::round(e * 65535.0));
}

/// chute libre simulée : y = -g t²/2, en pixels à 100 px/m
Pointage chute()
{
    Pointage p;
    p.alloue_mesures(1, 0.1, 8);
    p.origine = {0, 0};
    p.echelle = {{0, 0}, {0, 100}, 1.0};
    // le pointage commence à la troisième image
    for (int i = 2; i < 8; ++i) {
        const double t = (i - 2) * 0.1;
        // l'axe y de l'image va vers le bas
        p.memorise_point(1, {50.0 * t, 100.0 * 0.5 * 10.0 * t * t}, i);
    }
    return p;
}

/// deux objets, pointés sur les images 2 à 4 ; le second manque sur la dernière image
Pointage exemple_export(int nb_obj)
{
    Pointage p;
    p.alloue_mesures(nb_obj, 0.04, 6);
    p.origine = {0, 100};
    p.echelle = {{0, 0}, {100, 0}, 1.0};
    for (int i = 1; i < 4; ++i) {
        p.memorise_point(1, {10.0 * i, 50}, i);
        if (nb_obj > 1)
            p.memorise_point(2, {5, 100.0 - i}, i);
    }
    p.memorise_point(1, {40, 50}, 4);
    return p;
}

// --------------------------------------------------------------------------------------------
// traductions : les textes des sources
// --------------------------------------------------------------------------------------------

/// La valeur d'une suite de chaînes littérales C++ (« "a" "b" », concaténées) qui commence au
/// guillemet ouvrant `debut` ; rien si elle n'est pas fermée.
std::optional<QString> litteral(const QString &code, qsizetype debut)
{
    QString res;
    qsizetype i = debut;
    while (i < code.size() && code[i] == u'"') {
        ++i;
        bool ferme = false;
        while (i < code.size()) {
            const QChar c = code[i++];
            if (c == u'"') {
                ferme = true;
                break;
            }
            if (c != u'\\') {
                res += c;
                continue;
            }
            if (i >= code.size())
                return std::nullopt;
            const QChar e = code[i++];
            switch (e.unicode()) {
            case 'n': res += u'\n'; break;
            case 't': res += u'\t'; break;
            case 'r': res += u'\r'; break;
            case '0': res += QChar(0); break;
            default: res += e;
            }
        }
        if (!ferme)
            return std::nullopt;
        // chaîne suivante, collée à la précédente
        while (i < code.size() && code[i].isSpace())
            ++i;
    }
    return res;
}

/// Les textes à traduire : le premier argument de chaque `trad("…")` des sources, avec le fichier
/// et la ligne de sa première apparition
QList<std::pair<QString, QString>> textes_a_traduire()
{
    const QDir racine(QString::fromUtf8(MEVCINE_SOURCES));
    QStringList fichiers;
    QDirIterator it(racine.filePath(u"src"_s), {u"*.cpp"_s, u"*.h"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        fichiers << it.next();
    fichiers.sort();
    QList<std::pair<QString, QString>> textes;
    for (const QString &f : fichiers) {
        QFile fichier(f);
        if (!fichier.open(QIODevice::ReadOnly))
            continue;
        const QString code = QString::fromUtf8(fichier.readAll());
        const QString nom = racine.relativeFilePath(f);
        qsizetype pos = 0;
        while ((pos = code.indexOf(u"trad("_s, pos)) >= 0) {
            const qsizetype debut_ligne = code.lastIndexOf(u'\n', pos) + 1;
            const QString avant = code.mid(debut_ligne, pos - debut_ligne);
            const int ligne = static_cast<int>(code.left(pos).count(u'\n')) + 1;
            pos += 5;
            // ni les commentaires, ni les autres fonctions dont le nom finit par « trad »
            const bool colle = !avant.isEmpty() && (avant.back().isLetterOrNumber() || avant.back() == u'_');
            if (colle || avant.trimmed().startsWith(u"//"_s))
                continue;
            qsizetype q = pos;
            while (q < code.size() && code[q].isSpace())
                ++q;
            if (q >= code.size() || code[q] != u'"')
                continue;
            const auto texte = litteral(code, q);
            if (!texte) {
                qWarning("%s:%d : chaîne non fermée", qUtf8Printable(nom), ligne);
                continue;
            }
            const bool deja = std::any_of(textes.begin(), textes.end(), [&](const auto &t) { return t.first == *texte; });
            if (!deja)
                textes.append({*texte, u"%1:%2"_s.arg(nom).arg(ligne)});
        }
    }
    return textes;
}

/// les marques d'arguments d'un texte : nombre de `{}`, et numéros `{n}`
std::pair<int, QList<int>> marques(const QString &s)
{
    int anonymes = 0;
    QList<int> numeros;
    qsizetype pos = 0;
    while (true) {
        const qsizetype i = s.indexOf(u'{', pos);
        if (i < 0)
            break;
        const qsizetype fin = s.indexOf(u'}', i + 1);
        if (fin < 0)
            break;
        const QString interieur = s.mid(i + 1, fin - i - 1);
        bool ok = false;
        const int n = interieur.toInt(&ok);
        if (interieur.isEmpty())
            ++anonymes;
        else if (ok)
            numeros << n;
        pos = fin + 1;
    }
    std::sort(numeros.begin(), numeros.end());
    return {anonymes, numeros};
}

/// une chaîne au format des fichiers .po
QString ecrit_chaine_po(const QString &s)
{
    const auto echappe = [](QString t) {
        return t.replace(u"\\"_s, u"\\\\"_s).replace(u"\""_s, u"\\\""_s).replace(u"\t"_s, u"\\t"_s);
    };
    QString sans_fin = s;
    while (sans_fin.endsWith(u'\n'))
        sans_fin.chop(1);
    if (!sans_fin.contains(u'\n'))
        return u"\""_s + echappe(s).replace(u"\n"_s, u"\\n"_s) + u"\""_s;
    QString res = u"\"\""_s;
    qsizetype pos = 0;
    while (pos < s.size()) {
        qsizetype fin = s.indexOf(u'\n', pos);
        fin = fin < 0 ? s.size() : fin + 1;
        res += u"\n\""_s + echappe(s.mid(pos, fin - pos)).replace(u"\n"_s, u"\\n"_s) + u"\""_s;
        pos = fin;
    }
    return res;
}

/// les valeurs d'un attribut (« src="… » ou « href="… ») dans une page
QStringList attributs(const QString &page, const QString &prefixe)
{
    QStringList res;
    const QStringList morceaux = page.split(prefixe);
    for (qsizetype i = 1; i < morceaux.size(); ++i)
        res << morceaux[i].section(u'"', 0, 0);
    return res;
}

QString lit_texte(const QString &chemin)
{
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

class Tests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        i18n::choisit(Langue::Francais);
    }

    // ------------------------------------------------------------ format
    void format_g()
    {
        QCOMPARE(texte_significatif(0.0, 4), u"0"_s);
        QCOMPARE(texte_significatif(1.0, 4), u"1"_s);
        QCOMPARE(texte_significatif(0.123456, 4), u"0.1235"_s);
        QCOMPARE(texte_significatif(123456.0, 4), u"1.235e+05"_s);
        QCOMPARE(texte_significatif(1234.0, 4), u"1234"_s);
        QCOMPARE(texte_significatif(0.0001, 4), u"0.0001"_s);
        QCOMPARE(texte_significatif(0.00001234, 4), u"1.234e-05"_s);
        QCOMPARE(texte_significatif(-2.5, 6), u"-2.5"_s);
        QCOMPARE(texte_significatif(0.30000000000000004, 6), u"0.3"_s);
    }

    void analyse_flottants()
    {
        QCOMPARE(lit_reel(u"1,5"_s), 1.5);
        QCOMPARE(lit_reel(u" .5 "_s), 0.5);
        QCOMPARE(lit_reel(u"3."_s), 3.0);
        QCOMPARE(lit_reel(u"-2e-3"_s), -0.002);
        QCOMPARE(lit_reel(u"abc"_s), std::nullopt);
        QCOMPARE(lit_reel(u""_s), std::nullopt);
        QCOMPARE(lit_reel(u"."_s), std::nullopt);
        QCOMPARE(lit_reel(u"inf"_s), std::nullopt);
        QCOMPARE(lit_reel(u"1 2"_s), std::nullopt);
    }

    void longueurs_acceptees()
    {
        // la saisie, l'unité choisie dans la liste, la longueur en mètre attendue
        const std::tuple<QString, Unite, double> cas[] = {
            {u"0,85"_s, Unite::Metre, 0.85},           {u"0.85 m"_s, Unite::Metre, 0.85},
            {u" 0,85m "_s, Unite::Metre, 0.85},        {u"1E10m"_s, Unite::Metre, 1e10},
            {u"1E-10m"_s, Unite::Metre, 1e-10},        {u"1e-10 m"_s, Unite::Metre, 1e-10},
            {u"E-10m"_s, Unite::Metre, 1e-10},         {u"E10m"_s, Unite::Metre, 1e10},
            {u"8,5E-1"_s, Unite::Metre, 0.85},         {u"1e+3"_s, Unite::Metre, 1000.0},
            {u"2,5×10^-3 m"_s, Unite::Metre, 2.5e-3},  {u"3x10^2"_s, Unite::Metre, 300.0},
            {u"1,5*10**-2"_s, Unite::Metre, 1.5e-2},   {u"10^-3"_s, Unite::Metre, 1e-3},
            {u"1,20 mètre"_s, Unite::Metre, 1.2},      {u"0.85 meter"_s, Unite::Metre, 0.85},
            {u"2 Meter"_s, Unite::Metre, 2.0},         {u"1,5 metros"_s, Unite::Metre, 1.5},
            // l'unité de la liste
            {u"85"_s, Unite::Centimetre, 0.85},        {u"850"_s, Unite::Millimetre, 0.85},
            {u"8,5E1"_s, Unite::Centimetre, 0.85},
            // une unité écrite remplace celle de la liste
            {u"85 cm"_s, Unite::Metre, 0.85},          {u"850mm"_s, Unite::Metre, 0.85},
            {u"85 centimeters"_s, Unite::Metre, 0.85}, {u"0,85 m"_s, Unite::Millimetre, 0.85},
            {u"85 centimètres"_s, Unite::Metre, 0.85},
        };
        for (const auto &[saisie, unite, attendu] : cas) {
            const auto v = lit_longueur(saisie, unite);
            QVERIFY2(v.has_value(), qUtf8Printable(saisie + u" : "_s + v.error_or(QString())));
            QVERIFY2(std::abs(v->metres() - attendu) <= 1e-12 * attendu, qUtf8Printable(saisie));
        }
        QCOMPARE(lit_longueur(u"85 cm"_s, Unite::Metre)->unite, Unite::Centimetre);
        QCOMPARE(lit_longueur(u"85"_s, Unite::Centimetre)->unite, Unite::Centimetre);
        QCOMPARE(texte_longueur(0.85, Unite::Centimetre), u"85 cm"_s);
        QCOMPARE(texte_longueur(0.85, Unite::Millimetre), u"850 mm"_s);
    }

    void longueurs_refusees()
    {
        for (const QString &saisie : {u"1km"_s, u"85 Zentimeter"_s, u"-1"_s, u"0"_s, u"abc"_s, u"1E"_s, u""_s, u"  "_s,
                                      u"1,2,3"_s, u"5 kg"_s, u"1E10M"_s, u"210^3"_s, u"8 dm"_s})
            QVERIFY2(!lit_longueur(saisie, Unite::Metre).has_value(), qUtf8Printable(saisie + u" devrait être refusé"_s));
        QVERIFY(lit_longueur(u"1km"_s, Unite::Metre).error().contains(u"(mm)"_s));
    }

    void nombres_pour_python()
    {
        QCOMPARE(nombre_python(0.0), u"0.0"_s);
        QCOMPARE(nombre_python(120.0), u"120.0"_s);
        QCOMPARE(nombre_python(0.04), u"0.04"_s);
        QCOMPARE(nombre_python(1e-10), u"1e-10"_s);
        QCOMPARE(nombre_python(-2.5), u"-2.5"_s);
        QCOMPARE(nombre_local(0.1234567, 4), u"0,1235"_s);
    }

    // ------------------------------------------------------------ vecteur, échelle
    void vecteurs()
    {
        const Vecteur a(3, 4), b(1, -1);
        QVERIFY(a + b == Vecteur(4, 3));
        QVERIFY(a - b == Vecteur(2, 5));
        QVERIFY(a * 2.0 == Vecteur(6, 8));
        QCOMPARE(a.module(), 5.0);
        QVERIFY(a.oriente_coordonnees(-1, 1) == Vecteur(-3, -4));
    }

    void echelles()
    {
        const Echelle indefinie;
        QVERIFY(!indefinie.est_definie());
        QCOMPARE(indefinie.metres_par_pixel(), 1.0);
        QCOMPARE(indefinie.pixels_par_metre(), 1.0);
        const Echelle e{{10, 10}, {10, 210}, 0.5};
        QVERIFY(e.est_definie());
        QCOMPARE(e.pixels_par_metre(), 400.0);
        QCOMPARE(e.metres_par_pixel(), 0.0025);
    }

    // ------------------------------------------------------------ pointage
    /// vidéo à cadence variable : une image perdue entre les images 2 et 3
    void pointage_cadence_variable()
    {
        Pointage p;
        p.alloue_mesures(1, 0.1, 5);
        p.instants = std::vector<double>{0, 1, 3, 4, 5};
        QVERIFY(proche(p.date(2), 0.3, 1e-12));
        p.echelle = {{0, 0}, {100, 0}, 1.0};
        // mouvement uniforme à 2 m/s (200 px/s) dans le temps réel
        const double t[] = {0.0, 0.1, 0.3, 0.4, 0.5};
        for (int i = 0; i < 5; ++i)
            p.memorise_point(1, {200.0 * t[i], 0}, i);
        for (int i = 1; i < 4; ++i)
            QVERIFY(proche(p.vitesse_px(1, i)->x, 200.0));
        // vecteurs vitesse : 2 m/s, à 100 pixels pour 1 m/s
        for (const auto &[o, b] : p.fleches_vitesse(100.0)[0])
            QVERIFY(proche(b.x - o.x, 200.0));
        // les dates exportées sont les dates réelles
        const QString csv = p.mesures_en_csv(u";"_s, false, 2, {});
        QStringList dates;
        for (const QString &l : csv.split(u'\n').mid(1)) {
            if (!l.isEmpty())
                dates << l.section(u';', 0, 0);
        }
        QCOMPARE(dates, QStringList({u"0.000"_s, u"0.200"_s, u"0.300"_s, u"0.400"_s}));
        // une nouvelle vidéo efface les dates de la précédente
        p.alloue_mesures(1, 0.04, 3);
        QVERIFY(!p.cadence_variable());
    }

    /// cadence presque régulière (40 puis 44 ms, comme certains téléphones) : les dates réelles des
    /// images sont gardées, sans signaler une cadence variable ; une cadence fixe, même avec des
    /// horodatages arrondis à la milliseconde, garde des dates régulières
    void cadence_presque_reguliere()
    {
        InformationsVideo v;
        v.cadence_lecture = 1.0 / 0.042;
        v.dates = {0.0, 0.040, 0.084, 0.124, 0.168};
        QVERIFY(!cadence_variable(v.dates));
        QVERIFY(::cadence_presque_reguliere(v.dates, 1.0 / 90000));
        v.dates_reelles = true;
        const auto instants = v.instants();
        QVERIFY(instants);
        QVERIFY(proche((*instants)[1] / v.cadence_lecture, 0.040, 1e-9) && proche((*instants)[2] / v.cadence_lecture, 0.084, 1e-9));
        // chaque trame est une image, à sa date
        QCOMPARE(v.trame(3), 3);
        // une cadence qui dérive lentement (30 puis 25 images/s) : chaque intervalle reste proche de
        // l'habituel, mais les dates s'éloignent de la cadence moyenne
        std::vector<double> derive{0.0};
        for (int k = 1; k < 60; ++k)
            derive.push_back(derive.back() + (k < 30 ? 1.0 / 30 : 1.0 / 25));
        QVERIFY(!cadence_variable(derive));
        QVERIFY(::cadence_presque_reguliere(derive, 1.0 / 90000));
        // cadence fixe : les arrondis des dates ne comptent pas (MKV et WebM à la milliseconde, à 30,
        // 60 et 240 images/s ; MP4 en 1/600 s à 29,97 images/s)
        InformationsVideo fixe;
        fixe.cadence_lecture = 30.0;
        fixe.dates = {0.0, 0.033, 0.067, 0.100, 0.133, 0.167, 0.200};
        QVERIFY(!::cadence_presque_reguliere(fixe.dates, 0.001));
        QVERIFY(!fixe.instants());
        for (double cadence : {60.0, 240.0}) {
            std::vector<double> ms;
            for (int k = 0; k < 600; ++k)
                ms.push_back(std::round(k * 1000.0 / cadence) / 1000.0);
            QVERIFY(!::cadence_presque_reguliere(ms, 0.001));
        }
        std::vector<double> ntsc;
        for (int k = 0; k < 600; ++k)
            ntsc.push_back(std::round(k * 20.02) / 600.0);
        QVERIFY(!::cadence_presque_reguliere(ntsc, 1.0 / 600));
        Pointage p;
        p.alloue_mesures(1, 1.0 / v.cadence_lecture, 5);
        p.instants = instants;
        QVERIFY(proche(p.date(2), 0.084, 1e-9));
        QVERIFY(!p.cadence_variable());
    }

    void premiere_et_derniere()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 10);
        QVERIFY(!p.a_des_pointages());
        QCOMPARE(p.image_initiale(), std::nullopt);
        p.memorise_point(1, {1, 2}, 3);
        p.memorise_point(2, {3, 4}, 3);
        p.memorise_point(1, {5, 6}, 4);
        QVERIFY(p.a_des_pointages());
        QCOMPARE(p.image_initiale(), 4);
        QCOMPARE(p.image_finale(), 5);
        QCOMPARE(p.nb_images_pointees(), 2);
        // la date zéro est celle de la première image pointée
        QVERIFY(proche(p.date_relative(3), 0.0, 1e-12));
        QVERIFY(proche(p.date_relative(4), 0.04, 1e-12));
        // l'image 5 n'a que le premier objet : elle n'est pas complète
        const auto lignes = p.lignes_export();
        QCOMPARE(lignes.size(), size_t(2));
        QVERIFY(!lignes[1].positions[1]);
        QCOMPARE(lignes[0].index, 3);
        QCOMPARE(lignes[0].positions.size(), size_t(2));
    }

    void defaire_refaire()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 10);
        p.memorise_point(1, {1, 2}, 2);
        p.memorise_point(1, {5, 6}, 3);
        p.annule_mesure();
        QCOMPARE(p.image_finale(), 3);
        p.annule_mesure();
        QVERIFY(!p.a_des_pointages());
        QVERIFY(p.retablissement_possible());
        p.retablit_mesure();
        QVERIFY(p.point_de_l_objet(1, 2) == Vecteur(1, 2));
        p.retablit_mesure();
        QVERIFY(p.point_de_l_objet(1, 3) == Vecteur(5, 6));
        QVERIFY(!p.retablissement_possible());
    }

    void conversions_en_metre()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 10);
        p.origine = {100, 100};
        p.echelle = {{0, 0}, {0, 200}, 1.0};
        QVERIFY(p.position_dans_repere({300, 0}) == Vecteur(1.0, 0.5));
        p.sens_y = -1;
        QVERIFY(p.position_dans_repere({300, 0}) == Vecteur(1.0, -0.5));
        // une vitesse ne dépend pas de l'origine
        p.sens_y = 1;
        QVERIFY(p.vecteur_dans_repere({200, -400}) == Vecteur(1.0, 2.0));
    }

    void csv_et_restauration()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 10);
        p.origine = {100, 100};
        p.echelle = {{0, 0}, {0, 200}, 1.0};
        p.memorise_point(1, {300, 0}, 2);
        p.memorise_point(2, {100, 300}, 2);
        p.memorise_point(1, {320, 10}, 3);
        p.memorise_point(2, {110, 290}, 3);
        const QString csv = p.mesures_en_csv(u"\t"_s, true, 3, p.origine);
        const QStringList lignes = prefs::lignes(csv);
        QCOMPARE(lignes[0], u"t\tx1\ty1\tx2\ty2"_s);
        QCOMPARE(lignes[1], u"0.000\t1\t0.5\t0\t-1"_s);
        QCOMPARE(lignes[2], u"0.040\t1.1\t0.45\t0.05\t-0.95"_s);
        // rien après la dernière image pointée : le projet ne grossit pas avec la vidéo
        QCOMPARE(lignes.size(), 3);
        // relecture
        std::vector<std::vector<double>> data;
        for (const QString &l : lignes.mid(1)) {
            std::vector<double> v;
            for (const QString &x : l.split(u'\t'))
                v.push_back(x.toDouble());
            data.push_back(v);
        }
        Pointage q;
        q.alloue_mesures(2, 0.04, 10);
        q.origine = p.origine;
        q.echelle = p.echelle;
        q.recharge_mesures(data, 3);
        QVERIFY(q.point_de_l_objet(1, 2) == p.point_de_l_objet(1, 2));
        QVERIFY(q.point_de_l_objet(2, 3) == p.point_de_l_objet(2, 3));
        QCOMPARE(q.image_finale(), 4);
    }

    void csv_ligne_incomplete()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 5);
        p.origine = {0, 0};
        p.echelle = {{0, 0}, {0, 100}, 1.0}; // 100 px par mètre
        // seul l'objet 2 est pointé : les colonnes de l'objet 1 restent présentes
        p.memorise_point(2, {100, 200}, 1);
        QVERIFY(p.a_des_pointages());
        QCOMPARE(p.image_initiale(), 2);
        QCOMPARE(p.image_finale(), 2);
        QCOMPARE(p.nb_images_pointees(), 1);
        const QString donnees = p.mesures_en_csv(u"\t"_s, true, 1, p.origine);
        const QStringList lignes = prefs::lignes(donnees);
        QCOMPARE(lignes.size(), 3);
        QCOMPARE(lignes[1], u"0.000"_s);
        QCOMPARE(lignes[2], u"0.040\tnan\tnan\t1\t-2"_s);
        // relecture par le format de projet : l'objet 2 reste l'objet 2
        prefs::Config c;
        c.definit(u"nb_obj"_s, u"2"_s);
        QTemporaryDir dir;
        const QString f = dir.filePath(u"incomplet.mevcine"_s);
        QFile fichier(f);
        QVERIFY(fichier.open(QIODevice::WriteOnly));
        fichier.write(prefs::texte_projet(c, u"test"_s, donnees).toUtf8());
        fichier.close();
        const auto projet = prefs::lit_projet(f);
        QVERIFY(projet.has_value());
        Pointage q;
        q.alloue_mesures(2, 0.04, 5);
        q.origine = p.origine;
        q.echelle = p.echelle;
        q.recharge_mesures(projet->donnees, 1);
        QVERIFY(!q.point_de_l_objet(1, 1).has_value());
        QVERIFY(q.point_de_l_objet(2, 1).has_value());
        QVERIFY(proche(q.point_de_l_objet(2, 1)->x, 100.0));
        QVERIFY(proche(q.point_de_l_objet(2, 1)->y, 200.0));
    }

    void mesures_espacees_et_absences()
    {
        Pointage p;
        p.alloue_mesures(15, 0.04, 100000);
        QCOMPARE(p.ligne(99999).size(), size_t(15));
        QVERIFY(!p.image_initiale() && !p.image_finale());
        p.memorise_absence(2, 80000);
        p.memorise_point(14, {10, 20}, 80002);
        p.memorise_absence(2, 80004);
        QCOMPARE(p.nb_images_pointees(), 1);
        QCOMPARE(p.image_initiale(), 80001);
        QCOMPARE(p.image_finale(), 80005);
        const auto tableau = Tableau::construit(p);
        QCOMPARE(tableau.images, (std::vector<int>{80000, 80002, 80004}));
        const auto exportees = p.lignes_export();
        QCOMPARE(exportees.size(), size_t(5));
        QVERIFY(proche(exportees[2].t, 0.08));
        QVERIFY(!exportees[1].positions[13]);
        p.supprime_points({{80002, 14}});
        QVERIFY(!p.a_des_pointages());
        QCOMPARE(p.nb_images_pointees(), 0);
        QCOMPARE(p.image_initiale(), 80001); // une absence a sa propre date
        p.annule_mesure();
        QCOMPARE(p.point_de_l_objet(14, 80002), Vecteur(10, 20));
        p.retablit_mesure();
        QVERIFY(!p.point_de_l_objet(14, 80002));
        p.supprime_points({{80000, 2}, {80004, 2}});
        QVERIFY(!p.image_initiale() && !p.image_finale());
    }

    void changement_de_referentiel()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 10);
        p.memorise_point(1, {10, 10}, 0);
        p.memorise_point(2, {15, 20}, 0);
        p.memorise_point(1, {20, 10}, 1);
        p.memorise_point(2, {30, 20}, 1);
        p.memorise_point(2, {40, 20}, 2);
        // un pointage défait, qui ne doit pas suivre la copie dans un autre référentiel
        p.memorise_point(1, {50, 50}, 2);
        p.annule_mesure();
        QVERIFY(p.retablissement_possible());
        const Pointage q = p.dans_referentiel(1, {100, 100});
        QVERIFY(q.origine == Vecteur(100, 100));
        QVERIFY(q.point_de_l_objet(1, 0) == Vecteur(100, 100));
        QVERIFY(q.point_de_l_objet(1, 1) == Vecteur(100, 100));
        QVERIFY(q.point_de_l_objet(2, 1) == Vecteur(110, 110));
        // l'objet de référence n'est pas pointé sur la troisième image
        QCOMPARE(q.point_de_l_objet(2, 2), std::nullopt);
        // l'historique de l'ancien référentiel ne suit pas la copie
        QVERIFY(!q.retablissement_possible());
    }

    void vitesses()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 10);
        p.memorise_point(1, {0, 0}, 0);
        p.memorise_point(1, {4, 0}, 1);
        p.memorise_point(1, {8, 0}, 2);
        QCOMPARE(p.vitesse_px(1, 0), std::nullopt);
        QVERIFY(p.vitesse_px(1, 1) == Vecteur(100, 0));
        const auto vv = p.fleches_vitesse(0.01);
        QCOMPARE(vv[0].size(), size_t(1));
        QVERIFY(vv[0][0].first == Vecteur(4, 0));
        QVERIFY(proche(vv[0][0].second.x, 5.0));
        QVERIFY(vv[1].empty());
    }

    void suppression_plage_et_vitesse()
    {
        Pointage p;
        p.alloue_mesures(2, 0.04, 5);
        for (int i = 0; i < 5; ++i)
            for (int obj = 1; obj <= 2; ++obj)
                p.memorise_point(obj, {double(i), double(obj)}, i);
        p.supprime_points({{1, 1}, {1, 1}, {2, 1}});
        QVERIFY(!p.point_de_l_objet(1, 1));
        QVERIFY(p.point_de_l_objet(2, 1));
        QVERIFY(p.fleches_vitesse(1)[0].empty());
        p.annule_mesure();
        QCOMPARE(p.fleches_vitesse(1)[0].size(), size_t(3));
        p.retablit_mesure();
        QVERIFY(!p.point_de_l_objet(1, 2));
        p.memorise_point(2, {90, 40}, 4);
        p.annule_mesure();
        QVERIFY(p.point_de_l_objet(2, 4) == Vecteur(4, 2));
        p.annule_mesure();
        QVERIFY(p.point_de_l_objet(1, 2) == Vecteur(2, 1));
    }

    void vitesses_verticales()
    {
        Pointage p;
        p.alloue_mesures(1, 0.04, 5);
        p.origine = {0, 0};
        p.echelle = {{0, 0}, {0, 100}, 1.0}; // 100 px par mètre
        p.memorise_point(1, {0, 0}, 0);
        p.memorise_point(1, {0, 10}, 1);
        p.memorise_point(1, {0, 20}, 2);
        // 10 px en 0,04 s = 250 px/s = 2,5 m/s ; à 10 points par m/s, la flèche mesure 25 px
        const auto v = p.fleches_vitesse(10.0)[0];
        QCOMPARE(v.size(), size_t(1));
        QVERIFY(proche(v[0].second.y - v[0].first.y, 25.0));
        QVERIFY(proche(v[0].second.x - v[0].first.x, 0.0));
        // la flèche suit le déplacement dans l'image, quel que soit le sens choisi pour l'axe
        p.sens_y = -1;
        const auto w = p.fleches_vitesse(10.0)[0];
        QVERIFY(proche(w[0].second.y - w[0].first.y, 25.0));
    }

    // ------------------------------------------------------------ calculs
    void vitesses_et_accelerations()
    {
        const Pointage p = chute();
        const auto grandeurs = grandeurs_disponibles(p.nombre_objets());
        QCOMPARE(grandeurs.size(), size_t(9)); // le temps et huit grandeurs cinématiques
        const auto vx = std::ranges::find(grandeurs, u"vx"_s, &Grandeur::nom);
        const auto ay = std::ranges::find(grandeurs, u"ay"_s, &Grandeur::nom);
        QVERIFY(vx != grandeurs.end() && ay != grandeurs.end());
        QVERIFY(!vx->valeur(p, 2) && !ay->valeur(p, 2));
        QVERIFY(proche(vx->valeur(p, 5).value(), 0.5));
        QVERIFY(!ay->valeur(p, p.nombre_images() - 1));
        for (int i = 3; i + 1 < p.nombre_images(); ++i)
            QVERIFY(proche(ay->valeur(p, i).value(), -10.0));
    }

    void acceleration_a_cadence_variable()
    {
        // chute libre aux dates réelles d'une vidéo dont des images manquent : l'accélération reste
        // g à chaque image, et non g × (Δtᵢ + Δtᵢ₋₁) / (2Δtᵢ)
        Pointage p;
        const std::vector<double> instants{0, 1, 2, 4, 5, 6, 8, 9};
        p.alloue_mesures(1, 0.04, int(instants.size()));
        p.instants = instants;
        p.origine = {0, 0};
        p.echelle = {{0, 0}, {0, 100}, 0.1};
        for (int i = 0; i < p.nombre_images(); ++i) {
            const double t = p.date(i);
            // l'axe y de l'image va vers le bas : y(t) = −g t² / 2 en mètres, 1 000 px par mètre
            p.memorise_point(1, {0, 1000.0 * 9.81 * t * t / 2.0}, i);
        }
        const auto grandeurs = grandeurs_disponibles(1);
        const Grandeur &ay = *std::ranges::find(grandeurs, u"ay"_s, &Grandeur::nom);
        const Grandeur &vy = *std::ranges::find(grandeurs, u"vy"_s, &Grandeur::nom);
        QVERIFY(!ay.valeur(p, 0) && !ay.valeur(p, p.nombre_images() - 1));
        const auto fleches = p.fleches_vitesse(1).front();
        for (int i = 1; i + 1 < p.nombre_images(); ++i) {
            QVERIFY(ay.valeur(p, i) && proche(*ay.valeur(p, i), -9.81, 1e-9));
            QVERIFY(vy.valeur(p, i) && proche(*vy.valeur(p, i), -9.81 * p.date(i), 1e-9));
            const auto &[o, b] = fleches.at(i - 1);
            QVERIFY(proche(b.y - o.y, -*vy.valeur(p, i), 1e-9));
        }
    }

    void tableau()
    {
        const Tableau t = Tableau::construit(chute());
        QCOMPARE(t.en_tetes, QStringList({u"t (s)"_s, u"x (m)"_s, u"y (m)"_s}));
        // seulement les images pointées, à partir de t = 0
        QCOMPARE(t.lignes.size(), size_t(6));
        QCOMPARE(t.images[0], 2);
        QCOMPARE(t.lignes[0], QStringList({u"0,000"_s, u"0"_s, u"0"_s}));
        QCOMPARE(t.lignes[1], QStringList({u"0,100"_s, u"0,05"_s, u"-0,05"_s}));
    }

    void noms_et_unites()
    {
        QCOMPARE(nom_grandeur(u"x"_s, 1, 1), u"x"_s);
        QCOMPARE(nom_grandeur(u"vx"_s, 2, 3), u"vx2"_s);
        QCOMPARE(unite(u"vx2"_s, true), u"m/s"_s);
        QCOMPARE(unite(u"a"_s, false), u"px/s²"_s);
        QCOMPARE(unite(u"y3"_s, true), u"m"_s);
        QCOMPARE(unite(u"t"_s, true), u"s"_s);
    }

    void dates_affichees()
    {
        QCOMPARE(texte_date(0.04, 0.04), u"0,040"_s);
        QCOMPARE(texte_date(1.0 / 240.0, 1.0 / 240.0), u"0,0042"_s);
        QCOMPARE(texte_date(0.0, 0.0), u"0,000"_s);
    }

    // ------------------------------------------------------------ exports
    void csv_un_objet()
    {
        const QString dest = qEnvironmentVariable("MEVSUITE_CSV_TEST");
        if (!dest.isEmpty())
            QVERIFY(ecrit_csv(QDir(dest).filePath(u"trajectoire.csv"_s), exemple_export(2)).isEmpty());
        QCOMPARE(texte_csv(exemple_export(1)),
                 u"t (s),x (m),y (m)\r\n0,0.1,0.5\r\n0.04,0.2,0.5\r\n0.08,0.3,0.5\r\n0.12,0.4,0.5\r\n"_s);
    }

    void csv_deux_objets_et_lignes_completes()
    {
        const QStringList lignes = texte_csv(exemple_export(2)).split(u"\r\n"_s);
        QCOMPARE(lignes[0], u"t (s),x1 (m),y1 (m),x2 (m),y2 (m)"_s);
        QCOMPARE(lignes[1], u"0,0.1,0.5,0.05,0.01"_s);
        // La mesure du premier mobile reste exploitable.
        QCOMPARE(lignes.size(), 6);
        QCOMPARE(lignes[4], u"0.12,0.4,0.5,,"_s);
    }

    void csv_sans_echelle()
    {
        Pointage p = exemple_export(1);
        p.echelle = {};
        QVERIFY(texte_csv(p).startsWith(u"t (s),x (px),y (px)\r\n0,10,50\r\n"_s));
    }

    void python()
    {
        QString s = texte_python(exemple_export(1), u"chute.mp4"_s);
        QVERIFY2(s.startsWith(u"import numpy as np\n\n# Mesures MeVCine — vidéo : chute.mp4\n"_s), qUtf8Printable(s));
        QVERIFY(s.contains(u"# t : temps en s ; x, y : positions en m\n"_s));
        QVERIFY(s.contains(u"lst_t = np.array([0.0, 0.04, 0.08, 0.12])\n"_s));
        QVERIFY(s.contains(u"lst_x = np.array([0.1, 0.2, 0.3, 0.4])\n"_s));
        QVERIFY(s.contains(u"lst_y = np.array([0.5, 0.5, 0.5, 0.5])\n"_s));
        s = texte_python(exemple_export(2), std::nullopt);
        QVERIFY2(s.contains(u"lst_x2 = np.array([0.05, 0.05, 0.05, np.nan])\n"_s), qUtf8Printable(s));
        QVERIFY(s.contains(u"lst_y2 = np.array([0.01, 0.02, 0.03, np.nan])\n"_s));
        QVERIFY(s.contains(u"objet 1, objet 2"_s));
    }

    /// les commentaires suivent la langue de l'interface, pas le code
    void python_en_anglais()
    {
        i18n::choisit(Langue::Anglais);
        const QString s = texte_python(exemple_export(1), u"chute.mp4"_s);
        i18n::choisit(Langue::Francais);
        QVERIFY2(s.contains(u"# MeVCine measurements — video: chute.mp4\n"_s), qUtf8Printable(s));
        QVERIFY(s.contains(u"lst_t = np.array([0.0, 0.04, 0.08, 0.12])\n"_s));
        QVERIFY(texte_csv(exemple_export(1)).startsWith(u"t (s),x (m),y (m)\r\n"_s));
    }

    /// le texte copié doit être un programme Python valide (si python3 et numpy sont installés)
    void python_execute()
    {
        const QString erreur = outils::execute_python(
            texte_python(exemple_export(1), u"a.mp4"_s),
            u"assert len(lst_t) == len(lst_x) == len(lst_y) == 4\nassert lst_x.dtype == float\nprint('ok')"_s);
        QVERIFY2(erreur.isEmpty(), qUtf8Printable(erreur));
    }

    void python_nom_video_sur_une_ligne()
    {
        const Pointage p = exemple_export(2);
        const QString programme = texte_python(p, u"séance 1\r\npendule.mp4"_s);
        QCOMPARE(programme, texte_python(p, u"séance 1 pendule.mp4"_s));
        QVERIFY(texte_python(p, u"deux  billes.mp4"_s).contains(u"deux  billes.mp4"_s));
        const QString erreur = outils::execute_python(programme, u"assert len(lst_x2) == len(lst_t)"_s);
        QVERIFY2(erreur.isEmpty(), qPrintable(erreur));
    }

    // ------------------------------------------------------------ préférences et projets
    void ini_aller_retour()
    {
        prefs::Config c;
        c.definit(u"Sens_X"_s, u"-1"_s);
        c.definit(u"origine"_s, u"(12, 34)"_s);
        prefs::Config d;
        d.lit_ini(c.ecrit_ini());
        QCOMPARE(d.valeur_entiere(u"sens_x"_s), -1);
        QVERIFY(d.valeur_vecteur(u"origine"_s) == Vecteur(12, 34));
        QCOMPARE(d.valeur_vecteur(u"etalon_org"_s), std::nullopt);
        QCOMPARE(d.version_du_fichier(), std::make_pair(1u, 0u));
        QVERIFY(d.est_mevcine() && d.version_ok());
        // la signature « cinemev » est reconnue aussi
        d.definit(u"version"_s, u"cinemev 1.0.0"_s);
        QVERIFY(d.est_mevcine() && d.version_ok());
        QCOMPARE(d.version_du_fichier(), std::make_pair(1u, 0u));
    }

    void projet_aller_retour()
    {
        QTemporaryDir dir;
        const QString f = dir.filePath(u"essai.mevcine"_s);
        prefs::Config c;
        c.definit(u"nb_obj"_s, u"2"_s);
        c.definit(u"langue"_s, u"en"_s);
        QFile fichier(f);
        QVERIFY(fichier.open(QIODevice::WriteOnly));
        fichier.write(prefs::texte_projet(c, u"temps en seconde, positions en mètre"_s,
                                          u"t\tx1\ty1\n0,000\t0,5\t-1,25\n0.040\n"_s)
                          .toUtf8());
        fichier.close();
        QVERIFY(prefs::est_projet(f));
        // la langue est un réglage du poste, pas du projet
        QVERIFY(!lit_texte(f).contains(u"langue"_s));
        const auto projet = prefs::lit_projet(f);
        QVERIFY(projet.has_value());
        QCOMPARE(projet->config.valeur_entiere(u"nb_obj"_s), 2);
        QCOMPARE(projet->donnees, (std::vector<std::vector<double>>{{0.0, 0.5, -1.25}, {0.04}}));
        QVERIFY(prefs::texte_projet(projet->config, {}, {}).startsWith(u"# version = mevcine"_s));
        QVERIFY(prefs::ecrit_projet(f, u"t\tx1\ty1\n0\t10\t20\n"_s).isEmpty());
        const auto sans_version = prefs::lit_projet(f);
        QVERIFY(sans_version && !sans_version->config.version_ok());
    }

    void projet_controle_du_tableau()
    {
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath(u"mesures.mevcine"_s);
        const QString debut = u"# version = cinemev 1.0\nt x1 y1 x2 y2\n"_s;
        for (const QString &ligne : {u"0.04 1"_s, u"0.04 nan 3"_s, u"nan 1 2"_s, u"0.04 1 inf"_s,
                                     u"0.04\t\tvaleur"_s, u"0.04 1 2 3 4 5 6"_s}) {
            QVERIFY(prefs::ecrit_projet(chemin, debut + ligne).isEmpty());
            const auto projet = prefs::lit_projet(chemin);
            QVERIFY(!projet);
            QVERIFY2(projet.error().contains(u"3"), qPrintable(projet.error()));
        }
        QVERIFY(prefs::ecrit_projet(chemin, debut + u"0,04\t\t\t3\t4\r\n0,08\r\n").isEmpty());
        const auto ancien = prefs::lit_projet(chemin);
        QVERIFY(ancien);
        QCOMPARE(ancien->config.valeur_entiere(u"nb_obj"_s), 2);
        QVERIFY(std::isnan(ancien->donnees[0][1]));
        QCOMPARE(ancien->donnees[0][3], 3.0);
        QCOMPARE(ancien->donnees[1].size(), size_t(1));
        for (const QString &contenu : {u"# version = mevcine 1.0\n"_s, u"0 1 2\n0 3 4"_s, u"t y1 x1"_s}) {
            QVERIFY(prefs::ecrit_projet(chemin, contenu).isEmpty());
            QVERIFY(!prefs::lit_projet(chemin));
        }
    }

    void projet_cinemev()
    {
        // projet à la signature « cinemev »
        const QString texte = u"# version = cinemev 1.0.0\n# taille_image = (640,480)\n# origine = (320, 240)\n"
                              "# deltat = 0.04\n# nb_obj = 1\n# \n# temps en seconde, positions en mètre\n"
                              "t\tx1\ty1\n0,000\t0,1\t0,2\n"_s;
        QTemporaryDir dir;
        const QString f = dir.filePath(u"projet.cinemev"_s);
        QFile fichier(f);
        QVERIFY(fichier.open(QIODevice::WriteOnly));
        fichier.write(texte.toUtf8());
        fichier.close();
        QVERIFY(prefs::est_projet(f));
        const auto projet = prefs::lit_projet(f);
        QVERIFY(projet.has_value());
        QVERIFY(projet->config.est_mevcine() && projet->config.version_ok());
        QCOMPARE(projet->config.valeur_entiere(u"nb_obj"_s), 1);
    }

    void noms_de_video_dans_les_projets()
    {
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath(u"essai.mevcine"_s);
        const QString film = u"/séance\n2/bille\r\nbleue.mp4 "_s;
        prefs::Config reglages;
        reglages.definit(u"lastvideo"_s, film);
        reglages.definit(u"videodir"_s, u"/séance\n2"_s);
        QVERIFY(prefs::ecrit_projet(chemin, prefs::texte_projet(reglages, {}, u"t\tx1\ty1\n0\t10\t20\n"_s)).isEmpty());
        const auto projet = prefs::lit_projet(chemin);
        QVERIFY(projet);
        QCOMPARE(projet->config.valeur_texte(u"lastvideo"_s), film);
        QCOMPARE(projet->config.valeur_texte(u"videodir"_s), u"/séance\n2"_s);
        QCOMPARE(projet->donnees, (std::vector<std::vector<double>>{{0, 10, 20}}));
    }

    void reprise_des_anciens_projets()
    {
        prefs::Projet projet;
        projet.config.definit(u"index_depart"_s, u"3"_s);
        projet.donnees = {{0.0, 10, 20}};
        Pointage p;
        const auto relit = [&] {
            p.alloue_mesures(3, 0.04, 5);
            return seance::restaure_mesures(projet, p);
        };
        const Pointage::ProchainClic attendu{3, 2};
        QVERIFY(relit() == attendu);
        projet.donnees[0] = {0.0, 10, 20, NAN, NAN, 30, 40};
        QVERIFY(!relit()); // l'objet 2 avait été retiré entre deux positions conservées
        projet.donnees[0] = {0.0, 10, 20};
        projet.config.definit(u"objet_suivant"_s, u"0"_s);
        QVERIFY(!relit()); // un projet récent indique explicitement que la série est terminée
        projet.config.retire(u"objet_suivant"_s);
        projet.config.definit(u"automatiques"_s, u"2:1"_s);
        QVERIFY(!relit()); // la détection ne réclame pas un clic manuel
        projet.config.definit(u"index_depart"_s, QString::number(std::numeric_limits<int>::max()));
        QVERIFY(!relit());
        QVERIFY(!p.a_des_pointages());
    }

    void dates_du_journal()
    {
        QCOMPARE(prefs::horodatage(0), u"1970-01-01 00:00:00 UTC"_s);
        QCOMPARE(prefs::horodatage(951'782'400), u"2000-02-29 00:00:00 UTC"_s);
        QCOMPARE(prefs::horodatage(1'700'000'000), u"2023-11-14 22:13:20 UTC"_s);
        QCOMPARE(prefs::horodatage(1'792'000'000), u"2026-10-14 17:46:40 UTC"_s);
    }

    // ------------------------------------------------------------ chemins
    void videos_de_telephone()
    {
        const QStringList filtre = chemins::filtre_videos();
        for (const QString &nom : {u"IMG_1234.MOV"_s, u"VID_20261012_120000.mp4"_s, u"PXL_1.MP4"_s, u"00001.MTS"_s,
                                   u"clip.webm"_s, u"GL010001.LRV"_s})
            QVERIFY2(filtre.contains(u"*."_s + QFileInfo(nom).suffix()), qUtf8Printable(nom));
        QVERIFY(!filtre.contains(u"*.txt"_s));
        QVERIFY(filtre.contains(u"*.MOV"_s) && filtre.contains(u"*.mov"_s));
    }

    void chemin_absolu()
    {
        const QString c = chemins::absolu(QString::fromUtf8(MEVCINE_SOURCES) + u"/../mevcine/CMakeLists.txt"_s);
        QVERIFY(QFileInfo(c).isAbsolute() && c.endsWith(u"mevcine/CMakeLists.txt"_s) && !c.contains(u".."_s));
    }

    // ------------------------------------------------------------ traductions
    void codes_des_langues()
    {
        QCOMPARE(i18n::depuis_code(u"fr_FR.UTF-8"_s), Langue::Francais);
        QCOMPARE(i18n::depuis_code(u"en-GB"_s), Langue::Anglais);
        QCOMPARE(i18n::depuis_code(u"FR"_s), Langue::Francais);
        QCOMPARE(i18n::depuis_code(u"de_DE"_s), std::nullopt);
        QCOMPARE(i18n::depuis_code(u"C"_s), std::nullopt);
        QCOMPARE(i18n::depuis_code(u"nl-NL"_s), std::nullopt);
        QCOMPARE(i18n::depuis_code(u""_s), std::nullopt);
        for (Langue l : TOUTES_LES_LANGUES)
            QCOMPARE(i18n::depuis_code(i18n::code(l)), l);
    }

    void arguments()
    {
        QCOMPARE(trad("Image {} sur {}", 3, "10"), u"Image 3 sur 10"_s);
        QCOMPARE(i18n::remplit(u"{1} : {0}"_s, {u"a"_s, u"b"_s}), u"b : a"_s);
        QCOMPARE(i18n::remplit(u"sans argument"_s, {}), u"sans argument"_s);
        QCOMPARE(i18n::remplit(u"{} {x} {"_s, {u"1"_s}), u"1 {x} {"_s);
        QCOMPARE(i18n::remplit(u"{} et {}"_s, {u"1"_s}), u"1 et {}"_s);
    }

    void fichier_po()
    {
        const QString po = u"msgid \"\"\nmsgstr \"\"\n\"Language: en\\n\"\n\n#: src/a.cpp\nmsgid \"Ouvrir\"\nmsgstr \"Open\"\n\n"
                           "msgid \"\"\n\"deux \"\n\"lignes\\n\"\nmsgstr \"two \\\"lines\\\"\"\n\n#, fuzzy\nmsgid \"Flou\"\n"
                           "msgstr \"Fuzzy\"\n\nmsgid \"Vide\"\nmsgstr \"\"\nmsgid \"Collé\"\nmsgstr \"Glued\""_s;
        const auto t = i18n::analyse_po(po);
        QCOMPARE(t.value(u"Ouvrir"_s), u"Open"_s);
        QCOMPARE(t.value(u"deux lignes\n"_s), u"two \"lines\""_s);
        QCOMPARE(t.value(u"Collé"_s), u"Glued"_s);
        QVERIFY(!t.contains(u"Flou"_s) && !t.contains(u"Vide"_s) && !t.contains(QString()));
    }

    void langue_choisie()
    {
        i18n::choisit(Langue::Anglais);
        QCOMPARE(i18n::langue(), Langue::Anglais);
        QCOMPARE(trad("Suivant"), u"Next"_s);
        i18n::choisit(Langue::Francais);
        QCOMPARE(trad("Suivant"), u"Suivant"_s);
    }

    /// Chaque texte de l'interface est traduit dans chaque langue, avec les mêmes arguments ; les
    /// fichiers .po ne gardent pas de traduction devenue inutile.
    void traductions_completes()
    {
        const auto textes = textes_a_traduire();
        QVERIFY2(textes.size() > 250, qUtf8Printable(u"%1 textes seulement"_s.arg(textes.size())));
        QStringList erreurs;
        for (Langue l : TOUTES_LES_LANGUES) {
            if (l == Langue::Francais)
                continue;
            const QString code = i18n::code(l);
            const auto t = i18n::analyse_po(i18n::po(l));
            for (const auto &[source, ou] : textes) {
                if (!t.contains(source)) {
                    erreurs << u"%1 : pas de traduction pour « %2 » (%3)"_s.arg(code, source, ou);
                    continue;
                }
                const QString trad = t.value(source);
                const auto [n, numeros] = marques(source);
                const auto [n2, numeros2] = marques(trad);
                QList<int> attendus;
                for (int k = 0; k < n; ++k)
                    attendus << k;
                const bool ok = (n2 == n && numeros2.isEmpty()) || (n2 == 0 && numeros2 == attendus);
                if (!ok || !numeros.isEmpty())
                    erreurs << u"%1 : arguments différents dans « %2 » → « %3 »"_s.arg(code, source, trad);
                if (source.endsWith(u'\n') != trad.endsWith(u'\n'))
                    erreurs << u"%1 : fin de ligne différente dans « %2 »"_s.arg(code, source);
            }
            for (auto it = t.begin(); it != t.end(); ++it) {
                const bool utile = std::any_of(textes.begin(), textes.end(), [&](const auto &x) { return x.first == it.key(); });
                if (!utile)
                    erreurs << u"%1 : traduction inutile de « %2 »"_s.arg(code, it.key());
            }
        }
        QVERIFY2(erreurs.isEmpty(), qUtf8Printable(u"%1 erreurs :\n"_s.arg(erreurs.size()) + erreurs.join(u'\n')));
    }

    /// Refait le modèle des traductions, data/lang/mevcine.pot :
    /// `MEVCINE_ECRIT_POT=1 ./mevcine_tests modelePo`
    void modelePo()
    {
        if (qEnvironmentVariableIsEmpty("MEVCINE_ECRIT_POT"))
            QSKIP("MEVCINE_ECRIT_POT=1 pour refaire data/lang/mevcine.pot");
        QString pot = u"# Modèle des traductions de MeVCine (C++, Qt)\n# Rémi MEVAERE\nmsgid \"\"\nmsgstr \"\"\n"
                      "\"Project-Id-Version: MeVCine\\n\"\n\"MIME-Version: 1.0\\n\"\n"
                      "\"Content-Type: text/plain; charset=UTF-8\\n\"\n\"Content-Transfer-Encoding: 8bit\\n\"\n"_s;
        for (const auto &[texte, ou] : textes_a_traduire())
            pot += u"\n#: %1\nmsgid %2\nmsgstr \"\"\n"_s.arg(ou, ecrit_chaine_po(texte));
        QFile f(QString::fromUtf8(MEVCINE_SOURCES) + u"/data/lang/mevcine.pot"_s);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(pot.toUtf8());
    }

    /// La notice (le site data/help, fait par scripts/site-aide.py à partir de aide/) existe dans
    /// chaque langue et est à jour de ses sources : chaque page est là, dans la langue, avec son
    /// sommaire et ses images, et chacun de ses liens mène quelque part.
    void aide_dans_chaque_langue()
    {
        const QDir racine(QString::fromUtf8(MEVCINE_SOURCES));
        const QDir sources(racine.filePath(u"aide"_s));
        const QDir site(racine.filePath(u"data/help"_s));
        const QString refaire = u"lancez python3 -I scripts/site-aide.py"_s;
        for (const QString &f : {u"help.css"_s, u"aide.css"_s, u"mevcine.svg"_s})
            QVERIFY2(QFileInfo(site.filePath(f)).isFile(), qUtf8Printable(f + u" manque : "_s + refaire));
        // la feuille de style est celle de toute la suite (celle de MeVSono, quand le dépôt est complet)
        const QString commune = racine.filePath(u"../mevsono/assets/help/help.css"_s);
        if (QFileInfo(commune).isFile())
            QVERIFY2(lit_texte(sources.filePath(u"help.css"_s)) == lit_texte(commune),
                     "aide/help.css diffère de la feuille de style commune (mevsono/assets/help/help.css)");
        for (Langue l : TOUTES_LES_LANGUES) {
            const QString code = i18n::code(l);
            QStringList pages;
            // les lignes « == titre » ouvrent une partie du sommaire : élèves, puis professeurs
            QStringList parties;
            for (const QString &ligne : prefs::lignes(lit_texte(sources.filePath(code + u"/pages.txt"_s)))) {
                if (ligne.startsWith(u"=="_s))
                    parties << ligne.mid(2).trimmed();
                else if (!ligne.trimmed().isEmpty() && !ligne.startsWith(u'#'))
                    pages << ligne.section(u'\t', 0, 0);
            }
            QVERIFY2(pages.size() >= 8, qUtf8Printable(code));
            QCOMPARE(parties.size(), 2);
            for (const QString &page : {u"index"_s, u"demarrer"_s, u"simplifie"_s, u"expert"_s, u"faq"_s, u"professeurs"_s})
                QVERIFY2(pages.contains(page), qUtf8Printable(code + u" : pas de page "_s + page));
            const QDir dossier(site.filePath(code));
            for (const QString &page : pages) {
                const QString fichier = dossier.filePath(page + u".html"_s);
                const QString html = lit_texte(fichier);
                QVERIFY2(!html.isEmpty(), qUtf8Printable(fichier + u" manque : "_s + refaire));
                QVERIFY2(html.contains(u"<html lang=\"%1\">"_s.arg(code)), qUtf8Printable(code + u"/"_s + page));
                const QString fragment = lit_texte(sources.filePath(code + u"/"_s + page + u".html"_s)).trimmed();
                QVERIFY2(html.contains(fragment), qUtf8Printable(code + u"/"_s + page + u" n'est pas à jour : "_s + refaire));
                for (const QString &autre : pages)
                    QVERIFY2(html.contains(u"href=\"%1.html\""_s.arg(autre)), qUtf8Printable(code + u"/"_s + page + u" → "_s + autre));
                for (const QString &partie : parties)
                    QVERIFY2(html.contains(u"<p class=\"group\">%1</p>"_s.arg(partie.toHtmlEscaped())),
                             qUtf8Printable(code + u"/"_s + page + u" : partie "_s + partie));
                const QStringList captures = attributs(fragment, u"src=\""_s);
                QVERIFY2(!captures.isEmpty(), qUtf8Printable(fichier));
                const QStringList descriptions = attributs(fragment, u"alt=\""_s);
                QCOMPARE(descriptions.size(), captures.size());
                QVERIFY(!descriptions.contains(QString()));
                QCOMPARE(fragment.count(u"<figcaption>"), captures.size());
                for (const QString &capture : captures) {
                    const QString chemin = dossier.filePath(capture);
                    QVERIFY2(!QImage(chemin).isNull(), qUtf8Printable(chemin));
                }
                for (const QString &lien : attributs(html, u"href=\""_s)) {
                    if (lien.startsWith(u"http"_s) || lien.startsWith(u"mailto:"_s))
                        continue;
                    const QString cible_texte = lien.section(u'#', 0, 0);
                    const QString ancre = lien.contains(u'#') ? lien.section(u'#', 1) : QString();
                    const QString cible = cible_texte.isEmpty() ? fichier : dossier.filePath(cible_texte);
                    QVERIFY2(QFileInfo(cible).isFile(), qUtf8Printable(code + u"/"_s + page + u" : lien "_s + lien));
                    if (!ancre.isEmpty() && cible.endsWith(u".html"_s))
                        QVERIFY2(lit_texte(cible).contains(u"id=\"%1\""_s.arg(ancre)),
                                 qUtf8Printable(code + u"/"_s + page + u" : lien "_s + lien));
                }
            }
        }
    }

    // ------------------------------------------------------------ HDR
    void blanc_de_reference()
    {
        // le blanc à 203 cd/m² est codé 75 % en HLG, 58 % en PQ : il doit redevenir blanc (un peu
        // gris : les hautes lumières sont compressées, comme avec ffmpeg), et le noir rester noir
        for (const auto &[t, blanc] : {std::pair{Transfert::Hlg, 0.7499}, std::pair{Transfert::Pq, 0.5807}}) {
            const ConversionHdr c(t);
            const uint16_t b = code(blanc);
            const uint16_t px[3] = {b, b, b};
            const auto r = c.pixel(px);
            QVERIFY(r[0] == r[1] && r[1] == r[2] && r[0] >= 225);
            const uint16_t noir[3] = {0, 0, 0};
            QVERIFY((c.pixel(noir) == std::array<uint8_t, 3>{0, 0, 0}));
        }
    }

    void dimensions_image_hdr()
    {
        const ConversionHdr conversion(Transfert::Hlg);
        const std::array<uint16_t, 6> deux_pixels{};
        QVERIFY(conversion.convertit(2, 1, std::span(deux_pixels).first(3)).isNull());
        QVERIFY(conversion.convertit(-1, 1, {}).isNull());
        QCOMPARE(conversion.convertit(2, 1, deux_pixels).size(), QSize(2, 1));
    }

    void hautes_lumieres_et_teintes()
    {
        const ConversionHdr c(Transfert::Pq);
        // une lumière très forte (1000 cd/m²) est compressée sans saturer brutalement, et le gris reste
        // gris
        const uint16_t fort = code(0.7518);
        const uint16_t gris[3] = {fort, fort, fort};
        auto px = c.pixel(gris);
        QVERIFY(px[0] == px[1] && px[1] == px[2] && px[0] >= 250);
        // un rouge reste rouge
        const uint16_t rouge[3] = {code(0.58), code(0.3), code(0.3)};
        px = c.pixel(rouge);
        QVERIFY(px[0] > 200 && px[1] < 80 && px[2] < 80);
    }

    void courbe_de_mobius()
    {
        QCOMPARE(mobius(0.3f), 0.3f);
        // continue au seuil, croissante, et la crête devient 1
        QVERIFY(std::abs(mobius(0.6001f) - 0.6001f) < 1e-3f);
        QVERIFY(mobius(2.0f) > mobius(1.0f));
        QVERIFY(std::abs(mobius(MOBIUS_CRETE) - 1.0f) < 1e-4f);
    }

    // ------------------------------------------------------------ vidéos
    void analyse_ppm()
    {
        QByteArray flux("P6\n2 1\n255\n\x01\x02\x03\x04\x05\x06P6 1 1 255 \x07\x08\x09", 32);
        qsizetype pos = 0;
        const Lecture lecture = lecture_de(flux, pos);
        auto img = lit_ppm(lecture, nullptr);
        QVERIFY(img.has_value());
        QCOMPARE(img->size(), QSize(2, 1));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(img->constScanLine(0)), 6), QByteArray("\x01\x02\x03\x04\x05\x06"));
        img = lit_ppm(lecture, nullptr);
        QVERIFY(img.has_value());
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(img->constScanLine(0)), 3), QByteArray("\x07\x08\x09"));
        QVERIFY(!lit_ppm(lecture, nullptr).has_value());
    }

    void journal_showinfo()
    {
        QList<QByteArray> lignes = {
            "Input #0, avi\n",
            "[Parsed_showinfo_0 @ 0x1] config in time_base: 1/25\n",
            "[Parsed_showinfo_0 @ 0x1] n:   0 pts:     78 pts_time:0.416333 duration:      1 fmt:yuv420p\n",
            "[Parsed_showinfo_0 @ 0x1] color_range:tv\n",
            "[Parsed_showinfo_0 @ 0x1] n:   1 pts:     79 pts_time:0.42167 duration: 1\n",
        };
        const LectureLigne ligne = [&]() -> std::optional<QByteArray> {
            if (lignes.isEmpty())
                return std::nullopt;
            return lignes.takeFirst();
        };
        QCOMPARE(lit_date(ligne), 0.416333);
        QCOMPARE(lit_date(ligne), 0.42167);
        QCOMPARE(lit_date(ligne), std::nullopt);
    }

    void nombres_rationnels()
    {
        QCOMPARE(rationnel(u"25/1"_s), 25.0);
        QCOMPARE(rationnel(u"0/0"_s), std::nullopt);
        QCOMPARE(rationnel(u"30000/1001"_s), 30000.0 / 1001.0);
    }

    void rotation_des_images()
    {
        QImage img(3, 2, QImage::Format_RGB888);
        img.fill(Qt::black);
        img.setPixelColor(0, 0, Qt::red);
        // un quart de tour dans le sens horaire : le coin haut-gauche passe en haut à droite
        const QImage r = tourne(img, 90);
        QCOMPARE(r.size(), QSize(2, 3));
        QCOMPARE(r.pixelColor(1, 0), QColor(Qt::red));
        QCOMPARE(tourne(img, 180).pixelColor(2, 1), QColor(Qt::red));
        QCOMPARE(tourne(img, 270).pixelColor(0, 2), QColor(Qt::red));
        QCOMPARE(tourne(tourne(img, 90), 270), img);
        QCOMPARE(tourne(img, -90), tourne(img, 270));
        QCOMPARE(tourne(img, 450), r);
        QVERIFY(!rotation_video(45));
        QVERIFY(tourne(img, 45).isNull());
    }

    void lecture_video()
    {
        auto ouverte = LecteurVideo::ouvre(outils::mire(16));
        QVERIFY2(ouverte.has_value(), qUtf8Printable(ouverte.error_or(QString())));
        LecteurVideo &v = **ouverte;
        QCOMPARE(v.info.nb_images, 16);
        QCOMPARE(v.info.cadence_lecture, 25.0);
        QCOMPARE(v.dimensions(), QSize(400, 300));
        // lecture séquentielle puis accès direct : les images doivent être identiques
        QList<QImage> sequentielles;
        for (int i = 1; i <= 16; ++i)
            sequentielles << *v.image(i);
        for (int i : {12, 3, 16, 7}) {
            v.oublie_cache_et_flux();
            QVERIFY2(*v.image(i) == sequentielles[i - 1], qUtf8Printable(u"image %1"_s.arg(i)));
        }
        v.fixe_rotation(90);
        QCOMPARE(v.dimensions(), QSize(300, 400));
        QCOMPARE(v.image(1)->size(), QSize(300, 400));
        v.fixe_rotation(45);
        QCOMPARE(v.rotation(), 90);
    }

    void decodage_par_lots_data()
    {
        QTest::addColumn<QSize>("taille");
        QTest::addColumn<int>("nombre");
        QTest::newRow("lot de plusieurs images") << QSize(800, 450) << 100;
        QTest::newRow("une seule image 4K par lot") << QSize(3840, 2160) << 5;
    }

    /// Le journal de FFmpeg donne la limite réellement reçue. Le test vérifie la quantité
    /// préparée sans dépendre de la consommation de mémoire du système de test.
    void decodage_par_lots()
    {
        QFETCH(QSize, taille);
        QFETCH(int, nombre);
        const QString dimensions = u"%1x%2"_s.arg(taille.width()).arg(taille.height());
        const auto film = outils::video_test(u"lots-%1.avi"_s.arg(dimensions),
            {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc2=s=%1:r=25"_s.arg(dimensions),
             u"-frames:v"_s, QString::number(nombre), u"-c:v"_s, u"mpeg4"_s});
        QVERIFY(film);
        QTemporaryDir dossier;
        const QString avant = QDir::currentPath();
        const QByteArray reglage = qgetenv("FFREPORT");
        const auto retablit = qScopeGuard([&] {
            QDir::setCurrent(avant);
            if (reglage.isNull())
                qunsetenv("FFREPORT");
            else
                qputenv("FFREPORT", reglage);
        });
        QVERIFY(QDir::setCurrent(dossier.path()));
        qputenv("FFREPORT", "file=%p.log:level=32");
        const auto limite = [&] {
            const QString journal = lit_texte(dossier.filePath(u"ffmpeg.log"_s));
            return journal.section(u"-frames:v "_s, 1, 1).section(u' ', 0, 0).toInt();
        };
        auto lecteur = LecteurVideo::ouvre(*film);
        QVERIFY2(lecteur.has_value(), qPrintable(lecteur.error_or(QString())));
        QCOMPARE(limite(), 1);
        QVERIFY((*lecteur)->image(2));
        const int lot = limite();
        QVERIFY(lot >= 1 && lot <= 32);
        QVERIFY(qint64(lot) * taille.width() * taille.height() * 3 <= 32 * 1024 * 1024);
        // Lire au-delà du lot doit lancer le suivant et retrouver la bonne trame.
        for (int i = 3; i <= lot + 2; ++i)
            QVERIFY((*lecteur)->image(i));
        const QImage derniere = *(*lecteur)->image(lot + 2);
        (*lecteur)->oublie_cache_et_flux();
        QCOMPARE(*(*lecteur)->image(lot + 2), derniere);
    }

    /// l'accès direct à une image doit donner la même image que la lecture séquentielle, quel que
    /// soit le format
    void acces_direct_exact_data()
    {
        QTest::addColumn<QString>("nom");
        QTest::addColumn<QStringList>("options");
        QTest::newRow("H.264") << u"h264.mp4"_s << QStringList{u"-c:v"_s, u"libx264"_s};
        QTest::newRow("VP9 avec images perdues") << u"rapide.avi"_s
            << QStringList{u"-vf"_s, u"select='not(between(n,1,5))'"_s, u"-fps_mode"_s, u"passthrough"_s,
                           u"-c:v"_s, u"libvpx-vp9"_s, u"-cpu-used"_s, u"8"_s};
        QTest::newRow("Theora") << u"theora.ogv"_s << QStringList{u"-c:v"_s, u"libtheora"_s};
        QTest::newRow("MPEG-4") << u"mpeg4.avi"_s << QStringList{u"-c:v"_s, u"mpeg4"_s};
        QTest::newRow("Xvid avec images B") << u"xvid-compact.avi"_s
            << QStringList{u"-c:v"_s, u"libxvid"_s, u"-bf"_s, u"2"_s,
                           u"-bsf:v"_s, u"setts=pts=N:dts=N:duration=1"_s};
    }

    void acces_direct_exact()
    {
        QFETCH(QString, nom);
        QFETCH(QStringList, options);
        const bool rapide = nom == u"rapide.avi";
        const QString cadence = rapide ? u"78125/417"_s : u"25"_s;
        const auto fichier = outils::video_test(nom,
            QStringList{u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc2=s=160x120:r="_s + cadence,
                        u"-frames:v"_s, u"34"_s} + options);
        if (!fichier)
            QSKIP("encodeur absent");
        auto ouverte = LecteurVideo::ouvre(*fichier);
        QVERIFY2(ouverte.has_value(), qUtf8Printable(nom));
        LecteurVideo &v = **ouverte;
        // Xvid retarde ses dernières trames. Le filtre setts évite des paquets vides
        // au début de l'AVI et chaque position annoncée doit être lisible.
        const int n = v.info.nb_images;
        if (nom == u"xvid-compact.avi")
            QVERIFY(n >= 32 && n <= 34);
        else
            QCOMPARE(n, 34);
        QList<QImage> sequentielles;
        for (int i = 1; i <= n; ++i) {
            const auto image = v.image(i);
            QVERIFY2(image.has_value(), qUtf8Printable(u"%1 : image %2"_s.arg(nom).arg(i)));
            sequentielles << *image;
        }
        if (rapide) {
            // caméra rapide (187,35 images/s) ; les 5 trames après la première sont perdues (les
            // dates sautent de 0 à 6) : la cadence est variable, chaque trame est une image, et la
            // deuxième image est datée de 6 intervalles
            QVERIFY(v.info.cadence_variable);
            QVERIFY(std::abs(v.info.cadence_lecture - 78125.0 / 417.0) < 1e-3);
            const auto instants = *v.info.instants();
            QVERIFY(std::abs(instants[1] - 6.0) < 1e-3 && std::abs(instants[2] - 7.0) < 1e-3);
            QVERIFY(sequentielles[1] != sequentielles[0]);
        } else {
            QVERIFY2(!v.info.cadence_variable, qUtf8Printable(nom));
            QVERIFY2(!v.info.dates_reelles, qUtf8Printable(nom));
        }
        for (int i : {n, 2, n / 2, 7, n - 1, 1}) {
            v.oublie_cache_et_flux();
            const auto image = v.image(i);
            QVERIFY2(image && *image == sequentielles[i - 1], qUtf8Printable(u"%1 : image %2"_s.arg(nom).arg(i)));
        }
    }

    /// Vidéos HDR (HLG de l'iPhone, PQ du HDR10) fabriquées à partir d'une vidéo ordinaire : une fois
    /// converties, elles doivent redonner ses couleurs.
    void video_hdr()
    {
        const auto reference = outils::video_test(
            u"degrade-sdr.mkv"_s, {u"-f"_s, u"lavfi"_s, u"-i"_s,
                u"nullsrc=s=320x240:r=25,geq=lum=48+130*X/W:cb=124+8*Y/H:cr=130-6*Y/H"_s,
                u"-frames:v"_s, u"8"_s, u"-c:v"_s, u"libx264"_s, u"-qp"_s, u"0"_s});
        if (!reference)
            QSKIP("libx264 absent");
        const QString lineaire =
            u"zscale=tin=bt709:min=bt709:pin=bt709:rin=tv:t=linear:p=bt709:npl=203,format=gbrpf32le,zscale=p=bt2020"_s;
        for (const auto &[nom, trc, transfert] :
             {std::tuple{u"degrade-hlg.mov"_s, u"arib-std-b67"_s, Transfert::Hlg}, std::tuple{u"degrade-pq.mov"_s, u"smpte2084"_s, Transfert::Pq}}) {
            const QString filtres = lineaire + u",zscale=t=%1:m=bt2020nc:r=tv:npl=203,format=yuv420p10le"_s.arg(trc);
            const auto f = outils::video_test(
                nom, {u"-i"_s, *reference, u"-vf"_s, filtres, u"-c:v"_s, u"libx265"_s, u"-x265-params"_s,
                      u"log-level=error:lossless=1"_s, u"-color_primaries"_s, u"bt2020"_s, u"-color_trc"_s, trc,
                      u"-colorspace"_s, u"bt2020nc"_s, u"-tag:v"_s, u"hvc1"_s});
            if (!f)
                QSKIP("libx265 ou zscale absent");
            auto v = std::move(*LecteurVideo::ouvre(*f));
            QCOMPARE(v->info.hdr, transfert);
            auto r = std::move(*LecteurVideo::ouvre(*reference));
            for (int i : {1, 5}) {
                const double e = ecart_moyen(*v->image(i), *r->image(i));
                QVERIFY2(e < 2.5, qUtf8Printable(u"%1, image %2 : écart moyen %3"_s.arg(nom).arg(i).arg(e)));
            }
            // sans conversion, les couleurs seraient très différentes
            v->sans_conversion_hdr();
            v->oublie_cache_et_flux();
            const double e = ecart_moyen(*v->image(1), *r->image(1));
            QVERIFY2(e > 8.0, qUtf8Printable(u"%1 sans conversion : écart moyen %2"_s.arg(nom).arg(e)));
        }
    }

    /// Vidéo à cadence variable, comme celles des téléphones : 30 images/s, une image perdue, puis
    /// 24 images/s (faible lumière). Un carré rouge avance de 60 pixels par seconde de temps réel.
    void video_cadence_variable()
    {
        const auto f = outils::video_test(
            u"cadence-variable-4.mp4"_s,
            {u"-filter_complex"_s,
             u"color=c=gray:s=320x240:r=30:d=4,settb=1/90000,"
             "setpts='if(lt(N,80),N/30,80/30+(N-80)/24)/TB',select='not(eq(n,20))'[fond];"
             "color=c=red:s=16x16[carre];[fond][carre]overlay=x='20+60*t':y=100:eval=frame:shortest=1:format=yuv444[v]"_s,
             u"-map"_s, u"[v]"_s, u"-fps_mode"_s, u"passthrough"_s, u"-video_track_timescale"_s, u"90000"_s, u"-c:v"_s,
             u"libx264"_s, u"-qp"_s, u"0"_s, u"-pix_fmt"_s, u"yuv444p"_s});
        if (!f)
            QSKIP("libx264 absent");
        auto v = std::move(*LecteurVideo::ouvre(*f));
        QVERIFY(v->info.cadence_variable);
        QCOMPARE(v->info.nb_images, 119);
        QVERIFY(proche(v->info.duree_lecture, 4.333333, 0.001));
        QVERIFY(proche(v->info.duree_a_la_cadence(2 * v->info.cadence_lecture), 2.166667, 0.001));
        QVERIFY(std::abs(v->info.cadence_lecture - 30.0) < 1e-3);
        const auto instants = *v->info.instants();
        // position du carré rouge : bord gauche des pixels rouges
        const auto bord = [](const QImage &img) -> std::optional<int> {
            for (int x = 0; x < img.width(); ++x) {
                const QColor p = img.pixelColor(x, 108);
                if (p.red() > 200 && p.green() < 60)
                    return x;
            }
            return std::nullopt;
        };
        Pointage p;
        p.alloue_mesures(1, 1.0 / v->info.cadence_lecture, v->info.nb_images);
        p.instants = instants;
        for (int i : {1, 19, 20, 21, 22, 23, 60, 79, 80, 81, 82, 100, 119, 2, 50}) {
            const QImage img = *v->image(i);
            const double t = p.date(i - 1);
            const auto x = bord(img);
            QVERIFY2(x.has_value(), "carré rouge");
            QVERIFY2(std::abs(*x - std::floor(20.0 + 60.0 * t)) <= 1.0, qUtf8Printable(u"image %1 : x = %2, t = %3"_s.arg(i).arg(*x).arg(t)));
            p.memorise_point(1, {double(*x), 108.0}, i - 1);
        }
        // la vitesse est la même avant et après l'image perdue
        for (int i : {21, 22, 81})
            QVERIFY(std::abs(p.vitesse_px(1, i - 1)->x - 60.0) < 35.0);
        // l'image 21 vient après une image perdue : 2 intervalles
        QVERIFY(std::abs(instants[20] - instants[19] - 2.0) < 1e-3);
    }

    /// vidéo au ralenti d'un téléphone Android : filmée à 240 images/s, enregistrée pour être lue à
    /// 30 images/s
    void video_ralenti()
    {
        const auto f = outils::video_test(
            u"ralenti.mp4"_s, {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc2=s=320x240:r=30:d=1"_s, u"-c:v"_s, u"libx264"_s,
                               u"-metadata"_s, u"com.android.capture.fps=240.000000"_s, u"-movflags"_s, u"use_metadata_tags"_s});
        if (!f)
            QSKIP("libx264 absent");
        const auto v = std::move(*LecteurVideo::ouvre(*f));
        QCOMPARE(v->info.cadence_lecture, 30.0);
        QCOMPARE(v->info.cadence_capture, 240.0);
        QVERIFY(!v->info.cadence_variable);
    }

    void duree_de_la_video_avec_une_piste_son_plus_longue()
    {
        const auto film = outils::video_test(u"son-apres-la-fin-du-film.mkv"_s,
            {u"-f"_s, u"lavfi"_s, u"-i"_s, u"color=s=96x64:r=25:d=0.4"_s,
             u"-f"_s, u"lavfi"_s, u"-i"_s, u"anullsrc=r=8000:cl=mono:d=2"_s,
             u"-c:v"_s, u"ffv1"_s, u"-c:a"_s, u"pcm_s16le"_s});
        QVERIFY(film);
        auto lecteur = LecteurVideo::ouvre(*film);
        QVERIFY2(lecteur.has_value(), qPrintable(lecteur.error_or(QString())));
        QCOMPARE((*lecteur)->info.nb_images, 10);
        QVERIFY(proche((*lecteur)->info.duree_lecture, 0.4, 0.001));
    }

    /// vidéo filmée en portrait : le téléphone l'enregistre couchée, avec une rotation dans les
    /// métadonnées
    void video_portrait()
    {
        const auto couchee = outils::video_test(
            u"couchee.mp4"_s, {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc2=s=320x240:r=30:d=1"_s, u"-c:v"_s, u"libx264"_s});
        if (!couchee)
            QSKIP("libx264 absent");
        const auto f = outils::video_test(u"portrait.mp4"_s,
                                          {u"-display_rotation"_s, u"90"_s, u"-i"_s, *couchee, u"-c"_s, u"copy"_s});
        if (!f)
            QSKIP("-display_rotation absent");
        auto v = std::move(*LecteurVideo::ouvre(*f));
        QCOMPARE(v->dimensions(), QSize(240, 320));
        QCOMPARE(v->image(10)->size(), QSize(240, 320));
    }

    /// Les formats des téléphones et des appareils actuels : chaque vidéo, fabriquée par ffmpeg, doit
    /// s'ouvrir avec les bonnes dimensions et donner les mêmes images en accès direct qu'en lecture
    /// dans l'ordre. Un format dont ce ffmpeg n'a pas l'encodeur est sauté.
    void formats_de_telephone()
    {
        const QStringList source = {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc2=s=320x240:r=30:d=1"_s};
        const auto portrait = [](const std::optional<QString> &couchee, const QString &nom, const QString &rotation) {
            if (!couchee)
                return std::optional<QString>();
            return outils::video_test(nom, {u"-display_rotation"_s, rotation, u"-i"_s, *couchee, u"-c"_s, u"copy"_s});
        };
        // une pochette (image JPEG) ajoutée à une vidéo, comme le font certaines applications
        const auto pochette = [](const std::optional<QString> &base) {
            if (!base)
                return std::optional<QString>();
            const auto image = outils::video_test(u"pochette.jpg"_s, {u"-f"_s, u"lavfi"_s, u"-i"_s, u"color=c=blue:s=64x64"_s,
                                                                     u"-frames:v"_s, u"1"_s, u"-update"_s, u"1"_s});
            if (!image)
                return std::optional<QString>();
            return outils::video_test(u"pochette.mp4"_s, {u"-i"_s, *base, u"-i"_s, *image, u"-map"_s, u"0"_s, u"-map"_s,
                                                          u"1"_s, u"-c"_s, u"copy"_s, u"-disposition:v:1"_s, u"attached_pic"_s});
        };
        const QString hlg = u"log-level=error:colorprim=bt2020:transfer=arib-std-b67:colormatrix=bt2020nc"_s;
        const std::tuple<QString, std::optional<QString>, QSize> cas[] = {
            {u"iPhone : HEVC 10 bits HLG, MOV, en portrait"_s,
             portrait(outils::video_test(u"couchee-hlg.mov"_s, source + QStringList{u"-c:v"_s, u"libx265"_s, u"-x265-params"_s, hlg,
                                                                                    u"-pix_fmt"_s, u"yuv420p10le"_s, u"-tag:v"_s, u"hvc1"_s}),
                      u"IMG_0001.MOV"_s, u"90"_s),
             QSize(240, 320)},
            {u"Android : HEVC, MP4, en portrait"_s,
             portrait(outils::video_test(u"couchee-hevc.mp4"_s,
                                         source + QStringList{u"-c:v"_s, u"libx265"_s, u"-x265-params"_s, u"log-level=error"_s}),
                      u"VID_android.mp4"_s, u"270"_s),
             QSize(240, 320)},
            {u"AV1, MP4"_s,
             outils::video_test(u"av1.mp4"_s, source + QStringList{u"-c:v"_s, u"libaom-av1"_s, u"-cpu-used"_s, u"8"_s, u"-row-mt"_s, u"1"_s}),
             QSize(320, 240)},
            {u"VP9, WebM"_s,
             outils::video_test(u"vp9.webm"_s, source + QStringList{u"-c:v"_s, u"libvpx-vp9"_s, u"-deadline"_s, u"realtime"_s,
                                                                    u"-cpu-used"_s, u"8"_s}),
             QSize(320, 240)},
            {u"ProRes, MOV"_s,
             outils::video_test(u"prores.mov"_s, source + QStringList{u"-c:v"_s, u"prores_ks"_s, u"-profile:v"_s, u"2"_s}),
             QSize(320, 240)},
            {u"Motion JPEG, AVI"_s,
             outils::video_test(u"mjpeg.avi"_s, source + QStringList{u"-c:v"_s, u"mjpeg"_s, u"-q:v"_s, u"3"_s}), QSize(320, 240)},
            {u"H.264, MP4 avec une pochette"_s,
             pochette(outils::video_test(u"base-h264.mp4"_s, source + QStringList{u"-c:v"_s, u"libx264"_s})), QSize(320, 240)},
        };
        for (const auto &[nom, chemin, dimensions] : cas) {
            if (!chemin) {
                qWarning("%s : test sauté", qUtf8Printable(nom));
                continue;
            }
            auto ouverte = LecteurVideo::ouvre(*chemin);
            QVERIFY2(ouverte.has_value(), qUtf8Printable(nom + u" : "_s + ouverte.error_or(QString())));
            LecteurVideo &v = **ouverte;
            QCOMPARE(v.dimensions(), dimensions);
            QCOMPARE(v.info.nb_images, 30);
            QVERIFY(!v.info.cadence_variable);
            if (nom.startsWith(u"iPhone"_s)) {
                QCOMPARE(v.info.hdr, Transfert::Hlg);
                QCOMPARE(nom_codec(v.info.codec), u"HEVC (H.265)"_s);
            }
            QList<QImage> sequentielles;
            for (int i = 1; i <= 30; ++i)
                sequentielles << *v.image(i);
            QCOMPARE(sequentielles[0].size(), dimensions);
            for (int i : {30, 2, 17, 1}) {
                v.oublie_cache_et_flux();
                const auto image = v.image(i);
            QVERIFY2(image && *image == sequentielles[i - 1], qUtf8Printable(u"%1 : image %2"_s.arg(nom).arg(i)));
            }
        }
    }

    /// un fichier qui n'est pas une vidéo est refusé avec un message clair
    void fichier_qui_n_est_pas_une_video()
    {
        // (pas un fichier .txt : ffmpeg le lirait comme une animation de texte)
        const auto v = LecteurVideo::ouvre(QString::fromUtf8(MEVCINE_SOURCES) + u"/src/vecteur.h"_s);
        QVERIFY(!v.has_value());
        QVERIFY(!v.error().isEmpty());
    }

    void detection_de_la_cadence()
    {
        std::vector<double> reguliere;
        for (int i = 0; i < 10; ++i)
            reguliere.push_back(i / 25.0);
        QCOMPARE(cadence_variable(reguliere), std::nullopt);
        // arrondis de la base de temps : 20 ou 21 tics de 1/600 s
        std::vector<double> arrondie;
        for (int i = 0; i < 10; ++i)
            arrondie.push_back(std::round(i * 20.02) / 600.0);
        QCOMPARE(cadence_variable(arrondie), std::nullopt);
        std::vector<double> perdue = reguliere;
        perdue.erase(perdue.begin() + 4);
        QVERIFY(proche(*cadence_variable(perdue), 0.04));
    }

    // ------------------------------------------------------------ suivi automatique
    void retrouve_un_motif()
    {
        const QImage img = bruit(200, 150, 7);
        const QImage motif = *detect::decoupe(img, 120, 40, 16, 12);
        QCOMPARE(detect::localise_motif(motif, img, std::nullopt), QPoint(120, 40));
        bool douteux = false;
        QImage fond(img.size(), QImage::Format_RGB888);
        fond.fill(Qt::white);
        QVERIFY(!detect::localise_motif(motif, fond, std::nullopt, &douteux));
        // La texture reste identifiable malgré un fort changement d'éclairage.
        for (int y = 0; y < motif.height(); ++y) {
            for (int x = 0; x < motif.width(); ++x) {
                const QColor c = motif.pixelColor(x, y);
                fond.setPixelColor(x + 32, y + 45, QColor(c.red() * 2 / 3, c.green() * 2 / 3, c.blue() * 2 / 3));
            }
        }
        QCOMPARE(detect::localise_motif(motif, fond, std::nullopt, &douteux), QPoint(32, 45));
        QVERIFY(douteux);
        QCOMPARE(detect::localise_motif(motif, img, std::nullopt, &douteux), QPoint(120, 40));
        QVERIFY(!douteux);
        // recherche locale, réussie
        QCOMPARE(detect::localise_motif(motif, img, QPoint(110, 50)), QPoint(120, 40));
        // recherche locale ratée (trop loin) puis globale
        QCOMPARE(detect::localise_motif(motif, img, QPoint(0, 130)), QPoint(120, 40));
        // motif vide ou plus grand que l'image : aucune position
        QCOMPARE(detect::localise_motif(QImage(), img, std::nullopt), std::nullopt);
        QCOMPARE(detect::localise_motif(QImage(img.width() + 1, 1, QImage::Format_RGB888), img, std::nullopt),
                 std::nullopt);
    }

    void suit_un_motif_deplace()
    {
        QImage img = bruit(160, 120, 3);
        const QImage objet = bruit(10, 10, 99);
        for (int y = 0; y < 10; ++y)
            std::memcpy(img.scanLine(33 + y) + 3 * 70, objet.constScanLine(y), 30);
        QCOMPARE(detect::localise_motif(objet, img, QPoint(60, 30)), QPoint(70, 33));
    }

    void suivi_ambiguite_et_prediction()
    {
        const QImage motif = bruit(12, 14, 42);
        const auto trame = [&](std::initializer_list<int> xs) {
            QImage image(128, 50, QImage::Format_RGB888);
            image.fill(Qt::black);
            for (int x : xs)
                for (int y = 0; y < motif.height(); ++y)
                    std::memcpy(image.scanLine(16 + y) + 3*x, motif.constScanLine(y), 3*motif.width());
            return detect::Image(image);
        };
        detect::Piste piste(motif, QPoint(18, 16), 0.0);
        auto image = trame({26});
        const auto simple = piste.recherche(image, 0.04);
        QCOMPARE(simple.coin, QPoint(26, 16));
        QVERIFY(!simple.a_verifier);
        piste.enregistre(simple, 0.04);
        // Un intervalle double prévoit un déplacement double. Les deux mobiles sont sur la même ligne.
        image = trame({25, 42});
        const auto choix = piste.recherche(image, 0.12);
        QCOMPARE(choix.coin, QPoint(42, 16));
        QVERIFY(!choix.a_verifier);
        piste.enregistre(choix, 0.12);
        auto perdue = trame({});
        const auto absence = piste.recherche(perdue, 0.16);
        QVERIFY(!absence.coin);
        piste.enregistre(absence, 0.16);
        // Une mesure incertaine ou une occultation ne déplace pas la piste vers l'autre objet.
        piste.enregistre({QPoint(25,16), true}, 0.20);
        QCOMPARE(piste.dernier_coin(), QPoint(42,16));
        image = trame({30, 66});
        QCOMPARE(piste.recherche(image,0.24).coin, QPoint(66,16));
        // Sans historique, deux objets identiques sur la même ligne exigent une vérification.
        detect::Piste inconnue(motif, std::nullopt, 0.0);
        const auto ambigu = inconnue.recherche(image,0.24);
        QVERIFY(ambigu.coin);
        QVERIFY(ambigu.a_verifier);
        QImage uni(30,30,QImage::Format_RGB888);
        uni.fill(Qt::gray);
        const QImage neutre = uni.copy(2,2,9,9);
        QVERIFY(!detect::localise_motif(neutre, uni, QPoint(3,2)));
        uni.setPixelColor(14, 9, Qt::red);
        QVERIFY(!detect::localise_motif(neutre, uni, QPoint(3,2)));
    }

    void visee_sur_un_detail()
    {
        const QImage motif = bruit(13, 13, 37);
        const auto trame = [&](std::initializer_list<QPoint> coins) {
            QImage image(176, 92, QImage::Format_RGB888);
            image.fill(Qt::darkGray);
            for (const QPoint coin : coins)
                for (int y = 0; y < motif.height(); ++y)
                    std::memcpy(image.scanLine(coin.y() + y) + 3 * coin.x(), motif.constScanLine(y), 3 * motif.width());
            return image;
        };
        const QImage avant = trame({{46, 41}});
        const QPointF detail(51.25, 47.75);
        const QImage avance = trame({{65, 39}});
        const QPointF suivant = detail + QPointF(19, -2);
        QCOMPARE(detect::retrouve_detail(avant, avance, detail), suivant);
        QCOMPARE(detect::retrouve_detail(avance, avant, suivant), detail); // rebond
        QVERIFY(!detect::retrouve_detail(avant, trame({}), detail));
        QVERIFY(!detect::retrouve_detail(avant, trame({{65, 39}, {30, 43}}), detail));
        QVERIFY(!detect::retrouve_detail(trame({{46, 41}, {21, 40}}), avance, detail));
        QVERIFY(!detect::retrouve_detail(avant, trame({{152, 41}}), detail));
        QVERIFY(!detect::retrouve_detail(avant, avance, {132, 70}));
        QVERIFY(!detect::retrouve_detail(avant, avance, {51, 24})); // fond uni à côté du mobile
        QCOMPARE(detect::retrouve_detail(trame({{0, 0}}), trame({{5, 4}}), {3, 2}), QPointF(8, 6));
    }

    void exemples_visee_par_objet()
    {
        using Saisie = Pointage::Saisie;
        Pointage mesures;
        mesures.alloue_mesures(2, 0.04, 32);
        for (int i = 0; i < 25; ++i) {
            mesures.memorise_point(1, {double(i), 30}, i, i % 2 ? Saisie::Assistee : Saisie::Manuelle);
            mesures.memorise_point(2, {double(i), 50}, i, Saisie::Automatique);
        }
        auto exemples = mesures.exemples_visee(1, 17);
        QCOMPARE(exemples.size(), 5u);
        QCOMPARE(exemples.front().first, 0);
        QCOMPARE(exemples.back().first, 16);
        for (const auto &[i, p] : exemples)
            QVERIFY(i % 2 == 0 && p.y == 30);
        QVERIFY(mesures.exemples_visee(2, 25).empty());
        mesures.memorise_point(2, {12, 51}, 9, Saisie::AssisteeCorrigee);
        QCOMPARE(mesures.exemples_visee(2, 10).size(), 1u);
        QVERIFY(mesures.exemples_visee(2, 9).empty());
        mesures.supprime_points({{0, 1}, {9, 2}});
        QCOMPARE(mesures.exemples_visee(1, 17).front().first, 2);
        QVERIFY(mesures.exemples_visee(2, 25).empty());
        mesures.annule_mesure();
        QCOMPARE(mesures.exemples_visee(1, 17), exemples);
        QCOMPARE(mesures.exemples_visee(2, 25).front().second, Vecteur(12, 51));
    }

    void visee_deplacement_fractionnaire()
    {
        const auto trame = [](QPointF centre) {
            QImage image(164, 104, QImage::Format_RGB888);
            for (int y = 0; y < image.height(); ++y) {
                uchar *p = image.scanLine(y);
                for (int x = 0; x < image.width(); ++x, p += 3) {
                    const double dx = x - centre.x(), dy = y - centre.y();
                    const double v = std::exp(-dx * dx / 68 - dy * dy / 48);
                    p[0] = std::lround(22 + 196 * v);
                    p[1] = std::lround(28 + 150 * v);
                    p[2] = std::lround(41 + 100 * v);
                }
            }
            return image;
        };
        const QPointF depart(63.25, 54.5), detail = depart + QPointF(-1.75, 2.25);
        const std::array exemple{detect::prepare_detail(trame(depart), detail)};
        for (const QPointF pas : {QPointF(1.25, -0.25), QPointF(-1.75, 0.25), QPointF(0.25, 0.25)}) {
            QPointF derniere = detail;
            for (int i = 1; i <= 24; ++i) {
                const auto p = detect::retrouve_detail(exemple, trame(depart + i * pas), derniere);
                QVERIFY(p);
                const auto erreur = *p - (detail + i * pas);
                QVERIFY(std::hypot(erreur.x(), erreur.y()) < 0.1);
                derniere = *p;
            }
        }
    }

    void visee_exemples_et_rotation()
    {
        const auto vue = [](double angle, QPointF centre) {
            QImage image(175, 110, QImage::Format_RGB32);
            image.fill(QColor(81, 85, 88));
            QPainter p(&image);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(centre);
            p.rotate(angle);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(234, 183, 76));
            p.drawEllipse(QRectF(-13, -9, 26, 18));
            p.setBrush(QColor(23, 46, 91));
            p.drawRect(QRectF(-9, -2, 18, 4));
            p.drawRect(QRectF(-2, -8, 4, 8));
            p.setBrush(QColor(250, 243, 213));
            p.drawEllipse(QRectF(-1.7, -1.7, 3.4, 3.4));
            return image;
        };
        const QPointF depart(44.5, 52.5);
        std::vector<detect::ReferenceDetail> exemples;
        for (int i = 0; i < 5; ++i) {
            const QPointF centre = depart + QPointF(4 * i, 0);
            exemples.push_back(detect::prepare_detail(vue(22.5 * i, centre), centre));
        }
        const auto compte = [&](std::span<const detect::ReferenceDetail> refs) {
            int reconnues = 0;
            for (int i = 0; i < 18; ++i) {
                const QPointF centre(106.5 + (7 * i + 3) % 19, 55.5);
                const auto p = detect::retrouve_detail(refs, vue(2.5 + 5 * i, centre), centre - QPointF(6, 1));
                if (p && std::hypot(p->x() - centre.x(), p->y() - centre.y()) > 1)
                    return -1;
                reconnues += p.has_value();
            }
            return reconnues;
        };
        const int une = compte(std::span(exemples).first(1));
        const int cinq = compte(exemples);
        QVERIFY(une > 0 && cinq > une);

        // Deux repères placés à des endroits différents sur le même objet donnent deux positions sûres mais incompatibles.
        const QImage fixe = vue(0, depart);
        const QPointF voisin = depart + QPointF(6, 0);
        const std::array autres{detect::prepare_detail(fixe, voisin)};
        QCOMPARE(detect::retrouve_detail(autres, fixe, depart), voisin);
        const std::array conflit{exemples.front(), autres.front()};
        QVERIFY(!detect::retrouve_detail(conflit, fixe, depart));
    }

    /// vidéos 1080p et 4K : recherche réduite puis affinage, au pixel près
    void grandes_images()
    {
        for (const auto &[w, h, rayon, f] : {std::tuple{1920, 1080, 30, 2}, std::tuple{3840, 2160, 60, 4}}) {
            const QPoint depart(w / 3, h / 2);
            const QImage img = grande_image(w, h, depart, rayon);
            const QImage motif = *detect::decoupe(img, depart.x() - 4, depart.y() - 4, 2 * rayon + 8, 2 * rayon + 8);
            QCOMPARE(detect::facteur(img, motif), f);
            for (const QPoint arrivee : {QPoint(w / 3 + 37, h / 2 - 23), QPoint(w / 3 + 3 * rayon + 7, h / 2 + 11), QPoint(w - 3 * rayon, 5)}) {
                const QImage image = grande_image(w, h, arrivee, rayon);
                const QPoint attendu = arrivee - QPoint(4, 4);
                if (arrivee == depart + QPoint(37, -23)) {
                    const QPoint centre(rayon, rayon);
                    QCOMPARE(detect::retrouve_detail(img, image, depart + centre), QPointF(arrivee + centre));
                }
                // recherche locale près de la position précédente, puis recherche dans toute l'image
                QCOMPARE(detect::localise_motif(motif, image, depart - QPoint(4, 4)), attendu);
                QCOMPARE(detect::localise_motif(motif, image, std::nullopt), attendu);
            }
        }
    }

    void petites_videos_sans_reduction()
    {
        const QImage motif(64, 64, QImage::Format_RGB888);
        QCOMPARE(detect::facteur(QImage(720, 576, QImage::Format_RGB888), motif), 1);
        QCOMPARE(detect::facteur(QImage(1280, 720, QImage::Format_RGB888), motif), 2);
        // motif trop petit pour être réduit
        QCOMPARE(detect::facteur(QImage(1280, 720, QImage::Format_RGB888), QImage(20, 20, QImage::Format_RGB888)), 1);
    }

    void reduction()
    {
        QImage img(4, 2, QImage::Format_RGB888);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 4; ++x)
                img.setPixelColor(x, y, QColor(x * 10, 0, 255));
        }
        const QImage r = detect::reduit(img, 2);
        QCOMPARE(r.size(), QSize(2, 1));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(r.constScanLine(0)), 6), QByteArray("\x05\x00\xff\x19\x00\xff", 6));
        // facteur 1 : l'image est seulement convertie, sans réduction
        QCOMPARE(detect::reduit(img, 1).size(), img.size());
    }

    void decoupe_aux_bords()
    {
        const QImage img = bruit(20, 10, 1);
        QCOMPARE(detect::decoupe(img, 15, 5, 10, 10)->size(), QSize(5, 5));
        QVERIFY(!detect::decoupe(img, 25, 5, 10, 10).has_value());
    }
};

QTEST_GUILESS_MAIN(Tests)
#include "tests.moc"
