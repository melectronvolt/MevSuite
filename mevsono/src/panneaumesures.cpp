#include "panneaumesures.h"

#include "format.h"
#include "libelles.h"
#include "session.h"
#include "style.h"

#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

void demi_gras(QLabel *label)
{
    // La taille reste héritée de la fenêtre lorsque l'utilisateur agrandit les textes.
    QFont police;
    police.setWeight(TITLE_WEIGHT);
    label->setFont(police);
}

QLabel *nom_curseur(const QString &nom, QWidget *parent)
{
    auto *label = new QLabel(nom, parent);
    demi_gras(label);
    return label;
}

} // namespace

PanneauMesures::PanneauMesures(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    construit_periode();
    construit_frequences();
    layout->addWidget(m_periodSection);
    layout->addWidget(m_frequencySection);
}

void PanneauMesures::construit_periode()
{
    m_periodSection = new QWidget(this);
    auto *periodSection = new QVBoxLayout(m_periodSection);
    periodSection->setContentsMargins(0, 0, 0, 0);
    m_periodHeading = etiquette_a_la_ligne(m_periodSection);
    demi_gras(m_periodHeading);
    periodSection->addWidget(m_periodHeading);
    m_periodBox = new QWidget(m_periodSection);
    auto *period = new QGridLayout(m_periodBox);
    period->setContentsMargins(0, 0, 0, 0);
    period->setHorizontalSpacing(12);
    m_t1Value = new QLabel(m_periodBox);
    m_t2Value = new QLabel(m_periodBox);
    m_deltaValue = new QLabel(m_periodBox);
    m_t1Name = nom_curseur(u"t₁"_s, m_periodBox);
    m_t2Name = nom_curseur(u"t₂"_s, m_periodBox);
    period->addWidget(m_t1Name, 0, 0);
    period->addWidget(m_t1Value, 0, 1);
    period->addWidget(m_t2Name, 1, 0);
    period->addWidget(m_t2Value, 1, 1);
    period->addWidget(new QLabel(u"Δt = t₂ − t₁"_s, m_periodBox), 2, 0);
    period->addWidget(m_deltaValue, 2, 1);
    period->setColumnStretch(1, 1);
    periodSection->addWidget(m_periodBox);
    m_analyseIntervalle = new QPushButton(m_periodSection);
    periodSection->addWidget(m_analyseIntervalle);
    connect(m_analyseIntervalle, &QPushButton::clicked, this, &PanneauMesures::intervalle_demande);
    m_periodHint = etiquette_discrete(m_periodSection);
    periodSection->addWidget(m_periodHint);
    m_snapCheck = new QCheckBox(m_periodSection);
    m_snapCheck->setObjectName(u"snap"_s);
    periodSection->addWidget(m_snapCheck);
    m_autocorrPeriode = etiquette_a_la_ligne(m_periodSection);
    periodSection->addWidget(m_autocorrPeriode);
    connect(m_snapCheck, &QCheckBox::clicked, this, &PanneauMesures::aimantation_changee);
}

void PanneauMesures::construit_frequences()
{
    m_frequencySection = new QWidget(this);
    auto *frequencies = new QVBoxLayout(m_frequencySection);
    frequencies->setContentsMargins(0, 0, 0, 0);
    m_extractHeading = etiquette_a_la_ligne(m_frequencySection);
    demi_gras(m_extractHeading);
    frequencies->addWidget(m_extractHeading);
    auto *extract = new QGridLayout;
    extract->setHorizontalSpacing(12);
    m_aValue = new QLabel(m_frequencySection);
    m_bValue = new QLabel(m_frequencySection);
    m_aName = nom_curseur(u"A"_s, m_frequencySection);
    m_bName = nom_curseur(u"B"_s, m_frequencySection);
    extract->addWidget(m_aName, 0, 0);
    extract->addWidget(m_aValue, 0, 1);
    extract->addWidget(m_bName, 1, 0);
    extract->addWidget(m_bValue, 1, 1);
    extract->setColumnStretch(1, 1);
    frequencies->addLayout(extract);
    m_extractInfo = etiquette_a_la_ligne(m_frequencySection);
    frequencies->addWidget(m_extractInfo);
    m_qualite = etiquette_a_la_ligne(m_frequencySection);
    frequencies->addWidget(m_qualite);
    // mode expert : la période trouvée par le logiciel dans l'extrait, en une ligne (l'explication dans
    // la bulle)
    m_autocorrValue = etiquette_a_la_ligne(m_frequencySection);
    frequencies->addWidget(m_autocorrValue);
    m_computingBox = new QWidget(m_frequencySection);
    auto *computing = new QHBoxLayout(m_computingBox);
    computing->setContentsMargins(0, 0, 0, 0);
    auto *spinner = new QProgressBar(m_computingBox);
    spinner->setRange(0, 0);
    spinner->setTextVisible(false);
    spinner->setMaximumWidth(40);
    m_computing = new QLabel(m_computingBox);
    computing->addWidget(spinner);
    computing->addWidget(m_computing, 1);
    frequencies->addWidget(m_computingBox);
    m_tooShort = etiquette_discrete(m_frequencySection);
    frequencies->addWidget(m_tooShort);
    m_reticleHint = etiquette_discrete(m_frequencySection);
    frequencies->addSpacing(4);
    frequencies->addWidget(m_reticleHint);

    construit_resultats_experts();
    frequencies->addWidget(m_expertBox);
}

void PanneauMesures::construit_resultats_experts()
{
    m_expertBox = new QWidget(m_frequencySection);
    auto *expert = new QVBoxLayout(m_expertBox);
    expert->setContentsMargins(0, 0, 0, 0);
    auto *resolution = new QGridLayout;
    resolution->setHorizontalSpacing(12);
    m_dfLabel = new QLabel(m_expertBox);
    m_dfValue = new QLabel(m_expertBox);
    m_resolutionLabel = new QLabel(m_expertBox);
    m_resolutionValue = new QLabel(m_expertBox);
    resolution->addWidget(m_dfLabel, 0, 0);
    resolution->addWidget(m_dfValue, 0, 1);
    resolution->addWidget(m_resolutionLabel, 1, 0);
    resolution->addWidget(m_resolutionValue, 1, 1);
    resolution->setColumnStretch(1, 1);
    expert->addLayout(resolution);
    expert->addSpacing(6);
    expert->addWidget(separateur(m_expertBox));
    m_fundamentalHeading = etiquette_a_la_ligne(m_expertBox);
    demi_gras(m_fundamentalHeading);
    expert->addWidget(m_fundamentalHeading);
    // f₁, et sur la même ligne, trouvée automatiquement ou choisie à la main
    auto *f0Line = new QHBoxLayout;
    m_f0Value = new QLabel(m_expertBox);
    m_f0Auto = new QLabel(m_expertBox);
    m_f0Manual = new QLabel(m_expertBox);
    f0Line->addWidget(m_f0Value);
    f0Line->addWidget(m_f0Auto);
    f0Line->addWidget(m_f0Manual);
    f0Line->addStretch();
    expert->addLayout(f0Line);
    m_f0ManualBox = new QWidget(m_expertBox);
    auto *manual = new QHBoxLayout(m_f0ManualBox);
    manual->setContentsMargins(0, 0, 0, 0);
    m_autoButton = new QPushButton(m_f0ManualBox);
    manual->addWidget(m_autoButton);
    manual->addStretch();
    expert->addWidget(m_f0ManualBox);
    m_f0None = etiquette_discrete(m_expertBox);
    expert->addWidget(m_f0None);
    // Tout le panneau défile : le tableau prend la hauteur de ses lignes, sans défilement propre.
    m_harmonicsTable = new QTableWidget(0, 5, m_expertBox);
    m_harmonicsTable->setObjectName(u"harmonics"_s);
    m_harmonicsTable->verticalHeader()->hide();
    m_harmonicsTable->setAlternatingRowColors(true);
    m_harmonicsTable->setShowGrid(false);
    m_harmonicsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_harmonicsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_harmonicsTable->setFocusPolicy(Qt::NoFocus);
    m_harmonicsTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_harmonicsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_harmonicsTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // colonnes réglables à la main ; la dernière prend la place qui reste
    m_harmonicsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_harmonicsTable->horizontalHeader()->setStretchLastSection(true);
    m_harmonicsTable->horizontalHeader()->setHighlightSections(false);
    expert->addWidget(m_harmonicsTable);
    m_copyTable = new QPushButton(m_expertBox);
    expert->addWidget(m_copyTable, 0, Qt::AlignLeft);
    m_f0NoPeak = etiquette_discrete(m_expertBox);
    expert->addWidget(m_f0NoPeak);
    connect(m_autoButton, &QPushButton::clicked, this, &PanneauMesures::detection_automatique_demandee);
    connect(m_copyTable, &QPushButton::clicked, this, &PanneauMesures::copie_demandee);
}

void PanneauMesures::retraduit(const Tr &t)
{
    m_analyseIntervalle->setText(t.analyseInterval);
    m_periodHeading->setText(t.periodHeading);
    m_periodHint->setText(t.periodHint);
    m_snapCheck->setText(t.snapZero);
    m_snapCheck->setToolTip(t.snapZeroTip);
    m_autocorrValue->setToolTip(t.autocorrelationTip);
    m_autocorrPeriode->setToolTip(t.autocorrelationTip);
    m_extractHeading->setText(t.extractHeading);
    m_computing->setText(t.computing);
    m_tooShort->setText(t.extractTooShort);
    m_reticleHint->setText(t.reticleHint);
    m_dfLabel->setText(t.dfLabel);
    m_resolutionLabel->setText(t.resolutionLabel);
    m_resolutionLabel->setToolTip(t.resolutionTip);
    m_fundamentalHeading->setText(t.fundamentalHeading);
    m_f0Manual->setText(t.f0Manual);
    m_autoButton->setText(t.autoButton);
    m_f0Auto->setText(t.f0Auto);
    m_f0None->setText(t.f0None);
    for (QLabel *label : {m_f0Value, m_f0Auto, m_f0Manual})
        label->setToolTip(t.f0RightClick);
    m_harmonicsTable->setHorizontalHeaderLabels({t.colRank, t.colFreq, t.colRatio, t.colAmp, t.colRel});
    m_copyTable->setText(t.copyTable);
    m_f0NoPeak->setText(t.f0NoPeak);
    const QPalette pal = QApplication::palette();
    for (QLabel *label : {m_periodHint, m_tooShort, m_reticleHint, m_f0Manual, m_f0Auto, m_f0None, m_f0NoPeak})
        definit_couleur_texte(label, pal.color(QPalette::PlaceholderText));
    definit_couleur_texte(m_t1Name, couleur_curseur_t(pal));
    definit_couleur_texte(m_t2Name, couleur_curseur_t(pal));
    definit_couleur_texte(m_aName, couleur_curseur_a(pal));
    definit_couleur_texte(m_bName, couleur_curseur_b(pal));
}

void PanneauMesures::affiche(const Session &session, const dsp::Analysis *analyse, bool enAttente,
                             const std::optional<dsp::Peak> &pic, const QString &comparaison, const Tr &t)
{
    const bool periode = session.tool == Tool::Period;
    m_periodSection->setVisible(periode);
    m_frequencySection->setVisible(!periode);
    if (!session.loaded)
        return;
    const double rate = session.loaded->audio.rate;
    affiche_autocorrelation(analyse, session.params.expert, t);
    if (periode) {
        affiche_curseurs(session, rate, t);
        return;
    }
    affiche_extrait(session, rate, analyse, enAttente, pic, comparaison, t);
    m_expertBox->setVisible(session.params.expert && analyse);
    if (session.params.expert && analyse)
        affiche_resultats_experts(session, *analyse, enAttente, t);
}

void PanneauMesures::affiche_curseurs(const Session &session, double rate, const Tr &t)
{
    const QChar d = t.decimal;
    const bool expert = session.params.expert;
    if (const auto &c = session.period) {
        m_t1Value->setText(texte_position((*c)[0], rate, d));
        m_t2Value->setText(texte_position((*c)[1], rate, d));
        m_deltaValue->setText(texte_temps((*c)[1] - (*c)[0], d));
    }
    m_periodBox->setVisible(session.period.has_value());
    m_analyseIntervalle->setVisible(expert && session.period && (*session.period)[0] != (*session.period)[1]);
    // la consigne est pour l'élève (mode simplifié) ; l'expert la retrouve dans la bulle de l'onglet
    m_periodHint->setVisible(!session.period && !expert);
    m_snapCheck->setVisible(expert);
    m_snapCheck->setChecked(session.params.snapZero);
}

void PanneauMesures::affiche_autocorrelation(const dsp::Analysis *analyse, bool expert, const Tr &t)
{
    // Les deux onglets indiquent la période du même extrait entre A et B.
    QString texte;
    QPalette::ColorRole couleur = QPalette::PlaceholderText;
    if (expert && analyse) {
        if (const auto &periode = analyse->period) {
            const auto inseparable = [](QString s) { return s.replace(u' ', QChar(0x00a0)); };
            texte = u"%1 T\u00a0≈\u00a0%2 · f\u00a0≈\u00a0%3"_s.arg(
                t.autocorrelation, inseparable(texte_temps(*periode, t.decimal)),
                inseparable(texte_frequence(1.0 / *periode, t.decimal)));
            couleur = QPalette::WindowText;
        } else {
            texte = u"%1 %2"_s.arg(t.autocorrelation, t.noPeriod);
        }
    }
    for (QLabel *label : {m_autocorrPeriode, m_autocorrValue}) {
        label->setVisible(!texte.isEmpty());
        label->setText(texte);
        definit_couleur_texte(label, palette().color(couleur));
    }
}

void PanneauMesures::affiche_extrait(const Session &session, double rate, const dsp::Analysis *analyse, bool enAttente,
                                    const std::optional<dsp::Peak> &pic, const QString &comparaison, const Tr &t)
{
    const QChar d = t.decimal;
    const bool expert = session.params.expert;
    const auto range = session.intervalle_extrait();
    m_aValue->setText(texte_position(session.extract[0], rate, d));
    m_bValue->setText(texte_position(session.extract[1], rate, d));
    const size_t count = range ? range->second - range->first : 0;
    QStringList constats;
    if (analyse && !enAttente) {
        if (analyse->plateaux)
            constats << t.clippingWarning;
        if (!expert)
            constats << t.resolutionAdvice.arg(texte_frequence(analyse->spectrum.resolution(), d));
        if (!comparaison.isEmpty())
            constats << comparaison;
    }
    m_qualite->setText(constats.join(u'\n'));
    m_qualite->setVisible(!constats.empty());
    m_extractInfo->setText(t.extractInfo(texte_temps(double(count) / rate, d), count));
    m_computingBox->setVisible(!analyse && enAttente);
    m_tooShort->setVisible(!analyse && !enAttente);
    // le mode expert l'analyse dans la ligne d'aide du spectre
    if (analyse && pic) {
        const QString amplitude = session.reglages_actifs().db
            ? texte_decimal(en_decibels(pic->amplitude), 1, d) + u" dBFS"_s : texte_nombre(pic->amplitude, 4, d);
        m_reticleHint->setText(u"%1 · %2 · A = %3"_s.arg(t.selectedPeak, texte_frequence(pic->freq, d), amplitude));
    } else {
        m_reticleHint->setText(t.reticleHint);
    }
    definit_couleur_texte(m_reticleHint, palette().color(pic ? QPalette::WindowText : QPalette::PlaceholderText));
    m_reticleHint->setVisible(analyse && (pic || !expert));
}

void PanneauMesures::affiche_resultats_experts(const Session &session, const dsp::Analysis &analyse,
                                              bool enAttente, const Tr &t)
{
    const QChar decimale = t.decimal;
    const dsp::Spectrum &spectre = analyse.spectrum;
    const size_t indiceFenetre = size_t(std::ranges::find(dsp::ALL_WINDOWS, session.params.window) - dsp::ALL_WINDOWS.begin());
    // détail de la FFT dans les bulles de l'écart entre raies
    QString descriptionFFT = t.fftInfo(spectre.fftLen, t.windowNames[indiceFenetre]);
    if (spectre.segments > 1)
        descriptionFFT += u"\n"_s + t.segmentsInfo(spectre.segments, spectre.segmentLen);
    for (QLabel *label : {m_dfLabel, m_dfValue})
        label->setToolTip(u"%1\n\n%2"_s.arg(descriptionFFT, t.dfTip));
    m_dfValue->setText(texte_frequence(spectre.pas_frequence(), decimale));
    m_resolutionValue->setText(texte_frequence(spectre.resolution(), decimale));

    const auto fondamentale = session.fondamentale(&analyse);
    m_f0Value->setVisible(fondamentale.has_value());
    if (fondamentale)
        m_f0Value->setText(u"f₁ = %1"_s.arg(texte_frequence(*fondamentale, decimale)));
    m_f0Manual->setVisible(fondamentale && session.manualF0);
    m_f0ManualBox->setVisible(fondamentale && session.manualF0);
    m_f0Auto->setVisible(fondamentale && !session.manualF0);
    m_f0None->setVisible(!fondamentale);
    const std::vector<dsp::Harmonic> harmoniques = session.harmoniques(&analyse);
    // le tableau dès qu'un rang a un pic : sans pic à la fondamentale (fondamentale absente), un tiret
    // au rang 1 et les autres rangs mesurés
    const bool tableauVisible = std::ranges::any_of(harmoniques, [](const dsp::Harmonic &harmonique) {
        return harmonique.peak.has_value();
    });
    m_harmonicsTable->setVisible(tableauVisible);
    m_copyTable->setVisible(tableauVisible);
    m_copyTable->setEnabled(!enAttente);
    m_f0NoPeak->setVisible(fondamentale && (harmoniques.empty() || !harmoniques.front().peak));
    if (tableauVisible)
        remplit_tableau(harmoniques, decimale);
}

void PanneauMesures::remplit_tableau(const std::vector<dsp::Harmonic> &harmoniques, QChar decimale)
{
    // rapports à la fondamentale : son pic, ou sa fréquence attendue s'il n'y en a pas
    const std::optional<dsp::Peak> &picFondamental = harmoniques.front().peak;
    const double f1 = picFondamental ? picFondamental->freq : harmoniques.front().expected;
    const int lignes = int(harmoniques.size());
    m_harmonicsTable->setRowCount(lignes);
    const QColor couleurSecondaire = palette().color(QPalette::PlaceholderText);
    const auto remplit_case = [&](int ligne, int colonne, const QString &texte, std::optional<QColor> couleur = std::nullopt) {
        QTableWidgetItem *item = m_harmonicsTable->item(ligne, colonne);
        if (!item) {
            item = new QTableWidgetItem;
            m_harmonicsTable->setItem(ligne, colonne, item);
        }
        item->setText(texte);
        item->setTextAlignment(colonne == 0 ? Qt::AlignCenter : Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(couleur ? QBrush(*couleur) : QBrush());
    };
    for (int ligne = 0; ligne < lignes; ++ligne) {
        const dsp::Harmonic &harmonique = harmoniques[size_t(ligne)];
        remplit_case(ligne, 0, QString::number(harmonique.rank), couleur_harmonique(palette()));
        if (const auto &pic = harmonique.peak) {
            remplit_case(ligne, 1, texte_decimal(pic->freq, 1, decimale));
            remplit_case(ligne, 2, texte_decimal(pic->freq / f1, 3, decimale));
            remplit_case(ligne, 3, texte_decimal(en_decibels(pic->amplitude), 1, decimale));
            if (picFondamental) {
                const double pourcentage = 100.0 * pic->amplitude / picFondamental->amplitude;
                remplit_case(ligne, 4, texte_decimal(pourcentage, 1, decimale) + u" %"_s);
            } else {
                remplit_case(ligne, 4, u"—"_s, couleurSecondaire);
            }
        } else {
            for (int colonne = 1; colonne < 5; ++colonne)
                remplit_case(ligne, colonne, u"—"_s, couleurSecondaire);
        }
    }
    m_harmonicsTable->resizeRowsToContents();
    // Colonnes à la largeur de leur contenu (la dernière prend la place qui reste), et le tableau assez
    // large pour toutes : le panneau s'élargit d'autant, au lieu de cacher les dernières colonnes.
    QHeaderView *entete = m_harmonicsTable->horizontalHeader();
    entete->setStretchLastSection(false);
    m_harmonicsTable->resizeColumnsToContents();
    int largeur = 2 * m_harmonicsTable->frameWidth();
    for (int colonne = 0; colonne < entete->count(); ++colonne)
        largeur += entete->sectionSize(colonne);
    entete->setStretchLastSection(true);
    m_harmonicsTable->setMinimumWidth(largeur);
    m_harmonicsTable->setFixedHeight(m_harmonicsTable->horizontalHeader()->height()
                                     + m_harmonicsTable->verticalHeader()->length()
                                     + 2 * m_harmonicsTable->frameWidth());
}
