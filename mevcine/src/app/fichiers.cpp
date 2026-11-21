// Les commandes du menu Fichier réunissent le choix de l'élève et le travail sur disque.
// La conversion des mesures en texte se trouve dans seance.cpp, l'écriture des secours dans secours.cpp.
#include "app.h"

#include "chemins.h"
#include "debogage.h"
#include "ecriture_image.h"
#include "export.h"
#include "format.h"
#include "i18n.h"

#include <QClipboard>
#include <QDataStream>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

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

} // namespace

// ------------------------------------------------------------------------------------------------
// fichiers
// ------------------------------------------------------------------------------------------------

void App::choisit_video(const QString &dossier)
{
    const QString filtre = trad("Vidéos (mp4, mov, avi, mkv, webm...)") + u" ("_s + chemins::filtre_videos().join(u' ')
        + u");;"_s + trad("Tous les fichiers") + u" (*)"_s;
    const QString chemin = QFileDialog::getOpenFileName(this, trad("Ouvrir une vidéo"), dossier, filtre);
    if (!chemin.isEmpty())
        ouvre_video(chemin, 0);
}

QString App::dossier_prefere() const
{
    const QString d = QDir::fromNativeSeparators(config.valeur_texte(u"videodir"_s).value_or(QString()));
    return !d.isEmpty() && QFileInfo(d).isDir() ? d : chemins::dossier_videos_utilisateur();
}

void App::choisit_video_dossier_prefere()
{
    confirme_si_mesures({Apres::Type::OuvrirVideo, dossier_prefere()});
}

QByteArray App::empreinte_mesures() const
{
    QByteArray donnees;
    QDataStream flux(&donnees, QIODevice::WriteOnly);
    const Echelle &e = pointage.echelle;
    const int debut = pointage.image_initiale().value_or(1) - 1, fin = pointage.image_finale().value_or(0);
    flux << fichier_video << (video ? video->rotation() : 0) << pointage.nombre_objets() << pointage.delta_t
         << pointage.origine.x << pointage.origine.y << pointage.sens_x << pointage.sens_y
         << (guide.origine_choisie || fin > 0) << (guide.axes_choisis || fin > 0) << guide.visee_assistee << e.p1.x << e.p1.y << e.p2.x << e.p2.y
         << e.longueur_reelle_etalon << static_cast<int>(unite_etalon);
    flux << debut << fin << (pointage_arrete && fin > 0);
    const auto clic = clic_en_cours();
    flux << (clic ? clic->image : 0) << (clic ? clic->objet : 0);
    flux << pointage.nb_images_pointees();
    for (const auto &[index_image, positions] : pointage.positions_par_image()) {
        flux << index_image;
        for (const Position &p : positions) {
            flux << p.has_value();
            if (p)
                flux << p->x << p->y;
        }
    }
    for (const auto &[cellule, saisie] : pointage.provenances())
        flux << cellule.first << cellule.second << static_cast<int>(saisie);
    return donnees;
}

bool App::travail_en_securite() const
{
    return !video || empreinte_mesures() == (empreinte_sauvee.isEmpty() ? empreinte_initiale : empreinte_sauvee);
}

bool App::projet_a_jour() const
{
    return !empreinte_sauvee.isEmpty() && travail_en_securite();
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
        trad("Pour reprendre ce travail, annulez puis enregistrez le projet MeVCine.");
    Dialogue d{Dialogue::Type::Confirmation};
    d.apres = apres;
    switch (apres.type) {
    case Apres::Type::Quitter:
        d.titre = trad("Quitter MeVCine ?");
        d.texte = trad("Le projet contient des réglages ou des mesures non enregistrés.") + u"\n\n"_s + garder;
        d.bouton = trad("Quitter sans enregistrer");
        break;
    case Apres::Type::ToutRecommencer:
        d.titre = trad("Tout recommencer ?");
        d.texte = trad("Les mesures, l'échelle et le repère seront effacés.") + u"\n\n"_s + garder;
        d.bouton = trad("Tout effacer");
        break;
    default:
        d.titre = trad("Abandonner les mesures ?");
        d.texte = trad("Ouvrir un autre fichier remplacera le projet en cours et ses modifications.")
            + u"\n\n"_s + garder;
        d.bouton = trad("Continuer sans enregistrer");
    }
    dialogues.push_back(d);
    QTimer::singleShot(0, this, &App::affiche_dialogues);
}

void App::execute(const Apres &apres)
{
    switch (apres.type) {
    case Apres::Type::ToutRecommencer: tout_recommencer(); break;
    case Apres::Type::OuvrirVideo: choisit_video(apres.chemin); break;
    case Apres::Type::OuvrirProjet: charge_projet_ui(); break;
    case Apres::Type::OuvrirFichier: ouvre_argument(apres.chemin); break;
    case Apres::Type::Quitter:
        fermeture_confirmee = true;
        QTimer::singleShot(0, this, &QWidget::close);
        break;
    }
    rafraichit();
}

bool App::sauvegarde_de_secours()
{
    if (!video || m_secours_seance.chemin().isEmpty())
        return false;
    if (!m_secours_seance.deja_ecrit() && !pointage.image_initiale() && !pointage.derniere_suppression()
        && empreinte_mesures() == empreinte_initiale)
        return false;
    if (const QString e = m_secours_seance.ecrit(texte_projet()); e.isEmpty()) {
        secours_en_echec = false;
        m_recuperer->setEnabled(true);
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

void App::termine_seance()
{
    m_secours.stop();
    if (!video)
        return;
    // Le projet peut avoir été remplacé par une autre fenêtre depuis Ctrl+S. Vérifier
    // son contenu avant de retirer l'unique copie du travail de cette séance.
    if (projet_a_jour() && prefs::projet_identique(m_fichier_projet, m_empreinte_fichier_projet))
        m_secours_seance.retire_copie_enregistree();
    else
        sauvegarde_de_secours();
    m_secours_seance = {}; // libère immédiatement le verrou, même si la fenêtre existe encore
}

void App::choisit_secours()
{
    const QStringList fichiers = SecoursSeance::disponibles();
    if (fichiers.isEmpty())
        return;
    QString choix = fichiers.first();
    if (fichiers.size() > 1) {
        const QString filtre = trad("Sauvegardes de secours") + u" ("_s + SecoursSeance::filtres().join(u' ') + u')';
        choix = QFileDialog::getOpenFileName(this, trad("Choisir la séance à récupérer"), choix, filtre);
    }
    if (fichiers.contains(choix))
        confirme_si_mesures({Apres::Type::OuvrirFichier, choix});
}

std::optional<Pointage::ProchainClic> App::clic_en_cours() const
{
    if (etat == Etat::SerieEnCours && !refait && objet_courant > 1)
        return Pointage::ProchainClic{index, objet_courant};
    return {};
}

bool App::enregistrement_permis() const
{
    return video && (etat == Etat::Preparation || etat == Etat::Pointage || clic_en_cours().has_value());
}

void App::charge_projet_ui()
{
    const QString chemin = QFileDialog::getOpenFileName(
        this, trad("Ouvrir un projet MeVCine"), chemins::dossier_documents(),
        trad("Projet MeVCine") + u" (*.mevcine *.MEVCINE)"_s);
    if (!chemin.isEmpty())
        charge_projet(chemin);
}

QString App::nom_de_base() const
{
    return fichier_video.isEmpty() ? trad("mesures") : QFileInfo(fichier_video).completeBaseName();
}

std::optional<QString> App::nom_video() const
{
    if (fichier_video.isEmpty())
        return std::nullopt;
    return QFileInfo(fichier_video).fileName();
}

void App::ecrit_projet_ui()
{
    if (!enregistrement_permis())
        return;
    const QString propose = QDir(chemins::dossier_documents()).filePath(nom_de_base() + u'.' + prefs::EXTENSION_PROJET);
    const QString chemin = QFileDialog::getSaveFileName(this, trad("Enregistrer le projet MeVCine"), propose,
                                                        trad("Projet MeVCine") + u" (*.mevcine)"_s);
    if (!chemin.isEmpty())
        ecrit_projet(avec_extension(chemin, {prefs::EXTENSION_PROJET}));
}

prefs::Config App::config_projet() const
{
    const Pointage &p = pointage;
    const Echelle &e = p.echelle;
    const auto point = [](Vecteur v) { return u"(%1, %2)"_s.arg(texte_reel(v.x), texte_reel(v.y)); };
    prefs::Config c = config;
    c.definit(u"version"_s, u"mevcine "_s + prefs::VERSION);
    c.definit(u"proximite"_s, traj.pres_souris ? u"True"_s : u"False"_s);
    if (!fichier_video.isEmpty()) {
        c.definit(u"signature_video"_s, signature_source);
        c.definit(u"lastvideo"_s, QDir::toNativeSeparators(fichier_video));
        c.definit(u"videodir"_s, QDir::toNativeSeparators(QFileInfo(fichier_video).absolutePath()));
    }
    c.definit(u"niveaudbg"_s, QString::number(NIVEAU_DBG.load()));
    const bool avec_mesures = p.image_initiale().has_value();
    c.definit(u"origine_choisie"_s, (guide.origine_choisie || avec_mesures) ? u"1"_s : u"0"_s);
    c.definit(u"visee_assistee"_s, guide.visee_assistee ? u"1"_s : u"0"_s);
    c.definit(u"axes_choisis"_s, (guide.axes_choisis || avec_mesures) ? u"1"_s : u"0"_s);
    c.definit(u"sens_x"_s, QString::number(p.sens_x));
    c.definit(u"sens_y"_s, QString::number(p.sens_y));
    c.definit(u"origine"_s, point(p.origine));
    c.definit(u"pointage_arrete"_s, pointage_arrete && avec_mesures ? u"1"_s : u"0"_s);
    c.definit(u"index_depart"_s, QString::number(p.image_initiale().value_or(1)));
    c.definit(u"etalon_m"_s, texte_reel(e.longueur_reelle_etalon));
    c.definit(u"unite_etalon"_s, symbole(unite_etalon));
    c.definit(u"etalon_px"_s, texte_reel(e.etalon_pixels()));
    c.definit(u"etalon_org"_s, e.est_definie() ? point(e.p1) : u"None"_s);
    c.definit(u"etalon_ext"_s, e.est_definie() ? point(e.p2) : u"None"_s);
    c.definit(u"deltat"_s, texte_reel(p.delta_t));
    c.definit(u"nb_obj"_s, QString::number(p.nombre_objets()));
    const QSize d = dimensions();
    c.definit(u"taille_image"_s, u"(%1,%2)"_s.arg(d.width()).arg(d.height()));
    c.definit(u"rotation"_s, QString::number(video ? video->rotation() : 0));
    return c;
}

void App::sauve_prefs()
{
    config = config_projet();
    prefs::sauve(config);
}

QString App::texte_projet() const
{
    return seance::texte(config_projet(), pointage, clic_en_cours());
}

void App::ecrit_projet(const QString &chemin)
{
    if (SecoursSeance::est_secours(chemin)) {
        message(trad("Erreur lors de l'enregistrement"), trad("Choisissez un nom différent de celui d'une sauvegarde automatique."));
        return;
    }
    sauve_prefs();
    const QString texte = texte_projet();
    const QString erreur = prefs::ecrit_projet(chemin, texte);
    if (erreur.isEmpty()) {
        m_fichier_projet = chemin;
        m_empreinte_fichier_projet = prefs::empreinte_projet(texte.toUtf8());
        note_travail_sauve();
        affiche_statut(trad("Projet enregistré : {}", QDir::toNativeSeparators(chemin)), icones::Statut::Reussite);
    } else {
        message(trad("Erreur lors de l'enregistrement"),
                trad("Le projet n'a pas pu être enregistré :\n{}\n\n{}", QDir::toNativeSeparators(chemin), erreur));
    }
}

void App::charge_projet(const QString &fichier)
{
    dbg_p(2, u"ouverture du projet %1"_s.arg(fichier));
    const bool recuperation = SecoursSeance::est_secours(fichier);
    if (recuperation && QFileInfo(fichier) == QFileInfo(fichier_secours()))
        sauvegarde_de_secours(); // le dernier clic peut encore attendre la minuterie
    // Garder le verrou pendant la lecture de la vidéo : une autre fenêtre peut ouvrir ce
    // secours ou le mettre à jour pendant le décodage de la première image.
    auto secours_reserve = SecoursSeance::reserve_pour_reprise(fichier);
    const auto projet = prefs::lit_projet(fichier);
    if (!projet) {
        message(trad("Projet illisible"), QDir::toNativeSeparators(fichier) + u'\n' + projet.error());
        return;
    }
    const prefs::Config &c = projet->config;
    if (!version_conf_ok(c))
        return;
    const auto longueur = c.valeur_reelle(u"etalon_m"_s);
    if (!longueur || !std::isfinite(*longueur) || *longueur <= 0.0) {
        message(trad("Projet illisible"), QDir::toNativeSeparators(fichier) + u'\n' +
                trad("La longueur de l'étalon ({} m) n'est pas valide.", c.valeur_texte(u"etalon_m"_s).value_or(QString())));
        return;
    }
    const auto debut = c.valeur_entiere(u"index_depart"_s);
    if (!debut || *debut < 1 || *debut > std::numeric_limits<int>::max() ||
        projet->donnees.size() > static_cast<size_t>(std::numeric_limits<int>::max() - *debut + 1)) {
        message(trad("Projet illisible"), trad("La numérotation des images du projet est invalide."));
        return;
    }
    const int derniere_mesure = projet->donnees.empty() ? 0 : static_cast<int>(*debut - 1 + projet->donnees.size());
    const auto rotation = c.valeur_entiere(u"rotation"_s).and_then(rotation_video);
    if (!rotation) {
        message(trad("Projet illisible"), trad("La rotation de la vidéo doit être un multiple de 90 degrés."));
        return;
    }
    const QString signature = c.valeur_texte(u"signature_video"_s).value_or(QString());
    const QDir dossier_projet = QFileInfo(fichier).absoluteDir();
    const QString chemin_inscrit = QDir::fromNativeSeparators(c.valeur_texte(u"lastvideo"_s).value_or(QString()));
    const QString nom = chemin_inscrit.section(u'\\', -1).section(u'/', -1);
    QString chemin_video;
    QStringList candidats{dossier_projet.absoluteFilePath(chemin_inscrit), dossier_projet.filePath(nom)};
    candidats.removeDuplicates();
    for (const QString &candidat : candidats) {
        if (QFileInfo(candidat).isFile() && (signature.isEmpty() || seance::signature_video(candidat) == signature)) {
            chemin_video = candidat;
            break;
        }
    }
    if (chemin_video.isEmpty()) {
        chemin_video = QFileDialog::getOpenFileName(this, trad("Retrouver la vidéo du projet…"), QFileInfo(fichier).absolutePath());
        if (chemin_video.isEmpty())
            return;
        if (!signature.isEmpty() && seance::signature_video(chemin_video) != signature) {
            message(trad("Vidéo introuvable"), trad("La vidéo choisie ne correspond pas à celle du projet."));
            return;
        }
        if (signature.isEmpty() && QMessageBox::question(this, trad("Vérifier la vidéo"),
                trad("Ce projet ancien ne contient pas de signature. Utiliser la vidéo {} ?", QDir::toNativeSeparators(chemin_video)),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
    }
    if (!ouvre_video(chemin_video, *rotation, derniere_mesure))
        return;
    // les positions sont enregistrées pour une image de dimension taille_image : on les ramène à
    // celle de la vidéo
    const QSize d = dimensions();
    double fx = 1.0, fy = 1.0;
    if (const auto t = c.valeur_vecteur(u"taille_image"_s); t && t->x > 0 && t->y > 0) {
        fx = d.width() / t->x;
        fy = d.height() / t->y;
    }
    const auto ecran_vers_image = [&](Vecteur v) { return Vecteur(v.x * fx, v.y * fy); };
    pointage.sens_x = c.valeur_entiere(u"sens_x"_s) == -1 ? -1 : 1;
    pointage.sens_y = c.valeur_entiere(u"sens_y"_s) == -1 ? -1 : 1;
    if (const auto o = c.valeur_vecteur(u"origine"_s))
        pointage.origine = ecran_vers_image(*o);
    if (const auto dt = c.valeur_reelle(u"deltat"_s); dt && std::isfinite(*dt) && *dt > 0.0 && std::isfinite(1.0 / *dt)) {
        cadence_prise_de_vue = 1.0 / *dt;
        saisie_cadence = nombre_local(cadence_prise_de_vue, 6);
    }
    nombre_objets_demandes = static_cast<int>(std::clamp<qint64>(c.valeur_entiere(u"nb_obj"_s).value_or(1), 1, 20));
    Echelle echelle;
    echelle.longueur_reelle_etalon = *longueur;
    const auto p1 = c.valeur_vecteur(u"etalon_org"_s), p2 = c.valeur_vecteur(u"etalon_ext"_s);
    if (p1 && p2) {
        echelle.p1 = ecran_vers_image(*p1);
        echelle.p2 = ecran_vers_image(*p2);
    }
    if (const auto u = unite_depuis_symbole(c.valeur_texte(u"unite_etalon"_s).value_or(QString())))
        change_unite_etalon(*u);
    const Vecteur origine = pointage.origine;
    ajuste_nombre_objets();
    pointage.origine = origine;
    pointage.echelle = echelle;
    const auto reprise = seance::restaure_mesures(*projet, pointage);
    config.definit(u"lastvideo"_s, QDir::toNativeSeparators(chemin_video));
    guide.reprend(pointage.echelle.est_definie());
    // Les anciens projets ne distinguaient pas le repère initial d'un choix explicite.
    guide.origine_choisie = c.valeur_entiere(u"origine_choisie"_s).value_or(1);
    guide.visee_assistee = c.valeur_entiere(u"visee_assistee"_s).value_or(0);
    guide.axes_choisis = c.valeur_entiere(u"axes_choisis"_s).value_or(1);
    if (!guide.origine_choisie)
        guide.etape = Etape::Origine;
    else if (!guide.axes_choisis)
        guide.etape = Etape::Axes;
    guide.deja_utilise = true;
    const bool pret_a_pointer = simplifie() && guide.etape == Etape::Pointage;
    passe_a_etat(pointage.image_initiale() || pret_a_pointer ? Etat::Pointage : Etat::Preparation);
    const auto der = pointage.image_finale();
    index = der ? std::min(*der, nombre_images_video - 1) + 1 : 1;
    if (reprise) {
        index = reprise->image;
        objet_courant = reprise->objet;
        guide.etape = Etape::Pointage;
        choix_origine = false;
        passe_a_etat(Etat::SerieEnCours);
    } else if (der && c.valeur_entiere(u"pointage_arrete"_s) == 1) {
        arrete_pointage();
    }
    prepare_clic_suivant();
    if (recuperation) {
        if (secours_reserve)
            m_secours_seance = std::move(*secours_reserve);
        affiche_statut(trad("Travail récupéré. Enregistrez le projet pour le conserver."));
    } else {
        m_fichier_projet = fichier;
        m_empreinte_fichier_projet = projet->empreinte_lue;
        note_travail_sauve();
    }
}

bool App::version_conf_ok(const prefs::Config &c)
{
    if (c.version_ok())
        return true;
    message(trad("Fichier non reconnu"),
            trad("Ce fichier n'a pas été écrit par MeVCine : il ne peut pas être pris en compte."));
    return false;
}

bool App::ouvre_argument(const QString &fichier)
{
    if (fichier.isEmpty())
        return false;
    if (prefs::est_projet(fichier)) {
        charge_projet(fichier);
        return true;
    }
    if (QFileInfo(fichier).isFile())
        return ouvre_video(fichier, 0);
    message(trad("Fichier introuvable"), trad("Le fichier {} n'existe pas.", QDir::toNativeSeparators(fichier)));
    return false;
}

void App::applique_preferences()
{
    if (!config.version_ok()) {
        config = prefs::Config();
        return;
    }
    traj.pres_souris = config.valeur_texte(u"proximite"_s) == u"True"_s;
}

void App::exporte_csv()
{
    if (!pointage.a_des_pointages()) {
        message(trad("Aucune mesure"), trad("Pointez d'abord l'objet sur quelques images."));
        return;
    }
    const QString propose = QDir(chemins::dossier_documents()).filePath(nom_de_base() + u".csv"_s);
    const QString chemin = QFileDialog::getSaveFileName(this, trad("Enregistrer les mesures en CSV"), propose,
                                                        trad("Fichier CSV") + u" (*.csv)"_s);
    if (!chemin.isEmpty())
        ecrit_csv(avec_extension(chemin, {u"csv"_s}));
}

void App::ecrit_csv(const QString &chemin)
{
    if (const QString e = ::ecrit_csv(chemin, pointage); e.isEmpty()) {
        dernier_csv = texte_csv(pointage);
        affiche_statut(trad("Mesures enregistrées : {}", QDir::toNativeSeparators(chemin)), icones::Statut::Reussite);
    } else {
        message(trad("Erreur lors de l'enregistrement"),
                trad("Le fichier CSV n'a pas pu être enregistré :\n{}\n\n{}", QDir::toNativeSeparators(chemin), e));
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
    affiche_statut(trad("Mesures copiées dans le presse-papiers : collez-les (Ctrl+V) dans votre programme Python"),
                   icones::Statut::Reussite);
}

bool App::copie_a_jour() const
{
    return !presse_papier.isEmpty() && presse_papier == QGuiApplication::clipboard()->text() && presse_papier == texte_python();
}

bool App::csv_a_jour() const
{
    return !dernier_csv.isEmpty() && dernier_csv == texte_csv(pointage);
}

QString App::etat_des_sorties() const
{
    QStringList etats;
    if (copie_a_jour())
        etats << trad("Mesures dans le presse-papiers");
    if (csv_a_jour())
        etats << trad("CSV exporté");
    if (projet_a_jour())
        etats << trad("Projet enregistré");
    return etats.join(u" · "_s);
}

void App::tout_recommencer()
{
    if (!video || !(etat == Etat::Preparation || etat == Etat::Pointage))
        return;
    termine_seance();
    reinitialise_mesures();
}

void App::enregistre_image(const QPixmap &capture, const QString &titre_dialogue, const QString &filtre, const QString &extension)
{
    const QString propose = QDir(chemins::dossier_documents()).filePath(nom_de_base() + u'.' + extension);
    QString chemin = QFileDialog::getSaveFileName(this, titre_dialogue, propose, filtre);
    if (chemin.isEmpty())
        return;
    const QStringList permises = extension == u"png" ? QStringList{u"png"_s} : QStringList{u"jpg"_s, u"png"_s, u"jpeg"_s};
    chemin = avec_extension(chemin, permises);
    QString erreur;
    if (capture.isNull())
        erreur = trad("image vide");
    else if (ecriture_image::enregistre(chemin, capture.toImage(), QFileInfo(chemin).suffix().toLower().toLatin1(), erreur))
        return;
    message(trad("Erreur lors de l'enregistrement"),
            trad("L'image n'a pas pu être enregistrée :\n{}\n\n{}", QDir::toNativeSeparators(chemin), erreur));
}
