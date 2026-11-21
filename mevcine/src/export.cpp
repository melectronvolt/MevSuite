#include "export.h"

#include "calculs.h"
#include "fichier.h"
#include "format.h"
#include "i18n.h"

#include <QSaveFile>
#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace {

/// les noms des colonnes de positions : x, y (ou x1, y1, x2, y2...)
QStringList noms_positions(const Pointage &p)
{
    QStringList noms;
    for (int obj = 1; obj <= p.nombre_objets(); ++obj)
        noms << nom_grandeur(u"x"_s, obj, p.nombre_objets()) << nom_grandeur(u"y"_s, obj, p.nombre_objets());
    return noms;
}

QString liste(const std::vector<double> &valeurs)
{
    QStringList l;
    for (double v : valeurs)
        l << (std::isfinite(v) ? nombre_python(v) : u"np.nan"_s);
    return l.join(u", "_s);
}

} // namespace

QString texte_csv(const Pointage &p)
{
    const QString u = p.unite();
    QStringList en_tete{u"t (s)"_s};
    for (const QString &n : noms_positions(p))
        en_tete << u"%1 (%2)"_s.arg(n, u);
    QString s = en_tete.join(u',') + u"\r\n"_s;
    for (const auto &l : p.lignes_export()) {
        QStringList ligne{texte_significatif(l.t, 6)};
        for (const Position &m : l.positions) {
            if (m)
                ligne << texte_significatif(m->x, 6) << texte_significatif(m->y, 6);
            else
                ligne << QString() << QString();
        }
        s += ligne.join(u',') + u"\r\n"_s;
    }
    return s;
}

QString ecrit_csv(const QString &chemin, const Pointage &p)
{
    QSaveFile f(chemin);
    if (!f.open(QIODevice::WriteOnly) || !ecrit_tout(f, texte_csv(p).toUtf8()) || !f.commit())
        return f.errorString();
    return {};
}

QString texte_python(const Pointage &p, const std::optional<QString> &video)
{
    const auto lignes = p.lignes_export();
    const QStringList noms = noms_positions(p);
    QString s = u"import numpy as np\n\n"_s;
    // Sous Linux, un nom de fichier peut s'étendre sur plusieurs lignes. Le titre entier
    // doit tenir dans le commentaire Python qui précède les mesures.
    static const QRegularExpression sauts_de_ligne(u"\\R+"_s);
    const QString titre = video ? trad("Mesures MeVCine — vidéo : {}", QString(*video).replace(sauts_de_ligne, u" "_s))
                                : trad("Mesures MeVCine");
    s += u"# %1\n"_s.arg(titre);
    QString positions;
    if (p.nombre_objets() > 1) {
        QStringList objets;
        for (int o = 1; o <= p.nombre_objets(); ++o)
            objets << trad("objet {}", o);
        positions = trad("{} : positions de {}", noms.join(u", "_s), objets.join(u", "_s));
    } else {
        positions = trad("x, y : positions");
    }
    const QString unites = p.echelle.est_definie()
        ? trad("t : temps en s ; {} en m", positions)
        : trad("t : temps en s ; {} en pixels (l'échelle n'est pas définie)", positions);
    s += u"# %1\n"_s.arg(unites);
    std::vector<double> t;
    for (const auto &l : lignes)
        t.push_back(l.t);
    s += u"lst_t = np.array([%1])\n"_s.arg(liste(t));
    for (qsizetype j = 0; j < noms.size(); ++j) {
        const size_t obj = j / 2;
        std::vector<double> valeurs;
        for (const auto &l : lignes)
            valeurs.push_back(l.positions[obj] ? (j % 2 == 0 ? l.positions[obj]->x : l.positions[obj]->y) : NAN);
        s += u"lst_%1 = np.array([%2])\n"_s.arg(noms[j], liste(valeurs));
    }
    return s;
}
