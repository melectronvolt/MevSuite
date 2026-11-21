#include "session.h"

using namespace Qt::StringLiterals;

void Seance::memorise(Travail avant)
{
    if (avant == static_cast<const Travail &>(*this))
        return;
    if (m_historique.size() == 100)
        m_historique.pop_front();
    m_historique.push_back(std::move(avant));
}

bool Seance::annule()
{
    if (m_historique.empty())
        return false;
    static_cast<Travail &>(*this) = std::move(m_historique.back());
    m_historique.pop_back();
    return true;
}

Segment *Seance::segment(CibleTrace cible)
{
    switch (cible.nature) {
    case CibleTrace::Etalon: return segmentEtalon ? &*segmentEtalon : nullptr;
    case CibleTrace::Mesure:
        return cible.indiceMesure >= 0 && cible.indiceMesure < mesures.size() ? &mesures[cible.indiceMesure] : nullptr;
    case CibleTrace::Coupe: return coupe ? &*coupe : nullptr;
    }
    return nullptr;
}

std::optional<Etalonnage> Seance::etalonnage_saisi() const
{
    const auto longueurReelle = lit_nombre(saisieLongueur);
    if (!longueurReelle || !segmentEtalon)
        return std::nullopt;
    const double diagonalePx = image ? image->diagonale_px() : 0.0;
    return Etalonnage::fabrique(segmentEtalon->longueur_px(), *longueurReelle, uniteSaisie, diagonalePx);
}

bool Seance::etalonnage_en_attente() const
{
    if (!etalonnage)
        return false;
    const auto saisie = etalonnage_saisi();
    return !saisie || saisie->unite != etalonnage->unite || !qFuzzyCompare(saisie->referencePx, etalonnage->referencePx)
           || !qFuzzyCompare(saisie->longueurReelle, etalonnage->longueurReelle);
}

void Seance::definit_image(std::shared_ptr<const ImageChargee> nouvelleImage)
{
    image = std::move(nouvelleImage);
    outil = Outil::Etalonner;
    saisieLongueur.clear();
    segmentEtalon.reset();
    etalonnage.reset();
    mesures.clear();
    coupe.reset();
    fractionsCurseurs = {0.25, 0.75};
    curseurActif = 0;
    m_historique.clear();
    m_profilEnCache.reset();
}

std::pair<QString, double> Seance::facteur_unite() const
{
    if (etalonnage)
        return {symbole_unite(etalonnage->unite), etalonnage->unites_par_pixel()};
    return {u"px"_s, 1.0};
}

const Profil *Seance::profil()
{
    if (!image || !coupe_mesurable()) {
        m_profilEnCache.reset();
        return nullptr;
    }
    if (!m_profilEnCache || m_profilEnCache->seg != *coupe
        || m_profilEnCache->largeurBande != largeurBande || m_profilEnCache->canal != canal) {
        m_profilEnCache = ProfilEnCache{*coupe, largeurBande, canal,
                                       echantillonne_profil(image->rgb, *coupe, largeurBande, canal)};
    }
    return &m_profilEnCache->profil;
}
