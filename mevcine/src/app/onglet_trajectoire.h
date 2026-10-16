// Onglet des trajectoires du mode Expert : tracé des points pointés, chronophotographie,
// chronogramme, vecteurs vitesse, changement de référentiel, et vidéo recadrée autour d'un objet.

#pragma once

#include "pointage.h"

#include <QDialog>
#include <QImage>
#include <QSize>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <vector>

class App;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;

enum class ModeTrajectoire { Trajectoire, Chronophotographie, Chronogramme };

struct Trajectoire {
    ModeTrajectoire mode = ModeTrajectoire::Trajectoire;
    bool vecteurs = false;
    bool pres_souris = false;
    /// nombre de pixels de l'image pour 1 m/s (pour 1 px/s sans échelle), tel qu'il est saisi ; vide
    /// pour l'échelle automatique
    QString echelle_vitesse;
    /// image de fond de la chronophotographie
    int chrono_img = 1;
    /// 0 pour le référentiel de la caméra, sinon le numéro d'un objet
    int referentiel = 0;

    void choisit_camera() { referentiel = 0; }
    /// l'échelle des vitesses saisie, si elle est correcte ; rien pour l'échelle automatique
    std::optional<double> echelle_saisie() const;
};

/// Le nombre « rond » (1, 2 ou 5 fois une puissance de 10) le plus proche de `x`, positif
double arrondi_rond(double x);
/// L'échelle automatique des vitesses, en pixels de l'image pour 1 m/s : le plus long vecteur mesure
/// environ le septième de l'image
double echelle_vitesse_auto(const Pointage &p, QSize dimensions);

/// La vidéo recadrée de façon que l'objet de référence reste immobile
struct Ralenti {
    int obj = 1;
    /// index (à partir de 0) des images où l'objet est pointé
    std::vector<int> images;
    /// positions de l'objet sur ces images
    std::vector<Vecteur> positions;
    /// position de l'objet dans le cadre
    Vecteur decalage;
    QSize taille;

    /// calcule le plus grand cadre qui peut suivre l'objet sans déborder de l'image
    static std::optional<Ralenti> nouveau(const Pointage &p, int obj, QSize dimensions);
};

/// le dessin des trajectoires
class DessinTrajectoire : public QWidget {
    Q_OBJECT

public:
    DessinTrajectoire(App &app, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    App &m_app;
    std::optional<QPointF> m_souris;
};

/// l'onglet Trajectoires du mode Expert
class OngletTrajectoire : public QWidget {
    Q_OBJECT

public:
    OngletTrajectoire(App &app, QWidget *parent);
    void maj();
    DessinTrajectoire *dessin() const { return m_dessin; }

private:
    void enregistre_image();

    App &m_app;
    QComboBox *m_mode;
    QGroupBox *m_groupe_vecteurs, *m_groupe_chrono;
    QLabel *m_titre_vecteurs, *m_titre_echelle, *m_px_par_ms, *m_incorrecte, *m_titre_chrono, *m_image_n,
        *m_titre_referentiel;
    QCheckBox *m_vecteurs;
    QRadioButton *m_pres, *m_partout;
    QLineEdit *m_echelle_vitesse;
    QSpinBox *m_chrono_img;
    QPushButton *m_enregistrer, *m_voir;
    QComboBox *m_referentiel;
    DessinTrajectoire *m_dessin;
};

/// la fenêtre de la vidéo recadrée autour d'un objet
class FenetreRalenti : public QDialog {
    Q_OBJECT

public:
    FenetreRalenti(App &app, Ralenti ralenti, QWidget *parent);

private:
    void image_suivante();

    App &m_app;
    Ralenti m_r;
    size_t m_k = 0;
    QTimer m_minuterie;
    QLabel *m_image, *m_titre_ralenti;
    QSlider *m_ralenti;
};
