#include "core/stats.h"
#include "core/numeric.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace stats {

namespace {

struct Dispersion {
    size_t effectif = 0;
    double minimum = 0, maximum = 0;
    long double moyenne = 0, sommeCarres = 0;

    double ecart_type(size_t degresRetires) const
    {
        if (effectif <= degresRetires)
            return NAN;
        return static_cast<double>(std::sqrt(sommeCarres / (effectif - degresRetires)));
    }
};

Dispersion mesure_dispersion(const std::vector<double> &mesures)
{
    Dispersion resultat;
    long double somme = 0, correction = 0;
    for (double mesure : mesures) {
        if (!std::isfinite(mesure))
            continue;
        if (resultat.effectif == 0)
            resultat.minimum = resultat.maximum = mesure;
        ++resultat.effectif;
        resultat.minimum = std::min(resultat.minimum, mesure);
        resultat.maximum = std::max(resultat.maximum, mesure);
        // Somme compensée de Neumaier. Une petite mesure doit encore compter
        // lorsque deux grandes lectures de signes opposés se compensent.
        const long double nouvelleSomme = somme + mesure;
        if (std::abs(somme) >= std::abs(mesure))
            correction += (somme - nouvelleSomme) + mesure;
        else
            correction += (mesure - nouvelleSomme) + somme;
        somme = nouvelleSomme;
    }
    if (resultat.effectif == 0)
        return resultat;

    resultat.moyenne = (somme + correction) / resultat.effectif;
    long double sommeEcarts = 0;
    for (double mesure : mesures) {
        if (std::isfinite(mesure)) {
            const long double ecart = mesure - resultat.moyenne;
            sommeEcarts += ecart;
            resultat.sommeCarres += ecart * ecart;
        }
    }
    // La moyenne a elle-même pu être arrondie. Recentrer les écarts évite
    // d'attribuer cet arrondi à la dispersion expérimentale.
    resultat.sommeCarres = std::max(0.0L, resultat.sommeCarres - sommeEcarts * (sommeEcarts / resultat.effectif));
    return resultat;
}

} // namespace

double ecart_type(const std::vector<double> &mesures, size_t degresRetires)
{
    return mesure_dispersion(mesures).ecart_type(degresRetires);
}

std::optional<Resume> resume(const std::vector<double> &mesures)
{
    const Dispersion serie = mesure_dispersion(mesures);
    if (serie.effectif == 0)
        return std::nullopt;

    Resume resultat;
    resultat.effectif = serie.effectif;
    resultat.minimum = serie.minimum;
    resultat.maximum = serie.maximum;
    resultat.moyenne = static_cast<double>(serie.moyenne);
    resultat.ecartTypePopulation = serie.ecart_type(0);
    resultat.ecartTypeEchantillon = serie.ecart_type(1);
    resultat.uA = resultat.ecartTypeEchantillon / std::sqrt(static_cast<double>(serie.effectif));
    return resultat;
}

size_t classes_par_defaut(size_t effectif)
{
    if (effectif < 2)
        return 1;
    const auto classes = static_cast<size_t>(std::ceil(std::log2(static_cast<double>(effectif)) + 1.0));
    return std::clamp<size_t>(classes, 3, 30);
}

std::optional<Histogramme> histogramme(const std::vector<double> &mesures, size_t nombreClasses)
{
    const auto plage = numeric::plage_finie(mesures);
    if (!plage)
        return std::nullopt;
    auto [minimum, maximum] = *plage;
    nombreClasses = std::max<size_t>(nombreClasses, 1);

    // Pour une série constante ou presque, deux bornes arrondies au même
    // double donneraient une barre de largeur nulle sur le graphe.
    const long double amplitude = std::max(std::abs(minimum), std::abs(maximum));
    const long double pasMinimal = std::max(amplitude * std::numeric_limits<double>::epsilon(),
                                            static_cast<long double>(std::numeric_limits<double>::denorm_min()));
    const long double precision = pasMinimal * nombreClasses;
    if (static_cast<long double>(maximum) - minimum <= precision) {
        const long double marge = minimum == maximum ? std::max(0.5L, precision) : precision;
        constexpr long double limite = std::numeric_limits<double>::max();
        minimum = static_cast<double>(std::max(-limite, minimum - marge));
        maximum = static_cast<double>(std::min(limite, maximum + marge));
    }

    Histogramme resultat;
    resultat.effectifs.resize(nombreClasses);
    resultat.bornes.reserve(nombreClasses + 1);
    for (size_t i = 0; i <= nombreClasses; ++i)
        resultat.bornes.push_back(std::lerp(minimum, maximum, static_cast<double>(i) / nombreClasses));

    // Une mesure sur une borne intérieure appartient à la classe de droite.
    // La dernière classe comprend aussi la plus grande mesure.
    for (double mesure : mesures) {
        if (std::isfinite(mesure)) {
            const auto premiereBorne = resultat.bornes.begin() + 1;
            const auto suivante = std::upper_bound(premiereBorne, resultat.bornes.end() - 1, mesure);
            ++resultat.effectifs[static_cast<size_t>(suivante - premiereBorne)];
        }
    }
    return resultat;
}

} // namespace stats
