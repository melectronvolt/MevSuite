// Écran d'accueil, fenêtre « À propos » et logo (règle graduée et segment de mesure).

#pragma once

#include "i18n.h"
#include "settings.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QPainter;

namespace about {

inline const QString NAME = QStringLiteral("MeVPix");
inline const QString AUTHOR = QStringLiteral("Rémi MEVAERE");
inline const QString EMAIL = QStringLiteral("suite@sciences-physiques.net");
inline const QString VERSION = QStringLiteral(MEVPIX_VERSION);

/// Logo : un objet sur une grille de pixels, mesuré par un segment à poignées (comme dans le
/// logiciel), au-dessus d'une règle graduée. `ink` : couleur du trait, adaptée au fond.
void paintLogo(QPainter &painter, const QRectF &rect, const QColor &ink);

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
    void skip();
    double elapsed() const;

    const Tr &m_tr;
    QElapsedTimer m_clock;
    /// Début de l'affichage, en secondes depuis `m_clock` (avancé par `skip`).
    double m_start = 0.0;
    QTimer m_timer;
};

/// Fenêtre « À propos » : auteur, logiciels libres utilisés et leurs licences, fichiers de réglages.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    AboutDialog(const Tr &tr, const SettingsFiles &settings, QWidget *parent);
};
