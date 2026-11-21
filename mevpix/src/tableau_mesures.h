// Tableau M1, M2… du panneau Mesurer. Les longueurs proviennent de la séance.
#pragma once

#include <QTableView>

class ModeleMesures;
struct Seance;
struct Textes;

class TableauMesures : public QTableView {
    Q_OBJECT

public:
    explicit TableauMesures(QWidget *parent);
    /// Recharge la liste après une modification de sa taille, de l'échelle ou de la présentation.
    void affiche(const Seance &seance, const Textes &textes);
    /// Pendant le déplacement d'une poignée, seule la longueur de cette ligne change.
    void actualise_longueur(int ligne);

signals:
    void suppression_demandee(int ligne);

protected:
    void keyPressEvent(QKeyEvent *evenement) override;

private:
    void actionne_corbeille(const QModelIndex &cellule);
    ModeleMesures *m_modele;
};
