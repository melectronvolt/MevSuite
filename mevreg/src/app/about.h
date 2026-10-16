// Écran de démarrage, fenêtre « À propos » (MeVReg, son auteur, sa licence et les composants
// libres qu'il utilise, avec leur licence) et icône du programme.

#pragma once

#include <QElapsedTimer>
#include <QIcon>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace about {

inline const QString NAME = QStringLiteral("MeVReg");
inline const QString AUTHOR = QStringLiteral("Rémi MEVAERE");
inline const QString EMAIL = QStringLiteral("suite@sciences-physiques.net");
inline const QString VERSION = QStringLiteral(MEVREG_VERSION);
/// Sous-titre de l'écran de démarrage, de la page d'accueil et de « À propos ».
inline const QString TAGLINE = QStringLiteral("Traitement des données des TP de physique-chimie");
/// Sous-titre de la fenêtre, tant qu'aucun document n'est commencé.
inline const QString SUBTITLE = QStringLiteral("traitement des données des TP");

/// Icône du programme (vectorielle), aussi affichée par la page d'accueil et « À propos ».
QIcon icon();
/// Fenêtre « À propos de MeVReg ».
void show(QWidget *parent);

} // namespace about

/// Écran de démarrage : affiché au lancement par-dessus la fenêtre, il s'efface seul après deux
/// secondes, ou dès un clic ou une touche.
class Splash : public QWidget {
    Q_OBJECT

public:
    explicit Splash(QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Passe directement à l'effacement.
    void skip();
    double elapsed() const;

    QElapsedTimer m_clock;
    /// Début de l'affichage, en secondes depuis `m_clock` (avancé par `skip`).
    double m_start = 0.0;
    QTimer m_timer;
};
