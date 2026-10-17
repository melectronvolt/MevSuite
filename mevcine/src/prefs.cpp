#include "prefs.h"

#include "fichier.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace prefs {

namespace {

/// les réglages propres au poste, qui ne vont pas dans les fichiers de projet
const QStringList REGLAGES_DU_POSTE = {u"langue"_s,         u"niveaudbg"_s,         u"theme"_s,
                                       u"taille_fenetre"_s, u"fenetre_maximisee"_s, u"agrandissement"_s};

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
        set(k, v);
}

std::optional<QString> Config::get(const QString &cle) const
{
    const QString c = cle.toLower();
    for (const auto &[k, v] : m_entrees) {
        if (k == c)
            return v;
    }
    return std::nullopt;
}

void Config::set(const QString &cle, const QString &valeur)
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

std::optional<qint64> Config::get_int(const QString &cle) const
{
    const auto v = get(cle);
    if (!v)
        return std::nullopt;
    bool ok = false;
    const qint64 n = v->trimmed().toLongLong(&ok);
    return ok ? std::optional(n) : std::nullopt;
}

std::optional<double> Config::get_float(const QString &cle) const
{
    const auto v = get(cle);
    if (!v)
        return std::nullopt;
    bool ok = false;
    const double x = v->trimmed().replace(u',', u'.').toDouble(&ok);
    return ok ? std::optional(x) : std::nullopt;
}

std::optional<Vecteur> Config::get_vecteur(const QString &cle) const
{
    const auto valeur = get(cle);
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
    if (!okx || !oky)
        return std::nullopt;
    return Vecteur(x, y);
}

bool Config::est_mevcine() const
{
    const auto v = get(u"version"_s);
    return v && (v->trimmed().startsWith(u"mevcine"_s) || v->trimmed().startsWith(u"cinemev"_s));
}

bool Config::version_ok() const
{
    return est_mevcine() || version() >= VERSION_MIN;
}

std::pair<unsigned, unsigned> Config::version() const
{
    QString v = get(u"version"_s).value_or(QString()).trimmed();
    for (const QString &prefixe : {u"pymecavideo "_s, u"mevcine "_s, u"cinemev "_s}) {
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
        set(l.left(sep).trimmed(), l.mid(sep + 1).trimmed());
    }
}

QString Config::ecrit_ini() const
{
    QString s = u"[DEFAULT]\n"_s;
    for (const auto &[k, v] : m_entrees)
        s += u"%1 = %2\n"_s.arg(k, v);
    s += u'\n';
    return s;
}

std::optional<Langue> langue(const Config &c)
{
    const auto v = c.get(u"langue"_s);
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

QString fichier_secours()
{
    return QDir(dossier_conf()).filePath(u"mevcine-secours.mevcine"_s);
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
    return debut.startsWith("# version = mevcine") || debut.startsWith("# version = cinemev")
           || debut.startsWith("# version = pymecavideo");
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

QString ecrit_secours(const QString &texte)
{
    if (!QDir().mkpath(dossier_conf()))
        return QDir::toNativeSeparators(dossier_conf());
    // QSaveFile écrit à côté, puis met le fichier à sa place d'un coup
    return ecrit(fichier_secours(), texte);
}

std::expected<Projet, QString> lit_projet(const QString &chemin)
{
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(f.errorString());
    const QString texte = QString::fromUtf8(f.readAll());
    Projet projet;
    QString lignes_config;
    bool en_tete_vu = false;
    for (const QString &ligne : lignes(texte)) {
        if (ligne.startsWith(u"# "_s)) {
            const QString l = ligne.mid(2);
            if (l.contains(u" = "_s))
                lignes_config += l + u'\n';
        } else if (!ligne.startsWith(u'#') && !ligne.trimmed().isEmpty()) {
            // on ne garde pas la première ligne, « t x1 y1 ... »
            if (!en_tete_vu) {
                en_tete_vu = true;
                continue;
            }
            std::vector<double> valeurs;
            for (const QString &v : ligne.simplified().split(u' ', Qt::SkipEmptyParts)) {
                bool ok = false;
                const double x = QString(v).replace(u',', u'.').toDouble(&ok);
                valeurs.push_back(ok ? x : std::nan(""));
            }
            projet.donnees.push_back(std::move(valeurs));
        }
    }
    projet.config.lit_ini(lignes_config);
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
