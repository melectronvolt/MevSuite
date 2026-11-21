#include "plot.h"

#include "style.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

/// Intensités affichées (de 0 à 255, avec une marge de 5 %).
constexpr double Y_MIN = -12.75;
constexpr double Y_MAX = 267.75;

/// Pas « rond » (1, 2 ou 5 × 10ⁿ) pour environ `nombre` graduations sur l’étendue visible.
double pas_rond(double etendue, double nombre)
{
    const double valeurBrute = etendue / std::max(nombre, 1.0);
    if (!(valeurBrute > 0.0) || !std::isfinite(valeurBrute))
        return 1.0;
    const double puissanceDix = std::pow(10.0, std::floor(std::log10(valeurBrute)));
    if (puissanceDix == 0.0)
        return valeurBrute;
    const double mantisse = valeurBrute / puissanceDix;
    return (mantisse <= 1.0 ? 1.0 : mantisse <= 2.0 ? 2.0 : mantisse <= 5.0 ? 5.0 : 10.0) * puissanceDix;
}

int decimales_du_pas(double pas)
{
    return std::max(0, -static_cast<int>(std::floor(std::log10(pas) + 1e-9)));
}

/// Pour les échelles extrêmes, on calcule les graduations dans une unité intermédiaire.
/// Le graphe reste ainsi gradué même si sa marge dépasse la plus grande longueur représentable.
void dessine_abscisses(QPainter &peintre, const QRectF &cadre, int largeurWidget,
                       double longueurCoupe, double fractionGauche, double fractionDroite,
                       QChar decimale, QColor grille, QColor texte)
{
    if (!(longueurCoupe > 0) || !std::isfinite(longueurCoupe))
        return;
    const bool notationScientifique = notation_scientifique(longueurCoupe);
    const int exposant = std::clamp(int(std::floor(std::log10(longueurCoupe))), -300, 300);
    const double facteur = notationScientifique ? std::pow(10.0, exposant) : 1.0;
    const double longueur = longueurCoupe / facteur;
    const double minimum = fractionGauche * longueur;
    const double maximum = fractionDroite * longueur;
    const QFontMetricsF police(peintre.font());
    const double nombreVise = cadre.width() / (police.horizontalAdvance(u"0000000"_s) + 10.0);
    const double pas = pas_rond(maximum - minimum, nombreVise);
    if (!(pas > 0.0) || !std::isfinite(pas))
        return;
    const double premiere = std::ceil(minimum / pas - 1e-9);
    const int decimales = decimales_du_pas(pas);
    double finEtiquette = -std::numeric_limits<double>::infinity();
    for (int i = 0; i <= std::ceil(nombreVise) + 1; ++i) {
        const double graduation = (premiere + i) * pas;
        if (graduation > maximum + 1e-9 * pas)
            break;
        const double valeur = graduation * facteur;
        if (!std::isfinite(valeur))
            continue;
        const double x = cadre.left() + (graduation / longueur - fractionGauche) * cadre.width()
                                       / (fractionDroite - fractionGauche);
        peintre.setPen(grille);
        peintre.drawLine(QPointF(x, cadre.top()), QPointF(x, cadre.bottom()));
        const QString etiquette = notationScientifique
            ? avec_separateur_decimal(QString::number(valeur, 'g', 12), decimale)
            : texte_decimal(valeur, decimales, decimale);
        const double largeur = police.horizontalAdvance(etiquette);
        const double gauche = std::min(x - largeur / 2, largeurWidget - largeur - 2);
        if (gauche < finEtiquette + 6)
            continue;
        finEtiquette = gauche + largeur;
        peintre.setPen(texte);
        peintre.drawText(QRectF(gauche - 2, cadre.bottom() + 2, largeur + 4, police.height()),
                         Qt::AlignHCenter | Qt::AlignTop, etiquette);
    }
}

/// Dans chaque colonne fortement sous-échantillonnée, conserver les extrema et raccorder
/// les valeurs de sortie et d'entrée. Le CSV et les lectures utilisent toujours le profil entier.
void dessine_courbe(QPainter &p, const QRectF &r, const Profil &profil, double t0, double t1, QColor couleur)
{
    const auto &valeurs = profil.intensites;
    if (valeurs.size() < 2 || t1 < 0 || t0 > 1)
        return;
    const double dernier = double(valeurs.size() - 1);
    const size_t debut = size_t(std::floor(std::clamp(t0, 0.0, 1.0) * dernier));
    const size_t fin = size_t(std::ceil(std::clamp(t1, 0.0, 1.0) * dernier));
    const double pas = r.width() / ((t1 - t0) * dernier);
    const auto x = [&](size_t k) { return r.left() + (double(k) - t0 * dernier) * pas; };
    const auto y = [&](double valeur) { return r.bottom() - (valeur - Y_MIN) * r.height() / (Y_MAX - Y_MIN); };
    p.save();
    if (pas * p.device()->devicePixelRatioF() >= 1.0) {
        // Des segments indépendants évitent la triangulation coûteuse d'une
        // longue polyligne lorsque des pics voisins traversent toute la hauteur.
        QList<QLineF> segments;
        segments.reserve(qsizetype(fin - debut));
        QPointF precedent(x(debut), y(valeurs[debut]));
        for (size_t k = debut + 1; k <= fin; ++k) {
            const QPointF suivant(x(k), y(valeurs[k]));
            segments.append(QLineF(precedent, suivant));
            precedent = suivant;
        }
        p.setPen(QPen(couleur, 1.0, Qt::SolidLine, Qt::SquareCap));
        p.drawLines(segments);
    } else {
        const double ratio = p.device()->devicePixelRatioF();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(couleur, 1.0, Qt::SolidLine, Qt::FlatCap));
        QPointF sortie;
        for (size_t k = debut; k <= fin;) {
            const size_t premier = k;
            const double colonne = std::floor(x(k) * ratio);
            double bas = valeurs[k], haut = valeurs[k];
            ++k;
            while (k <= fin && std::floor(x(k) * ratio) == colonne) {
                bas = std::min(bas, valeurs[k]);
                haut = std::max(haut, valeurs[k]);
                ++k;
            }
            const double milieu = (colonne + 0.5) / ratio;
            if (premier != debut)
                p.drawLine(sortie, QPointF(milieu, y(valeurs[premier])));
            p.drawLine(QPointF(milieu, y(bas)), QPointF(milieu, y(haut)));
            sortie = QPointF(milieu, y(valeurs[k - 1]));
        }
    }
    p.restore();
}

} // namespace

GraphiqueProfil::GraphiqueProfil(Seance &seance, QWidget *parent)
    : QWidget(parent)
    , m_seance(seance)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_delaiClic.setTimerType(Qt::PreciseTimer);
    connect(&m_delaiClic, &QTimer::timeout, this, &GraphiqueProfil::termine_clic);
    setMinimumHeight(120);
}

void GraphiqueProfil::definit_traductions(const Textes *tr)
{
    m_textes = tr;
    update();
}

void GraphiqueProfil::reinitialise_vue()
{
    definit_vue(-0.05, 1.05);
}

void GraphiqueProfil::definit_vue(double t0, double t1)
{
    if (!std::isfinite(t0) || !std::isfinite(t1 - t0) || t1 <= t0)
        return;
    m_fractionGauche = t0;
    m_fractionDroite = t1;
    update();
}

QRectF GraphiqueProfil::cadre_graphique() const
{
    const QFontMetricsF fm(font());
    const double left = fm.horizontalAdvance(u"255"_s) + fm.height() + 16.0;
    // à droite, la moitié de la dernière graduation de l'axe horizontal (« 31,25 » sous le bord)
    const double right = std::max(14.0, fm.horizontalAdvance(u"00000"_s) / 2 + 6.0);
    const double bottom = 2.0 * fm.height() + 12.0;
    return QRectF(left, 8.0, std::max(width() - left - right, 10.0), std::max(height() - bottom - 8.0, 10.0));
}

double GraphiqueProfil::fraction_en(double px) const
{
    const QRectF r = cadre_graphique();
    return m_fractionGauche + (px - r.left()) / r.width() * (m_fractionDroite - m_fractionGauche);
}

void GraphiqueProfil::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QPalette pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Window));
    if (!m_textes)
        return;
    const Textes &tr = *m_textes;
    const QChar d = tr.decimal;

    const Profil *profil = m_seance.profil();
    if (!profil) {
        // indication, en couleur discrète
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap,
                         m_seance.coupe ? tr.cutTooShort : tr.cutPrompt);
        return;
    }
    const auto [unite, unitesParPixel] = m_seance.facteur_unite();
    const double longueurCoupe = profil->longueur_px() * unitesParPixel;
    const QRectF r = cadre_graphique();
    const auto ecran_x = [&](double t) { return r.left() + (t - m_fractionGauche) * r.width() / (m_fractionDroite - m_fractionGauche); };
    const auto ecran_y = [&](double v) { return r.bottom() - (v - Y_MIN) * r.height() / (Y_MAX - Y_MIN); };
    const QFontMetricsF fm(font());
    QColor grid = pal.color(QPalette::Text);
    grid.setAlpha(30);

    painter.fillRect(r, pal.color(QPalette::Base));

    // Graduations et grille. Abscisses en unité de longueur, ordonnées en intensité.
    painter.setPen(pal.color(QPalette::Text));
    dessine_abscisses(painter, r, width(), longueurCoupe, m_fractionGauche, m_fractionDroite, d, grid, pal.color(QPalette::Text));
    const double pasIntensite = pas_rond(Y_MAX - Y_MIN, r.height() / (2.5 * fm.height()));
    for (auto k = static_cast<long long>(std::ceil(Y_MIN / pasIntensite)); k * pasIntensite <= Y_MAX; ++k) {
        const double y = k * pasIntensite;
        const double sy = ecran_y(y);
        painter.setPen(grid);
        painter.drawLine(QPointF(r.left(), sy), QPointF(r.right(), sy));
        painter.setPen(pal.color(QPalette::Text));
        painter.drawText(QRectF(0.0, sy - fm.height() / 2, r.left() - 4.0, fm.height()),
                         Qt::AlignRight | Qt::AlignVCenter, texte_decimal(y, decimales_du_pas(pasIntensite), d));
    }

    // Titres des axes.
    painter.drawText(QRectF(r.left(), height() - fm.height() - 4.0, r.width(), fm.height()), Qt::AlignHCenter,
                     tr.axisX(unite));
    painter.save();
    painter.translate(4.0, r.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-r.height() / 2, 0.0, r.height(), fm.height()), Qt::AlignHCenter, tr.axisY);
    painter.restore();

    // Courbe et curseurs, limités à la zone du graphique.
    painter.save();
    painter.setClipRect(r);
    painter.setRenderHint(QPainter::Antialiasing, true);
    dessine_courbe(painter, r, *profil, m_fractionGauche, m_fractionDroite,
                   est_sombre(pal) ? QColor(230, 230, 230) : QColor(30, 30, 30));
    // curseurs et leurs noms, sur un cartouche du fond : lisibles au-dessus de la courbe
    QFont nameFont = font();
    nameFont.setWeight(TITLE_WEIGHT);
    painter.setFont(nameFont);
    const QFontMetricsF nameMetrics(nameFont);
    QColor tag = pal.color(QPalette::Base);
    tag.setAlphaF(0.85f);
    const QColor colors[] = {couleur_curseur_a(pal), couleur_curseur_b(pal)};
    const QString names[] = {u"A"_s, u"B"_s};
    for (int i : {1 - m_seance.curseurActif, m_seance.curseurActif}) {
        const double sx = ecran_x(m_seance.fractionsCurseurs[i]);
        painter.setPen(QPen(colors[i], i == m_seance.curseurActif ? 2.5 : 1.5));
        painter.drawLine(QPointF(sx, r.top()), QPointF(sx, r.bottom()));
        // à droite du trait, à gauche près du bord
        const double w = nameMetrics.horizontalAdvance(names[i]) + 6.0;
        const double left = sx + 3.0 + w > r.right() ? sx - 3.0 - w : sx + 3.0;
        const QRectF box(left, r.top() + 2.0, w, nameMetrics.height());
        painter.fillRect(box, tag);
        painter.setPen(colors[i]);
        painter.drawText(box, Qt::AlignCenter, names[i]);
    }
    painter.restore();

    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(r);

    // Position du pointeur.
    if (m_survol && r.contains(*m_survol)) {
        const double t = fraction_en(m_survol->x());
        if (t < 0 || t > 1)
            return;
        const double x = t * longueurCoupe;
        const double y = profil->valeur_en(t);
        const QPointF point(m_survol->x(), ecran_y(y));
        const QString text = u"x = %1 %2\nI = %3"_s.arg(texte_nombre(x, 4, d), unite, QString::number(std::lround(y)));
        QPen dashed(pal.color(QPalette::Text), 1.0, Qt::DashLine);
        painter.setPen(dashed);
        painter.drawLine(QPointF(m_survol->x(), r.top()), QPointF(m_survol->x(), r.bottom()));
        painter.drawLine(QPointF(r.left(), point.y()), QPointF(r.right(), point.y()));
        painter.setPen(QPen(pal.color(QPalette::Base), 1));
        painter.setBrush(pal.color(QPalette::Text));
        painter.drawEllipse(point, 3.5, 3.5);
        const QRectF box = fm.boundingRect(QRectF(0, 0, 400, 100), Qt::AlignLeft, text).adjusted(-4, -3, 4, 3);
        QPointF at = point + QPointF(12.0, -box.height() - 6.0);
        at.setX(std::min(at.x(), r.right() - box.width()));
        at.setY(std::max(at.y(), r.top()));
        const QRectF placed(at, box.size());
        painter.setPen(pal.color(QPalette::Mid));
        painter.setBrush(pal.color(QPalette::ToolTipBase));
        painter.drawRoundedRect(placed, 3, 3);
        painter.setPen(pal.color(QPalette::ToolTipText));
        painter.drawText(placed.adjusted(4, 3, -4, -3), Qt::AlignLeft, text);
    }
}

void GraphiqueProfil::place_curseur(int index, double t)
{
    m_seance.curseurActif = index;
    m_seance.fractionsCurseurs[index] = std::clamp(t, 0.0, 1.0);
    emit curseurs_deplaces();
    update();
}

void GraphiqueProfil::selectionne_curseur(int index)
{
    termine_clic();
    m_seance.curseurActif = index;
    emit curseurs_deplaces();
    setFocus();
    update();
}

void GraphiqueProfil::deplace_un_echantillon(int sens)
{
    termine_clic();
    const Profil *profil = m_seance.profil();
    if (!profil || profil->intensites.size() < 2)
        return;
    const int i = m_seance.curseurActif;
    const double dernier = double(profil->intensites.size() - 1);
    double position = m_seance.fractionsCurseurs[i] * dernier;
    const double proche = std::round(position);
    if (qFuzzyCompare(1 + position, 1 + proche))
        position = proche;
    const double t = (sens > 0 ? std::floor(position) + 1 : std::ceil(position) - 1) / dernier;
    m_seance.modifie([&] { place_curseur(i, t); });
}

void GraphiqueProfil::termine_clic()
{
    if (m_delaiClic.isActive()) {
        m_delaiClic.stop();
        m_seance.modifie([this] { place_curseur(m_curseurClic, m_fractionClic); });
    }
}

bool GraphiqueProfil::annule_glisser()
{
    const bool enCours = m_geste.has_value() || m_delaiClic.isActive();
    m_delaiClic.stop();
    if (m_geste) {
        static_cast<Travail &>(m_seance) = std::move(m_geste->avant);
        m_geste.reset();
        emit curseurs_deplaces();
        update();
    }
    return enCours;
}

void GraphiqueProfil::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_seance.coupe_mesurable() || !cadre_graphique().contains(event->position()))
        return;
    termine_clic();
    const double t = fraction_en(event->position().x());
    const int actif = m_seance.curseurActif;
    const int i = std::abs(t - m_seance.fractionsCurseurs[actif]) <= std::abs(t - m_seance.fractionsCurseurs[1 - actif])
        ? actif : 1 - actif;
    m_geste = Geste{m_seance, i, event->position()};
}

void GraphiqueProfil::mouseMoveEvent(QMouseEvent *event)
{
    m_survol = event->position();
    if (m_geste && (event->buttons() & Qt::LeftButton) &&
        (m_geste->deplace || (event->position() - m_geste->depart).manhattanLength() >= QApplication::startDragDistance())) {
        m_geste->deplace = true;
        place_curseur(m_geste->curseur, fraction_en(event->position().x()));
    } else {
        update();
    }
}

void GraphiqueProfil::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_geste)
        return;
    const double t = fraction_en(event->position().x());
    if (m_geste->deplace || (event->position() - m_geste->depart).manhattanLength() >= QApplication::startDragDistance()) {
        place_curseur(m_geste->curseur, t);
        m_seance.memorise(std::move(m_geste->avant));
    } else {
        m_curseurClic = m_geste->curseur;
        m_fractionClic = t;
        m_delaiClic.start(QApplication::doubleClickInterval());
    }
    m_geste.reset();
}

void GraphiqueProfil::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        annule_glisser();
        reinitialise_vue();
    }
}

void GraphiqueProfil::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::NoModifier && m_seance.coupe_mesurable() && !m_geste) {
        switch (event->key()) {
        case Qt::Key_A: selectionne_curseur(0); return;
        case Qt::Key_B: selectionne_curseur(1); return;
        case Qt::Key_Left: deplace_un_echantillon(-1); return;
        case Qt::Key_Right: deplace_un_echantillon(1); return;
        }
    }
    QWidget::keyPressEvent(event);
}

void GraphiqueProfil::focusOutEvent(QFocusEvent *event)
{
    if (m_geste)
        annule_glisser();
    else
        termine_clic();
    QWidget::focusOutEvent(event);
}

void GraphiqueProfil::wheelEvent(QWheelEvent *event)
{
    if (!m_seance.profil())
        return;
    const QPoint angle = event->angleDelta();
    const QPoint pixels = event->pixelDelta();
    const QPoint valeurBrute = pixels.isNull() ? angle * 50 / 120 : pixels;
    // Selon le système, Maj + molette arrive en défilement vertical ou horizontal.
    const double delta = valeurBrute.y() != 0 ? valeurBrute.y() : valeurBrute.x();
    const double etendue = m_fractionDroite - m_fractionGauche;
    if (event->modifiers() & Qt::ShiftModifier || (valeurBrute.y() == 0 && valeurBrute.x() != 0)) {
        // Déplacement horizontal.
        const double decalage = -delta / cadre_graphique().width() * etendue;
        definit_vue(m_fractionGauche + decalage, m_fractionDroite + decalage);
    } else {
        // Zoom horizontal autour de la souris, comme sur l'image.
        const double facteur = std::exp(delta * 0.002);
        const double nouvelleEtendue = std::clamp(etendue / facteur, 1e-6, 100.0);
        const double ancrage = fraction_en(event->position().x());
        const double gauche = ancrage - (ancrage - m_fractionGauche) * nouvelleEtendue / etendue;
        definit_vue(gauche, gauche + nouvelleEtendue);
    }
    event->accept();
}

void GraphiqueProfil::leaveEvent(QEvent *)
{
    m_survol.reset();
    update();
}
