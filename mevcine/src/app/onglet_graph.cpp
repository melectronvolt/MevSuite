#include "onglet_graph.h"

#include "app.h"
#include "calculs.h"
#include "dessin.h"
#include "format.h"
#include "i18n.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

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

// Pente commune à deux segments d'Hermite. La moyenne harmonique pondérée par les durées
// conserve le sens de variation de chaque coordonnée. Sa dérivée s'annule à un extremum.
double pente_monotone(double avant, double apres, double h1, double h2)
{
    if (avant == 0.0 || apres == 0.0 || std::signbit(avant) != std::signbit(apres))
        return 0.0;
    const double w1 = 2.0 * h2 + h1, w2 = h2 + 2.0 * h1;
    return (w1 + w2) / (w1 / avant + w2 / apres);
}

double norme2(QPointF p)
{
    return QPointF::dotProduct(p, p);
}

constexpr double RAYON_LECTURE = 10.0; // pixels à l’écran

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
    setMouseTracking(true);
    m_clic.setSingleShot(true);
    connect(&m_clic, &QTimer::timeout, this, [this] {
        if (isVisible())
            emit pointClique(m_imageCliquee);
    });
}

void Graphe::montre(const std::vector<Point> &points, const QString &titre, const QString &legende_x,
                    const QString &legende_y, int style, bool nouvelles_grandeurs, const QString &message)
{
    const bool vide = m_points.empty();
    const bool nouveaux_points = points != m_points;
    const bool nouveau_trace = nouveaux_points || ((style == 0) != (m_style == 0));
    if (nouvelles_grandeurs || nouveaux_points)
        m_clic.stop();
    if (nouveaux_points)
        m_points = points;
    if (nouvelles_grandeurs || nouveaux_points || style != m_style)
        affiche_lecture(std::nullopt);
    m_titre = titre;
    m_message = message;
    m_legende_x = legende_x;
    m_legende_y = legende_y;
    m_style = style;
    if (nouveau_trace)
        construit_courbe();
    if (nouvelles_grandeurs || vide || !m_vue.isValid())
        vue_complete();
    update();
}

void Graphe::vue_complete()
{
    affiche_lecture(std::nullopt);
    if (m_points.empty()) {
        m_vue = QRectF(0, 0, 1, 1);
        update();
        return;
    }
    double x0 = m_points.front().coordonnees.x(), x1 = x0, y0 = m_points.front().coordonnees.y(), y1 = y0;
    for (const auto &point : m_points) {
        const QPointF &p = point.coordonnees;
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

QTransform Graphe::vers_ecran(const QRectF &z) const
{
    QTransform repere;
    repere.translate(z.left(), z.bottom());
    repere.scale(z.width() / m_vue.width(), -z.height() / m_vue.height());
    repere.translate(-m_vue.left(), -m_vue.top());
    return repere;
}

QPointF Graphe::ecran(QPointF p) const
{
    return vers_ecran(zone()).map(p);
}

QPointF Graphe::valeur(QPointF e) const
{
    const QRectF z = zone();
    return {m_vue.left() + (e.x() - z.left()) / z.width() * m_vue.width(),
            m_vue.top() + (z.bottom() - e.y()) / z.height() * m_vue.height()};
}

void Graphe::construit_courbe()
{
    m_courbe.clear();
    m_troncons.clear();
    if (m_style == 0 || m_points.size() < 2)
        return;
    m_troncons.reserve(m_points.size() - 1);
    // La date paramètre les deux coordonnées, même quand l'abscisse est constante ou fait demi-tour.
    const auto duree = [this](size_t i) {
        const double dt = m_points[i + 1].date - m_points[i].date;
        return dt > 0.0 ? dt : 1.0;
    };
    QPointF tangente;
    for (size_t i = 0; i + 1 < m_points.size(); ++i) {
        const Point &a = m_points[i], &b = m_points[i + 1];
        if (b.image != a.image + 1)
            continue;
        const double h = duree(i);
        const QPointF pente = (b.coordonnees - a.coordonnees) / h;
        if (i == 0 || a.image != m_points[i - 1].image + 1) {
            m_courbe.moveTo(a.coordonnees);
            tangente = pente;
        }
        QPointF suivante = pente;
        if (i + 2 < m_points.size() && m_points[i + 2].image == b.image + 1) {
            const double h2 = duree(i + 1);
            const QPointF apres = (m_points[i + 2].coordonnees - b.coordonnees) / h2;
            suivante = {pente_monotone(pente.x(), apres.x(), h, h2),
                        pente_monotone(pente.y(), apres.y(), h, h2)};
        }
        const auto &s = m_troncons.emplace_back(i, a.coordonnees, a.coordonnees + tangente * (h / 3.0),
                                                b.coordonnees - suivante * (h / 3.0), b.coordonnees);
        m_courbe.cubicTo(s.controle1, s.controle2, s.fin);
        tangente = suivante;
    }
}

QPointF Graphe::Troncon::valeur(double u) const
{
    const auto melange = [u](QPointF a, QPointF b) { return a + (b - a) * u; };
    const QPointF a = melange(debut, controle1), b = melange(controle1, controle2), c = melange(controle2, fin);
    return melange(melange(a, b), melange(b, c));
}

double Graphe::Troncon::distance_carree(QPointF souris, const QTransform &repere, const QRectF &cadre, double limite) const
{
    const auto cherche = [&](auto &suite, double u0, QPointF a, double u1, QPointF b) -> void {
        // Par monotonie de chaque coordonnée, tout le morceau est dans le rectangle de ses extrémités.
        const QPointF proche(std::clamp(souris.x(), std::min(a.x(), b.x()), std::max(a.x(), b.x())),
                             std::clamp(souris.y(), std::min(a.y(), b.y()), std::max(a.y(), b.y())));
        if (norme2(proche - souris) >= limite || !cadre.contains(proche))
            return;
        const QPointF ab = b - a;
        const double longueur2 = norme2(ab);
        if (longueur2 <= 0.25 || u1 - u0 <= 1e-10) {
            const double fraction = longueur2 == 0.0 ? 0.0 :
                std::clamp(QPointF::dotProduct(souris - a, ab) / longueur2, 0.0, 1.0);
            const double u = std::lerp(u0, u1, fraction);
            const QPointF candidat = repere.map(valeur(u));
            const double d = norme2(candidat - souris);
            if (d < limite && cadre.contains(candidat))
                limite = d;
            return;
        }
        const double u = (u0 + u1) / 2.0;
        const QPointF milieu = repere.map(valeur(u));
        suite(suite, u0, a, u, milieu);
        suite(suite, u, milieu, u1, b);
    };
    cherche(cherche, 0.0, repere.map(debut), 1.0, repere.map(fin));
    return limite;
}

const Graphe::Point *Graphe::point_proche(std::span<const Point> points, QPointF souris, const QTransform &repere,
                                         double limite) const
{
    const Point *proche = nullptr;
    for (const Point &point : points) {
        const double d = norme2(repere.map(point.coordonnees) - souris);
        if (d < limite && m_vue.contains(point.coordonnees)) {
            limite = d;
            proche = &point;
        }
    }
    return proche;
}

std::optional<Graphe::Point> Graphe::lit(QPointF souris) const
{
    const QRectF z = zone();
    if (m_points.empty() || !z.contains(souris))
        return std::nullopt;
    const QTransform repere = vers_ecran(z);
    double distance2 = RAYON_LECTURE * RAYON_LECTURE;
    if (const Point *p = point_proche(m_points, souris, repere, distance2))
        return *p;
    // Le trait désigne deux mesures voisines. La lecture reste sur une extrémité visible de ce tronçon.
    const Point *mesure = nullptr;
    for (const Troncon &s : m_troncons) {
        const double d = s.distance_carree(souris, repere, z, distance2);
        if (d < distance2) {
            const auto extremites = std::span(m_points).subspan(s.premier, 2);
            if (const Point *p = point_proche(extremites, souris, repere, std::numeric_limits<double>::infinity())) {
                mesure = p;
                distance2 = d;
            }
        }
    }
    return mesure ? std::optional(*mesure) : std::nullopt;
}

void Graphe::affiche_lecture(std::optional<Point> lecture)
{
    if (lecture != m_lecture) {
        m_lecture = lecture;
        if (!m_glisse) {
            if (lecture)
                setCursor(Qt::CrossCursor);
            else
                unsetCursor();
        }
        emit lectureChangee();
        update();
    }
}

void Graphe::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Window));
    const QRectF z = zone();
    p.fillRect(z, pal.color(QPalette::Base));
    const QFontMetricsF fm(font());
    // les couleurs des graphiques de la suite : fond Base, cadre Mid, grille Text à 30/255
    // d'opacité, graduations et titres Text
    QColor grille = pal.color(QPalette::Text);
    grille.setAlpha(30);
    const QColor texte = pal.color(QPalette::Text);

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
    const QTransform repere = vers_ecran(z);
    const double pas_x = pas_rond(m_vue.width(), z.width() / (fm.horizontalAdvance(u"0000000"_s) + 10.0));
    for (auto k = static_cast<long long>(std::ceil(m_vue.left() / pas_x)); k * pas_x <= m_vue.right(); ++k) {
        const double x = k * pas_x;
        const double sx = repere.map(QPointF(x, 0)).x();
        p.setPen(grille);
        p.drawLine(QPointF(sx, z.top()), QPointF(sx, z.bottom()));
        p.setPen(texte);
        p.drawText(QRectF(sx - 60.0, z.bottom() + 2.0, 120.0, fm.height()), Qt::AlignHCenter | Qt::AlignTop,
                   graduation(x, pas_x));
    }
    const double pas_y = pas_rond(m_vue.height(), z.height() / (2.5 * fm.height()));
    for (auto k = static_cast<long long>(std::ceil(m_vue.top() / pas_y)); k * pas_y <= m_vue.bottom(); ++k) {
        const double y = k * pas_y;
        const double sy = repere.map(QPointF(0, y)).y();
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
    if (m_style != 0) {
        p.setPen(QPen(Qt::gray, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(repere.map(m_courbe));
    }
    const QColor bleu = dessin::sombre(pal) ? QColor(120, 200, 255) : QColor(20, 90, 160);
    if (m_style != 2)
        p.setPen(QPen(bleu, 1.5));
    for (const auto &point : m_points) {
        const QPointF e = repere.map(point.coordonnees);
        if (m_style == 2) {
            // La même épaisseur que le trait rend aussi visibles les points isolés ou confondus.
            p.drawPoint(e);
        } else {
            p.drawLine(e - QPointF(5, 0), e + QPointF(5, 0));
            p.drawLine(e - QPointF(0, 5), e + QPointF(0, 5));
        }
    }
    if (m_lecture) {
        const QPointF e = repere.map(m_lecture->coordonnees);
        p.setPen(QPen(bleu, 1.0, Qt::DashLine));
        p.drawLine(QPointF(z.left(), e.y()), e);
        p.drawLine(QPointF(e.x(), z.bottom()), e);
        p.setPen(QPen(bleu, 1.8));
        p.drawEllipse(e, 5.0, 5.0);
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
    const QRectF vue(QPointF(c.x() + (m_vue.left() - c.x()) * k, c.y() + (m_vue.top() - c.y()) * k),
                     QPointF(c.x() + (m_vue.right() - c.x()) * k, c.y() + (m_vue.bottom() - c.y()) * k));
    if (!vue.isValid() || !std::isfinite(vue.width()) || !std::isfinite(vue.height()))
        return;
    m_vue = vue;
    if (!m_glisse)
        affiche_lecture(lit(event->position()));
    update();
    event->accept();
}

void Graphe::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_points.empty()) {
        m_appui = event->position();
        m_glisse = event->position();
        m_deplace = false;
        affiche_lecture(std::nullopt);
        setCursor(Qt::ClosedHandCursor);
    }
}

void Graphe::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_glisse) {
        affiche_lecture(lit(event->position()));
        return;
    }
    if (!m_deplace && (event->position() - m_appui).manhattanLength() < 4.0)
        return;
    m_deplace = true;
    m_clic.stop();
    const QPointF d = valeur(event->position()) - valeur(*m_glisse);
    m_vue.translate(-d);
    m_glisse = event->position();
    update();
}

void Graphe::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_glisse)
        return;
    const auto lecture = lit(event->position());
    if (!m_deplace && (event->position() - m_appui).manhattanLength() < 4 && lecture &&
        norme2(ecran(lecture->coordonnees) - event->position()) < RAYON_LECTURE * RAYON_LECTURE) {
        m_imageCliquee = lecture->image;
        m_clic.start(QApplication::doubleClickInterval());
    }
    m_glisse.reset();
    unsetCursor();
    affiche_lecture(lecture);
}

void Graphe::mouseDoubleClickEvent(QMouseEvent *)
{
    m_clic.stop();
    m_glisse.reset();
    unsetCursor();
    vue_complete();
}

void Graphe::leaveEvent(QEvent *event)
{
    affiche_lecture(std::nullopt);
    QWidget::leaveEvent(event);
}

QPixmap Graphe::image_du_trace()
{
    const QScopedValueRollback garde(m_lecture, std::optional<Point>{});
    return grab();
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
    m_enregistrer->setIcon(icones::commande(icones::Commande::Enregistrer));
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
    connect(m_graphe, &Graphe::pointClique, this, [this](int i) {
        QString nom = m_app.graph.y.value_or(QString());
        if (nom == u"t")
            nom = m_app.graph.x.value_or(QString());
        const auto numero_objet = [](const QString &grandeur) {
            qsizetype debut = 0;
            while (debut < grandeur.size() && !grandeur[debut].isDigit())
                ++debut;
            return std::max(1, grandeur.mid(debut).toInt());
        };
        const int objet = numero_objet(nom);
        const Position pos = m_app.pointage.point_de_l_objet(objet, i);
        if (!pos)
            return;
        const QImage image = m_app.apercu_point(i, objet, {480, 320});
        const Vecteur pos_reelle = m_app.pointage.position_dans_repere(*pos);
        auto *boite = new QDialog(this);
        boite->setAttribute(Qt::WA_DeleteOnClose);
        boite->setWindowTitle(trad("Image {} · objet {}", i + 1, objet));
        auto *layout = new QVBoxLayout(boite);
        if (!image.isNull()) {
            auto *vue = new QLabel(boite);
            vue->setPixmap(QPixmap::fromImage(image));
            layout->addWidget(vue);
        }
        layout->addWidget(new QLabel(u"t = %1 s     x = %2 %4     y = %3 %4"_s.arg(
            nombre_local(m_app.pointage.date_relative(i), 5), nombre_local(pos_reelle.x, 5),
            nombre_local(pos_reelle.y, 5), m_app.pointage.unite()), boite));
        QList<int> objets_expliques;
        const Pointage &p = m_app.pointage;
        for (const auto &axe : {m_app.graph.y, m_app.graph.x}) {
            if (!axe || !(axe->startsWith(u'v') || axe->startsWith(u'a')))
                continue;
            const int obj = numero_objet(*axe);
            if (objets_expliques.contains(obj) || !p.cinematique_px(obj, i))
                continue;
            objets_expliques << obj;
            QStringList lignes{trad("Calcul pour l'objet {} · intervalle de {} s", obj, nombre_local(p.date(i + 1) - p.date(i - 1), 6))};
            for (int j = i - 1; j <= i + 1; ++j) {
                const Vecteur q = p.position_dans_repere(*p.point_de_l_objet(obj, j));
                lignes << trad("Image {} · t = {} s · x = {} {} · y = {} {}", j + 1, nombre_local(p.date_relative(j), 6),
                               nombre_local(q.x, 6), p.unite(), nombre_local(q.y, 6), p.unite());
            }
            layout->addWidget(new QLabel(lignes.join(u'\n'), boite));
        }
        boite->open();
    });
    l->addWidget(m_graphe, 1);
    // L'aide et la lecture occupent la même place pour garder le graphe immobile au survol.
    m_indications = new QStackedWidget(this);
    m_indications->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_aide = new QLabel(m_indications);
    m_aide->setWordWrap(true);
    dessin::discret(m_aide);
    m_lecture = new QLabel(m_indications);
    m_lecture->setTextFormat(Qt::PlainText);
    m_lecture->setWordWrap(true);
    m_indications->addWidget(m_aide);
    m_indications->addWidget(m_lecture);
    connect(m_graphe, &Graphe::lectureChangee, this, &OngletGraph::actualise_lecture);
    l->addWidget(m_indications);
}

void OngletGraph::enregistre_image()
{
    m_app.enregistre_image(m_graphe->image_du_trace(), trad("Enregistrer le graphique"), trad("Image PNG") + u" (*.png)"_s, u"png"_s);
}

void OngletGraph::actualise_lecture()
{
    const auto &lecture = m_graphe->lecture();
    if (!lecture || !m_app.graph.x || !m_app.graph.y) {
        m_indications->setCurrentWidget(m_aide);
        return;
    }
    const auto coordonnee = [this](const QString &nom, double valeur) {
        return u"%1 = %2 %3"_s.arg(nom_affiche(nom), nombre_local(valeur, 5),
                                   unite(nom, m_app.pointage.echelle.est_definie()));
    };
    const QString origine = trad("Point de l'image {}", lecture->image + 1);
    m_lecture->setText(u"%1    ·    %2    ·    %3"_s.arg(coordonnee(*m_app.graph.x, lecture->coordonnees.x()),
                                                        coordonnee(*m_app.graph.y, lecture->coordonnees.y()), origine));
    m_indications->setCurrentWidget(m_lecture);
}

void OngletGraph::actualise()
{
    App &a = m_app;
    const auto grandeurs = grandeurs_disponibles(a.pointage.nombre_objets());
    QStringList noms;
    for (const auto &grandeur : grandeurs)
        noms << grandeur.nom;
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
    m_aide->setText(trad("Survolez pour lire une mesure · Cliquez sur un point pour voir son image · Zoomez à la molette · Glissez pour déplacer · Double-cliquez pour tout voir"));
    // une grandeur manque : le graphique vide, avec les légendes déjà choisies
    if (!gx || !gy) {
        m_grandeurs.reset();
        m_enregistrer->setEnabled(false);
        m_indications->setVisible(false);
        m_graphe->montre({}, QString(), gx ? legende(*gx, metre) : QString(), gy ? legende(*gy, metre) : QString(),
                         a.graph.style, true, trad("Choisissez en haut la grandeur à tracer, et en fonction de quoi."));
        return;
    }
    const Grandeur &abscisse = grandeurs[noms.indexOf(*gx)];
    const Grandeur &ordonnee = grandeurs[noms.indexOf(*gy)];
    std::vector<Graphe::Point> points;
    for (const auto &[index, positions] : a.pointage.positions_par_image()) {
        const auto x = abscisse.valeur(a.pointage, index);
        const auto y = ordonnee.valeur(a.pointage, index);
        if (x && y)
            points.push_back({QPointF(*x, *y), index, a.pointage.date(index)});
    }
    const std::pair grandeurs_choisies{*gx, *gy};
    m_graphe->montre(points, trad("{} en fonction de {}", nom_affiche(*gy), nom_affiche(*gx)), legende(*gx, metre),
                     legende(*gy, metre), a.graph.style, m_grandeurs != grandeurs_choisies,
                     trad("Pas encore de valeur à tracer : pointez d'autres images."));
    m_enregistrer->setEnabled(!points.empty());
    actualise_lecture();
    m_indications->setVisible(!points.empty());
    m_grandeurs = grandeurs_choisies;
}
