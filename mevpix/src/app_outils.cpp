// Commandes des trois outils et résultats affichés dans le panneau à gauche de la photo.
#include "app.h"

#include "canvas.h"
#include "plot.h"
#include "style.h"
#include "tableau_mesures.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

/// Titres du panneau latéral : taille de la police de l'interface (en demi-gras, `TITLE_WEIGHT`).
constexpr double HEADING_SIZE = 1.0;

QLabel *enveloppe(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    return label;
}

QFrame *separateur(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

/// Près d'un sommet de frange, A et B doivent rester distinguables même si leur écart
/// est beaucoup plus petit que leur distance à l'origine de la coupe.
QString position_curseur(double position, double ecart, QChar decimale)
{
    if (position == 0.0)
        return texte_nombre(0.0, 3, decimale);
    if (notation_scientifique(position)) {
        int chiffres = 3;
        if (ecart > 0.0) {
            const int ordres = int(std::floor(std::log10(position))) - int(std::floor(std::log10(ecart)));
            chiffres = std::clamp(3 + ordres, 3, std::numeric_limits<double>::max_digits10);
        }
        return texte_nombre(position, chiffres, decimale);
    }
    const int decimales = std::max(decimales_significatives(ecart, 3), decimales_significatives(position, 3));
    const int ordre = int(std::floor(std::log10(position)));
    const int decimalesUtiles = std::numeric_limits<double>::max_digits10 - 1 - ordre;
    return texte_decimal(position, std::min(decimales, decimalesUtiles), decimale);
}

} // namespace

QWidget *FenetrePrincipale::construit_panneau_lateral()
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    m_calibratePage = construit_page_etalonnage();
    m_measurePage = construit_page_mesures();
    m_profilePage = construit_page_profil();
    layout->addWidget(m_calibratePage);
    layout->addWidget(m_measurePage);
    layout->addWidget(m_profilePage);
    layout->addStretch();
    return panel;
}

QWidget *FenetrePrincipale::construit_page_etalonnage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_calibHeading = enveloppe(page);
    police_du_style(m_calibHeading, HEADING_SIZE, TITLE_WEIGHT);
    m_calibStep1 = enveloppe(page);
    m_calibStep2 = enveloppe(page);
    m_longueurSegment = enveloppe(page);
    layout->addWidget(m_calibHeading);
    layout->addWidget(m_calibStep1);
    layout->addWidget(m_calibStep2);
    layout->addSpacing(8);
    layout->addWidget(m_longueurSegment);

    auto *row = new QHBoxLayout;
    m_realLength = new QLabel(page);
    m_champLongueur = new QLineEdit(page);
    m_champLongueur->setObjectName(u"calibLength"_s);
    m_champLongueur->setMaximumWidth(90);
    m_unitCombo = new QComboBox(page);
    for (Unite unite : UNITES)
        m_unitCombo->addItem(symbole_unite(unite));
    row->addWidget(m_realLength);
    row->addWidget(m_champLongueur);
    row->addWidget(m_unitCombo);
    row->addStretch();
    layout->addLayout(row);

    m_invalidLength = enveloppe(page);
    m_validate = new QPushButton(page);
    m_validate->setObjectName(u"validate"_s);
    m_currentScale = enveloppe(page);
    m_alerteEtalonnage = enveloppe(page);
    layout->addWidget(m_invalidLength);
    layout->addWidget(m_validate);
    layout->addSpacing(8);
    layout->addWidget(m_currentScale);
    layout->addWidget(m_alerteEtalonnage);

    connect(m_champLongueur, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!m_avantSaisie)
            m_avantSaisie = m_seance;
        m_seance.saisieLongueur = text;
        rafraichit();
    });
    connect(m_champLongueur, &QLineEdit::editingFinished, this, &FenetrePrincipale::termine_saisie);
    // Touche Entrée dans le champ : valide comme le bouton.
    connect(m_champLongueur, &QLineEdit::returnPressed, this, &FenetrePrincipale::valide_etalonnage);
    connect(m_unitCombo, &QComboBox::activated, this, [this](int index) {
        termine_saisie();
        m_seance.modifie([&] { m_seance.uniteSaisie = UNITES[index]; });
        rafraichit();
        reglages_modifies();
    });
    connect(m_validate, &QPushButton::clicked, this, &FenetrePrincipale::valide_etalonnage);
    return page;
}

QWidget *FenetrePrincipale::construit_page_mesures()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_measuresHeading = enveloppe(page);
    police_du_style(m_measuresHeading, HEADING_SIZE, TITLE_WEIGHT);
    m_uncalibrated = enveloppe(page);
    m_measureHint = enveloppe(page);
    layout->addWidget(m_measuresHeading);
    layout->addWidget(m_uncalibrated);
    layout->addWidget(m_measureHint);
    layout->addSpacing(8);

    m_tableauMesures = new TableauMesures(page);
    connect(m_tableauMesures, &TableauMesures::suppression_demandee, this, [this](int ligne) {
        if (ligne < 0 || ligne >= m_seance.mesures.size())
            return;
        m_seance.modifie([&] { m_seance.mesures.removeAt(ligne); });
        rafraichit_tableau_mesures();
        rafraichit();
    });
    layout->addWidget(m_tableauMesures);
    m_noMeasure = enveloppe(page);
    layout->addWidget(m_noMeasure);

    m_measureButtons = new QWidget(page);
    auto *buttons = new QHBoxLayout(m_measureButtons);
    buttons->setContentsMargins(0, 4, 0, 0);
    m_copyMeasures = new QPushButton(m_measureButtons);
    m_clearAll = new QPushButton(m_measureButtons);
    buttons->addWidget(m_copyMeasures);
    buttons->addWidget(m_clearAll);
    buttons->addStretch();
    layout->addWidget(m_measureButtons);

    connect(m_copyMeasures, &QPushButton::clicked, this, &FenetrePrincipale::copie_mesures);
    connect(m_clearAll, &QPushButton::clicked, this, [this] {
        m_seance.modifie([&] { m_seance.mesures.clear(); });
        rafraichit_tableau_mesures();
        rafraichit();
    });
    return page;
}

QWidget *FenetrePrincipale::construit_page_profil()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_profileHeading = enveloppe(page);
    police_du_style(m_profileHeading, HEADING_SIZE, TITLE_WEIGHT);
    m_profileHint = enveloppe(page);
    layout->addWidget(m_profileHeading);
    layout->addWidget(m_profileHint);
    layout->addSpacing(8);

    auto *grid = new QGridLayout;
    m_channelLabel = new QLabel(page);
    m_channelCombo = new QComboBox(page);
    for (size_t i = 0; i < CANAUX.size(); ++i)
        m_channelCombo->addItem(QString());
    m_thicknessLabel = new QLabel(page);
    auto *largeurBande = new QHBoxLayout;
    m_glissiereEpaisseur = new QSlider(Qt::Horizontal, page);
    m_glissiereEpaisseur->setRange(1, 50);
    m_glissiereEpaisseur->setTracking(false);
    m_champEpaisseur = new QSpinBox(page);
    m_champEpaisseur->setRange(1, 50);
    m_champEpaisseur->setKeyboardTracking(false);
    m_champEpaisseur->setSuffix(u" px"_s);
    largeurBande->addWidget(m_glissiereEpaisseur, 1);
    largeurBande->addWidget(m_champEpaisseur);
    grid->addWidget(m_channelLabel, 0, 0);
    grid->addWidget(m_channelCombo, 0, 1);
    grid->addWidget(m_thicknessLabel, 1, 0);
    grid->addLayout(largeurBande, 1, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    m_cursorsBox = new QWidget(page);
    auto *box = new QVBoxLayout(m_cursorsBox);
    box->setContentsMargins(0, 8, 0, 0);
    box->addWidget(separateur(m_cursorsBox));
    m_cursorsTitle = enveloppe(m_cursorsBox);
    police_du_style(m_cursorsTitle, HEADING_SIZE, TITLE_WEIGHT);
    m_cursorsHint = enveloppe(m_cursorsBox);
    box->addWidget(m_cursorsTitle);
    box->addWidget(m_cursorsHint);
    auto *cursors = new QGridLayout;
    for (int i : {0, 1}) {
        // La couleur des lettres A et B suit le thème dans retraduit().
        auto *name = new QToolButton(m_cursorsBox);
        name->setText(i == 0 ? u"A"_s : u"B"_s);
        name->setCheckable(true);
        name->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(name, &QToolButton::clicked, this, [this, i] { m_graphique->selectionne_curseur(i); });
        police_du_style(name, 1.0, TITLE_WEIGHT);
        m_boutonsCurseurs[i] = name;
        m_positionsCurseurs[i] = new QLabel(m_cursorsBox);
        m_positionsCurseurs[i]->setObjectName(i == 0 ? u"positionA"_s : u"positionB"_s);
        m_intensitesCurseurs[i] = new QLabel(m_cursorsBox);
        police_du_style(m_positionsCurseurs[i], 1.0, QFont::Normal, true);
        police_du_style(m_intensitesCurseurs[i], 1.0, QFont::Normal, true);
        cursors->addWidget(name, i, 0);
        cursors->addWidget(m_positionsCurseurs[i], i, 1);
        cursors->addWidget(m_intensitesCurseurs[i], i, 2);
    }
    cursors->setColumnStretch(2, 1);
    box->addLayout(cursors);
    m_deltaX = enveloppe(m_cursorsBox);
    m_deltaX->setObjectName(u"deltaX"_s);
    police_du_style(m_deltaX, 1.0, QFont::Normal, true);
    box->addWidget(m_deltaX);
    m_alerteProfil = enveloppe(m_cursorsBox);
    box->addWidget(m_alerteProfil);
    m_exportCsv = new QPushButton(m_cursorsBox);
    box->addSpacing(8);
    box->addWidget(m_exportCsv, 0, Qt::AlignLeft);
    layout->addWidget(m_cursorsBox);

    connect(m_channelCombo, &QComboBox::activated, this, [this](int index) {
        m_seance.modifie([&] { m_seance.canal = CANAUX[index]; });
        rafraichit();
        reglages_modifies();
    });
    const auto setBand = [this](int value) {
        if (value == m_seance.largeurBande)
            return;
        m_seance.modifie([&] { m_seance.largeurBande = value; });
        rafraichit();
        reglages_modifies();
    };
    connect(m_glissiereEpaisseur, &QSlider::valueChanged, this, setBand);
    connect(m_champEpaisseur, &QSpinBox::valueChanged, this, setBand);
    connect(m_exportCsv, &QPushButton::clicked, this, &FenetrePrincipale::exporte_csv);
    return page;
}

void FenetrePrincipale::rafraichit_tableau_mesures()
{
    m_tableauMesures->affiche(m_seance, tr());
    m_noMeasure->setVisible(m_seance.mesures.isEmpty());
    m_measureButtons->setVisible(!m_seance.mesures.isEmpty());
}

void FenetrePrincipale::rafraichit_curseurs()
{
    const Textes &t = tr();
    const QChar d = t.decimal;
    const Profil *profile = m_seance.profil();
    m_cursorsBox->setVisible(profile != nullptr);
    if (!profile)
        return;
    const auto [unite, unitesParPixel] = m_seance.facteur_unite();
    const double len = profile->longueur_px() * unitesParPixel;
    const double x[2] = {m_seance.fractionsCurseurs[0] * len, m_seance.fractionsCurseurs[1] * len};
    // Deux positions proches doivent rester distinctes. Si A et B coïncident,
    // leur abscisse garde ses propres décimales, par exemple 0,250 mm.
    const double delta = std::abs(m_seance.fractionsCurseurs[1] - m_seance.fractionsCurseurs[0]) * len;
    for (int i : {0, 1}) {
        m_boutonsCurseurs[i]->setChecked(i == m_seance.curseurActif);
        m_positionsCurseurs[i]->setText(u"x = %1 %2"_s.arg(position_curseur(x[i], delta, d), unite));
        m_intensitesCurseurs[i]->setText(u"I = %1"_s.arg(texte_decimal(profile->valeur_en(m_seance.fractionsCurseurs[i]), 0, d)));
    }
    m_deltaX->setText(u"Δx = |xB − xA| = %1 %2"_s.arg(texte_nombre(delta, 3, d), unite));
    QStringList avertissements;
    if (profile->echantillonsSatures > 0) {
        const double pct = 100.0 * profile->echantillonsSatures / double(profile->intensites.size());
        avertissements << t.alerteSaturation(texte_nombre(pct, 2, d));
    }
    if (profile->tronques)
        avertissements << t.clippedBand;
    m_alerteProfil->setText(avertissements.join(u'\n'));
    m_alerteProfil->setVisible(!avertissements.isEmpty());
}

void FenetrePrincipale::choisit_outil(Outil outil)
{
    if (outil != m_seance.outil) {
        termine_saisie();
        m_graphique->termine_clic();
        m_graphique->annule_glisser();
        m_canevas->annule_glisser();
    }
    m_seance.outil = outil;
    rafraichit();
}

// ---------------------------------------------------------------- segments

void FenetrePrincipale::valide(const Segment &seg)
{
    const Textes &t = tr();
    const QChar d = t.decimal;
    termine_saisie();
    Travail avant = m_seance;
    switch (m_seance.outil) {
    case Outil::Etalonner:
        affiche_statut(t.calibDrawn(texte_nombre(seg.longueur_px(), 4, d)));
        m_seance.segmentEtalon = seg;
        break;
    case Outil::Mesurer:
        affiche_statut(t.measureTag(static_cast<int>(m_seance.mesures.size()) + 1,
                               texte_longueur(seg.longueur_px(), m_seance.etalonnage, d)));
        m_seance.mesures.push_back(seg);
        rafraichit_tableau_mesures();
        break;
    case Outil::Profil:
        affiche_statut(t.cutDrawn);
        m_seance.coupe = seg;
        m_seance.fractionsCurseurs = {0.25, 0.75};
        m_graphique->reinitialise_vue();
        break;
    }
    m_seance.memorise(std::move(avant));
    rafraichit();
}

void FenetrePrincipale::termine_saisie()
{
    if (m_avantSaisie) {
        m_seance.memorise(std::move(*m_avantSaisie));
        m_avantSaisie.reset();
    }
}

void FenetrePrincipale::valide_etalonnage()
{
    termine_saisie();
    const auto c = m_seance.etalonnage_saisi();
    if (!c)
        return;
    m_seance.modifie([&] { m_seance.etalonnage = c; });
    rafraichit_tableau_mesures();
    const QString echelle = texte_nombre(c->unites_par_pixel(), 3, tr().decimal) + u' ' + symbole_unite(c->unite);
    affiche_statut(tr().calibValidated(echelle), icons::Status::Success);
    choisit_outil(Outil::Mesurer);
}
