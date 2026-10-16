// Onglet du grapheur du mode Expert : en haut, le choix des grandeurs ; dessous, le graphique, toujours
// là (vide tant qu'il manque une grandeur), avec son titre et les légendes de ses axes, unités
// comprises. Une grandeur cinématique en fonction d'une autre (positions, vitesses, accélérations ;
// pas d'énergie). Le graphique est dessiné par le programme : molette pour zoomer, glisser pour se
// déplacer, double-clic pour revenir à la vue complète.

#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

class App;
class QComboBox;
class QLabel;
class QPushButton;

struct Grapheur {
    /// grandeur en abscisse : la date, d'office
    std::optional<QString> x = QStringLiteral("t");
    /// grandeur en ordonnée
    std::optional<QString> y;
    /// 0 : points seuls, 1 : points et lignes, 2 : lignes seules
    int style = 0;
};

/// nom affiché d'une grandeur : les normes sont entre barres verticales
QString nom_affiche(const QString &nom);

/// le graphique : titre, points et lignes, axes gradués, légendes
class Graphe : public QWidget {
    Q_OBJECT

public:
    explicit Graphe(QWidget *parent);
    /// Nouveaux points, titre et légendes ; la vue complète est rétablie si les grandeurs ont changé.
    /// Sans points, le graphique est vide : sa grille, ses légendes, et `message` au milieu.
    void montre(const std::vector<QPointF> &points, const QString &titre, const QString &legende_x,
                const QString &legende_y, int style, bool nouvelles_grandeurs, const QString &message = {});
    const QString &titre() const { return m_titre; }
    const QString &message() const { return m_message; }
    /// revient à la vue complète des points
    void vue_complete();
    /// la zone visible, en unités des grandeurs
    QRectF vue() const { return m_vue; }

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QRectF zone() const;
    QPointF ecran(QPointF p) const;
    QPointF valeur(QPointF ecran) const;

    std::vector<QPointF> m_points;
    QString m_titre, m_message, m_legende_x, m_legende_y;
    int m_style = 0;
    QRectF m_vue;
    std::optional<QPointF> m_glisse;
};

/// l'onglet Grapheur du mode Expert
class OngletGraph : public QWidget {
    Q_OBJECT

public:
    OngletGraph(App &app, QWidget *parent);
    void maj();
    Graphe *graphe() const { return m_graphe; }

private:
    void enregistre_image();

    App &m_app;
    QLabel *m_tracer, *m_en_fonction, *m_style_titre, *m_aide;
    QComboBox *m_y, *m_x, *m_style;
    QPushButton *m_enregistrer;
    Graphe *m_graphe;
    /// les grandeurs du dernier graphique
    std::optional<std::pair<QString, QString>> m_grandeurs;
};
