// Graphique du profil d'intensité : courbe, curseurs A et B, zoom horizontal à la molette.

#pragma once

#include "i18n.h"
#include "session.h"

#include <QTimer>
#include <QWidget>

#include <optional>

class GraphiqueProfil : public QWidget {
    Q_OBJECT

public:
    GraphiqueProfil(Seance &seance, QWidget *parent = nullptr);

    void definit_traductions(const Textes *tr);
    /// Revient à la vue complète de la coupe (nouvelle coupe, double-clic).
    void reinitialise_vue();
    /// Partie visible de la coupe, en fractions de sa longueur (essais de l'interface).
    void definit_vue(double t0, double t1);
    void selectionne_curseur(int index);
    void deplace_un_echantillon(int sens);
    void termine_clic();
    bool annule_glisser();

signals:
    /// Curseur A ou B déplacé sur le graphique.
    void curseurs_deplaces();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    /// Zone de la courbe, à l'intérieur des axes.
    QRectF cadre_graphique() const;
    /// Fraction de la coupe affichée au bord gauche ou droit, à l'abscisse `px` (pixels du widget).
    double fraction_en(double px) const;
    void place_curseur(int index, double t);

    Seance &m_seance;
    const Textes *m_textes = nullptr;
    /// Partie visible de la coupe, en fractions de sa longueur (marges de 5 % de chaque côté).
    double m_fractionGauche = -0.05;
    double m_fractionDroite = 1.05;
    struct Geste {
        Travail avant;
        int curseur;
        QPointF depart;
        bool deplace = false;
    };
    std::optional<Geste> m_geste;
    /// Un clic immobile ne place le curseur qu'une fois écarté le double-clic de recentrage.
    QTimer m_delaiClic;
    int m_curseurClic = 0;
    double m_fractionClic = 0.0;
    std::optional<QPointF> m_survol;
};
