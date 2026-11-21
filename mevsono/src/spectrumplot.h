// Graphique du spectre de l'extrait : amplitudes (linéaires ou en dB), axe des fréquences linéaire ou
// logarithmique, pics repérés et repères des harmoniques (mode expert). Le réticule donne la valeur
// sous la souris et celle du pic le plus proche (rond plein). Mode expert : clic droit sur un pic pour
// le prendre comme fondamentale.

#pragma once

#include "analyzer.h"
#include "plot.h"
#include "session.h"

#include <QTimer>

#include <optional>

class SpectrumPlot : public PlotWidget {
    Q_OBJECT

public:
    SpectrumPlot(Session &session, const Analyzer &analyzer, QWidget *parent = nullptr);

    /// Pic le plus proche de la souris.
    std::optional<dsp::Peak> pic_survole() const;
    /// À appeler quand l'analyse ou les réglages ont changé (repères, calcul en cours).
    void analyse_changee();
    std::optional<dsp::Peak> pic_selectionne() const { return m_selection; }
    void memorise();
    void oublie() { m_reference.reset(); invalide_courbes(); }
    void normalise(bool oui) { m_normalise = oui; invalide_courbes(); }
    bool a_reference() const { return m_reference.has_value(); }
    bool reference_compatible() const;
    QString nom_reference() const { return m_nomReference; }

signals:
    void selection_changee();
    /// Fondamentale choisie dans le menu du clic droit ; vide : détection automatique.
    void fondamentale_choisie(std::optional<double> f0);

protected:
    double centre_zoom_clavier() const override;
    void keyPressEvent(QKeyEvent *event) override;
    void gauche_appuye(QPointF pos) override;
    void gauche_relache(QPointF pos) override;
    bool a_des_donnees() const override;
    void dessine_vide(QPainter &painter) override;
    std::pair<double, double> x_par_defaut() const override;
    std::pair<double, double> domaine_x() const override;
    std::pair<double, double> prepare_courbes(size_t colonnes) override;
    QString legende_axe_x() const override;
    QString legende_axe_y() const override;
    std::vector<Tick> graduations_x(double x0, double x1, double pixels) const override;
    QString texte_survol(QPointF data) const override;
    void dessine_donnees(QPainter &painter, const QRectF &r) override;
    void clic_droit(QPointF pos) override;

private:
    const dsp::Analysis *analyse_courante() const;
    bool axe_logarithmique() const { return m_session.reglages_actifs().logFreq; }
    bool echelle_decibels() const { return m_session.reglages_actifs().db; }
    /// Abscisse d'une fréquence (log10 de la fréquence sur l'axe logarithmique), et inversement.
    double vers_x(double f) const;
    double depuis_x(double x) const;
    /// Amplitude affichée : « −12,3 dB » ou « 0,251 ».
    QString texte_amplitude(double y) const;

    std::optional<dsp::Analysis> m_reference;
    std::vector<QPointF> m_points, m_pointsReference;
    dsp::SpectrumParams m_parametresReference;
    QString m_nomReference;
    bool m_normalise = false;
    Session &m_session;
    const Analyzer &m_analyzer;
    void selectionne(std::optional<dsp::Peak> pic);
    std::optional<dsp::Peak> pic_a_la_position(QPointF position) const;
    std::optional<dsp::Peak> m_selection;
    std::optional<QPointF> m_appui;
    /// Animation de l'attente pendant le calcul.
    QTimer m_spinner;
    int m_spinnerAngle = 0;
};
