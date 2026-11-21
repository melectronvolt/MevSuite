#include "spectrumplot.h"

#include "format.h"
#include "icons.h"
#include "style.h"

#include <QMenu>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Plus petite échelle des amplitudes : le spectre d'un silence garde des graduations courtes, qui
/// tiennent dans la marge.
constexpr double MIN_AMPLITUDE = 1e-4;

} // namespace

SpectrumPlot::SpectrumPlot(Session &session, const Analyzer &analyzer, QWidget *parent)
    : PlotWidget(parent)
    , m_session(session)
    , m_analyzer(analyzer)
{
    m_spinner.setInterval(60);
    connect(&m_spinner, &QTimer::timeout, this, [this] {
        m_spinnerAngle = (m_spinnerAngle + 30) % 360;
        update();
    });
}

void SpectrumPlot::analyse_changee()
{
    const bool waiting = m_session.loaded && !analyse_courante() && m_analyzer.en_attente();
    if (waiting && !m_spinner.isActive())
        m_spinner.start();
    else if (!waiting)
        m_spinner.stop();
    if (!analyse_courante()) {
        annule_clic();
        m_appui.reset();
        selectionne(std::nullopt);
    }
    invalide_courbes();
}

const dsp::Analysis *SpectrumPlot::analyse_courante() const
{
    return m_session.loaded ? m_analyzer.resultat(m_session.loaded->id) : nullptr;
}

void SpectrumPlot::memorise()
{
    if (const auto *a = analyse_courante(); a && !m_analyzer.en_attente()) {
        m_reference = *a;
        m_parametresReference = m_session.parametres_spectre();
        const auto [aDebut, aFin] = trie(m_session.extract);
        const QChar decimal = textes_interface()->decimal;
        m_nomReference = u"%1 [%2 – %3]"_s.arg(m_session.loaded->audio.name, texte_temps(aDebut, decimal), texte_temps(aFin, decimal));
        invalide_courbes();
    }
}

bool SpectrumPlot::reference_compatible() const
{
    return m_reference && m_parametresReference == m_session.parametres_spectre();
}

bool SpectrumPlot::a_des_donnees() const
{
    return analyse_courante() != nullptr;
}

void SpectrumPlot::dessine_vide(QPainter &painter)
{
    if (!m_session.loaded) {
        dessine_message(painter, textes_interface()->spectrumEmpty);
    } else if (m_analyzer.en_attente()) {
        // calcul en cours : un arc qui tourne
        painter.setRenderHint(QPainter::Antialiasing, true);
        const double side = 28.0 * echelle_interface();
        const QRectF box(rect().center().x() - side / 2, rect().center().y() - side / 2, side, side);
        painter.setPen(QPen(palette().color(QPalette::PlaceholderText), 3.0 * echelle_interface(), Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(box, -m_spinnerAngle * 16, 270 * 16);
    } else {
        dessine_message(painter, textes_interface()->extractTooShort);
    }
}

double SpectrumPlot::vers_x(double f) const
{
    return axe_logarithmique() ? std::log10(std::max(f, 1e-3)) : f;
}

double SpectrumPlot::depuis_x(double x) const
{
    return axe_logarithmique() ? std::pow(10.0, x) : x;
}

QString SpectrumPlot::texte_amplitude(double y) const
{
    const QChar d = textes_interface()->decimal;
    return echelle_decibels() ? texte_decimal(y, 1, d) + u" dBFS"_s : texte_nombre(y, 3, d);
}

std::pair<double, double> SpectrumPlot::x_par_defaut() const
{
    const double maximum = m_session.fmax_affichee();
    if (!axe_logarithmique())
        return {0.0, maximum};
    // Un enregistrement à très basse cadence peut s'arrêter avant 20 Hz.
    const double gauche = maximum > FMIN ? vers_x(FMIN) : domaine_x().first;
    return {gauche, vers_x(maximum)};
}

std::pair<double, double> SpectrumPlot::domaine_x() const
{
    double nyquist = m_session.loaded ? m_session.loaded->audio.rate / 2 : m_session.fmax_affichee();
    double minimum = std::min(FMIN, nyquist / 2);
    if (const auto *analyse = analyse_courante())
        minimum = std::min(minimum, analyse->spectrum.pas_frequence());
    if (reference_compatible()) {
        // La référence peut provenir d'un son enregistré à une autre cadence.
        nyquist = std::max(nyquist, m_reference->spectrum.rate / 2);
        minimum = std::min(minimum, m_reference->spectrum.pas_frequence());
    }
    return {axe_logarithmique() ? vers_x(minimum) : 0.0, vers_x(nyquist)};
}

std::pair<double, double> SpectrumPlot::prepare_courbes(size_t colonnes)
{
    const dsp::Analysis &a = *analyse_courante();
    const Settings active = m_session.reglages_actifs();
    const double f0 = depuis_x(vue_x0()), f1 = depuis_x(vue_x1());
    const auto prepare = [&](const dsp::Analysis &analyse, double facteur) {
        auto points = points_spectre(analyse.spectrum, analyse.peaks, f0, f1, colonnes, active.logFreq);
        for (QPointF &p : points) {
            if (active.logFreq)
                p.setX(std::log10(std::max(p.x(), 1e-3)));
            const double amplitude = p.y() * facteur;
            p.setY(active.db ? en_decibels(amplitude) : amplitude);
        }
        return points;
    };
    m_points = prepare(a, 1.0);
    m_pointsReference.clear();
    if (reference_compatible()) {
        double facteur = 1.0;
        if (m_normalise) {
            const double maximumReference = m_reference->amplitude_max();
            if (maximumReference > 0)
                facteur = a.amplitude_max() / maximumReference;
        }
        m_pointsReference = prepare(*m_reference, facteur);
    }
    if (active.autoScaleY) {
        auto bornes = extrema_visibles(m_points);
        if (const auto ref = extrema_visibles(m_pointsReference)) {
            if (bornes)
                bornes = std::pair(std::min(bornes->first, ref->first), std::max(bornes->second, ref->second));
            else
                bornes = ref;
        }
        if (bornes) {
            const auto [lo, hi] = *bornes;
            if (active.db)
                return {std::max(lo - 3.0, hi - DB_RANGE), hi + 3.0};
            return {0.0, hi > 0.0 ? hi * 1.15 : MIN_AMPLITUDE};
        }
    }
    double maxA = std::max(a.amplitude_max(), MIN_AMPLITUDE);
    if (reference_compatible() && !m_normalise)
        maxA = std::max(maxA, m_reference->amplitude_max());
    if (active.db)
        return {en_decibels(maxA) - DB_RANGE, en_decibels(maxA) + 6.0};
    return {0.0, maxA * 1.15};
}

QString SpectrumPlot::legende_axe_x() const
{
    return textes_interface()->axisFreq;
}

QString SpectrumPlot::legende_axe_y() const
{
    return echelle_decibels() ? textes_interface()->axisDb : textes_interface()->axisAmplitude;
}

std::vector<PlotWidget::Tick> SpectrumPlot::graduations_x(double x0, double x1, double pixels) const
{
    if (!axe_logarithmique())
        return PlotWidget::graduations_x(x0, x1, pixels);
    // axe logarithmique : 1, 2, 5 × 10ⁿ
    std::vector<Tick> ticks;
    for (double m : graduations_log(x0, x1))
        ticks.push_back({m, texte_frequence_court(std::pow(10.0, m), textes_interface()->decimal)});
    return ticks;
}

QString SpectrumPlot::texte_survol(QPointF data) const
{
    const QChar d = textes_interface()->decimal;
    const double f = depuis_x(data.x());
    QString text = u"f = %1\n%2"_s.arg(texte_frequence(f, d), texte_amplitude(data.y()));
    if (const auto peak = pic_le_plus_proche(analyse_courante()->peaks, f, axe_logarithmique())) {
        const double y = echelle_decibels() ? en_decibels(peak->amplitude) : peak->amplitude;
        text += u"\n\n%1\nf = %2\n%3"_s.arg(textes_interface()->nearestPeak, texte_frequence(peak->freq, d), texte_amplitude(y));
    }
    return text;
}

void SpectrumPlot::dessine_donnees(QPainter &painter, const QRectF &r)
{
    const dsp::Analysis &a = *analyse_courante();
    const Settings active = m_session.reglages_actifs();
    const QPalette pal = palette();
    const double s = echelle_interface();
    const QColor lineColor = couleur_spectre(pal);
    const double plancher = plage_y().first;
    const auto trace = [&](const std::vector<QPointF> &points) {
        QPolygonF line;
        line.reserve(qsizetype(points.size()));
        for (const QPointF &p : points)
            line << QPointF(ecran_x(p.x(), r), ecran_y(std::max(p.y(), plancher), r));
        trace_courbe(painter, line);
    };
    painter.setPen(QPen(lineColor, 1.2 * s));
    trace(m_points);
    if (!m_pointsReference.empty()) {
        painter.setPen(QPen(couleur_curseur_t(pal), 1.5 * s, Qt::DashLine));
        trace(m_pointsReference);
    }

    // repères des harmoniques n × f₁ (mode expert)
    if (active.expert && active.harmonicMarks) {
        QFont rank = font();
        rank.setWeight(TITLE_WEIGHT);
        painter.setFont(rank);
        const QFontMetricsF fm(rank);
        const QColor harmonic = couleur_harmonique(pal);
        for (const dsp::Harmonic &h : m_session.harmoniques(&a)) {
            const double x = ecran_x(vers_x(h.expected), r);
            const QColor color = h.peak ? harmonic : attenuee(harmonic, 0.4);
            QPen dashed(color, 1.0 * s, Qt::CustomDashLine);
            dashed.setDashPattern({3.0, 5.0});
            painter.setPen(dashed);
            painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            painter.setPen(color);
            painter.drawText(QRectF(x - 20.0, r.top() + 1.0, 40.0, fm.height()), Qt::AlignHCenter | Qt::AlignTop,
                             QString::number(h.rank));
        }
    }
    // pics repérés (mode expert)
    const auto ordonnee = [&](double amplitude) {
        return ecran_y(std::max(active.db ? en_decibels(amplitude) : amplitude, plancher), r);
    };
    if (active.expert) {
        painter.setPen(QPen(lineColor, 1.2 * s));
        painter.setBrush(Qt::NoBrush);
        for (const dsp::Peak &p : a.peaks)
            painter.drawEllipse(QPointF(ecran_x(vers_x(p.freq), r), ordonnee(p.amplitude)), 3.0 * s, 3.0 * s);
    }
    // le pic le plus proche de la souris, marqué d'un rond plein
    if (m_selection) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(couleur_curseur_t(pal), 2 * s));
        painter.drawEllipse(QPointF(ecran_x(vers_x(m_selection->freq), r), ordonnee(m_selection->amplitude)), 6 * s, 6 * s);
    }
    if (const auto pic = pic_survole()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(couleur_curseur_t(pal));
        painter.drawEllipse(QPointF(ecran_x(vers_x(pic->freq), r), ordonnee(pic->amplitude)), 3.5 * s,
                            3.5 * s);
    }
}

std::optional<dsp::Peak> SpectrumPlot::pic_a_la_position(QPointF position) const
{
    const auto *analyse = analyse_courante();
    if (!analyse || !cadre_graphique().contains(position))
        return std::nullopt;
    return pic_le_plus_proche(analyse->peaks, depuis_x(donnee_x(position.x())), axe_logarithmique());
}

std::optional<dsp::Peak> SpectrumPlot::pic_survole() const
{
    return survol() ? pic_a_la_position(*survol()) : std::nullopt;
}

void SpectrumPlot::selectionne(std::optional<dsp::Peak> pic)
{
    if (pic != m_selection) {
        m_selection = pic;
        emit selection_changee();
        update();
    }
}

void SpectrumPlot::gauche_appuye(QPointF pos)
{
    m_appui = pos;
}

void SpectrumPlot::gauche_relache(QPointF pos)
{
    const auto appui = std::exchange(m_appui, {});
    if (!appui || (pos - *appui).manhattanLength() > 6)
        return;
    differe_clic([this, pic = pic_a_la_position(pos)] { selectionne(pic); });
}

double SpectrumPlot::centre_zoom_clavier() const
{
    return m_selection ? vers_x(m_selection->freq) : PlotWidget::centre_zoom_clavier();
}

void SpectrumPlot::keyPressEvent(QKeyEvent *event)
{
    const auto *a = analyse_courante();
    const int key = event->key();
    termine_clic();
    if (key == Qt::Key_Escape) {
        selectionne(std::nullopt);
        return;
    }
    if (!a || (key != Qt::Key_Left && key != Qt::Key_Right)) {
        PlotWidget::keyPressEvent(event);
        return;
    }
    const auto first = std::ranges::lower_bound(a->peaks, depuis_x(vue_x0()), {}, &dsp::Peak::freq);
    const auto end = std::ranges::upper_bound(a->peaks, depuis_x(vue_x1()), {}, &dsp::Peak::freq);
    if (first == end)
        return;
    auto next = first;
    if (key == Qt::Key_Right && m_selection) {
        next = std::min(std::ranges::upper_bound(first, end, m_selection->freq, {}, &dsp::Peak::freq), end - 1);
    } else if (key == Qt::Key_Left) {
        next = m_selection ? std::ranges::lower_bound(first, end, m_selection->freq, {}, &dsp::Peak::freq) : end;
        if (next != first)
            --next;
    }
    selectionne(*next);
}

void SpectrumPlot::clic_droit(QPointF pos)
{
    if (!m_session.reglages_actifs().expert || !analyse_courante())
        return;
    const QChar d = textes_interface()->decimal;
    const std::optional<dsp::Peak> peak = pic_a_la_position(pos);
    QMenu menu(this);
    if (peak) {
        auto *choisir = menu.addAction(icons::commande(icons::Commande::ChoisirPic),
                                       textes_interface()->setF0(texte_frequence(peak->freq, d)), this,
                                       [this, f = peak->freq] { emit fondamentale_choisie(f); });
        choisir->setIconVisibleInMenu(true);
    } else {
        menu.addAction(textes_interface()->noPeak)->setEnabled(false);
    }
    if (m_session.manualF0) {
        auto *automatique = menu.addAction(icons::commande(icons::Commande::DetectionAuto), textes_interface()->autoButton,
                                           this, [this] { emit fondamentale_choisie(std::nullopt); });
        automatique->setIconVisibleInMenu(true);
    }
    menu.exec(mapToGlobal(pos.toPoint()));
}
