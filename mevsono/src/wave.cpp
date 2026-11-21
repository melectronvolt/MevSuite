#include "wave.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

/// Taille des plus petits blocs précalculés.
constexpr size_t TAILLE_BLOC = 64;

using Bornes = std::pair<float, float>;

constexpr Bornes BORNES_VIDES = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};

Bornes fusionne(Bornes a, Bornes b)
{
    return {std::min(a.first, b.first), std::max(a.second, b.second)};
}

Bornes parcourt(std::span<const float> echantillons)
{
    Bornes r = BORNES_VIDES;
    for (float x : echantillons)
        r = {std::min(r.first, x), std::max(r.second, x)};
    return r;
}

/// Indice d'échantillon d'un instant, borné entre 0 et `taille`.
size_t indice_borne(double x, size_t taille)
{
    if (!(x > 0.0))
        return 0;
    return x >= double(taille) ? taille : static_cast<size_t>(x);
}

} // namespace

Envelope::Envelope(std::span<const float> echantillons)
{
    std::vector<Bornes> niveau;
    niveau.reserve((echantillons.size() + TAILLE_BLOC - 1) / TAILLE_BLOC);
    for (size_t i = 0; i < echantillons.size(); i += TAILLE_BLOC)
        niveau.push_back(parcourt(echantillons.subspan(i, std::min(TAILLE_BLOC, echantillons.size() - i))));
    while (niveau.size() > 1) {
        std::vector<Bornes> parents;
        parents.reserve((niveau.size() + 1) / 2);
        for (size_t i = 0; i < niveau.size(); i += 2)
            parents.push_back(i + 1 < niveau.size() ? fusionne(niveau[i], niveau[i + 1]) : niveau[i]);
        m_niveaux.push_back(std::move(niveau));
        niveau = std::move(parents);
    }
    m_niveaux.push_back(std::move(niveau));
}

std::pair<float, float> Envelope::min_max(std::span<const float> echantillons, size_t debut, size_t fin) const
{
    size_t gauche = (debut + TAILLE_BLOC - 1) / TAILLE_BLOC;
    size_t droite = fin / TAILLE_BLOC;
    if (gauche >= droite)
        return parcourt(echantillons.subspan(debut, fin - debut));

    // Seuls les deux bouts qui tombent entre deux blocs de 64 sont relus dans le son.
    Bornes bornes = parcourt(echantillons.subspan(debut, gauche * TAILLE_BLOC - debut));
    bornes = fusionne(bornes, parcourt(echantillons.subspan(droite * TAILLE_BLOC, fin - droite * TAILLE_BLOC)));
    // Chaque paire de blocs voisins possède un résumé à l'étage suivant. On prend les
    // blocs isolés aux bords, puis on monte d'un étage pour traiter les paires restantes.
    for (size_t niveau = 0; gauche < droite; ++niveau) {
        const auto &blocs = m_niveaux[niveau];
        if (gauche % 2 == 1)
            bornes = fusionne(bornes, blocs[gauche++]);
        if (droite % 2 == 1)
            bornes = fusionne(bornes, blocs[--droite]);
        gauche /= 2;
        droite /= 2;
    }
    return bornes;
}

std::vector<QPointF> Envelope::points(std::span<const float> echantillons, double cadence, double tempsDebut, double tempsFin,
                                      size_t colonnes) const
{
    if (!(cadence > 0 && tempsFin > tempsDebut))
        return {};
    const size_t taille = echantillons.size();
    const size_t debut = indice_borne(std::floor(tempsDebut * cadence), taille);
    const size_t fin = std::min(indice_borne(std::ceil(tempsFin * cadence), taille) + 1, taille);
    if (fin <= debut + 1)
        return {};
    const size_t nombre = fin - debut;
    colonnes = std::max<size_t>(colonnes, 1);
    std::vector<QPointF> points;
    if (colonnes >= (nombre + 1) / 2) {
        points.reserve(nombre);
        for (size_t i = debut; i < fin; ++i)
            points.emplace_back(double(i) / cadence, echantillons[i]);
        return points;
    }
    // Les échantillons qui encadrent les bords servent à interpoler la courbe au bord.
    // Les inclure dans le min/max d'une colonne ramènerait un pic hors champ dans la vue.
    const bool coupeDebut = double(debut) < tempsDebut * cadence;
    const bool coupeFin = double(fin - 1) > tempsFin * cadence;
    const size_t debutInterieur = debut + size_t(coupeDebut);
    const size_t finInterieure = fin - size_t(coupeFin);
    const double parColonne = double(finInterieure - debutInterieur) / double(colonnes);
    points.reserve(2 * colonnes + 2);
    const auto au_bord = [&](size_t i, double t) {
        points.emplace_back(t, std::lerp(double(echantillons[i]), double(echantillons[i + 1]), t * cadence - double(i)));
    };
    if (coupeDebut)
        au_bord(debut, tempsDebut);
    size_t debutColonne = debutInterieur;
    for (size_t colonne = 1; colonne <= colonnes; ++colonne) {
        // La dernière colonne inclut le dernier échantillon, même si la division
        // en colonnes a arrondi sa position (par exemple 30 échantillons sur 11 colonnes).
        const size_t finColonne = colonne == colonnes ? finInterieure :
            debutInterieur + size_t(double(colonne) * parColonne);
        const auto [minimum, maximum] = min_max(echantillons, debutColonne, finColonne);
        const double tempsCentre = (double(debutColonne) + double(finColonne - 1)) / (2.0 * cadence);
        points.emplace_back(tempsCentre, minimum);
        points.emplace_back(tempsCentre, maximum);
        debutColonne = finColonne;
    }
    if (coupeFin)
        au_bord(fin - 2, tempsFin);
    return points;
}
