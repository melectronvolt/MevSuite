// Présentation commune des textes de la fenêtre et du panneau de mesures.
#pragma once

class QColor;
class QFrame;
class QLabel;
class QWidget;

QLabel *etiquette_a_la_ligne(QWidget *parent);
QLabel *etiquette_discrete(QWidget *parent);
void definit_couleur_texte(QWidget *widget, const QColor &color);
QFrame *separateur(QWidget *parent);
