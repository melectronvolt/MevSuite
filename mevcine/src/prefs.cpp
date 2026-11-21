#include "prefs.h"

#include "fichier.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace prefs {

namespace {

/// les réglages propres au poste, qui ne vont pas dans les fichiers de projet
const QStringList REGLAGES_DU_POSTE = {u"langue"_s,         u"niveaudbg"_s,         u"theme"_s,
                                       u"taille_fenetre"_s, u"fenetre_maximisee"_s, u"agrandissement"_s,
                                       u"mode"_s};

QString ecrit(const QString &chemin, const QString &texte)
{
    QSaveFile f(chemin);
    if (!f.open(QIODevice::WriteOnly) || !ecrit_tout(f, texte.toUtf8()) || !f.commit())
        return f.errorString();
    return {};
}

} // namespace

Config::Config()
{
    const std::pair<QString, QString> defauts[] = {
        {u"version"_s, u"mevcine "_s + VERSION},
        {u"langue"_s, QString()},             // langue de l'interface (vide : celle du système)
        {u"proximite"_s, u"False"_s},         // vecteurs tracés près du curseur
        {u"lastvideo"_s, QString()},          // vidéo consultée précédemment
        {u"videodir"_s, QString()},           // répertoire des vidéos (vide : celui de l'utilisateur)
        {u"niveaudbg"_s, u"0"_s},             // niveau des messages de débogage
        {u"sens_x"_s, u"1"_s},                // sens des abscisses
        {u"sens_y"_s, u"1"_s},                // sens des ordonnées
        {u"taille_image"_s, u"(320,240)"_s},  // dimension de l'image de vidéo
        {u"rotation"_s, u"0"_s},              // rotation de l'image
        {u"origine"_s, u"(320,240)"_s},       // origine pour les pointages
        {u"index_depart"_s, u"1"_s},          // première image pointée
        {u"etalon_m"_s, u"1.00"_s},           // longueur de l'étalon en mètre
        {u"etalon_px"_s, u"100"_s},           // longueur de l'étalon en pixel
        {u"etalon_org"_s, u"None"_s},         // point 1 de l'étalon sur l'image
        {u"etalon_ext"_s, u"None"_s},         // point 2 de l'étalon sur l'image
        {u"deltat"_s, u"0.040"_s},            // intervalle de temps entre deux images
        {u"nb_obj"_s, u"1"_s},                // nombre d'objets suivis
    };
    for (const auto &[k, v] : defauts)
        definit(k, v);
}

std::optional<QString> Config::valeur_texte(const QString &cle) const
{
    const QString c = cle.toLower();
    for (const auto &[k, v] : m_entrees) {
        if (k == c)
            return v;
    }
    return std::nullopt;
}

void Config::definit(const QString &cle, const QString &valeur)
{
    const QString c = cle.toLower();
    for (auto &[k, v] : m_entrees) {
        if (k == c) {
            v = valeur;
            return;
        }
    }
    m_entrees.emplace_back(c, valeur);
}

void Config::retire(const QString &cle)
{
    const QString c = cle.toLower();
    std::erase_if(m_entrees, [&](const auto &e) { return e.first == c; });
}

std::optional<qint64> Config::valeur_entiere(const QString &cle) const
{
    const auto v = valeur_texte(cle);
    if (!v)
        return std::nullopt;
    bool ok = false;
    const qint64 n = v->trimmed().toLongLong(&ok);
    return ok ? std::optional(n) : std::nullopt;
}

std::optional<double> Config::valeur_reelle(const QString &cle) const
{
    const auto v = valeur_texte(cle);
    if (!v)
        return std::nullopt;
    bool ok = false;
    const double x = v->trimmed().replace(u',', u'.').toDouble(&ok);
    return ok ? std::optional(x) : std::nullopt;
}

std::optional<Vecteur> Config::valeur_vecteur(const QString &cle) const
{
    const auto valeur = valeur_texte(cle);
    if (!valeur)
        return std::nullopt;
    const QString v = valeur->trimmed();
    if (!v.startsWith(u'(') || !v.endsWith(u')'))
        return std::nullopt;
    const QString interieur = v.mid(1, v.size() - 2);
    const qsizetype virgule = interieur.indexOf(u',');
    if (virgule < 0)
        return std::nullopt;
    bool okx = false, oky = false;
    const double x = interieur.left(virgule).trimmed().toDouble(&okx);
    const double y = interieur.mid(virgule + 1).trimmed().toDouble(&oky);
    if (!okx || !oky || !std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;
    return Vecteur(x, y);
}

bool Config::est_mevcine() const
{
    const auto v = valeur_texte(u"version"_s);
    return v && (v->trimmed().startsWith(u"mevcine"_s) || v->trimmed().startsWith(u"cinemev"_s));
}

bool Config::version_ok() const
{
    return est_mevcine();
}

std::pair<unsigned, unsigned> Config::version_du_fichier() const
{
    QString v = valeur_texte(u"version"_s).value_or(QString()).trimmed();
    for (const QString &prefixe : {u"mevcine "_s, u"cinemev "_s}) {
        if (v.startsWith(prefixe)) {
            v = v.mid(prefixe.size());
            break;
        }
    }
    const qsizetype point = v.indexOf(u'.');
    if (point < 0)
        return {0, 0};
    bool ok = false;
    const unsigned majeur = v.left(point).trimmed().toUInt(&ok);
    QString chiffres;
    for (QChar c : QStringView(v).mid(point + 1)) {
        if (c < u'0' || c > u'9')
            break;
        chiffres += c;
    }
    bool ok2 = false;
    const unsigned mineur = chiffres.toUInt(&ok2);
    if (!ok || !ok2)
        return {0, 0};
    return {majeur, mineur};
}

void Config::lit_ini(const QString &texte)
{
    for (const QString &ligne : lignes(texte)) {
        const QString l = ligne.trimmed();
        if (l.isEmpty() || l.startsWith(u'#') || l.startsWith(u';') || l.startsWith(u'['))
            continue;
        const qsizetype a = l.indexOf(u'='), b = l.indexOf(u':');
        const qsizetype sep = a < 0 ? b : b < 0 ? a : std::min(a, b);
        if (sep < 0)
            continue;
        QString cle = l.left(sep).trimmed().toLower();
        QString valeur = l.mid(sep + 1).trimmed();
        if (cle == u"lastvideo_utf8" || cle == u"videodir_utf8") {
            const QByteArray codes = valeur.toLatin1();
            const QByteArray octets = QByteArray::fromHex(codes);
            if (octets.toHex() != codes.toLower())
                continue;
            cle.chop(5); // retrouve « lastvideo » ou « videodir »
            valeur = QString::fromUtf8(octets);
        }
        definit(cle, valeur);
    }
}

QString Config::ecrit_ini() const
{
    QString s = u"[DEFAULT]\n"_s;
    for (const auto &[cle, valeur] : m_entrees) {
        // Un chemin peut contenir un retour à la ligne sous Linux. Dans ce cas seulement
        // (ou avec des espaces aux extrémités), écrire ses octets en hexadécimal sur une ligne.
        if ((cle == u"lastvideo" || cle == u"videodir") &&
            (valeur.contains(u'\n') || valeur.contains(u'\r') || valeur != valeur.trimmed()))
            s += u"%1_utf8 = %2\n"_s.arg(cle, QString::fromLatin1(valeur.toUtf8().toHex()));
        else
            s += u"%1 = %2\n"_s.arg(cle, valeur);
    }
    s += u'\n';
    return s;
}

std::optional<Langue> langue(const Config &c)
{
    const auto v = c.valeur_texte(u"langue"_s);
    return v ? i18n::depuis_code(*v) : std::nullopt;
}

namespace {

/// Ancien dossier de MeVCine (~/.local/share/mevcine) : ses fichiers passent dans celui de la
/// suite, sous leurs nouveaux noms, puis il est retiré s'il est vide.
void migre(const QString &dossier)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString ancien = base.isEmpty() ? QString() : QDir(base).filePath(u"mevcine"_s);
    if (ancien.isEmpty() || !QFileInfo(ancien).isDir() || QDir(ancien) == QDir(dossier))
        return;
    QDir().mkpath(dossier);
    const std::pair<QString, QString> noms[] = {{u"mevcine.conf"_s, u"mevcine.ini"_s},
                                                {u"secours.mevcine"_s, u"mevcine-secours.mevcine"_s},
                                                {u"mevcine-erreurs.log"_s, u"mevcine.log"_s}};
    for (const auto &[de, vers] : noms) {
        const QString a = QDir(ancien).filePath(de), b = QDir(dossier).filePath(vers);
        if (QFileInfo(a).isFile() && !QFileInfo(b).exists())
            QFile::rename(a, b);
    }
    QDir().rmdir(ancien);
}

} // namespace

QString dossier_conf()
{
    const QString d = qEnvironmentVariable("MEVCINE_CONF");
    if (!d.isEmpty())
        return d;
    // dossier commun aux logiciels de la suite : %APPDATA%\MeVSuite sous Windows, ~/.config/MeVSuite
    // ailleurs ; un fichier de réglages par logiciel, sans autre dossier
    QString base;
#ifdef Q_OS_WIN
    base = qEnvironmentVariable("APPDATA");
#else
    base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() && !qEnvironmentVariable("HOME").isEmpty())
        base = QDir(qEnvironmentVariable("HOME")).filePath(u".config"_s);
#endif
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString dossier = QDir(base).filePath(u"MeVSuite"_s);
    static bool migre_fait = false;
    if (!migre_fait) {
        migre_fait = true;
        migre(dossier);
    }
    return dossier;
}

QString fichier_conf()
{
    return QDir(dossier_conf()).filePath(u"mevcine.ini"_s);
}

QString fichier_journal_erreurs()
{
    return QDir(dossier_conf()).filePath(u"mevcine.log"_s);
}

QString horodatage(qint64 secondes)
{
    return QDateTime::fromSecsSinceEpoch(secondes, QTimeZone::UTC).toString(u"yyyy-MM-dd hh:mm:ss 'UTC'"_s);
}

std::expected<Config, QString> charge()
{
    Config c;
    QFile f(fichier_conf());
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly))
            return std::unexpected(f.errorString());
        c.lit_ini(QString::fromUtf8(f.readAll()));
    }
    return c;
}

QString sauve(const Config &c)
{
    if (!QDir().mkpath(dossier_conf()))
        return QDir::toNativeSeparators(dossier_conf());
    return ecrit(fichier_conf(), c.ecrit_ini());
}

bool est_projet(const QString &chemin)
{
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray debut = f.read(23);
    return debut.startsWith("# version = mevcine") || debut.startsWith("# version = cinemev");
}

QString texte_projet(const Config &config, const QString &message, const QString &donnees)
{
    Config c = config;
    for (const QString &k : REGLAGES_DU_POSTE)
        c.retire(k);
    QString texte;
    const QStringList l = lignes(c.ecrit_ini());
    for (qsizetype i = 1; i < l.size(); ++i)
        texte += u"# "_s + l[i] + u'\n';
    texte += u"# %1\n"_s.arg(message);
    texte += donnees;
    return texte;
}

QString ecrit_projet(const QString &chemin, const QString &texte)
{
    return ecrit(chemin, texte);
}

QByteArray empreinte_projet(const QByteArray &contenu)
{
    return QCryptographicHash::hash(contenu, QCryptographicHash::Sha256);
}

bool projet_identique(const QString &chemin, const QByteArray &empreinte)
{
    if (chemin.isEmpty() || empreinte.isEmpty())
        return false;
    QFile fichier(chemin);
    QCryptographicHash calcul(QCryptographicHash::Sha256);
    return fichier.open(QIODevice::ReadOnly) && calcul.addData(&fichier) && calcul.result() == empreinte;
}

namespace {

/// Les anciens projets emploient des espaces. Avec des tabulations, deux champs vides
/// au milieu d’une ligne réservent la place d’un objet dont la position manque.
QStringList champs_mesure(const QString &ligne)
{
    if (ligne.contains(u'\t'))
        return ligne.split(u'\t', Qt::KeepEmptyParts);
    return ligne.simplified().split(u' ', Qt::SkipEmptyParts);
}

bool est_entete_mesures(const QStringList &champs)
{
    if (champs.size() < 3 || champs.size() % 2 == 0 || champs.front().trimmed() != u"t")
        return false;
    for (qsizetype i = 1; i < champs.size(); i += 2) {
        const QString suffixe = QString::number((i + 1) / 2);
        const QString x = champs[i].trimmed(), y = champs[i + 1].trimmed();
        if (champs.size() == 3 && x == u"x" && y == u"y")
            return true;
        if (x != u'x' + suffixe || y != u'y' + suffixe)
            return false;
    }
    return true;
}

std::expected<std::vector<double>, QString> lit_mesure(const QStringList &champs, qsizetype colonnes, qsizetype numero)
{
    if (champs.size() > colonnes || champs.size() % 2 == 0)
        return std::unexpected(trad("Ligne {}. Chaque objet doit avoir deux colonnes, x et y.", numero));
    std::vector<double> valeurs;
    valeurs.reserve(champs.size());
    for (const QString &champ : champs) {
        const QString texte = champ.trimmed();
        if (!valeurs.empty() && (texte.isEmpty() || texte.compare(u"nan", Qt::CaseInsensitive) == 0)) {
            valeurs.push_back(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        bool ok = false;
        const double valeur = QString(texte).replace(u',', u'.').toDouble(&ok);
        if (!ok || !std::isfinite(valeur))
            return std::unexpected(trad("Ligne {}. La colonne {} doit contenir un nombre.", numero, valeurs.size() + 1));
        valeurs.push_back(valeur);
    }
    for (size_t x = 1; x < valeurs.size(); x += 2) {
        if (std::isnan(valeurs[x]) != std::isnan(valeurs[x + 1]))
            return std::unexpected(trad("Ligne {}. Il manque une coordonnée de l'objet {}.", numero, (x + 1) / 2));
    }
    return valeurs;
}

} // namespace

std::expected<Projet, QString> lit_projet(const QString &chemin)
{
    QFile fichier(chemin);
    if (!fichier.open(QIODevice::ReadOnly))
        return std::unexpected(fichier.errorString());
    const QByteArray octets = fichier.readAll();
    if (fichier.error() != QFileDevice::NoError)
        return std::unexpected(fichier.errorString());
    Projet projet;
    projet.empreinte_lue = empreinte_projet(octets);
    // Un fichier doit fournir sa propre version avant de remplacer la séance à l’écran.
    projet.config.definit(u"version"_s, {});
    QString lignes_config;
    qsizetype numero = 0;
    qsizetype colonnes = 0;
    for (const QString &ligne : lignes(QString::fromUtf8(octets))) {
        ++numero;
        if (ligne.startsWith(u'#')) {
            if (ligne.startsWith(u"# ") && ligne.contains(u" = "))
                lignes_config += ligne.mid(2) + u'\n';
            continue;
        }
        if (ligne.trimmed().isEmpty())
            continue;
        const QStringList champs = champs_mesure(ligne);
        if (colonnes == 0) {
            if (!est_entete_mesures(champs))
                return std::unexpected(trad("Ligne {}. Le tableau doit commencer par t et les colonnes x et y de chaque objet.", numero));
            colonnes = champs.size();
        } else {
            auto mesure = lit_mesure(champs, colonnes, numero);
            if (!mesure)
                return std::unexpected(mesure.error());
            projet.donnees.push_back(std::move(*mesure));
        }
    }
    if (colonnes == 0)
        return std::unexpected(trad("Le tableau des mesures manque dans le projet."));
    projet.config.lit_ini(lignes_config);
    // Certaines séances anciennes décrivent les objets uniquement dans l’en-tête du tableau.
    const qsizetype objets_du_tableau = (colonnes - 1) / 2;
    if (projet.config.valeur_entiere(u"nb_obj"_s).value_or(1) < objets_du_tableau)
        projet.config.definit(u"nb_obj"_s, QString::number(objets_du_tableau));
    return projet;
}

QStringList lignes(const QString &texte)
{
    QStringList l = texte.split(u'\n');
    if (texte.endsWith(u'\n'))
        l.removeLast();
    for (QString &ligne : l) {
        if (ligne.endsWith(u'\r'))
            ligne.chop(1);
    }
    if (texte.isEmpty())
        l.clear();
    return l;
}

} // namespace prefs
