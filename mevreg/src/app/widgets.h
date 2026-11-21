// Petits composants communs aux fenêtres : saisie de nombres, choix d'une grandeur, couleur,
// disposition qui passe à la ligne, glisser-déposer des grandeurs.

#pragma once

#include "core/format.h"
#include "core/plotspec.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QMimeData>
#include <QScrollArea>
#include <QToolButton>

#include <optional>

class Store;

/// Saisie d'un nombre : virgule ou point décimal, notation scientifique, chiffres significatifs.
/// Molette et flèches : ±1 % de la valeur (ou du pas donné).
class NumberEdit : public QAbstractSpinBox {
    Q_OBJECT

public:
    explicit NumberEdit(QWidget *parent = nullptr);

    double valeur() const { return m_value; }
    void definit_valeur(double v);
    void definit_style(NumberStyle s);
    void definit_pas(std::optional<double> step) { m_step = step; }
    void definit_plage(double min, double max);
    void definit_suffixe(const QString &s);

    void stepBy(int steps) override;
    QValidator::State validate(QString &input, int &pos) const override;

Q_SIGNALS:
    /// Valeur modifiée par l'utilisateur (saisie validée, molette, flèches).
    void valeur_modifiee(double v);

protected:
    StepEnabled stepEnabled() const override;

private:
    void valide();
    QString texte(double v) const;

    double m_value = 0.0;
    NumberStyle m_style;
    std::optional<double> m_step;
    double m_min = -INFINITY, m_max = INFINITY;
    QString m_suffix;
};

/// Liste déroulante des grandeurs du document.
class QuantityCombo : public QComboBox {
    Q_OBJECT

public:
    QuantityCombo(Store &store, bool allowNone = false, QWidget *parent = nullptr);

    std::optional<QId> actuel() const;
    void definit_actuel(std::optional<QId> id);
    /// Remet la liste à jour (grandeurs ajoutées, renommées).
    void rafraichit();

Q_SIGNALS:
    void choisi(std::optional<QId> id);

private:
    Store &m_store;
    bool m_allowNone;
};

/// Bouton à menu : la flèche « ▾ » est dans le texte, celle de Qt (minuscule, collée au texte)
/// n'est pas dessinée.
class MenuToolButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent *event) override;
};

/// Bouton plat à menu, icône et texte suivi de « ▾ ».
MenuToolButton *bouton_menu(const char *icon, const QString &text, const QString &tip, QMenu *menu, QWidget *parent = nullptr);

/// Bouton de couleur : pastille, choix dans une palette ou nuancier complet.
class ColorButton : public QToolButton {
    Q_OBJECT

public:
    explicit ColorButton(QWidget *parent = nullptr);

    Color couleur() const { return m_color; }
    void definit_couleur(Color c);

Q_SIGNALS:
    void couleur_choisie(Color c);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Color m_color = PALETTE[0];
};

/// Disposition de gauche à droite qui passe à la ligne quand la place manque.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int spacing = 4);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override { return static_cast<int>(m_items.size()); }
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

private:
    int arrange(const QRect &rect, bool apply) const;
    std::vector<QLayoutItem *> m_items;
    int m_spacing;
};

/// Texte discret, sur plusieurs lignes si besoin (explications, astuces).
QLabel *etiquette_attenuee(const QString &text, QWidget *parent = nullptr);
/// Titre de section.
QLabel *titre_section(const QString &text, QWidget *parent = nullptr);
/// Bouton plat avec icône (et texte facultatif) pour les barres d'outils des fenêtres.
QToolButton *bouton_outil(const char *icon, const QString &text, const QString &tip, QWidget *parent = nullptr, bool showText = true);

namespace dnd {
/// Type des données glissées : une ou plusieurs grandeurs (colonnes d'un tableur), dans l'ordre.
inline constexpr char MIME[] = "application/x-mevreg-quantity";
QMimeData *mime_grandeur(QId id);
QMimeData *mime_grandeurs(const std::vector<QId> &ids);
/// Première grandeur glissée.
std::optional<QId> grandeur_de(const QMimeData *mime);
/// Toutes les grandeurs glissées (vide si ce n'en sont pas).
std::vector<QId> grandeurs_de(const QMimeData *mime);
} // namespace dnd

/// Panneaux reconstruits à chaque modification de l'espace de travail (réglages d'un graphe,
/// modélisation) : la reconstruction attend la fin d'une saisie et rend ensuite le focus au
/// champ équivalent de la nouvelle page.
namespace live {
/// Saisie non validée dans la page, ou liste déroulante, menu ouvert.
bool occupe(const QWidget *page);
/// Les champs de la page oublient leur état « modifié » une fois la saisie validée.
void suit(QWidget *page);

struct Focus {
    QByteArray cls;
    int index = -1;
    int cursor = -1;
};
Focus retient(const QWidget *page);
void restaure(QWidget *page, const Focus &f);
} // namespace live

/// Zone défilante verticale d'un panneau : jamais plus étroite que son contenu (pas de défilement
/// horizontal, rien de rogné, quel que soit l'agrandissement de l'interface).
class PanelScroll : public QScrollArea {
public:
    explicit PanelScroll(QWidget *parent = nullptr);
    QSize minimumSizeHint() const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

/// Remet la zone défilante à sa position une fois la nouvelle page mise en place.
void garde_defilement(QScrollArea *area, int pos);
