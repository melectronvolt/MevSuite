// Outils du mode simplifié. Les vues travaillent dans le même Seance que le mode expert.
#pragma once

#include "core/plotspec.h"
#include <QWidget>

class GraphCanvas;
class CourbeResultat;
class QCheckBox;
class MainWindow;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QVBoxLayout;
class TableGrid;
class TableModel;

class Guide : public QWidget {
public:
    explicit Guide(MainWindow &win);
    void rafraichit();
    void ouvre_tableur(ViewId id);
    void ouvre_graphe(ViewId id);
    void reinitialise();

private:
    void trace();
    void ajuste();
    void actualise_courbes();
    void retrouve_modele();
    std::pair<QId, QId> axes_choisis() const;
    void affiche_resultat();
    void affiche_etape();
    const FitSpec *modele_actuel() const;

    MainWindow &m_win;
    QTabWidget *m_etapes;
    QCheckBox *m_voirResidus, *m_inclureTable, *m_inclureGraphe;
    CourbeResultat *m_residus;
    QComboBox *m_tables, *m_graphes, *m_x, *m_y, *m_modele, *m_courbes, *m_tableRapport, *m_grapheRapport;
    QVBoxLayout *m_mesures, *m_trace;
    QWidget *m_apercu;
    ViewId m_sourceCourbes = 0;
    QPushButton *m_statsColonne, *m_stats, *m_tangentes, *m_derivee, *m_primitive, *m_maximum;
    QLabel *m_aideModele, *m_resultat, *m_etatTrace;
    QPushButton *m_tracer, *m_ajuster, *m_copierGraphe, *m_exporterTable;
    QPlainTextEdit *m_conclusion;
    TableModel *m_tableModel = nullptr;
    TableGrid *m_table = nullptr;
    GraphCanvas *m_graphe = nullptr;
    ViewId m_tableId = 0, m_grapheId = 0;
};
