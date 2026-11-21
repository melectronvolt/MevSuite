// Commandes du menu Fichier, du premier choix de photo à la fermeture de la séance.
#include "app.h"

#include "about.h"
#include "annotation.h"
#include "canvas.h"
#include "ecriture_image.h"
#include "fichier_seance.h"
#include "plot.h"
#include "projet.h"
#include "savefile.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QString choisit_fichier_sortie(QWidget *parent, const QString &titre, const QString &suggestion,
                              const QString &filtre, const QString &suffixe,
                              QFileDialog::Options options = {})
{
    QFileDialog dialogue(parent, titre, QFileInfo(suggestion).absolutePath(), filtre);
    dialogue.setOptions(options);
    dialogue.setAcceptMode(QFileDialog::AcceptSave);
    dialogue.setDefaultSuffix(suffixe);
    dialogue.selectFile(QFileInfo(suggestion).fileName());
    return dialogue.exec() == QDialog::Accepted ? dialogue.selectedFiles().value(0) : QString();
}

bool confirme_remplacement(QWidget *parent, const Textes &t, const QString &chemin)
{
    QMessageBox boite(QMessageBox::Question, about::NAME, t.replaceFile(QFileInfo(chemin).fileName()),
                      QMessageBox::NoButton, parent);
    boite.setObjectName(u"confirmeRemplacementFichier"_s);
    boite.setTextFormat(Qt::PlainText);
    auto *remplacer = boite.addButton(t.replaceButton, QMessageBox::AcceptRole);
    auto *annuler = boite.addButton(t.cancel, QMessageBox::RejectRole);
    boite.setDefaultButton(annuler);
    boite.setEscapeButton(annuler);
    boite.exec();
    return boite.clickedButton() == remplacer;
}

bool propose_copie(QWidget *parent, const Textes &t, const QString &message)
{
    QMessageBox boite(QMessageBox::Warning, about::NAME, message, QMessageBox::NoButton, parent);
    boite.setTextFormat(Qt::PlainText);
    boite.setObjectName(u"conflitSeance"_s);
    auto *copier = boite.addButton(t.saveCopy, QMessageBox::AcceptRole);
    auto *annuler = boite.addButton(t.cancel, QMessageBox::RejectRole);
    boite.setDefaultButton(annuler);
    boite.setEscapeButton(annuler);
    boite.exec();
    return boite.clickedButton() == copier;
}

} // namespace

// ---------------------------------------------------------------- chargement

bool FenetrePrincipale::occupe() const
{
    return !m_cheminChargement.isEmpty() || m_travailAEcrire.has_value();
}

void FenetrePrincipale::ouvre_dialogue()
{
    if (m_travailAEcrire) {
        m_apresSauvegarde = [this] { ouvre_dialogue(); };
        return;
    }
    if (!m_cheminChargement.isEmpty())
        return;
    const Textes &t = tr();
    QStringList patterns;
    for (const QString &ext : formats_image() + QStringList{u"mevpix"_s})
        patterns << u"*."_s + ext << u"*."_s + ext.toUpper();
    const QString filters = u"%1 (%2);;%3 (*)"_s.arg(t.images, patterns.join(u' '), t.allFiles);
    const StatusMessage avant = m_statut;
    affiche_statut(t.chooseImage);
    const QString path = QFileDialog::getOpenFileName(this, t.dialogOpen, m_dernierDossier, filters, nullptr, QFileDialog::HideNameFilterDetails);
    affiche_statut(avant);
    if (!path.isEmpty())
        charge_chemin(path);
}

void FenetrePrincipale::charge_chemin(const QString &path)
{
    if (m_travailAEcrire) {
        m_apresSauvegarde = [this, path] { charge_chemin(path); };
        return;
    }
    if (!m_cheminChargement.isEmpty())
        return;
    m_idleStatus = m_statut;
    m_cheminChargement = QFileInfo(path).absoluteFilePath();
    affiche_statut(tr().loading(QDir::toNativeSeparators(path)));
    // Décodage dans un fil de fond : la fenêtre reste réactive pendant le chargement d'une grande photo.
    m_lecture.setFuture(QtConcurrent::run(charge_image, m_cheminChargement));
    rafraichit();
}

void FenetrePrincipale::apres_chargement()
{
    const Textes &t = tr();
    if (m_fermetureEnCours || m_travailAEcrire || m_cheminChargement.isEmpty() || !m_lecture.isFinished())
        return;
    const QString chemin = std::exchange(m_cheminChargement, {});
    ResultatChargement result;
    try {
        // En cas d'annulation, la photo et ses réductions sont libérées à la sortie.
        result = m_lecture.future().takeResult();
    } catch (...) {
        affiche_statut(t.unexpectedError, icons::Status::Error);
        rafraichit();
        return;
    }
    if (!result.image) {
        QString reason = result.error;
        if (result.typeErreur == ResultatChargement::Erreur::FormatInconnu)
            reason = t.unsupportedFormat;
        else if (result.typeErreur == ResultatChargement::Erreur::JpegIncomplet)
            reason = t.incompleteJpeg;
        else if (result.typeErreur == ResultatChargement::Erreur::FichierSpecial)
            reason = t.regularFileRequired;
        affiche_statut(t.openFailed(QDir::toNativeSeparators(chemin), reason), icons::Status::Error);
        if (result.typeErreur == ResultatChargement::Erreur::FormatInconnu)
            m_messageEtat->setToolTip(u"<p>%1</p>"_s.arg(formats_image().join(u", "_s).toHtmlEscaped()));
        rafraichit();
        return;
    }
    propose_ouverture(chemin, result);
}

void FenetrePrincipale::propose_ouverture(const QString &chemin, const ResultatChargement &result)
{
    const Textes &t = tr();
    Seance seance;
    seance.image = result.image;
    const bool projet = !result.seance.isEmpty();
    if (projet && !restaure_seance(seance, result.seance)) {
        affiche_statut(t.openFailed(chemin, t.invalidSession), icons::Status::Error);
        rafraichit();
        return;
    }
    termine_saisie();
    m_graphique->termine_clic();
    m_graphique->annule_glisser();
    m_canevas->annule_glisser();
    affiche_statut(m_idleStatus);
    if (seance_modifiee()) {
        const auto choix = confirme_abandon(t.replaceImage(QFileInfo(chemin).fileName()), u"confirmReplace"_s);
        if (choix == ChoixAbandon::Annuler) {
            rafraichit();
            return;
        }
        if (choix == ChoixAbandon::Enregistrer) {
            m_apresSauvegarde = [this, chemin, result] {
                // Si l'on vient de sauver dans le fichier demandé, sa version en mémoire est périmée.
                if (est_seance_courante(chemin))
                    charge_chemin(chemin);
                else
                    propose_ouverture(chemin, result);
            };
            enregistre_projet();
            return;
        }
    }
    if (projet)
        m_seance = std::move(seance);
    else
        m_seance.definit_image(result.image);
    m_cheminSeance = projet ? chemin : QString();
    m_empreinteSeance = result.empreinteSeance;
    m_derniereSauvegarde = m_seance;
    m_dernierDossier = QFileInfo(chemin).absolutePath();
    reglages_modifies();
    m_canevas->demande_ajustement();
    m_graphique->reinitialise_vue();
    rafraichit_tableau_mesures();
    affiche_statut(t.imageLoaded(result.image->name, result.image->rgb.width(), result.image->rgb.height()));
    rafraichit();
}

void FenetrePrincipale::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        m_voileDepot->setGeometry(rect());
        m_voileDepot->raise();
        m_voileDepot->show();
    }
}

void FenetrePrincipale::dragLeaveEvent(QDragLeaveEvent *)
{
    m_voileDepot->hide();
}

void FenetrePrincipale::dropEvent(QDropEvent *event)
{
    m_voileDepot->hide();
    // une image se charge déjà : le dépôt est ignoré (le message à rétablir après le chargement
    // reste celui d'avant)
    if (!m_cheminChargement.isEmpty())
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            charge_chemin(url.toLocalFile());
            return;
        }
    }
}

void FenetrePrincipale::copie_mesures()
{
    if (!confirme_echelle_export())
        return;
    const Textes &t = tr();
    const auto &etalonnage = m_seance.etalonnage;
    const QString unite = etalonnage ? symbole_unite(etalonnage->unite) : u"px"_s;
    std::vector<double> longueurs;
    longueurs.reserve(m_seance.mesures.size());
    for (const Segment &mesure : m_seance.mesures)
        longueurs.push_back(mesure.longueur_px());
    QApplication::clipboard()->setText(tableau_mesures(longueurs, etalonnage, t.decimal, {t.colMeasure, t.colLength(unite)}));
    affiche_statut(t.measuresCopied, icons::Status::Success);
}

// ---------------------------------------------------------------- sauvegarde d'une séance

bool FenetrePrincipale::seance_modifiee() const
{
    if (!m_seance.image)
        return false;
    if (m_cheminSeance.isEmpty() && !m_seance.a_du_travail() && !m_seance.segmentEtalon && m_seance.saisieLongueur.isEmpty())
        return false;
    return m_derniereSauvegarde != static_cast<const Travail &>(m_seance);
}

bool FenetrePrincipale::est_seance_courante(const QString &chemin) const
{
    if (m_cheminSeance.isEmpty())
        return false;
    // Ctrl+S reprend exactement le chemin ouvert. Un autre nom peut désigner un lien vers lui.
    return chemin == m_cheminSeance ||
           QFileInfo(chemin_reel_seance(chemin)) == QFileInfo(chemin_reel_seance(m_cheminSeance));
}

void FenetrePrincipale::enregistre_projet(bool choisirNom)
{
    termine_saisie();
    m_graphique->termine_clic();
    m_canevas->annule_glisser();
    m_graphique->annule_glisser();

    // Les gestes suivants modifient la séance à l'écran. Le fil d'écriture garde les
    // mesures prises à cet instant et partage uniquement les pixels immuables de la photo.
    if (m_travailAEcrire) {
        m_prochaineEcriture = m_seance;
        return;
    }
    m_travailAEcrire = m_seance;
    QString chemin = m_cheminSeance;
    if (chemin.isEmpty()) {
        const QString nom = QFileInfo(m_seance.image->name).completeBaseName() + u".mevpix"_s;
        chemin = QDir(m_dernierDossier).filePath(nom);
        choisirNom = true;
    }

    ecrit_projet(chemin, choisirNom);
}

void FenetrePrincipale::ecrit_projet(QString chemin, bool choisirNom)
{
    if (choisirNom) {
        chemin = choisit_fichier_sortie(this, tr().saveSessionAs, chemin, u"MeVPix (*.mevpix)"_s,
                                        u"mevpix"_s, QFileDialog::DontConfirmOverwrite);
        if (chemin.isEmpty()) {
            termine_sauvegarde(false);
            return;
        }
    }
    m_destinationEcriture = chemin;
    if (est_seance_courante(chemin)) {
        lance_ecriture(m_empreinteSeance);
        return;
    }
    // Enregistrer sous peut viser une grosse séance. Lire sa version en arrière-plan,
    // avant la confirmation, permet aussi de repérer un changement pendant le dialogue.
    rafraichit();
    QtConcurrent::run(empreinte_sur_disque, chemin)
        .onFailed([] { return VersionFichier{}; })
        .then(this, [this, chemin](const VersionFichier &version) {
            if (!version.empreinte) {
                apres_sauvegarde({version.causeEchec, {}, version.detail});
                return;
            }
            if (version.empreinte->isEmpty() || confirme_remplacement(this, tr(), chemin)) {
                lance_ecriture(*version.empreinte);
                return;
            }
            termine_sauvegarde(false);
        });
}

void FenetrePrincipale::lance_ecriture(const QByteArray &empreinte)
{
    affiche_statut(tr().savingFile.arg(QDir::toNativeSeparators(m_destinationEcriture)));
    rafraichit();
    // Le contexte « this » ramène le résultat dans la fenêtre pour mettre à jour son titre.
    QtConcurrent::run(enregistre_seance, m_destinationEcriture, *m_travailAEcrire, empreinte)
        .onFailed([] { return ResultatSauvegarde{EtatSauvegarde::Erreur, {}, {}}; })
        .then(this, [this](const ResultatSauvegarde &resultat) { apres_sauvegarde(resultat); });
}

void FenetrePrincipale::apres_sauvegarde(const ResultatSauvegarde &resultat)
{
    const Textes &t = tr();
    const QFileInfo original(m_destinationEcriture);
    if (resultat.reussie()) {
        m_cheminSeance = original.absoluteFilePath();
        m_empreinteSeance = resultat.empreinte;
        m_derniereSauvegarde = *m_travailAEcrire;
        m_dernierDossier = original.absolutePath();
        reglages_modifies();
        affiche_statut(t.savedFile.arg(QDir::toNativeSeparators(m_cheminSeance)), icons::Status::Success);
        termine_sauvegarde(true);
        return;
    }
    if (resultat.etat == EtatSauvegarde::Conflit || resultat.etat == EtatSauvegarde::Occupe) {
        const QString message = resultat.etat == EtatSauvegarde::Conflit ? t.sessionChanged : t.sessionBusy;
        const QString detail = message.arg(original.fileName());
        affiche_statut(detail, icons::Status::Error);
        if (propose_copie(this, t, detail)) {
            ecrit_projet(original.dir().filePath(original.completeBaseName() + u"-2.mevpix"_s), true);
            return;
        }
    } else {
        QString erreur = resultat.erreur;
        if (resultat.etat == EtatSauvegarde::VerrouImpossible)
            erreur = t.lockFailed;
        else if (resultat.etat == EtatSauvegarde::EnteteTropGrande)
            erreur = t.sessionHeaderTooLarge;
        else if (resultat.etat == EtatSauvegarde::FichierSpecial)
            erreur = t.regularFileRequired;
        affiche_statut(t.saveFailed.arg(QDir::toNativeSeparators(m_destinationEcriture),
                                        erreur.isEmpty() ? t.unexpectedError : erreur), icons::Status::Error);
    }
    termine_sauvegarde(false);
}

void FenetrePrincipale::termine_sauvegarde(bool reussie)
{
    auto suivante = std::exchange(m_prochaineEcriture, std::nullopt);
    if (reussie && suivante && *suivante != *m_travailAEcrire) {
        m_travailAEcrire = std::move(suivante);
        ecrit_projet(m_cheminSeance, false);
        return;
    }
    m_travailAEcrire.reset();
    auto suite = std::exchange(m_apresSauvegarde, {});
    rafraichit();
    if (reussie && suite)
        suite();
    // La photo demandée a pu finir de se charger pendant la confirmation de fermeture.
    if (!m_cheminChargement.isEmpty() && m_lecture.isFinished())
        QTimer::singleShot(0, this, &FenetrePrincipale::apres_chargement);
}

FenetrePrincipale::ChoixAbandon FenetrePrincipale::confirme_abandon(const QString &message, const QString &nom)
{
    const Textes &t = tr();
    QMessageBox boite(QMessageBox::Question, about::NAME, message, QMessageBox::NoButton, this);
    boite.setTextFormat(Qt::PlainText);
    boite.setObjectName(nom);
    auto *sauver = boite.addButton(t.saveSession, QMessageBox::AcceptRole);
    auto *abandonner = boite.addButton(t.discard, QMessageBox::DestructiveRole);
    auto *annuler = boite.addButton(t.cancel, QMessageBox::RejectRole);
    boite.setDefaultButton(annuler);
    boite.setEscapeButton(annuler);
    boite.exec();
    if (boite.clickedButton() == sauver)
        return ChoixAbandon::Enregistrer;
    if (boite.clickedButton() == abandonner)
        return ChoixAbandon::Abandonner;
    return ChoixAbandon::Annuler;
}

// ---------------------------------------------------------------- documents à remettre aux élèves

bool FenetrePrincipale::confirme_echelle_export()
{
    termine_saisie();
    m_graphique->termine_clic();
    if (!m_seance.etalonnage_en_attente())
        return true;
    const Textes &t = tr();
    const auto echelle = [&](const Etalonnage &c) {
        return texte_nombre(c.unites_par_pixel(), 6, t.decimal) + u' ' + symbole_unite(c.unite);
    };
    const auto saisie = m_seance.etalonnage_saisi();
    QMessageBox boite(QMessageBox::Question, about::NAME, t.pendingScale.arg(echelle(*m_seance.etalonnage)),
                      QMessageBox::NoButton, this);
    boite.setObjectName(u"confirmScale"_s);
    boite.setInformativeText(saisie ? t.newScale.arg(echelle(*saisie)) : t.invalidLength);
    auto *valider = boite.addButton(t.useNewScale, QMessageBox::AcceptRole);
    valider->setEnabled(saisie.has_value());
    auto *garder = boite.addButton(t.useValidatedScale, QMessageBox::ActionRole);
    auto *annuler = boite.addButton(t.cancel, QMessageBox::RejectRole);
    boite.setDefaultButton(annuler);
    boite.setEscapeButton(annuler);
    boite.exec();
    if (boite.clickedButton() == valider && saisie) {
        m_seance.modifie([&] { m_seance.etalonnage = saisie; });
        rafraichit_tableau_mesures();
        rafraichit();
        return true;
    }
    return boite.clickedButton() == garder;
}

void FenetrePrincipale::exporte_annotation()
{
    if (!confirme_echelle_export())
        return;
    const Textes &t = tr();
    QDialog boite(this);
    boite.setWindowTitle(t.annotatedImage);
    QVBoxLayout layout(&boite);
    QLabel apercu;
    apercu.setAlignment(Qt::AlignCenter);
    layout.addWidget(&apercu);
    QCheckBox etalon(t.referenceSegment), coupe(t.cutAndCursors), graphique(t.includeGraph);
    const bool etalonValide = m_seance.segmentEtalon && m_seance.etalonnage &&
        qFuzzyCompare(m_seance.segmentEtalon->longueur_px(), m_seance.etalonnage->referencePx);
    etalon.setEnabled(etalonValide);
    etalon.setChecked(etalonValide);
    const bool coupeDisponible = m_seance.coupe_mesurable();
    coupe.setEnabled(coupeDisponible);
    coupe.setChecked(coupeDisponible);
    graphique.setEnabled(coupeDisponible);
    QComboBox choix;
    choix.addItem(t.allMeasurements, 0);
    for (int k = 0; k < m_seance.mesures.size(); ++k)
        choix.addItem(u"M%1"_s.arg(k + 1), k + 1);
    choix.addItem(t.noMeasure, -1);
    choix.setEnabled(!m_seance.mesures.isEmpty());
    layout.addWidget(&etalon);
    layout.addWidget(&choix);
    layout.addWidget(&coupe);
    layout.addWidget(&graphique);
    const auto rendu = [&](QSize limite = {}) {
        return image_annotee(m_seance, t, {etalon.isChecked(), choix.currentData().toInt(),
                                            coupe.isChecked(), graphique.isChecked()}, limite);
    };
    const auto affiche = [&] { apercu.setPixmap(QPixmap::fromImage(rendu({500, 320}))); };
    for (auto *caseACocher : {&etalon, &coupe, &graphique})
        connect(caseACocher, &QCheckBox::toggled, &boite, affiche);
    connect(&choix, &QComboBox::currentIndexChanged, &boite, affiche);
    affiche();
    QDialogButtonBox boutons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto *copier = boutons.addButton(t.copyAnnotation, QDialogButtonBox::ActionRole);
    connect(copier, &QPushButton::clicked, &boite, [&] {
        const QImage image = rendu();
        if (image.isNull()) {
            affiche_statut(t.unexpectedError, icons::Status::Error);
            return;
        }
        QApplication::clipboard()->setImage(image);
        affiche_statut(t.annotationCopied, icons::Status::Success);
        boite.reject();
    });
    connect(&boutons, &QDialogButtonBox::accepted, &boite, &QDialog::accept);
    connect(&boutons, &QDialogButtonBox::rejected, &boite, &QDialog::reject);
    layout.addWidget(&boutons);
    if (boite.exec() != QDialog::Accepted)
        return;
    const QString nom = QFileInfo(m_seance.image->name).completeBaseName() + u"-MeVPix.png"_s;
    const QString chemin = choisit_fichier_sortie(this, t.annotatedImage, QDir(m_dernierDossier).filePath(nom),
                                                  u"PNG (*.png)"_s, u"png"_s);
    if (chemin.isEmpty())
        return;
    QString erreur;
    if (!ecriture_image::enregistre(chemin, rendu(), "PNG", erreur))
        affiche_statut(t.saveFailed.arg(QDir::toNativeSeparators(chemin), erreur), icons::Status::Error);
    else
        affiche_statut(t.savedFile.arg(QDir::toNativeSeparators(chemin)), icons::Status::Success);
}

void FenetrePrincipale::exporte_csv()
{
    if (occupe() || !m_seance.coupe_mesurable() || !confirme_echelle_export())
        return;
    const Textes &t = tr();
    const StatusMessage statutPrecedent = m_statut;
    affiche_statut(t.chooseSave);
    const QString proposition = QDir(m_dernierDossier).filePath(t.csvName);
    const QString nomSaisi = QFileDialog::getSaveFileName(this, t.dialogExport, proposition, u"CSV (*.csv)"_s);
    if (nomSaisi.isEmpty()) {
        affiche_statut(statutPrecedent);
        return;
    }
    const QString chemin = avec_extension_csv(nomSaisi);
    const QFileInfo cible(chemin);
    if (cible.exists() && !cible.isFile()) {
        affiche_statut(t.exportFailed(t.regularFileRequired), icons::Status::Error);
        return;
    }
    // Le dialogue a confirmé le nom saisi. Si on lui ajoute .csv, vérifier la vraie destination.
    if (chemin != nomSaisi && cible.exists() && !confirme_remplacement(this, t, chemin)) {
        affiche_statut(statutPrecedent);
        return;
    }
    const Profil *profil = m_seance.profil();
    if (!profil) {
        affiche_statut(statutPrecedent);
        return;
    }
    QSaveFile fichier(chemin);
    if (!fichier.open(QIODevice::WriteOnly)) {
        affiche_statut(t.exportFailed(fichier.errorString()), icons::Status::Error);
        return;
    }
    const QByteArray donnees = en_csv(*profil, m_seance.etalonnage, t.decimal).toUtf8();
    if (ecrit_tout(fichier, donnees) && fichier.commit())
        affiche_statut(t.exported(QDir::toNativeSeparators(chemin)), icons::Status::Success);
    else
        affiche_statut(t.exportFailed(fichier.errorString()), icons::Status::Error);
}

// ---------------------------------------------------------------- fermeture

void FenetrePrincipale::closeEvent(QCloseEvent *event)
{
    if (m_travailAEcrire) {
        m_apresSauvegarde = [this] { close(); };
        event->ignore();
        return;
    }
    termine_saisie();
    m_graphique->termine_clic();
    m_graphique->annule_glisser();
    m_canevas->annule_glisser();
    m_fermetureEnCours = true;
    const auto choix = seance_modifiee() ? confirme_abandon(tr().quitWithMeasures, u"confirmQuit"_s)
                                        : ChoixAbandon::Abandonner;
    m_fermetureEnCours = false;
    if (choix == ChoixAbandon::Enregistrer) {
        event->ignore();
        m_apresSauvegarde = [this] { close(); };
        enregistre_projet();
        return;
    }
    if (choix == ChoixAbandon::Annuler) {
        event->ignore();
        if (occupe() && m_lecture.isFinished())
            QTimer::singleShot(0, this, &FenetrePrincipale::apres_chargement);
        return;
    }
    m_cheminChargement.clear();
    ecrit_reglages();
    event->accept();
}
