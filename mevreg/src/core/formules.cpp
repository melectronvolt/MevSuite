#include "core/formules.h"
#include "core/document.h"

#include <QHash>

#include <algorithm>
#include <deque>

OrdreFormules ordonne_formules(const Document &doc)
{
    const auto &colonnes = doc.quantities;
    const size_t n = colonnes.size();
    OrdreFormules plan;
    plan.analyses.resize(n);
    QHash<QString, size_t> indice;
    QHash<QId, size_t> parId;
    for (size_t i = 0; i < n; ++i) {
        indice.insert(colonnes[i].name, i);
        parId.insert(colonnes[i].id, i);
    }
    plan.mesuresParIncertitude.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (auto u = colonnes[i].uncertainty; u && parId.contains(*u))
            plan.mesuresParIncertitude[parId.value(*u)].push_back(i);
    }

    // Pour vitesse = deriv(x,t), une mesure est disponible d'emblée. Si x est
    // elle-même calculée, vitesse doit attendre que son tour soit passé.
    std::vector<size_t> attentes(n);
    std::vector<std::vector<size_t>> suivantes(n);
    for (size_t i = 0; i < n; ++i) {
        if (!colonnes[i].formula)
            continue;
        auto &analyse = plan.analyses[i] = expr::analyse(*colonnes[i].formula);
        if (!analyse.ok())
            continue;
        for (const QString &nom : analyse.expr->variables()) {
            const auto source = indice.constFind(nom);
            if (source != indice.cend() && colonnes[*source].est_calculee()) {
                ++attentes[i];
                suivantes[*source].push_back(i);
            }
        }
    }

    std::deque<size_t> pretes;
    for (size_t i = 0; i < n; ++i)
        if (colonnes[i].est_calculee() && attentes[i] == 0)
            pretes.push_back(i);
    std::vector<bool> traitee(n);
    const auto longueurConnue = [&](size_t i) { return !colonnes[i].est_calculee() || traitee[i]; };
    while (!pretes.empty()) {
        // Lorsque c'est possible, placer la mesure avant son incertitude.
        // Si la mesure utilise elle-même cette incertitude, on la traitera au
        // passage suivant pour sa longueur, sans bloquer le calcul de ses valeurs.
        auto prochaine = std::find_if(pretes.begin(), pretes.end(), [&](size_t i) {
            const auto &mesures = plan.mesuresParIncertitude[i];
            return std::all_of(mesures.begin(), mesures.end(), longueurConnue);
        });
        if (prochaine == pretes.end())
            prochaine = pretes.begin();
        const size_t colonne = *prochaine;
        pretes.erase(prochaine);
        for (size_t mesure : plan.mesuresParIncertitude[colonne])
            if (!longueurConnue(mesure))
                plan.mesuresDifferees.push_back(mesure);
        traitee[colonne] = true;
        plan.calculables.push_back(colonne);
        for (size_t i : suivantes[colonne])
            if (--attentes[i] == 0)
                pretes.push_back(i);
    }
    for (size_t i = 0; i < n; ++i)
        if (attentes[i] > 0)
            plan.bloquees.push_back(i);

    // Les grandeurs bloquées par une formule circulaire resteront vides.
    std::erase_if(plan.mesuresDifferees, [&](size_t i) { return !traitee[i]; });
    return plan;
}
