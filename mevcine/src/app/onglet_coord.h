// Onglet des coordonnées du mode Expert : le tableau des mesures, sur toute la place, et au-dessus une
// ligne pour les exporter (copie pour Python, fichier CSV, projet), comme les réglages du grapheur.
// MeVCine ne calcule pas d'énergie : c'est le travail de l'élève.

#pragma once

#include <QWidget>

class App;
class QLabel;
class QPushButton;
class TableauMesures;

class OngletCoord : public QWidget {
    Q_OBJECT

public:
    OngletCoord(App &app, QWidget *parent);
    void maj();
    TableauMesures *tableau() const { return m_tableau; }

private:
    App &m_app;
    QLabel *m_titre_export, *m_copie, *m_titre_tableau;
    QPushButton *m_copier, *m_csv, *m_projet;
    TableauMesures *m_tableau;
};
