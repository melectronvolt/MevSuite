#include "plein_ecran.h"

#include "app.h"
#include "dessin.h"
#include "i18n.h"
#include "onglet_pointage.h"

#include <QAction>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/// écart entre le cadre de la loupe et les bords de l'image, en points d'écran
constexpr int MARGE_CADRE = 12;
/// distance à laquelle la souris fait passer le cadre de l'autre côté
constexpr int ECART_SOURIS = 40;

} // namespace

PleinEcran::PleinEcran(App &app, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_app(app)
{
    setObjectName(u"fenetre_plein_ecran"_s);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    m_zone = new ZoneVideo(app, this);
    m_zone->installEventFilter(this);
    l->addWidget(m_zone, 1);
    l->addWidget(dessin::trait(this));
    auto *bas = new QHBoxLayout();
    bas->setContentsMargins(0, 0, 8, 0);
    m_controles = new ControlesImage(app, this, true);
    m_arreter = new QPushButton(this);
    dessin::demi_gras(m_arreter);
    connect(m_arreter, &QPushButton::clicked, this, [this] {
        m_app.arrete_pointage();
        m_app.rafraichit();
    });
    bas->addWidget(m_controles, 1);
    bas->addWidget(m_arreter);
    l->addLayout(bas);

    // la loupe et la consigne, dans un cadre posé sur l'image
    m_cadre = new QFrame(this);
    m_cadre->setObjectName(u"cadre_loupe"_s);
    m_cadre->setFrameShape(QFrame::StyledPanel);
    m_cadre->setAutoFillBackground(true);
    auto *lc = new QVBoxLayout(m_cadre);
    lc->setContentsMargins(10, 8, 10, 8);
    m_loupe = new PanneauLoupe(app, m_cadre);
    m_consigne = new QLabel(m_cadre);
    m_consigne->setWordWrap(true);
    lc->addWidget(m_loupe);
    lc->addWidget(m_consigne);
    m_cadre->raise();

    // Échap : renoncer à la mesure à refaire, sinon revenir à la fenêtre ; F11 : y revenir. Les autres
    // raccourcis (images, annuler, rétablir, agrandissement) sont ceux de la fenêtre.
    const auto raccourci = [this](const QKeySequence &touches, auto fonction) {
        auto *a = new QAction(this);
        a->setShortcut(touches);
        connect(a, &QAction::triggered, this, [this, fonction] {
            fonction();
            m_app.rafraichit();
        });
        addAction(a);
    };
    raccourci(QKeySequence(Qt::Key_Escape), [this] {
        if (m_app.refait)
            m_app.annule_refait();
        else
            m_app.quitte_plein_ecran();
    });
    raccourci(QKeySequence(Qt::Key_F11), [this] { m_app.quitte_plein_ecran(); });
    addActions(app.raccourcis_plein_ecran());
}

void PleinEcran::maj()
{
    const App &a = m_app;
    setWindowTitle(a.windowTitle());
    m_loupe->maj();
    // la barre d'état n'est pas là : la consigne est sous la loupe
    m_consigne->setText(a.statut.isEmpty() ? a.message_par_defaut() : a.statut);
    m_controles->maj();
    if (a.simplifie()) {
        m_arreter->setText(trad("Terminer le pointage ▶"));
        m_arreter->setToolTip(trad("Revenir à la fenêtre, à l'étape du tableau des mesures"));
        m_arreter->setEnabled(a.etape_faite(Etape::Pointage) && !a.refait);
    } else {
        m_arreter->setText(trad("⏹ Arrêter le pointage"));
        m_arreter->setToolTip(trad("Finir le pointage : un clic sur l'image ne pointe plus, et les coordonnées s'affichent"));
        m_arreter->setEnabled(!a.refait);
    }
    m_zone->update();
    place_cadre();
}

void PleinEcran::maj_souris()
{
    m_loupe->maj();
    m_zone->update();
}

void PleinEcran::place_cadre()
{
    // la largeur de la loupe et de ses coordonnées ; la consigne passe à la ligne
    const int largeur = m_loupe->sizeHint().width() + 20;
    QLayout *l = m_cadre->layout();
    const int hauteur = l->hasHeightForWidth() ? l->totalHeightForWidth(largeur) : m_cadre->sizeHint().height();
    const QRect z = m_zone->geometry();
    const int x = m_a_droite ? z.right() + 1 - MARGE_CADRE - largeur : z.left() + MARGE_CADRE;
    m_cadre->setGeometry(x, z.top() + MARGE_CADRE, largeur, hauteur);
    m_cadre->raise();
}

bool PleinEcran::eventFilter(QObject *objet, QEvent *event)
{
    // la souris s'approche du cadre : il passe de l'autre côté, pour ne jamais cacher le point visé
    if (objet == m_zone && event->type() == QEvent::MouseMove) {
        const QPoint pos = m_zone->mapTo(this, static_cast<QMouseEvent *>(event)->position().toPoint());
        if (m_cadre->geometry().adjusted(-ECART_SOURIS, -ECART_SOURIS, ECART_SOURIS, ECART_SOURIS).contains(pos)) {
            m_a_droite = !m_a_droite;
            place_cadre();
        }
    }
    return QWidget::eventFilter(objet, event);
}

void PleinEcran::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    place_cadre();
}

void PleinEcran::closeEvent(QCloseEvent *event)
{
    // fermé par le gestionnaire de fenêtres (Alt+F4…) : retour à la fenêtre, le pointage continue
    QWidget::closeEvent(event);
    QTimer::singleShot(0, &m_app, [app = &m_app] {
        app->activateWindow();
        app->rafraichit();
    });
}
