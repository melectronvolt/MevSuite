#include "onglet_graph.h"

#include "app.h"
#include "calculs.h"
#include "dessin.h"
#include "format.h"
#include "i18n.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <format>

using namespace Qt::StringLiterals;

namespace {

/// Pas « rond » (1, 2 ou 5 × 10ⁿ) pour environ `nombre` graduations sur `etendue`
double pas_rond(double etendue, double nombre)
{
    const double brut = etendue / std::max(nombre, 1.0);
    if (!(brut > 0.0) || !std::isfinite(brut))
        return 1.0;
    const double ordre = std::pow(10.0, std::floor(std::log10(brut)));
    const double n = brut / ordre;
    return (n <= 1.0 ? 1.0 : n <= 2.0 ? 2.0 : n <= 5.0 ? 5.0 : 10.0) * ordre;
}

/// un nombre de graduation, avec les décimales du pas
QString graduation(double v, double pas)
{
    const int decimales = std::clamp(-static_cast<int>(std::floor(std::log10(pas) + 1e-9)), 0, 12);
    if (std::abs(v) < pas * 1e-9)
        v = 0.0;
    return QString::fromStdString(std::format("{:.{}f}", v, decimales)).replace(u'.', separateur_decimal());
}

/// légende d'un axe, comme « vx (m/s) »
QString legende(const QString &nom, bool metre)
{
    return u"%1 (%2)"_s.arg(nom_affiche(nom), unite(nom, metre));
}

} // namespace

QString nom_affiche(const QString &nom)
{
    qsizetype n = 0;
    while (n < nom.size() && !(nom[n] >= u'0' && nom[n] <= u'9'))
        ++n;
    const QString lettres = nom.left(n);
    return lettres == u"v" || lettres == u"a" ? u"|%1|"_s.arg(nom) : nom;
}

// ------------------------------------------------------------------------------------------------
// Graphe
// ------------------------------------------------------------------------------------------------

Graphe::Graphe(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(300, 200);
}

void Graphe::montre(const std::vector<QPointF> &points, const QString &titre, const QString &legende_x,
                    const QString &legende_y, int style, bool nouvelles_grandeurs, const QString &message)
{
    const bool vide = m_points.empty();
    m_points = points;
    m_titre = titre;
    m_message = message;
    m_legende_x = legende_x;
    m_legende_y = legende_y;
    m_style = style;
    if (nouvelles_grandeurs || vide || !m_vue.isValid())
        vue_complete();
    update();
}

void Graphe::vue_complete()
{
    if (m_points.empty()) {
        m_vue = QRectF(0, 0, 1, 1);
        return;
    }
    double x0 = m_points[0].x(), x1 = x0, y0 = m_points[0].y(), y1 = y0;
    for (const QPointF &p : m_points) {
        x0 = std::min(x0, p.x());
        x1 = std::max(x1, p.x());
        y0 = std::min(y0, p.y());
        y1 = std::max(y1, p.y());
    }
    const auto marge = [](double &a, double &b) {
        const double e = b - a;
        if (e <= std::max(std::abs(a), std::abs(b)) * 1e-12) {
            const double d = a == 0.0 ? 0.5 : std::abs(a) * 0.1;
            a -= d;
            b += d;
        } else {
            a -= 0.05 * e;
            b += 0.05 * e;
        }
    };
    marge(x0, x1);
    marge(y0, y1);
    m_vue = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    update();
}

QRectF Graphe::zone() const
{
    const QFontMetricsF fm(font());
    const double gauche = fm.horizontalAdvance(u"-0,000000"_s) + fm.height() + 14.0;
    const double bas = 2.0 * fm.height() + 14.0;
    // à droite, une demi-étiquette : la dernière graduation n'est pas coupée ; en haut, une demi-ligne
    const double droite = fm.horizontalAdvance(u"-0,0000"_s) / 2.0 + 4.0;
    // en haut, le titre
    const double haut = 1.6 * fm.height() + 6.0;
    return QRectF(gauche, haut, std::max(width() - gauche - droite, 10.0), std::max(height() - bas - haut, 10.0));
}

QPointF Graphe::ecran(QPointF p) const
{
    const QRectF z = zone();
    return {z.left() + (p.x() - m_vue.left()) / m_vue.width() * z.width(),
            z.bottom() - (p.y() - m_vue.top()) / m_vue.height() * z.height()};
}

QPointF Graphe::valeur(QPointF e) const
{
    const QRectF z = zone();
    return {m_vue.left() + (e.x() - z.left()) / z.width() * m_vue.width(),
            m_vue.top() + (z.bottom() - e.y()) / z.height() * m_vue.height()};
}

void Graphe::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Window));
    const QRectF z = zone();
    p.fillRect(z, pal.color(QPalette::Base));
    const QFontMetricsF fm(font());
    // les couleurs des graphiques de la suite : fond Base, cadre et grille Mid, textes WindowText
    const QColor grille = pal.color(QPalette::Mid);
    const QColor texte = pal.color(QPalette::WindowText);

    // le titre, en demi-gras comme ceux de la suite, au-dessus du graphique
    if (!m_titre.isEmpty()) {
        QFont titre = font();
        titre.setWeight(QFont::DemiBold);
        p.setFont(titre);
        p.setPen(texte);
        p.drawText(QRectF(z.left(), 2.0, z.width(), z.top() - 4.0), Qt::AlignCenter, m_titre);
        p.setFont(font());
    }

    // graphique vide : la grille seule, sans graduations, et ce qu'il faut faire au milieu
    if (m_points.empty()) {
        p.setPen(grille);
        for (int k = 1; k < 10; ++k)
            p.drawLine(QPointF(z.left() + z.width() * k / 10.0, z.top()), QPointF(z.left() + z.width() * k / 10.0, z.bottom()));
        for (int k = 1; k < 6; ++k)
            p.drawLine(QPointF(z.left(), z.top() + z.height() * k / 6.0), QPointF(z.right(), z.top() + z.height() * k / 6.0));
        p.setPen(pal.color(QPalette::Mid));
        p.drawRect(z);
        // le message, sur un cartouche du fond : la grille ne le barre pas
        const QRectF place = z.adjusted(20, 20, -20, -20);
        QRectF cadre = fm.boundingRect(place, Qt::AlignCenter | Qt::TextWordWrap, m_message);
        cadre.adjust(-10, -6, 10, 6);
        p.fillRect(cadre, pal.color(QPalette::Base));
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(place, Qt::AlignCenter | Qt::TextWordWrap, m_message);
        p.setPen(texte);
        p.drawText(QRectF(z.left(), z.bottom() + fm.height() + 6.0, z.width(), fm.height()), Qt::AlignHCenter, m_legende_x);
        p.save();
        p.translate(fm.height() / 2.0 + 2.0, z.center().y());
        p.rotate(-90.0);
        p.drawText(QRectF(-z.height() / 2.0, -fm.height() / 2.0, z.height(), fm.height()), Qt::AlignCenter, m_legende_y);
        p.restore();
        return;
    }

    // graduations et grille
    const double pas_x = pas_rond(m_vue.width(), z.width() / (fm.horizontalAdvance(u"0000000"_s) + 10.0));
    for (auto k = static_cast<long long>(std::ceil(m_vue.left() / pas_x)); k * pas_x <= m_vue.right(); ++k) {
        const double x = k * pas_x;
        const double sx = ecran({x, 0}).x();
        p.setPen(grille);
        p.drawLine(QPointF(sx, z.top()), QPointF(sx, z.bottom()));
        p.setPen(texte);
        p.drawText(QRectF(sx - 60.0, z.bottom() + 2.0, 120.0, fm.height()), Qt::AlignHCenter | Qt::AlignTop,
                   graduation(x, pas_x));
    }
    const double pas_y = pas_rond(m_vue.height(), z.height() / (2.5 * fm.height()));
    for (auto k = static_cast<long long>(std::ceil(m_vue.top() / pas_y)); k * pas_y <= m_vue.bottom(); ++k) {
        const double y = k * pas_y;
        const double sy = ecran({0, y}).y();
        p.setPen(grille);
        p.drawLine(QPointF(z.left(), sy), QPointF(z.right(), sy));
        p.setPen(texte);
        p.drawText(QRectF(0.0, sy - fm.height() / 2, z.left() - 4.0, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   graduation(y, pas_y));
    }
    p.setPen(pal.color(QPalette::Mid));
    p.drawRect(z);

    // légendes des axes
    p.setPen(texte);
    p.drawText(QRectF(z.left(), z.bottom() + fm.height() + 6.0, z.width(), fm.height()), Qt::AlignHCenter, m_legende_x);
    p.save();
    p.translate(fm.height() / 2.0 + 2.0, z.center().y());
    p.rotate(-90.0);
    p.drawText(QRectF(-z.height() / 2.0, -fm.height() / 2.0, z.height(), fm.height()), Qt::AlignCenter, m_legende_y);
    p.restore();

    // les points et les lignes
    p.setClipRect(z);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_style != 0 && m_points.size() > 1) {
        QPolygonF ligne;
        for (const QPointF &q : m_points)
            ligne << ecran(q);
        p.setPen(QPen(Qt::gray, 1.5));
        p.drawPolyline(ligne);
    }
    if (m_style != 2) {
        // un bleu lisible sur le fond clair comme sur le fond sombre
        p.setPen(QPen(dessin::sombre(pal) ? QColor(120, 200, 255) : QColor(20, 90, 160), 1.5));
        for (const QPointF &q : m_points) {
            const QPointF e = ecran(q);
            p.drawLine(e - QPointF(5, 0), e + QPointF(5, 0));
            p.drawLine(e - QPointF(0, 5), e + QPointF(0, 5));
        }
    }
}

void Graphe::wheelEvent(QWheelEvent *event)
{
    const double crans = event->angleDelta().y() / 120.0;
    if (crans == 0.0 || !m_vue.isValid() || m_points.empty())
        return;
    // zoom autour du point sous la souris
    const double k = std::pow(1.0 / 1.15, crans);
    const QPointF c = valeur(event->position());
    m_vue = QRectF(QPointF(c.x() + (m_vue.left() - c.x()) * k, c.y() + (m_vue.top() - c.y()) * k),
                   QPointF(c.x() + (m_vue.right() - c.x()) * k, c.y() + (m_vue.bottom() - c.y()) * k));
    update();
    event->accept();
}

void Graphe::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_points.empty()) {
        m_glisse = event->position();
        setCursor(Qt::ClosedHandCursor);
    }
}

void Graphe::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_glisse)
        return;
    const QPointF d = valeur(event->position()) - valeur(*m_glisse);
    m_vue.translate(-d);
    m_glisse = event->position();
    update();
}

void Graphe::mouseReleaseEvent(QMouseEvent *)
{
    m_glisse.reset();
    unsetCursor();
}

void Graphe::mouseDoubleClickEvent(QMouseEvent *)
{
    vue_complete();
}

// ------------------------------------------------------------------------------------------------
// OngletGraph
// ------------------------------------------------------------------------------------------------

OngletGraph::OngletGraph(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *l = new QVBoxLayout(this);
    auto *groupe = new QGroupBox(this);
    auto *lg = new QHBoxLayout(groupe);
    m_tracer = new QLabel(groupe);
    m_y = new QComboBox(groupe);
    m_en_fonction = new QLabel(groupe);
    m_x = new QComboBox(groupe);
    m_style_titre = new QLabel(groupe);
    m_style = new QComboBox(groupe);
    m_style->addItems({QString(), QString(), QString()});
    m_enregistrer = new QPushButton(groupe);
    const auto choix = [this](QComboBox *combo, std::optional<QString> Grapheur::*grandeur) {
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, grandeur](int i) {
            const QString nom = combo->itemData(i).toString();
            m_app.graph.*grandeur = nom.isEmpty() ? std::nullopt : std::optional(nom);
            m_app.rafraichit();
        });
    };
    choix(m_y, &Grapheur::y);
    choix(m_x, &Grapheur::x);
    connect(m_style, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_app.graph.style = std::max(i, 0);
        m_app.rafraichit();
    });
    connect(m_enregistrer, &QPushButton::clicked, this, [this] { enregistre_image(); });
    for (QWidget *w : {static_cast<QWidget *>(m_tracer), static_cast<QWidget *>(m_y), static_cast<QWidget *>(m_en_fonction),
                       static_cast<QWidget *>(m_x), static_cast<QWidget *>(m_style_titre), static_cast<QWidget *>(m_style),
                       static_cast<QWidget *>(m_enregistrer)})
        lg->addWidget(w);
    lg->addStretch(1);
    l->addWidget(groupe);
    // dessous, le graphique, toujours là ; son titre est dessiné avec lui (il est dans l'image
    // enregistrée) ; la ligne d'aide rappelle les gestes
    m_graphe = new Graphe(this);
    l->addWidget(m_graphe, 1);
    m_aide = new QLabel(this);
    m_aide->setWordWrap(true);
    dessin::discret(m_aide);
    l->addWidget(m_aide);
}

void OngletGraph::enregistre_image()
{
    m_app.enregistre_image(m_graphe, trad("Enregistrer le graphique"), trad("Image PNG") + u" (*.png)"_s, u"png"_s);
}

void OngletGraph::maj()
{
    App &a = m_app;
    const std::vector<Serie> series = grandeurs(a.pointage);
    QStringList noms{u"t"_s};
    for (const Serie &s : series)
        noms << s.nom;
    // une grandeur disparue, après un changement du nombre d'objets, est oubliée
    for (auto *choix : {&a.graph.x, &a.graph.y}) {
        if (*choix && !noms.contains(**choix))
            choix->reset();
    }
    m_tracer->setText(trad("Tracer :"));
    m_en_fonction->setText(trad("en fonction de :"));
    m_style_titre->setText(trad("Style :"));
    m_enregistrer->setText(trad("Enregistrer"));
    m_enregistrer->setToolTip(trad("Enregistrer le graphique comme image"));
    const QSignalBlocker b1(m_x), b2(m_y), b3(m_style);
    for (auto [combo, choix] : {std::pair{m_y, a.graph.y}, std::pair{m_x, a.graph.x}}) {
        if (combo->count() != noms.size() + 1) {
            combo->clear();
            combo->addItem(QString(), QString());
            for (const QString &n : noms)
                combo->addItem(nom_affiche(n), n);
        } else {
            for (qsizetype k = 0; k < noms.size(); ++k) {
                combo->setItemText(static_cast<int>(k) + 1, nom_affiche(noms[k]));
                combo->setItemData(static_cast<int>(k) + 1, noms[k]);
            }
        }
        combo->setItemText(0, trad("Choisir…"));
        combo->setCurrentIndex(choix ? static_cast<int>(noms.indexOf(*choix)) + 1 : 0);
    }
    const QString styles[] = {trad("Points seuls"), trad("Points et lignes"), trad("Lignes seules")};
    for (int i = 0; i < 3; ++i)
        m_style->setItemText(i, styles[i]);
    m_style->setCurrentIndex(a.graph.style);

    const auto gx = a.graph.x, gy = a.graph.y;
    const bool metre = a.pointage.echelle.est_definie();
    m_aide->setText(trad("molette : zoom · glisser : se déplacer · double-clic : tout voir"));
    // une grandeur manque : le graphique vide, avec les légendes déjà choisies
    if (!gx || !gy) {
        m_grandeurs.reset();
        m_enregistrer->setEnabled(false);
        m_aide->setVisible(false);
        m_graphe->montre({}, QString(), gx ? legende(*gx, metre) : QString(), gy ? legende(*gy, metre) : QString(),
                         a.graph.style, true, trad("Choisissez en haut la grandeur à tracer, et en fonction de quoi."));
        return;
    }
    // les dates depuis la première image pointée, comme dans le tableau
    const auto valeurs = [&](const QString &nom) {
        std::vector<std::optional<double>> v;
        if (nom == u"t") {
            for (int i = 0; i < a.pointage.len(); ++i)
                v.push_back(a.pointage.date_relative(i));
        } else {
            for (const Serie &s : series) {
                if (s.nom == nom)
                    v = s.valeurs;
            }
        }
        return v;
    };
    const auto vx = valeurs(*gx), vy = valeurs(*gy);
    std::vector<QPointF> points;
    for (size_t i = 0; i < std::min(vx.size(), vy.size()); ++i) {
        if (vx[i] && vy[i])
            points.emplace_back(*vx[i], *vy[i]);
    }
    const std::pair grandeurs_choisies{*gx, *gy};
    m_graphe->montre(points, trad("{} en fonction de {}", nom_affiche(*gy), nom_affiche(*gx)), legende(*gx, metre),
                     legende(*gy, metre), a.graph.style, m_grandeurs != grandeurs_choisies,
                     trad("Pas encore de valeur à tracer : pointez d'autres images."));
    m_enregistrer->setEnabled(!points.empty());
    m_aide->setVisible(!points.empty());
    m_grandeurs = grandeurs_choisies;
}
