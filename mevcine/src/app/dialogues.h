// Dialogues modaux : messages, demandes de confirmation avant de perdre des mesures, et saisie de la
// longueur de l'étalon (mode Expert ; le mode Simplifié la demande dans son panneau). Ils sont
// affichés un par un, dans l'ordre où ils ont été demandés.

#pragma once

#include "format.h"

#include <QDialog>
#include <QString>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;

/// ce qu'il faut faire après la fermeture d'un message, ou après une confirmation
struct Apres {
    enum class Type {
        /// passer en mode simplifié, en ne gardant que le premier objet suivi
        ModeSimplifie,
        ToutRecommencer,
        /// le dialogue d'ouverture d'une vidéo, dans le dossier `chemin`
        OuvrirVideo,
        /// le dialogue d'ouverture d'un projet
        OuvrirProjet,
        /// ouvrir le fichier `chemin` (déposé sur la fenêtre, ou sauvegarde de secours)
        OuvrirFichier,
        Quitter,
    };
    Type type;
    QString chemin = {};

    bool operator==(const Apres &) const = default;
};

struct Dialogue {
    enum class Type {
        Message,
        /// une action qui ferait perdre les mesures : on demande confirmation ; `bouton` est
        /// l'intitulé du bouton qui confirme
        Confirmation,
        /// saisie de la longueur réelle de l'étalon, qui vient d'être sélectionné sur l'image
        Echelle,
    };
    Type type;
    QString titre = {};
    QString texte = {};
    QString bouton = {};
    std::optional<Apres> apres = {};
    /// longueur proposée, et son unité (dialogue de l'échelle)
    QString saisie = {};
    Unite unite = Unite::Metre;

    static Dialogue message(const QString &titre, const QString &texte)
    {
        return {Type::Message, titre, texte};
    }
};

/// La saisie de la longueur réelle de l'étalon et de son unité : la valeur proposée est sélectionnée
/// (la saisie la remplace) ; une longueur mal écrite est refusée avec un message, sans fermer le
/// dialogue.
class DialogueEchelle : public QDialog {
    Q_OBJECT

public:
    DialogueEchelle(const QString &saisie, Unite unite, QWidget *parent);

    /// la longueur acceptée, et l'unité dans laquelle elle a été donnée
    Longueur longueur() const { return m_longueur; }
    QLineEdit *champ() const { return m_champ; }
    QComboBox *unite() const { return m_unite; }
    /// le message d'erreur affiché (vide s'il n'y en a pas)
    QString erreur() const;

    void accept() override;

private:
    QLineEdit *m_champ = nullptr;
    QComboBox *m_unite = nullptr;
    QLabel *m_erreur = nullptr;
    Longueur m_longueur{0.0, Unite::Metre};
};
