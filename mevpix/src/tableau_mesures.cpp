#include "tableau_mesures.h"

#include "i18n.h"
#include "session.h"

#include <QAbstractTableModel>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeyEvent>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {
enum Colonne { Numero, Longueur, Corbeille, NombreColonnes };
constexpr int LIGNES_VISIBLES = 12;
} // namespace

/// Qt demande uniquement les cellules à dessiner. Les segments de la séance restent
/// la source des longueurs, y compris lorsque l'étalonnage change.
class ModeleMesures : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_nombre; }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : NombreColonnes; }

    QVariant data(const QModelIndex &cellule, int role) const override
    {
        const int ligne = cellule.row();
        if (!cellule.isValid() || !m_seance || ligne < 0 || ligne >= m_seance->mesures.size())
            return {};
        if (role == Qt::FontRole) {
            if (cellule.column() == Numero)
                return m_gras;
            if (cellule.column() == Longueur)
                return m_chiffres;
        }
        if (cellule.column() == Corbeille) {
            if (role == Qt::TextAlignmentRole)
                return Qt::AlignCenter;
            if (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole)
                return m_textes->deleteMeasure + u" (M%1)"_s.arg(ligne + 1);
        }
        if (role != Qt::DisplayRole)
            return {};
        switch (cellule.column()) {
        case Numero: return u"M%1"_s.arg(ligne + 1);
        case Longueur: return texte_longueur(m_seance->mesures.at(ligne).longueur_px(), m_seance->etalonnage, m_textes->decimal);
        case Corbeille: return u"🗑"_s;
        }
        return {};
    }

    void affiche(const Seance &seance, const Textes &textes, const QFont &police)
    {
        beginResetModel();
        m_seance = &seance;
        m_nombre = static_cast<int>(seance.mesures.size());
        m_textes = &textes;
        m_gras = police;
        m_gras.setBold(true);
        m_chiffres = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (police.pointSizeF() > 0)
            m_chiffres.setPointSizeF(police.pointSizeF());
        else
            m_chiffres.setPixelSize(police.pixelSize());
        endResetModel();
    }

    void actualise_longueur(int ligne)
    {
        const QModelIndex cellule = index(ligne, Longueur);
        emit dataChanged(cellule, cellule, {Qt::DisplayRole});
    }

private:
    const Seance *m_seance = nullptr;
    const Textes *m_textes = nullptr;
    int m_nombre = 0;
    QFont m_gras, m_chiffres;
};

TableauMesures::TableauMesures(QWidget *parent)
    : QTableView(parent)
    , m_modele(new ModeleMesures(this))
{
    setModel(m_modele);
    horizontalHeader()->hide();
    verticalHeader()->hide();
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    setAlternatingRowColors(true);
    setShowGrid(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    horizontalHeader()->setSectionResizeMode(Longueur, QHeaderView::Stretch);
    connect(this, &QTableView::clicked, this, &TableauMesures::actionne_corbeille);
}

void TableauMesures::actualise_longueur(int ligne)
{
    m_modele->actualise_longueur(ligne);
}

void TableauMesures::affiche(const Seance &seance, const Textes &textes)
{
    m_modele->affiche(seance, textes, font());
    const int nombre = m_modele->rowCount();
    QFont gras = font();
    gras.setBold(true);
    setColumnWidth(Numero, QFontMetrics(gras).horizontalAdvance(u"M%1"_s.arg(nombre)) + 10);
    setColumnWidth(Corbeille, fontMetrics().height() + 12);
    const int hauteur = std::max(fontMetrics().height() + 8, verticalHeader()->minimumSectionSize());
    verticalHeader()->setDefaultSectionSize(hauteur);
    setFixedHeight(std::min(nombre, LIGNES_VISIBLES) * hauteur + 2 * frameWidth());
    setVisible(nombre > 0);
}

void TableauMesures::actionne_corbeille(const QModelIndex &cellule)
{
    if (!cellule.isValid() || cellule.column() != Corbeille)
        return;
    const int ligne = cellule.row();
    emit suppression_demandee(ligne);
    if (m_modele->rowCount() > 0)
        setCurrentIndex(m_modele->index(std::min(ligne, m_modele->rowCount() - 1), Corbeille));
}

void TableauMesures::keyPressEvent(QKeyEvent *evenement)
{
    const int touche = evenement->key();
    if (currentIndex().column() == Corbeille && (touche == Qt::Key_Space || touche == Qt::Key_Return || touche == Qt::Key_Enter)) {
        if (!evenement->isAutoRepeat())
            actionne_corbeille(currentIndex());
        return;
    }
    QTableView::keyPressEvent(evenement);
}
