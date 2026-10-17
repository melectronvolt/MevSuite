// La page affichée tant qu'aucune vidéo n'est ouverte, épurée comme dans tous les logiciels de la
// suite : le logo, le nom, un sous-titre, le bouton d'ouverture, une ligne discrète. Le mode de
// travail se choisit dans le menu Mode ; la langue et le thème sont à droite de la barre des menus.

#pragma once

#include <QWidget>

class App;
class QLabel;
class QPushButton;

/// l'écran affiché tant qu'aucune vidéo n'est ouverte : ouvrir une vidéo
class PageSansVideo : public QWidget {
    Q_OBJECT

public:
    PageSansVideo(App &app, QWidget *parent);
    void maj();

private:
    App &m_app;
    QLabel *m_sous_titre, *m_glissez;
    QPushButton *m_ouvrir;
};
