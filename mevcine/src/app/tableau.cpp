#include "tableau.h"

#include "app.h"
#include "dessin.h"
#include "i18n.h"
#include "onglet_pointage.h"

#include <QHeaderView>
#include <QShortcut>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QTimer>
#include <QStyle>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

using namespace Qt::StringLiterals;

TableauMesures::TableauMesures(App &app, QWidget *parent, Presentation presentation, ZoneVideo *zone)
    : QWidget(parent)
    , m_zone(zone)
    , m_app(app)
    , m_presentation(presentation)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_vide = new QLabel(this);
    m_vide->setWordWrap(true);
    m_vide->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_vide->setForegroundRole(QPalette::PlaceholderText);
    m_table = new QTableWidget(this);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setMouseTracking(true);
    m_table->viewport()->installEventFilter(this);
    m_table->installEventFilter(this);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int col) {
        if (m_apercu_clavier && m_table->hasFocus())
            survole_cellule(m_table->model()->index(row, col));
    });
    m_delai = new QTimer(this);
    m_delai->setSingleShot(true);
    for (auto *barre : {m_table->verticalScrollBar(), m_table->horizontalScrollBar()})
        connect(barre, &QScrollBar::valueChanged, this, [this] {
            if (m_apercu_clavier)
                return;
            auto *vue = m_table->viewport();
            survole_cellule(m_table->indexAt(vue->mapFromGlobal(QCursor::pos())));
        });
    connect(m_delai, &QTimer::timeout, this, &TableauMesures::montre_apercu);
    m_supprimer = new QShortcut(QKeySequence::Delete, m_table);
    m_supprimer->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_supprimer, &QShortcut::activated, this, &TableauMesures::supprime_selection);
    m_table->setContextMenuPolicy(Qt::PreventContextMenu);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setResizeContentsPrecision(-1);
    m_table->horizontalHeader()->setStretchLastSection(false);
    l->addWidget(m_vide);
    l->addWidget(m_table, 1);
    m_verifier = new QPushButton(this);
    m_verifier->setIcon(icones::commande(icones::Commande::Examiner));
    m_confirmer = new QPushButton(this);
    m_confirmer->setIcon(icones::commande(icones::Commande::Valider));
    m_annuler = new QPushButton(this);
    m_annuler->setIcon(icones::commande(icones::Commande::Annuler));
    l->addWidget(m_verifier);
    l->addWidget(m_confirmer);
    l->addWidget(m_annuler);
    connect(m_verifier, &QPushButton::clicked, this, [this] {
        efface_apercu();
        m_app.verifie_point_suivant();
        m_app.rafraichit();
    });
    connect(m_confirmer, &QPushButton::clicked, this, [this] {
        if (m_app.refait && m_app.objet_repris && m_app.image_affichee()) {
            m_app.pointage.confirme_suivi(m_app.objet_repris, m_app.index - 1);
            m_app.annule_refait();
            m_app.rafraichit();
        }
    });
    connect(m_annuler, &QPushButton::clicked, this, [this] {
        m_app.retire_mesure_avant();
        m_app.rafraichit();
    });
    if (m_presentation == Presentation::Coordonnees) {
        m_zone = new ZoneVideo(app, m_table->viewport());
        m_zone->setMinimumSize(0, 0);
        m_zone->setObjectName(u"apercuMesure"_s);
        m_zone->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_zone->hide();
    }
}

void TableauMesures::ajoute_ligne(std::vector<Pointage::Cellule> &cellules, int ligne) const
{
    for (int objet = 1; objet <= m_app.pointage.nombre_objets(); ++objet)
        cellules.emplace_back(m_affiche.images.at(ligne), objet);
}

void TableauMesures::supprime_selection()
{
    std::vector<Pointage::Cellule> cellules;
    for (auto *item : m_table->selectedItems()) {
        const int col = item->column();
        if (m_presentation == Presentation::Numeros)
            ajoute_ligne(cellules, item->row());
        else if (col > 0 && col <= 2 * m_app.pointage.nombre_objets())
            cellules.emplace_back(m_affiche.images.at(item->row()), (col + 1) / 2);
    }
    supprime(cellules);
}

void TableauMesures::supprime(const std::vector<Pointage::Cellule> &cellules)
{
    if (m_app.etat != Etat::Pointage || m_app.refait || m_app.choix_origine || cellules.empty())
        return;
    efface_apercu();
    const auto fin = m_app.pointage.image_finale();
    m_app.pointage.supprime_points(cellules);
    // Si on attendait le prochain clic, permettre de poursuivre après la dernière mesure restante.
    if (fin && m_app.index == *fin + 1) {
        if (const auto nouvelle_fin = m_app.pointage.image_finale())
            m_app.change_image(*nouvelle_fin + 1);
    }
    m_table->clearSelection();
    m_app.rafraichit();
}

void TableauMesures::efface_apercu()
{
    m_delai->stop();
    m_cellule = {};
    if (m_zone) {
        m_zone->quitte_survol();
        if (m_presentation == Presentation::Coordonnees)
            m_zone->hide();
    }
}

void TableauMesures::survole_cellule(const QModelIndex &index)
{
    if (m_cellule == index)
        return;
    if (!index.isValid()) {
        efface_apercu();
        return;
    }
    // Conserver le dernier aperçu pendant le délai : le remplacer en une seule peinture.
    m_cellule = index;
    m_delai->start(90);
}

bool TableauMesures::eventFilter(QObject *objet, QEvent *event)
{
    if (objet == m_table && (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
                             event->type() == QEvent::DevicePixelRatioChange)) {
        m_largeurs_a_revoir = true;
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && m_largeurs_a_revoir)
                m_app.rafraichit();
        });
    }
    if (objet == m_table && event->type() == QEvent::ShortcutOverride) {
        const int touche = static_cast<QKeyEvent *>(event)->key();
        if (touche == Qt::Key_Left || touche == Qt::Key_Right || touche == Qt::Key_Up || touche == Qt::Key_Down) {
            m_apercu_clavier = true;
            survole_cellule(m_table->currentIndex());
            event->accept(); // les flèches déplacent la cellule même dans l'onglet Pointage
            return true;
        }
    }
    if (objet == m_table && event->type() == QEvent::FocusOut && m_apercu_clavier) {
        m_apercu_clavier = false;
        efface_apercu();
    }
    if (objet == m_table->viewport()) {
        if (event->type() == QEvent::Resize)
            etire_colonnes();
        else if (event->type() == QEvent::Hide)
            efface_apercu();
        else if (event->type() == QEvent::Leave && !m_apercu_clavier) {
            // Un bouton de ligne ou une infobulle peut recevoir la souris au-dessus du tableau.
            auto *vue = m_table->viewport();
            survole_cellule(m_table->indexAt(vue->mapFromGlobal(QCursor::pos())));
        }
        else if (event->type() == QEvent::MouseMove) {
            m_apercu_clavier = false;
            const auto *souris = static_cast<QMouseEvent *>(event);
            const QModelIndex cellule = m_table->indexAt(souris->position().toPoint());
            survole_cellule(cellule);
            // Qt traiterait le bouton sous la souris comme un éditeur et lui donnerait la sélection.
            if (souris->buttons() == Qt::NoButton && cellule.isValid() && m_table->cellWidget(cellule.row(), cellule.column())) {
                event->accept();
                return true;
            }
        }
    } else if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) {
        if (auto *bouton = qobject_cast<QToolButton *>(objet)) {
            m_apercu_clavier = false;
            survole_cellule(m_table->indexAt(bouton->mapTo(m_table->viewport(), bouton->rect().center())));
        }
    }
    return QWidget::eventFilter(objet, event);
}

void TableauMesures::montre_apercu()
{
    if (!m_zone || !m_cellule.isValid() || size_t(m_cellule.row()) >= m_affiche.images.size())
        return;
    const int col = m_cellule.column();
    const bool coordonnee = m_presentation != Presentation::Numeros && col > 0 && col < m_affiche.en_tetes.size();
    if (!m_zone->survole_mesure(m_affiche.images[m_cellule.row()], coordonnee ? (col + 1) / 2 : 0)) {
        efface_apercu();
        return;
    }
    if (m_presentation == Presentation::Coordonnees) {
        const QSize dispo = m_table->viewport()->size();
        const QSize taille = m_app.dimensions().scaled(std::min(300, dispo.width() / 2), std::min(220, dispo.height()),
                                                       Qt::KeepAspectRatio);
        const int x = m_table->visualRect(m_cellule).center().x() < dispo.width() / 2 ? dispo.width() - taille.width() : 0;
        m_zone->setGeometry(QRect(QPoint(x, 0), taille));
        m_zone->show();
        m_zone->raise();
    }
}

QToolButton *TableauMesures::bouton_action(int ligne, int colonne, bool refaire)
{
    if (auto *bouton = qobject_cast<QToolButton *>(m_table->cellWidget(ligne, colonne)))
        return bouton;

    // Le bouton gère son survol et son focus. La sélection du tableau porte sur les mesures.
    auto *cellule = new QTableWidgetItem;
    cellule->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(ligne, colonne, cellule);
    auto *bouton = new QToolButton(m_table);
    bouton->setAutoRaise(true);
    bouton->setMouseTracking(true);
    bouton->installEventFilter(this);
    if (refaire) {
        bouton->setIcon(icones::onglet(icones::Onglet::Pointage));
        connect(bouton, &QToolButton::clicked, this, [this, ligne] {
            efface_apercu();
            m_app.reprend_mesure(m_affiche.images.at(ligne) + 1);
            m_app.rafraichit();
        });
    } else {
        bouton->setIcon(icones::commande(icones::Commande::Supprimer));
        connect(bouton, &QToolButton::clicked, this, [this, ligne] {
            std::vector<Pointage::Cellule> cellules;
            ajoute_ligne(cellules, ligne);
            supprime(cellules);
        });
    }
    m_table->setCellWidget(ligne, colonne, bouton);
    return bouton;
}

void TableauMesures::actualise()
{
    App &a = m_app;
    const bool compact = m_presentation == Presentation::Numeros;
    const bool etire = m_presentation == Presentation::Coordonnees;
    const Tableau tableau = Tableau::construit(a.pointage, compact);
    const int n = static_cast<int>(tableau.lignes.size());
    const int donnees = static_cast<int>(tableau.en_tetes.size());
    const auto &sources = a.pointage.provenances();
    const bool a_verifier = std::any_of(sources.begin(), sources.end(), [](const auto &p) {
        return p.second == Pointage::Saisie::AVerifier || p.second == Pointage::Saisie::Perdue;
    });
    m_verifier->setText(compact ? trad("À vérifier") : trad("Point suivant à vérifier"));
    m_verifier->setAccessibleName(trad("Point suivant à vérifier"));
    m_verifier->setToolTip(trad("Examiner le prochain point incertain ou perdu"));
    m_verifier->setVisible(a_verifier && a.etat == Etat::Pointage && !a.refait && !a.choix_origine);
    const bool verification = a.refait && a.objet_repris && a.image_affichee();
    m_confirmer->setVisible(verification);
    if (verification) {
        const bool perdu = a.pointage.saisie(a.objet_repris, a.index - 1) == Pointage::Saisie::Perdue;
        const QString action = perdu ? trad("Confirmer l’absence") : trad("Valider le point");
        m_confirmer->setText(compact ? trad("Confirmer") : action);
        m_confirmer->setAccessibleName(action);
        m_confirmer->setToolTip(action);
    }
    m_annuler->setText(compact ? trad("Annuler") : trad("Annuler la suppression"));
    m_annuler->setAccessibleName(trad("Annuler la suppression"));
    m_annuler->setToolTip(trad("Annuler la suppression"));
    m_annuler->setVisible(a.pointage.derniere_suppression() && a.annulation_possible());
    m_vide->setText(trad("Les mesures s'afficheront ici, au fur et à mesure du pointage."));
    m_vide->setVisible(n == 0);
    m_table->setVisible(n > 0);
    const bool refaire_permis = a.etat == Etat::Pointage && !a.choix_origine;
    // l'image dont on refait la mesure
    const std::optional<int> en_cours = a.refait ? std::optional(a.index - 1) : std::nullopt;
    m_supprimer->setEnabled(refaire_permis && !a.refait);
    m_table->setToolTip(compact ? trad("Survolez un numéro pour voir les points de cette image.")
                                : trad("Survolez une coordonnée pour voir le point. Sélectionnez des coordonnées "
                                       "puis appuyez sur Suppr pour les effacer."));
    const Langue langue = i18n::langue();
    const bool reconstruit = tableau != m_affiche || m_langue != langue;
    bool redimensionne = reconstruit || m_largeurs_a_revoir;
    if (reconstruit) {
        efface_apercu();
        m_table->clearSelection();
        // on fait défiler le tableau jusqu'à une nouvelle mesure
        const bool defile = n > 0 && (m_affiche.images.empty() || tableau.images.back() > m_affiche.images.back());
        const int colonnes = donnees + 2;
        if (colonnes != m_table->columnCount())
            m_table->setRowCount(0);
        m_table->setColumnCount(colonnes);
        QStringList en_tetes = tableau.en_tetes;
        en_tetes << trad("Rec") << trad("Sup.");
        m_table->setHorizontalHeaderLabels(en_tetes);
        m_table->horizontalHeaderItem(donnees)->setToolTip(trad("Recommencer la mesure"));
        m_table->horizontalHeaderItem(donnees + 1)->setToolTip(trad("Supprimer"));
        if (compact)
            m_table->horizontalHeaderItem(0)->setToolTip(trad("Image n°"));
        // les titres des colonnes en demi-gras, à la taille de l'interface
        dessin::demi_gras(m_table->horizontalHeader());
        m_table->setRowCount(n);
        for (int i = 0; i < n; ++i) {
            const int image = tableau.images[i];
            for (int j = 0; j < static_cast<int>(tableau.lignes[i].size()); ++j) {
                auto *item = m_table->item(i, j);
                if (!item) {
                    item = new QTableWidgetItem();
                    m_table->setItem(i, j, item);
                }
                item->setText(tableau.lignes[i][j]);
                if (m_presentation != Presentation::Guide)
                    item->setTextAlignment(Qt::AlignCenter);
            }
            for (int col = donnees; col < colonnes; ++col) {
                const bool refaire = col == donnees;
                auto *bouton = bouton_action(i, col, refaire);
                const QString action = refaire ? trad("Refaire la mesure de l'image {}", image + 1)
                                               : a.pointage.nombre_objets() > 1
                                                   ? trad("Supprimer les {} objets de l'image {}", a.pointage.nombre_objets(), image + 1)
                                                   : trad("Supprimer la mesure de l'image {}", image + 1);
                bouton->setToolTip(action);
                bouton->setAccessibleName(action);
            }
        }
        m_affiche = tableau;
        m_langue = langue;
        if (defile)
            m_table->scrollToBottom();
    }
    for (int i = 0; i < n; ++i) {
        m_table->cellWidget(i, donnees)->setEnabled(refaire_permis);
        m_table->cellWidget(i, donnees + 1)->setEnabled(m_supprimer->isEnabled());
        const bool choisie = en_cours == tableau.images[i];
        for (int j = 0; j < donnees; ++j) {
            if (auto *item = m_table->item(i, j)) {
                Pointage::Saisie saisie = Pointage::Saisie::Manuelle;
                if (j > 0) {
                    saisie = a.pointage.saisie((j + 1) / 2, tableau.images[i]);
                } else if (compact) {
                    for (int obj = 1; obj <= a.pointage.nombre_objets(); ++obj)
                        saisie = std::max(saisie, a.pointage.saisie(obj, tableau.images[i]));
                }
                const bool assistee = Pointage::est_assistee(saisie);
                const QIcon marque = assistee ? icones::commande(icones::Commande::ViseeAssistee)
                                   : saisie != Pointage::Saisie::Manuelle ? icones::robot() : QIcon();
                if (marque.cacheKey() != item->icon().cacheKey()) {
                    item->setIcon(marque);
                    redimensionne = true;
                }
                if (j > 0) {
                    const QString &valeur = Pointage::absence_signalee(saisie) ? u"—"_s : tableau.lignes[i][j];
                    if (item->text() != valeur)
                        item->setText(valeur);
                }
                item->setToolTip(saisie == Pointage::Saisie::AbsenceConfirmee ? trad("Absence confirmée")
                                 : saisie == Pointage::Saisie::Perdue ? trad("Mobile perdu · position manquante")
                                 : saisie == Pointage::Saisie::AVerifier ? trad("Point du suivi automatique à vérifier")
                                 : saisie == Pointage::Saisie::AssisteeCorrigee ? trad("Visée corrigée par l’élève · référence")
                                 : assistee ? trad("Visée assistée validée")
                                 : saisie == Pointage::Saisie::Automatique ? trad("Suivi automatique") : QString());
                item->setBackground(choisie ? m_table->palette().highlight() : QBrush());
            }
        }
    }
    if (redimensionne) {
        m_largeurs_a_revoir = false;
        m_largeurs_donnees.clear();
        m_table->resizeColumnsToContents();
        if (etire) {
            for (int j = 0; j < donnees; ++j)
                m_largeurs_donnees.push_back(m_table->columnWidth(j));
            etire_colonnes();
        } else if (compact) {
            setFixedWidth(largeur_necessaire());
        }
    }
}

int TableauMesures::largeur_necessaire() const
{
    return m_table->horizontalHeader()->length() + 2 * m_table->frameWidth() + m_table->verticalScrollBar()->sizeHint().width();
}

void TableauMesures::etire_colonnes()
{
    const int n = static_cast<int>(m_largeurs_donnees.size());
    if (n == 0 || n + 2 != m_table->columnCount())
        return;
    const int donnees = std::accumulate(m_largeurs_donnees.begin(), m_largeurs_donnees.end(), 0);
    const int actions = m_table->columnWidth(n) + m_table->columnWidth(n + 1);
    const int libre = std::max(0, m_table->viewport()->width() - actions - donnees);
    for (int j = 0; j < n; ++j)
        m_table->setColumnWidth(j, m_largeurs_donnees[j] + libre / n + (j < libre % n));
}
