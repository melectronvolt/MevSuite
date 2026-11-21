// Écran d'accueil, fenêtre « À propos » et logo (règle graduée et segment de mesure).

#pragma once

#include "i18n.h"
#include "presentation_demarrage.h"
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
void dessine_logo(QPainter &painter, const QRectF &rect, const QColor &ink);

} // namespace about

/// Écran d'accueil : affiché au démarrage par-dessus la fenêtre, il s'efface seul après quelques
/// secondes, ou dès un clic ou une touche.
class EcranDemarrage : public QWidget {
    Q_OBJECT

public:
    EcranDemarrage(const Textes &tr, QWidget *parent);

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

/// Fenêtre « À propos » : auteur, logiciels libres utilisés et leurs licences, fichiers de réglages.
class DialogueAPropos : public QDialog {
    Q_OBJECT

public:
    DialogueAPropos(const Textes &tr, const FichiersReglages &settings, QWidget *parent);
};
