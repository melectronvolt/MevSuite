// Suivi par apparence et continuité du mouvement, en coordonnées de l'image vidéo.
#pragma once

#include <QImage>
#include <QPointF>

#include <array>
#include <memory>
#include <optional>
#include <span>

namespace detect {

/// Les objets d'une même image partagent ses pixels et ses réductions, calculées à la demande.
class Image {
public:
    explicit Image(const QImage &image);

private:
    friend class Piste;
    const QImage &niveau(int facteur);
    std::array<QImage, 3> m_niveaux;
};

struct Observation {
    std::optional<QPoint> coin;
    bool a_verifier = false;
};

struct Motif;
class Piste {
public:
    Piste(const QImage &motif, std::optional<QPoint> coin, double date);
    QSize taille() const;
    std::optional<QPoint> dernier_coin() const { return m_dernier; }
    Observation recherche(Image &image, double date) const;
    /// Appelé après comparaison avec les autres mobiles. Seule une observation sûre guide la suite.
    void enregistre(Observation observation, double date);

private:
    std::shared_ptr<const Motif> m_motif;
    std::optional<QPoint> m_dernier;
    QPointF m_vitesse;
    double m_date, m_intervalle = 0;
    int m_manques = 0;
};

/// Recherche ponctuelle d'un motif, avec ou sans position de départ.
std::optional<QPoint> localise_motif(const QImage &motif, const QImage &image, std::optional<QPoint> coin,
                                     bool *douteux = nullptr);
struct ExempleDetail;
using ReferenceDetail = std::shared_ptr<const ExempleDetail>;
/// Conserve le détail pointé et son voisinage sur une image de référence.
ReferenceDetail prepare_detail(const QImage &image, QPointF point);
/// Les exemples vont du plus ancien au plus récent. La recherche porte sur le voisinage de la dernière mesure.
std::optional<QPointF> retrouve_detail(std::span<const ReferenceDetail> exemples, const QImage &image, QPointF derniere);
/// Cas d'un seul pointage sur la trame précédente.
std::optional<QPointF> retrouve_detail(const QImage &precedente, const QImage &suivante, QPointF point);
std::optional<QImage> decoupe(const QImage &image, int x, int y, int w, int h);
QImage reduit(const QImage &image, int facteur);
int facteur(const QImage &image, const QImage &motif);

} // namespace detect
