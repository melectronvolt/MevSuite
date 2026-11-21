// Barre d'état et agrandissement des textes et des graphiques.
#include "app.h"

#include "libelles.h"
#include "signalplot.h"
#include "spectrumplot.h"
#include "style.h"

#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QStatusBar>
#include <QTabBar>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Information permanente de la barre d’état, séparée du message par un filet vertical.
class StatusInfo : public QLabel {
public:
    explicit StatusInfo(QWidget *parent)
        : QLabel(parent)
    {
        setTextFormat(Qt::PlainText);
        setContentsMargins(11, 0, 10, 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QLabel::paintEvent(event);
        QPainter(this).fillRect(QRect(0, 0, 1, height()), QApplication::palette().color(QPalette::Mid));
    }
};

} // namespace

void MainWindow::construit_barre_etat()
{
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);
    m_statusIcon = new QLabel(bar);
    m_statusIcon->setContentsMargins(6, 0, 0, 0);
    // Un message long (chemin du fichier, formats acceptés…) passe à la ligne.
    m_statusLabel = etiquette_a_la_ligne(bar);
    m_statusLabel->setObjectName(u"statusMessage"_s);
    m_statusLabel->setContentsMargins(0, 4, 0, 4);
    m_statusLabel->installEventFilter(this);
    m_busy = new QProgressBar(bar);
    m_busy->setRange(0, 0);
    m_busy->setMaximumWidth(80);
    m_busy->setTextVisible(false);
    m_busy->hide();
    m_soundStatus = new StatusInfo(bar);
    m_soundStatus->setObjectName(u"soundStatus"_s);
    m_modeStatus = new StatusInfo(bar);
    m_modeStatus->setObjectName(u"modeStatus"_s);
    bar->addWidget(m_statusIcon);
    bar->addWidget(m_busy);
    bar->addWidget(m_statusLabel, 1);
    bar->addPermanentWidget(m_soundStatus);
    bar->addPermanentWidget(m_modeStatus);
}

void MainWindow::rafraichit_infos_etat()
{
    const Tr &t = tr();
    m_modeStatus->setText(m_session.params.expert ? t.modeExpertName : t.modeSimpleName);
    m_modeStatus->setToolTip(t.modeStatusTip);
    m_soundStatus->setVisible(m_session.loaded.has_value());
    if (!m_session.loaded)
        return;
    // nom du son (raccourci au milieu s'il est long) et ses caractéristiques
    const audio::Audio &sound = m_session.loaded->audio;
    const QString full = u"%1 · %2"_s.arg(sound.name, infos_son(t, sound));
    m_soundStatus->setText(m_soundStatus->fontMetrics().elidedText(full, Qt::ElideMiddle, std::max(width() / 3, 120)));
    m_soundStatus->setToolTip(u"<qt>%1</qt>"_s.arg(full.toHtmlEscaped()));
}

void MainWindow::affiche_statut(const QString &text, icons::Status kind)
{
    m_status = {text, kind};
    m_statusLabel->setText(text);
    ajuste_barre_etat();
    const int side = m_statusLabel->fontMetrics().height();
    m_statusIcon->setPixmap(icons::etat(kind).pixmap(QSize(side, side), devicePixelRatioF()));
    m_statusIcon->setVisible(!text.isEmpty());
    const QPalette palette = QApplication::palette();
    QColor color = palette.color(QPalette::WindowText);
    switch (kind) {
    case icons::Status::Info: break;
    case icons::Status::Success: color = couleur_reussite(palette); break;
    case icons::Status::Warning: color = couleur_attention(palette); break;
    case icons::Status::Error: color = couleur_erreur(palette); break;
    }
    definit_couleur_texte(m_statusLabel, color);
}

void MainWindow::ajuste_barre_etat()
{
    // Hauteur donnée à la barre : celle que QLabel estime coupe le texte dès une vingtaine de
    // caractères (et la barre changeait de hauteur d'un message à l'autre). Ici, une ligne si le
    // message tient dans la largeur qu'il occupe, plusieurs sinon.
    const int line = std::max(m_statusLabel->heightForWidth(QWIDGETSIZE_MAX), m_modeStatus->sizeHint().height());
    statusBar()->setFixedHeight(std::max(line, m_statusLabel->heightForWidth(m_statusLabel->width())) + 6);
}

void MainWindow::style_barre_etat()
{
    // Bande distincte du reste de la fenêtre : fond un peu plus foncé, filet au-dessus ; son et mode
    // séparés du message par un filet vertical. Le son, plus discret : couleur donnée ici, la feuille
    // de style l'emportant sur la palette des étiquettes qu'elle décrit.
    // Couleurs avec leur transparence : en thème clair, PlaceholderText est un noir translucide, que
    // `QColor::name()` donnerait noir.
    const auto css = [](const QColor &c) {
        return u"rgba(%1, %2, %3, %4%)"_s.arg(c.red()).arg(c.green()).arg(c.blue()).arg(std::lround(c.alphaF() * 100));
    };
    const QPalette palette = QApplication::palette();
    const QColor background = palette.color(QPalette::Window).darker(est_sombre(palette) ? 135 : 107);
    statusBar()->setStyleSheet(u"QStatusBar { background: %1; border-top: 1px solid %2; }"
                               u"QStatusBar::item { border: none; }"
                               u"QLabel#soundStatus { color: %3; }"_s.arg(css(background),
                                                                          css(palette.color(QPalette::Mid)),
                                                                          css(palette.color(QPalette::PlaceholderText))));
    affiche_statut(m_status);
}

double MainWindow::agrandissement_max() const
{
    // Le maximum dépend de la largeur de la fenêtre : il reste toujours au moins la largeur minimale
    // pour les panneaux et les graphiques, même sur un écran 1366 × 768.
    if (width() < MIN_WINDOW.width())
        return UI_ZOOM_MAX; // fenêtre pas encore mesurée
    return std::clamp(double(width()) / MIN_WINDOW.width(), 1.0, UI_ZOOM_MAX);
}

void MainWindow::change_agrandissement(double zoom)
{
    // L'agrandissement demandé est retenu ; celui qui s'affiche est borné par la largeur de la
    // fenêtre, et revient à la demande quand la fenêtre s'élargit (au démarrage, la fenêtre n'a pas
    // encore sa taille : l'agrandissement enregistré se retrouve au premier redimensionnement).
    m_uiZoomWanted = std::clamp(zoom, UI_ZOOM_MIN, UI_ZOOM_MAX);
    applique_agrandissement();
}

void MainWindow::applique_agrandissement()
{
    m_uiZoom = std::clamp(m_uiZoomWanted, UI_ZOOM_MIN, agrandissement_max());
    QFont font = m_baseFont;
    if (m_baseFont.pointSizeF() > 0.0)
        font.setPointSizeF(m_baseFont.pointSizeF() * m_uiZoom);
    else
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(m_baseFont.pixelSize() * m_uiZoom))));
    QApplication::setFont(font);
    applique_polices();
    m_signal->change_echelle_interface(m_uiZoom);
    m_spectrum->change_echelle_interface(m_uiZoom);
    dimensionne_coin_des_menus();
    // pictogrammes des onglets : de la hauteur d'une ligne de texte
    const int tabIcon = QFontMetrics(QApplication::font()).height();
    m_tabs->setIconSize(QSize(tabIcon, tabIcon));
    dimensionne_boutons_accueil();
    // les étiquettes décrites par la feuille de style de la barre d'état ne prennent la nouvelle police
    // qu'avec elle (`style_barre_etat` réaffiche aussi le message, à sa hauteur)
    style_barre_etat();
    rafraichit_panneau_lateral();
    rafraichit_infos_etat();
    ajuste_panneau_lateral();
    reglages_modifies();
}

void MainWindow::police_du_style(QWidget *widget, double factor, QFont::Weight weight)
{
    m_fonts.push_back({widget, factor, weight});
}

void MainWindow::applique_polices()
{
    const QFont base = QApplication::font();
    for (const ScaledFont &f : m_fonts) {
        if (!f.widget)
            continue;
        QFont font = base;
        if (base.pointSizeF() > 0.0)
            font.setPointSizeF(base.pointSizeF() * f.factor);
        else
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(base.pixelSize() * f.factor))));
        font.setWeight(f.weight);
        f.widget->setFont(font);
    }
}
