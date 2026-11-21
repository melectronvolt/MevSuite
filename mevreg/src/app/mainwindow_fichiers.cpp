// Ouvrir, enregistrer et récupérer une séance, importer ou écrire un CSV.
#include "app/mainwindow.h"

#include "app/csvimport.h"
#include "app/guide.h"
#include "app/savefile.h"
#include "app/scriptpanel.h"
#include "app/tableview.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace {

bool est_fichier_mesures(const QString &path)
{
    static const QStringList suffixes = {u"csv"_s, u"txt"_s, u"tsv"_s, u"dat"_s};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

void MainWindow::ouvre_dialogue()
{
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    const QString p = QFileDialog::getOpenFileName(this, u"Ouvrir"_s, dir, u"Documents MeVReg et mesures (*.phy *.csv *.txt *.tsv *.dat);;Espace de travail MeVReg (*.phy);;Mesures CSV (*.csv *.txt *.tsv *.dat);;Tous les fichiers (*)"_s);
    if (!p.isEmpty())
        demande_ouverture(p);
}

void MainWindow::demande_ouverture(const QString &chemin)
{
    // Un CSV ajoute des mesures à la séance. Un fichier .phy la remplace.
    if (est_fichier_mesures(chemin) || confirme_remplacement())
        ouvre_chemin(chemin);
}

bool MainWindow::confirme_remplacement()
{
    if (!valide_saisies())
        return false;
    m_store.valide_maintenant();
    if (!m_store.est_modifie())
        return true;
    QMessageBox box(QMessageBox::Warning, u"Modifications non enregistrées"_s, u"Le document a été modifié. Voulez-vous enregistrer les modifications ?"_s,
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.button(QMessageBox::Save)->setText(u"Enregistrer"_s);
    box.button(QMessageBox::Discard)->setText(u"Ne pas enregistrer"_s);
    box.button(QMessageBox::Cancel)->setText(u"Annuler"_s);
    box.setDefaultButton(QMessageBox::Save);
    switch (box.exec()) {
    case QMessageBox::Save:
        return enregistre(false);
    case QMessageBox::Discard:
        return true;
    default:
        return false;
    }
}

void MainWindow::definit_espace_travail(Seance ws, const QJsonValue &layout, const std::optional<QByteArray> &file)
{
    oublie_secours();
    ferme_toutes_fenetres();
    if (m_guide) m_guide->reinitialise();
    m_store.reinitialise(std::move(ws));
    const quint64 document = ++m_document;
    if (m_modeSimple)
        m_dispositionExpert = layout;
    else if (layout.isObject())
        restaure_disposition(layout);
    else
        disposition_par_defaut();
    if (file && !m_store.ws.scripts.empty()) {
        // Le fichier contient déjà les mesures corrigées. Rejouer, par exemple,
        // une conversion de V en mV multiplierait une deuxième fois les valeurs.
        const bool recu = !m_store.prefs.est_de_confiance(*file);
        for (const auto &s : m_store.ws.scripts) {
            if (s.linked)
                m_store.runs[s.id].inputHash = empreinte_entrees(m_store.ws, s.id);
            if (recu)
                m_store.blockedScripts.insert(s.id);
        }
        if (recu)
            QTimer::singleShot(0, this, [this, content = *file, document] { demande_scripts(content, document); });
    }
    rafraichit();
}

void MainWindow::demande_scripts(const QByteArray &file, quint64 document)
{
    if (document != m_document || m_store.blockedScripts.empty())
        return;
    const int n = static_cast<int>(m_store.ws.scripts.size());
    auto *box = new QMessageBox(QMessageBox::Warning, u"Scripts Python"_s,
                                n > 1 ? u"Ce document contient %1 scripts Python. Les lancer ?"_s.arg(n)
                                      : u"Ce document contient un script Python. Le lancer ?"_s,
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setObjectName(u"confirmScripts"_s);
    box->setInformativeText(u"Un script peut agir sur l'ordinateur : lire, modifier ou effacer des fichiers. Lancez-le seulement "
                            "si le document vient d'une personne de confiance. Sinon, lisez-le d'abord : le bouton « Exécuter » "
                            "de sa fenêtre le lance."_s);
    QPushButton *run = box->addButton(n > 1 ? u"Lancer les scripts"_s : u"Lancer le script"_s, QMessageBox::AcceptRole);
    run->setObjectName(u"run"_s);
    run->setIcon(icons::obtient("play"));
    QPushButton *no = box->addButton(u"Ne pas lancer"_s, QMessageBox::RejectRole);
    no->setIcon(icons::obtient("stop"));
    no->setObjectName(u"norun"_s);
    // Entrée ou Échap : rien ne se lance
    box->setDefaultButton(no);
    box->setEscapeButton(no);
    connect(box, &QMessageBox::buttonClicked, this, [this, run, file, document](QAbstractButton *clicked) {
        if (document != m_document)
            return;
        if (clicked != run) {
            m_store.message(u"Scripts non lancés : le bouton « Exécuter » de la fenêtre d'un script le lance"_s, icons::Status::Warning);
            return;
        }
        // accepté une fois : ce document-là ne pose plus la question
        m_store.prefs.fait_confiance(file);
        m_store.prefs.enregistre();
        m_store.blockedScripts.clear();
        lance_tous_scripts();
    });
    box->open();
}

static std::optional<phy::SeanceLue> lit_seance(const QString &chemin, QByteArray &octets, QString &erreur)
{
    QFile fichier(chemin);
    if (fichier.open(QIODevice::ReadOnly)) {
        octets = fichier.readAll();
        if (fichier.error() == QFileDevice::NoError)
            return phy::charge(octets, &erreur);
    }
    erreur = fichier.errorString();
    return std::nullopt;
}

static QDir dossier_secours()
{
    return QDir(QFileInfo(fichier_reglages()).dir().filePath(u"recuperation-mevreg"_s));
}

void MainWindow::oublie_secours()
{
    m_delaiSecours.stop();
    if (!m_secours.isEmpty())
        QFile::remove(m_secours);
    m_secours.clear();
    m_verrouSecours.reset();
}

void MainWindow::ecrit_secours()
{
    transmet_saisie_scripts();
    if (!m_store.est_modifie())
        return;
    if (m_secours.isEmpty()) {
        const QDir dir = dossier_secours();
        const QString titre = m_path.isEmpty() ? m_store.ws.meta.title : QFileInfo(m_path).completeBaseName();
        const QString nom = csv::nettoie_nom(titre, u"Sans titre"_s).left(60) + u" -- "_s +
                            QUuid::createUuid().toString(QUuid::WithoutBraces) + u".phy"_s;
        const QString chemin = dir.filePath(nom);
        auto verrou = std::make_unique<QLockFile>(chemin + u".lock"_s);
        verrou->setStaleLockTime(0);
        if (!dir.mkpath(u"."_s) || !verrou->tryLock()) {
            m_delaiSecours.start(30000);
            affiche_statut(u"Sauvegarde de secours inaccessible"_s, icons::Status::Warning);
            return;
        }
        m_verrouSecours = std::move(verrou);
        m_secours = chemin;
    }
    QSaveFile f(m_secours);
    const QByteArray bytes = phy::enregistre(m_store.ws, disposition_json());
    if (!f.open(QIODevice::WriteOnly) || !ecrit_tout(f, bytes) || !f.commit()) {
        m_delaiSecours.start(30000);
        affiche_statut(u"Sauvegarde de secours impossible : %1"_s.arg(f.errorString()), icons::Status::Warning);
    }
}

void MainWindow::propose_recuperation(bool demarrage)
{
    QStringList titres, chemins;
    for (const QFileInfo &file : dossier_secours().entryInfoList({u"*.phy"_s}, QDir::Files, QDir::Time)) {
        QLockFile verrou(file.absoluteFilePath() + u".lock"_s);
        verrou.setStaleLockTime(0);
        if (!verrou.tryLock())
            continue; // séance encore ouverte dans un autre processus
        titres << u"%1 — %2 (%3)"_s.arg(file.completeBaseName().section(u" -- "_s, 0, 0),
            file.lastModified().toLocalTime().toString(u"dd/MM/yyyy hh:mm:ss"_s)).arg(titres.size() + 1);
        chemins << file.absoluteFilePath();
    }
    if (titres.isEmpty()) {
        if (!demarrage)
            affiche_statut(u"Aucune séance interrompue à récupérer."_s);
        return;
    }
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, u"Récupérer une séance"_s, u"Séance et dernière sauvegarde locale"_s,
                                                titres, 0, false, &ok);
    if (!ok)
        return;
    const QString chemin = chemins[titres.indexOf(choix)];
    if (!confirme_remplacement())
        return;
    auto verrou = std::make_unique<QLockFile>(chemin + u".lock"_s);
    verrou->setStaleLockTime(0);
    if (!verrou->tryLock() || !QFileInfo::exists(chemin)) {
        affiche_statut(u"Cette sauvegarde a déjà été reprise."_s, icons::Status::Warning);
        return;
    }
    QByteArray bytes;
    QString erreur;
    auto document = lit_seance(chemin, bytes, erreur);
    if (!document) {
        affiche_statut(erreur, icons::Status::Error);
        return;
    }
    m_path.clear();
    definit_espace_travail(std::move(document->seance), document->disposition, bytes);
    m_secours = chemin;
    m_verrouSecours = std::move(verrou);
    m_store.marque_modifie(true);
    affiche_statut(u"Séance récupérée. Enregistrez-la dans votre dossier de travail."_s);
}

void MainWindow::nouveau_fichier()
{
    definit_espace_travail(Seance{}, QJsonValue());
    m_path.clear();
    maj_titre();
    maj_statut();
    affiche_statut(u"Nouvel espace de travail : créez un tableur pour saisir vos mesures."_s);
}

bool MainWindow::ouvre_chemin(const QString &chemin)
{
    // fichier de mesures : aperçu avant import, dans le document ouvert
    if (est_fichier_mesures(chemin)) {
        csvimport::ouvre(*this, chemin, std::nullopt);
        return true;
    }
    QByteArray octets;
    QString erreur;
    auto lecture = lit_seance(chemin, octets, erreur);
    if (!lecture) {
        m_store.message(u"%1 : %2"_s.arg(QFileInfo(chemin).fileName(), erreur), icons::Status::Error);
        return false;
    }
    m_path = QFileInfo(chemin).absoluteFilePath();
    m_empreinteFichier = QCryptographicHash::hash(octets, QCryptographicHash::Sha256);
    definit_espace_travail(std::move(lecture->seance), lecture->disposition, octets);
    ajoute_recent(m_path);
    m_store.message(u"%1 ouvert"_s.arg(QFileInfo(chemin).fileName()));
    maj_titre();
    maj_statut();
    return true;
}

void MainWindow::ajoute_recent(const QString &path)
{
    QStringList &r = m_store.prefs.recent;
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 8)
        r.removeLast();
    m_store.prefs.enregistre();
}

void MainWindow::transmet_saisie_scripts()
{
    for (const auto &[k, d] : m_docks)
        if (d && d->key.kind == TabKey::Script)
            if (auto *sp = qobject_cast<ScriptPanel *>(d->widget()))
                sp->transmet_saisie();
}

bool MainWindow::valide_saisies()
{
    for (auto *grille : findChildren<TableGrid *>())
        if (!grille->valide_saisie())
            return false;
    transmet_saisie_scripts();
    return true;
}

bool MainWindow::enregistre(bool saveAs, const QString &to)
{
    if (!valide_saisies())
        return false;
    m_store.valide_maintenant();
    QString path = to.isEmpty() ? m_path : resout_chemin(to, u"phy"_s);
    if (path.isEmpty() || (saveAs && to.isEmpty())) {
        const QString base = m_store.ws.meta.title.trimmed().isEmpty() ? u"mesures"_s : csv::nettoie_nom(m_store.ws.meta.title, u"mesures"_s);
        const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
        path = QFileDialog::getSaveFileName(this, u"Enregistrer"_s, QDir(dir).filePath(base + u".phy"_s), u"Espace de travail MeVReg (*.phy)"_s);
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += u".phy"_s;
    }
    const QByteArray bytes = phy::enregistre(m_store.ws, disposition_json());
    const QFileInfo cible(path);
    const QString canonique = cible.exists() ? cible.canonicalFilePath() : cible.absoluteFilePath();
    QLockFile verrou(canonique + u".mevreg.lock"_s);
    if (!verrou.tryLock()) {
        m_store.message(u"Enregistrement impossible. Vérifiez que le dossier est accessible et qu’aucune autre séance n’enregistre ce fichier."_s, icons::Status::Error);
        return false;
    }
    if (!m_path.isEmpty() && QFileInfo(m_path) == cible && !m_empreinteFichier.isEmpty()) {
        QFile precedent(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!precedent.open(QIODevice::ReadOnly) || !hash.addData(&precedent) || hash.result() != m_empreinteFichier) {
            m_store.message(u"Le fichier a changé ou a été déplacé. Utilisez Fichier › Enregistrer sous… pour conserver votre travail dans une copie."_s, icons::Status::Error);
            return false;
        }
    }
    // écrit à côté, puis mis à la place de l'ancien fichier : un échec (disque plein, clé USB
    // retirée) laisse le document précédent intact
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || !ecrit_tout(f, bytes) || !f.commit()) {
        m_store.message(u"Enregistrement impossible : %1 ; le fichier précédent n'a pas été modifié"_s.arg(f.errorString()), icons::Status::Error);
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_empreinteFichier = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    m_store.marque_modifie(false);
    if (!m_secours.isEmpty() && QFileInfo(m_secours) == QFileInfo(path))
        m_secours.clear();
    oublie_secours();
    ajoute_recent(m_path);
    // Le contenu enregistré ici est reconnu à sa prochaine ouverture. Une copie
    // d'un document reçu garde sa demande d'accord si ses scripts sont encore bloqués.
    if (!m_store.ws.scripts.empty() && m_store.blockedScripts.empty()) {
        m_store.prefs.fait_confiance(bytes);
        m_store.prefs.enregistre();
    }
    m_store.message(u"%1 enregistré"_s.arg(QFileInfo(path).fileName()));
    maj_titre();
    maj_statut();
    return true;
}

void MainWindow::importe_csv(std::optional<ViewId> table)
{
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, u"Importer un fichier de données"_s, dir, u"Fichiers de données (*.csv *.txt *.tsv *.dat);;Tous les fichiers (*)"_s);
    if (!path.isEmpty())
        csvimport::ouvre(*this, path, table);
}

void MainWindow::exporte_csv(const std::vector<QId> &ids)
{
    if (!valide_saisies())
        return;
    csvimport::exporte_fichier(*this, ids);
}

QString MainWindow::demande_chemin_csv()
{
    const QString base = m_store.ws.meta.title.trimmed().isEmpty() ? u"donnees"_s : csv::nettoie_nom(m_store.ws.meta.title, u"donnees"_s);
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, u"Exporter en CSV"_s, QDir(dir).filePath(base + u".csv"_s), u"Fichier CSV (*.csv *.txt)"_s);
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += u".csv"_s;
    return path;
}

bool MainWindow::ecrit_csv(const QString &path, const std::vector<QId> &ids, const csv::ExportOptions &opts, csv::Encoding encoding)
{
    const QByteArray bytes = csv::encode_texte(csv::texte_export(m_store.ws.doc, ids, opts), encoding);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || !ecrit_tout(f, bytes) || !f.commit()) {
        m_store.message(u"Export impossible : %1"_s.arg(f.errorString()), icons::Status::Error);
        return false;
    }
    m_store.message(u"Données exportées : %1"_s.arg(QFileInfo(path).fileName()), icons::Status::Success);
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirme_remplacement()) {
        event->ignore();
        return;
    }
    oublie_secours();
    m_store.prefs.enregistre();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &u : event->mimeData()->urls()) {
            const QString chemin = u.toLocalFile();
            if (QFileInfo(chemin).suffix().toLower() == u"phy" || est_fichier_mesures(chemin)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &u : event->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == u"phy") {
            event->acceptProposedAction();
            QTimer::singleShot(0, this, [this, path] { demande_ouverture(path); });
            return;
        }
        if (est_fichier_mesures(path)) {
            event->acceptProposedAction();
            // aperçu avant import, une fois le glisser-déposer terminé
            QTimer::singleShot(0, this, [this, path] { csvimport::ouvre(*this, path, std::nullopt); });
        }
    }
}
