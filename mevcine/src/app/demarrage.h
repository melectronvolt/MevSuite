// L'écran de démarrage : MeVCine et son auteur, sur le fond bleu nuit de l'icône, pendant deux
// secondes, puis il s'efface. Un clic ou une touche le fait disparaître aussitôt.

#pragma once

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

    QElapsedTimer m_horloge;
    QTimer m_minuterie;
};
