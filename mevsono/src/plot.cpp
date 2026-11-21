#include "plot.h"

#include <QKeyEvent>
#include <QApplication>

#include "format.h"
#include "style.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

/// Pas « rond » (1, 2 ou 5 × 10ⁿ) pour environ `count` graduations sur `range`.
double pas_rond(double range, double count)
{
    const double raw = range / std::max(count, 1.0);
    if (!(raw > 0.0) || !std::isfinite(raw))
        return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    return (norm <= 1.0 ? 1.0 : norm <= 2.0 ? 2.0 : norm <= 5.0 ? 5.0 : 10.0) * mag;
}

int decimales_du_pas(double step)
{
    return std::max(0, -static_cast<int>(std::floor(std::log10(step) + 1e-9)));
}

/// Plus petite largeur de vue accessible au zoom (en unités de l'axe).
constexpr double LARGEUR_MINIMALE = 1e-9;

} // namespace

PlotWidget::PlotWidget(QWidget *parent)
    : QWidget(parent)
{
    m_simpleClic.setSingleShot(true);
    m_simpleClic.setTimerType(Qt::PreciseTimer);
    connect(&m_simpleClic, &QTimer::timeout, this, &PlotWidget::termine_clic);
    setMouseTracking(true);
    setMinimumHeight(100);
    setFocusPolicy(Qt::StrongFocus);
}

void PlotWidget::definit_traductions(const Tr *tr)
{
    m_tr = tr;
    update();
}

void PlotWidget::change_echelle_interface(double scale)
{
    m_uiScale = scale;
    update();
}

void PlotWidget::reinitialise_vue()
{
    annule_clic();
    m_needsReset = true;
    invalide_courbes();
}

void PlotWidget::invalide_courbes()
{
    m_vuePreparee.reset();
    update();
}

void PlotWidget::definit_vue_x(double x0, double x1)
{
    if (!(x1 > x0) || !std::isfinite(x0) || !std::isfinite(x1))
        return;
    place_vue(x1 - x0, x0, 0.0);
}

void PlotWidget::place_vue(double largeur, double repere, double positionRelative)
{
    const auto [debut, fin] = domaine_x();
    // Même à grande abscisse, les deux bornes doivent rester deux nombres distincts.
    const double precision = std::max(std::abs(debut), std::abs(fin)) * std::numeric_limits<double>::epsilon();
    const double largeurMin = std::min(fin - debut, std::max(LARGEUR_MINIMALE, precision));
    largeur = std::clamp(largeur, largeurMin, fin - debut);
    // Au bord du son, la vue conserve sa largeur et s'arrête au dernier point mesurable.
    m_x0 = std::clamp(repere - positionRelative * largeur, debut, std::max(debut, fin - largeur));
    m_x1 = std::min(m_x0 + largeur, fin);
    m_needsReset = false;
    update();
}

void PlotWidget::assure_vue()
{
    if (m_needsReset) {
        const auto [debut, fin] = x_par_defaut();
        m_x0 = debut;
        m_x1 = fin;
        m_needsReset = false;
    } else if (!m_vuePreparee) {
        // Une référence enregistrée à plus haute fréquence a pu être retirée.
        const auto [debut, fin] = domaine_x();
        if (m_x0 < debut || m_x1 > fin)
            place_vue(m_x1 - m_x0, m_x0, 0.0);
    }
}

QRectF PlotWidget::cadre_graphique() const
{
    const QFontMetricsF fm(font());
    const double left = std::max(fm.horizontalAdvance(u"−000,00"_s), m_largeurGraduationsY) + fm.height() + 14.0;
    // à droite, la moitié de la dernière graduation de l'axe horizontal (« 5000 » sous le bord)
    const double right = std::max(14.0, fm.horizontalAdvance(u"00000"_s) / 2 + 6.0);
    const double bottom = 2.0 * fm.height() + 10.0;
    return QRectF(left, 8.0, std::max(width() - left - right, 10.0), std::max(height() - bottom - 8.0, 10.0));
}

double PlotWidget::ecran_x(double x) const
{
    return ecran_x(x, cadre_graphique());
}

double PlotWidget::ecran_x(double x, const QRectF &r) const
{
    return r.left() + (x - m_x0) / (m_x1 - m_x0) * r.width();
}

double PlotWidget::donnee_x(double px) const
{
    const QRectF r = cadre_graphique();
    return m_x0 + (px - r.left()) / r.width() * (m_x1 - m_x0);
}

double PlotWidget::ecran_y(double y) const
{
    return ecran_y(y, cadre_graphique());
}

double PlotWidget::ecran_y(double y, const QRectF &r) const
{
    const auto [y0, y1] = plage_y();
    return r.bottom() - (y - y0) / (y1 - y0) * r.height();
}

double PlotWidget::donnee_y(double py) const
{
    const QRectF r = cadre_graphique();
    const auto [y0, y1] = plage_y();
    return y0 + (r.bottom() - py) / r.height() * (y1 - y0);
}

std::optional<std::pair<double, double>> PlotWidget::extrema_visibles(std::span<const QPointF> points) const
{
    if (points.empty() || points.back().x() < m_x0 || points.front().x() > m_x1)
        return std::nullopt;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    const auto inclut = [&](double y) {
        lo = std::min(lo, y);
        hi = std::max(hi, y);
    };
    const auto debut = std::ranges::lower_bound(points, m_x0, {}, &QPointF::x);
    const auto fin = std::ranges::upper_bound(points, m_x1, {}, &QPointF::x);
    for (auto it = debut; it != fin; ++it)
        inclut(it->y());
    // Un zoom peut ne montrer qu'un morceau de segment entre deux échantillons.
    const auto au_bord = [&](auto it, double x) {
        if (it != points.begin() && it != points.end()) {
            const QPointF &a = it[-1], &b = *it;
            inclut(std::lerp(a.y(), b.y(), (x - a.x()) / (b.x() - a.x())));
        }
    };
    au_bord(debut, m_x0);
    au_bord(fin, m_x1);
    return std::pair(lo, hi);
}

std::vector<PlotWidget::Tick> PlotWidget::graduations_rondes(double lo, double hi, double count) const
{
    std::vector<Tick> ticks;
    const double step = pas_rond(hi - lo, count);
    const int decimals = decimales_du_pas(step);
    const QChar d = m_tr ? m_tr->decimal : u'.';
    for (auto k = static_cast<long long>(std::ceil(lo / step)); k * step <= hi && ticks.size() < 200; ++k) {
        const double v = double(k) * step;
        ticks.push_back({v, texte_decimal(k == 0 ? 0.0 : v, decimals, d)});
    }
    return ticks;
}

std::vector<PlotWidget::Tick> PlotWidget::graduations_x(double x0, double x1, double pixels) const
{
    const QFontMetricsF fm(font());
    return graduations_rondes(x0, x1, pixels / (fm.horizontalAdvance(u"0000000"_s) + 10.0));
}

void PlotWidget::dessine_message(QPainter &painter, const QString &text, double scale)
{
    QFont font = this->font();
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(font.pointSizeF() * scale);
    painter.setFont(font);
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, text);
}

void PlotWidget::trace_courbe(QPainter &painter, const QPolygonF &line)
{
    // extrémités carrées (celles du stylo par défaut) : les segments se chevauchent aux sommets, sans
    // trou ; des extrémités rondes coûteraient dix fois plus
    QList<QLineF> segments;
    segments.reserve(std::max<qsizetype>(line.size() - 1, 0));
    for (qsizetype i = 0; i + 1 < line.size(); ++i)
        segments << QLineF(line[i], line[i + 1]);
    painter.drawLines(segments);
}

void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QPalette pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Window));
    if (!m_tr)
        return;
    if (!a_des_donnees()) {
        dessine_vide(painter);
        return;
    }
    assure_vue();
    const VueCourbes vue{m_x0, m_x1, size_t(cadre_graphique().width())};
    if (vue != m_vuePreparee) {
        m_plageY = prepare_courbes(vue.colonnes);
        m_vuePreparee = vue;
    }
    const auto [y0, y1] = plage_y();
    const QFontMetricsF fm(font());
    const auto graduationsY = graduations_rondes(y0, y1, cadre_graphique().height() / (2.5 * fm.height()));
    m_largeurGraduationsY = 0.0;
    for (const Tick &tick : graduationsY)
        m_largeurGraduationsY = std::max(m_largeurGraduationsY, fm.horizontalAdvance(tick.label));
    const QRectF r = cadre_graphique();
    QColor grid = pal.color(QPalette::Text);
    grid.setAlpha(30);
    const QColor text = pal.color(QPalette::Text);
    painter.fillRect(r, pal.color(QPalette::Base));

    // Graduations et grille.
    double lastLabelRight = -1e9;
    for (const Tick &tick : graduations_x(m_x0, m_x1, r.width())) {
        const double sx = ecran_x(tick.value, r);
        if (sx < r.left() - 0.5 || sx > r.right() + 0.5)
            continue;
        painter.setPen(grid);
        painter.drawLine(QPointF(sx, r.top()), QPointF(sx, r.bottom()));
        // étiquettes qui se chevaucheraient (graduations logarithmiques serrées) : la première seulement
        const double w = fm.horizontalAdvance(tick.label);
        // une étiquette plus longue que la marge reste entière, ramenée contre le bord
        const double labelLeft = std::min(sx - w / 2, width() - w - 2.0);
        if (tick.label.isEmpty() || labelLeft < lastLabelRight + 6.0)
            continue;
        lastLabelRight = labelLeft + w;
        painter.setPen(text);
        painter.drawText(QRectF(labelLeft - 2.0, r.bottom() + 2.0, w + 4.0, fm.height()), Qt::AlignCenter, tick.label);
    }
    for (const Tick &tick : graduationsY) {
        const double sy = ecran_y(tick.value, r);
        painter.setPen(grid);
        painter.drawLine(QPointF(r.left(), sy), QPointF(r.right(), sy));
        painter.setPen(text);
        painter.drawText(QRectF(0.0, sy - fm.height() / 2, r.left() - 4.0, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         tick.label);
    }

    // Titres des axes.
    painter.drawText(QRectF(r.left(), height() - fm.height() - 4.0, r.width(), fm.height()), Qt::AlignHCenter,
                     legende_axe_x());
    painter.save();
    painter.translate(2.0, r.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-r.height() / 2, 0.0, r.height(), fm.height()), Qt::AlignHCenter, legende_axe_y());
    painter.restore();

    // Courbes et repères, limités à la zone du graphique.
    painter.save();
    painter.setClipRect(r);
    painter.setRenderHint(QPainter::Antialiasing, true);
    dessine_donnees(painter, r);
    painter.restore();

    painter.setPen(pal.color(hasFocus() ? QPalette::Highlight : QPalette::Mid));
    painter.drawRect(r);

    // Réticule : valeur sous la souris.
    if (m_hover && r.contains(*m_hover)) {
        const QPointF data(donnee_x(m_hover->x()), donnee_y(m_hover->y()));
        const QString label = texte_survol(data);
        QPen dashed(attenuee(text, 0.6), 1.0, Qt::DashLine);
        painter.setPen(dashed);
        painter.drawLine(QPointF(m_hover->x(), r.top()), QPointF(m_hover->x(), r.bottom()));
        painter.drawLine(QPointF(r.left(), m_hover->y()), QPointF(r.right(), m_hover->y()));
        const QRectF box = fm.boundingRect(QRectF(0, 0, 600, 400), Qt::AlignLeft, label).adjusted(-5, -4, 5, 4);
        QPointF at = *m_hover + QPointF(14.0, -box.height() - 8.0);
        // près d'un bord, la boîte passe de l'autre côté du pointeur
        if (at.x() + box.width() > r.right())
            at.setX(m_hover->x() - box.width() - 14.0);
        if (at.y() < r.top())
            at.setY(m_hover->y() + 14.0);
        const QRectF placed(at, box.size());
        painter.setPen(pal.color(QPalette::Mid));
        painter.setBrush(attenuee(pal.color(QPalette::ToolTipBase), 0.92));
        painter.drawRoundedRect(placed, 4, 4);
        painter.setPen(pal.color(QPalette::ToolTipText));
        painter.drawText(placed.adjusted(5, 4, -5, -4), Qt::AlignLeft, label);
    }
}

void PlotWidget::decale_pixels(double pixels)
{
    const double decalage = pixels * (m_x1 - m_x0) / cadre_graphique().width();
    definit_vue_x(m_x0 - decalage, m_x1 - decalage);
}

void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    // seule la zone des courbes réagit, pas les graduations ni les titres des axes
    if (!a_des_donnees() || !cadre_graphique().contains(event->position()))
        return;
    if (event->button() == Qt::LeftButton) {
        termine_clic();
        gauche_appuye(event->position());
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panFrom = event->position();
        m_panButton = event->button();
        m_panDistance = 0.0;
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_hover = event->position();
    if (a_des_donnees()) {
        survol_deplace(m_hover);
        if (m_panFrom && (event->buttons() & m_panButton)) {
            const QPointF delta = event->position() - *m_panFrom;
            m_panDistance += std::abs(delta.x()) + std::abs(delta.y());
            m_panFrom = event->position();
            decale_pixels(delta.x());
        }
        if (event->buttons() & Qt::LeftButton)
            gauche_deplace(event->position());
    }
    update();
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!a_des_donnees())
        return;
    if (event->button() == Qt::LeftButton) {
        gauche_relache(event->position());
    } else if (m_panFrom && event->button() == m_panButton) {
        if (m_panButton == Qt::RightButton && m_panDistance < 4.0)
            clic_droit(event->position());
        m_panFrom.reset();
    }
    update();
}

void PlotWidget::termine_clic()
{
    m_simpleClic.stop();
    if (auto action = std::exchange(m_actionClic, {}))
        action();
}

void PlotWidget::differe_clic(std::function<void()> action)
{
    m_actionClic = std::move(action);
    m_simpleClic.start(QApplication::doubleClickInterval());
}

void PlotWidget::annule_clic()
{
    m_simpleClic.stop();
    m_actionClic = {};
}

void PlotWidget::focusOutEvent(QFocusEvent *event)
{
    termine_clic();
    QWidget::focusOutEvent(event);
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && a_des_donnees())
        reinitialise_vue();
}

void PlotWidget::keyPressEvent(QKeyEvent *event)
{
    if (a_des_donnees() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        const int key = event->key();
        if (key == Qt::Key_Home) {
            reinitialise_vue();
            return;
        }
        if (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus) {
            assure_vue();
            const double facteur = key == Qt::Key_Minus ? 2.0 : 0.5;
            place_vue((m_x1 - m_x0) * facteur, centre_zoom_clavier(), 0.5);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void PlotWidget::wheelEvent(QWheelEvent *event)
{
    if (!a_des_donnees())
        return;
    assure_vue();
    const QPoint angle = event->angleDelta();
    const QPoint pixels = event->pixelDelta();
    const QPoint raw = pixels.isNull() ? angle * 50 / 120 : pixels;
    // Selon le système, Maj + molette arrive en défilement vertical ou horizontal.
    const double delta = raw.y() != 0 ? raw.y() : raw.x();
    if (event->modifiers() & Qt::ShiftModifier || (raw.y() == 0 && raw.x() != 0)) {
        decale_pixels(delta);
    } else {
        const double largeur = m_x1 - m_x0;
        const double ancre = donnee_x(event->position().x());
        place_vue(largeur / std::exp(delta * 0.002), ancre, (ancre - m_x0) / largeur);
    }
    event->accept();
}

void PlotWidget::leaveEvent(QEvent *)
{
    m_hover.reset();
    survol_deplace(std::nullopt);
    update();
}
