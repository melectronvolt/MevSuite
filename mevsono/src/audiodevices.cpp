#include "audiodevices.h"
#include "i18n.h"

#include <QComboBox>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QLabel>
#include <QMediaDevices>
#include <QPushButton>
#include <QSignalBlocker>

using namespace Qt::StringLiterals;

namespace {

bool remplit(QComboBox *liste, const QList<QAudioDevice> &disponibles, const QAudioDevice &defaut)
{
    const QByteArray id = liste->currentData().value<QAudioDevice>().id();
    const QSignalBlocker bloque(liste);
    liste->clear();
    int choix = -1, repli = 0;
    for (const auto &peripherique : disponibles) {
        const int i = liste->count();
        liste->addItem(peripherique.description(), QVariant::fromValue(peripherique));
        liste->setItemData(i, peripherique.description(), Qt::ToolTipRole);
        if (peripherique.id() == id)
            choix = i;
        if (peripherique.id() == defaut.id())
            repli = i;
    }
    liste->setCurrentIndex(choix >= 0 ? choix : disponibles.isEmpty() ? -1 : repli);
    liste->setEnabled(!disponibles.isEmpty());
    liste->setToolTip(liste->currentText());
    return id != liste->currentData().value<QAudioDevice>().id();
}
}

AudioDevices::AudioDevices(QWidget *parent) : QWidget(parent)
{
    auto *grille = new QGridLayout(this);
    m_entrees = new QComboBox(this);
    m_sorties = new QComboBox(this);
    m_entrees->setObjectName(u"audioInput"_s);
    m_sorties->setObjectName(u"audioOutput"_s);
    m_labelEntree = new QLabel(this);
    m_labelSortie = new QLabel(this);
    m_labelEntree->setBuddy(m_entrees);
    m_labelSortie->setBuddy(m_sorties);
    for (auto *liste : {m_entrees, m_sorties}) {
        liste->setMinimumContentsLength(32);
        connect(liste, &QComboBox::currentTextChanged, liste, &QWidget::setToolTip);
        liste->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    }
    m_actualiser = new QPushButton(this);
    grille->addWidget(m_labelEntree, 0, 0);
    grille->addWidget(m_entrees, 0, 1);
    grille->addWidget(m_labelSortie, 1, 0);
    grille->addWidget(m_sorties, 1, 1);
    grille->addWidget(m_actualiser, 2, 1, Qt::AlignRight);
    connect(m_entrees, &QComboBox::currentIndexChanged, this, &AudioDevices::entree_changee);
    connect(m_sorties, &QComboBox::currentIndexChanged, this, &AudioDevices::sortie_changee);
    connect(m_actualiser, &QPushButton::clicked, this, &AudioDevices::actualise);
    if (qgetenv("QT_AUDIO_BACKEND") == "pulseaudio") {
        const auto pactl = QStandardPaths::findExecutable(u"pactl"_s);
        if (!pactl.isEmpty())
            surveille_retraits_pulse(pactl);
    }
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioInputsChanged, this, &AudioDevices::actualise, Qt::QueuedConnection);
    connect(devices, &QMediaDevices::audioOutputsChanged, this, &AudioDevices::actualise, Qt::QueuedConnection);
    affiche(QMediaDevices::audioInputs(), QMediaDevices::audioOutputs());
    actualise();
}

void AudioDevices::surveille_retraits_pulse(const QString &pactl)
{
    // Le backend Pulse de Qt peut garder dans son cache un périphérique retiré.
    // Comparer les identifiants Qt à l'inventaire du serveur, hors du fil de l'interface.
    m_inventaire = new QProcess(this);
    m_inventaire->setProgram(pactl);
    m_inventaire->setArguments({u"--format=json"_s, u"list"_s});
    connect(m_inventaire, &QProcess::errorOccurred, this, [this](QProcess::ProcessError erreur) {
        // Si pactl a disparu depuis le démarrage, Qt peut toujours actualiser la liste.
        if (erreur == QProcess::FailedToStart)
            affiche(QMediaDevices::audioInputs(), QMediaDevices::audioOutputs());
    });
    connect(m_inventaire, &QProcess::finished, this, [this](int code, QProcess::ExitStatus statut) {
        const auto inventaire = QJsonDocument::fromJson(m_inventaire->readAllStandardOutput()).object();
        auto disponibles = [&](QList<QAudioDevice> liste, const QString &cle) {
            if (statut == QProcess::NormalExit && code == 0 && inventaire.value(cle).isArray()) {
                QSet<QByteArray> ids;
                for (const auto &element : inventaire.value(cle).toArray())
                    ids.insert(element.toObject().value(u"name"_s).toString().toUtf8());
                liste.removeIf([&](const QAudioDevice &d) { return !ids.contains(d.id()); });
            }
            return liste;
        };
        affiche(disponibles(QMediaDevices::audioInputs(), u"sources"_s),
                disponibles(QMediaDevices::audioOutputs(), u"sinks"_s));
        if (m_actualisationDemandee)
            actualise();
    });
    m_evenements = new QProcess(this);
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(u"LC_ALL"_s, u"C"_s);
    m_evenements->setProcessEnvironment(env);
    connect(m_evenements, &QProcess::readyReadStandardOutput, this, [this] {
        while (m_evenements->canReadLine()) {
            const auto ligne = m_evenements->readLine();
            if (ligne.contains("'remove' on sink #") || ligne.contains("'remove' on source #"))
                actualise();
        }
    });
    m_evenements->start(pactl, {u"subscribe"_s});
}

AudioDevices::~AudioDevices()
{
    for (auto *processus : {m_evenements, m_inventaire}) {
        if (processus && processus->state() != QProcess::NotRunning) {
            disconnect(processus, nullptr, this, nullptr);
            processus->kill();
            processus->waitForFinished();
        }
    }
}

void AudioDevices::actualise()
{
    if (m_inventaire) {
        m_actualisationDemandee = m_inventaire->state() != QProcess::NotRunning;
        if (!m_actualisationDemandee)
            m_inventaire->start();
    } else {
        affiche(QMediaDevices::audioInputs(), QMediaDevices::audioOutputs());
    }
}

void AudioDevices::affiche(QList<QAudioDevice> entrees, QList<QAudioDevice> sorties)
{
    const bool entreeModifiee = remplit(m_entrees, entrees, QMediaDevices::defaultAudioInput());
    const bool sortieModifiee = remplit(m_sorties, sorties, QMediaDevices::defaultAudioOutput());
    if (entreeModifiee)
        emit entree_changee();
    if (sortieModifiee)
        emit sortie_changee();
}

QAudioDevice AudioDevices::entree() const { return m_entrees->currentData().value<QAudioDevice>(); }
QAudioDevice AudioDevices::sortie() const { return m_sorties->currentData().value<QAudioDevice>(); }

void AudioDevices::retraduit(const Tr &t)
{
    m_labelEntree->setText(t.inputDevice);
    m_labelSortie->setText(t.outputDevice);
    m_entrees->setPlaceholderText(t.noInput);
    m_sorties->setPlaceholderText(t.noAudioOutput);
    m_actualiser->setText(t.refreshDevices);
}
