#include "libelles.h"

#include <QApplication>
#include <QLabel>

QLabel *etiquette_a_la_ligne(QWidget *parent)
{
    auto *label = new QLabel(parent);
    // Les noms de fichiers peuvent contenir des signes < et >.
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

void definit_couleur_texte(QWidget *widget, const QColor &color)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText, color);
    palette.setColor(QPalette::ButtonText, color);
    widget->setPalette(palette);
}

/// Texte secondaire, plus discret.
QLabel *etiquette_discrete(QWidget *parent)
{
    QLabel *label = etiquette_a_la_ligne(parent);
    definit_couleur_texte(label, QApplication::palette().color(QPalette::PlaceholderText));
    return label;
}

QFrame *separateur(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}
