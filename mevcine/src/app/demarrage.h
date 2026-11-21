// L'écran de démarrage s'efface après deux secondes. Un clic ou une touche le ferme aussitôt.

#pragma once

#include "presentation_demarrage.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class EcranDemarrage : public QWidget {
    Q_OBJECT

public:
    explicit EcranDemarrage(QWidget *parent);
    /// fait disparaître l'écran aussitôt
    void termine();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    bool eventFilter(QObject *objet, QEvent *event) override;

private:
    double ecoule() const;

    mevsuite::PresentationDemarrage m_presentation;
    QElapsedTimer m_horloge;
    QTimer m_minuterie;
};
