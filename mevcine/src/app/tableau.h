// Le tableau des mesures, commun aux deux modes : une ligne par image pointée, avec la date, les
// positions dans le repère choisi (ou le numéro seul), et les actions pour refaire ou supprimer.

#pragma once

#include "calculs.h"
#include "i18n.h"

#include <QLabel>
#include <QPointer>
#include <QTableWidget>

#include <optional>

class App;
class ZoneVideo;
class QToolButton;
class QPushButton;
class QShortcut;

class TableauMesures : public QWidget {
    Q_OBJECT

public:
    enum class Presentation { Guide, Coordonnees, Numeros };
    TableauMesures(App &app, QWidget *parent, Presentation presentation, ZoneVideo *zone = nullptr);
    void actualise();
    QTableWidget *table() const { return m_table; }
    int largeur_necessaire() const;

private:
    bool eventFilter(QObject *objet, QEvent *event) override;
    void efface_apercu();
    void montre_apercu();
    void etire_colonnes();
    void survole_cellule(const QModelIndex &index);
    QToolButton *bouton_action(int ligne, int colonne, bool refaire);
    void supprime_selection();
    void supprime(const std::vector<Pointage::Cellule> &cellules);
    void ajoute_ligne(std::vector<Pointage::Cellule> &cellules, int ligne) const;
    QTimer *m_delai;
    QPointer<ZoneVideo> m_zone;
    QShortcut *m_supprimer;
    QModelIndex m_cellule;
    App &m_app;
    QLabel *m_vide;
    QPushButton *m_verifier, *m_confirmer, *m_annuler;
    bool m_apercu_clavier = false;
    QTableWidget *m_table;
    /// le tableau affiché, et sa langue (en-têtes, bulles d'aide)
    Tableau m_affiche;
    std::optional<Langue> m_langue;
    bool m_largeurs_a_revoir = true;
    std::vector<int> m_largeurs_donnees;
    Presentation m_presentation;
};
