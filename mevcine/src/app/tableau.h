// Le tableau des mesures, commun aux deux modes : une ligne par image pointée, avec la date, les
// positions dans le repère choisi, et un bouton pour refaire la mesure.

#pragma once

#include "calculs.h"
#include "i18n.h"

#include <QLabel>
#include <QTableWidget>

#include <optional>

class App;

class TableauMesures : public QWidget {
    Q_OBJECT

public:
    TableauMesures(App &app, QWidget *parent);
    void maj();
    QTableWidget *table() const { return m_table; }
    /// Colonnes des mesures élargies à toute la largeur du tableau (onglet Coordonnées), valeurs
    /// centrées ; sinon, à la largeur de leur contenu (panneau du mode simplifié)
    void etire_colonnes(bool oui) { m_etire = oui; }

private:
    App &m_app;
    QLabel *m_vide;
    QTableWidget *m_table;
    /// le tableau affiché, et sa langue (en-têtes, bulles d'aide)
    Tableau m_affiche;
    std::optional<Langue> m_langue;
    bool m_etire = false;
};
