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

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

std::optional<double> Trajectoire::echelle_saisie() const
{
    const auto v = lit_reel(echelle_vitesse);
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

double echelle_vitesse_auto(const Pointage::FlechesVitesse &fleches_unitaires, QSize dimensions)
{
    double vmax = 0.0;
    for (const auto &objet : fleches_unitaires) {
        for (const auto &[org, ext] : objet)
            vmax = std::max(vmax, (ext - org).module());
    }
    if (vmax <= 0.0)
        return 1.0;
    return arrondi_rond(std::min(dimensions.width(), dimensions.height()) / 7.0 / vmax);
}

std::optional<Ralenti> Ralenti::nouveau(const Pointage &p, int obj, QSize dimensions)
{
    Ralenti r;
    r.obj = obj;
    if (obj < 1 || obj > p.nombre_objets())
        return {};
    for (const auto &[i, positions] : p.positions_par_image()) {
        if (const Position &q = positions[obj - 1]) {
            if (!r.images.empty()) {
                auto &precedente = r.images.back();
                precedente.duree = p.date(i) - p.date(precedente.index);
            }
            r.images.push_back({i, *q, p.delta_t});
        }
    }
    if (r.images.empty())
        return std::nullopt;
    const int w = std::max(dimensions.width(), 1), h = std::max(dimensions.height(), 1);
    double xmin = std::numeric_limits<double>::infinity(), xmax = -xmin;
    double ymin = xmin, ymax = -xmin;
    for (const auto &image : r.images) {
        const Vecteur &q = image.position;
        xmin = std::min(xmin, q.x);
        xmax = std::max(xmax, q.x);
        ymin = std::min(ymin, q.y);
        ymax = std::max(ymax, q.y);
    }
    // Le plus grand cadre qui suit l'objet sans sortir de l'image : la place disponible à gauche et
    // à droite (respectivement au-dessus et au-dessous) du déplacement.
    const int gauche = static_cast<int>(std::clamp(std::floor(xmin), 0.0, double(w - 1)));
    const int droite = static_cast<int>(std::clamp(std::floor(w - 1.0 - xmax), 0.0, double(w - 1)));
    const int dessus = static_cast<int>(std::clamp(std::floor(ymin), 0.0, double(h - 1)));
    const int dessous = static_cast<int>(std::clamp(std::floor(h - 1.0 - ymax), 0.0, double(h - 1)));
    r.taille = QSize(std::clamp(gauche + droite, 1, w), std::clamp(dessus + dessous, 1, h));
    r.decalage = {double(gauche), double(dessus)};
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
    const Vecteur centre(d.width() / 2.0, d.height() / 2.0);
    const int image_fond = std::clamp(a.traj.chrono_img, 1, std::max(a.nombre_images_video, 1));
    Vecteur translation;
    painter.setClipRect(vue.rect);
    painter.fillRect(vue.rect, feuille);
    if (mode == ModeTrajectoire::Chronophotographie) {
        if (a.traj.referentiel) {
            const auto reference = a.pointage.point_de_l_objet(a.traj.referentiel, image_fond - 1);
            if (!reference) {
                painter.setPen(encre);
                painter.drawText(vue.rect, Qt::AlignCenter, trad("Objet de référence absent de cette image"));
                return;
            }
            translation = centre - *reference;
        }
        if (a.video) {
            if (auto img = a.video->image(image_fond))
                painter.drawImage(vue.rect.translated(translation.x * vue.echelle, translation.y * vue.echelle), *img);
        }
    }
    std::optional<Pointage> mobile;
    if (a.traj.referentiel)
        mobile = a.pointage.dans_referentiel(a.traj.referentiel, centre);
    const Pointage &p = mobile ? *mobile : a.pointage;
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
        const double t = a.pointage.date_relative(image_fond - 1);
        dessin::texte(painter, {x, y1 + 50.0}, u"t = %1 s"_s.arg(texte_date(t, p.delta_t)), 14.0, Qt::white, fond, false);
        const QColor bleu(0, 0, 255);
        if (e.est_definie()) {
            const QPointF e1 = vue.ecran(e.p1 + translation), e2 = vue.ecran(e.p2 + translation);
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
            const double longueur = e.etalon_pixels() * vue.echelle;
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
    const bool plusieurs = p.nombre_objets() > 1;
    for (const auto &[i, ligne] : p.positions_par_image()) {
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
        const auto vecteurs = p.fleches_vitesse(1.0);
        const auto saisie = a.traj.echelle_saisie();
        const double echelle = saisie ? *saisie : echelle_vitesse_auto(vecteurs, d);
        const QColor vitesse = fonce ? QColor(112, 207, 255) : QColor(0, 103, 158);
        for (size_t j = 0; j < vecteurs.size(); ++j) {
            for (const auto &[org, ext] : vecteurs[j]) {
                if (org == ext)
                    continue;
                const QPointF e1 = vue.ecran(org), e2 = vue.ecran(org + (ext - org) * echelle);
                // près de la souris seulement, si on l'a demandé
                if (a.traj.pres_souris) {
                    if (!m_souris)
                        continue;
                    const QPointF ecart = *m_souris - e1;
                    if (std::abs(ecart.x()) + std::abs(ecart.y()) > 20.0)
                        continue;
                }
                dessin::fleche(painter, e1, e2, vitesse, 1.5);
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
            dessin::fleche(painter, depart, depart + QPointF(longueur, 0), vitesse, 1.5);
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
    m_vecteurs->setIcon(icones::commande(icones::Commande::Vecteur));
    connect(m_vecteurs, &QCheckBox::toggled, this, apres([this](bool v) { m_app.traj.vecteurs = v; }));
    m_pres = new QRadioButton(m_groupe_vecteurs);
    m_pres->setIcon(icones::commande(icones::Commande::PresSouris));
    m_partout = new QRadioButton(m_groupe_vecteurs);
    m_partout->setIcon(icones::commande(icones::Commande::TousVecteurs));
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
    m_enregistrer->setIcon(icones::commande(icones::Commande::Enregistrer));
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
    m_voir->setIcon(icones::commande(icones::Commande::Demarrer));
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
    m_app.enregistre_image(m_dessin->grab(), trad("Enregistrer comme image"), trad("Images (png, jpg)") + u" (*.png *.jpg *.jpeg)"_s,
                           u"jpg"_s);
}

void OngletTrajectoire::actualise()
{
    App &a = m_app;
    Trajectoire &t = a.traj;
    if (t.mode != ModeTrajectoire::Trajectoire)
        t.vecteurs = false;
    if (t.mode == ModeTrajectoire::Chronophotographie && t.chrono_img == 1) {
        // par défaut, la première image pointée
        if (const auto prem = a.pointage.image_initiale())
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
    if (t.mode == ModeTrajectoire::Trajectoire && t.vecteurs && t.echelle_vitesse.isEmpty())
        m_echelle_vitesse->setPlaceholderText(nombre_local(echelle_vitesse_auto(a.pointage.fleches_vitesse(1.0), a.dimensions()), 3));
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
    m_chrono_img->setRange(1, std::max(a.nombre_images_video, 1));
    m_chrono_img->setValue(t.chrono_img);
    m_enregistrer->setText(trad("Enregistrer"));
    m_enregistrer->setToolTip(trad("Enregistrer comme image"));

    m_titre_referentiel->setText(trad("Changement de référentiel :"));
    const int n = a.pointage.nombre_objets() + 1;
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
    connect(m_ralenti, &QSlider::valueChanged, this, &FenetreRalenti::regle_minuterie);
    ligne->addWidget(m_titre_ralenti);
    ligne->addWidget(m_ralenti, 1);
    l->addLayout(ligne);
    auto *fermer = new QPushButton(icones::commande(icones::Commande::Fermer), trad("Fermer"), this);
    connect(fermer, &QPushButton::clicked, this, &QDialog::close);
    l->addWidget(fermer, 0, Qt::AlignLeft);
    m_minuterie.setTimerType(Qt::PreciseTimer);
    connect(&m_minuterie, &QTimer::timeout, this, [this] {
        m_rang_image = (m_rang_image + 1) % m_r.images.size();
        affiche_image();
    });
    affiche_image();
}

void FenetreRalenti::regle_minuterie()
{
    const int facteur = m_ralenti->value();
    m_titre_ralenti->setText(trad("Ralenti : 1/{}", facteur));
    if (m_r.images.size() > 1) {
        const double ms = m_r.images[m_rang_image].duree * facteur * 1000;
        m_minuterie.start(static_cast<int>(std::clamp(std::round(ms), 1.0, double(std::numeric_limits<int>::max()))));
    }
}

void FenetreRalenti::affiche_image()
{
    if (m_r.images.empty() || !m_app.video)
        return;
    const auto &courante = m_r.images[m_rang_image];
    const auto img = m_app.video->image(courante.index + 1);
    if (img) {
        const Vecteur coin = courante.position - m_r.decalage;
        const int x = static_cast<int>(std::clamp(coin.x, 0.0, double(img->width() - 1)));
        const int y = static_cast<int>(std::clamp(coin.y, 0.0, double(img->height() - 1)));
        const auto cadre = detect::decoupe(*img, x, y, m_r.taille.width(), m_r.taille.height());
        m_image->setPixmap(cadre ? QPixmap::fromImage(*cadre) : QPixmap());
    } else {
        m_image->setText(trad("Image {} illisible : {}", courante.index + 1, img.error()));
    }
    regle_minuterie();
}
