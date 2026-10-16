#include "export.h"

#include "calculs.h"
#include "format.h"
#include "i18n.h"

#include <QSaveFile>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace {

/// les noms des colonnes de positions : x, y (ou x1, y1, x2, y2...)
QStringList noms_positions(const Pointage &p)
{
    QStringList noms;
    for (int obj = 1; obj <= p.nb_obj(); ++obj)
        noms << nom_grandeur(u"x"_s, obj, p.nb_obj()) << nom_grandeur(u"y"_s, obj, p.nb_obj());
    return noms;
}

QString liste(const std::vector<double> &valeurs)
{
    QStringList l;
    for (double v : valeurs)
        l << fmt_py(v);
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
    for (const auto &l : p.lignes_completes()) {
        QStringList ligne{fmt_g(l.t, 6)};
        for (const Vecteur &m : l.positions)
            ligne << fmt_g(m.x, 6) << fmt_g(m.y, 6);
        s += ligne.join(u',') + u"\r\n"_s;
    }
    return s;
}

QString ecrit_csv(const QString &chemin, const Pointage &p)
{
    QSaveFile f(chemin);
    if (!f.open(QIODevice::WriteOnly) || f.write(texte_csv(p).toUtf8()) < 0 || !f.commit())
        return f.errorString();
    return {};
}

QString texte_python(const Pointage &p, const std::optional<QString> &video)
{
    const auto lignes = p.lignes_completes();
    const QStringList noms = noms_positions(p);
    QString s = u"import numpy as np\n\n"_s;
    const QString titre = video ? trad("Mesures MeVCine — vidéo : {}", *video) : trad("Mesures MeVCine");
    s += u"# %1\n"_s.arg(titre);
    QString positions;
    if (p.nb_obj() > 1) {
        QStringList objets;
        for (int o = 1; o <= p.nb_obj(); ++o)
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
            valeurs.push_back(j % 2 == 0 ? l.positions[obj].x : l.positions[obj].y);
        s += u"lst_%1 = np.array([%2])\n"_s.arg(noms[j], liste(valeurs));
    }
    return s;
}
