#include "tableau.h"

#include "app.h"
#include "dessin.h"
#include "i18n.h"

#include <QHeaderView>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

TableauMesures::TableauMesures(App &app, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_vide = new QLabel(this);
    m_vide->setWordWrap(true);
    m_vide->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_vide->setForegroundRole(QPalette::PlaceholderText);
    m_table = new QTableWidget(this);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(false);
    l->addWidget(m_vide);
    l->addWidget(m_table, 1);
}

void TableauMesures::maj()
{
    App &a = m_app;
    const Tableau tableau = Tableau::construit(a.pointage);
    const int n = static_cast<int>(tableau.lignes.size());
    m_vide->setText(trad("Les mesures s'afficheront ici, au fur et à mesure du pointage."));
    m_vide->setVisible(n == 0);
    m_table->setVisible(n > 0);
    const bool refaire_permis = a.etat == Etat::D && !a.choix_origine;
    // l'image dont on refait la mesure
    const std::optional<int> en_cours = a.refait ? std::optional(a.index - 1) : std::nullopt;
    const Langue langue = i18n::langue();
    if (tableau != m_affiche || m_langue != langue) {
        // on fait défiler le tableau jusqu'à une nouvelle mesure
        const bool defile = n != static_cast<int>(m_affiche.lignes.size());
        const int colonnes = static_cast<int>(tableau.en_tetes.size()) + 1;
        m_table->setColumnCount(colonnes);
        QStringList en_tetes = tableau.en_tetes;
        en_tetes << trad("Refaire");
        m_table->setHorizontalHeaderLabels(en_tetes);
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
                if (m_etire)
                    item->setTextAlignment(Qt::AlignCenter);
            }
            auto *b = qobject_cast<QToolButton *>(m_table->cellWidget(i, colonnes - 1));
            if (!b) {
                b = new QToolButton(m_table);
                b->setText(u"⌖"_s);
                b->setAutoRaise(true);
                m_table->setCellWidget(i, colonnes - 1, b);
            }
            b->disconnect(this);
            connect(b, &QToolButton::clicked, this, [this, image] {
                m_app.refait_point_depuis_tableau(image + 1);
                m_app.rafraichit();
            });
            b->setToolTip(trad("Refaire la mesure de l'image {}", image + 1));
            b->setAccessibleName(u"⌖"_s);
        }
        const int chiffres = m_table->fontMetrics().horizontalAdvance(u"-0,00000"_s) + 12;
        QHeaderView *entetes = m_table->horizontalHeader();
        if (m_etire) {
            // Les colonnes des mesures se partagent la largeur, sans descendre sous celle d'un nombre :
            // trop nombreuses (plusieurs objets), le tableau défile en largeur. « Refaire », à sa
            // largeur.
            // Les largeurs restent réglables à la main (Interactive) : elles sont seulement données
            // au départ, partagées, sans être figées.
            entetes->setMinimumSectionSize(chiffres);
            entetes->setSectionResizeMode(QHeaderView::Interactive);
            m_table->resizeColumnToContents(colonnes - 1);
            const int reste = m_table->viewport()->width() - m_table->columnWidth(colonnes - 1);
            const int part = colonnes > 1 ? std::max(chiffres, reste / (colonnes - 1)) : chiffres;
            for (int j = 0; j + 1 < colonnes; ++j)
                m_table->setColumnWidth(j, part);
        } else {
            m_table->resizeColumnsToContents();
            for (int j = 0; j + 1 < colonnes; ++j)
                m_table->setColumnWidth(j, std::max(m_table->columnWidth(j), chiffres));
        }
        m_affiche = tableau;
        m_langue = langue;
        if (defile && n > 0)
            m_table->scrollToBottom();
    }
    for (int i = 0; i < n; ++i) {
        if (auto *b = m_table->cellWidget(i, m_table->columnCount() - 1))
            b->setEnabled(refaire_permis);
        const bool choisie = en_cours == tableau.images[i];
        for (int j = 0; j + 1 < m_table->columnCount(); ++j) {
            if (auto *item = m_table->item(i, j))
                item->setBackground(choisie ? m_table->palette().highlight() : QBrush());
        }
    }
}
