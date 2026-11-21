// Écran d'accueil, fenêtre « À propos » et icône du programme.

#pragma once

#include "i18n.h"
#include "presentation_demarrage.h"
#include "settings.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

namespace about {

inline const QString NAME = QStringLiteral("MeVSono");
inline const QString AUTHOR = QStringLiteral("Rémi MEVAERE");
inline const QString EMAIL = QStringLiteral("suite@sciences-physiques.net");
inline const QString VERSION = QStringLiteral(MEVSONO_VERSION);

/// Icône du programme (256 × 256), aussi affichée par l'écran d'accueil et « À propos ».
QPixmap icone();

} // namespace about

/// Écran d'accueil : affiché au démarrage par-dessus la fenêtre, il s'efface seul après quelques
/// secondes, ou dès un clic ou une touche.
class Splash : public QWidget {
    Q_OBJECT

public:
    Splash(const Tr &tr, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Passe directement à l'effacement.
    void saute();
    double duree_ecoulee() const;

    mevsuite::PresentationDemarrage m_presentation;
    QElapsedTimer m_clock;
    /// Début de l'affichage, en secondes depuis `m_clock` (avancé par `saute`).
    double m_start = 0.0;
    QTimer m_timer;
};

/// Fenêtre « À propos » : auteur, licence, logiciels libres utilisés et leurs licences, fichiers de
/// réglages.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    AboutDialog(const Tr &tr, const SettingsFiles &settings, QWidget *parent);
};
