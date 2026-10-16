// Les pages affichées tant qu'aucune vidéo n'est ouverte, épurées comme dans tous les logiciels de la
// suite : le logo, le nom, un sous-titre, deux boutons (ou deux cartes) côte à côte, une ligne
// discrète. L'écran d'accueil fait choisir le mode de travail ; l'écran suivant, ouvrir une vidéo.
// La langue, le thème et la notice sont à droite de la barre des menus.

#pragma once

#include <QAbstractButton>
#include <QWidget>

class App;
class QLabel;
class QPushButton;

/// une carte cliquable : un titre et une ligne
class Carte : public QAbstractButton {
    Q_OBJECT

public:
    explicit Carte(QWidget *parent);
    void textes(const QString &titre, const QString &ligne);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void changeEvent(QEvent *event) override;

private:
    QString m_ligne;
};

/// l'écran d'accueil : le choix du mode
class PageAccueil : public QWidget {
    Q_OBJECT

public:
    PageAccueil(App &app, QWidget *parent);
    void maj();

private:
    App &m_app;
    QLabel *m_sous_titre, *m_remarque;
    Carte *m_simplifie, *m_expert;
};

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
