// Copie du tableau et écriture des fichiers CSV depuis le menu Fichier.
#include "app.h"

#include "about.h"
#include "savefile.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Le fichier de destination est remplacé une fois toutes les lignes écrites.
// Une annulation ferme le fichier provisoire et conserve l'export précédent.
void ecrit_csv(QPromise<QString> &promise, const QString &path, size_t lignes,
               const std::function<QString(size_t, size_t)> &bloc, const std::atomic_bool &annule)
{
    promise.setProgressRange(0, 100);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        promise.addResult(file.errorString());
        return;
    }
    constexpr size_t tailleBloc = 4096;
    size_t first = 0;
    do {
        if (annule)
            return;
        const size_t count = std::min(tailleBloc, lignes - first);
        if (!ecrit_tout(file, bloc(first, count).toUtf8())) {
            promise.addResult(file.errorString());
            return;
        }
        first += count;
        if (lignes)
            promise.setProgressValue(int(100.0 * double(first) / double(lignes)));
    } while (first < lignes);
    if (!annule)
        promise.addResult(file.commit() ? QString() : file.errorString());
}

} // namespace

void MainWindow::exporte_spectre()
{
    const dsp::Analysis *a = analyse_courante();
    if (!a || m_analyzer.en_attente())
        return;
    const size_t lignes = a->spectrum.amplitude.size();
    // Le résultat peut être remplacé pendant le dialogue de destination.
    enregistre_csv(tr().dialogExportSpectrum, tr().csvSpectrumName, lignes,
        [sp = a->spectrum, lang = m_lang](size_t first, size_t count) {
            return csv_spectre(textes(lang), sp, first, count);
        });
}

void MainWindow::exporte_extrait()
{
    const auto range = m_session.intervalle_extrait();
    if (!m_session.loaded || !range)
        return;
    const auto &son = m_session.loaded->audio;
    enregistre_csv(tr().dialogExportExtract, tr().csvExtractName, range->second - range->first,
        [samples = son.signal(m_session.canal()), start = range->first, rate = son.rate, lang = m_lang]
        (size_t first, size_t count) {
            return csv_extrait(textes(lang), std::span(*samples).subspan(start + first, count),
                               start + first, rate, first == 0);
        });
}

void MainWindow::enregistre_csv(const QString &title, const QString &name, size_t lignes,
                                const std::function<QString(size_t, size_t)> &bloc)
{
    if (occupe())
        return;
    const Tr &t = tr();
    const StatusMessage precedent = m_status;
    affiche_statut(t.chooseSave);
    const QString &dir = m_session.params.lastDir;
    const QString start = dir.isEmpty() ? name : QDir(dir).filePath(name);
    QString path = QFileDialog::getSaveFileName(this, title, start, u"CSV (*.csv)"_s);
    if (path.isEmpty()) {
        affiche_statut(precedent);
        return;
    }
    const bool completed = QFileInfo(path).suffix().isEmpty();
    path = avec_extension_csv(path);
    // nom tapé sans extension : le dialogue n'a pas vu que le fichier .csv existait déjà
    if (completed && QFileInfo::exists(path)) {
        QMessageBox box(QMessageBox::Question, about::NAME, t.replaceFile(QFileInfo(path).fileName()), QMessageBox::NoButton, this);
        QPushButton *replace = box.addButton(t.replaceButton, QMessageBox::AcceptRole);
        QPushButton *cancel = box.addButton(t.cancel, QMessageBox::RejectRole);
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() != replace) {
            affiche_statut(precedent);
            return;
        }
    }
    QProgressDialog progression(title, t.cancel, 0, 100, this);
    progression.setAutoClose(false);
    progression.setAutoReset(false);
    progression.setWindowModality(Qt::WindowModal);
    QFutureWatcher<QString> suivi;
    std::atomic_bool annule = false;
    connect(&progression, &QProgressDialog::canceled, this, [&] { annule = true; });
    connect(&suivi, &QFutureWatcher<QString>::progressValueChanged, &progression, &QProgressDialog::setValue);
    connect(&suivi, &QFutureWatcher<QString>::finished, &progression, &QDialog::accept);
    suivi.setFuture(QtConcurrent::run([&](QPromise<QString> &promise) {
        ecrit_csv(promise, path, lignes, bloc, annule);
    }));
    if (progression.exec() != QDialog::Accepted)
        annule = true;
    try {
        suivi.waitForFinished();
        if (!suivi.future().resultCount())
            affiche_statut(precedent);
        else if (const QString erreur = suivi.result(); !erreur.isEmpty())
            affiche_statut(t.exportFailed(erreur), icons::Status::Error);
        else
            affiche_statut(t.exported(QDir::toNativeSeparators(path)), icons::Status::Success);
    } catch (...) {
        affiche_statut(t.unexpectedError, icons::Status::Error);
    }
}

void MainWindow::copie_tableau()
{
    const std::vector<dsp::Harmonic> h = harmoniques();
    if (std::none_of(h.begin(), h.end(), [](const dsp::Harmonic &x) { return x.peak.has_value(); }))
        return;
    QApplication::clipboard()->setText(texte_tableau_harmoniques(tr(), h));
    affiche_statut(tr().tableCopied, icons::Status::Success);
}
