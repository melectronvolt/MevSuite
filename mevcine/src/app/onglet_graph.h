// Onglet du grapheur du mode Expert : en haut, le choix des grandeurs ; dessous, le graphique, toujours
// là (vide tant qu'il manque une grandeur), avec son titre et les légendes de ses axes, unités
// comprises. Une grandeur cinématique en fonction d'une autre (positions, vitesses, accélérations ;
// pas d'énergie). Le graphique est dessiné par le programme : molette pour zoomer, glisser pour se
// déplacer, double-clic pour revenir à la vue complète.

#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <span>
#include <vector>

class App;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;

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
    struct Point {
        QPointF coordonnees;
        int image;
        double date;
        bool operator==(const Point &) const = default;
    };
    explicit Graphe(QWidget *parent);
    /// Nouveaux points, titre et légendes ; la vue complète est rétablie si les grandeurs ont changé.
    /// Sans points, le graphique est vide : sa grille, ses légendes, et `message` au milieu.
    void montre(const std::vector<Point> &points, const QString &titre, const QString &legende_x,
                const QString &legende_y, int style, bool nouvelles_grandeurs, const QString &message = {});
    const QString &titre() const { return m_titre; }
    const QString &message() const { return m_message; }
    /// revient à la vue complète des points
    void vue_complete();
    /// la zone visible, en unités des grandeurs
    QRectF vue() const { return m_vue; }
    QPointF ecran(QPointF coordonnees) const;
    const std::optional<Point> &lecture() const { return m_lecture; }
    QPixmap image_du_trace();

signals:
    void pointClique(int image);
    void lectureChangee();
protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Troncon {
        size_t premier;
        QPointF debut, controle1, controle2, fin;
        QPointF valeur(double u) const;
        double distance_carree(QPointF souris, const QTransform &repere, const QRectF &cadre, double limite) const;
    };
    QRectF zone() const;
    QTransform vers_ecran(const QRectF &zone) const;
    QPointF valeur(QPointF ecran) const;
    void construit_courbe();
    const Point *point_proche(std::span<const Point> points, QPointF souris, const QTransform &repere, double limite) const;
    std::optional<Point> lit(QPointF souris) const;
    void affiche_lecture(std::optional<Point> lecture);

    std::vector<Point> m_points;
    std::vector<Troncon> m_troncons;
    QPainterPath m_courbe;
    std::optional<Point> m_lecture;
    QTimer m_clic;
    int m_imageCliquee = 0;
    QString m_titre, m_message, m_legende_x, m_legende_y;
    int m_style = 0;
    QRectF m_vue;
    QPointF m_appui;
    std::optional<QPointF> m_glisse;
    bool m_deplace = false;
};

/// l'onglet Grapheur du mode Expert
class OngletGraph : public QWidget {
    Q_OBJECT

public:
    OngletGraph(App &app, QWidget *parent);
    void actualise();
    Graphe *graphe() const { return m_graphe; }

private:
    void enregistre_image();
    void actualise_lecture();

    App &m_app;
    QLabel *m_tracer, *m_en_fonction, *m_style_titre, *m_aide, *m_lecture;
    QStackedWidget *m_indications;
    QComboBox *m_y, *m_x, *m_style;
    QPushButton *m_enregistrer;
    Graphe *m_graphe;
    /// les grandeurs du dernier graphique
    std::optional<std::pair<QString, QString>> m_grandeurs;
};
