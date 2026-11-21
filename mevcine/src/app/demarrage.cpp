#include "demarrage.h"

#include "a_propos.h"
#include "i18n.h"

#include <QApplication>
#include <QKeyEvent>

#include <algorithm>

namespace {

/// durée de l'écran de démarrage, puis de son effacement, en secondes
constexpr double DUREE = 2.0;
constexpr double EFFACEMENT = 0.5;

} // namespace

EcranDemarrage::EcranDemarrage(QWidget *parent)
    : QWidget(parent)
    , m_presentation{a_propos::icone(), a_propos::NOM, trad("Étude cinématique de vidéos"),
                     trad("Version {}", a_propos::VERSION), a_propos::AUTEUR}
{
    setGeometry(parent->rect());
    raise();
    show();
    m_horloge.start();
    connect(&m_minuterie, &QTimer::timeout, this, [this] {
        if (ecoule() >= DUREE + EFFACEMENT)
            termine();
        else
            update();
    });
    m_minuterie.start(16);
    // un clic ou une touche, n'importe où, fait disparaître l'écran
    qApp->installEventFilter(this);
}

double EcranDemarrage::ecoule() const
{
    return m_horloge.elapsed() / 1000.0;
}

void EcranDemarrage::termine()
{
    m_minuterie.stop();
    qApp->removeEventFilter(this);
    hide();
    deleteLater();
}

bool EcranDemarrage::eventFilter(QObject *objet, QEvent *event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride:
        // les raccourcis ne servent pas tant que l'écran est là
        event->accept();
        return true;
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
        termine();
        return true;
    default: return QWidget::eventFilter(objet, event);
    }
}

void EcranDemarrage::mousePressEvent(QMouseEvent *)
{
    termine();
}

void EcranDemarrage::paintEvent(QPaintEvent *)
{
    const double t = ecoule();
    const double opacite = t > DUREE ? std::clamp(1.0 - (t - DUREE) / EFFACEMENT, 0.0, 1.0) : 1.0;
    m_presentation.dessine(*this, opacite);
}
