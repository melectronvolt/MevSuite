#include "demarrage.h"

#include "a_propos.h"
#include "i18n.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// durée de l'écran de démarrage, puis de son effacement, en secondes
constexpr double DUREE = 2.0;
constexpr double EFFACEMENT = 0.5;

/// le fond, propre à MeVCine : le bleu nuit de son icône
const QColor FOND(0x1d, 0x35, 0x57);

} // namespace

EcranDemarrage::EcranDemarrage(QWidget *parent)
    : QWidget(parent)
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
    // la même mise en page que dans les autres logiciels de la suite
    const double t = ecoule();
    const double opacite = t > DUREE ? std::clamp(1.0 - (t - DUREE) / EFFACEMENT, 0.0, 1.0) : 1.0;
    QPainter p(this);
    p.setOpacity(opacite);
    p.fillRect(rect(), FOND);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF r(rect());
    const QPointF c = r.center();
    const double cote = std::min(r.height() * 0.3, 180.0);
    const QPixmap logo = a_propos::icone();
    p.drawPixmap(QRectF(c.x() - cote / 2, c.y() - 150.0 - cote / 2, cote, cote), logo, logo.rect());
    const auto texte = [&](QPointF pos, const QString &s, double taille, const QColor &couleur) {
        QFont f = font();
        f.setPixelSize(static_cast<int>(std::lround(taille)));
        f.setBold(false);
        p.setFont(f);
        p.setPen(couleur);
        p.drawText(QRectF(pos.x() - r.width() / 2, pos.y() - taille, r.width(), 2 * taille), Qt::AlignCenter, s);
    };
    texte(c + QPointF(0, -20), a_propos::NOM, 64, Qt::white);
    texte(c + QPointF(0, 34), trad("Étude cinématique de vidéos"), 18, QColor(190, 190, 190));
    texte(c + QPointF(0, 100), a_propos::AUTEUR, 28, Qt::white);
    texte(c + QPointF(0, 138), a_propos::COURRIEL, 16, QColor(160, 160, 160));
    texte(QPointF(c.x(), r.bottom() - 24), trad("version {}", a_propos::VERSION), 13, QColor(120, 120, 120));
}
