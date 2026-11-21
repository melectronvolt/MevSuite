#include "app.h"

#include "a_propos.h"
#include "accueil.h"
#include "chemins.h"
#include "debogage.h"
#include "demarrage.h"
#include "detect.h"
#include "format.h"
#include "i18n.h"
#include "onglet_coord.h"
#include "plein_ecran.h"
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

/// intervalle entre deux sauvegardes automatiques du travail, en millisecondes
constexpr int SECOURS_INTERVALLE = 2000;
/// distance, en points d'écran, à laquelle on saisit l'origine pour la faire glisser
constexpr double PRISE_ORIGINE = 12.0;

/// la police du système, avant tout agrandissement de l'interface
const QFont &police_systeme()
{
    static const QFont police = QApplication::font();
    return police;
}


/// Une information permanente de la barre d'état, séparée de ce qui la précède par un filet (couleur
/// Mid) et 10 points de marge. Le filet est dessiné ici : une bordure de feuille de style ne l'est pas
/// dans une QStatusBar. Les marges sont données par la feuille de style de la barre (`style_barre_etat`),
/// qui l'emporterait sur `setContentsMargins`.
class InfoEtat : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QLabel::paintEvent(event);
        QPainter(this).fillRect(QRect(0, 0, 1, height()), QApplication::palette().color(QPalette::Mid));
    }
};

/// L'indication du glisser-déposer : pendant qu'on survole la fenêtre avec un fichier, une invitation
/// à le déposer
class Depot : public QWidget {
public:
    explicit Depot(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 170));
        QFont f = font();
        f.setPixelSize(24);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, trad("Déposez la vidéo ici"));
    }
};

/// Pendant l'ouverture d'une vidéo, clics, touches, raccourcis, molette et dépôts de fichiers sont
/// ignorés ; la fermeture de la fenêtre passe et interrompt l'ouverture. (Une boucle d'attente sans
/// les « événements de l'utilisateur » retiendrait aussi la demande de fermeture du bureau.)
class SansSaisie : public QObject {
public:
    SansSaisie() { qApp->installEventFilter(this); }

protected:
    bool eventFilter(QObject *, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::Shortcut:
        case QEvent::ContextMenu:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::Drop:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TabletPress:
        case QEvent::TabletRelease:
            return true;
        default:
            return false;
        }
    }
};

} // namespace

QString nom_etat(Etat e)
{
    switch (e) {
    case Etat::SansVideo: return u"SansVideo"_s;
    case Etat::Preparation: return u"Preparation"_s;
    case Etat::SelectionMotifs: return u"SelectionMotifs"_s;
    case Etat::SuiviAutomatique: return u"SuiviAutomatique"_s;
    case Etat::Etalonnage: return u"Etalonnage"_s;
    case Etat::Pointage: return u"Pointage"_s;
    case Etat::SerieEnCours: return u"SerieEnCours"_s;
    }
    return {};
}

App::App(const QString &fichier, std::optional<Mode> mode_impose)
{
    setWindowIcon(QApplication::windowIcon());
    setMinimumSize(TAILLE_MIN_FENETRE);
    setAcceptDrops(true);
    police_systeme();

    bool erreur_conf = false;
    if (auto c = prefs::charge())
        config = *c;
    else
        erreur_conf = true;
    i18n::choisit(prefs::langue(config).value_or(i18n::langue_du_systeme()));
    // le mode imposé (--simple, --expert), sinon celui de la dernière fois
    mode = mode_impose.value_or(config.valeur_texte(u"mode"_s) == u"expert"_s ? Mode::Expert : Mode::Simplifie);
    mode_a_choisir = !mode_impose && !config.valeur_texte(u"mode"_s);
    unite_etalon = unite_depuis_symbole(config.valeur_texte(u"unite_etalon"_s).value_or(QString())).value_or(Unite::Metre);

    construit_menus();
    construit_coin_des_menus();
    m_pages = new QStackedWidget(this);
    m_sans_video = new PageSansVideo(*this, m_pages);
    m_guide = new PageGuide(*this, m_pages);
    m_expert = new QTabWidget(m_pages);
    m_expert->setDocumentMode(true);
    m_onglet_pointage = new OngletPointage(*this, m_expert);
    m_onglet_coord = new OngletCoord(*this, m_expert);
    m_onglet_trajectoire = new OngletTrajectoire(*this, m_expert);
    m_onglet_graph = new OngletGraph(*this, m_expert);
    // dans l'ordre de l'énumération Onglet, chacun avec son pictogramme
    const std::pair<QWidget *, icones::Onglet> onglets[] = {
        {m_onglet_pointage, icones::Onglet::Pointage},
        {m_onglet_coord, icones::Onglet::Coordonnees},
        {m_onglet_trajectoire, icones::Onglet::Trajectoires},
        {m_onglet_graph, icones::Onglet::Grapheur},
    };
    for (const auto &[w, pictogramme] : onglets)
        m_expert->addTab(w, icones::onglet(pictogramme), QString());
    connect(m_expert, &QTabWidget::currentChanged, this, [this](int i) {
        const auto o = static_cast<Onglet>(i);
        if (m_rafraichissement || o == onglet)
            return;
        annule_refait();
        onglet = o;
        if (o == Onglet::Trajectoires)
            traj.choisit_camera();
        rafraichit();
    });
    for (QWidget *page : {static_cast<QWidget *>(m_sans_video), static_cast<QWidget *>(m_guide), static_cast<QWidget *>(m_expert)})
        m_pages->addWidget(page);
    setCentralWidget(m_pages);

    construit_barre_etat();
    construit_raccourcis();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        if (!presse_papier.isEmpty())
            rafraichit();
    }, Qt::QueuedConnection);
    m_depot = new Depot(this);

    // le suivi automatique : une image à la fois, sans bloquer l'interface
    m_detection.setSingleShot(true);
    m_detection.setInterval(0);
    connect(&m_detection, &QTimer::timeout, this, [this] {
        if (etat != Etat::SuiviAutomatique)
            return;
        etape_detection();
        rafraichit();
        if (etat == Etat::SuiviAutomatique)
            m_detection.start();
    });
    // la sauvegarde automatique : tout de suite après un changement, puis au plus toutes les deux
    // secondes
    m_secours.setSingleShot(true);
    m_secours.setInterval(SECOURS_INTERVALLE);
    connect(&m_secours, &QTimer::timeout, this, [this] {
        // Rattrape les clics arrivés pendant les deux secondes précédentes. En cas d'échec,
        // réessaye même si l'élève ne touche plus à la fenêtre.
        sauvegarde_de_secours();
        if (secours_en_echec)
            m_secours.start();
    });

    if (erreur_conf)
        message(trad("Erreur de lecture de la configuration"),
                trad("Le fichier de configuration est illisible : MeVCine repart d'une configuration neuve."));
    passe_a_etat(Etat::SansVideo);
    applique_preferences();
    change_agrandissement(config.valeur_reelle(u"agrandissement"_s).value_or(1.0));
    ouvre_argument(fichier);

    // l'écran de démarrage, par-dessus tout, même un message ouvert au démarrage
    m_demarrage = new EcranDemarrage(this);
    connect(m_demarrage, &QObject::destroyed, this, [this] {
        m_demarrage = nullptr;
        affiche_dialogues();
    });
    rafraichit();
}

App::~App() = default;

QSize App::dimensions() const
{
    return video ? video->dimensions() : QSize(320, 240);
}

void App::message(const QString &t, const QString &texte)
{
    dialogues.push_back(Dialogue::message(t, texte));
    QTimer::singleShot(0, this, &App::affiche_dialogues);
}

QString App::message_etat() const
{
    switch (etat) {
    case Etat::SansVideo: return trad("Ouvrez une vidéo");
    case Etat::Preparation: return trad("Définissez l'échelle, ou démarrez le pointage");
    case Etat::SelectionMotifs:
        return trad("Préparation du suivi automatique : tracez un cadre autour de chaque objet à suivre ({} en tout)",
                    pointage.nombre_objets());
    case Etat::SuiviAutomatique: return trad("Suivi automatique en cours : le bouton STOP l'interrompt");
    case Etat::Etalonnage: return consigne_etalon();
    case Etat::Pointage:
    case Etat::SerieEnCours: return trad("Pointage");
    }
    return {};
}

QString App::message_par_defaut() const
{
    if (!video || etat == Etat::SansVideo)
        return trad("Ouvrez une vidéo");
    if (simplifie())
        return trad("Étape {} sur {} : {}", numero(guide.etape), TOUTES_LES_ETAPES.size(), nom(guide.etape));
    return message_etat();
}

QString App::consigne_etalon() const
{
    if (etalon_deux_clics)
        return trad("Cliquez sur l'autre extrémité de l'étalon (clic droit ou Échap : annuler)");
    return trad("Cliquez sur une extrémité de l'étalon, puis sur l'autre, ou faites un tirer-glisser (clic droit ou "
                "Échap : annuler)");
}

// ------------------------------------------------------------------------------------------------
// modes
// ------------------------------------------------------------------------------------------------

void App::change_mode(Mode m)
{
    mode_a_choisir = false;
    config.definit(u"mode"_s, m == Mode::Expert ? u"expert"_s : u"simplifie"_s);
    if (mode == m)
        return;

    mode = m;
    glisse_origine = false;
    choix_origine = false;
    if (simplifie()) {
        if (!guide.deja_utilise && (pointage.a_des_pointages() || pointage.echelle.est_definie()))
            guide.reprend(pointage.echelle.est_definie());
        if (etat == Etat::SelectionMotifs || etat == Etat::SuiviAutomatique || etat == Etat::SerieEnCours || refait)
            guide.etape = Etape::Pointage;
        else if (etat == Etat::Etalonnage || etalon_en_attente)
            guide.etape = Etape::Echelle;
        else if (guide.etape == Etape::Pointage && etat == Etat::Preparation)
            lance_pointage();
    } else {
        guide.deja_utilise = true;
        if (refait || etat == Etat::Etalonnage || etalon_en_attente)
            onglet = Onglet::Pointage;
        if (etalon_en_attente)
            demande_longueur_etalon(guide.saisie_etalon);
    }
}

// ------------------------------------------------------------------------------------------------
// machine à états
// ------------------------------------------------------------------------------------------------

void App::passe_a_etat(Etat e)
{
    const Etat precedent = etat;
    etat = e;
    dbg_p(1, u"========> État précédent = %1. État suivant = %2"_s.arg(nom_etat(precedent), nom_etat(e)));
    statut.clear();
    switch (e) {
    case Etat::SansVideo:
        onglet = Onglet::Pointage;
        pointage_arrete = false;
        index = 1;
        objet_courant = 1;
        choix_origine = false;
        drag.reset();
        traj = Trajectoire();
        break;
    case Etat::Preparation: {
        if (fichier_video.isEmpty())
            return;
        objet_courant = 1;
        onglet = Onglet::Pointage;
        suivi_auto = false;
        pointage_arrete = false;
        drag.reset();
        // on retient le dossier des vidéos (mais on ne rouvrira pas la vidéo au prochain lancement :
        // les postes sont partagés)
        config.definit(u"lastvideo"_s, QDir::toNativeSeparators(fichier_video));
        config.definit(u"videodir"_s, QDir::toNativeSeparators(QFileInfo(fichier_video).absolutePath()));
        ecrit_dimensions_conf();
        prefs::sauve(config);
        break;
    }
    case Etat::SelectionMotifs:
        image_motifs = 0;
        pistes.clear();
        cadres_des_motifs.clear();
        break;
    case Etat::SuiviAutomatique:
        m_detection.start();
        break;
    case Etat::Etalonnage:
        etat_avant_etalonnage = precedent;
        drag.reset();
        etalon_deux_clics = false;
        break;
    case Etat::Pointage:
        choix_origine = simplifie() && guide.etape == Etape::Origine;
        montre_repere_attendu();
        break;
    case Etat::SerieEnCours: break;
    }
}

bool App::onglets_actifs() const
{
    return etat == Etat::Pointage || (etat == Etat::Etalonnage && etat_avant_etalonnage == Etat::Pointage);
}

bool App::controle_image_actif() const
{
    return (etat == Etat::Preparation || etat == Etat::Pointage) && !refait;
}

bool App::repere_modifiable() const
{
    return (etat == Etat::Preparation || etat == Etat::Pointage) && !refait;
}

bool App::rotation_permise() const
{
    return etat == Etat::Preparation && !pointage.echelle.est_definie() && !etalon_en_attente && !pointage.a_des_pointages();
}

// ------------------------------------------------------------------------------------------------
// vidéo
// ------------------------------------------------------------------------------------------------

bool App::ouvre_video(const QString &chemin_donne, int rotation, int derniere_mesure)
{
    dbg_p(2, u"rentre dans 'ouvre_video' %1"_s.arg(chemin_donne));
    // chemin absolu, pour les préférences et les fichiers de projet
    const QString chemin = chemins::absolu(chemin_donne);
    // L'examen d'une grande vidéo (ffprobe en compte les paquets) peut prendre du temps, surtout sur une
    // clé USB : il se fait dans un autre fil, avec le curseur d'attente. La fenêtre se redessine, mais
    // ni clic ni touche n'agissent avant la fin (SansSaisie) ; la fermer interrompt l'ouverture.
    using Ouverture = std::expected<std::unique_ptr<LecteurVideo>, QString>;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ouverture_annulee = false;
    ouverture_en_cours = true;
    QFuture<Ouverture> tache = QtConcurrent::run([chemin, annule = &ouverture_annulee] {
        return LecteurVideo::ouvre(chemin, annule);
    });
    QEventLoop attente;
    QFutureWatcher<Ouverture> fin;
    connect(&fin, &QFutureWatcherBase::finished, &attente, &QEventLoop::quit);
    fin.setFuture(tache);
    if (!tache.isFinished()) {
        const SansSaisie sans_saisie;
        attente.exec();
    }
    tache.waitForFinished();
    Ouverture lecteur = tache.takeResult();
    ouverture_en_cours = false;
    QApplication::restoreOverrideCursor();
    if (ouverture_annulee) {
        // la fenêtre a été fermée pendant l'ouverture : on la ferme maintenant (en demandant d'abord si
        // des mesures seraient perdues)
        QTimer::singleShot(0, this, &QWidget::close);
        return false;
    }
    if (!lecteur) {
        dbg_p(1, u"erreur de lecture de %1 : %2"_s.arg(chemin, lecteur.error()));
        message(trad("Vidéo illisible"), trad("MeVCine n'arrive pas à lire la vidéo :\n{}\n\n{}", QDir::toNativeSeparators(chemin), lecteur.error()));
        return false;
    }
    (*lecteur)->fixe_rotation(rotation);
    const InformationsVideo &info = (*lecteur)->info;
    if (derniere_mesure > info.nb_images) {
        message(trad("Projet illisible"), trad("Le projet contient une mesure à l'image {} et la vidéo ne contient que {} images.",
                                              derniere_mesure, info.nb_images));
        return false;
    }
    // Jusqu'ici la vidéo précédente restait ouverte, même en cas d'erreur de décodage.
    termine_seance();
    nombre_images_video = info.nb_images;
    // vidéo au ralenti : les calculs se font avec la cadence de prise de vue, et non celle de lecture
    cadence_prise_de_vue = info.cadence_capture.value_or(info.cadence_lecture);
    video = std::move(*lecteur);
    fichier_video = chemin;
    signature_source = seance::signature_video(chemin);
    image_cle.reset();
    image = QImage();
    saisie_cadence = nombre_local(cadence_prise_de_vue, 6);
    if (etat != Etat::SansVideo)
        passe_a_etat(Etat::SansVideo);
    reinitialise_mesures();
    if (info.cadence_capture) {
        const QString filmee = nombre_local(*info.cadence_capture, 6), lue = nombre_local(info.cadence_lecture, 6);
        if (*info.cadence_capture > info.cadence_lecture)
            message(trad("Vidéo au ralenti"),
                    trad("Cette vidéo au ralenti a été filmée à {} images par seconde, et se lit à {} images par seconde.\n\n"
                         "Les dates sont calculées avec la cadence de prise de vue : {} images par seconde. "
                         "On peut la corriger dans le champ « Images par seconde » (partie « Vidéo au ralenti ? » en mode simplifié).",
                         filmee, lue, filmee));
        else
            message(trad("Vidéo en accéléré"),
                    trad("Cette vidéo en accéléré a été filmée à {} images par seconde, et se lit à {} images par seconde.\n\n"
                         "Les dates sont calculées avec la cadence de prise de vue : {} images par seconde. "
                         "On peut la corriger dans le champ « Images par seconde » (partie « Vidéo au ralenti ? » en mode simplifié).",
                         filmee, lue, filmee));
    }
    return true;
}

void App::ecrit_dimensions_conf()
{
    const QSize d = dimensions();
    config.definit(u"taille_image"_s, u"(%1,%2)"_s.arg(d.width()).arg(d.height()));
    config.definit(u"rotation"_s, QString::number(video ? video->rotation() : 0));
}

void App::remet_origine()
{
    const QSize d = dimensions();
    pointage.origine = {d.width() / 2.0, d.height() / 2.0};
}

void App::ajuste_nombre_objets()
{
    m_visee.reset();
    m_references_visee.clear();
    if (nombre_images_video > 0 && cadence_prise_de_vue > 0.0) {
        pointage.alloue_mesures(nombre_objets_demandes, 1.0 / cadence_prise_de_vue, nombre_images_video);
        pointage.instants = video ? video->info.instants() : std::nullopt;
        pointage.cadence_irreguliere = video && video->info.cadence_variable;
    }
}

void App::reinitialise_mesures()
{
    dbg_p(2, u"rentre dans 'reinitialise_mesures'"_s);
    for (auto *ralenti : findChildren<FenetreRalenti *>())
        ralenti->close();
    pointage.echelle = Echelle();
    etalon_en_attente.reset();
    index = 1;
    remet_origine();
    pistes.clear();
    cadres_des_motifs.clear();
    prochaine_image_du_suivi.reset();
    refait.reset();
    ajuste_nombre_objets();
    pointage.sens_x = 1;
    pointage.sens_y = 1;
    guide = Guide();
    correction.clear();
    objet_repris = 0;
    derniere_verification.reset();
    presse_papier.clear();
    dernier_csv.clear();
    m_secours_seance.nouvelle(fichier_video);
    m_fichier_projet.clear();
    m_empreinte_fichier_projet.clear();
    secours_en_echec = false;
    m_secours.stop();
    empreinte_sauvee.clear();
    passe_a_etat(Etat::Preparation);
    empreinte_initiale = empreinte_mesures();
}

std::optional<QImage> App::image_courante()
{
    if (!video)
        return std::nullopt;
    const std::pair cle{index, video->rotation()};
    if (image_cle == cle)
        return image.isNull() ? std::nullopt : std::optional(image);
    auto img = video->image(index);
    if (!img) {
        image = {};
        image_cle = cle;
        dbg_p(1, u"image %1 illisible : %2"_s.arg(index).arg(img.error()));
        affiche_statut(trad("Image {} illisible : {}", index, img.error()), icones::Statut::Erreur);
        return std::nullopt;
    }
    image = *img;
    image_cle = cle;
    return image;
}

void App::pivote_video(int increment)
{
    if (!rotation_permise() || !video)
        return;
    video->fixe_rotation(((video->rotation() + increment) % 360 + 360) % 360);
    image_cle.reset();
    remet_origine();
    guide.origine_choisie = false;
    ajuste_nombre_objets();
    ecrit_dimensions_conf();
}

void App::change_image(int i)
{
    index = std::clamp(i, 1, std::max(nombre_images_video, 1));
}

// Commandes de présentation et gestes sur la vidéo.

bool App::plein_ecran_permis() const
{
    if (!video || !(etat == Etat::Pointage || etat == Etat::SerieEnCours) || choix_origine)
        return false;
    return !simplifie() || guide.etape == Etape::Pointage || refait.has_value();
}

bool App::plein_ecran() const
{
    return m_plein_ecran && m_plein_ecran->isVisible();
}

void App::ouvre_plein_ecran()
{
    if (!plein_ecran_permis() || plein_ecran())
        return;
    // pointer en plein écran, c'est reprendre le pointage
    if (pointage_arrete) {
        pointage_arrete = false;
        montre_repere_attendu();
    }
    onglet = Onglet::Pointage;
    if (!m_plein_ecran)
        m_plein_ecran = new PleinEcran(*this, this);
    m_plein_ecran->showFullScreen();
    m_plein_ecran->activateWindow();
    if (simplifie() && guide.visee_assistee)
        m_plein_ecran->zone()->setFocus();
}

void App::quitte_plein_ecran()
{
    if (!plein_ecran())
        return;
    m_plein_ecran->hide();
    activateWindow();
    if (simplifie() && guide.visee_assistee)
        m_guide->zone()->setFocus();
}

QList<QAction *> App::raccourcis_plein_ecran() const
{
    return m_raccourcis_image + QList<QAction *>{m_defaire, m_refaire};
}

void App::controle_cadence()
{
    if (saisie_cadence.trimmed().isEmpty())
        return;
    const auto v = lit_reel(saisie_cadence);
    if (v && *v > 0.0 && std::isfinite(*v)) {
        cadence_prise_de_vue = *v;
        pointage.delta_t = 1.0 / *v;
        statut.clear();
    } else {
        affiche_statut(trad("Le nombre d'images par seconde doit être un nombre positif"), icones::Statut::Erreur);
    }
}

// ------------------------------------------------------------------------------------------------
// échelle et origine
// ------------------------------------------------------------------------------------------------

void App::saisit_echelle()
{
    if (!(etat == Etat::Preparation || etat == Etat::Pointage) || refait)
        return;
    choix_origine = false;
    etalon_en_attente.reset();
    passe_a_etat(Etat::Etalonnage);
}

void App::fin_echelle(Vecteur p1, Vecteur p2)
{
    if ((p1 - p2).module() < 2.0)
        // trop court : on attend une vraie sélection
        return;
    etalon_en_attente = {p1, p2};
    drag.reset();
    etalon_deux_clics = false;
    passe_a_etat(etat_avant_etalonnage);
    if (simplifie()) {
        guide.erreur_etalon.reset();
        guide.focus_saisie = true;
        affiche_statut(trad("Indiquez la longueur réelle de l'étalon et son unité, puis validez"), icones::Statut::Attention);
    } else {
        const Echelle &e = pointage.echelle;
        demande_longueur_etalon(e.est_definie() ? nombre_local(e.longueur_reelle_etalon / en_metre(unite_etalon), 6) : QString());
    }
}

void App::demande_longueur_etalon(const QString &saisie)
{
    Dialogue d{Dialogue::Type::Echelle};
    d.saisie = saisie;
    d.unite = unite_etalon;
    if (etalon_en_attente)
        d.texte = precision_etalon((etalon_en_attente->second - etalon_en_attente->first).module());
    dialogues.push_back(d);
    QTimer::singleShot(0, this, &App::affiche_dialogues);
}

void App::valide_longueur_etalon(double longueur)
{
    if (etalon_en_attente) {
        pointage.echelle = {etalon_en_attente->first, etalon_en_attente->second, longueur};
        etalon_en_attente.reset();
    } else if (pointage.echelle.est_definie()) {
        // on corrige seulement la longueur de l'étalon déjà tracé
        pointage.echelle.longueur_reelle_etalon = longueur;
    } else {
        return;
    }
    affiche_statut(trad("Échelle définie"), icones::Statut::Reussite);
}

void App::annule_echelle()
{
    drag.reset();
    etalon_deux_clics = false;
    if (etat == Etat::Etalonnage)
        passe_a_etat(etat_avant_etalonnage);
    affiche_statut(trad("Échelle inchangée"));
}

void App::change_unite_etalon(Unite u)
{
    unite_etalon = u;
    config.definit(u"unite_etalon"_s, symbole(u));
}

void App::place_origine()
{
    if (!repere_modifiable())
        return;
    choix_origine = !choix_origine;
    statut.clear();
}

void App::origine_placee()
{
    guide.origine_choisie = true;
    if (!simplifie()) {
        choix_origine = false;
        affiche_statut(trad("Origine déplacée : les mesures sont recalculées"), icones::Statut::Reussite);
    }
}

QString App::texte_echelle() const
{
    const Echelle &e = pointage.echelle;
    if (!e.est_definie())
        return trad("indéfinie");
    return nombre_local(e.pixels_par_metre(), 4);
}

QString App::precision_etalon(double pixels) const
{
    return trad("1 pixel sur cet étalon représente {} % de sa longueur.", nombre_local(100.0 / pixels, 2));
}

std::optional<App::Coordonnees> App::releve_coordonnees(Vecteur p) const
{
    if (simplifie() && !(guide.origine_choisie && guide.axes_choisis))
        return std::nullopt;
    const Vecteur q = (p - pointage.origine).oriente_coordonnees(pointage.sens_x, pointage.sens_y);
    Coordonnees c{QString::number(std::llround(q.x)), QString::number(std::llround(q.y)), std::nullopt};
    const Echelle &e = pointage.echelle;
    if (e.est_definie())
        c.metre = std::pair{nombre_local(q.x / e.pixels_par_metre(), 3), nombre_local(q.y / e.pixels_par_metre(), 3)};
    return c;
}

QString App::etiquette_loupe() const
{
    if (choix_origine)
        return trad("Origine du repère");
    switch (etat) {
    case Etat::Pointage:
    case Etat::SerieEnCours:
        if (refait)
            return trad("Mesure à refaire");
        if (pointage.nombre_objets() > 1)
            return trad("Pointage de l'objet {}", objet_courant);
        return trad("Pointage");
    case Etat::SelectionMotifs: return trad("Zone à suivre n° {}", pistes.size() + 1);
    case Etat::Etalonnage: return trad("Étalon");
    default: return trad("Loupe");
    }
}

// ------------------------------------------------------------------------------------------------
// la souris sur l'image
// ------------------------------------------------------------------------------------------------

bool App::curseur_croix() const
{
    if (choix_origine)
        return true;
    switch (etat) {
    case Etat::Etalonnage:
    case Etat::SelectionMotifs: return true;
    case Etat::Pointage:
    case Etat::SerieEnCours: return pointage_ok();
    default: return false;
    }
}

bool App::origine_deplacable() const
{
    if (!video || refait || !(etat == Etat::Preparation || etat == Etat::Pointage))
        return false;
    if (simplifie())
        return guide.etape == Etape::Origine || (guide.etape == Etape::Axes && guide.origine_choisie);
    return choix_origine || etat == Etat::Preparation;
}

bool App::pres_de_l_origine(Vecteur p, double echelle) const
{
    return (p - pointage.origine).module() * echelle <= PRISE_ORIGINE;
}

Qt::CursorShape App::curseur(std::optional<Vecteur> p, double echelle) const
{
    if (glisse_origine)
        return Qt::ClosedHandCursor;
    if (!p)
        return Qt::ArrowCursor;
    const bool origine_visible = !simplifie() || guide.origine_choisie;
    if (origine_visible && origine_deplacable() && pres_de_l_origine(*p, echelle))
        return Qt::OpenHandCursor;
    return curseur_croix() ? Qt::CrossCursor : Qt::ArrowCursor;
}

void App::souris_bouge(std::optional<Vecteur> position)
{
    souris = position;
    if (visee_proposee())
        return; // le mouvement du pointeur laisse la cible et la loupe fixes
    // sélection de l'étalon en deux clics : le trait suit la souris
    if (etat == Etat::Etalonnage && etalon_deux_clics && drag && souris)
        (*drag)[1] = *souris;
    rafraichit_loupe_et_image();
}

void App::rafraichit_loupe_et_image()
{
    if (plein_ecran())
        m_plein_ecran->maj_souris();
    else if (m_guide && m_pages->currentWidget() == m_guide)
        m_guide->maj_souris();
    else if (m_pages->currentWidget() == m_expert)
        m_onglet_pointage->maj_souris();
}

void App::geste(const Geste &g, const dessin::Vue &vue)
{
    if (!image_affichee())
        return;
    const QSize d = dimensions();
    const auto dans_image = [&](Vecteur p) { return p.x >= 0 && p.y >= 0 && p.x <= d.width() && p.y <= d.height(); };
    const auto borne = [&](Vecteur p) {
        return Vecteur(std::clamp(p.x, 0.0, double(d.width())), std::clamp(p.y, 0.0, double(d.height())));
    };
    using T = Geste::Type;
    if (glisse_origine) {
        // l'origine suit la souris, jusqu'au relâchement du bouton
        if (g.type == T::Glisse || g.type == T::FinGlisse)
            pointage.origine = borne(g.position);
        if (g.type != T::Glisse) {
            glisse_origine = false;
            origine_placee();
        }
    } else if (g.type == T::DebutGlisse && origine_deplacable()
               && (choix_origine || ((!simplifie() || guide.origine_choisie) && pres_de_l_origine(g.origine, vue.echelle)))) {
        glisse_origine = true;
        pointage.origine = borne(g.position);
    } else if (choix_origine) {
        if (g.type == T::Clic && dans_image(g.position)) {
            pointage.origine = g.position;
            origine_placee();
        }
    } else if (etat == Etat::Etalonnage) {
        // Sélection de l'étalon : un tirer-glisser d'une extrémité à l'autre, ou deux clics (le trait
        // suit alors la souris entre les deux)
        if (g.type == T::ClicDroit) {
            annule_echelle();
        } else if (etalon_deux_clics) {
            if ((g.type == T::Clic || g.type == T::FinGlisse) && drag)
                fin_echelle((*drag)[0], g.position);
        } else if (g.type == T::DebutGlisse) {
            drag = std::array{g.origine, g.position};
        } else if (g.type == T::Glisse) {
            if (drag)
                (*drag)[1] = g.position;
        } else if (g.type == T::FinGlisse) {
            if (drag) {
                const auto [p1, p2] = *drag;
                drag.reset();
                if ((p2 - p1).module() * vue.echelle >= 6.0) {
                    fin_echelle(p1, p2);
                } else {
                    // un appui sans vrai glissement : c'est le premier clic
                    drag = std::array{p1, p1};
                    etalon_deux_clics = true;
                }
            }
        } else if (g.type == T::Clic) {
            drag = std::array{g.position, g.position};
            etalon_deux_clics = true;
        }
    } else if (etat == Etat::SelectionMotifs) {
        if (g.type == T::DebutGlisse) {
            drag = std::array{g.origine, g.position};
        } else if (g.type == T::Glisse) {
            if (drag)
                (*drag)[1] = g.position;
        } else if (g.type == T::FinGlisse && drag) {
            const auto [p1, p2] = *drag;
            drag.reset();
            fin_selection_motif(p1, p2);
        }
    } else if (etat == Etat::Pointage || etat == Etat::SerieEnCours) {
        // on pointe au relâchement du bouton, même après un petit glissement
        if ((g.type == T::Clic || g.type == T::FinGlisse) && pointage_ok() && dans_image(g.position))
            clic_pointage(g.position);
    }
    if (g.type == T::Glisse)
        // pendant un glissement, seule l'image change
        souris_bouge(g.position);
    else
        rafraichit();
}

// ------------------------------------------------------------------------------------------------
// aide, langue
// ------------------------------------------------------------------------------------------------

void App::aide_page(const QString &page)
{
    // Le navigateur peut continuer à lire la copie après la fermeture de MeVCine.
    const QString fichier = page.section(u'#', 0, 0);
    const QString ancre = page.section(u'#', 1);
    const QString copie = chemins::copie_aide();
    const QDir dossier(copie.isEmpty() ? chemins::dossier_notice() : copie);
    for (const QString &code : {i18n::code(i18n::langue()), u"en"_s, u"fr"_s}) {
        const QString f = dossier.filePath(code + u'/' + fichier);
        if (QFileInfo(f).isFile() && chemins::ouvre_avec_le_bureau(f, ancre))
            return;
    }
    message(trad("Aide"), trad("La notice est introuvable : le dossier {} manque à côté du programme.", QDir::toNativeSeparators(dossier.path())));
}

QString App::page_d_aide() const
{
    if (!video)
        return u"demarrer.html"_s;
    if (etat == Etat::SuiviAutomatique || etat == Etat::SelectionMotifs)
        return u"expert.html#suivi"_s;
    if (plein_ecran())
        return simplifie() ? u"simplifie.html#plein-ecran"_s : u"expert.html#plein-ecran"_s;
    if (simplifie()) {
        switch (guide.etape) {
        case Etape::Origine: return u"simplifie.html#origine"_s;
        case Etape::Axes: return u"simplifie.html#axes"_s;
        case Etape::Echelle: return u"simplifie.html#echelle"_s;
        case Etape::Pointage: return guide.visee_assistee ? u"simplifie.html#visee"_s : u"simplifie.html#pointage"_s;
        case Etape::Export: return u"simplifie.html#export"_s;
        }
    }
    switch (onglet) {
    case Onglet::Coordonnees: return u"expert.html#coordonnees"_s;
    case Onglet::Trajectoires: return u"expert.html#trajectoires"_s;
    case Onglet::Grapheur: return u"expert.html#grapheur"_s;
    case Onglet::Pointage: return etat == Etat::Etalonnage ? u"echelle.html#tracer"_s : u"expert.html#pointage"_s;
    }
    return u"index.html"_s;
}

void App::ouvre_notice()
{
    aide_page(page_d_aide());
}

void App::change_langue(std::optional<Langue> l)
{
    config.definit(u"langue"_s, l ? i18n::code(*l) : QString());
    i18n::choisit(l.value_or(i18n::langue_du_systeme()));
    // la valeur affichée du nombre d'images par seconde suit la langue
    if (const auto v = lit_reel(saisie_cadence); v && std::abs(*v - cadence_prise_de_vue) < 1e-9)
        saisie_cadence = nombre_local(cadence_prise_de_vue, 6);
    prefs::sauve(config);
    maj_qt_traductions();
}

void App::langue_suivante()
{
    change_langue(i18n::langue() == Langue::Francais ? Langue::Anglais : Langue::Francais);
}

void App::theme_suivant()
{
    const Qt::ColorScheme choix = dessin::sombre(QApplication::palette()) ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark;
    config.definit(u"theme"_s, theme::en_texte(choix));
    prefs::sauve(config);
    // les couleurs suivent la palette (changeEvent)
    theme::choisit(choix);
}

void App::montre()
{
    QSize taille = TAILLE_FENETRE;
    if (const auto t = config.valeur_vecteur(u"taille_fenetre"_s); t && t->x >= 400 && t->y >= 300)
        taille = QSize(static_cast<int>(t->x), static_cast<int>(t->y));
    resize(taille);
    bool agrandie = config.valeur_texte(u"fenetre_maximisee"_s) == u"True"_s;
    if (const QScreen *ecran = screen(); ecran && !agrandie) {
        // Fenêtre retenue plus grande que l'écran (configuration venue d'un autre poste,
        // vidéoprojecteur en basse définition) : elle est ramenée à l'écran, place gardée pour la barre
        // de titre. Elle n'est agrandie que si l'écran est plus petit que sa taille minimale.
        const QRect dispo = ecran->availableGeometry();
        const QSize place = dispo.size() - QSize(16, 48);
        agrandie = place.width() < minimumWidth() || place.height() < minimumHeight();
        resize(size().boundedTo(place).expandedTo(minimumSize()));
        move(dispo.center() - rect().center());
    }
    if (agrandie)
        showMaximized();
    else
        show();
}

double App::agrandissement_max() const
{
    // Le maximum dépend de la largeur de la fenêtre : il reste toujours au moins la largeur minimale
    // pour les panneaux et l'image, même sur un écran 1366 × 768
    if (width() < TAILLE_MIN_FENETRE.width())
        return AGRANDISSEMENT_MAX; // fenêtre pas encore mesurée
    return std::clamp(static_cast<double>(width()) / TAILLE_MIN_FENETRE.width(), 1.0, AGRANDISSEMENT_MAX);
}

void App::change_agrandissement(double a)
{
    if (!std::isfinite(a))
        a = 1.0;
    m_agrandissement_voulu = std::clamp(a, AGRANDISSEMENT_MIN, AGRANDISSEMENT_MAX);
    // retenu dans la configuration du poste, s'il a changé : la valeur demandée, et non celle que
    // permet la fenêtre en ce moment
    const QString texte = QString::number(m_agrandissement_voulu, 'g', 3);
    if (config.valeur_texte(u"agrandissement"_s).value_or(u"1"_s) != texte) {
        config.definit(u"agrandissement"_s, texte);
        prefs::sauve(config);
    }
    applique_agrandissement();
}

void App::applique_agrandissement()
{
    m_agrandissement = std::clamp(m_agrandissement_voulu, AGRANDISSEMENT_MIN, std::max(agrandissement_max(), AGRANDISSEMENT_MIN));
    // la police de toute l'application : les widgets qui ont la leur (accueil) la reçoivent ensuite
    const QFont &base = police_systeme();
    QFont police = base;
    if (base.pointSizeF() > 0.0)
        police.setPointSizeF(base.pointSizeF() * m_agrandissement);
    else
        police.setPixelSize(std::max(1, static_cast<int>(std::lround(base.pixelSize() * m_agrandissement))));
    if (QApplication::font() != police)
        QApplication::setFont(police);
    dessin::reapplique_polices(this);
    // la barre d'état a une feuille de style : ses textes ne prennent la nouvelle police qu'avec elle
    statusBar()->setFont(police);
    style_barre_etat();
    // les pictogrammes des onglets, à la hauteur du texte
    const int ligne = QFontMetrics(police).height();
    m_expert->setIconSize(QSize(ligne, ligne));
    // la barre des menus ne prend la nouvelle police qu'au tour suivant
    QTimer::singleShot(0, this, [this] { dimensionne_coin_des_menus(); });
    rafraichit();
    ajuste_barre_etat();
}

void App::maj_qt_traductions()
{
    // les boutons des boîtes de dialogue de Qt (fichiers…), traduits s'ils le peuvent
    if (m_qt_traductions_installees) {
        QApplication::removeTranslator(&m_qt_traductions);
        m_qt_traductions_installees = false;
    }
    if (i18n::langue() == Langue::Anglais)
        return;
    if (m_qt_traductions.load(u"qtbase_"_s + i18n::code(i18n::langue()), QLibraryInfo::path(QLibraryInfo::TranslationsPath))
        || m_qt_traductions.load(u"qtbase_"_s + i18n::code(i18n::langue()),
                                 QCoreApplication::applicationDirPath() + u"/translations"_s))
        m_qt_traductions_installees = QApplication::installTranslator(&m_qt_traductions);
}

void App::ouvre_a_propos()
{
    auto *f = new FenetreAPropos(this);
    f->setAttribute(Qt::WA_DeleteOnClose);
    f->open();
}

// ------------------------------------------------------------------------------------------------
// menus et raccourcis
// ------------------------------------------------------------------------------------------------

void App::construit_menus()
{
    using enum icones::Commande;
    // Les menus donnent tout : il n'y a pas de barre d'outils
    const auto action = [this](QMenu *menu, const QIcon &icone, auto fonction) {
        auto *a = menu->addAction(icone, QString());
        a->setIconVisibleInMenu(true);
        connect(a, &QAction::triggered, this, [this, fonction] {
            fonction();
            rafraichit();
        });
        return a;
    };
    m_menu_fichier = menuBar()->addMenu(QString());
    m_ouvrir = action(m_menu_fichier, commande(OuvrirVideo), [this] { choisit_video_dossier_prefere(); });
    m_ouvrir->setShortcut(QKeySequence::Open);
    m_menu_fichier->addSeparator();
    m_ouvrir_projet = action(m_menu_fichier, commande(OuvrirProjet), [this] { confirme_si_mesures({Apres::Type::OuvrirProjet}); });
    m_recuperer = action(m_menu_fichier, commande(Recuperer), [this] { choisit_secours(); });
    m_recuperer->setEnabled(!SecoursSeance::disponibles().isEmpty());
    connect(m_menu_fichier, &QMenu::aboutToShow, this, [this] {
        m_recuperer->setEnabled(!SecoursSeance::disponibles().isEmpty());
    });
    m_enregistrer = action(m_menu_fichier, commande(Enregistrer), [this] { ecrit_projet_ui(); });
    m_enregistrer->setShortcut(QKeySequence::Save);
    m_menu_fichier->addSeparator();
    m_copier = action(m_menu_fichier, commande(Copier), [this] { copie_python(); });
    m_copier->setShortcut(QKeySequence::Copy);
    m_csv = action(m_menu_fichier, commande(ExporterCsv), [this] { exporte_csv(); });
    m_menu_fichier->addSeparator();
    m_quitter = action(m_menu_fichier, commande(Quitter), [this] { close(); });
    m_quitter->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    m_quitter->setMenuRole(QAction::QuitRole);

    m_menu_edition = menuBar()->addMenu(QString());
    m_defaire = action(m_menu_edition, commande(Annuler), [this] { retire_mesure_avant(); });
    m_defaire->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    m_refaire = action(m_menu_edition, commande(Retablir), [this] { retablit_point(); });
    m_refaire->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    m_menu_edition->addSeparator();
    m_recommencer = action(m_menu_edition, commande(Recommencer), [this] { confirme_si_mesures({Apres::Type::ToutRecommencer}); });

    m_menu_mode = menuBar()->addMenu(QString());
    auto *groupe_mode = new QActionGroup(this);
    m_mode_simplifie = action(m_menu_mode, commande(ModeSimplifie), [this] { change_mode(Mode::Simplifie); });
    m_mode_expert = action(m_menu_mode, commande(ModeExpert), [this] { change_mode(Mode::Expert); });
    for (QAction *a : {m_mode_simplifie, m_mode_expert}) {
        a->setCheckable(true);
        groupe_mode->addAction(a);
    }

    // l'aide, en dernier : la notice (à la page de ce que l'on fait), « À propos »
    m_menu_aide = menuBar()->addMenu(QString());
    m_notice = action(m_menu_aide, commande(Notice), [this] { ouvre_notice(); });
    m_notice->setShortcut(QKeySequence(Qt::Key_F1));
    m_a_propos = action(m_menu_aide, QIcon(u":/icones/mevcine-32.png"_s), [this] { ouvre_a_propos(); });
    m_a_propos->setMenuRole(QAction::AboutRole);
    m_a_propos_qt = action(m_menu_aide, icones::statut(icones::Statut::Info), [this] { QMessageBox::aboutQt(this); });
    m_a_propos_qt->setMenuRole(QAction::AboutQtRole);
    for (QMenu *m : {m_menu_fichier, m_menu_edition, m_menu_mode, m_menu_aide})
        m->setToolTipsVisible(true);

    // le thème et la langue : un bouton chacun, à droite de la barre des menus ; un clic passe à
    // l'autre
    m_theme = new QAction(this);
    m_theme->setObjectName(u"theme"_s);
    connect(m_theme, &QAction::triggered, this, [this] { theme_suivant(); });
    m_langue = new QAction(this);
    m_langue->setObjectName(u"langue"_s);
    connect(m_langue, &QAction::triggered, this, [this] {
        langue_suivante();
        rafraichit();
    });
    maj_qt_traductions();
}

void App::construit_coin_des_menus()
{
    // Pas de barre d'outils : tout est dans les menus (ouvrir, enregistrer, mode, notice). À droite de
    // la barre des menus, sur sa ligne, toujours dans cet ordre : le thème (soleil ou lune), la langue
    // (drapeau). La barre reste dans la fenêtre, même avec un menu global du bureau, qui cacherait ces
    // boutons.
    menuBar()->setNativeMenuBar(false);
    auto *coin = new QWidget(menuBar());
    coin->setObjectName(u"coin_menus"_s);
    auto *l = new QHBoxLayout(coin);
    l->setContentsMargins(0, 0, 4, 0);
    l->setSpacing(2);
    // le thème et la langue ; la notice est dans le menu Aide (F1), un bouton de plus faisait doublon
    const std::array<QAction *, 2> actions{m_theme, m_langue};
    for (size_t k = 0; k < actions.size(); ++k) {
        auto *b = new QToolButton(coin);
        b->setObjectName(actions[k]->objectName());
        b->setDefaultAction(actions[k]);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        l->addWidget(b);
        m_boutons_coin[k] = b;
    }
    menuBar()->setCornerWidget(coin, Qt::TopRightCorner);
}

void App::dimensionne_coin_des_menus()
{
    const int hauteur = menuBar()->actionGeometry(m_menu_fichier->menuAction()).height();
    if (hauteur <= 0)
        return;
    const int icone = std::min(static_cast<int>(std::lround(20 * m_agrandissement)), hauteur - 4);
    for (QToolButton *b : m_boutons_coin) {
        b->setIconSize(QSize(icone, icone));
        b->setFixedHeight(hauteur);
    }
    // la barre des menus ne revoit la place du coin que quand on le lui redonne : il garderait sinon sa
    // taille d'avant, trop petite pour les boutons agrandis (comme MeVReg)
    if (QWidget *coin = menuBar()->cornerWidget(Qt::TopRightCorner)) {
        coin->adjustSize();
        menuBar()->setCornerWidget(coin, Qt::TopRightCorner);
    }
    menuBar()->updateGeometry();
}

void App::construit_barre_etat()
{
    // À gauche, ce qu'il faut faire (ou ce qui vient de se passer), avec un pictogramme ; un message
    // long passe à la ligne au lieu d'être coupé. À droite, la vidéo ouverte, l'image affichée et le
    // mode.
    QStatusBar *barre = statusBar();
    barre->setSizeGripEnabled(false);
    m_statut_icone = new QLabel(barre);
    m_statut_icone->setContentsMargins(6, 0, 0, 0);
    m_statut = new QLabel(barre);
    m_statut->setObjectName(u"statut"_s);
    m_statut->setWordWrap(true);
    m_statut->setContentsMargins(0, 4, 0, 4);
    m_statut->installEventFilter(this);
    m_info_video = new InfoEtat(barre);
    m_info_video->setObjectName(u"statut_video"_s);
    m_info_image = new InfoEtat(barre);
    m_info_image->setObjectName(u"statut_image"_s);
    m_info_mode = new InfoEtat(barre);
    m_info_mode->setObjectName(u"statut_mode"_s);
    barre->addWidget(m_statut_icone);
    barre->addWidget(m_statut, 1);
    barre->addPermanentWidget(m_info_video);
    barre->addPermanentWidget(m_info_image);
    barre->addPermanentWidget(m_info_mode);
    style_barre_etat();
}

void App::style_barre_etat()
{
    // Une bande distincte du reste de la fenêtre : fond un peu plus foncé, filet au-dessus ; les
    // informations de droite, séparées par un filet vertical (`InfoEtat`). La vidéo, plus discrète :
    // sa couleur est donnée ici, la feuille de style l'emportant sur la palette.
    const QPalette pal = QApplication::palette();
    const QColor fond = pal.color(QPalette::Window).darker(dessin::sombre(pal) ? 135 : 107);
    statusBar()->setStyleSheet(u"QStatusBar { background: %1; border-top: 1px solid %2; }"
                               "QStatusBar::item { border: none; }"
                               "QLabel#statut_video, QLabel#statut_image, QLabel#statut_mode { padding: 0 10px 0 11px; }"
                               "QLabel#statut_video { color: %3; }"_s.arg(fond.name(), pal.color(QPalette::Mid).name(),
                                                                          pal.color(QPalette::PlaceholderText).name()));
}

void App::ajuste_barre_etat()
{
    if (!m_statut)
        return;
    // Hauteur donnée à la barre : celle que QLabel estime coupe le texte dès une vingtaine de
    // caractères (et la barre changeait de hauteur d'un message à l'autre). Ici, une ligne si le
    // message tient dans la largeur qu'il occupe, plusieurs sinon.
    const int ligne = std::max(m_statut->heightForWidth(QWIDGETSIZE_MAX), m_info_mode->sizeHint().height());
    statusBar()->setFixedHeight(std::max(ligne, m_statut->heightForWidth(m_statut->width())) + 6);
}

void App::maj_barre_etat()
{
    QString texte = statut;
    icones::Statut type = type_statut;
    if (texte.isEmpty()) {
        texte = message_par_defaut();
        type = icones::Statut::Info;
    }
    if (m_statut->text() != texte) {
        m_statut->setText(texte);
        ajuste_barre_etat();
    }
    switch (type) {
    case icones::Statut::Info:
        m_statut->setProperty("ton", QVariant());
        dessin::colore(m_statut, QApplication::palette().color(QPalette::WindowText));
        break;
    case icones::Statut::Reussite: dessin::colore(m_statut, dessin::Ton::Reussite); break;
    case icones::Statut::Attention: dessin::colore(m_statut, dessin::Ton::Attention); break;
    case icones::Statut::Erreur: dessin::colore(m_statut, dessin::Ton::Erreur); break;
    }
    // le pictogramme, de la hauteur d'une ligne
    const int cote = m_statut->fontMetrics().height();
    m_statut_icone->setPixmap(icones::statut(type).pixmap(QSize(cote, cote), devicePixelRatioF()));
    m_info_video->setVisible(video != nullptr);
    m_info_image->setVisible(video != nullptr);
    if (video) {
        // le nom de la vidéo (raccourci au milieu s'il est long) et ses caractéristiques
        const QSize d = dimensions();
        const QString complet = u"%1 · %2 × %3 px · %4"_s.arg(QFileInfo(fichier_video).fileName())
                                    .arg(d.width())
                                    .arg(d.height())
                                    .arg(trad("{} im/s", nombre_local(cadence_prise_de_vue, 4)));
        m_info_video->setText(m_info_video->fontMetrics().elidedText(complet, Qt::ElideMiddle, std::max(width() / 3, 120)));
        m_info_video->setToolTip(QDir::toNativeSeparators(fichier_video));
        m_info_image->setText(trad("Image {} sur {}", index, nombre_images_video));
    }
    m_info_mode->setText(simplifie() ? trad("Mode simplifié") : trad("Mode expert"));
    m_info_mode->setToolTip(trad("Pour changer de mode : menu Mode."));
}

void App::construit_raccourcis()
{
    const auto raccourci = [this](const QKeySequence &touches, auto fonction) {
        auto *a = new QAction(this);
        a->setShortcut(touches);
        connect(a, &QAction::triggered, this, [this, fonction] {
            fonction();
            rafraichit();
        });
        addAction(a);
        return a;
    };
    raccourci(QKeySequence(Qt::Key_Escape), [this] { echap(); });
    raccourci(QKeySequence(Qt::Key_F11), [this] { ouvre_plein_ecran(); });
    // changer d'image et agrandir l'interface : aussi en plein écran
    m_raccourcis_image = {
        raccourci(QKeySequence(Qt::Key_Right), [this] { touche_image(1); }),
        raccourci(QKeySequence(Qt::Key_PageDown), [this] { touche_image(1); }),
        raccourci(QKeySequence(Qt::Key_Left), [this] { touche_image(-1); }),
        raccourci(QKeySequence(Qt::Key_PageUp), [this] { touche_image(-1); }),
    };
    // Ctrl + / Ctrl − / Ctrl 0 : l'agrandissement de l'interface, de 50 à 300 %
    // agrandir : pas sous l'agrandissement voulu (celui affiché, borné par une fenêtre étroite, peut
    // être plus petit ; le retenu ne doit pas baisser)
    const auto plus = [this] {
        change_agrandissement(std::max(m_agrandissement_voulu, std::round(m_agrandissement * 10.0 + 1.0) / 10.0));
    };
    m_raccourcis_image << raccourci(QKeySequence::ZoomIn, plus) << raccourci(QKeySequence(Qt::CTRL | Qt::Key_Equal), plus)
                       << raccourci(QKeySequence::ZoomOut,
                                    [this] { change_agrandissement(std::round(m_agrandissement * 10.0 - 1.0) / 10.0); })
                       << raccourci(QKeySequence(Qt::CTRL | Qt::Key_0), [this] { change_agrandissement(1.0); });
}

void App::echap()
{
    // la touche Échap d'un champ de saisie lui revient
    if (qobject_cast<QLineEdit *>(QApplication::focusWidget()))
        return;
    if (etat == Etat::Etalonnage)
        annule_echelle();
    else if (refait)
        annule_refait();
    else if (choix_origine && mode == Mode::Expert)
        choix_origine = false;
}

void App::touche_image(int pas)
{
    const bool image_visible = simplifie() || onglet == Onglet::Pointage;
    if (image_visible && controle_image_actif())
        change_image(index + pas);
}

void App::maj_menus()
{
    const bool disponible = video && (etat == Etat::Preparation || etat == Etat::Pointage);
    const bool a_des_donnees = pointage.a_des_pointages();
    m_menu_fichier->setTitle(trad("Fichier"));
    m_ouvrir->setText(trad("Ouvrir une vidéo…"));
    m_ouvrir_projet->setText(trad("Ouvrir un projet…"));
    m_recuperer->setText(trad("Récupérer le travail précédent…"));
    m_recuperer->setToolTip(trad("Reprend une séance à partir de sa dernière sauvegarde automatique."));
    m_enregistrer->setText(trad("Enregistrer le projet…"));
    m_enregistrer->setToolTip(trad("Enregistre le chemin de la vidéo, le repère, l'échelle et les mesures, pour continuer plus tard"));
    m_enregistrer->setEnabled(enregistrement_permis());
    m_copier->setText(trad("Copier pour Python"));
    m_copier->setToolTip(trad("Copie lst_t, lst_x, lst_y (tableaux numpy), à coller dans un programme Python"));
    m_copier->setEnabled(disponible && a_des_donnees);
    m_csv->setText(trad("Enregistrer en CSV…"));
    m_csv->setEnabled(disponible && a_des_donnees);
    m_quitter->setText(trad("Quitter"));

    m_menu_edition->setTitle(trad("Édition"));
    m_defaire->setText(pointage.derniere_suppression() ? trad("Annuler la suppression") : trad("Annuler le dernier point"));
    m_defaire->setEnabled(annulation_possible());
    m_refaire->setText(trad("Rétablir le point"));
    m_refaire->setEnabled(retablissement_possible());
    m_recommencer->setText(trad("Tout recommencer"));
    m_recommencer->setToolTip(trad("Efface les mesures, l'échelle et le repère"));
    m_recommencer->setEnabled(disponible);

    m_menu_mode->setTitle(trad("Mode"));
    m_mode_simplifie->setText(trad("Mode simplifié (pas à pas)"));
    m_mode_expert->setText(trad("Mode expert (toutes les fonctions)"));
    m_mode_simplifie->setChecked(mode == Mode::Simplifie);
    m_mode_expert->setChecked(mode == Mode::Expert);

    m_menu_aide->setTitle(trad("Aide"));
    m_notice->setText(trad("Notice d'utilisation…"));
    m_a_propos->setText(trad("À propos de MeVCine…"));
    m_a_propos_qt->setText(trad("À propos de Qt…"));

    m_ouvrir->setToolTip(trad("Ouvrir une vidéo (Ctrl+O)"));
    m_mode_simplifie->setToolTip(trad("Suivre les cinq étapes du mode simplifié."));
    m_mode_expert->setToolTip(trad("Mode expert : toutes les fonctions. Les mesures sont gardées."));
    // la notice (menu Aide) ; à droite de la barre des menus, le soleil ou la lune et le drapeau, qui
    // montrent le thème et la langue affichés
    m_notice->setToolTip(trad("Ouvrir la notice d'utilisation dans le navigateur, à la page de ce que vous faites (F1)"));
    const bool sombre = dessin::sombre(QApplication::palette());
    m_theme->setIcon(icones::theme(sombre));
    m_theme->setText(sombre ? trad("Thème sombre. Cliquer pour passer au thème clair.")
                            : trad("Thème clair. Cliquer pour passer au thème sombre."));
    m_theme->setToolTip(m_theme->text());
    m_langue->setIcon(icones::drapeau(i18n::langue()));
    m_langue->setText(trad("Langue : français. Cliquer pour passer en anglais (English)."));
    m_langue->setToolTip(m_langue->text());
}

// ------------------------------------------------------------------------------------------------
// rafraîchissement
// ------------------------------------------------------------------------------------------------

void App::rafraichit()
{
    if (m_rafraichissement)
        return;
    const QScopedValueRollback garde(m_rafraichissement, true);
    // à l'étape 1 du mode Simplifié, un clic sur l'image place l'origine
    if (simplifie() && video)
        choix_origine = guide.etape == Etape::Origine && (etat == Etat::Preparation || etat == Etat::Pointage);
    if (mode == Mode::Expert && !onglets_actifs())
        onglet = Onglet::Pointage;
    if (video)
        image_courante();
    actualise_visee();
    // le titre : MeVCine — la vidéo ouverte, ou le sous-titre
    const QString titre =
        u"%1 — %2"_s.arg(a_propos::NOM, video ? QFileInfo(fichier_video).fileName() : trad("Étude cinématique de vidéos"));
    if (windowTitle() != titre)
        setWindowTitle(titre);
    maj_menus();

    if (!m_secours.isActive() && sauvegarde_de_secours())
        m_secours.start();

    if (!video) {
        m_pages->setCurrentWidget(m_sans_video);
        m_sans_video->actualise();
    } else if (simplifie()) {
        m_pages->setCurrentWidget(m_guide);
        m_guide->actualise();
    } else {
        m_pages->setCurrentWidget(m_expert);
        const Onglet onglets[] = {Onglet::Pointage, Onglet::Coordonnees, Onglet::Trajectoires, Onglet::Grapheur};
        const QString noms[] = {trad("Pointage"), trad("Coordonnées"), trad("Trajectoires"), trad("Grapheur")};
        for (int i = 0; i < 4; ++i) {
            m_expert->setTabText(i, noms[i]);
            m_expert->setTabEnabled(i, onglets[i] == Onglet::Pointage || onglets_actifs());
        }
        m_expert->setCurrentIndex(static_cast<int>(onglet));
        switch (onglet) {
        case Onglet::Pointage: m_onglet_pointage->actualise(); break;
        case Onglet::Trajectoires: m_onglet_trajectoire->actualise(); break;
        case Onglet::Coordonnees: m_onglet_coord->actualise(); break;
        case Onglet::Grapheur: m_onglet_graph->actualise(); break;
        }
    }
    // le plein écran ne sert qu'au pointage : il se ferme quand celui-ci n'est plus possible
    if (plein_ecran()) {
        if (plein_ecran_permis())
            m_plein_ecran->actualise();
        else
            quitte_plein_ecran();
    }
    maj_barre_etat();
}

// ------------------------------------------------------------------------------------------------
// dialogues
// ------------------------------------------------------------------------------------------------

void App::affiche_dialogues()
{
    // un seul dialogue à la fois, et pas sous l'écran de démarrage
    if (m_boite || dialogues.empty() || m_demarrage)
        return;
    // ni caché derrière le plein écran
    quitte_plein_ecran();
    const Dialogue &d = dialogues.front();
    if (d.type == Dialogue::Type::Echelle) {
        auto *boite = new DialogueEchelle(d.saisie, d.unite, d.texte, this);
        connect(boite, &QDialog::finished, this, [this, boite](int r) {
            if (r == QDialog::Accepted) {
                change_unite_etalon(boite->longueur().unite);
                valide_longueur_etalon(boite->longueur().metres());
            }
            ferme_dialogue(r == QDialog::Accepted);
        });
        m_boite = boite;
    } else {
        auto *boite = new QMessageBox(this);
        boite->setWindowTitle(d.titre);
        boite->setText(d.titre);
        boite->setInformativeText(d.texte);
        boite->setTextFormat(Qt::PlainText);
        // le titre du message, en demi-gras
        if (auto *titre = boite->findChild<QLabel *>(u"qt_msgbox_label"_s))
            dessin::demi_gras(titre);
        if (d.type == Dialogue::Type::Message) {
            boite->setIcon(QMessageBox::Information);
            QPushButton *ok = boite->addButton(trad("OK"), QMessageBox::AcceptRole);
            ok->setIcon(icones::commande(icones::Commande::Valider));
            boite->setDefaultButton(ok);
            boite->setEscapeButton(ok);
        } else {
            // pas de validation par Entrée : on perdrait des mesures d'une touche enfoncée par mégarde
            boite->setIcon(QMessageBox::Warning);
            QPushButton *annuler = boite->addButton(trad("Annuler"), QMessageBox::RejectRole);
            QPushButton *confirmer = boite->addButton(d.bouton, QMessageBox::DestructiveRole);
            annuler->setIcon(icones::commande(icones::Commande::Fermer));
            const auto commande = d.apres && d.apres->type == Apres::Type::ToutRecommencer ? icones::Commande::Supprimer
                                : d.apres && d.apres->type == Apres::Type::Quitter ? icones::Commande::Quitter
                                : icones::Commande::Suivant;
            confirmer->setIcon(icones::commande(commande));
            dessin::demi_gras(confirmer);
            boite->setDefaultButton(annuler);
            boite->setEscapeButton(annuler);
        }
        connect(boite, &QMessageBox::finished, this, [this, boite] {
            const auto role = boite->buttonRole(boite->clickedButton());
            ferme_dialogue(role == QMessageBox::AcceptRole || role == QMessageBox::DestructiveRole);
        });
        m_boite = boite;
    }
    m_boite->setAttribute(Qt::WA_DeleteOnClose);
    m_boite->setWindowModality(Qt::WindowModal);
    m_boite->open();
}

void App::ferme_dialogue(bool ok)
{
    m_boite = nullptr;
    if (dialogues.empty())
        return;
    const Dialogue d = dialogues.front();
    dialogues.pop_front();
    if (d.type == Dialogue::Type::Echelle) {
        if (!ok) {
            // l'étalon sélectionné est oublié : l'échelle ne change pas
            etalon_en_attente.reset();
            affiche_statut(trad("Échelle inchangée"));
        }
    } else if (ok && d.apres) {
        execute(*d.apres);
    }
    rafraichit();
    QTimer::singleShot(0, this, &App::affiche_dialogues);
}

// ------------------------------------------------------------------------------------------------
// événements de la fenêtre
// ------------------------------------------------------------------------------------------------

void App::closeEvent(QCloseEvent *event)
{
    // une vidéo en cours d'ouverture : on l'interrompt d'abord, la fenêtre se ferme ensuite
    if (ouverture_en_cours) {
        ouverture_annulee = true;
        event->ignore();
        return;
    }
    if (fermeture_confirmee || travail_en_securite()) {
        termine_seance();
        // à la fermeture, on retient le dossier des vidéos et la taille de la fenêtre
        ecrit_dimensions_conf();
        if (m_taille_normale.isValid())
            config.definit(u"taille_fenetre"_s, u"(%1,%2)"_s.arg(m_taille_normale.width()).arg(m_taille_normale.height()));
        config.definit(u"fenetre_maximisee"_s, isMaximized() ? u"True"_s : u"False"_s);
        prefs::sauve(config);
        event->accept();
        return;
    }
    // des mesures seraient perdues : on demande d'abord
    event->ignore();
    const bool deja = std::any_of(dialogues.begin(), dialogues.end(), [](const Dialogue &d) {
        return d.type == Dialogue::Type::Confirmation && d.apres && d.apres->type == Apres::Type::Quitter;
    });
    if (!deja)
        confirme_si_mesures({Apres::Type::Quitter});
}

void App::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    event->acceptProposedAction();
    m_depot->setGeometry(rect());
    m_depot->raise();
    m_depot->show();
}

void App::dragLeaveEvent(QDragLeaveEvent *)
{
    m_depot->hide();
}

void App::dropEvent(QDropEvent *event)
{
    m_depot->hide();
    QString fichier;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            fichier = url.toLocalFile();
            break;
        }
    }
    if (fichier.isEmpty() || etat == Etat::SuiviAutomatique || etat == Etat::SelectionMotifs || etat == Etat::Etalonnage || !dialogues.empty())
        return;
    event->acceptProposedAction();
    confirme_si_mesures({Apres::Type::OuvrirFichier, fichier});
    rafraichit();
}

void App::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)))
        m_taille_normale = size();
    if (m_demarrage)
        m_demarrage->setGeometry(rect());
    m_depot->setGeometry(rect());
    // Fenêtre rétrécie : l'agrandissement reste dans ses bornes ; élargie : il revient vers celui qui
    // est demandé
    if (std::clamp(m_agrandissement_voulu, AGRANDISSEMENT_MIN, std::max(agrandissement_max(), AGRANDISSEMENT_MIN))
        != m_agrandissement)
        applique_agrandissement();
    else if (m_info_video && video)
        maj_barre_etat();
}

bool App::eventFilter(QObject *objet, QEvent *event)
{
    // le message a changé de largeur : il tient peut-être sur une ligne de plus ou de moins
    if (objet == m_statut && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &App::ajuste_barre_etat);
    return QMainWindow::eventFilter(objet, event);
}

void App::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::PaletteChange || !m_statut)
        return;
    const bool sombre = dessin::sombre(QApplication::palette());
    if (m_sombre == sombre)
        return;
    m_sombre = sombre;
    // Thème clair ou sombre : les couleurs des textes, une fois la nouvelle palette transmise à tous
    // les widgets
    QTimer::singleShot(0, this, [this] {
        dessin::recolore(this);
        style_barre_etat();
        rafraichit();
        ajuste_barre_etat();
    });
}
