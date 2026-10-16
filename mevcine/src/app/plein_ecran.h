// Le pointage en plein écran, dans les deux modes : l'image de la vidéo sur tout l'écran ; la loupe,
// les coordonnées du point visé et la consigne dans un cadre posé sur l'image, qui passe de l'autre
// côté quand la souris s'en approche ; en bas, la barre des images, l'arrêt du pointage et le retour
// à la fenêtre (Échap).

#pragma once

#include <QWidget>

class App;
class ControlesImage;
class PanneauLoupe;
class QFrame;
class QLabel;
class QPushButton;
class ZoneVideo;

class PleinEcran : public QWidget {
    Q_OBJECT

public:
    PleinEcran(App &app, QWidget *parent);
    void maj();
    /// après un mouvement de la souris sur l'image
    void maj_souris();
    ZoneVideo *zone() const { return m_zone; }
    /// le cadre de la loupe, et son côté
    QFrame *cadre_loupe() const { return m_cadre; }
    bool cadre_a_droite() const { return m_a_droite; }

protected:
    bool eventFilter(QObject *objet, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    /// pose le cadre de la loupe dans un coin haut de l'image, du côté choisi
    void place_cadre();

    App &m_app;
    ZoneVideo *m_zone;
    QFrame *m_cadre;
    PanneauLoupe *m_loupe;
    QLabel *m_consigne;
    ControlesImage *m_controles;
    QPushButton *m_arreter;
    bool m_a_droite = false;
};
