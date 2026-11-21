// Mise à jour de l'interface à partir de la séance : textes, outils actifs et barre d'état.
#include "app.h"

#include "about.h"
#include "canvas.h"
#include "plot.h"
#include "style.h"
#include "tableau_mesures.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int HAUTEUR_GRAPHIQUE = 260;

void definit_couleur_texte(QWidget *widget, const QColor &color)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText, color);
    // Dans la barre d'outils, le texte d'une étiquette prend la couleur des boutons.
    palette.setColor(QPalette::ButtonText, color);
    widget->setPalette(palette);
}

/// Couleur pour une feuille de style, transparence comprise (la couleur des textes discrets du thème
/// clair est souvent celle du texte, à demi transparente).
QString couleur_css(const QColor &color)
{
    return u"rgba(%1, %2, %3, %4)"_s.arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

/// Texte secondaire, plus discret.
void attenue(QWidget *widget)
{
    definit_couleur_texte(widget, QApplication::palette().color(QPalette::PlaceholderText));
}

} // namespace

// ---------------------------------------------------------------- textes et état

void FenetrePrincipale::retraduit()
{
    const Textes &t = tr();
    m_menuFile->setTitle(t.menuFile);
    m_sauver->setText(t.saveSession);
    m_sauverSous->setText(t.saveSessionAs);
    m_annoter->setText(tr().annotatedImage);
    m_actOpen->setText(t.openImage);
    // le bouton de la barre d'outils garde les points de suspension, comme l'accueil et le menu
    m_actOpen->setIconText(t.openImage);
    m_actQuit->setText(t.menuQuit);
    // Le soleil ou la lune et le drapeau montrent le thème et la langue affichés.
    const bool dark = est_sombre(QApplication::palette());
    m_actTheme->setIcon(icons::theme(dark));
    m_actTheme->setText(dark ? t.themeToLight : t.themeToDark);
    m_actLanguage->setIcon(icons::drapeau(m_lang));
    m_actLanguage->setText(t.switchLanguage);
    m_infoZoom->setToolTip(t.zoomTip);
    m_menuHelp->setTitle(t.menuHelp);
    m_actManual->setText(t.menuManual);
    m_actAbout->setText(t.menuAbout);
    m_actAboutQt->setText(t.menuAboutQt);
    m_actionEtalonner->setText(t.toolCalibrate);
    m_actionMesurer->setText(t.toolMeasure);
    m_actionProfil->setText(t.toolProfile);
    const QColor trait = m_toolbar->palette().color(QPalette::ButtonText);
    m_agrandirImage->setIcon(icons::loupe(true, trait));
    m_agrandirImage->setText(t.zoomInImage);
    m_reduireImage->setIcon(icons::loupe(false, trait));
    m_reduireImage->setText(t.zoomOutImage);
    m_ajusterImage->setText(t.fitWindow);
    for (size_t i = 0; i < SENS_FLECHES.size(); ++i) {
        m_actionsDeplacement[i]->setIcon(icons::fleche(SENS_FLECHES[i], trait));
        m_actionsDeplacement[i]->setText(t.panImage[i]);
    }

    m_welcomeTagline->setText(t.tagline);
    m_welcomeOpen->setText(t.openImage);
    m_welcomeDropHint->setText(t.welcomeDropHint);
    dimensionne_bouton_accueil();
    m_voileDepot->setText(t.dropHere);

    m_calibHeading->setText(t.calibHeading);
    m_calibStep1->setText(t.calibStep1);
    m_calibStep2->setText(t.calibStep2);
    m_realLength->setText(t.realLength);
    m_champLongueur->setPlaceholderText(t.example);
    m_invalidLength->setText(t.invalidLength);
    definit_couleur_texte(m_invalidLength, couleur_erreur(palette()));
    m_validate->setText(t.validate);
    m_alerteEtalonnage->setText(t.calibChanged);
    definit_couleur_texte(m_alerteEtalonnage, couleur_attention(palette()));

    m_measuresHeading->setText(t.measuresHeading);
    m_uncalibrated->setText(t.uncalibratedPx);
    definit_couleur_texte(m_uncalibrated, couleur_attention(palette()));
    m_measureHint->setText(t.measureHint);
    m_noMeasure->setText(t.noMeasure);
    m_copyMeasures->setText(t.copyMeasures);
    m_clearAll->setText(t.clearAll);
    rafraichit_tableau_mesures();

    m_profileHeading->setText(t.profileHeading);
    m_profileHint->setText(t.profileHint);
    m_channelLabel->setText(t.channelLabel);
    for (size_t i = 0; i < CANAUX.size(); ++i)
        m_channelCombo->setItemText(static_cast<int>(i), t.nom_canal(CANAUX[i]));
    m_thicknessLabel->setText(t.thickness);
    m_glissiereEpaisseur->setToolTip(t.thicknessTip);
    m_champEpaisseur->setToolTip(t.thicknessTip);
    m_cursorsTitle->setText(t.cursors);
    m_cursorsHint->setText(t.cursorsHint);
    definit_couleur_texte(m_boutonsCurseurs[0], couleur_curseur_a(QApplication::palette()));
    definit_couleur_texte(m_boutonsCurseurs[1], couleur_curseur_b(QApplication::palette()));
    definit_couleur_texte(m_alerteProfil, couleur_attention(palette()));
    m_exportCsv->setText(t.exportCsv);

    // couleurs des textes discrets (le thème a pu changer)
    for (QWidget *label : {m_welcomeTagline, m_welcomeDropHint, m_noMeasure, m_cursorsHint})
        attenue(label);
    // En sombre, le style dessine le bord des champs (longueur, épaisseur, tableau) un peu plus foncé
    // que le fond de la fenêtre : sur le fond du panneau, il ne se voyait plus. Fond de référence plus
    // clair pour ce dessin, dans le panneau seulement (le panneau garde le fond de `SideBackground`).
    QPalette fields;
    if (dark)
        fields.setColor(QPalette::Window, QApplication::palette().color(QPalette::Light));
    m_side->widget()->setPalette(fields);
    style_barre_etat();

    m_canevas->definit_traductions(&t);
    m_graphique->definit_traductions(&t);
}

void FenetrePrincipale::rafraichit()
{
    const Textes &t = tr();
    const Seance &s = m_seance;

    const QString nom = m_cheminSeance.isEmpty() ? (s.image ? s.image->name : t.subtitle) : QFileInfo(m_cheminSeance).fileName();
    setWindowTitle(s.image ? u"%1 — %2[*]"_s.arg(about::NAME, nom) : u"%1 — %2"_s.arg(about::NAME, nom));
    setWindowModified(seance_modifiee());
    const bool loaded = s.image != nullptr;
    if (loaded && m_pages->currentIndex() == 0) {
        m_pages->setCurrentIndex(1);
        // Largeur du panneau latéral, donnée à son premier affichage : avant, caché derrière la page
        // d'accueil, il n'a pas sa taille, et QSplitter répartirait en proportion.
        if (!m_sideSized) {
            m_sideSized = true;
            QTimer::singleShot(0, this, [this] {
                const int side = std::max({LARGEUR_PANNEAU, fontMetrics().averageCharWidth() * 38,
                                           m_side->parentWidget()->minimumSizeHint().width()});
                m_hsplit->setSizes({side, std::max(m_hsplit->width() - side, 1)});
            });
        }
    } else if (!loaded) {
        m_pages->setCurrentIndex(0);
    }
    m_actOpen->setEnabled(m_cheminChargement.isEmpty());
    m_sauver->setEnabled(loaded && m_cheminChargement.isEmpty());
    m_sauverSous->setEnabled(loaded && !occupe());
    m_annoter->setEnabled(loaded && !occupe());
    m_copyMeasures->setEnabled(loaded && !occupe());
    m_welcomeOpen->setEnabled(!occupe());
    for (QAction *action : {m_actionEtalonner, m_actionMesurer, m_actionProfil, m_agrandirImage, m_reduireImage, m_ajusterImage})
        action->setEnabled(loaded);
    for (QAction *action : m_actionsDeplacement)
        action->setEnabled(loaded);
    m_scaleAction->setVisible(loaded);
    m_scaleSeparator->setVisible(loaded);
    // sans image, aucun outil n'est montré enfoncé
    m_actionEtalonner->setChecked(loaded && s.outil == Outil::Etalonner);
    m_actionMesurer->setChecked(loaded && s.outil == Outil::Mesurer);
    m_actionProfil->setChecked(loaded && s.outil == Outil::Profil);
    m_calibratePage->setVisible(s.outil == Outil::Etalonner);
    m_measurePage->setVisible(s.outil == Outil::Mesurer);
    m_profilePage->setVisible(s.outil == Outil::Profil);

    rafraichit_etalonnage();

    // Mesures
    m_uncalibrated->setVisible(!s.etalonnage);

    // Profil
    m_channelCombo->setCurrentIndex(
        static_cast<int>(std::ranges::find(CANAUX, s.canal) - CANAUX.begin()));
    {
        const QSignalBlocker blockSlider(m_glissiereEpaisseur), blockSpin(m_champEpaisseur);
        m_glissiereEpaisseur->setValue(s.largeurBande);
        m_champEpaisseur->setValue(s.largeurBande);
    }
    m_exportCsv->setEnabled(!occupe());
    // Une séance peut contenir une longue coupe même si elle s'ouvre sur les mesures.
    // Le calcul du profil attend son affichage ou son export.
    if (s.outil == Outil::Profil)
        rafraichit_curseurs();

    if (m_graphique->isHidden() == (s.outil == Outil::Profil)) {
        m_graphique->setVisible(s.outil == Outil::Profil);
        if (!m_graphique->isHidden() && !m_plotSized) {
            const int total = m_vsplit->height();
            m_vsplit->setSizes({std::max(total - HAUTEUR_GRAPHIQUE, 1), HAUTEUR_GRAPHIQUE});
            m_plotSized = true;
        }
    }
    m_indicateurActivite->setVisible(occupe());
    rafraichit_infos_etat();
    ajuste_panneau_lateral();
    m_canevas->update();
    m_graphique->update();
}

void FenetrePrincipale::rafraichit_etalonnage()
{
    const Textes &t = tr();
    const QChar d = t.decimal;
    const Seance &s = m_seance;
    if (const auto &c = s.etalonnage) {
        const QString text = t.scale(texte_nombre(c->unites_par_pixel(), 3, d) + u' ' + symbole_unite(c->unite));
        const bool pending = s.etalonnage_en_attente();
        m_scaleLabel->setText(pending ? u"⚠ "_s + text : text);
        m_scaleLabel->setToolTip(pending ? t.calibChanged : QString());
        definit_couleur_texte(m_scaleLabel, pending ? couleur_attention(palette()) : palette().color(QPalette::WindowText));
    } else {
        m_scaleLabel->setText(t.notCalibrated);
        m_scaleLabel->setToolTip(QString());
        definit_couleur_texte(m_scaleLabel, couleur_attention(palette()));
    }

    // Étalonnage
    if (s.segmentEtalon) {
        m_longueurSegment->setText(t.segmentDrawn(texte_nombre(s.segmentEtalon->longueur_px(), 4, d)));
        definit_couleur_texte(m_longueurSegment, palette().color(QPalette::WindowText));
    } else {
        m_longueurSegment->setText(t.noSegment);
        attenue(m_longueurSegment);
    }
    if (m_champLongueur->text() != s.saisieLongueur)
        m_champLongueur->setText(s.saisieLongueur);
    m_unitCombo->setCurrentIndex(static_cast<int>(std::ranges::find(UNITES, s.uniteSaisie) - UNITES.begin()));
    const auto value = lit_nombre(s.saisieLongueur);
    const auto echelleSaisie = s.etalonnage_saisi();
    const bool echelleInvalide = s.segmentEtalon && s.segmentEtalon->est_mesurable() && !echelleSaisie;
    m_invalidLength->setVisible(!s.saisieLongueur.trimmed().isEmpty() && (!value || *value <= 0.0 || echelleInvalide));
    m_validate->setEnabled(echelleSaisie.has_value());
    m_currentScale->setVisible(s.etalonnage.has_value());
    if (const auto &c = s.etalonnage) {
        m_currentScale->setText(t.currentScale(texte_nombre(c->unites_par_pixel(), 3, d), texte_nombre(1.0 / c->unites_par_pixel(), 3, d),
                                               symbole_unite(c->unite)));
    }
    m_alerteEtalonnage->setVisible(s.etalonnage_en_attente());
}

void FenetrePrincipale::trace_modifie(CibleTrace cible)
{
    setWindowModified(seance_modifiee());
    switch (cible.nature) {
    case CibleTrace::Etalon:
        rafraichit_etalonnage();
        break;
    case CibleTrace::Mesure:
        m_tableauMesures->actualise_longueur(cible.indiceMesure);
        break;
    case CibleTrace::Coupe:
        rafraichit_curseurs();
        m_graphique->update();
        break;
    }
}

void FenetrePrincipale::rafraichit_infos_etat()
{
    const Textes &t = tr();
    const ImageChargee *image = m_seance.image.get();
    m_infoImage->setVisible(image != nullptr);
    m_infoZoom->setVisible(image != nullptr && m_zoom > 0.0);
    if (!image)
        return;
    // nom de l'image (raccourci au milieu s'il est long) et ses dimensions
    QString full = u"%1 · %2 × %3 px"_s.arg(image->name, QString::number(image->rgb.width()),
                                                 QString::number(image->rgb.height()));
    if (image->bitsSource > 8)
        full += u" · %1 → 8 bits"_s.arg(image->bitsSource);
    m_infoImage->setText(
        m_infoImage->fontMetrics().elidedText(full, Qt::ElideMiddle, std::max(width() / 3, 120)));
    const QString detail = image->bitsSource > 8 ? full + u'\n' + t.depthConverted : full;
    m_infoImage->setToolTip(Qt::convertFromPlainText(detail));
    const double pct = m_zoom * 100.0;
    m_infoZoom->setText(t.zoomInfo(pct >= 10.0 ? QString::number(std::lround(pct)) : texte_decimal(pct, 1, t.decimal)));
}

void FenetrePrincipale::ajuste_panneau_lateral()
{
    // Contenu plus large (outil, langue, agrandissement) : le panneau s'élargit d'autant.
    const int width = m_side->minimumSizeHint().width();
    if (width != m_sideMinWidth) {
        m_sideMinWidth = width;
        m_side->updateGeometry();
    }
}

void FenetrePrincipale::affiche_statut(const QString &text, icons::Status kind)
{
    m_statut = {text, kind};
    m_messageEtat->setText(text);
    m_messageEtat->setToolTip({});
    ajuste_barre_etat();
    // pictogramme de la hauteur d'une ligne
    const int side = m_messageEtat->fontMetrics().height();
    const QSize taille(side, side);
    const qreal dpr = devicePixelRatioF();
    QPixmap &picto = m_pictosStatut[static_cast<size_t>(kind)];
    if (picto.size() != taille * dpr || picto.devicePixelRatio() != dpr)
        picto = icons::etat(kind).pixmap(taille, dpr);
    m_iconeEtat->setPixmap(picto);
    m_iconeEtat->setVisible(!text.isEmpty());
    const QPalette palette = QApplication::palette();
    QColor color = palette.color(QPalette::WindowText);
    switch (kind) {
    case icons::Status::Info: break;
    case icons::Status::Success: color = couleur_reussite(palette); break;
    case icons::Status::Warning: color = couleur_attention(palette); break;
    case icons::Status::Error: color = couleur_erreur(palette); break;
    }
    definit_couleur_texte(m_messageEtat, color);
}

void FenetrePrincipale::ajuste_barre_etat()
{
    // Hauteur donnée à la barre : celle que QLabel estime coupe le texte dès une vingtaine de
    // caractères (et la barre changeait de hauteur d'un message à l'autre). Ici, une ligne si le
    // message tient dans la largeur qu'il occupe, plusieurs sinon.
    const int line = std::max(m_messageEtat->heightForWidth(QWIDGETSIZE_MAX), m_infoZoom->sizeHint().height());
    statusBar()->setFixedHeight(std::max(line, m_messageEtat->heightForWidth(m_messageEtat->width())) + 6);
}

void FenetrePrincipale::style_barre_etat()
{
    // Bande distincte du reste de la fenêtre : fond un peu plus foncé, filet au-dessus ; image et
    // zoom séparés du message par un filet vertical. L'image, plus discrète : couleur donnée ici, la
    // feuille de style l'emportant sur la palette des étiquettes qu'elle décrit.
    const QPalette palette = QApplication::palette();
    const QColor background = palette.color(QPalette::Window).darker(est_sombre(palette) ? 135 : 107);
    statusBar()->setStyleSheet(u"QStatusBar { background: %1; border-top: 1px solid %2; }"
                               u"QStatusBar::item { border: none; }"
                               u"QLabel#imageStatus { color: %3; }"_s.arg(
                                   couleur_css(background), couleur_css(palette.color(QPalette::Mid)),
                                   couleur_css(palette.color(QPalette::PlaceholderText))));
    affiche_statut(m_statut);
}
