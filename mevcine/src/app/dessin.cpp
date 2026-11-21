#include "dessin.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace dessin {

namespace {

/// couleurs des objets suivis
const std::array<QColor, 7> COULEURS = {
    QColor(255, 0, 0), QColor(0, 0, 255),     QColor(0, 255, 255), QColor(255, 0, 255),
    QColor(255, 255, 0), QColor(128, 128, 128), QColor(0, 128, 0),
};

QPointF tourne(QPointF v, double degres)
{
    const double a = degres * std::numbers::pi / 180.0;
    const double s = std::sin(a), c = std::cos(a);
    return {v.x() * c - v.y() * s, v.x() * s + v.y() * c};
}

} // namespace

QColor couleur(int obj)
{
    return COULEURS[(std::max(obj, 1) - 1) % COULEURS.size()];
}

Vue Vue::ajuste(const QRectF &dispo, QSize taille)
{
    const double w = std::max(taille.width(), 1), h = std::max(taille.height(), 1);
    const double echelle = std::max(std::min(dispo.width() / w, dispo.height() / h), 0.01);
    const QSizeF t(w * echelle, h * echelle);
    return {QRectF(QPointF(dispo.center().x() - t.width() / 2.0, dispo.top()), t), echelle};
}

QPointF Vue::ecran(Vecteur p) const
{
    return rect.topLeft() + QPointF(p.x, p.y) * echelle;
}

Vecteur Vue::image(QPointF pos) const
{
    const QPointF v = (pos - rect.topLeft()) / echelle;
    return {v.x(), v.y()};
}

double longueur_axes(const Vue &vue)
{
    return std::clamp(std::min(vue.rect.width(), vue.rect.height()) * 0.18, 60.0, 160.0);
}

void croix(QPainter &p, QPointF pos, double demi, const QColor &couleur, double epaisseur)
{
    p.save();
    p.setPen(QPen(couleur, epaisseur));
    p.drawLine(pos - QPointF(demi, 0), pos + QPointF(demi, 0));
    p.drawLine(pos - QPointF(0, demi), pos + QPointF(0, demi));
    p.restore();
}

void texte_halo(QPainter &p, QPointF pos, const QString &texte, const QFont &police, const QColor &couleur)
{
    QPainterPath chemin;
    chemin.addText(0, 0, police, texte);
    const QRectF r = chemin.boundingRect();
    chemin.translate(pos - r.center());
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.strokePath(chemin, QPen(HALO, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(chemin, couleur);
    p.restore();
}

void repere(QPainter &p, QPointF origine, int sens_x, int sens_y, const QColor &couleur, double longueur, bool sur_image)
{
    const double sx = sens_x, l = longueur;
    const double epaisseur = sur_image ? 2.0 : 1.5;
    // la pointe de la flèche : longueur et demi-largeur
    const double pl = sur_image ? std::clamp(l * 0.1, 8.0, 12.0) : 5.0;
    const double pw = sur_image ? std::clamp(l * 0.045, 4.0, 6.0) : 3.0;
    QFont police = QApplication::font();
    police.setPixelSize(static_cast<int>(std::lround(sur_image ? std::clamp(l * 0.13, 13.0, 17.0) : std::clamp(l * 0.3, 9.0, 15.0))));
    police.setWeight(sur_image ? QFont::DemiBold : QFont::Normal);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const std::pair<double, QString> axes[] = {{0.0, u"x"_s}, {-90.0 * sens_x * sens_y, u"y"_s}};
    for (const auto &[angle, nom] : axes) {
        const auto point = [&](double x, double y) { return origine + tourne({x, y}, angle); };
        const QLineF trait(point(-l * sx, 0), point((l - pl * 0.5) * sx, 0));
        const QPolygonF pointe({point(l * sx, 0), point((l - pl) * sx, pw), point((l - pl) * sx, -pw)});
        if (sur_image) {
            p.setPen(QPen(HALO, epaisseur + 2.0, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(trait);
            p.setPen(QPen(HALO, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(HALO);
            p.drawPolygon(pointe);
        }
        p.setPen(QPen(couleur, epaisseur, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(trait);
        p.setPen(Qt::NoPen);
        p.setBrush(couleur);
        p.drawPolygon(pointe);
        // le nom de l'axe, un peu au-delà de la pointe
        const QPointF bout = point((l + police.pixelSize() * 0.6 + 2.0) * sx, 0);
        if (sur_image) {
            texte_halo(p, bout, nom, police, couleur);
        } else {
            p.setPen(couleur);
            p.setFont(police);
            const QFontMetricsF fm(police);
            p.drawText(QRectF(bout - QPointF(20, fm.height() / 2), QSizeF(40, fm.height())), Qt::AlignCenter, nom);
        }
    }
    p.restore();
}

void origine(QPainter &p, QPointF pos, int sens_x, int sens_y, const QColor &couleur)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(HALO, 1.0));
    p.setBrush(couleur);
    p.drawEllipse(pos, 3.5, 3.5);
    QFont police = QApplication::font();
    police.setPixelSize(14);
    police.setWeight(QFont::DemiBold);
    texte_halo(p, pos + QPointF(-sens_x * 11.0, sens_y * 11.0), u"O"_s, police, couleur);
    p.restore();
}

void fleche(QPainter &p, QPointF org, QPointF ext, const QColor &couleur, double epaisseur)
{
    const QPointF v = ext - org;
    const double n = std::hypot(v.x(), v.y());
    if (n < 0.5)
        return;
    const QPointF dir = v / n, ortho(-dir.y(), dir.x());
    // une pointe de taille fixe, réduite pour une flèche très courte
    const double pl = std::min(4.0 + 3.0 * epaisseur, n * 0.6), pw = pl * 0.45;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(couleur, epaisseur, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(org, ext - dir * pl * 0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(couleur);
    p.drawPolygon(QPolygonF({ext, ext - dir * pl + ortho * pw, ext - dir * pl - ortho * pw}));
    p.restore();
}

void texte(QPainter &p, QPointF pos, const QString &t, double taille, const QColor &couleur,
           const std::optional<QColor> &fond, bool centre)
{
    QFont police = QApplication::font();
    police.setPixelSize(static_cast<int>(std::lround(taille)));
    const QFontMetricsF fm(police);
    const QSizeF s(fm.horizontalAdvance(t), fm.height());
    const QPointF coin = centre ? QPointF(pos.x() - s.width() / 2, pos.y() - s.height()) : QPointF(pos.x(), pos.y() - s.height());
    const QRectF r(coin, s);
    p.save();
    if (fond)
        p.fillRect(r.adjusted(-5, -5, 5, 5), *fond);
    p.setFont(police);
    p.setPen(couleur);
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, t);
    p.restore();
}

bool sombre(const QPalette &palette)
{
    return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
}

QColor couleur_reussite(const QPalette &palette)
{
    return sombre(palette) ? QColor(110, 215, 130) : QColor(20, 125, 55);
}

QColor couleur_attention(const QPalette &palette)
{
    return sombre(palette) ? QColor(255, 143, 0) : QColor(200, 90, 0);
}

QColor couleur_erreur(const QPalette &palette)
{
    return sombre(palette) ? QColor(255, 90, 90) : QColor(200, 0, 0);
}

QFont police(double facteur, bool gras)
{
    QFont f = QApplication::font();
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * facteur);
    else
        f.setPixelSize(std::max(1, static_cast<int>(std::lround(f.pixelSize() * facteur))));
    f.setBold(gras);
    return f;
}

QFont police_chiffres(double facteur)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QFont base = QApplication::font();
    if (base.pointSizeF() > 0)
        f.setPointSizeF(base.pointSizeF() * facteur);
    else
        f.setPixelSize(std::max(1, static_cast<int>(std::lround(base.pixelSize() * facteur))));
    return f;
}

void demi_gras(QWidget *w)
{
    // seule la graisse est imposée : la taille reste celle de l'interface, et suit son agrandissement
    QFont f;
    f.setWeight(QFont::DemiBold);
    w->setFont(f);
}

void chiffres(QWidget *w)
{
    // seule la famille est imposée : la taille reste celle de l'interface
    const QFont fixe = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFont f;
    f.setFamilies(fixe.families().isEmpty() ? QStringList{fixe.family()} : fixe.families());
    f.setStyleHint(QFont::TypeWriter);
    w->setFont(f);
}

void style_police(QWidget *w, double facteur, bool gras)
{
    w->setProperty("facteur_police", facteur);
    w->setProperty("police_grasse", gras);
    w->setFont(police(facteur, gras));
}

void reapplique_polices(QWidget *racine)
{
    QList<QWidget *> widgets = racine->findChildren<QWidget *>();
    widgets << racine;
    for (QWidget *w : widgets) {
        const QVariant facteur = w->property("facteur_police");
        if (facteur.isValid())
            w->setFont(police(facteur.toDouble(), w->property("police_grasse").toBool()));
    }
}

QColor couleur_ton(Ton ton, const QPalette &palette)
{
    switch (ton) {
    case Ton::Reussite: return couleur_reussite(palette);
    case Ton::Attention: return couleur_attention(palette);
    case Ton::Erreur: return couleur_erreur(palette);
    case Ton::Discret: return palette.color(QPalette::PlaceholderText);
    }
    return palette.color(QPalette::WindowText);
}

void colore(QWidget *w, const QColor &couleur)
{
    QPalette p = w->palette();
    p.setColor(QPalette::WindowText, couleur);
    p.setColor(QPalette::Text, couleur);
    p.setColor(QPalette::ButtonText, couleur);
    w->setPalette(p);
}

void colore(QWidget *w, Ton ton)
{
    w->setProperty("ton", static_cast<int>(ton));
    colore(w, couleur_ton(ton, QApplication::palette()));
}

void discret(QWidget *w)
{
    colore(w, Ton::Discret);
}

void recolore(QWidget *racine)
{
    QList<QWidget *> widgets = racine->findChildren<QWidget *>();
    widgets << racine;
    for (QWidget *w : widgets) {
        const QVariant ton = w->property("ton");
        if (ton.isValid())
            colore(w, couleur_ton(static_cast<Ton>(ton.toInt()), QApplication::palette()));
        if (auto *fond = dynamic_cast<FondLateral *>(w))
            fond->suit_le_theme();
    }
}

int largeur_panneau()
{
    const QFontMetrics fm(QApplication::font());
    return std::max(330, fm.averageCharWidth() * 46);
}

QWidget *trait(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

QLabel *titre_partie(QWidget *parent)
{
    auto *l = new QLabel(parent);
    demi_gras(l);
    l->setWordWrap(true);
    return l;
}

// ------------------------------------------------------------------ le panneau latéral

FondLateral::FondLateral(QWidget *parent, bool filet_a_droite)
    : QWidget(parent)
    , m_filet_a_droite(filet_a_droite)
{
    setObjectName(u"panneau_lateral"_s);
}

void FondLateral::suit_le_theme()
{
    // En sombre, Fusion dessine le bord des champs (cases, listes, tableaux) un peu plus foncé que le
    // fond de la fenêtre (Window) : sur le fond plus sombre du panneau, il disparaîtrait. Le contenu du
    // panneau a donc une palette locale où Window est la couleur Light ; le fond du panneau, lui, est
    // dessiné ici d'après la palette de l'application. En clair, rien ne change.
    const QPalette app = QApplication::palette();
    QPalette contenu;
    if (sombre(app))
        contenu.setColor(QPalette::Window, app.color(QPalette::Light));
    for (QWidget *w : findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
        if (auto *defile = qobject_cast<QScrollArea *>(w)) {
            if (defile->widget())
                defile->widget()->setPalette(contenu);
        } else {
            w->setPalette(contenu);
        }
    }
}

void FondLateral::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // le contenu est en place : il reçoit sa palette
    if (!m_theme_suivi) {
        m_theme_suivi = true;
        suit_le_theme();
    }
}

void FondLateral::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = QApplication::palette();
    p.fillRect(rect(), pal.color(QPalette::Window).darker(sombre(pal) ? 125 : 104));
    p.fillRect(QRect(m_filet_a_droite ? width() - 1 : 0, 0, 1, height()), pal.color(QPalette::Mid));
}

DefilementLateral::DefilementLateral(QWidget *contenu, QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(u"defilement_lateral"_s);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setWidget(contenu);
    // fond transparent : celui du panneau
    viewport()->setAutoFillBackground(false);
    contenu->setAutoFillBackground(false);
    // le contenu change de largeur (langue, agrandissement, partie dépliée) : le panneau suit
    contenu->installEventFilter(this);
}

QSize DefilementLateral::minimumSizeHint() const
{
    const int contenu = widget() ? widget()->minimumSizeHint().width() : 0;
    return {contenu + verticalScrollBar()->sizeHint().width() + 2 * frameWidth(), QScrollArea::minimumSizeHint().height()};
}

bool DefilementLateral::eventFilter(QObject *objet, QEvent *event)
{
    if (objet == widget() && event->type() == QEvent::LayoutRequest)
        updateGeometry();
    return QScrollArea::eventFilter(objet, event);
}

FondLateral *panneau_lateral(QWidget *contenu, QWidget *parent)
{
    auto *fond = new FondLateral(parent);
    auto *l = new QVBoxLayout(fond);
    // le filet, à droite
    l->setContentsMargins(0, 0, 1, 0);
    l->addWidget(new DefilementLateral(contenu, fond));
    return fond;
}

SeparationLaterale::SeparationLaterale(QWidget *panneau, QWidget *zone, QWidget *parent)
    : QSplitter(Qt::Horizontal, parent)
{
    addWidget(panneau);
    addWidget(zone);
    setCollapsible(0, false);
    setCollapsible(1, false);
    setStretchFactor(1, 1);
    setChildrenCollapsible(false);
}

void SeparationLaterale::showEvent(QShowEvent *event)
{
    QSplitter::showEvent(event);
    if (m_dimensionnee)
        return;
    m_dimensionnee = true;
    // Largeur de départ du panneau, donnée à sa première apparition : avant, caché derrière une autre
    // page, il n'a pas sa taille, et QSplitter répartirait en proportion
    QTimer::singleShot(0, this, [this] {
        const int panneau = std::max(largeur_panneau(), widget(0)->minimumSizeHint().width());
        setSizes({panneau, std::max(width() - panneau - handleWidth(), 1)});
    });
}

} // namespace dessin
