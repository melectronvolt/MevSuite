#include "app.h"
#include "audiodevices.h"
#include "format.h"

#include <QAudioSource>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

void MainWindow::montre_peripheriques()
{
    QDialog boite(this);
    boite.setWindowTitle(tr().audioDevices);
    auto *accueil = m_devices->parentWidget();
    auto *layout = qobject_cast<QVBoxLayout *>(accueil->layout());
    const int position = layout->indexOf(m_devices);
    QVBoxLayout contenu(&boite);
    contenu.addWidget(m_devices);
    QDialogButtonBox boutons(QDialogButtonBox::Close);
    contenu.addWidget(&boutons);
    connect(&boutons, &QDialogButtonBox::rejected, &boite, &QDialog::reject);
    boite.exec();
    layout->insertWidget(position, m_devices, 0, Qt::AlignHCenter);
    m_devices->show();
}

void MainWindow::enregistre_son()
{
    if (occupe())
        return;
    const QAudioDevice micro = m_devices->entree();
    const Tr &t = tr();
    QAudioFormat format = micro.preferredFormat();
    if (micro.isNull() || !format.isValid()) {
        affiche_statut(t.inputFailure, icons::Status::Error);
        return;
    }
    if (!confirme_remplacement())
        return;
    // Un micro mono suffit en TP. Garder le format natif si le pilote impose plusieurs voies.
    QAudioFormat mono = format;
    mono.setChannelCount(1);
    if (micro.isFormatSupported(mono))
        format = mono;
    m_player.arrete();
    met_a_jour_lecture();

    QDialog boite(this);
    boite.setWindowTitle(t.recordSound);
    QVBoxLayout contenu(&boite);
    QLabel nom(micro.description()), duree;
    nom.setTextFormat(Qt::PlainText);
    QHBoxLayout entree;
    entree.addWidget(&nom, 1);
    QPushButton actualiser(t.refreshDevices);
    entree.addWidget(&actualiser);
    connect(&actualiser, &QPushButton::clicked, m_devices, &AudioDevices::actualise);
    contenu.addLayout(&entree);
    contenu.addWidget(&duree);
    QLabel niveau(t.inputLevel);
    QProgressBar crete;
    crete.setAccessibleName(t.inputLevel);
    crete.setRange(-60, 0);
    crete.setValue(-60);
    crete.setTextVisible(false);
    contenu.addWidget(&niveau);
    contenu.addWidget(&crete);
    QDialogButtonBox boutons;
    auto *arreter = boutons.addButton(t.stopRecording, QDialogButtonBox::AcceptRole);
    arreter->setDefault(true);
    boutons.addButton(t.cancel, QDialogButtonBox::RejectRole);
    contenu.addWidget(&boutons);
    connect(&boutons, &QDialogButtonBox::accepted, &boite, &QDialog::accept);
    connect(&boutons, &QDialogButtonBox::rejected, &boite, &QDialog::reject);

    audio::Prise prise(format);
    QAudioSource source(micro, format);
    QPointer<QIODevice> flux = source.start();
    if (!flux || source.error() != QtAudio::NoError) {
        affiche_statut(t.inputFailure, icons::Status::Error);
        return;
    }

    bool interrompue = false;
    const auto recoit = [&] {
        if (flux)
            prise.ajoute(flux->readAll());
    };
    connect(flux, &QIODevice::readyRead, &boite, recoit);
    QTimer suivi;
    const auto actualise = [&] {
        const float amplitude = prise.releve_crete();
        const double db = en_decibels(amplitude);
        crete.setValue(std::clamp(int(std::round(db)), -60, 0));
        const QString valeur = amplitude > 0 ? texte_decimal(db, 1, t.decimal) : u"−∞"_s;
        QString etat = t.inputLevel + u" : "_s + valeur + u" dBFS"_s;
        if (amplitude >= 0.998f)
            etat += u" · "_s + t.fullScale;
        else if (db < -40)
            etat += u" · "_s + t.weakSignal;
        niveau.setText(etat);
        duree.setText(t.recordingTime.arg(texte_duree(prise.duree(), t.decimal)));
        interrompue = source.error() != QtAudio::NoError || source.state() == QtAudio::StoppedState ||
                      micro.id() != m_devices->entree().id();
        if (interrompue || prise.pleine())
            boite.accept();
    };
    duree.setText(t.recordingTime.arg(texte_duree(0, t.decimal)));
    connect(&suivi, &QTimer::timeout, &boite, actualise);
    connect(m_devices, &AudioDevices::entree_changee, &boite, actualise);
    suivi.start(100);
    const bool garder = boite.exec() == QDialog::Accepted;
    suivi.stop();
    if (flux)
        disconnect(flux, nullptr, &boite, nullptr);
    if (garder) {
        recoit();
        interrompue = interrompue || source.error() != QtAudio::NoError;
    }
    source.stop();
    if (!garder)
        return;
    auto son = prise.termine(t.takeName);
    if (!son) {
        affiche_statut(interrompue ? t.inputFailure : t.emptyTake, icons::Status::Warning);
        return;
    }
    // Un micro débranché laisse une prise partielle, utilisable jusqu'à la dernière trame reçue.
    son->damaged = interrompue;
    QString message = t.loaded(t.takeName, infos_son(t, *son));
    if (son->truncated)
        message += u" — "_s + t.truncated;
    if (son->invalidSamples)
        message += u" — "_s + t.invalidSamples;
    if (interrompue)
        message += u" — "_s + t.inputFailure;
    const bool avertissement = son->truncated || son->invalidSamples || interrompue;
    auto apercu = ApercuSon::prepare(*son, m_session.params);
    m_prise = m_priseModifiee = true;
    installe_son(std::move(*son), std::move(apercu));
    affiche_statut(message, avertissement ? icons::Status::Warning : icons::Status::Success);
}

bool MainWindow::sauvegarde_prise()
{
    if (!m_prise || !m_session.loaded)
        return false;
    const Tr &t = tr();
    auto &son = m_session.loaded->audio;
    QFileDialog choix(this, t.saveTake, m_session.params.lastDir, u"WAV (*.wav)"_s);
    choix.setAcceptMode(QFileDialog::AcceptSave);
    choix.setDefaultSuffix(u"wav"_s);
    choix.selectFile(son.name);
    if (choix.exec() != QDialog::Accepted)
        return false;
    const QString path = choix.selectedFiles().first();
    if (!audio::ecrit_wav(path, son)) {
        QMessageBox::warning(this, t.saveTake, t.exportFailed(QDir::toNativeSeparators(path)));
        return false;
    }
    m_priseModifiee = false;
    const QFileInfo fichier(path);
    son.name = fichier.fileName();
    m_session.params.lastDir = fichier.absolutePath();
    m_appliedParams.lastDir = m_session.params.lastDir;
    reglages_modifies();
    rafraichit();
    affiche_statut(t.exported(QDir::toNativeSeparators(path)), icons::Status::Success);
    return true;
}

bool MainWindow::confirme_remplacement()
{
    if (!m_priseModifiee)
        return true;
    const auto choix = QMessageBox::question(this, tr().takeName, tr().unsavedTake,
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choix == QMessageBox::Discard || (choix == QMessageBox::Save && sauvegarde_prise());
}
