// Le mode Simplifié : une procédure guidée en six étapes, pour un seul objet.
//
// 1. l'origine du repère, choisie d'un clic ;
// 2. l'orientation des axes, choisie parmi quatre repères dessinés ;
// 3. l'échelle : on sélectionne l'étalon sur l'image, puis on donne sa longueur réelle (en mm, cm
//    ou m) ;
// 4. le pointage, image par image ;
// 5. le tableau des mesures (t, x, y), avec un bouton pour refaire une mesure ;
// 6. l'export : copie pour Python ou fichier CSV.
//
// Les étapes 1 à 3 restent modifiables après le pointage : les positions sont gardées en pixels de
// la vidéo, et les mesures sont recalculées. Aux étapes 1 et 2, l'origine se déplace aussi en la
// faisant glisser.

#pragma once

#include <QAbstractButton>
#include <QString>
#include <QWidget>

#include <array>
#include <optional>

class App;
class ControlesImage;
class QComboBox;
class PanneauLoupe;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;
class TableauMesures;
class ZoneVideo;

enum class Etape { Origine, Axes, Echelle, Pointage, Tableau, Export };

inline constexpr std::array<Etape, 6> TOUTES_LES_ETAPES = {Etape::Origine, Etape::Axes,    Etape::Echelle,
                                                          Etape::Pointage, Etape::Tableau, Etape::Export};

/// numéro d'une étape, à partir de 1
inline int numero(Etape e)
{
    return static_cast<int>(e) + 1;
}
/// nom d'une étape, dans la langue de l'interface
QString nom(Etape e);
std::optional<Etape> suivante(Etape e);
std::optional<Etape> precedente(Etape e);

/// les quatre orientations possibles des axes : sens de x, sens de y
inline constexpr std::array<std::pair<int, int>, 4> ORIENTATIONS = {{{1, 1}, {-1, 1}, {1, -1}, {-1, -1}}};

/// le sens des axes, en mots : « x vers la droite », « y vers le haut »
std::pair<QString, QString> sens_en_mots(int sens_x, int sens_y);

struct Guide {
    Etape etape = Etape::Origine;
    /// l'élève a placé l'origine
    bool origine_choisie = false;
    /// l'élève a choisi l'orientation des axes (aucune n'est proposée d'office)
    bool axes_choisis = false;
    /// la longueur de l'étalon, telle qu'elle est saisie
    QString saisie_etalon;
    std::optional<QString> erreur_etalon;
    /// donner le focus au champ de la longueur (l'étalon vient d'être tracé)
    bool focus_saisie = false;

    /// on ne pointe qu'à l'étape du pointage, ou pour refaire une mesure depuis le tableau
    bool pointage_permis(bool refait) const
    {
        return etape == Etape::Pointage || (refait && etape >= Etape::Pointage);
    }

    /// Reprise d'un travail (projet rouvert, passage du mode Expert au mode Simplifié) : le repère
    /// est déjà choisi
    void reprend(bool a_des_mesures, bool echelle)
    {
        origine_choisie = true;
        axes_choisis = true;
        etape = !echelle ? Etape::Echelle : a_des_mesures ? Etape::Tableau : Etape::Pointage;
    }
};

/// Bouton de choix d'une orientation des axes : un petit repère dessiné et sa description, sur deux
/// lignes. Son nom accessible est la description (« x vers la droite, y vers le haut »).
class BoutonAxes : public QAbstractButton {
    Q_OBJECT

public:
    BoutonAxes(int sens_x, int sens_y, QWidget *parent);
    QSize sizeHint() const override;
    int sens_x() const { return m_sx; }
    int sens_y() const { return m_sy; }
    /// la description, dans la langue de l'interface
    QString description() const;

protected:
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *event) override;

private:
    int m_sx, m_sy;
};

/// L'interface du mode Simplifié : le bandeau des étapes ; à gauche, la loupe et les coordonnées du
/// point visé, les consignes de l'étape en cours et les boutons Précédent et Suivant ; l'image de la
/// vidéo ; le tableau des mesures (à droite, à partir du pointage)
class PageGuide : public QWidget {
    Q_OBJECT

public:
    PageGuide(App &app, QWidget *parent);
    void maj();
    /// après un mouvement de la souris sur l'image
    void maj_souris();
    ZoneVideo *zone() const { return m_zone; }
    TableauMesures *tableau() const { return m_tableau; }
    QLineEdit *saisie_etalon() const { return m_saisie; }
    QComboBox *unite_etalon() const { return m_e_unite; }

private:
    QWidget *etape_origine();
    QWidget *etape_axes();
    QWidget *etape_echelle();
    QWidget *etape_pointage();
    QWidget *etape_tableau();
    QWidget *etape_export();

    App &m_app;
    std::array<QPushButton *, 6> m_etapes{};
    QWidget *m_refait;
    QLabel *m_refait_texte;
    QPushButton *m_refait_annuler;
    QStackedWidget *m_pages;
    QPushButton *m_precedent, *m_suivant;
    PanneauLoupe *m_loupe;
    ZoneVideo *m_zone;
    ControlesImage *m_controles_image;
    QWidget *m_droite;
    QLabel *m_titre_tableau;
    TableauMesures *m_tableau;

    // 1. origine
    QLabel *m_o_titre, *m_o_consigne, *m_o_reussite, *m_o_remarque, *m_o_couchee;
    QWidget *m_o_rotation;
    QPushButton *m_o_gauche, *m_o_droite;
    // 2. axes
    QLabel *m_a_titre, *m_a_consigne, *m_a_reussite, *m_a_remarque;
    std::array<BoutonAxes *, 4> m_a_boutons{};
    // 3. échelle
    QLabel *m_e_titre, *m_e_consigne_a, *m_e_attention, *m_e_consigne_b, *m_e_remarque, *m_e_erreur, *m_e_etat;
    QPushButton *m_e_annuler, *m_e_selection, *m_e_valider;
    QWidget *m_e_ligne;
    QLineEdit *m_saisie;
    QComboBox *m_e_unite;
    // 4. pointage
    QLabel *m_p_titre, *m_p_consigne1, *m_p_consigne2, *m_p_remarque, *m_p_nombre, *m_p_derniere, *m_p_ralenti_texte,
        *m_p_ips, *m_p_cadence;
    QPushButton *m_p_annuler;
    QToolButton *m_p_ralenti;
    QWidget *m_p_ralenti_contenu;
    QLineEdit *m_p_saisie_ips;
    // 5. tableau
    QLabel *m_t_titre, *m_t_consigne1, *m_t_consigne2, *m_t_remarque;
    QPushButton *m_t_reprendre;
    // 6. export
    QLabel *m_x_titre, *m_x_consigne1, *m_x_reussite, *m_x_consigne2, *m_x_apercu;
    QPushButton *m_x_copier, *m_x_csv, *m_x_projet;
    QPlainTextEdit *m_x_texte;
};
