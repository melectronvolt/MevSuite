// Outils de dessin partagés par les onglets : correspondance entre les coordonnées de l'image et
// celles de l'écran, croix, repère, flèches ; couleurs et styles des textes de l'interface.

#pragma once

#include "vecteur.h"

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QScrollArea>
#include <QSize>
#include <QSplitter>

#include <optional>

class QLabel;
class QPainter;
class QPalette;
class QWidget;

namespace dessin {

/// couleur de l'objet n° `obj` (à partir de 1) : rouge, bleu, cyan, magenta, jaune, gris, vert
QColor couleur(int obj);

inline const QColor VERT(0, 128, 0);
inline const QColor ROUGE(255, 0, 0);
inline const QColor ORANGE(255, 165, 0);
/// la couleur du repère dessiné sur l'image : un vert vif, bordé de sombre
inline const QColor VERT_REPERE(40, 230, 90);
/// le bord sombre, léger, des traits et des textes dessinés sur l'image
inline const QColor HALO(0, 0, 0, 120);

/// Placement d'une image à l'écran : rectangle occupé et facteur d'échelle
struct Vue {
    QRectF rect;
    /// pixels d'écran par pixel de l'image
    double echelle = 1.0;

    /// la plus grande vue d'une image de `taille` pixels qui tienne dans `dispo`, centrée
    /// horizontalement
    static Vue ajuste(const QRectF &dispo, QSize taille);
    QPointF ecran(Vecteur p) const;
    Vecteur image(QPointF pos) const;
};

/// longueur des axes dessinés sur une vue : visibles, sans cacher l'image
double longueur_axes(const Vue &vue);

/// petite croix centrée sur `pos`, de demi-largeur `demi` et de trait `epaisseur`
void croix(QPainter &p, QPointF pos, double demi, const QColor &couleur, double epaisseur = 1.0);

/// un texte épais bordé de sombre, lisible sur tous les fonds, centré sur `pos`
void texte_halo(QPainter &p, QPointF pos, const QString &texte, const QFont &police, const QColor &couleur);

/// Dessine les axes du repère : des flèches de `longueur` pixels de part et d'autre de l'origine,
/// avec leurs noms x et y au bout. Sur l'image d'une vidéo (`sur_image`), les traits sont bordés
/// d'un liseré sombre léger, pour rester visibles sur tous les fonds.
void repere(QPainter &p, QPointF origine, int sens_x, int sens_y, const QColor &couleur, double longueur,
            bool sur_image);

/// L'origine O du repère sur l'image : un petit point et son nom, placé du côté opposé aux axes
void origine(QPainter &p, QPointF pos, int sens_x, int sens_y, const QColor &couleur);

/// flèche de `org` à `ext`, d'épaisseur `epaisseur`, avec une pointe de taille fixe
void fleche(QPainter &p, QPointF org, QPointF ext, const QColor &couleur, double epaisseur = 2.0);

/// texte posé sur sa ligne de base (centré ou non), éventuellement sur un fond coloré
void texte(QPainter &p, QPointF pos, const QString &texte, double taille, const QColor &couleur,
           const std::optional<QColor> &fond, bool centre);

// ------------------------------------------------------------------ styles des textes

/// vrai si le thème du bureau est sombre (texte plus clair que le fond)
bool sombre(const QPalette &palette);
/// Couleurs des messages : réussite, avertissement (une action attendue), erreur ; lisibles sur fond
/// clair comme sur fond sombre (les mêmes dans tous les logiciels de la suite)
QColor couleur_reussite(const QPalette &palette);
QColor couleur_attention(const QPalette &palette);
QColor couleur_erreur(const QPalette &palette);

/// police de l'interface agrandie (ou réduite) d'un facteur, pour dessiner
QFont police(double facteur, bool gras = false);
/// police à chasse fixe, pour les nombres alignés, à la taille de l'interface
QFont police_chiffres(double facteur = 1.0);

/// Un titre de panneau ou de graphique (et un bouton à mettre en avant) : en demi-gras, à la taille de
/// l'interface ; il suit son agrandissement. Le gras plein ne sert qu'au nom, sur la page d'accueil
void demi_gras(QWidget *w);
/// Les chiffres d'un widget à chasse fixe, à la taille de l'interface : ils suivent son agrandissement
void chiffres(QWidget *w);
/// Donne à un widget la police de l'interface agrandie d'un facteur (page d'accueil seulement), et
/// la retient : elle suit l'agrandissement de l'interface (voir `reapplique_polices`)
void style_police(QWidget *w, double facteur, bool gras = false);
/// redonne leur police aux widgets d'une fenêtre (`style_police`), après un changement
/// d'agrandissement de l'interface
void reapplique_polices(QWidget *racine);

/// la nature d'un texte coloré
enum class Ton { Reussite, Attention, Erreur, Discret };
/// la couleur d'un ton, dans le thème de la palette
QColor couleur_ton(Ton ton, const QPalette &palette);
/// change la couleur du texte d'un widget
void colore(QWidget *w, const QColor &couleur);
/// Donne au texte d'un widget la couleur d'un ton ; elle suit le thème clair ou sombre (voir
/// `recolore`)
void colore(QWidget *w, Ton ton);
/// texte secondaire, plus discret (indications : couleur PlaceholderText, taille normale)
void discret(QWidget *w);
/// redonne leur couleur aux textes colorés et aux panneaux latéraux d'une fenêtre, après un
/// changement de thème
void recolore(QWidget *racine);

// ------------------------------------------------------------------ mise en page

/// largeur de départ des panneaux latéraux (au moins 300 points), d'après la police
int largeur_panneau();
/// un trait horizontal de séparation
QWidget *trait(QWidget *parent);
/// le titre d'une partie d'un panneau : en demi-gras, à la taille du texte
QLabel *titre_partie(QWidget *parent);

/// Le fond d'un panneau latéral : un peu plus sombre que la zone de travail, avec un filet de 1 point
/// (couleur Mid) du côté de celle-ci
class FondLateral : public QWidget {
public:
    /// `filet_a_droite` : la zone de travail est à droite du panneau
    explicit FondLateral(QWidget *parent, bool filet_a_droite = true);
    /// après un changement de thème (voir `recolore`)
    void suit_le_theme();

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;

private:
    bool m_filet_a_droite;
    bool m_theme_suivi = false;
};

/// Le défilement d'un panneau latéral : en hauteur seulement, et jamais plus étroit que son contenu
/// (sinon les textes seraient rognés à droite, sans barre de défilement horizontale). Son fond est
/// transparent : celui du panneau.
class DefilementLateral : public QScrollArea {
public:
    explicit DefilementLateral(QWidget *contenu, QWidget *parent);
    QSize minimumSizeHint() const override;

protected:
    bool eventFilter(QObject *objet, QEvent *event) override;
};

/// un panneau latéral : `contenu` sur le fond des panneaux, qui défile en hauteur si la fenêtre est
/// trop basse
FondLateral *panneau_lateral(QWidget *contenu, QWidget *parent);

/// Sépare un panneau latéral (à gauche) de la zone de travail : la séparation se déplace, et le
/// panneau reçoit sa largeur de départ quand il s'affiche pour la première fois
class SeparationLaterale : public QSplitter {
public:
    SeparationLaterale(QWidget *panneau, QWidget *zone, QWidget *parent);

protected:
    void showEvent(QShowEvent *event) override;

private:
    bool m_dimensionnee = false;
};

} // namespace dessin
