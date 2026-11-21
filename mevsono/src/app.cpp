#include "app.h"

#include "about.h"
#include "audiodevices.h"
#include "help.h"
#include "libelles.h"
#include "panneaumesures.h"
#include "settingsdialog.h"
#include "signalplot.h"
#include "spectrumplot.h"
#include "style.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QLibraryInfo>
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Voile affiché pendant le glisser-déposer d’un fichier.
class DropOverlay : public QWidget {
public:
    explicit DropOverlay(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

    QString text;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 160));
        QFont big = font();
        big.setPixelSize(24);
        painter.setFont(big);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, text);
    }
};

} // namespace

MainWindow::MainWindow(const Settings &settings, const SettingsFiles &files, const QString &startupPath)
    : m_lang(resout_langue(settings.lang))
    , m_files(files)
    , m_saved(settings)
    , m_windowSize(settings.windowSize)
    , m_maximized(settings.maximized)
    , m_baseFont(QApplication::font())
{
    m_session.params = settings;
    m_appliedParams = settings;

    setWindowIcon(QApplication::windowIcon());
    setMinimumSize(MIN_WINDOW);
    setAcceptDrops(true);

    construit_actions();
    construit_menus();
    construit_coin_des_menus();

    construit_espace_travail();
    construit_barre_etat();

    m_dropOverlay = new DropOverlay(this);
    relie_signaux();
    installe_raccourcis();

    change_langue(settings.lang);
    change_theme(settings.theme);
    affiche_statut(tr().statusStart);
    change_agrandissement(settings.uiZoom);
    rafraichit();

    if (!startupPath.isEmpty())
        QTimer::singleShot(0, this, [this, startupPath] { charge_chemin(startupPath); });
}

MainWindow::~MainWindow()
{
    // un chargement en cours lit m_cancelLoad : il est interrompu et attendu (fenêtre détruite sans
    // closeEvent, comme dans les essais)
    m_cancelLoad = true;
    m_loader.waitForFinished();
    // le son se coupe avant que les échantillons et la fenêtre disparaissent
    m_player.arrete();
}

// ---------------------------------------------------------------- construction

void MainWindow::relie_signaux()
{
    connect(m_signal, &SignalPlot::extrait_change, this, &MainWindow::rafraichit);
    connect(m_signal, &SignalPlot::periode_changee, this, [this] {
        rafraichit_panneau_lateral();
        ajuste_panneau_lateral();
    });
    connect(m_spectrum, &SpectrumPlot::selection_changee, this, &MainWindow::rafraichit_panneau_lateral);
    connect(m_spectrum, &SpectrumPlot::fondamentale_choisie, this, [this](std::optional<double> f0) {
        m_session.manualF0 = f0;
        rafraichit();
    });
    connect(&m_analyzer, &Analyzer::mis_a_jour, this, &MainWindow::analyse_mise_a_jour);
    connect(&m_loader, &QFutureWatcher<Chargement>::finished, this, &MainWindow::apres_chargement);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &MainWindow::ecrit_reglages);
    connect(m_devices, &AudioDevices::sortie_changee, this, &MainWindow::sortie_changee);
    connect(m_devices, &AudioDevices::entree_changee, this, &MainWindow::rafraichit);
    m_playTimer.setInterval(16);
    connect(&m_playTimer, &QTimer::timeout, this, &MainWindow::met_a_jour_lecture);
}

// ---------------------------------------------------------------- textes et état

void MainWindow::retraduit()
{
    const Tr &t = tr();
    retraduit_menus(t);

    m_welcomeTagline->setText(t.tagline);
    m_welcomeOpen->setText(u"\u2002"_s + t.openSound);
    m_welcomeOpen->setAccessibleName(t.openSound);
    m_welcomeRecord->setText(u"\u2002"_s + t.recordSound);
    m_welcomeRecord->setAccessibleName(t.recordSound);
    m_devices->retraduit(t);
    m_welcomeDropHint->setText(t.welcomeDropHint);
    dimensionne_boutons_accueil();

    m_signalTitle->setText(t.signalTitle);
    m_tabs->setTabText(0, t.tabPeriod);
    m_tabs->setTabToolTip(0, t.tabPeriodTip);
    m_tabs->setTabText(1, t.tabFrequencies);
    m_tabs->setTabToolTip(1, t.tabFrequenciesTip);
    m_spectrumTitle->setText(t.spectrumTitle);

    m_listenCombo->setItemText(0, t.listenSound);
    m_listenCombo->setItemText(1, t.listenExtract);
    m_listenCombo->setToolTip(t.listenTip);
    m_playButton->setToolTip(t.playTip);
    m_pauseButton->setToolTip(t.pauseTip);
    m_stopButton->setToolTip(t.stopTip);
    m_mesures->retraduit(t);
    static_cast<DropOverlay *>(m_dropOverlay)->text = t.dropHint;

    // couleurs des textes discrets (le thème a pu changer)
    const QPalette pal = QApplication::palette();
    const QColor placeholder = pal.color(QPalette::PlaceholderText);
    for (QLabel *label : {m_signalHint, m_spectrumHint, m_soundInfo, m_welcomeTagline, m_welcomeDropHint})
        definit_couleur_texte(label, placeholder);
    // En sombre, le style dessine le bord des champs (liste d'écoute, case, tableau) un peu plus foncé
    // que le fond de la fenêtre : sur le fond du panneau, il ne se voyait plus. Fond de référence plus
    // clair pour ce dessin, dans le panneau seulement (le panneau garde son propre fond).
    QPalette fields;
    if (est_sombre(pal))
        fields.setColor(QPalette::Window, pal.color(QPalette::Light));
    m_side->widget()->setPalette(fields);
    style_barre_etat();

    m_signal->setAccessibleName(t.signalTitle);
    m_spectrum->setAccessibleName(t.spectrumTitle);
    m_signal->definit_traductions(&t);
    m_spectrum->definit_traductions(&t);
    if (m_settingsDialog)
        m_settingsDialog->retraduit(t);
}

void MainWindow::rafraichit()
{
    const Clip *lectureEnCours = m_player.extrait_en_lecture();
    if (lectureEnCours && m_session.extrait_en_lecture() != *lectureEnCours) {
        m_player.arrete();
        m_signal->place_tete_lecture(std::nullopt);
    }
    demande_analyse();
    const Tr &libelles = tr();
    const Settings &reglages = m_session.params;
    const bool expert = reglages.expert;
    const bool sonCharge = m_session.loaded.has_value();
    const bool stereo = sonCharge && m_session.loaded->audio.channels.size() >= 2;

    setWindowTitle(u"%1 — %2"_s.arg(about::NAME, sonCharge ? m_session.loaded->audio.name : libelles.subtitle));
    affiche_page(sonCharge);
    m_actOpen->setEnabled(!occupe());
    m_welcomeOpen->setEnabled(!occupe());
    m_actRecord->setEnabled(!occupe() && !m_devices->entree().isNull());
    m_welcomeRecord->setEnabled(m_actRecord->isEnabled());
    m_actSaveTake->setEnabled(m_prise && !occupe());
    m_saveTakeButton->setVisible(m_prise);
    m_sepExports->setVisible(expert);
    m_actExportSpectrum->setVisible(expert);
    m_actExportExtract->setVisible(expert);
    actualise_actions_spectre();
    m_actExportExtract->setEnabled(sonCharge && !occupe());
    m_actFitAll->setEnabled(sonCharge);
    const bool etudeFrequences = m_session.tool == Tool::Extract;
    m_actZoomSelection->setEnabled(sonCharge && etudeFrequences);
    m_zoomButton->setVisible(etudeFrequences);
    for (QAction *action :
         {m_sepExpertView, m_actDb, m_actLog, m_actMarks, m_sepSettings, m_actSettings})
        action->setVisible(expert);
    m_settingsButton->setVisible(expert);
    m_menuChannel->menuAction()->setVisible(expert && stereo);
    m_actDb->setChecked(reglages.db);
    m_actLog->setChecked(reglages.logFreq);
    m_actMarks->setChecked(reglages.harmonicMarks);
    for (size_t i = 0; i < m_actChannels.size(); ++i)
        m_actChannels[i]->setChecked(audio::ALL_CHANNELS[i] == reglages.channel);
    m_actSimple->setChecked(!expert);
    m_actExpert->setChecked(expert);

    m_tabs->setCurrentIndex(etudeFrequences ? 1 : 0);
    m_signalHint->setText(!sonCharge ? QString() : etudeFrequences ? libelles.signalHint : libelles.signalHintPeriod);
    m_spectrumHint->setText(expert ? libelles.spectrumHintExpert : libelles.spectrumHint);
    m_busy->setVisible(occupe());

    rafraichit_panneau_lateral();
    rafraichit_transport();
    rafraichit_infos_etat();
    ajuste_panneau_lateral();
    m_signal->invalide_courbes();
    m_spectrum->analyse_changee();
    if (m_settingsDialog)
        m_settingsDialog->charge();
}

void MainWindow::rafraichit_panneau_lateral()
{
    m_soundBox->setVisible(m_session.loaded.has_value());
    if (!m_session.loaded)
        return;
    const Tr &t = tr();
    const audio::Audio &sound = m_session.loaded->audio;
    m_soundName->setText(sound.name);
    m_soundInfo->setText(infos_son(t, sound));
    QString comparaison;
    if (m_spectrum->a_reference()) {
        comparaison = m_spectrum->reference_compatible()
            ? t.spectrumLegend.arg(sound.name, m_spectrum->nom_reference(), m_normalise->isChecked() ? t.matchedPeaks : QString())
            : t.incompatibleSpectrum;
    }
    m_mesures->affiche(m_session, analyse_courante(), m_analyzer.en_attente(), m_spectrum->pic_selectionne(), comparaison, t);
}

void MainWindow::choisit_outil(Tool tool)
{
    if (tool == m_session.tool)
        return;
    // Chaque activité garde sa vue du signal : la période se mesure de près, l'extrait se choisit de
    // plus loin.
    if (m_session.loaded)
        m_signalViews[size_t(m_session.tool)] = std::pair(m_signal->vue_x0(), m_signal->vue_x1());
    m_signal->annule_geste();
    m_session.tool = tool;
    if (const auto &view = m_signalViews[size_t(tool)])
        m_signal->definit_vue_x(view->first, view->second);
    else
        m_signal->reinitialise_vue();
    // le spectre n'a sa place que dans l'étude des fréquences ; le signal prend alors toute la hauteur
    if (tool == Tool::Period) {
        m_splitSizes = m_vsplit->sizes();
        m_spectrumPane->hide();
    } else {
        m_spectrumPane->show();
        if (m_splitMoved && m_splitSizes.size() == 2)
            m_vsplit->setSizes(m_splitSizes);
        else
            partage_hauteur_signal();
    }
    rafraichit();
}

void MainWindow::rafraichit_transport()
{
    const bool expert = m_session.params.expert;
    m_listenCombo->setVisible(expert);
    m_listenLabel->setVisible(!expert);
    m_listenLabel->setText(m_session.choix_ecoute() == Listen::Sound ? tr().listenSoundOnly : tr().listenExtractOnly);
    m_listenCombo->setCurrentIndex(m_session.listen == Listen::Sound ? 0 : 1);
    const bool playing = m_player.en_lecture();
    const auto clip = m_session.extrait_en_lecture();
    m_playButton->setEnabled(!playing && clip && !clip->est_vide());
    m_pauseButton->setEnabled(playing);
    m_stopButton->setEnabled(playing || m_player.en_pause());
}

void MainWindow::change_langue(std::optional<Lang> choice)
{
    m_session.params.lang = choice;
    m_appliedParams.lang = choice;
    m_lang = resout_langue(choice);
    // Textes de Qt lui-même (boutons des boîtes de dialogue, menu contextuel des champs de saisie).
    QApplication::removeTranslator(&m_qtTranslator);
    if (m_qtTranslator.load(u"qtbase_"_s + code_langue(m_lang), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&m_qtTranslator);
    retraduit();
    affiche_statut(tr().languageChanged);
    rafraichit();
    reglages_modifies();
}

void MainWindow::change_theme(Qt::ColorScheme choice)
{
    m_session.params.theme = choice;
    m_appliedParams.theme = choice;
    // la nouvelle palette (changeEvent) met à jour le bouton, les couleurs des textes et des graphiques
    theme::choisit(choice);
    reglages_modifies();
}

void MainWindow::change_mode(bool expert)
{
    if (expert == m_session.params.expert)
        return;
    const auto listened = m_session.extrait_en_lecture();
    m_session.params.expert = expert;
    m_appliedParams.expert = expert;
    // En mode simplifié, l'onglet détermine si l'on écoute le son ou l'extrait.
    if (m_session.extrait_en_lecture() != listened)
        m_player.arrete();
    if (!expert && m_settingsDialog)
        m_settingsDialog->close();
    m_spectrum->reinitialise_vue();
    affiche_statut(expert ? tr().modeExpertOn : tr().modeSimpleOn);
    rafraichit();
    met_a_jour_lecture();
    reglages_modifies();
}

void MainWindow::parametres_modifies()
{
    const Settings &p = m_session.params;
    const Settings &old = m_appliedParams;
    if (p.db != old.db || p.logFreq != old.logFreq || p.fmax != old.fmax)
        m_spectrum->reinitialise_vue();
    m_appliedParams = p;
    rafraichit();
    reglages_modifies();
}

void MainWindow::ouvre_notice()
{
    QString error;
    const QString page = help::ecrit(m_lang, &error);
    if (page.isEmpty()) {
        affiche_statut(tr().helpFailed(error), icons::Status::Error);
        return;
    }
    const QString chemin = QDir::toNativeSeparators(page);
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(page)))
        affiche_statut(tr().helpOpened(chemin), icons::Status::Success);
    else
        affiche_statut(tr().helpFailed(chemin), icons::Status::Error);
}

void MainWindow::montre_a_propos()
{
    AboutDialog dialog(tr(), m_files, this);
    dialog.exec();
}

void MainWindow::montre_reglages()
{
    if (!m_session.params.expert)
        return;
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(m_session.params, this);
        m_settingsDialog->setObjectName(u"settings"_s);
        connect(m_settingsDialog, &SettingsDialog::modifie, this, &MainWindow::parametres_modifies);
        m_settingsDialog->retraduit(tr());
    }
    m_settingsDialog->charge();
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

// ---------------------------------------------------------------- analyse

void MainWindow::demande_analyse()
{
    // En mesure manuelle, les deux curseurs suffisent. Garder un spectre déjà calculé
    // si l'élève revient aux fréquences, et interrompre un calcul encore en cours.
    if (m_session.tool == Tool::Period && !m_session.params.expert) {
        if (m_analyzer.en_attente())
            m_analyzer.vide();
        return;
    }
    const auto key = m_session.cle_analyse();
    if (!key)
        return;
    m_analyzer.demande(*key, m_session.loaded->audio.signal(key->channel), m_session.loaded->audio.rate);
}

const dsp::Analysis *MainWindow::analyse_courante() const
{
    return m_session.loaded ? m_analyzer.resultat(m_session.loaded->id) : nullptr;
}

void MainWindow::actualise_actions_spectre()
{
    const bool spectreDisponible = analyse_courante() && !occupe() && !m_analyzer.en_attente();
    m_actExportSpectrum->setEnabled(spectreDisponible);
    m_memorise->setEnabled(spectreDisponible && m_session.tool == Tool::Extract);
    m_oublie->setEnabled(m_spectrum->a_reference());
    m_normalise->setEnabled(m_spectrum->a_reference());
}

void MainWindow::analyse_mise_a_jour()
{
    actualise_actions_spectre();
    rafraichit_panneau_lateral();
    ajuste_panneau_lateral();
    m_spectrum->analyse_changee();
}

std::optional<double> MainWindow::fondamentale_courante() const
{
    return m_session.fondamentale(analyse_courante());
}

std::vector<dsp::Harmonic> MainWindow::harmoniques() const
{
    return m_session.harmoniques(analyse_courante());
}

SettingsDialog *MainWindow::dialogue_reglages() const
{
    return m_settingsDialog;
}

void MainWindow::montre_signal(double t0, double t1)
{
    m_signal->definit_vue_x(t0, t1);
}

// ---------------------------------------------------------------- écoute

void MainWindow::sortie_changee()
{
    m_player.arrete();
    met_a_jour_lecture();
    const auto sortie = m_devices->sortie();
    affiche_statut(sortie.isNull() ? tr().noAudioOutput : tr().outputChanged.arg(sortie.description()));
}

void MainWindow::analyse_intervalle()
{
    if (!m_session.params.expert || !m_session.period)
        return;
    const auto [first, last] = trie(*m_session.period);
    if (last <= first)
        return;
    m_session.manualF0.reset();
    m_session.extract = {first, last};
    m_tabs->setCurrentIndex(1);
    m_signal->definit_vue_x(first, last);
    m_spectrum->reinitialise_vue();
}

void MainWindow::joue()
{
    const auto clip = m_session.extrait_en_lecture();
    if (!clip || !m_session.loaded)
        return;
    const audio::Audio &sound = m_session.loaded->audio;
    if (const auto error = m_player.joue(*clip, sound.signal(clip->channel), sound.rate, m_devices->sortie())) {
        const Tr &t = tr();
        affiche_statut(t.playFailed(error->noDevice ? t.noAudioOutput : t.audioFailures[size_t(error->failure)]),
                  icons::Status::Error);
    }
    met_a_jour_lecture();
}

void MainWindow::bascule_lecture()
{
    if (m_player.en_lecture())
        m_player.pause();
    else
        joue();
    met_a_jour_lecture();
}

void MainWindow::choisit_ecoute(Listen listen)
{
    if (listen == m_session.listen)
        return;
    m_session.listen = listen;
    // pendant la lecture, on passe aussitôt à l'autre choix
    if (m_player.en_lecture())
        joue();
    else
        m_player.arrete();
    met_a_jour_lecture();
}

std::optional<double> MainWindow::tete_lecture() const
{
    const Clip *clip = m_player.extrait_en_lecture();
    if (!m_session.loaded || !clip || clip->audio != m_session.loaded->id)
        return std::nullopt;
    const auto position = m_player.position();
    if (!position)
        return std::nullopt;
    return *position / m_session.loaded->audio.rate;
}

void MainWindow::met_a_jour_lecture()
{
    if (const auto error = m_player.met_a_jour()) {
        affiche_statut(tr().playFailed(tr().audioFailures[size_t(*error)]), icons::Status::Error);
    }
    m_signal->place_tete_lecture(tete_lecture());
    rafraichit_transport();
    // la tête de lecture avance
    if (m_player.occupe() && !m_playTimer.isActive())
        m_playTimer.start();
    else if (!m_player.occupe())
        m_playTimer.stop();
}

// ---------------------------------------------------------------- réglages

Settings MainWindow::reglages_actuels() const
{
    Settings s = m_session.params;
    s.uiZoom = m_uiZoomWanted;
    s.windowSize = m_windowSize;
    s.maximized = m_maximized;
    return s;
}

void MainWindow::reglages_modifies()
{
    if (reglages_actuels() != m_saved)
        m_saveTimer.start();
}

void MainWindow::ecrit_reglages()
{
    m_saveTimer.stop();
    const Settings current = reglages_actuels();
    if (current == m_saved)
        return;
    if (!m_files.user.isEmpty()) {
        const QString error = current.enregistre(m_files.user);
        if (!error.isEmpty()) {
            if (!m_settingsErrorShown)
                affiche_statut(tr().settingsFailed(error), icons::Status::Warning);
            m_settingsErrorShown = true;
            return; // le prochain changement ou la fermeture retentera l'écriture
        }
        m_settingsErrorShown = false;
    }
    m_saved = current;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) && width() >= 400
        && height() >= 300) {
        m_previousWindowSize = m_windowSize;
        m_windowSize = size();
    }
    rafraichit_infos_etat();
    if (m_dropOverlay->isVisible())
        m_dropOverlay->setGeometry(rect());
    if (std::clamp(m_uiZoomWanted, UI_ZOOM_MIN, agrandissement_max()) != m_uiZoom)
        applique_agrandissement();
    reglages_modifies();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        m_maximized = isMaximized();
        // Agrandissement signalé après le redimensionnement : la taille notée est celle de l'écran.
        if ((isMaximized() || isFullScreen()) && m_windowSize == size())
            m_windowSize = m_previousWindowSize;
        reglages_modifies();
    } else if (event->type() == QEvent::PaletteChange && m_soundBox) {
        // Thème clair ou sombre : couleurs des textes discrets, une fois la nouvelle palette transmise
        // à tous les widgets.
        QTimer::singleShot(0, this, [this] {
            retraduit();
            rafraichit();
        });
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // le message a changé de largeur : il tient peut-être sur une ligne de plus ou de moins
    if (watched == m_statusLabel && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &MainWindow::ajuste_barre_etat);
    // Part du signal gardée tant que l'utilisateur n'a pas déplacé la séparation : quand la fenêtre
    // change de taille, et au premier affichage de l'analyse (après la page d'accueil). Après la mise
    // en page, qui donne la nouvelle hauteur des volets.
    if (watched == m_vsplit && event->type() == QEvent::Resize && !m_splitMoved)
        QTimer::singleShot(0, this, &MainWindow::partage_hauteur_signal);
    // Nouvelle mise en page du panneau (police agrandie, mode, textes) : sa largeur minimale n'est
    // connue qu'à ce moment, après les événements qui suivent le changement.
    if (m_side && watched == m_side->widget() && event->type() == QEvent::LayoutRequest)
        ajuste_panneau_lateral();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirme_remplacement()) {
        event->ignore();
        return;
    }
    // un chargement en cours (ffmpeg peut-être bloqué, long fichier sur une clé USB) est interrompu ;
    // le destructeur l'attend, fenêtre déjà fermée
    m_cancelLoad = true;
    m_player.arrete();
    ecrit_reglages();
    event->accept();
}
