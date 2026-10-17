#include "onglet_trajectoire.h"

#include "app.h"
#include "calculs.h"
#include "detect.h"
#include "dessin.h"
#include "format.h"
#include "i18n.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

std::optional<double> Trajectoire::echelle_saisie() const
{
    const auto v = parse_float(echelle_vitesse);
    if (v && *v > 0.0 && std::isfinite(*v))
        return v;
    return std::nullopt;
}

double arrondi_rond(double x)
{
    if (!(x > 0.0) || !std::isfinite(x))
        return 1.0;
    const double puissance = std::pow(10.0, std::floor(std::log10(x)));
    double meilleur = puissance;
    for (double m : {1.0, 2.0, 5.0, 10.0}) {
        if (std::abs(std::log(m * puissance / x)) < std::abs(std::log(meilleur / x)))
            meilleur = m * puissance;
    }
    return meilleur;
}

double echelle_vitesse_auto(const Pointage &p, QSize dimensions)
{
    double vmax = 0.0;
    for (const auto &objet : p.vecteurs_vitesse(1.0)) {
        for (const auto &[org, ext] : objet)
            vmax = std::max(vmax, (ext - org).norme());
    }
    if (vmax <= 0.0)
        return 1.0;
    return arrondi_rond(std::min(dimensions.width(), dimensions.height()) / 7.0 / vmax);
}

std::optional<Ralenti> Ralenti::nouveau(const Pointage &p, int obj, QSize dimensions)
{
    Ralenti r;
    r.obj = obj;
    for (int i = 0; i < p.len(); ++i) {
        if (const Position q = p.position(obj, i)) {
            r.images.push_back(i);
            r.positions.push_back(*q);
        }
    }
    if (r.positions.empty())
        return std::nullopt;
    const double w = dimensions.width(), h = dimensions.height();
    const auto min = [&](auto f) {
        double m = std::numeric_limits<double>::infinity();
        for (const Vecteur &q : r.positions)
            m = std::min(m, f(q));
        return std::max(m, 0.0);
    };
    const double agauche = std::floor(min([](Vecteur q) { return q.x; }));
    const double adroite = std::floor(min([&](Vecteur q) { return w - q.x - 1.0; }));
    const double dessus = std::floor(min([](Vecteur q) { return q.y; }));
    const double dessous = std::floor(min([&](Vecteur q) { return h - q.y - 1.0; }));
    r.taille = QSize(static_cast<int>(std::max(agauche + adroite, 1.0)), static_cast<int>(std::max(dessus + dessous, 1.0)));
    r.decalage = {agauche, dessus};
    return r;
}

// ------------------------------------------------------------------------------------------------
// DessinTrajectoire
// ------------------------------------------------------------------------------------------------

DessinTrajectoire::DessinTrajectoire(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DessinTrajectoire::mouseMoveEvent(QMouseEvent *event)
{
    m_souris = event->position();
    if (m_app.traj.vecteurs && m_app.traj.pres_souris)
        update();
}

void DessinTrajectoire::leaveEvent(QEvent *)
{
    m_souris.reset();
    update();
}

void DessinTrajectoire::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    App &a = m_app;
    const QSize d = a.dimensions();
    const dessin::Vue vue = dessin::Vue::ajuste(QRectF(rect()), d);
    const ModeTrajectoire mode = a.traj.mode;

    // le fond des graphiques de la suite (Base : blanc en thème clair, presque noir en sombre), et
    // l'encre du texte ; les couleurs des objets, éclaircies sur le fond sombre
    const QPalette pal = palette();
    const bool fonce = dessin::sombre(pal);
    const QColor feuille = pal.color(QPalette::Base), encre = pal.color(QPalette::Text);
    const auto couleur_objet = [fonce](int obj) {
        const QColor c = dessin::couleur(obj);
        if (!fonce)
            return c;
        // un tiers de blanc : le bleu et le vert foncé se voient sur le fond sombre
        return QColor((c.red() * 2 + 255) / 3, (c.green() * 2 + 255) / 3, (c.blue() * 2 + 255) / 3);
    };
    switch (mode) {
    case ModeTrajectoire::Trajectoire:
    case ModeTrajectoire::Chronogramme: painter.fillRect(vue.rect, feuille); break;
    case ModeTrajectoire::Chronophotographie: {
        const int i = std::clamp(a.traj.chrono_img, 1, std::max(a.image_max, 1));
        std::optional<QImage> img;
        if (a.video) {
            if (auto r = a.video->image(i))
                img = *r;
        }
        if (img)
            painter.drawImage(vue.rect, *img);
        else
            painter.fillRect(vue.rect, feuille);
        break;
    }
    }
    painter.setClipRect(vue.rect);

    // les pointages dans le référentiel choisi
    const Vecteur centre(d.width() / 2.0, d.height() / 2.0);
    const Pointage p = a.traj.referentiel == 0 ? a.pointage : a.pointage.dans_referentiel(a.traj.referentiel, centre);
    const QPointF origine = vue.ecran(p.origine);
    const double x1 = vue.rect.left() + 50.0, y1 = vue.rect.top() + 50.0;
    const Echelle &e = p.echelle;
    if (mode == ModeTrajectoire::Chronophotographie) {
        const QColor fond(90, 90, 90);
        const double x = vue.rect.right() - 150.0;
        // cadence variable : l'intervalle entre deux images n'est que l'intervalle habituel
        const QString dt = p.cadence_variable() ? trad("Δt habituel") : u"Δt"_s;
        dessin::texte(painter, {x, y1}, u"%1 = %2 s"_s.arg(dt, texte_date(p.delta_t, p.delta_t)), 14.0, Qt::white, fond,
                      false);
        const double t = p.date_relative(std::max(a.traj.chrono_img - 1, 0));
        dessin::texte(painter, {x, y1 + 50.0}, u"t = %1 s"_s.arg(texte_date(t, p.delta_t)), 14.0, Qt::white, fond, false);
        const QColor bleu(0, 0, 255);
        if (e.est_definie()) {
            const QPointF e1 = vue.ecran(a.pointage.echelle.p1), e2 = vue.ecran(a.pointage.echelle.p2);
            painter.setPen(QPen(bleu, 3.0));
            painter.drawLine(e1, e2);
            dessin::texte(painter, {e1.x(), (e1.y() + e2.y()) / 2.0 + 20.0},
                          u"d = %1 m"_s.arg(nombre_local(e.longueur_reelle_etalon, 4)), 15.0, bleu, std::nullopt, false);
        } else {
            dessin::texte(painter, {x1, y1 + 20.0}, trad("échelle non précisée"), 15.0, bleu, std::nullopt, false);
        }
    } else if (mode == ModeTrajectoire::Chronogramme) {
        painter.setPen(QPen(encre, 1.0));
        if (e.est_definie()) {
            const double longueur = e.longueur_pixel_etalon() * vue.echelle;
            painter.drawLine(QPointF(x1, y1 - 10), QPointF(x1, y1 + 10));
            painter.drawLine(QPointF(x1, y1), QPointF(x1 + longueur, y1));
            painter.drawLine(QPointF(x1 + longueur, y1 - 10), QPointF(x1 + longueur, y1 + 10));
            dessin::texte(painter, {x1 + longueur / 2.0, y1 + 30.0}, u"d = %1 m"_s.arg(nombre_local(e.longueur_reelle_etalon, 4)),
                          13.0, encre, std::nullopt, true);
        } else {
            dessin::texte(painter, {x1, y1 + 20.0}, trad("échelle non précisée"), 13.0, encre, std::nullopt, false);
        }
    }

    // les points : des croix nettes ; le numéro de l'objet, s'il y en a plusieurs
    QFont police = font();
    police.setPixelSize(12);
    painter.setFont(police);
    const bool plusieurs = p.nb_obj() > 1;
    for (int i = 0; i < p.len(); ++i) {
        const auto &ligne = p.ligne(i);
        for (size_t j = 0; j < ligne.size(); ++j) {
            if (!ligne[j])
                continue;
            const QPointF pos = vue.ecran(*ligne[j]);
            if (mode == ModeTrajectoire::Chronogramme) {
                dessin::croix(painter, pos, 4.0, encre);
                const double decal = pos.x() - vue.rect.left() - 25.0 < 0.0 ? 5.0 : -25.0;
                painter.setPen(encre);
                painter.drawText(pos + QPointF(-10.0 + decal, 15.0), u"M%1%2"_s.arg(QString(static_cast<qsizetype>(j), u'\'')).arg(i));
            } else {
                const QColor c = mode == ModeTrajectoire::Chronophotographie && a.video ? dessin::couleur(static_cast<int>(j) + 1)
                                                                                         : couleur_objet(static_cast<int>(j) + 1);
                dessin::croix(painter, pos, 5.0, c, 2.0);
                if (plusieurs) {
                    painter.setPen(c);
                    painter.drawText(pos + QPointF(-12.0, 14.0), QString::number(j + 1));
                }
            }
        }
    }

    // le repère et son origine
    if (mode != ModeTrajectoire::Chronogramme) {
        dessin::repere(painter, origine, p.sens_x, p.sens_y, dessin::VERT_REPERE, dessin::longueur_axes(vue), true);
        dessin::origine(painter, origine, p.sens_x, p.sens_y, dessin::VERT_REPERE);
    }

    // les vecteurs vitesse
    if (mode == ModeTrajectoire::Trajectoire && a.traj.vecteurs) {
        const double echelle = a.traj.echelle_saisie().value_or(echelle_vitesse_auto(p, d));
        const auto vecteurs = p.vecteurs_vitesse(echelle);
        for (size_t j = 0; j < vecteurs.size(); ++j) {
            for (const auto &[org, ext] : vecteurs[j]) {
                if (org == ext)
                    continue;
                const QPointF e1 = vue.ecran(org), e2 = vue.ecran(ext);
                // près de la souris seulement, si on l'a demandé
                if (a.traj.pres_souris) {
                    if (!m_souris)
                        continue;
                    const QPointF ecart = *m_souris - e1;
                    if (std::abs(ecart.x()) + std::abs(ecart.y()) > 20.0)
                        continue;
                }
                dessin::fleche(painter, e1, e2, couleur_objet(static_cast<int>(j) + 1), 2.0);
            }
        }
        // La légende, en bas à gauche : la flèche d'une vitesse ronde, d'environ 80 points d'écran
        const double px_ecran = echelle * vue.echelle;
        if (px_ecran > 0.0) {
            const double v = arrondi_rond(80.0 / px_ecran);
            const double longueur = v * px_ecran;
            const QPointF depart(vue.rect.left() + 20.0, vue.rect.bottom() - 18.0);
            const QString texte = u"%1 %2/s"_s.arg(nombre_local(v, 3), p.unite());
            QFont f = font();
            f.setPixelSize(13);
            const QFontMetricsF fm(f);
            const QRectF cadre(depart.x() - 8.0, depart.y() - fm.height() - 14.0,
                               std::max(longueur, fm.horizontalAdvance(texte)) + 16.0, fm.height() + 22.0);
            // un cartouche du fond, bordé, au-dessus des points ; texte et flèche à l'encre du texte
            QColor fond = feuille;
            fond.setAlpha(235);
            painter.setPen(QPen(pal.color(QPalette::Mid), 1.0));
            painter.setBrush(fond);
            painter.drawRoundedRect(cadre, 4, 4);
            dessin::fleche(painter, depart, depart + QPointF(longueur, 0), encre, 2.0);
            painter.setFont(f);
            painter.setPen(encre);
            painter.drawText(QPointF(depart.x(), depart.y() - 8.0), texte);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// OngletTrajectoire
// ------------------------------------------------------------------------------------------------

OngletTrajectoire::OngletTrajectoire(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    auto *controles = new QWidget(this);
    auto *lc = new QVBoxLayout(controles);
    lc->setContentsMargins(8, 6, 8, 6);
    const auto apres = [this](auto action) {
        return [this, action](auto &&...args) {
            action(args...);
            m_app.rafraichit();
        };
    };

    m_mode = new QComboBox(controles);
    m_mode->addItems({QString(), QString(), QString()});
    connect(m_mode, &QComboBox::currentIndexChanged, this,
            apres([this](int i) { m_app.traj.mode = static_cast<ModeTrajectoire>(i); }));
    lc->addWidget(m_mode);

    m_groupe_vecteurs = new QGroupBox(controles);
    auto *lv = new QVBoxLayout(m_groupe_vecteurs);
    m_titre_vecteurs = new QLabel(m_groupe_vecteurs);
    m_vecteurs = new QCheckBox(m_groupe_vecteurs);
    connect(m_vecteurs, &QCheckBox::toggled, this, apres([this](bool v) { m_app.traj.vecteurs = v; }));
    m_pres = new QRadioButton(m_groupe_vecteurs);
    m_partout = new QRadioButton(m_groupe_vecteurs);
    connect(m_pres, &QRadioButton::toggled, this, apres([this](bool v) { m_app.traj.pres_souris = v; }));
    m_titre_echelle = new QLabel(m_groupe_vecteurs);
    auto *ligne = new QHBoxLayout();
    m_echelle_vitesse = new QLineEdit(m_groupe_vecteurs);
    m_echelle_vitesse->setFixedWidth(QFontMetrics(m_echelle_vitesse->font()).horizontalAdvance(u"00000,0"_s) + 16);
    connect(m_echelle_vitesse, &QLineEdit::textEdited, this, apres([this](const QString &t) { m_app.traj.echelle_vitesse = t; }));
    m_px_par_ms = new QLabel(m_groupe_vecteurs);
    ligne->addWidget(m_echelle_vitesse);
    ligne->addWidget(m_px_par_ms);
    ligne->addStretch(1);
    m_incorrecte = new QLabel(m_groupe_vecteurs);
    dessin::colore(m_incorrecte, dessin::Ton::Erreur);
    for (QWidget *w : {static_cast<QWidget *>(m_titre_vecteurs), static_cast<QWidget *>(m_vecteurs),
                       static_cast<QWidget *>(m_pres), static_cast<QWidget *>(m_partout),
                       static_cast<QWidget *>(m_titre_echelle)})
        lv->addWidget(w);
    lv->addLayout(ligne);
    lv->addWidget(m_incorrecte);
    lc->addWidget(m_groupe_vecteurs);

    m_groupe_chrono = new QGroupBox(controles);
    auto *lch = new QVBoxLayout(m_groupe_chrono);
    m_titre_chrono = new QLabel(m_groupe_chrono);
    auto *ligne_chrono = new QHBoxLayout();
    m_image_n = new QLabel(m_groupe_chrono);
    m_chrono_img = new QSpinBox(m_groupe_chrono);
    connect(m_chrono_img, &QSpinBox::valueChanged, this, apres([this](int i) { m_app.traj.chrono_img = i; }));
    ligne_chrono->addWidget(m_image_n);
    ligne_chrono->addWidget(m_chrono_img);
    ligne_chrono->addStretch(1);
    lch->addWidget(m_titre_chrono);
    lch->addLayout(ligne_chrono);
    lc->addWidget(m_groupe_chrono);

    m_enregistrer = new QPushButton(controles);
    connect(m_enregistrer, &QPushButton::clicked, this, [this] { enregistre_image(); });
    lc->addWidget(m_enregistrer, 0, Qt::AlignLeft);
    lc->addWidget(dessin::trait(controles));
    m_titre_referentiel = new QLabel(controles);
    m_referentiel = new QComboBox(controles);
    connect(m_referentiel, &QComboBox::currentIndexChanged, this, apres([this](int i) {
                if (i >= 0)
                    m_app.traj.referentiel = i;
            }));
    m_voir = new QPushButton(controles);
    connect(m_voir, &QPushButton::clicked, this, [this] {
        if (auto r = Ralenti::nouveau(m_app.pointage, m_app.traj.referentiel, m_app.dimensions())) {
            auto *f = new FenetreRalenti(m_app, std::move(*r), &m_app);
            f->setAttribute(Qt::WA_DeleteOnClose);
            f->show();
        }
    });
    lc->addWidget(m_titre_referentiel);
    lc->addWidget(m_referentiel);
    lc->addWidget(m_voir, 0, Qt::AlignLeft);
    lc->addStretch(1);
    // le panneau latéral, et le dessin
    m_dessin = new DessinTrajectoire(app, this);
    l->addWidget(new dessin::SeparationLaterale(dessin::panneau_lateral(controles, this), m_dessin, this), 1);
}

void OngletTrajectoire::enregistre_image()
{
    m_app.enregistre_image(m_dessin, trad("Enregistrer comme image"), trad("Images (png, jpg)") + u" (*.png *.jpg *.jpeg)"_s,
                           u"jpg"_s);
}

void OngletTrajectoire::maj()
{
    App &a = m_app;
    Trajectoire &t = a.traj;
    if (t.mode != ModeTrajectoire::Trajectoire)
        t.vecteurs = false;
    if (t.mode == ModeTrajectoire::Chronophotographie && t.chrono_img == 1) {
        // par défaut, la première image pointée
        if (const auto prem = a.pointage.premiere_image())
            t.chrono_img = *prem;
    }
    const QSignalBlocker b1(m_mode), b2(m_vecteurs), b3(m_pres), b4(m_partout), b5(m_chrono_img), b6(m_referentiel);
    m_mode->setItemText(0, trad("Trajectoire"));
    m_mode->setItemText(1, trad("Chronophotographie"));
    m_mode->setItemText(2, trad("Chronogramme"));
    m_mode->setCurrentIndex(static_cast<int>(t.mode));

    m_groupe_vecteurs->setEnabled(t.mode == ModeTrajectoire::Trajectoire);
    m_titre_vecteurs->setText(trad("Vecteurs vitesse"));
    m_vecteurs->setText(trad("Montrer les vecteurs vitesse"));
    m_vecteurs->setChecked(t.vecteurs);
    for (QWidget *w : {static_cast<QWidget *>(m_pres), static_cast<QWidget *>(m_partout),
                       static_cast<QWidget *>(m_titre_echelle), static_cast<QWidget *>(m_echelle_vitesse),
                       static_cast<QWidget *>(m_px_par_ms)})
        w->setVisible(t.vecteurs);
    m_pres->setText(trad("Près de la souris"));
    m_partout->setText(trad("Partout"));
    m_pres->setChecked(t.pres_souris);
    m_partout->setChecked(!t.pres_souris);
    m_titre_echelle->setText(trad("Échelle de vitesses"));
    if (m_echelle_vitesse->text() != t.echelle_vitesse)
        m_echelle_vitesse->setText(t.echelle_vitesse);
    m_px_par_ms->setText(trad("px pour 1 {}/s", a.pointage.unite()));
    m_echelle_vitesse->setPlaceholderText(nombre_local(echelle_vitesse_auto(a.pointage, a.dimensions()), 3));
    // le mode expert donne des mesures, pas de consignes : l'explication est dans la bulle
    m_echelle_vitesse->setToolTip(trad("Longueur des flèches, en pixels de l'image pour une vitesse de 1 {}/s",
                                       a.pointage.unite())
                                  + u"\n"_s
                                  + trad("Laissez la case vide pour une échelle automatique. La légende est en bas à "
                                         "gauche du dessin."));
    m_incorrecte->setText(trad("Valeur incorrecte : un nombre positif"));
    m_incorrecte->setVisible(t.vecteurs && !t.echelle_vitesse.trimmed().isEmpty() && !t.echelle_saisie());

    m_groupe_chrono->setEnabled(t.mode == ModeTrajectoire::Chronophotographie);
    m_titre_chrono->setText(trad("Chronophotographie"));
    m_image_n->setText(trad("Image n°"));
    m_chrono_img->setRange(1, std::max(a.image_max, 1));
    m_chrono_img->setValue(t.chrono_img);
    m_enregistrer->setText(trad("Enregistrer"));
    m_enregistrer->setToolTip(trad("Enregistrer comme image"));

    m_titre_referentiel->setText(trad("Changement de référentiel :"));
    const int n = a.pointage.nb_obj() + 1;
    while (m_referentiel->count() > n)
        m_referentiel->removeItem(m_referentiel->count() - 1);
    while (m_referentiel->count() < n)
        m_referentiel->addItem(QString());
    for (int r = 0; r < n; ++r)
        m_referentiel->setItemText(r, r == 0 ? trad("caméra") : trad("objet n° {}", r));
    t.referentiel = std::min(t.referentiel, n - 1);
    m_referentiel->setCurrentIndex(t.referentiel);
    m_voir->setText(trad("Voir la vidéo"));
    m_voir->setEnabled(t.referentiel != 0);
    m_dessin->update();
}

// ------------------------------------------------------------------------------------------------
// FenetreRalenti
// ------------------------------------------------------------------------------------------------

FenetreRalenti::FenetreRalenti(App &app, Ralenti ralenti, QWidget *parent)
    : QDialog(parent)
    , m_app(app)
    , m_r(std::move(ralenti))
{
    setWindowTitle(trad("Vidéo dans le référentiel de l'objet"));
    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel(u"%1 — %2"_s.arg(trad("objet n° {}", m_r.obj), trad("Échap pour fermer")), this));
    m_image = new QLabel(this);
    const double k = std::min({800.0 / m_r.taille.width(), 600.0 / m_r.taille.height(), 2.0});
    m_image->setFixedSize((QSizeF(m_r.taille) * k).toSize());
    m_image->setScaledContents(true);
    l->addWidget(m_image);
    auto *ligne = new QHBoxLayout();
    m_titre_ralenti = new QLabel(this);
    m_ralenti = new QSlider(Qt::Horizontal, this);
    m_ralenti->setRange(1, 4);
    m_ralenti->setValue(1);
    const auto maj_ralenti = [this] {
        m_titre_ralenti->setText(trad("Ralenti : 1/{}", m_ralenti->value()));
        m_minuterie.setInterval(static_cast<int>(std::lround(1000.0 * m_ralenti->value() / std::max(m_app.fps, 1.0))));
    };
    connect(m_ralenti, &QSlider::valueChanged, this, maj_ralenti);
    ligne->addWidget(m_titre_ralenti);
    ligne->addWidget(m_ralenti, 1);
    l->addLayout(ligne);
    auto *fermer = new QPushButton(trad("Fermer"), this);
    connect(fermer, &QPushButton::clicked, this, &QDialog::close);
    l->addWidget(fermer, 0, Qt::AlignLeft);
    connect(&m_minuterie, &QTimer::timeout, this, &FenetreRalenti::image_suivante);
    maj_ralenti();
    image_suivante();
    m_minuterie.start();
}

void FenetreRalenti::image_suivante()
{
    if (m_r.images.empty() || !m_app.video)
        return;
    m_k = (m_k + 1) % m_r.images.size();
    const int index = m_r.images[m_k] + 1;
    const Vecteur coin = m_r.positions[m_k] - m_r.decalage;
    const auto img = m_app.video->image(index);
    if (!img)
        return;
    const auto cadre = detect::decoupe(*img, static_cast<int>(std::max(coin.x, 0.0)), static_cast<int>(std::max(coin.y, 0.0)),
                                       m_r.taille.width(), m_r.taille.height());
    if (cadre)
        m_image->setPixmap(QPixmap::fromImage(*cadre));
}
