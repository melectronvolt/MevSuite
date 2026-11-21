// État du travail sur l'image : outil, étalon, mesures, ligne de coupe. Partagé par la fenêtre
// principale, la zone image et le graphique du profil.

#pragma once

#include "model.h"
#include "profile.h"
#include "view.h"

#include <QList>
#include <QString>

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <utility>

/// Ces numéros identifient l'outil dans les fichiers .mevpix.
enum class Outil { Etalonner = 0, Mesurer = 1, Profil = 2 };

/// Segment modifiable à la souris.
struct CibleTrace {
    enum Nature { Etalon, Mesure, Coupe } nature;
    /// Indice de la mesure dans la liste (M1 a l’indice 0).
    int indiceMesure = 0;
};

/// Données éditables d'une image. La navigation et le cache du profil ne font pas partie de l'historique.
struct Travail {
    std::optional<Segment> segmentEtalon;
    QString saisieLongueur;
    Unite uniteSaisie = Unite::Millimetre;
    std::optional<Etalonnage> etalonnage;

    QList<Segment> mesures;

    std::optional<Segment> coupe;
    int largeurBande = 5;
    Canal canal = Canal::GrisPondere;
    /// Position des curseurs A et B, en fraction de la longueur de la coupe.
    std::array<double, 2> fractionsCurseurs = {0.25, 0.75};

    bool operator==(const Travail &) const = default;
};

/// Tout ce qui figure dans le fichier .mevpix. Une copie partage les pixels en lecture seule.
struct ContenuSeance : Travail {
    std::shared_ptr<const ImageChargee> image;
    Outil outil = Outil::Etalonner;
    bool operator==(const ContenuSeance &) const = default;
};

struct Seance : ContenuSeance {
    int curseurActif = 0;

    /// Une édition instantanée ou la fin d'un glisser compte pour une seule annulation.
    template<typename F> void modifie(F edition)
    {
        Travail avant = *this;
        edition();
        memorise(std::move(avant));
    }
    void memorise(Travail avant);
    bool annule();

    /// Donne accès au segment dont l'utilisateur déplace une poignée.
    Segment *segment(CibleTrace cible);
    /// Étalonnage que donneraient le segment, la longueur et l'unité actuellement saisis.
    std::optional<Etalonnage> etalonnage_saisi() const;
    /// Vrai si l'étalon, la longueur ou l'unité ont changé depuis la validation.
    bool etalonnage_en_attente() const;
    /// Vrai si changer d'image ferait perdre un étalonnage, des mesures ou une coupe.
    bool a_du_travail() const { return etalonnage.has_value() || !mesures.empty() || coupe.has_value(); }
    /// Une nouvelle image invalide l'étalonnage et les tracés précédents.
    void definit_image(std::shared_ptr<const ImageChargee> nouvelleImage);

    /// Unité des abscisses du profil et sa valeur par pixel.
    std::pair<QString, double> facteur_unite() const;
    /// Une ancienne séance peut contenir une coupe de moins d'un pixel. Ses poignées restent éditables.
    bool coupe_mesurable() const { return coupe && coupe->est_mesurable(); }
    /// Profil de la coupe actuelle, vide si elle manque ou mesure moins d'un pixel.
    /// Le calcul est repris lorsque la coupe, l'épaisseur ou le canal change.
    const Profil *profil();

private:
    std::deque<Travail> m_historique;
    struct ProfilEnCache {
        Segment seg;
        int largeurBande;
        Canal canal;
        Profil profil;
    };
    std::optional<ProfilEnCache> m_profilEnCache;
};
