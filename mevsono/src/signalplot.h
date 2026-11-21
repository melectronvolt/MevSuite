// Graphique du signal : tracé du son, position de lecture, et les curseurs de l'activité choisie :
// t₁ et t₂ de mesure d'une période, ou l'extrait analysé (A et B, A toujours avant B). Glisser près
// d'un curseur le déplace ; ailleurs, glisser trace une nouvelle paire ; un simple clic amène le
// curseur le plus proche.

#pragma once

#include "plot.h"
#include "session.h"

#include <optional>

class SignalPlot : public PlotWidget {
    Q_OBJECT

public:
    SignalPlot(Session &session, QWidget *parent = nullptr);

    /// Abandonne le geste quand le son ou l'activité change.
    void annule_geste();
    /// Position de lecture (secondes), pendant la lecture ou la pause.
    void place_tete_lecture(std::optional<double> t);

signals:
    /// Curseur A ou B déplacé.
    void extrait_change();
    /// Curseur t₁ ou t₂ déplacé.
    void periode_changee();

protected:
    double centre_zoom_clavier() const override;
    void keyPressEvent(QKeyEvent *event) override;
    bool a_des_donnees() const override;
    void dessine_vide(QPainter &painter) override;
    std::pair<double, double> x_par_defaut() const override;
    std::pair<double, double> prepare_courbes(size_t colonnes) override;
    QString legende_axe_x() const override;
    QString legende_axe_y() const override;
    QString texte_survol(QPointF data) const override;
    void dessine_donnees(QPainter &painter, const QRectF &r) override;
    void survol_deplace(std::optional<QPointF> pos) override;
    void gauche_appuye(QPointF pos) override;
    void gauche_deplace(QPointF pos) override;
    void gauche_relache(QPointF pos) override;

private:
    /// Instant sous l'abscisse `px`, borné à la durée du son.
    double temps_en(double px) const;
    /// Paire de curseurs de l'activité choisie (vide : pas encore de curseurs de mesure).
    std::optional<Cursors> paire_curseurs() const;
    void place_paire(const Cursors &c);
    void ordonne_curseurs_periode();
    /// Curseur de l'activité choisie à moins de `RAYON_PRISE` pixels de l'abscisse `px`.
    std::optional<int> curseur_proche(double px) const;
    /// Aimante les curseurs `moved` de mesure aux passages par zéro montants (si l'option est active).
    void aligne(std::initializer_list<int> moved);

    Session &m_session;
    std::vector<QPointF> m_points;
    std::optional<double> m_teteLecture;

    /// Un glissement déplace un curseur existant ou crée une paire depuis l'instant de départ.
    struct Glissement {
        std::optional<int> curseur;
        double instantDepart = 0.0;
    };
    std::optional<QPointF> m_pointAppui;
    std::optional<Glissement> m_glissement;
    int m_curseurActif = 0;
    void applique_clic(double instant);
};
