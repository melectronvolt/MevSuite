#include "app.h"

#include "a_propos.h"
#include "accueil.h"
#include "chemins.h"
#include "debogage.h"
#include "demarrage.h"
#include "detect.h"
#include "export.h"
#include "fichier.h"
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
#include <QFileDialog>
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
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// intervalle entre deux sauvegardes automatiques du travail, en millisecondes
constexpr int SECOURS_INTERVALLE = 2000;
/// distance, en points d'écran, à laquelle on saisit l'origine pour la faire glisser
constexpr double PRISE_ORIGINE = 12.0;

/// ajoute une extension au chemin s'il n'en a pas une des extensions permises
QString avec_extension(const QString &chemin, const QStringList &permises)
{
    const QString ext = QFileInfo(chemin).suffix();
    for (const QString &p : permises) {
        if (ext.compare(p, Qt::CaseInsensitive) == 0)
            return chemin;
    }
    return permises.isEmpty() ? chemin : chemin + u'.' + permises.first();
}

/// un chemin à afficher, avec les séparateurs du système
QString natif(const QString &chemin)
{
    return QDir::toNativeSeparators(chemin);
}

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

} // namespace

QString nom_etat(Etat e)
{
    switch (e) {
    case Etat::Debut: return u"Debut"_s;
    case Etat::A: return u"A"_s;
    case Etat::AB: return u"AB"_s;
    case Etat::B: return u"B"_s;
    case Etat::C: return u"C"_s;
    case Etat::D: return u"D"_s;
    case Etat::E: return u"E"_s;
    }
    return {};
}

App::App(const QString &fichier, std::optional<Mode> mode_impose)
    : mode(mode_impose)
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
    unite_etalon = unite_depuis_symbole(config.get(u"unite_etalon"_s).value_or(QString())).value_or(Unite::Metre);

    construit_menus();
    construit_coin_des_menus();
    m_pages = new QStackedWidget(this);
    m_accueil = new PageAccueil(*this, m_pages);
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
    for (QWidget *page : {static_cast<QWidget *>(m_accueil), static_cast<QWidget *>(m_sans_video),
                          static_cast<QWidget *>(m_guide), static_cast<QWidget *>(m_expert)})
        m_pages->addWidget(page);
    setCentralWidget(m_pages);

    construit_barre_etat();
    construit_raccourcis();
    m_depot = new Depot(this);

    // le suivi automatique : une image à la fois, sans bloquer l'interface
    m_detection.setSingleShot(true);
    m_detection.setInterval(0);
    connect(&m_detection, &QTimer::timeout, this, [this] {
        if (etat != Etat::B)
            return;
        etape_detection();
        rafraichit();
        if (etat == Etat::B)
            m_detection.start();
    });
    // la sauvegarde automatique : tout de suite après un changement, puis au plus toutes les deux
    // secondes
    m_secours.setSingleShot(true);
    m_secours.setInterval(SECOURS_INTERVALLE);
    connect(&m_secours, &QTimer::timeout, this, [this] {
        if (sauvegarde_de_secours())
            m_secours.start();
    });

    if (erreur_conf)
        message(trad("Erreur de lecture de la configuration"),
                trad("Le fichier de configuration est illisible : MeVCine repart d'une configuration neuve."));
    change_etat(Etat::Debut);
    applique_preferences();
    change_agrandissement(config.get_float(u"agrandissement"_s).value_or(1.0));
    traite_arg(fichier);

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
    case Etat::Debut: return trad("Ouvrez une vidéo");
    case Etat::A: return trad("Définissez l'échelle, ou démarrez le pointage");
    case Etat::AB:
        return trad("Préparation du suivi automatique : tracez un cadre autour de chaque objet à suivre ({} en tout)",
                    pointage.nb_obj());
    case Etat::B: return trad("Suivi automatique en cours : le bouton STOP l'interrompt");
    case Etat::C: return consigne_etalon();
    case Etat::D:
    case Etat::E: return trad("Pointage");
    }
    return {};
}

QString App::message_par_defaut() const
{
    if (!mode)
        return trad("Choisissez votre mode de travail : simplifié (pas à pas) ou expert");
    if (!video || etat == Etat::Debut)
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
    if (mode == m)
        return;
    if (m == Mode::Simplifie) {
        if (pointage.nb_obj() > 1 && pointage.a_des_pointages()) {
            // le mode simplifié ne suit qu'un objet : on demande avant d'oublier les autres
            const bool deja = std::any_of(dialogues.begin(), dialogues.end(), [](const Dialogue &d) {
                return d.apres && d.apres->type == Apres::Type::ModeSimplifie;
            });
            if (deja)
                return;
            Dialogue d{Dialogue::Type::Confirmation};
            d.titre = trad("Passer en mode simplifié ?");
            d.texte = trad("Le mode simplifié ne suit qu'un seul objet, et vos mesures en comptent {}. Seules les mesures "
                           "de l'objet 1 seront gardées.\n\nPour garder tous les objets, annulez et restez en mode expert.",
                           pointage.nb_obj());
            d.bouton = trad("Garder l'objet 1");
            d.apres = Apres{Apres::Type::ModeSimplifie};
            dialogues.push_back(d);
            QTimer::singleShot(0, this, &App::affiche_dialogues);
            return;
        }
        switch (etat) {
        case Etat::AB: change_etat(Etat::A); break;
        case Etat::B: stop_calculs(); break;
        case Etat::C: annule_echelle(); break;
        default: break;
        }
        suivi_auto = false;
        if (nb_obj != 1 || pointage.nb_obj() != 1) {
            nb_obj = 1;
            redimensionne_data();
        }
        if (pointage.a_des_pointages())
            guide.reprend(true, pointage.echelle.est_definie());
        else
            guide = Guide();
    } else {
        choix_origine = false;
        onglet = Onglet::Pointage;
    }
    annule_refait();
    glisse_origine = false;
    // le mode simplifié conduit lui-même le pointage, étape par étape
    pointage_arrete = false;
    mode = m;
}

void App::mode_simplifie_un_objet()
{
    if (mode != Mode::Expert || !(etat == Etat::A || etat == Etat::D))
        return;
    pointage.garde_premier_objet();
    nb_obj = 1;
    objet_courant = 1;
    change_mode(Mode::Simplifie);
    affiche_statut(trad("Mode simplifié : seules les mesures de l'objet 1 sont gardées"), icones::Statut::Attention);
}

// ------------------------------------------------------------------------------------------------
// machine à états
// ------------------------------------------------------------------------------------------------

void App::change_etat(Etat e)
{
    const Etat precedent = etat;
    etat = e;
    dbg_p(1, u"========> État précédent = %1. État suivant = %2"_s.arg(nom_etat(precedent), nom_etat(e)));
    statut.clear();
    switch (e) {
    case Etat::Debut:
        onglet = Onglet::Pointage;
        pointage_arrete = false;
        index = 1;
        objet_courant = 1;
        choix_origine = false;
        drag.reset();
        traj = Trajectoire();
        break;
    case Etat::A: {
        if (filename.isEmpty())
            return;
        objet_courant = 1;
        onglet = Onglet::Pointage;
        suivi_auto = false;
        pointage_arrete = false;
        drag.reset();
        // on retient le dossier des vidéos (mais on ne rouvrira pas la vidéo au prochain lancement :
        // les postes sont partagés)
        config.set(u"lastvideo"_s, natif(filename));
        config.set(u"videodir"_s, natif(QFileInfo(filename).absolutePath()));
        ecrit_dimensions_conf();
        prefs::sauve(config);
        break;
    }
    case Etat::AB:
        motifs.clear();
        rects.clear();
        points_probables.assign(pointage.nb_obj(), std::nullopt);
        message(trad("Suivi automatique"),
                trad("Tracez un cadre autour de chaque objet à suivre.\n\nLe bouton STOP arrête le suivi à tout moment."));
        break;
    case Etat::B:
        m_detection.start();
        break;
    case Etat::C:
        etat_ancien = precedent;
        drag.reset();
        etalon_deux_clics = false;
        break;
    case Etat::D:
        choix_origine = simplifie() && guide.etape == Etape::Origine;
        affiche_point_attendu();
        break;
    case Etat::E: break;
    }
}

bool App::onglets_actifs() const
{
    return etat == Etat::D || (etat == Etat::C && etat_ancien == Etat::D);
}

bool App::controle_image_actif() const
{
    return (etat == Etat::A || etat == Etat::D) && !refait;
}

bool App::repere_modifiable() const
{
    return (etat == Etat::A || etat == Etat::D) && !refait;
}

bool App::rotation_permise() const
{
    return etat == Etat::A && !pointage.echelle.est_definie() && !etalon_en_attente && !pointage.a_des_pointages();
}

// ------------------------------------------------------------------------------------------------
// vidéo
// ------------------------------------------------------------------------------------------------

bool App::ouvre_video(const QString &chemin_donne, int rotation)
{
    dbg_p(2, u"rentre dans 'ouvre_video' %1"_s.arg(chemin_donne));
    // chemin absolu, pour les préférences et les fichiers de projet
    const QString chemin = chemins::absolu(chemin_donne);
    // L'examen d'une grande vidéo (ffprobe en compte les paquets) peut prendre du temps, surtout sur une
    // clé USB : il se fait dans un autre fil, avec le curseur d'attente. La fenêtre se redessine, mais
    // ni clic ni touche n'agissent avant la fin ; la fermer interrompt l'ouverture.
    using Ouverture = std::expected<std::unique_ptr<VideoReader>, QString>;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ouverture_annulee = false;
    ouverture_en_cours = true;
    QFuture<Ouverture> tache = QtConcurrent::run([chemin, fil = thread(), annule = &ouverture_annulee] {
        Ouverture l = VideoReader::ouvre(chemin, annule);
        if (l)
            (*l)->confie_au_fil(fil);
        return l;
    });
    QEventLoop attente;
    QFutureWatcher<Ouverture> fin;
    connect(&fin, &QFutureWatcherBase::finished, &attente, &QEventLoop::quit);
    fin.setFuture(tache);
    if (!tache.isFinished())
        attente.exec(QEventLoop::ExcludeUserInputEvents);
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
        message(trad("Vidéo illisible"), trad("MeVCine n'arrive pas à lire la vidéo :\n{}\n\n{}", natif(chemin), lecteur.error()));
        return false;
    }
    (*lecteur)->set_rotation(rotation);
    const VideoInfo info = (*lecteur)->info;
    image_max = info.nb_images;
    // vidéo au ralenti : les calculs se font avec la cadence de prise de vue, et non celle de lecture
    fps = info.fps_capture.value_or(info.fps);
    video = std::move(*lecteur);
    filename = chemin;
    image_cle.reset();
    image = QImage();
    ips = nombre_local(fps, 6);
    if (etat != Etat::Debut)
        change_etat(Etat::Debut);
    reinitialise_capture();
    if (info.fps_capture) {
        const QString filmee = nombre_local(*info.fps_capture, 6), lue = nombre_local(info.fps, 6);
        if (*info.fps_capture > info.fps)
            message(trad("Vidéo au ralenti"),
                    trad("Cette vidéo au ralenti a été filmée à {} images par seconde, et se lit à {} images par seconde.\n\n"
                         "Les dates sont calculées avec la cadence de prise de vue : {} images par seconde. "
                         "On peut la corriger dans le champ « images par seconde ».",
                         filmee, lue, filmee));
        else
            message(trad("Vidéo en accéléré"),
                    trad("Cette vidéo en accéléré a été filmée à {} images par seconde, et se lit à {} images par seconde.\n\n"
                         "Les dates sont calculées avec la cadence de prise de vue : {} images par seconde. "
                         "On peut la corriger dans le champ « images par seconde ».",
                         filmee, lue, filmee));
    }
    return true;
}

void App::ecrit_dimensions_conf()
{
    const QSize d = dimensions();
    config.set(u"taille_image"_s, u"(%1,%2)"_s.arg(d.width()).arg(d.height()));
    config.set(u"rotation"_s, QString::number(video ? video->rotation() : 0));
}

void App::reinit_origine()
{
    const QSize d = dimensions();
    pointage.origine = {double(d.width() / 2), double(d.height() / 2)};
}

void App::redimensionne_data()
{
    if (image_max > 0 && fps > 0.0) {
        pointage.dimensionne(nb_obj, 1.0 / fps, image_max);
        pointage.instants = video ? video->info.instants() : std::nullopt;
        pointage.cadence_irreguliere = video && video->info.cadence_variable;
    }
}

void App::reinitialise_capture()
{
    dbg_p(2, u"rentre dans 'reinitialise_capture'"_s);
    pointage.echelle = Echelle();
    etalon_en_attente.reset();
    index = 1;
    reinit_origine();
    motifs.clear();
    rects.clear();
    points_probables.clear();
    pile_detections.clear();
    refait.reset();
    redimensionne_data();
    pointage.sens_x = 1;
    pointage.sens_y = 1;
    guide = Guide();
    change_etat(Etat::A);
}

std::optional<QImage> App::image_courante()
{
    if (!video)
        return std::nullopt;
    const std::pair cle{index, video->rotation()};
    if (image_cle == cle && !image.isNull())
        return image;
    auto img = video->image(index);
    if (!img) {
        dbg_p(1, u"image %1 illisible : %2"_s.arg(index).arg(img.error()));
        affiche_statut(trad("Image {} illisible : {}", index, img.error()), icones::Statut::Erreur);
        return std::nullopt;
    }
    image = *img;
    image_cle = cle;
    return image;
}

void App::tourne_image(int increment)
{
    if (!rotation_permise() || !video)
        return;
    video->set_rotation(((video->rotation() + increment) % 360 + 360) % 360);
    image_cle.reset();
    reinit_origine();
    guide.origine_choisie = false;
    redimensionne_data();
    ecrit_dimensions_conf();
}

void App::change_image(int i)
{
    index = std::clamp(i, 1, std::max(image_max, 1));
}

// ------------------------------------------------------------------------------------------------
// pointage
// ------------------------------------------------------------------------------------------------

bool App::pointage_ok() const
{
    if (!(etat == Etat::D || etat == Etat::E) || choix_origine)
        return false;
    // pointage arrêté : seule la mesure demandée par le tableau se refait
    if (pointage_arrete && !refait)
        return false;
    if (simplifie() && !guide.pointage_permis(refait.has_value()))
        return false;
    const auto prem = pointage.premiere_image(), der = pointage.derniere_image();
    if (prem && der)
        return index + 1 >= *prem && index <= *der + 1;
    return true;
}

void App::affiche_point_attendu()
{
    if (refait)
        affiche_statut(trad("Cliquez sur l'objet pour refaire la mesure de l'image {} (Échap : annuler)", index));
    else if (pointage_arrete)
        affiche_statut(trad("Pointage arrêté : « ▶ Reprendre le pointage » pour continuer"));
    else if (pointage.nb_obj() > 1)
        affiche_statut(trad("Cliquez sur l'objet {}", objet_courant));
    else
        affiche_statut(trad("Cliquez sur l'objet"));
}

void App::clic_pointage(Vecteur p)
{
    if (!pointage_ok())
        return;
    change_etat(Etat::E);
    pointage.pointe(objet_courant, p, index - 1);
    objet_suivant();
    termine_pointage();
    if (objet_courant == 1 && refait) {
        // la mesure demandée par le tableau est refaite : on revient là où l'on en était
        const int retour = *refait;
        refait.reset();
        change_image(retour);
        if (mode == Mode::Expert)
            onglet = Onglet::Coordonnees;
        affiche_statut(trad("Mesure refaite"), icones::Statut::Reussite);
    }
}

void App::objet_suivant()
{
    if (objet_courant < pointage.nb_obj()) {
        ++objet_courant;
        return;
    }
    objet_courant = 1;
    if (index < image_max)
        ++index;
    // on revient à l'état D sauf en cas de suivi automatique
    if (etat != Etat::B)
        change_etat(Etat::D);
}

void App::termine_pointage()
{
    pointage.purge_defaits();
    prepare_futur_clic();
}

void App::prepare_futur_clic()
{
    if (etat != Etat::B)
        affiche_point_attendu();
}

bool App::peut_defaire() const
{
    return etat == Etat::D && !refait && pointage.peut_defaire();
}

bool App::peut_refaire() const
{
    return etat == Etat::D && !refait && pointage.peut_refaire();
}

void App::efface_point_precedent()
{
    dbg_p(2, u"rentre dans 'efface_point_precedent'"_s);
    if (!peut_defaire())
        return;
    const auto der = pointage.derniere_image();
    pointage.defaire();
    // on revient sur l'image dont on vient d'effacer le pointage
    if (der)
        change_image(*der);
    objet_courant = 1;
    prepare_futur_clic();
}

void App::refait_point_suivant()
{
    dbg_p(2, u"rentre dans 'refait_point_suivant'"_s);
    if (!peut_refaire())
        return;
    pointage.refaire();
    if (const auto der = pointage.derniere_image())
        change_image(*der + 1);
    objet_courant = 1;
    prepare_futur_clic();
}

void App::refait_point_depuis_tableau(int i)
{
    if (etat != Etat::D || choix_origine)
        return;
    if (!refait)
        refait = index;
    objet_courant = 1;
    change_image(i);
    onglet = Onglet::Pointage;
    if (simplifie() && guide.etape < Etape::Pointage)
        guide.etape = Etape::Tableau;
    prepare_futur_clic();
}

void App::annule_refait()
{
    if (!refait)
        return;
    const int retour = *refait;
    refait.reset();
    change_image(retour);
    objet_courant = 1;
    if (mode == Mode::Expert)
        onglet = Onglet::Coordonnees;
    prepare_futur_clic();
}

void App::debut_capture()
{
    if (etat == Etat::D && pointage_arrete) {
        // reprise du pointage arrêté : les mesures sont gardées
        pointage_arrete = false;
        affiche_point_attendu();
        return;
    }
    if (pointage.nb_obj() != nb_obj || pointage.len() != image_max)
        redimensionne_data();
    change_etat(suivi_auto && !simplifie() ? Etat::AB : Etat::D);
}

void App::arrete_pointage()
{
    if (etat != Etat::D || refait || choix_origine)
        return;
    quitte_plein_ecran();
    if (simplifie()) {
        // comme « Terminer le pointage ▶ »
        va_a_l_etape(Etape::Tableau);
        return;
    }
    pointage_arrete = true;
    objet_courant = 1;
    // ce qui compte ensuite : les coordonnées
    if (pointage.a_des_pointages())
        onglet = Onglet::Coordonnees;
    affiche_point_attendu();
}

bool App::plein_ecran_permis() const
{
    if (!video || !(etat == Etat::D || etat == Etat::E) || choix_origine)
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
        affiche_point_attendu();
    }
    onglet = Onglet::Pointage;
    if (!m_plein_ecran)
        m_plein_ecran = new PleinEcran(*this, this);
    m_plein_ecran->showFullScreen();
    m_plein_ecran->activateWindow();
}

void App::quitte_plein_ecran()
{
    if (!plein_ecran())
        return;
    m_plein_ecran->hide();
    activateWindow();
}

QList<QAction *> App::raccourcis_plein_ecran() const
{
    return m_raccourcis_image + QList<QAction *>{m_defaire, m_refaire};
}

void App::verifie_ips()
{
    if (ips.trimmed().isEmpty())
        return;
    const auto v = parse_float(ips);
    if (v && *v > 0.0 && std::isfinite(*v)) {
        fps = *v;
        pointage.delta_t = 1.0 / *v;
        statut.clear();
    } else {
        affiche_statut(trad("Le nombre d'images par seconde doit être un nombre positif"), icones::Statut::Erreur);
    }
}

// ------------------------------------------------------------------------------------------------
// échelle et origine
// ------------------------------------------------------------------------------------------------

void App::demande_echelle()
{
    if (!(etat == Etat::A || etat == Etat::D) || refait)
        return;
    choix_origine = false;
    etalon_en_attente.reset();
    change_etat(Etat::C);
}

void App::fin_echelle(Vecteur p1, Vecteur p2)
{
    if ((p1 - p2).norme() < 2.0)
        // trop court : on attend une vraie sélection
        return;
    etalon_en_attente = {p1, p2};
    drag.reset();
    etalon_deux_clics = false;
    change_etat(etat_ancien);
    if (simplifie()) {
        guide.erreur_etalon.reset();
        guide.focus_saisie = true;
        affiche_statut(trad("Indiquez la longueur réelle de l'étalon et son unité, puis validez"), icones::Statut::Attention);
    } else {
        const Echelle &e = pointage.echelle;
        Dialogue d{Dialogue::Type::Echelle};
        d.saisie = e.est_definie() ? nombre_local(e.longueur_reelle_etalon / en_metre(unite_etalon), 6) : QString();
        d.unite = unite_etalon;
        dialogues.push_back(d);
        QTimer::singleShot(0, this, &App::affiche_dialogues);
    }
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
    if (etat == Etat::C)
        change_etat(etat_ancien);
    affiche_statut(trad("Échelle inchangée"));
}

void App::change_unite_etalon(Unite u)
{
    unite_etalon = u;
    config.set(u"unite_etalon"_s, symbole(u));
}

void App::nouvelle_origine()
{
    if (!repere_modifiable())
        return;
    choix_origine = !choix_origine;
    statut.clear();
}

void App::origine_placee()
{
    if (simplifie()) {
        guide.origine_choisie = true;
    } else {
        choix_origine = false;
        affiche_statut(trad("Origine déplacée : les mesures sont recalculées"), icones::Statut::Reussite);
    }
}

QString App::texte_echelle() const
{
    const Echelle &e = pointage.echelle;
    if (!e.est_definie())
        return trad("indéfinie");
    return nombre_local(e.px_par_m(), 4);
}

std::optional<App::Coordonnees> App::coords(Vecteur p) const
{
    if (simplifie() && !(guide.origine_choisie && guide.axes_choisis))
        return std::nullopt;
    const Vecteur q = (p - pointage.origine).redresse(pointage.sens_x, pointage.sens_y);
    Coordonnees c{QString::number(std::llround(q.x)), QString::number(std::llround(q.y)), std::nullopt};
    const Echelle &e = pointage.echelle;
    if (e.est_definie())
        c.metre = std::pair{nombre_local(q.x / e.px_par_m(), 3), nombre_local(q.y / e.px_par_m(), 3)};
    return c;
}

QString App::label_zoom() const
{
    if (choix_origine)
        return trad("Origine du repère");
    switch (etat) {
    case Etat::D:
    case Etat::E:
        if (refait)
            return trad("Mesure à refaire");
        if (pointage.nb_obj() > 1)
            return trad("Pointage de l'objet {}", objet_courant);
        return trad("Pointage");
    case Etat::AB: return trad("Zone à suivre n° {}", motifs.size() + 1);
    case Etat::C: return trad("Étalon");
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
    case Etat::C:
    case Etat::AB: return true;
    case Etat::D:
    case Etat::E: return pointage_ok();
    default: return false;
    }
}

bool App::origine_deplacable() const
{
    if (!video || refait || !(etat == Etat::A || etat == Etat::D))
        return false;
    if (simplifie())
        return guide.etape == Etape::Origine || (guide.etape == Etape::Axes && guide.origine_choisie);
    return choix_origine || etat == Etat::A;
}

bool App::pres_de_l_origine(Vecteur p, double echelle) const
{
    return (p - pointage.origine).norme() * echelle <= PRISE_ORIGINE;
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

void App::souris_bouge(std::optional<Vecteur> position, double echelle)
{
    souris = position;
    vue_echelle = echelle;
    // sélection de l'étalon en deux clics : le trait suit la souris
    if (etat == Etat::C && etalon_deux_clics && drag && souris)
        (*drag)[1] = *souris;
    if (plein_ecran())
        m_plein_ecran->maj_souris();
    else if (m_guide && m_pages->currentWidget() == m_guide)
        m_guide->maj_souris();
    else if (m_pages->currentWidget() == m_expert)
        m_onglet_pointage->maj_souris();
}

void App::geste(const Geste &g, const dessin::Vue &vue)
{
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
    } else if (etat == Etat::C) {
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
                if ((p2 - p1).norme() * vue.echelle >= 6.0) {
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
    } else if (etat == Etat::AB) {
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
    } else if (etat == Etat::D || etat == Etat::E) {
        // on pointe au relâchement du bouton, même après un petit glissement
        if ((g.type == T::Clic || g.type == T::FinGlisse) && pointage_ok() && dans_image(g.position))
            clic_pointage(g.position);
    }
    if (g.type == T::Glisse)
        // pendant un glissement, seule l'image change
        souris_bouge(g.position, vue.echelle);
    else
        rafraichit();
}

// ------------------------------------------------------------------------------------------------
// suivi automatique
// ------------------------------------------------------------------------------------------------

void App::fin_selection_motif(Vecteur p1, Vecteur p2)
{
    const double x1 = std::max(std::min(p1.x, p2.x), 0.0), y1 = std::max(std::min(p1.y, p2.y), 0.0);
    const double w = std::abs(p1.x - p2.x), h = std::abs(p1.y - p2.y);
    if (w < 3.0 || h < 3.0)
        return;
    const auto img = image_courante();
    if (!img)
        return;
    const auto motif = detect::decoupe(*img, static_cast<int>(std::lround(x1)), static_cast<int>(std::lround(y1)),
                                       static_cast<int>(std::lround(w)), static_cast<int>(std::lround(h)));
    if (!motif)
        return;
    rects.push_back({p1, p2});
    motifs.push_back(*motif);
    if (static_cast<int>(motifs.size()) == pointage.nb_obj()) {
        dbg_p(3, u"sélection des motifs finie"_s);
        pile_detections.clear();
        for (int i = index; i <= image_max; ++i)
            pile_detections.push_back(i);
        objet_courant = 1;
        change_etat(Etat::B);
    }
}

void App::etape_detection()
{
    if (pile_detections.empty()) {
        // fin de la détection automatique (ou bouton STOP)
        termine_pointage();
        change_etat(Etat::D);
        return;
    }
    const int n = pile_detections.front();
    pile_detections.pop_front();
    dbg_p(2, u"détection sur l'image %1"_s.arg(n));
    const auto img = image_courante();
    if (!img) {
        pile_detections.clear();
        return;
    }
    for (size_t j = 0; j < motifs.size(); ++j) {
        const QImage &motif = motifs[j];
        const auto point = detect::detect_part(motif, *img, points_probables[j]);
        if (!point)
            continue;
        points_probables[j] = point;
        // on recentre la détection par rapport au motif
        const Vecteur centre(point->x() + motif.width() / 2.0, point->y() + motif.height() / 2.0);
        pointage.pointe(objet_courant, centre, index - 1);
        objet_suivant();
    }
    pointage.purge_defaits();
}

void App::stop_calculs()
{
    pile_detections.clear();
}

// ------------------------------------------------------------------------------------------------
// fichiers
// ------------------------------------------------------------------------------------------------

void App::openfile(const QString &dossier)
{
    const QString filtre = trad("Vidéos (mp4, mov, avi, mkv, webm...)") + u" ("_s + chemins::filtre_videos().join(u' ')
        + u");;"_s + trad("Tous les fichiers") + u" (*)"_s;
    const QString chemin = QFileDialog::getOpenFileName(this, trad("Ouvrir une vidéo"), dossier, filtre);
    if (!chemin.isEmpty())
        ouvre_video(chemin, 0);
}

QString App::dossier_prefere() const
{
    const QString d = QDir::fromNativeSeparators(config.get(u"videodir"_s).value_or(QString()));
    return !d.isEmpty() && QFileInfo(d).isDir() ? d : chemins::dossier_videos_utilisateur();
}

void App::openfile_dossier_prefere()
{
    confirme_si_mesures({Apres::Type::OuvrirVideo, dossier_prefere()});
}

QString App::empreinte_mesures() const
{
    // l'export (il dépend aussi du repère et de l'échelle), puis chaque pointage en pixels, lignes
    // incomplètes comprises : avec plusieurs objets, l'export ne garde que les lignes complètes
    QString s = nom_video().value_or(QString()) + u'\n' + texte_csv(pointage);
    for (int i = 0; i < pointage.len(); ++i) {
        for (const Position &p : pointage.ligne(i))
            s += p ? u"%1 %2;"_s.arg(p->x, 0, 'g', 17).arg(p->y, 0, 'g', 17) : u"-;"_s;
        s += u'\n';
    }
    return s;
}

bool App::travail_en_securite() const
{
    return !pointage.a_des_pointages() || empreinte_sauvee == empreinte_mesures();
}

void App::note_travail_sauve()
{
    empreinte_sauvee = empreinte_mesures();
}

void App::confirme_si_mesures(const Apres &apres)
{
    if (travail_en_securite()) {
        execute(apres);
        return;
    }
    const QString garder =
        trad("Pour les garder, annulez, puis copiez-les pour Python ou enregistrez-les (fichier CSV, ou projet MeVCine).");
    Dialogue d{Dialogue::Type::Confirmation};
    d.apres = apres;
    switch (apres.type) {
    case Apres::Type::Quitter:
        d.titre = trad("Quitter MeVCine ?");
        d.texte = trad("Vos mesures n'ont été ni copiées ni enregistrées : elles seront perdues.") + u"\n\n"_s + garder;
        d.bouton = trad("Quitter sans enregistrer");
        break;
    case Apres::Type::ToutRecommencer:
        d.titre = trad("Tout recommencer ?");
        d.texte = trad("Les mesures, l'échelle et le repère seront effacés.") + u"\n\n"_s + garder;
        d.bouton = trad("Tout effacer");
        break;
    default:
        d.titre = trad("Abandonner les mesures ?");
        d.texte = trad("Vos mesures n'ont été ni copiées ni enregistrées : ouvrir un autre fichier les effacera.")
            + u"\n\n"_s + garder;
        d.bouton = trad("Continuer sans enregistrer");
    }
    dialogues.push_back(d);
    QTimer::singleShot(0, this, &App::affiche_dialogues);
}

void App::execute(const Apres &apres)
{
    switch (apres.type) {
    case Apres::Type::ModeSimplifie: mode_simplifie_un_objet(); break;
    case Apres::Type::ToutRecommencer: tout_recommencer(); break;
    case Apres::Type::OuvrirVideo: openfile(apres.chemin); break;
    case Apres::Type::OuvrirProjet: rouvre_ui(); break;
    case Apres::Type::OuvrirFichier: traite_arg(apres.chemin); break;
    case Apres::Type::Quitter:
        fermeture_confirmee = true;
        QTimer::singleShot(0, this, &QWidget::close);
        break;
    }
    rafraichit();
}

bool App::sauvegarde_de_secours()
{
    if (!video || !pointage.a_des_pointages() || !(etat == Etat::A || etat == Etat::D || etat == Etat::E))
        return false;
    const QString texte = texte_projet();
    if (texte == secours_texte)
        return true;
    if (const QString e = prefs::ecrit_secours(texte); e.isEmpty()) {
        secours_texte = texte;
        secours_en_echec = false;
    } else {
        dbg_p(1, u"sauvegarde de secours impossible : %1"_s.arg(e));
        // dit une fois (pas à chaque essai), dans une boîte : le message de la barre d'état serait
        // aussitôt remplacé par la consigne suivante, et le travail n'est plus protégé
        if (!secours_en_echec) {
            secours_en_echec = true;
            message(trad("Sauvegarde automatique impossible"),
                    trad("La sauvegarde automatique est impossible ({}) : enregistrez le projet (menu Fichier).", e));
        }
    }
    return true;
}

void App::rouvre_ui()
{
    const QString chemin = QFileDialog::getOpenFileName(
        this, trad("Ouvrir un projet MeVCine"), chemins::document_dir(),
        trad("Projet MeVCine") + u" (*.mevcine *.MEVCINE *.cinemev *.CINEMEV *.mecavideo *.MECAVIDEO)"_s);
    if (!chemin.isEmpty())
        rouvre(chemin);
}

QString App::nom_de_base() const
{
    return filename.isEmpty() ? trad("mesures") : QFileInfo(filename).completeBaseName();
}

std::optional<QString> App::nom_video() const
{
    if (filename.isEmpty())
        return std::nullopt;
    return QFileInfo(filename).fileName();
}

void App::enregistre_ui()
{
    if (!video || !(etat == Etat::A || etat == Etat::D))
        return;
    const QString propose = QDir(chemins::document_dir()).filePath(nom_de_base() + u'.' + prefs::EXTENSION_PROJET);
    const QString chemin = QFileDialog::getSaveFileName(this, trad("Enregistrer le projet MeVCine"), propose,
                                                        trad("Projet MeVCine") + u" (*.mevcine)"_s);
    if (!chemin.isEmpty())
        enregistre(avec_extension(chemin, {prefs::EXTENSION_PROJET}));
}

prefs::Config App::config_projet() const
{
    const Pointage &p = pointage;
    const Echelle &e = p.echelle;
    const auto point = [](Vecteur v) { return u"(%1, %2)"_s.arg(texte_reel(v.x), texte_reel(v.y)); };
    prefs::Config c = config;
    c.set(u"version"_s, u"mevcine "_s + prefs::VERSION);
    c.set(u"proximite"_s, traj.pres_souris ? u"True"_s : u"False"_s);
    if (!filename.isEmpty()) {
        c.set(u"lastvideo"_s, natif(filename));
        c.set(u"videodir"_s, natif(QFileInfo(filename).absolutePath()));
    }
    c.set(u"niveaudbg"_s, QString::number(NIVEAU_DBG.load()));
    c.set(u"sens_x"_s, QString::number(p.sens_x));
    c.set(u"sens_y"_s, QString::number(p.sens_y));
    c.set(u"origine"_s, point(p.origine));
    c.set(u"index_depart"_s, QString::number(p.premiere_image().value_or(1)));
    c.set(u"etalon_m"_s, texte_reel(e.longueur_reelle_etalon));
    c.set(u"unite_etalon"_s, symbole(unite_etalon));
    c.set(u"etalon_px"_s, texte_reel(e.longueur_pixel_etalon()));
    c.set(u"etalon_org"_s, e.est_definie() ? point(e.p1) : u"None"_s);
    c.set(u"etalon_ext"_s, e.est_definie() ? point(e.p2) : u"None"_s);
    c.set(u"deltat"_s, texte_reel(p.delta_t));
    c.set(u"nb_obj"_s, QString::number(p.nb_obj()));
    const QSize d = dimensions();
    c.set(u"taille_image"_s, u"(%1,%2)"_s.arg(d.width()).arg(d.height()));
    c.set(u"rotation"_s, QString::number(video ? video->rotation() : 0));
    return c;
}

void App::sauve_prefs()
{
    config = config_projet();
    prefs::sauve(config);
}

QString App::texte_projet() const
{
    // virgule décimale et commentaire en français, comme les anciens fichiers .mecavideo, quelle
    // que soit la langue de l'interface
    const QString donnees = pointage.csv_string(u"\t"_s, true, pointage.premiere_image().value_or(1), pointage.origine)
                                .replace(u'.', u',');
    const QString message = u"temps en seconde, positions en "_s + (pointage.echelle.est_definie() ? u"mètre"_s : u"pixel"_s);
    return prefs::texte_projet(config_projet(), message, donnees);
}

void App::enregistre(const QString &chemin)
{
    sauve_prefs();
    QSaveFile f(chemin);
    if (f.open(QIODevice::WriteOnly) && ecrit_tout(f, texte_projet().toUtf8()) && f.commit()) {
        note_travail_sauve();
        affiche_statut(trad("Projet enregistré : {}", natif(chemin)), icones::Statut::Reussite);
    } else {
        message(trad("Erreur lors de l'enregistrement"),
                trad("Le projet n'a pas pu être enregistré :\n{}\n\n{}", natif(chemin), f.errorString()));
    }
}

void App::rouvre(const QString &fichier)
{
    dbg_p(2, u"rentre dans 'rouvre'"_s);
    const auto projet = prefs::lit_projet(fichier);
    if (!projet) {
        message(trad("Projet illisible"), natif(fichier) + u'\n' + projet.error());
        return;
    }
    const prefs::Config &c = projet->config;
    if (!version_conf_ok(c))
        return;
    // un étalon de longueur nulle ou négative rendrait toutes les positions infinies
    if (const auto longueur = c.get_float(u"etalon_m"_s); longueur && !(std::isfinite(*longueur) && *longueur > 0.0)) {
        message(trad("Projet illisible"),
                natif(fichier) + u'\n' + trad("La longueur de l'étalon ({} m) n'est pas valide.", texte_reel(*longueur)));
        return;
    }
    // la vidéo, ou à défaut une vidéo de même nom à côté du projet
    QString chemin_video = QDir::fromNativeSeparators(c.get(u"lastvideo"_s).value_or(QString()));
    if (!QFileInfo(chemin_video).isFile()) {
        // le nom de la vidéo, qu'elle ait été enregistrée sous Windows ou sous Linux
        const QString nom = chemin_video.section(u'\\', -1).section(u'/', -1);
        const QString voisine = QFileInfo(fichier).dir().filePath(nom);
        if (!nom.isEmpty() && QFileInfo(voisine).isFile())
            chemin_video = voisine;
    }
    const int rotation = static_cast<int>(((c.get_int(u"rotation"_s).value_or(0) % 360) + 360) % 360);
    if (!QFileInfo(chemin_video).isFile()) {
        message(trad("Vidéo introuvable"),
                trad("La vidéo de ce projet est introuvable :\n{}\n\nPlacez-la à côté du fichier de projet, puis rouvrez-le.",
                     natif(chemin_video)));
        return;
    }
    if (!ouvre_video(chemin_video, rotation))
        return;
    // les anciens fichiers .mecavideo donnent des positions en pixels de l'écran, pour une image de
    // dimension taille_image
    const QSize d = dimensions();
    double fx = 1.0, fy = 1.0;
    if (const auto t = c.get_vecteur(u"taille_image"_s); t && t->x > 0 && t->y > 0) {
        fx = d.width() / t->x;
        fy = d.height() / t->y;
    }
    const auto ecran_vers_image = [&](Vecteur v) { return Vecteur(v.x * fx, v.y * fy); };
    pointage.sens_x = c.get_int(u"sens_x"_s) == -1 ? -1 : 1;
    pointage.sens_y = c.get_int(u"sens_y"_s) == -1 ? -1 : 1;
    if (const auto o = c.get_vecteur(u"origine"_s))
        pointage.origine = ecran_vers_image(*o);
    if (const auto dt = c.get_float(u"deltat"_s); dt && *dt > 0.0) {
        fps = 1.0 / *dt;
        ips = nombre_local(fps, 6);
    }
    nb_obj = static_cast<int>(std::clamp<qint64>(c.get_int(u"nb_obj"_s).value_or(1), 1, 20));
    Echelle echelle;
    echelle.longueur_reelle_etalon = c.get_float(u"etalon_m"_s).value_or(1.0);
    const auto p1 = c.get_vecteur(u"etalon_org"_s), p2 = c.get_vecteur(u"etalon_ext"_s);
    if (p1 && p2) {
        echelle.p1 = ecran_vers_image(*p1);
        echelle.p2 = ecran_vers_image(*p2);
    }
    if (const auto u = unite_depuis_symbole(c.get(u"unite_etalon"_s).value_or(QString())))
        change_unite_etalon(*u);
    const Vecteur origine = pointage.origine;
    redimensionne_data();
    pointage.origine = origine;
    pointage.echelle = echelle;
    const int depart = static_cast<int>(std::max<qint64>(c.get_int(u"index_depart"_s).value_or(1), 1));
    pointage.restaure_pointages(projet->donnees, depart);
    config.set(u"lastvideo"_s, natif(chemin_video));
    if (simplifie() && nb_obj > 1) {
        // le mode simplifié ne suit qu'un objet
        mode = Mode::Expert;
        message(trad("Mode expert"), trad("Ce projet suit plusieurs objets : il s'ouvre en mode expert."));
    }
    guide.reprend(pointage.a_des_pointages(), pointage.echelle.est_definie());
    change_etat(Etat::D);
    const auto der = pointage.derniere_image();
    index = der ? std::min(*der + 1, image_max) : 1;
    prepare_futur_clic();
    // ce qui vient d'un fichier ne risque rien
    note_travail_sauve();
}

bool App::version_conf_ok(const prefs::Config &c)
{
    if (c.version_ok())
        return true;
    const auto [majeur, mineur] = c.version();
    message(trad("Fichier trop ancien"),
            trad("Ce fichier a été écrit par une version trop ancienne ({}, antérieure à {}) : il ne peut pas être pris en "
                 "compte.",
                 u"%1.%2"_s.arg(majeur).arg(mineur), u"%1.%2"_s.arg(prefs::VERSION_MIN.first).arg(prefs::VERSION_MIN.second)));
    return false;
}

bool App::traite_arg(const QString &fichier)
{
    if (fichier.isEmpty())
        return false;
    if (prefs::est_projet(fichier)) {
        rouvre(fichier);
        return true;
    }
    if (QFileInfo(fichier).isFile())
        return ouvre_video(fichier, 0);
    message(trad("Fichier introuvable"), trad("Le fichier {} n'existe pas.", natif(fichier)));
    return false;
}

void App::applique_preferences()
{
    if (!config.version_ok()) {
        config = prefs::Config();
        return;
    }
    traj.pres_souris = config.get(u"proximite"_s) == u"True"_s;
}

void App::export_csv()
{
    if (!pointage.a_des_pointages()) {
        message(trad("Aucune mesure"), trad("Pointez d'abord l'objet sur quelques images."));
        return;
    }
    const QString propose = QDir(chemins::document_dir()).filePath(nom_de_base() + u".csv"_s);
    const QString chemin = QFileDialog::getSaveFileName(this, trad("Enregistrer les mesures en CSV"), propose,
                                                        trad("Fichier CSV") + u" (*.csv)"_s);
    if (!chemin.isEmpty())
        ecrit_csv(avec_extension(chemin, {u"csv"_s}));
}

void App::ecrit_csv(const QString &chemin)
{
    if (const QString e = ::ecrit_csv(chemin, pointage); e.isEmpty()) {
        note_travail_sauve();
        affiche_statut(trad("Mesures enregistrées : {}", natif(chemin)), icones::Statut::Reussite);
    } else {
        message(trad("Erreur lors de l'enregistrement"),
                trad("Le fichier CSV n'a pas pu être enregistré :\n{}\n\n{}", natif(chemin), e));
    }
}

QString App::texte_python() const
{
    return ::texte_python(pointage, nom_video());
}

void App::copie_python()
{
    if (!pointage.a_des_pointages()) {
        message(trad("Aucune mesure"), trad("Pointez d'abord l'objet sur quelques images."));
        return;
    }
    const QString texte = texte_python();
    QGuiApplication::clipboard()->setText(texte);
    presse_papier = texte;
    note_travail_sauve();
    affiche_statut(trad("Mesures copiées dans le presse-papiers : collez-les (Ctrl+V) dans votre programme Python"),
                   icones::Statut::Reussite);
}

bool App::copie_a_jour() const
{
    return !presse_papier.isEmpty() && presse_papier == texte_python();
}

void App::tout_recommencer()
{
    if (!video || !(etat == Etat::A || etat == Etat::D))
        return;
    reinitialise_capture();
}

void App::enregistre_image(QWidget *zone, const QString &titre_dialogue, const QString &filtre, const QString &extension)
{
    const QString propose = QDir(chemins::document_dir()).filePath(nom_de_base() + u'.' + extension);
    QString chemin = QFileDialog::getSaveFileName(this, titre_dialogue, propose, filtre);
    if (chemin.isEmpty())
        return;
    const QStringList permises = extension == u"png" ? QStringList{u"png"_s} : QStringList{u"jpg"_s, u"png"_s, u"jpeg"_s};
    chemin = avec_extension(chemin, permises);
    if (!zone->grab().save(chemin))
        message(trad("Erreur lors de l'enregistrement"),
                trad("L'image n'a pas pu être enregistrée :\n{}\n\n{}", natif(chemin), trad("image vide")));
}

// ------------------------------------------------------------------------------------------------
// aide, langue
// ------------------------------------------------------------------------------------------------

void App::aide_page(const QString &page)
{
    // copie dans le dossier temporaire (sinon, la notice d'origine)
    const QString copie = chemins::copie_aide();
    const QDir dossier(copie.isEmpty() ? chemins::help_dir() : copie);
    for (const QString &code : {i18n::code(i18n::langue()), u"en"_s, u"fr"_s}) {
        const QString f = dossier.filePath(code + u'/' + page);
        if (QFileInfo(f).isFile() && chemins::ouvre_avec_le_bureau(f))
            return;
    }
    message(trad("Aide"), trad("La notice est introuvable : le dossier {} manque à côté du programme.", natif(dossier.path())));
}

QString App::page_d_aide() const
{
    if (!mode)
        return u"index.html"_s;
    if (!video)
        return u"demarrer.html"_s;
    return simplifie() ? u"simplifie.html"_s : u"expert.html"_s;
}

void App::aide()
{
    aide_page(page_d_aide());
}

void App::change_langue(std::optional<Langue> l)
{
    config.set(u"langue"_s, l ? i18n::code(*l) : QString());
    i18n::choisit(l.value_or(i18n::langue_du_systeme()));
    // la valeur affichée du nombre d'images par seconde suit la langue
    if (const auto v = parse_float(ips); v && std::abs(*v - fps) < 1e-9)
        ips = nombre_local(fps, 6);
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
    config.set(u"theme"_s, theme::en_texte(choix));
    prefs::sauve(config);
    // les couleurs suivent la palette (changeEvent)
    theme::choisit(choix);
}

void App::montre()
{
    QSize taille = TAILLE_FENETRE;
    if (const auto t = config.get_vecteur(u"taille_fenetre"_s); t && t->x >= 400 && t->y >= 300)
        taille = QSize(static_cast<int>(t->x), static_cast<int>(t->y));
    resize(taille);
    bool agrandie = config.get(u"fenetre_maximisee"_s) == u"True"_s;
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
    if (config.get(u"agrandissement"_s).value_or(u"1"_s) != texte) {
        config.set(u"agrandissement"_s, texte);
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

void App::propos()
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
    // Les menus donnent tout : il n'y a pas de barre d'outils
    const auto action = [this](QMenu *menu, auto fonction) {
        auto *a = menu->addAction(QString());
        connect(a, &QAction::triggered, this, [this, fonction] {
            fonction();
            rafraichit();
        });
        return a;
    };
    m_menu_fichier = menuBar()->addMenu(QString());
    m_ouvrir = action(m_menu_fichier, [this] { openfile_dossier_prefere(); });
    m_ouvrir->setShortcut(QKeySequence::Open);
    m_menu_fichier->addSeparator();
    m_ouvrir_projet = action(m_menu_fichier, [this] { confirme_si_mesures({Apres::Type::OuvrirProjet}); });
    m_recuperer = action(m_menu_fichier, [this] { confirme_si_mesures({Apres::Type::OuvrirFichier, prefs::fichier_secours()}); });
    m_enregistrer = action(m_menu_fichier, [this] { enregistre_ui(); });
    m_enregistrer->setShortcut(QKeySequence::Save);
    m_menu_fichier->addSeparator();
    m_copier = action(m_menu_fichier, [this] { copie_python(); });
    m_copier->setShortcut(QKeySequence::Copy);
    m_csv = action(m_menu_fichier, [this] { export_csv(); });
    m_menu_fichier->addSeparator();
    m_quitter = action(m_menu_fichier, [this] { close(); });
    m_quitter->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    m_quitter->setMenuRole(QAction::QuitRole);

    m_menu_edition = menuBar()->addMenu(QString());
    m_defaire = action(m_menu_edition, [this] { efface_point_precedent(); });
    m_defaire->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    m_refaire = action(m_menu_edition, [this] { refait_point_suivant(); });
    m_refaire->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    m_menu_edition->addSeparator();
    m_recommencer = action(m_menu_edition, [this] { confirme_si_mesures({Apres::Type::ToutRecommencer}); });

    m_menu_mode = menuBar()->addMenu(QString());
    auto *groupe_mode = new QActionGroup(this);
    m_mode_simplifie = action(m_menu_mode, [this] { change_mode(Mode::Simplifie); });
    m_mode_expert = action(m_menu_mode, [this] { change_mode(Mode::Expert); });
    for (QAction *a : {m_mode_simplifie, m_mode_expert}) {
        a->setCheckable(true);
        groupe_mode->addAction(a);
    }

    // l'aide, en dernier : la notice (à la page de ce que l'on fait), « À propos »
    m_menu_aide = menuBar()->addMenu(QString());
    m_notice = action(m_menu_aide, [this] { aide(); });
    m_notice->setShortcut(QKeySequence(Qt::Key_F1));
    m_a_propos = action(m_menu_aide, [this] { propos(); });
    m_a_propos->setMenuRole(QAction::AboutRole);
    m_a_propos_qt = action(m_menu_aide, [this] { QMessageBox::aboutQt(this); });
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
    m_info_image->setVisible(video != nullptr && mode.has_value());
    m_info_mode->setVisible(mode.has_value());
    if (video) {
        // le nom de la vidéo (raccourci au milieu s'il est long) et ses caractéristiques
        const QSize d = dimensions();
        const QString complet = u"%1 · %2 × %3 px · %4"_s.arg(QFileInfo(filename).fileName())
                                    .arg(d.width())
                                    .arg(d.height())
                                    .arg(trad("{} im/s", nombre_local(fps, 4)));
        m_info_video->setText(m_info_video->fontMetrics().elidedText(complet, Qt::ElideMiddle, std::max(width() / 3, 120)));
        m_info_video->setToolTip(natif(filename));
        m_info_image->setText(trad("Image {} sur {}", index, image_max));
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
    const auto plus = [this] { change_agrandissement(std::round(m_agrandissement * 10.0 + 1.0) / 10.0); };
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
    if (etat == Etat::C)
        annule_echelle();
    else if (refait)
        annule_refait();
    else if (choix_origine && mode == Mode::Expert)
        choix_origine = false;
}

void App::touche_image(int pas)
{
    const bool image_visible = simplifie() || onglet == Onglet::Pointage;
    if (mode && image_visible && controle_image_actif())
        change_image(index + pas);
}

void App::maj_menus()
{
    const bool disponible = video && (etat == Etat::A || etat == Etat::D);
    const bool a_des_donnees = pointage.a_des_pointages();
    m_menu_fichier->setTitle(trad("Fichier"));
    m_ouvrir->setText(trad("Ouvrir une vidéo…"));
    m_ouvrir_projet->setText(trad("Ouvrir un projet…"));
    m_recuperer->setText(trad("Récupérer le travail précédent…"));
    m_recuperer->setToolTip(trad("Rouvre la sauvegarde automatique du travail en cours, faite toutes les deux secondes : "
                                 "après un plantage ou une fermeture malencontreuse"));
    m_recuperer->setEnabled(QFileInfo(prefs::fichier_secours()).isFile());
    m_enregistrer->setText(trad("Enregistrer le projet…"));
    m_enregistrer->setToolTip(trad("Enregistre le chemin de la vidéo, le repère, l'échelle et les mesures, pour continuer plus tard"));
    m_enregistrer->setEnabled(disponible);
    m_copier->setText(trad("Copier pour Python"));
    m_copier->setToolTip(trad("Copie lst_t, lst_x, lst_y (tableaux numpy), à coller dans un programme Python"));
    m_copier->setEnabled(disponible && a_des_donnees);
    m_csv->setText(trad("Enregistrer en CSV…"));
    m_csv->setEnabled(disponible && a_des_donnees);
    m_quitter->setText(trad("Quitter"));

    m_menu_edition->setTitle(trad("Édition"));
    m_defaire->setText(trad("Annuler le dernier point"));
    m_defaire->setEnabled(peut_defaire());
    m_refaire->setText(trad("Rétablir le point"));
    m_refaire->setEnabled(peut_refaire());
    m_recommencer->setText(trad("Tout recommencer"));
    m_recommencer->setToolTip(trad("Efface les mesures, l'échelle et le repère"));
    m_recommencer->setEnabled(disponible);

    m_menu_mode->setTitle(trad("Mode"));
    m_mode_simplifie->setText(trad("Mode simplifié (pas à pas)"));
    m_mode_expert->setText(trad("Mode expert (toutes les fonctions)"));
    m_mode_simplifie->setChecked(mode == Mode::Simplifie);
    m_mode_expert->setChecked(mode == Mode::Expert);
    if (!mode) {
        // aucun mode choisi : aucune case cochée
        m_mode_simplifie->actionGroup()->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
        m_mode_simplifie->setChecked(false);
        m_mode_expert->setChecked(false);
    } else {
        m_mode_simplifie->actionGroup()->setExclusionPolicy(QActionGroup::ExclusionPolicy::Exclusive);
    }

    m_menu_aide->setTitle(trad("Aide"));
    m_notice->setText(trad("Notice d'utilisation…"));
    m_a_propos->setText(trad("À propos de MeVCine…"));
    m_a_propos_qt->setText(trad("À propos de Qt…"));

    m_ouvrir->setToolTip(trad("Ouvrir une vidéo (Ctrl+O)"));
    m_mode_simplifie->setToolTip(trad("Mode simplifié : pas à pas, un seul objet. Les mesures de l'objet 1 sont gardées."));
    m_mode_expert->setToolTip(trad("Mode expert : toutes les fonctions. Les mesures sont gardées."));
    // À droite de la barre des menus : la notice, puis le soleil ou la lune et le drapeau, qui montrent
    // le thème et la langue affichés
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
        choix_origine = guide.etape == Etape::Origine && (etat == Etat::A || etat == Etat::D);
    if (mode == Mode::Expert && !onglets_actifs())
        onglet = Onglet::Pointage;
    if (video)
        image_courante();
    // le titre : MeVCine — la vidéo ouverte, ou le sous-titre
    const QString titre =
        u"%1 — %2"_s.arg(a_propos::NOM, video ? QFileInfo(filename).fileName() : trad("Étude cinématique de vidéos"));
    if (windowTitle() != titre)
        setWindowTitle(titre);
    maj_menus();

    if (!m_secours.isActive() && sauvegarde_de_secours())
        m_secours.start();

    if (!mode) {
        m_pages->setCurrentWidget(m_accueil);
        m_accueil->maj();
    } else if (!video) {
        m_pages->setCurrentWidget(m_sans_video);
        m_sans_video->maj();
    } else if (simplifie()) {
        m_pages->setCurrentWidget(m_guide);
        m_guide->maj();
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
        case Onglet::Pointage: m_onglet_pointage->maj(); break;
        case Onglet::Trajectoires: m_onglet_trajectoire->maj(); break;
        case Onglet::Coordonnees: m_onglet_coord->maj(); break;
        case Onglet::Grapheur: m_onglet_graph->maj(); break;
        }
    }
    // le plein écran ne sert qu'au pointage : il se ferme quand celui-ci n'est plus possible
    if (plein_ecran()) {
        if (plein_ecran_permis())
            m_plein_ecran->maj();
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
        auto *boite = new DialogueEchelle(d.saisie, d.unite, this);
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
            boite->setDefaultButton(ok);
            boite->setEscapeButton(ok);
        } else {
            // pas de validation par Entrée : on perdrait des mesures d'une touche enfoncée par mégarde
            boite->setIcon(QMessageBox::Warning);
            QPushButton *annuler = boite->addButton(trad("Annuler"), QMessageBox::RejectRole);
            QPushButton *confirmer = boite->addButton(d.bouton, QMessageBox::DestructiveRole);
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
        // à la fermeture, on retient le dossier des vidéos et la taille de la fenêtre
        ecrit_dimensions_conf();
        if (m_taille_normale.isValid())
            config.set(u"taille_fenetre"_s, u"(%1,%2)"_s.arg(m_taille_normale.width()).arg(m_taille_normale.height()));
        config.set(u"fenetre_maximisee"_s, isMaximized() ? u"True"_s : u"False"_s);
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
    if (fichier.isEmpty() || etat == Etat::B || etat == Etat::AB || etat == Etat::C || !dialogues.empty())
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
