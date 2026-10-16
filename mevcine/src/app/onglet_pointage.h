// L'image de la vidéo et ce qui l'entoure, commun aux deux modes : la loupe et les coordonnées du
// point visé, la glissière des images, les gestes sur l'image (origine, étalon, pointage, zones à
// suivre). Et l'onglet Pointage du mode Expert : la barre du repère en haut (origine, axes, échelle),
// le panneau du pointage et de la vidéo à gauche.

#pragma once

#include "dessin.h"
#include "vecteur.h"

#include <QPixmap>
#include <QWidget>

#include <array>
#include <optional>
#include <vector>

class App;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QToolButton;

/// un geste de la souris sur l'image, en pixels de l'image
struct Geste {
    enum class Type { Clic, ClicDroit, DebutGlisse, Glisse, FinGlisse };
    Type type;
    /// où le bouton a été enfoncé (le glissement n'est reconnu qu'après quelques pixels)
    Vecteur origine;
    Vecteur position;
};

/// L'image de la vidéo, avec les pointages, le repère, l'étalon et les zones à suivre
class ZoneVideo : public QWidget {
    Q_OBJECT

public:
    ZoneVideo(App &app, QWidget *parent);
    /// la place de l'image dans le widget
    dessin::Vue vue() const;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;

private:
    void dessine_sur_video(QPainter &p, const dessin::Vue &vue) const;
    void maj_curseur(QPointF pos);

    App &m_app;
    /// bouton enfoncé : position et bouton
    std::optional<QPointF> m_appui;
    Qt::MouseButton m_bouton = Qt::NoButton;
    bool m_glisse = false;
    /// l'image réduite à la taille de la vue, gardée tant que ni l'image ni la vue ne changent
    QPixmap m_cache;
    qint64 m_cle_cache = 0;
};

/// la loupe : une portion de l'image autour de la souris, grossie
class Loupe : public QWidget {
    Q_OBJECT

public:
    Loupe(App &app, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    App &m_app;
};

/// Les coordonnées du point visé : un petit tableau dont les en-têtes (x, y ; px, m) se distinguent
/// des valeurs
class TableCoordonnees : public QWidget {
    Q_OBJECT

public:
    struct Ligne {
        /// l'unité : px ou m
        QString unite;
        QString x, y;
    };

    explicit TableCoordonnees(QWidget *parent);
    void montre(std::vector<Ligne> lignes);
    const std::vector<Ligne> &lignes() const { return m_lignes; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *event) override;

private:
    /// largeur de la colonne des unités, des colonnes des valeurs ; hauteur d'une ligne
    int largeur_unites() const;
    int largeur_valeurs() const;
    int hauteur_ligne() const;

    std::vector<Ligne> m_lignes;
};

/// la loupe, son titre, et les coordonnées du point sous la souris
class PanneauLoupe : public QWidget {
    Q_OBJECT

public:
    PanneauLoupe(App &app, QWidget *parent);
    void maj();
    TableCoordonnees *coordonnees() const { return m_coordonnees; }

private:
    App &m_app;
    QLabel *m_titre;
    Loupe *m_loupe;
    TableCoordonnees *m_coordonnees;
};

/// La barre sous l'image : rotation (mode Expert), choix de l'image, défaire et refaire ; à droite,
/// le passage en plein écran (ou son abandon, `dans_plein_ecran`)
class ControlesImage : public QWidget {
    Q_OBJECT

public:
    ControlesImage(App &app, QWidget *parent, bool dans_plein_ecran = false);
    void maj();

private:
    App &m_app;
    bool m_dans_plein_ecran;
    QToolButton *m_gauche, *m_droite, *m_defaire, *m_refaire, *m_plein_ecran;
    QLabel *m_image, *m_sur;
    QSpinBox *m_numero;
    QSlider *m_glissiere;
};

/// La barre du repère, en haut de l'onglet Pointage du mode Expert : 1. l'origine, 2. le sens des
/// axes, 3. l'échelle ; et la consigne pendant le choix de l'origine ou de l'étalon
class BarreRepere : public QWidget {
    Q_OBJECT

public:
    BarreRepere(App &app, QWidget *parent);
    void maj();

private:
    App &m_app;
    QLabel *m_t_origine, *m_t_axes, *m_t_echelle, *m_echelle_etat, *m_consigne;
    QPushButton *m_origine, *m_echelle, *m_annuler;
    std::array<QToolButton *, 4> m_axes{};
    QWidget *m_ligne_consigne;
};

/// le panneau de gauche de l'onglet Pointage du mode Expert : la loupe, le pointage, la vidéo
class ControlesPointage : public QWidget {
    Q_OBJECT

public:
    ControlesPointage(App &app, QWidget *parent);
    void maj();
    void maj_souris();

private:
    App &m_app;
    PanneauLoupe *m_loupe;
    QLabel *m_t_pointage, *m_objets, *m_t_video, *m_ips, *m_cadence, *m_nom_video, *m_info_video;
    QPushButton *m_demarrer, *m_arreter, *m_stop, *m_recommencer;
    /// Démarrer (ou Reprendre), Arrêter, STOP : un seul à la fois, à la même place et de la même
    /// hauteur, quel que soit son texte
    QStackedWidget *m_pile_pointage;
    QSpinBox *m_nb_obj;
    QCheckBox *m_auto;
    QLineEdit *m_saisie_ips;
};

/// l'onglet Pointage du mode Expert
class OngletPointage : public QWidget {
    Q_OBJECT

public:
    OngletPointage(App &app, QWidget *parent);
    void maj();
    /// après un mouvement de la souris sur l'image
    void maj_souris();
    ZoneVideo *zone() const { return m_zone; }

private:
    BarreRepere *m_barre;
    ControlesPointage *m_controles;
    ZoneVideo *m_zone;
    ControlesImage *m_controles_image;
};
