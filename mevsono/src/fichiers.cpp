// Du choix du fichier à son installation dans les deux graphiques.
#include "app.h"

#include "signalplot.h"
#include "spectrumplot.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

using namespace Qt::StringLiterals;

bool MainWindow::occupe() const
{
    // Le décodeur peut avoir fini avant que la fenêtre reçoive sa notification.
    // Garder le chemin jusque-là évite qu’un second dépôt remplace le résultat à récupérer.
    return !m_loadingPath.isEmpty();
}

void MainWindow::ouvre_dialogue()
{
    if (occupe())
        return;
    const Tr &t = tr();
    QStringList patterns;
    for (const QString &ext : audio::extensions())
        patterns << u"*."_s + ext << u"*."_s + ext.toUpper();
    const QString filters = u"%1 (%2);;%3 (*)"_s.arg(t.sounds, patterns.join(u' '), t.allFiles);
    const StatusMessage precedent = m_status;
    affiche_statut(t.chooseSound);
    const QString path = QFileDialog::getOpenFileName(this, t.dialogOpen, m_session.params.lastDir, filters);
    affiche_statut(precedent);
    if (!path.isEmpty())
        charge_chemin(path);
}

void MainWindow::charge_chemin(const QString &path)
{
    if (path.isEmpty() || occupe())
        return;
    m_statutAvantChargement = m_status;
    m_loadingPath = path;
    affiche_statut(tr().loading(QDir::toNativeSeparators(path)));
    m_cancelLoad = false;
    prepare_chargement();
    rafraichit();
}

void MainWindow::prepare_chargement(std::optional<audio::Audio> sonDejaLu)
{
    m_loader.setFuture(QtConcurrent::run([chemin = m_loadingPath, reglages = m_session.params,
                                          annule = &m_cancelLoad, son = std::move(sonDejaLu)]() mutable {
        Chargement resultat;
        if (son)
            resultat.lecture.audio = std::move(son);
        else
            resultat.lecture = audio::charge(chemin, annule);
        if (resultat.lecture.audio && !*annule)
            resultat.apercu = ApercuSon::prepare(*resultat.lecture.audio, reglages);
        return resultat;
    }));
}

void MainWindow::apres_chargement()
{
    // chargement interrompu par la fermeture de la fenêtre
    if (m_cancelLoad)
        return;
    const Tr &t = tr();
    Chargement chargement;
    try {
        chargement = m_loader.future().takeResult();
    } catch (...) {
        m_loadingPath.clear();
        affiche_statut(t.unexpectedError, icons::Status::Error);
        rafraichit();
        return;
    }
    auto &result = chargement.lecture;
    if (result.audio && !chargement.apercu.correspond(*result.audio, m_session.params)) {
        // Le mode ou la voie a changé pendant l'ouverture. Le fichier est déjà décodé.
        prepare_chargement(std::move(result.audio));
        return;
    }
    const QString chemin = std::exchange(m_loadingPath, QString());
    if (!result.audio) {
        QString reason = result.message;
        switch (result.error) {
        case audio::LoadError::Unsupported: reason = t.unsupportedFormat; break;
        case audio::LoadError::NoAudio: reason = t.noAudio; break;
        case audio::LoadError::Incomplete: reason = t.incompleteAudio; break;
        case audio::LoadError::Other: break;
        }
        affiche_statut(t.openFailed(QDir::toNativeSeparators(chemin), reason), icons::Status::Error);
        rafraichit();
        return;
    }
    if (!confirme_remplacement()) {
        affiche_statut(m_statutAvantChargement);
        rafraichit();
        return;
    }
    m_prise = m_priseModifiee = false;
    m_session.params.lastDir = QFileInfo(chemin).absolutePath();
    m_appliedParams.lastDir = m_session.params.lastDir;
    reglages_modifies();
    QString status = t.loaded(result.audio->name, infos_son(t, *result.audio));
    const bool truncated = result.audio->truncated, damaged = result.audio->damaged,
               invalid = result.audio->invalidSamples;
    if (truncated)
        status += u" — "_s + t.truncated;
    if (damaged)
        status += u" — "_s + t.damaged;
    if (invalid)
        status += u" — "_s + t.invalidSamples;
    installe_son(std::move(*result.audio), std::move(chargement.apercu));
    affiche_statut(status, truncated || damaged || invalid ? icons::Status::Warning : icons::Status::Success);
}

void MainWindow::installe_son(audio::Audio sound, ApercuSon apercu)
{
    m_player.arrete();
    m_signal->annule_geste();
    m_session.installe_son(std::move(sound), std::move(apercu));
    m_analyzer.vide();
    m_signalViews = {};
    m_signal->reinitialise_vue();
    m_spectrum->reinitialise_vue();
    rafraichit();
    met_a_jour_lecture();
}

namespace {
QString chemin_du_depot(const QMimeData &donnees)
{
    for (const QUrl &url : donnees.urls()) {
        if (url.isLocalFile())
            return url.toLocalFile();
    }
    return {};
}
} // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (occupe() || chemin_du_depot(*event->mimeData()).isEmpty())
        return;
    event->acceptProposedAction();
    m_dropOverlay->setGeometry(rect());
    m_dropOverlay->raise();
    m_dropOverlay->show();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropOverlay->hide();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    m_dropOverlay->hide();
    const QString chemin = chemin_du_depot(*event->mimeData());
    if (occupe() || chemin.isEmpty())
        return;
    event->acceptProposedAction();
    charge_chemin(chemin);
}
