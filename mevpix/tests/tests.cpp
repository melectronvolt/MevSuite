// Tests du code sans interface.

#include "fichier_seance.h"
#include "help.h"
#include "i18n.h"
#include "model.h"
#include "profile.h"
#include "projet.h"
#include "session.h"
#include "settings.h"
#include "view.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QLockFile>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTest>

#include <cmath>
#include <functional>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

bool proche(double a, double b, double eps = 1e-6)
{
    return std::abs(a - b) < eps;
}

/// Image dont chaque pixel est donné par `f(x, y)`.
QImage fabrique_image(int w, int h, const std::function<QColor(int, int)> &f)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            img.setPixelColor(x, y, f(x, y));
    }
    return img;
}

/// Image dont la composante rouge vaut x (dégradé horizontal), vert = y, bleu = 0 (modulo 256).
QImage degrade(int w, int h)
{
    return fabrique_image(w, h, [](int x, int y) { return QColor(x & 0xff, y & 0xff, 0); });
}

void ecrit_fichier(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

/// Repère les parties claires d'une ligne du PNG, entre les abscisses données.
/// Les extrémités sont les bords de pixels, comme les points placés sur le canevas.
QList<Segment> plages_claires(const QImage &image, int y, int premiereColonne, int fin, int seuil)
{
    QList<Segment> plages;
    for (int x = premiereColonne; x < fin; ++x) {
        if (qGray(image.pixel(x, y)) < seuil)
            continue;
        const int debut = x;
        while (x + 1 < fin && qGray(image.pixel(x + 1, y)) >= seuil)
            ++x;
        plages.append({{double(debut), y + 0.5}, {double(x + 1), y + 0.5}});
    }
    return plages;
}

} // namespace

class Tests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ model
    void geometrie_segment()
    {
        const Segment s{{0, 0}, {3, 4}};
        QCOMPARE(s.longueur_px(), 5.0);
        QCOMPARE(s.point_en(0.5), QPointF(1.5, 2.0));
        const QPointF n = s.normale();
        QVERIFY(proche(longueur(n), 1.0));
        QVERIFY(proche(QPointF::dotProduct(n, s.b - s.a), 0.0));
        QVERIFY(proche(s.projette(QPointF(1.5, 2.0) + n * 3.0), 0.5));
        QCOMPARE(s.projette(QPointF(-10, 0)), 0.0);
        QCOMPARE(s.projette(QPointF(30, 40)), 1.0);
    }

    void alignement()
    {
        QCOMPARE(aligne_sur_axe(QPointF(0, 0), QPointF(10, 2)), QPointF(10, 0));
        QCOMPARE(aligne_sur_axe(QPointF(0, 0), QPointF(-1, 7)), QPointF(0, 7));
    }

    void etalonnage()
    {
        // 10 mm sur 250 px → 0,04 mm/px
        const auto c = Etalonnage::fabrique(250.0, 10.0, Unite::Millimetre);
        QVERIFY(c);
        QVERIFY(proche(c->unites_par_pixel(), 0.04, 1e-12));
        QVERIFY(proche(c->en_reel(500.0), 20.0, 1e-9));
        QCOMPARE(texte_longueur(311.0, c, u','), u"12,4 mm"_s);
        QCOMPARE(texte_longueur(311.0, c, u'.'), u"12.4 mm"_s);
        QCOMPARE(texte_longueur(311.0, std::nullopt, u','), u"311 px"_s);
        QVERIFY(!Etalonnage::fabrique(0.5, 10.0, Unite::Millimetre));
        QVERIFY(!Etalonnage::fabrique(100.0, 0.0, Unite::Millimetre));
        QVERIFY(!Etalonnage::fabrique(100.0, std::nan(""), Unite::Millimetre));
        QVERIFY(!Etalonnage::fabrique(100.0, std::numeric_limits<double>::denorm_min(), Unite::Metre));
        QVERIFY(!Etalonnage::fabrique(1.0, 1e308, Unite::Metre, 100.0));
        QVERIFY(Etalonnage::fabrique(10.0, 1e306, Unite::Metre, 100.0));
    }

    void etalonnage_correspond()
    {
        const auto c = *Etalonnage::fabrique(250.0, 10.0, Unite::Millimetre);
        QVERIFY(c.correspond(*Etalonnage::fabrique(250.3, 10.0, Unite::Millimetre)));
        QVERIFY(!c.correspond(*Etalonnage::fabrique(251.0, 10.0, Unite::Millimetre)));
        QVERIFY(!c.correspond(*Etalonnage::fabrique(250.0, 20.0, Unite::Millimetre)));
        QVERIFY(!c.correspond(*Etalonnage::fabrique(250.0, 10.0, Unite::Centimetre)));
        // étalon court : 1 px porté à 1,4 px change l'échelle de 40 % : à revalider
        const auto shortRef = *Etalonnage::fabrique(1.0, 1.0, Unite::Millimetre);
        QVERIFY(!shortRef.correspond(*Etalonnage::fabrique(1.4, 1.0, Unite::Millimetre)));
    }

    void mesures_en_tableau()
    {
        const auto c = Etalonnage::fabrique(250.0, 10.0, Unite::Millimetre);
        QCOMPARE(tableau_mesures({311.0, 50.0}, c, u',', {u"Mesure"_s, u"Longueur (mm)"_s}),
                 u"Mesure\tLongueur (mm)\nM1\t12,4\nM2\t2,00\n"_s);
        QCOMPARE(tableau_mesures({311.0}, std::nullopt, u'.', {u"M"_s, u"L (px)"_s}), u"M\tL (px)\nM1\t311\n"_s);
        QCOMPARE(tableau_mesures({}, std::nullopt, u',', {u"M"_s, u"L"_s}), u"M\tL\n"_s);
    }

    void ecriture_nombres()
    {
        QCOMPARE(texte_nombre(12.437, 3, u','), u"12,4"_s);
        QCOMPARE(texte_nombre(0.04234, 3, u','), u"0,0423"_s);
        QCOMPARE(texte_nombre(0.04234, 3, u'.'), u"0.0423"_s);
        QCOMPARE(texte_nombre(1.25e300, 3, u','), u"1,25e+300"_s);
        QCOMPARE(texte_nombre(2.5e-305, 3, u'.'), u"2.50e-305"_s);
        QCOMPARE(texte_nombre(1234.0, 3, u','), u"1234"_s);
        QCOMPARE(texte_nombre(0.0, 3, u','), u"0"_s);
        QCOMPARE(texte_nombre(-2.5, 3, u','), u"-2,50"_s);
        QCOMPARE(decimales_significatives(6.98, 3), 2);
        QCOMPARE(texte_decimal(3.4891, 2, u','), u"3,49"_s);
        QCOMPARE(texte_decimal(3.4891, 2, u'.'), u"3.49"_s);
        // L'arrondi change la puissance de 10 : toujours 3 chiffres significatifs.
        QCOMPARE(texte_nombre(9.996, 3, u','), u"10,0"_s);
        QCOMPARE(texte_nombre(0.09996, 3, u','), u"0,100"_s);
        QCOMPARE(texte_nombre(-9.996, 3, u','), u"-10,0"_s);
        QCOMPARE(texte_nombre(999.7, 3, u','), u"1000"_s);
        QCOMPARE(texte_nombre(9.94, 3, u','), u"9,94"_s);
        QCOMPARE(decimales_significatives(9.996, 3), 1);
        QCOMPARE(plus_court(0.1), u"0.1"_s);
        QCOMPARE(plus_court(2.0), u"2"_s);
        QCOMPARE(plus_court(0.0000012), u"0.0000012"_s);
    }

    void lecture_nombres()
    {
        QCOMPARE(lit_nombre(u" 2,5 "_s), std::optional(2.5));
        QCOMPARE(lit_nombre(u"10"_s), std::optional(10.0));
        QCOMPARE(lit_nombre(u"+3"_s), std::optional(3.0));
        QCOMPARE(lit_nombre(u"abc"_s), std::nullopt);
        QCOMPARE(lit_nombre(u"inf"_s), std::nullopt);
        QCOMPARE(lit_nombre(u""_s), std::nullopt);
        QCOMPARE(lit_nombre(u"10 mm"_s), std::nullopt);
    }

    // ------------------------------------------------------------ profile
    void profil_horizontal_suit_degrade()
    {
        const QImage img = degrade(100, 50);
        // Le long des centres de pixels de la ligne 10, de la colonne 10 à la colonne 60.
        const Segment seg{{10.5, 10.5}, {60.5, 10.5}};
        const Profil p = echantillonne_profil(img, seg, 1, Canal::Rouge);
        QCOMPARE(p.intensites.size(), size_t(51));
        QVERIFY(proche(p.pasPx, 1.0));
        for (size_t k = 0; k < p.intensites.size(); ++k)
            QVERIFY2(proche(p.intensites[k], double(10 + k), 1e-3), qPrintable(u"k=%1 v=%2"_s.arg(k).arg(p.intensites[k])));
        QCOMPARE(p.echantillonsSatures, 0);
    }

    void profil_oblique_interpole()
    {
        const QImage img = degrade(100, 100);
        const Segment seg{{10.5, 20.5}, {40.5, 60.5}}; // longueur 50
        const Profil p = echantillonne_profil(img, seg, 1, Canal::Vert);
        QCOMPARE(p.intensites.size(), size_t(51));
        QVERIFY(proche(p.intensites[0], 20.0, 1e-3));
        QVERIFY(proche(p.intensites[25], 40.0, 1e-3));
        QVERIFY(proche(p.intensites[50], 60.0, 1e-3));
    }

    void bande_moyenne_perpendiculaire()
    {
        // Rayures horizontales : lignes paires noires, impaires blanches.
        const QImage img = fabrique_image(50, 50, [](int, int y) { return y % 2 == 0 ? QColor(0, 0, 0) : QColor(200, 200, 200); });
        const Segment seg{{5.5, 20.5}, {45.5, 20.5}};
        const Profil thin = echantillonne_profil(img, seg, 1, Canal::Rouge);
        QVERIFY(std::ranges::all_of(thin.intensites, [](double v) { return v < 1e-3; }));
        const Profil wide = echantillonne_profil(img, seg, 10, Canal::Rouge);
        QVERIFY(std::ranges::all_of(wide.intensites, [](double v) { return proche(v, 100.0, 1e-3); }));
    }

    void bande_tronquee_au_bord()
    {
        const QImage img = fabrique_image(12, 3, [](int, int y) { return QColor(50 * y, 0, 0); });
        for (double y : {0.5, 2.5}) {
            const auto p = echantillonne_profil(img, {{0.5, y}, {11.5, y}}, 5, Canal::Rouge);
            QCOMPARE(p.tronques, 12);
            for (double valeur : p.intensites)
                QCOMPARE(valeur, 50.0); // seuls 0, 50 et 100 sont dans l’image
        }
        const auto centre = echantillonne_profil(img, {{0.5, 1.5}, {11.5, 1.5}}, 3, Canal::Rouge);
        QCOMPARE(centre.tronques, 0);
        const auto coin = echantillonne_profil(img, {{0, 0}, {2, 2}}, 2, Canal::Rouge);
        QCOMPARE(coin.intensites.front(), 0.0);
    }

    void luminance_et_saturation()
    {
        // 5 pixels rouges saturés, 5 blancs, 10 gris.
        const QImage img = fabrique_image(20, 1, [](int x, int) {
            return x < 5 ? QColor(255, 0, 0) : x < 10 ? QColor(255, 255, 255) : QColor(100, 100, 100);
        });
        const Segment seg{{0.5, 0.5}, {19.5, 0.5}};
        const Profil lum = echantillonne_profil(img, seg, 1, Canal::GrisPondere);
        QVERIFY(proche(lum.intensites[0], 0.299 * 255.0, 1e-3));
        QVERIFY(proche(lum.intensites[19], 100.0, 1e-3));
        // Le rouge écrêté compte aussi : la luminance réelle y est plus grande que 0,299 × 255.
        QCOMPARE(lum.echantillonsSatures, 10);
        QCOMPARE(echantillonne_profil(img, seg, 1, Canal::Rouge).echantillonsSatures, 10);
        QCOMPARE(echantillonne_profil(img, seg, 1, Canal::Vert).echantillonsSatures, 5); // seulement le blanc
    }

    void saturation_dans_la_bande()
    {
        // Ligne 12 blanche, le reste gris : la coupe passe en ligne 10, la bande de 5 px touche la ligne 12.
        const QImage img =
            fabrique_image(30, 30, [](int, int y) { return y == 12 ? QColor(255, 255, 255) : QColor(100, 100, 100); });
        const Segment seg{{5.5, 10.5}, {25.5, 10.5}};
        QCOMPARE(echantillonne_profil(img, seg, 1, Canal::GrisPondere).echantillonsSatures, 0);
        QCOMPARE(echantillonne_profil(img, seg, 5, Canal::GrisPondere).echantillonsSatures, 21);
    }

    void saturation_des_voisins_interpoles()
    {
        // Colonne 0 rouge écrêtée, colonne 1 noire ; en x = 1,1 le pixel le plus proche est noir, mais
        // la valeur interpolée prend 40 % du rouge : la coupe est signalée saturée.
        const QImage img = fabrique_image(2, 20, [](int x, int) { return x == 0 ? QColor(255, 0, 0) : QColor(0, 0, 0); });
        const Segment seg{{1.1, 0.5}, {1.1, 19.5}};
        const Profil red = echantillonne_profil(img, seg, 1, Canal::Rouge);
        QVERIFY(proche(red.intensites[0], 102.0, 1e-3));
        QCOMPARE(red.echantillonsSatures, 20);
        QCOMPARE(echantillonne_profil(img, Segment{{1.5, 0.5}, {1.5, 19.5}}, 1, Canal::Rouge).echantillonsSatures, 0);
    }

    void valeur_interpolee_sur_la_courbe()
    {
        const Profil p{1.0, {0.0, 10.0, 20.0}, 0};
        QCOMPARE(p.valeur_en(0.0), 0.0);
        QCOMPARE(p.valeur_en(0.6), 12.0);
        QCOMPARE(p.valeur_en(2.0), 20.0);
        QCOMPARE(p.longueur_px(), 2.0);
    }

    void seance_transportable()
    {
        QTemporaryDir dir;
        const QString photo = dir.filePath(u"source.png"_s);
        QVERIFY(degrade(32, 24).save(photo));
        Seance s;
        s.definit_image(charge_image(photo).image);
        s.mesures.push_back({{3, 4}, {18, 15}});
        s.coupe = s.mesures[0];
        s.segmentEtalon = Segment{{0, 5}, {20, 5}};
        s.saisieLongueur = u"1,5"_s;
        s.etalonnage = Etalonnage::fabrique(20, 1, Unite::Millimetre);
        s.fractionsCurseurs = {0.2, 0.8};
        const QString projet = dir.filePath(u"séance.mevpix"_s);
        QVERIFY(enregistre_seance(projet, s).reussie());
        QVERIFY(QFile::remove(photo));
        const auto lecture = charge_image(projet);
        QVERIFY2(lecture.image, qPrintable(lecture.error));
        Seance reprise;
        reprise.image = lecture.image;
        QVERIFY(restaure_seance(reprise, lecture.seance));
        QCOMPARE(reprise.image->rgb, s.image->rgb);
        QCOMPARE(donnees_seance(reprise), donnees_seance(s));
        auto invalide = lecture.seance;
        invalide["version"] = 2;
        QVERIFY(!restaure_seance(reprise, invalide));
        QCOMPARE(donnees_seance(reprise), donnees_seance(s));
        invalide = lecture.seance;
        invalide["saisie"] = 27; // la longueur en attente est conservée sous forme de texte
        QVERIFY(!restaure_seance(reprise, invalide));
        QCOMPARE(donnees_seance(reprise), donnees_seance(s));
    }

    void sauvegarde_verifie_aussi_les_pixels()
    {
        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"classe.mevpix"_s);
        const auto seance_unie = [](Qt::GlobalColor couleur) {
            auto photo = std::make_shared<ImageChargee>();
            photo->name = u"mire.png"_s;
            photo->rgb = QImage(64, 32, QImage::Format_RGB888);
            photo->rgb.fill(couleur);
            Seance seance;
            seance.definit_image(photo);
            return seance;
        };
        const Seance noire = seance_unie(Qt::black);
        const Seance blanche = seance_unie(Qt::white);
        QCOMPARE(donnees_seance(noire), donnees_seance(blanche));
        const auto premiere = enregistre_seance(chemin, noire);
        QVERIFY(premiere.reussie());
        QCOMPARE(charge_image(chemin).empreinteSeance, premiere.empreinte);

        const auto seconde = enregistre_seance(chemin, blanche, premiere.empreinte);
        QVERIFY(seconde.reussie());
        QVERIFY(seconde.empreinte != premiere.empreinte);
        QCOMPARE(enregistre_seance(chemin, noire, premiere.empreinte).etat, EtatSauvegarde::Conflit);
        QCOMPARE(charge_image(chemin).empreinteSeance, seconde.empreinte);

        // Une création tardive ou une suppression du fichier doit aussi être détectée.
        QCOMPARE(enregistre_seance(chemin, noire).etat, EtatSauvegarde::Conflit);
        QVERIFY(QFile::remove(chemin));
        QCOMPARE(enregistre_seance(chemin, blanche, seconde.empreinte).etat, EtatSauvegarde::Conflit);
        QVERIFY(!QFileInfo::exists(chemin));

        // Ne jamais remplacer le dossier homonyme créé entre deux sauvegardes.
        QVERIFY(QDir().mkdir(chemin));
        QVERIFY(!empreinte_sur_disque(chemin).empreinte);
        QCOMPARE(enregistre_seance(chemin, noire).etat, EtatSauvegarde::FichierSpecial);
        QVERIFY(QFileInfo(chemin).isDir());
    }

    void sauvegarde_respecte_le_verrou()
    {
        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"binome.mevpix"_s);
        auto photo = std::make_shared<ImageChargee>();
        photo->rgb = degrade(16, 16);
        Seance seance;
        seance.definit_image(photo);
        const auto avant = enregistre_seance(chemin, seance);
        QVERIFY(avant.reussie());
        QLockFile autreFenetre(chemin + u".lock"_s);
        QVERIFY(autreFenetre.tryLock());
        seance.mesures.append({{1, 2}, {9, 2}});
        QCOMPARE(enregistre_seance(chemin, seance, avant.empreinte).etat, EtatSauvegarde::Occupe);
        QCOMPARE(charge_image(chemin).empreinteSeance, avant.empreinte);
        autreFenetre.unlock();
        QVERIFY(enregistre_seance(chemin, seance, avant.empreinte).reussie());
    }

    void format_csv()
    {
        const Profil p{2.0, {12.0, 34.567}, 0};
        QCOMPARE(en_csv(p, std::nullopt, u','), u"x (px);I\n0;12,00\n2;34,57\n"_s);
        const auto c = Etalonnage::fabrique(100.0, 5.0, Unite::Millimetre);
        QCOMPARE(en_csv(p, c, u','), u"x (mm);I\n0;12,00\n0,1;34,57\n"_s);
        const QString dest = qEnvironmentVariable("MEVSUITE_CSV_TEST");
        if (!dest.isEmpty()) {
            ecrit_fichier(QDir(dest).filePath(u"profil-fr.csv"_s), en_csv(p, c, u',').toUtf8());
            ecrit_fichier(QDir(dest).filePath(u"profil-en.csv"_s), en_csv(p, c, u'.').toUtf8());
        }
        QCOMPARE(en_csv(p, c, u'.'), u"x (mm),I\n0,12.00\n0.1,34.57\n"_s);
        // La précision du tableur doit permettre de retrouver le pas d'une coupe oblique.
        const Profil oblique{0.970142500145332, {10, 20}, 0};
        QCOMPARE(en_csv(oblique, std::nullopt, u','), u"x (px);I\n0;10,00\n0,970142500145332;20,00\n"_s);
        const auto petiteEchelle = Etalonnage::fabrique(1.0, 1e-305, Unite::Metre);
        QVERIFY(petiteEchelle);
        QCOMPARE(en_csv(p, petiteEchelle, u'.'), u"x (m),I\n0,12.00\n2e-305,34.57\n"_s);
    }

    void extension_csv_ajoutee()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(avec_extension_csv(dir.filePath(u"profil"_s)), dir.filePath(u"profil.csv"_s));
        QCOMPARE(avec_extension_csv(dir.filePath(u"profil.csv"_s)), dir.filePath(u"profil.csv"_s));
        QCOMPARE(avec_extension_csv(dir.filePath(u"profil.txt"_s)), dir.filePath(u"profil.txt"_s));
        // un fichier existant garde son extension (le programme demande avant de le remplacer)
        ecrit_fichier(dir.filePath(u"ancien.csv"_s), "x");
        QCOMPARE(avec_extension_csv(dir.filePath(u"ancien"_s)), dir.filePath(u"ancien.csv"_s));
    }

    // ------------------------------------------------------------ settings
    void reglages_aller_retour()
    {
        Reglages s;
        s.lang = Lang::En;
        s.theme = Qt::ColorScheme::Dark;
        s.unite = Unite::Micrometre;
        s.canal = Canal::Rouge;
        s.largeurBande = 12;
        s.uiZoom = 1.2;
        s.lastDir = u"/home/élève/TP = diffraction"_s;
        s.windowSize = QSize(1400, 900);
        s.maximized = true;
        QVERIFY(Reglages::analyse(s.en_texte()) == s);
        QVERIFY(Reglages::analyse(Reglages().en_texte()) == Reglages());
        QVERIFY(s.en_texte().contains(u"ui_zoom = 1.2\n"_s));
        QVERIFY(s.en_texte().contains(u"theme = dark\n"_s));
        QVERIFY(Reglages().en_texte().contains(u"theme = auto\n"_s));
        QCOMPARE(Reglages::analyse(u"theme = light\n"_s).theme, Qt::ColorScheme::Light);
    }

    void reglages_tolerent_erreurs()
    {
        const Reglages s = Reglages::analyse(u"n'importe quoi\nlanguage = klingon\ntheme = rose\nband = 999\nunit = pouce\nui_zoom = 40\n"
                                           u"window_width = 10\nwindow_height = 600\n  channel = blue  \n"_s);
        QCOMPARE(s.lang, std::nullopt);
        QCOMPARE(s.theme, Qt::ColorScheme::Unknown);
        QCOMPARE(s.largeurBande, 50);
        QCOMPARE(s.unite, Unite::Millimetre);
        QCOMPARE(s.uiZoom, 1.0);
        QCOMPARE(s.windowSize, std::nullopt);
        QCOMPARE(s.canal, Canal::Bleu);
    }

    void fichier_admin_l_emporte()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString user = dir.filePath(u"user.ini"_s), admin = dir.filePath(u"admin.ini"_s);
        ecrit_fichier(user, "language = en\nunit = cm\nband = 12\n");
        ecrit_fichier(admin, "# imposé par le lycée\nlanguage = fr\n");

        const Reglages s = Reglages::charge({user, admin});
        QCOMPARE(s.lang, std::optional(Lang::Fr)); // imposé
        QCOMPARE(s.unite, Unite::Centimetre);        // choix de l'utilisateur conservé
        QCOMPARE(s.largeurBande, 12);

        QCOMPARE(Reglages::charge({user, QString()}).lang, std::optional(Lang::En));
        QVERIFY(Reglages::charge({dir.filePath(u"absent.ini"_s), QString()}) == Reglages());

        // Une faute de frappe dans le fichier du lycée préserve les choix personnels.
        ecrit_fichier(user, "language = en\ntheme = dark\nmaximized = true\n");
        ecrit_fichier(admin, "language = zz\ntheme = daark\nmaximized = tru\n");
        const Reglages personnels = Reglages::charge({user, {}});
        QVERIFY(Reglages::charge({user, admin}) == personnels);
        ecrit_fichier(admin, "language = auto\ntheme = auto\nmaximized = false\n");
        QVERIFY(Reglages::charge({user, admin}) == Reglages());
    }

    void reglages_creent_dossier()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"a/b/mevpix.ini"_s);
        Reglages s;
        s.largeurBande = 7;
        QCOMPARE(s.enregistre(path), QString());
        QCOMPARE(Reglages::charge({path, QString()}).largeurBande, 7);
    }

    void reglages_personnels_et_verrou()
    {
        QTemporaryDir dir;
        const FichiersReglages fichiers{dir.filePath(u"classe.ini"_s), dir.filePath(u"impose.ini"_s)};
        ecrit_fichier(fichiers.user, "band = 3\n");
        ecrit_fichier(fichiers.admin, "band = 12\n");
        const Reglages depart = Reglages::charge(fichiers);
        Reglages fenetre = depart;
        fenetre.lang = Lang::En;
        QLockFile verrou(fichiers.user + u".lock"_s);
        QVERIFY(verrou.tryLock());
        QVERIFY(!fenetre.enregistre(fichiers.user, &depart).isEmpty());
        QVERIFY(Reglages::charge(fichiers) == depart);
        verrou.unlock();
        QCOMPARE(fenetre.enregistre(fichiers.user, &depart), QString());
        QCOMPARE(Reglages::charge(fichiers).largeurBande, 12);
        const Reglages personnels = Reglages::charge({fichiers.user, {}});
        QCOMPARE(personnels.largeurBande, 3);
        QCOMPARE(personnels.lang, std::optional(Lang::En));

        // Si les deux fenêtres choisissent une nouvelle épaisseur, le dernier choix enregistré gagne.
        Reglages a = personnels, b = personnels;
        a.largeurBande = 7;
        b.largeurBande = 9;
        QCOMPARE(a.enregistre(fichiers.user, &personnels), QString());
        QCOMPARE(b.enregistre(fichiers.user, &personnels), QString());
        QCOMPARE(Reglages::charge({fichiers.user, {}}).largeurBande, 9);
    }

    // ------------------------------------------------------------ i18n
    void codes_langue()
    {
        QCOMPARE(langue_du_code(u"fr_FR.UTF-8"_s), std::optional(Lang::Fr));
        QCOMPARE(langue_du_code(u"en-GB"_s), std::optional(Lang::En));
        QCOMPARE(langue_du_code(u"FR"_s), std::optional(Lang::Fr));
        QCOMPARE(langue_du_code(u"de_DE"_s), std::nullopt);
        QCOMPARE(langue_du_code(u"C"_s), std::nullopt);
        QCOMPARE(langue_du_code(u"auto"_s), std::nullopt);
        QCOMPARE(langue_du_code(u"ru_RU"_s), std::nullopt);
        for (Lang lang : LANGUES)
            QCOMPARE(langue_du_code(code_langue(lang)), std::optional(lang));
        // Plusieurs langues préférées : la première traduite l'emporte.
        QCOMPARE(premier_traduit({u"ru"_s, u"it"_s, u"en"_s}), std::optional(Lang::En));
        QCOMPARE(premier_traduit({}), std::nullopt);
        QCOMPARE(resout_langue(Lang::Fr), Lang::Fr);
    }

    void conventions_nombres()
    {
        QCOMPARE(textes(Lang::En).decimal, u'.');
        for (Lang lang : LANGUES) {
            if (lang != Lang::En)
                QCOMPARE(textes(lang).decimal, u',');
        }
    }

    // ------------------------------------------------------------ notice
    void notice_ecrite_avec_version()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString helpDir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(help::DIR_NAME);
        QString error;
        const QString page = help::ecrit(Lang::En, &error);
        QCOMPARE(error, QString());
        QCOMPARE(page, QDir(helpDir).filePath(u"en.html"_s));
        QCOMPARE(help::ecrit(Lang::Fr, &error), QDir(helpDir).filePath(u"fr.html"_s));
        for (const QString &name : help::fichiers())
            QVERIFY2(QFile::exists(QDir(helpDir).filePath(name)), qPrintable(name));
        QFile file(page);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString html = QString::fromUtf8(file.readAll());
        QVERIFY(html.contains(QStringLiteral(MEVPIX_VERSION)) && !html.contains(u"{{version}}"_s));
        QVERIFY(html.contains(u"help.css"_s) && html.contains(u"fr.html"_s));
        QVERIFY(html.contains(formats_image().join(u", "_s)));
        QVERIFY(!html.contains(u"{{formats}}"_s));
        QStringList lecteurs;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            lecteurs << QString::fromLatin1(format).toLower();
        lecteurs.removeDuplicates();
        lecteurs.sort();
        QCOMPARE(formats_image(), lecteurs);
    }

#ifdef Q_OS_LINUX
    void notice_cache_indisponible()
    {
        QTemporaryDir dir;
        const QString obstacle = dir.filePath(u"fichier"_s);
        ecrit_fichier(obstacle, "cache indisponible");
        const QByteArray ancien = qgetenv("XDG_CACHE_HOME");
        const bool modeTest = QStandardPaths::isTestModeEnabled();
        const auto restaure = qScopeGuard([&] {
            if (ancien.isNull())
                qunsetenv("XDG_CACHE_HOME");
            else
                qputenv("XDG_CACHE_HOME", ancien);
            QStandardPaths::setTestModeEnabled(modeTest);
        });
        QStandardPaths::setTestModeEnabled(false);
        qputenv("XDG_CACHE_HOME", obstacle.toUtf8());
        QString erreur;
        const QString page = help::ecrit(Lang::Fr, &erreur);
        QVERIFY2(QFile::exists(page), qPrintable(erreur));
        const QFileInfo secours(QFileInfo(page).absolutePath());
        QVERIFY(secours.fileName().startsWith(u"mevpix-help-"_s));
        QVERIFY(!(secours.permissions() & (QFile::ReadGroup | QFile::ReadOther)));
        QCOMPARE(QFileInfo(help::ecrit(Lang::En)).absolutePath(), secours.absoluteFilePath());

        // Une deuxième ouverture garde la date des fichiers identiques et répare les autres,
        // même si leur taille est restée identique.
        QFile feuille(QDir(secours.absoluteFilePath()).filePath(u"help.css"_s));
        QVERIFY(feuille.open(QIODevice::ReadWrite));
        const QByteArray originale = feuille.readAll();
        QVERIFY(!originale.isEmpty());
        const QDateTime ancienne = QDateTime::currentDateTimeUtc().addDays(-2);
        QVERIFY(feuille.setFileTime(ancienne, QFileDevice::FileModificationTime));
        feuille.close();
        const QDateTime dateSurDisque = QFileInfo(feuille).lastModified();
        QCOMPARE(help::ecrit(Lang::Fr), page);
        QCOMPARE(QFileInfo(feuille).lastModified(), dateSurDisque);
        QVERIFY(feuille.open(QIODevice::ReadWrite));
        QCOMPARE(feuille.write("!", 1), 1);
        feuille.close();
        QCOMPARE(help::ecrit(Lang::Fr), page);
        QVERIFY(feuille.open(QIODevice::ReadOnly));
        QCOMPARE(feuille.readAll(), originale);
    }
#endif

    void tp_notice_diametres_rondelle()
    {
        Seance seance;
        seance.definit_image(charge_image(u":/help/rondelle_diametres.png"_s).image);
        QVERIFY(seance.image);
        const QImage &photo = seance.image->rgb;
        QCOMPARE(photo.size(), QSize(1200, 680));

        // Juste au-dessus de la barre de 20 mm, seules ses deux graduations sont claires.
        const auto graduations = plages_claires(photo, 647, 0, 380, 150);
        QCOMPARE(graduations.size(), 2);
        seance.segmentEtalon = Segment{graduations.first().point_en(0.5), graduations.last().point_en(0.5)};
        seance.saisieLongueur = u"20"_s;
        seance.etalonnage = seance.etalonnage_saisi();
        QVERIFY(seance.etalonnage);
        QCOMPARE(seance.etalonnage->unite, Unite::Millimetre);

        // La droite passant par le centre traverse deux parties métalliques séparées par le trou.
        const auto metal = plages_claires(photo, 315, 0, photo.width(), 120);
        QCOMPARE(metal.size(), 2);
        const Segment diametreExterieur{metal.first().a, metal.last().b};
        const Segment diametreInterieur{metal.first().b, metal.last().a};
        const double toleranceMm = seance.etalonnage->en_reel(2); // un pixel de pointage à chaque bord
        QVERIFY(proche(seance.etalonnage->en_reel(diametreExterieur.longueur_px()), 30.0, toleranceMm));
        QVERIFY(proche(seance.etalonnage->en_reel(diametreInterieur.longueur_px()), 12.0, toleranceMm));
    }

    void tp_notice_interfrange()
    {
        Seance seance;
        seance.definit_image(charge_image(u":/help/franges_interference.png"_s).image);
        QVERIFY(seance.image);
        const QImage &photo = seance.image->rgb;
        QCOMPARE(photo.size(), QSize(1830, 831));

        const auto graduations = plages_claires(photo, 797, 20, 210, 150);
        QCOMPARE(graduations.size(), 2);
        seance.segmentEtalon = Segment{graduations.first().point_en(0.5), graduations.last().point_en(0.5)};
        seance.saisieLongueur = u"10"_s;
        seance.etalonnage = seance.etalonnage_saisi();
        QVERIFY(seance.etalonnage);

        // Coupe horizontale sur les cinq pics centraux de la pose courte, avec la bande par défaut.
        seance.coupe = Segment{{406.5, 442.5}, {522.5, 442.5}};
        const Profil *profil = seance.profil();
        QVERIFY(profil);
        const auto &valeurs = profil->intensites;
        std::vector<size_t> maxima;
        for (size_t i = 1; i + 1 < valeurs.size(); ++i) {
            if (valeurs[i] > valeurs[i - 1] && valeurs[i] >= valeurs[i + 1])
                maxima.push_back(i);
        }
        QCOMPARE(maxima.size(), 5u);
        const double distancePx = (maxima.back() - maxima.front()) * profil->pasPx;
        const double ecartMm = seance.etalonnage->en_reel(distancePx);
        // La notice donne les deux lectures au dixième de millimètre.
        QCOMPARE(texte_decimal(ecartMm, 1, u','), u"6,1"_s);
        QCOMPARE(texte_decimal(ecartMm / 4.0, 1, u','), u"1,5"_s);
    }

    /// Chaque page offre les deux images des TP et les captures de son interface.
    void liens_et_images_de_la_notice()
    {
        QString error;
        const QString page = help::ecrit(Lang::Fr, &error);
        QVERIFY2(!page.isEmpty(), qPrintable(error));
        const QDir site = QFileInfo(page).dir();
        const QStringList exemples{u"rondelle_diametres.png"_s, u"franges_interference.png"_s};
        const QRegularExpression attribute(u"(?:src|href)=\"([^\"]+)\""_s);
        for (Lang lang : LANGUES) {
            QFile file(site.filePath(code_langue(lang) + u".html"_s));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString html = QString::fromUtf8(file.readAll());
            QVERIFY(html.contains(u"<img "_s));
            for (const QString &nom : exemples) {
                QVERIFY2(html.contains(u"href=\"%1\""_s.arg(nom)), qPrintable(nom));
                QVERIFY2(QImageReader(site.filePath(nom)).canRead(), qPrintable(nom));
            }
            const QString suffixe = u"-"_s + code_langue(lang) + u".png"_s;
            for (const QString &nom : help::fichiers()) {
                if (nom.endsWith(suffixe)) {
                    QVERIFY2(html.contains(u"src=\"%1\""_s.arg(nom)), qPrintable(nom));
                    QVERIFY2(QImageReader(site.filePath(nom)).canRead(), qPrintable(nom));
                }
            }
            for (const auto &match : attribute.globalMatch(html)) {
                const QString target = match.captured(1);
                if (target.startsWith(u"#"_s))
                    QVERIFY2(html.contains(u"id=\"%1\""_s.arg(target.mid(1))), qPrintable(target));
                else if (!target.startsWith(u"mailto:"_s) && !target.startsWith(u"http"_s))
                    QVERIFY2(QFile::exists(site.filePath(target)), qPrintable(target));
                if (target.endsWith(u".png"_s))
                    QVERIFY2(exemples.contains(target) || target.endsWith(suffixe), qPrintable(target));
            }
        }
    }

    // ------------------------------------------------------------ images et état
    void charge_image_refuse_autres()
    {
        QTemporaryDir dir;
        const QString png = dir.filePath(u"degrade.png"_s);
        QVERIFY(degrade(300, 200).save(png));
        const ResultatChargement ok = charge_image(png);
        QVERIFY(ok.image);
        QCOMPARE(ok.image->name, u"degrade.png"_s);
        QCOMPARE(ok.image->rgb.format(), QImage::Format_RGB888);
        QCOMPARE(ok.image->rgb.size(), QSize(300, 200));
        QCOMPARE(ok.image->rgb.pixelColor(42, 17), QColor(42, 17, 0));
        QVERIFY(ok.image->apercus.empty()); // la vue à pleine taille utilise directement les pixels d'origine
        QCOMPARE(charge_image(dir.path()).typeErreur, ResultatChargement::Erreur::FichierSpecial);

        const QString bmp = dir.filePath(u"mire-renommee.jpg"_s);
        QVERIFY(ok.image->rgb.save(bmp, "BMP"));
        const auto copie = charge_image(bmp);
        QVERIFY(copie.image);
        QCOMPARE(copie.image->rgb, ok.image->rgb);

        const QString text = dir.filePath(u"pas-une-image.png"_s);
        ecrit_fichier(text, "ceci n'est pas une image");
        const ResultatChargement bad = charge_image(text);
        QVERIFY(!bad.image);
        QCOMPARE(bad.typeErreur, ResultatChargement::Erreur::FormatInconnu);
    }

    void jpeg_fin_image_principale_data()
    {
        QTest::addColumn<bool>("progressif");
        QTest::newRow("ordinaire") << false;
        QTest::newRow("progressif") << true;
    }

    void jpeg_fin_image_principale()
    {
        QFETCH(bool, progressif);
        QTemporaryDir dir;
        const QString chemin = dir.filePath(u"photo-renommee.bin"_s);
        const QImage source = degrade(640, 300);
        QBuffer jpeg;
        QVERIFY(jpeg.open(QIODevice::WriteOnly));
        QImageWriter codeur(&jpeg, "JPEG");
        codeur.setProgressiveScanWrite(progressif);
        QVERIFY(codeur.write(source));
        const QByteArray photo = jpeg.data();
        QVERIFY(photo.endsWith(QByteArray::fromHex("ffd9")));

        // Un bloc EXIF contient une deuxième image, ici une miniature de 32 × 15 pixels.
        // Les deux champs de la miniature indiquent sa position (44) et sa longueur.
        QBuffer miniature;
        QVERIFY(miniature.open(QIODevice::WriteOnly));
        QVERIFY(source.scaled(32, 15).save(&miniature, "JPEG"));
        QByteArray exif = QByteArray::fromHex("4578696600004d4d002a0000000800000000000e"
                                              "000202010004000000010000002c0202000400000001");
        QDataStream finExif(&exif, QIODevice::Append);
        finExif << quint32(miniature.size()) << quint32(0);
        exif += miniature.data();
        QByteArray entete = QByteArray::fromHex("ffd8ffe1");
        QDataStream tailleExif(&entete, QIODevice::Append);
        tailleExif << quint16(exif.size() + 2);
        entete += exif;

        ecrit_fichier(chemin, entete + photo.sliced(2) + "texte apres la photo");
        const auto complete = charge_image(chemin);
        QVERIFY(complete.image);
        QCOMPARE(complete.image->rgb.size(), source.size());
        for (const QByteArray &tronquee : {entete, entete + photo.sliced(2, photo.size() / 2),
                                           entete + photo.sliced(2, photo.size() - 3)}) {
            ecrit_fichier(chemin, tronquee);
            const auto lecture = charge_image(chemin);
            QVERIFY(!lecture.image);
            QCOMPARE(lecture.typeErreur, ResultatChargement::Erreur::JpegIncomplet);
        }

        // Placer le FF final au dernier octet d'un bloc de lecture et le D9 dans le suivant.
        QByteArray avecCommentaire = QByteArray::fromHex("ffd8fffe");
        const int remplissage = 65535 - (photo.size() - 2) - 4;
        QVERIFY(remplissage > 0 && remplissage <= 65533);
        QDataStream tailleCommentaire(&avecCommentaire, QIODevice::Append);
        tailleCommentaire << quint16(remplissage + 2);
        avecCommentaire += QByteArray(remplissage, ' ');
        avecCommentaire += photo.sliced(2);
        ecrit_fichier(chemin, avecCommentaire);
        QVERIFY(charge_image(chemin).image);
    }

    void zoom_grande_image_conserve_les_pixels()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"bandes.png"_s);
        const QImage mire = fabrique_image(16384, 8, [](int x, int) { return x % 2 ? Qt::white : Qt::black; });
        QVERIFY(mire.save(path));
        auto lecture = charge_image(path);
        QVERIFY(lecture.image && lecture.image->apercus.front().width() < mire.width());
        VueImage vue;
        const QSize taille(64, 16);
        vue.dispose(taille, mire.size());
        QVERIFY(vue.vers_ecran({0, 0}).x() >= 0);
        QVERIFY(vue.vers_ecran({16384, 0}).x() <= taille.width());
        vue.zoome_autour({32, 8}, 4 / vue.zoom());
        QImage rendu(taille, QImage::Format_RGB32);
        rendu.fill(Qt::red);
        QPainter p(&rendu);
        vue.dessine_image(p, taille, *lecture.image);
        p.end();
        for (int x = 8186; x <= 8198; ++x) {
            const QPoint pixel = vue.vers_ecran({x + 0.5, 4.5}).toPoint();
            QCOMPARE(rendu.pixelColor(pixel), mire.pixelColor(x, 4));
        }
    }

    void profondeur_source_conservee_en_seance()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"gris16.png"_s);
        QImage mire(48, 20, QImage::Format_Grayscale16);
        mire.fill(QColor::fromRgba64(32768, 32768, 32768, 65535));
        QVERIFY(mire.save(path));
        const auto lecture = charge_image(path);
        QVERIFY(lecture.image);
        QCOMPARE(lecture.image->bitsSource, 16);
        QCOMPARE(lecture.image->rgb.format(), QImage::Format_RGB888);
        Seance s;
        s.definit_image(lecture.image);
        const QString projet = dir.filePath(u"gris.mevpix"_s);
        QVERIFY(enregistre_seance(projet, s).reussie());
        const auto reprise = charge_image(projet);
        QVERIFY(reprise.image);
        QCOMPARE(reprise.image->bitsSource, 16);
        QCOMPARE(reprise.image->rgb, lecture.image->rgb);
    }

    void etat_etalonnage_session()
    {
        Seance s;
        s.segmentEtalon = Segment{{0, 0}, {250, 0}};
        s.saisieLongueur = u"10"_s;
        const auto c = s.etalonnage_saisi();
        QVERIFY(c && proche(c->unites_par_pixel(), 0.04));
        QVERIFY(!s.etalonnage_en_attente());
        s.etalonnage = c;
        QVERIFY(!s.etalonnage_en_attente());
        s.segmentEtalon->b.setX(250.1);
        QVERIFY(s.etalonnage_en_attente());
        s.segmentEtalon->b.setX(250);
        s.saisieLongueur = u"20"_s; // longueur changée sans valider
        QVERIFY(s.etalonnage_en_attente());
        s.saisieLongueur = u"10"_s;
        s.uniteSaisie = Unite::Centimetre; // unité changée sans valider
        QVERIFY(s.etalonnage_en_attente());
        QVERIFY(s.a_du_travail());
    }

    void coupe_seule_est_du_travail()
    {
        // une coupe tracée sans étalon ni mesure est aussi du travail à protéger
        Seance s;
        QVERIFY(!s.a_du_travail());
        s.coupe = Segment{{10.5, 40.5}, {110.5, 40.5}};
        QVERIFY(s.a_du_travail());
    }
};

QTEST_GUILESS_MAIN(Tests)
#include "tests.moc"
