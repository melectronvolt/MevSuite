#include "signalplot.h"

#include "format.h"
#include "style.h"

#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Distance (pixels) en dessous de laquelle on attrape un curseur.
constexpr double RAYON_PRISE = 8.0;
/// Déplacement (pixels) au-delà duquel un appui devient un glisser, et non plus un clic.
constexpr double DISTANCE_GLISSEMENT = 6.0;

} // namespace

SignalPlot::SignalPlot(Session &session, QWidget *parent)
    : PlotWidget(parent)
    , m_session(session)
{
}

void SignalPlot::place_tete_lecture(std::optional<double> t)
{
    if (t == m_teteLecture)
        return;
    m_teteLecture = t;
    update();
}

bool SignalPlot::a_des_donnees() const
{
    return m_session.loaded.has_value();
}

void SignalPlot::dessine_vide(QPainter &painter)
{
    QFont font = this->font();
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(font.pointSizeF() * 1.2);
    painter.setFont(font);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(rect().adjusted(20, 10, -20, -10), Qt::AlignCenter | Qt::TextWordWrap, textes_interface()->emptyHint);
}

std::pair<double, double> SignalPlot::x_par_defaut() const
{
    return {0.0, m_session.loaded ? m_session.loaded->audio.duree() : 1.0};
}

std::pair<double, double> SignalPlot::prepare_courbes(size_t colonnes)
{
    const Loaded &loaded = *m_session.loaded;
    const audio::Channel canal = m_session.canal();
    m_points = loaded.enveloppe(canal).points(*loaded.audio.signal(canal), loaded.audio.rate, vue_x0(), vue_x1(), colonnes);
    if (m_session.reglages_actifs().autoScaleY) {
        if (const auto bornes = extrema_visibles(m_points)) {
            const auto [lo, hi] = *bornes;
            const double marge = hi > lo ? std::max((hi - lo) * 0.05, std::abs(lo) * 1e-7)
                                         : std::max(std::abs(lo) * 0.05, 1e-4);
            return {lo - marge, hi + marge};
        }
    }
    const double height = std::max(loaded.audio.crete() * 1.1, 1e-4);
    return {-height, height};
}

QString SignalPlot::legende_axe_x() const
{
    return textes_interface()->axisTime;
}

QString SignalPlot::legende_axe_y() const
{
    return textes_interface()->axisSignal;
}

QString SignalPlot::texte_survol(QPointF data) const
{
    const QChar d = textes_interface()->decimal;
    const auto [lo, hi] = plage_y();
    const double pasPixel = (hi - lo) / cadre_graphique().height();
    const int decimales = std::min(decimales_significatives(data.y(), 7),
                                   std::max(decimales_significatives(data.y(), 3), decimales_significatives(pasPixel, 1)));
    return u"t = %1\n%2"_s.arg(texte_temps(data.x(), d), texte_decimal(data.y(), decimales, d));
}

void SignalPlot::dessine_donnees(QPainter &painter, const QRectF &r)
{
    const QPalette pal = palette();
    const double s = echelle_interface();
    const bool extract = m_session.tool == Tool::Extract;

    // extrait analysé (étude des fréquences)
    if (extract) {
        const auto [lo, hi] = trie(m_session.extract);
        painter.fillRect(QRectF(QPointF(ecran_x(lo, r), r.top()), QPointF(ecran_x(hi, r), r.bottom())), nuance_couleur(pal));
    }

    // signal : les échantillons, ou leur minimum et leur maximum par colonne de pixels
    QPolygonF line;
    line.reserve(qsizetype(m_points.size()));
    for (const QPointF &p : m_points)
        line << QPointF(ecran_x(p.x(), r), ecran_y(p.y(), r));
    painter.setPen(QPen(couleur_signal(pal), 1.2 * s));
    trace_courbe(painter, line);

    // curseurs et leurs noms, sur un cartouche du fond : lisibles au-dessus du signal
    QFont name = font();
    name.setWeight(TITLE_WEIGHT);
    painter.setFont(name);
    const QFontMetricsF fm(name);
    const QColor tag = attenuee(pal.color(QPalette::Base), 0.85);
    const auto cursor = [&](double t, const QColor &color, const QString &text, bool top) {
        const double x = ecran_x(t, r);
        painter.setPen(QPen(color, 2.0 * s));
        painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        const double y = top ? r.top() + 2.0 : r.bottom() - fm.height() - 2.0;
        const QRectF box(x + 3.0, y, fm.horizontalAdvance(text) + 6.0, fm.height());
        painter.fillRect(box, tag);
        painter.setPen(color);
        name.setUnderline(hasFocus() && ((text == u"A" || text == u"t₁") == (m_curseurActif == 0)));
        painter.setFont(name);
        painter.drawText(box, Qt::AlignCenter, text);
    };
    // les curseurs de l'activité choisie seulement : A et B, ou t₁ et t₂
    if (extract) {
        cursor(m_session.extract[0], couleur_curseur_a(pal), u"A"_s, true);
        cursor(m_session.extract[1], couleur_curseur_b(pal), u"B"_s, true);
    } else if (const auto &p = m_session.period) {
        cursor((*p)[0], couleur_curseur_t(pal), u"t₁"_s, true);
        cursor((*p)[1], couleur_curseur_t(pal), u"t₂"_s, true);
    }
    if (m_teteLecture) {
        const double x = ecran_x(*m_teteLecture, r);
        painter.setPen(QPen(COLOR_PLAYHEAD, 1.5 * s));
        painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
}

double SignalPlot::temps_en(double px) const
{
    return std::clamp(donnee_x(px), 0.0, m_session.loaded->audio.duree());
}

std::optional<Cursors> SignalPlot::paire_curseurs() const
{
    if (m_session.tool == Tool::Period)
        return m_session.period;
    return m_session.extract;
}

void SignalPlot::place_paire(const Cursors &c)
{
    if (paire_curseurs() == c)
        return;
    if (m_session.tool == Tool::Period) {
        m_session.period = c;
        emit periode_changee();
    } else {
        m_session.extract = c;
        emit extrait_change();
    }
    update();
}

std::optional<int> SignalPlot::curseur_proche(double px) const
{
    const auto p = paire_curseurs();
    if (!p)
        return std::nullopt;
    std::optional<int> best;
    double bestDistance = RAYON_PRISE;
    for (int i : {0, 1}) {
        const double dx = std::abs(ecran_x((*p)[size_t(i)]) - px);
        if (dx <= bestDistance && (!best || dx < bestDistance)) {
            best = i;
            bestDistance = dx;
        }
    }
    return best;
}

void SignalPlot::survol_deplace(std::optional<QPointF> pos)
{
    const bool resize = (m_glissement && m_glissement->curseur)
        || (pos && !m_glissement && cadre_graphique().contains(*pos) && curseur_proche(pos->x()));
    if (resize)
        setCursor(Qt::SizeHorCursor);
    else
        unsetCursor();
}

void SignalPlot::gauche_appuye(QPointF pos)
{
    m_pointAppui = pos;
    m_glissement.reset();
}

void SignalPlot::gauche_deplace(QPointF pos)
{
    if (!m_pointAppui)
        return;
    if (!m_glissement) {
        if (std::hypot(pos.x() - m_pointAppui->x(), pos.y() - m_pointAppui->y()) < DISTANCE_GLISSEMENT)
            return;
        // glisser près d'un curseur le déplace ; ailleurs, une nouvelle paire part du point d'appui
        std::optional<int> curseur = curseur_proche(m_pointAppui->x());
        // A et B l'un sur l'autre : vers la droite, on tire B ; vers la gauche, A
        const Cursors &e = m_session.extract;
        if (curseur && m_session.tool == Tool::Extract && std::abs(ecran_x(e[0]) - ecran_x(e[1])) < 1.0)
            curseur = pos.x() > m_pointAppui->x() ? 1 : 0;
        m_glissement = Glissement{curseur, temps_en(m_pointAppui->x())};
    }
    const double t = temps_en(pos.x());
    // A reste avant B : un curseur de l'extrait s'arrête sur l'autre, une nouvelle paire met A à gauche
    const bool extrait = m_session.tool == Tool::Extract;
    if (m_glissement->curseur) {
        if (auto p = paire_curseurs()) {
            const auto i = size_t(*m_glissement->curseur);
            double instant = t;
            if (extrait)
                instant = i == 0 ? std::min(t, (*p)[1]) : std::max(t, (*p)[0]);
            (*p)[i] = instant;
            place_paire(*p);
        }
    } else if (extrait) {
        place_paire({std::min(m_glissement->instantDepart, t), std::max(m_glissement->instantDepart, t)});
    } else {
        place_paire({m_glissement->instantDepart, t});
    }
}

void SignalPlot::gauche_relache(QPointF pos)
{
    if (!m_pointAppui)
        return;
    // Le relâchement peut arriver entre deux événements de déplacement de la souris.
    gauche_deplace(pos);
    if (m_glissement) {
        if (m_glissement->curseur) {
            m_curseurActif = *m_glissement->curseur;
            aligne({m_curseurActif});
        } else {
            m_curseurActif = m_session.tool == Tool::Extract && temps_en(pos.x()) < m_glissement->instantDepart ? 0 : 1;
            aligne({0, 1});
        }
        ordonne_curseurs_periode();
        m_glissement.reset();
    } else if (paire_curseurs()) {
        differe_clic([this, instant = temps_en(pos.x())] { applique_clic(instant); });
    }
    m_pointAppui.reset();
    survol_deplace(pos);
    update();
}

void SignalPlot::ordonne_curseurs_periode()
{
    auto &paire = m_session.period;
    if (m_session.tool == Tool::Period && paire && (*paire)[0] > (*paire)[1]) {
        // t₁ désigne toujours l'instant le plus tôt. Après croisement, les flèches
        // doivent continuer à déplacer le trait que l'utilisateur vient de lâcher.
        std::swap((*paire)[0], (*paire)[1]);
        m_curseurActif = 1 - m_curseurActif;
        emit periode_changee();
    }
}

void SignalPlot::applique_clic(double instant)
{
    auto paire = paire_curseurs();
    if (!paire || !m_session.loaded)
        return;
    m_curseurActif = std::abs(instant - (*paire)[0]) <= std::abs(instant - (*paire)[1]) ? 0 : 1;
    (*paire)[m_curseurActif] = instant;
    place_paire(*paire);
    aligne({m_curseurActif});
    ordonne_curseurs_periode();
    update();
}

void SignalPlot::annule_geste()
{
    annule_clic();
    m_pointAppui.reset();
    m_glissement.reset();
}

double SignalPlot::centre_zoom_clavier() const
{
    const auto paire = paire_curseurs();
    return paire ? (*paire)[m_curseurActif] : PlotWidget::centre_zoom_clavier();
}

void SignalPlot::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (!m_session.loaded || (key != Qt::Key_1 && key != Qt::Key_2 &&
                              key != Qt::Key_Left && key != Qt::Key_Right)) {
        PlotWidget::keyPressEvent(event);
        return;
    }
    termine_clic();
    const auto &son = m_session.loaded->audio;
    const double span = vue_x1() - vue_x0();
    const auto borne = [&](double t) { return std::clamp(t, 0.0, son.duree()); };
    auto paire = paire_curseurs().value_or(Cursors{borne(vue_x0() + span / 3), borne(vue_x0() + 2 * span / 3)});
    if (key == Qt::Key_1 || key == Qt::Key_2) {
        m_curseurActif = key == Qt::Key_1 ? 0 : 1;
    } else {
        const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? std::max(1 / son.rate, span / 100) : 1 / son.rate;
        double &t = paire[m_curseurActif];
        t = borne(t + (key == Qt::Key_Right ? step : -step));
        t = m_curseurActif == 0 ? std::min(t, paire[1]) : std::max(t, paire[0]);
    }
    place_paire(paire);
    const double t = paire[m_curseurActif];
    if (t < vue_x0() || t > vue_x1())
        definit_vue_x(t - span / 2, t + span / 2);
    update();
    event->accept();
}

void SignalPlot::aligne(std::initializer_list<int> moved)
{
    if (m_session.tool != Tool::Period || !m_session.reglages_actifs().snapZero || !m_session.period)
        return;
    const Loaded &loaded = *m_session.loaded;
    const std::vector<float> &signal = *loaded.audio.signal(m_session.canal());
    // rayon de recherche : 5 % de la largeur visible, et au moins 16 pixels
    const double visible = vue_x1() - vue_x0();
    const double radius = std::max(0.05 * visible, visible / std::max(cadre_graphique().width(), 1.0) * RAYON_PRISE * 2.0);
    Cursors &c = *m_session.period;
    for (int i : moved) {
        if (const auto t = dsp::passage_par_zero_montant(signal, loaded.audio.rate, c[size_t(i)], radius))
            c[size_t(i)] = *t;
    }
    emit periode_changee();
}
