#pragma once

#include <QLockFile>
#include <QStringList>

#include <memory>
#include <optional>

/// Copie de travail d'une seule séance. Le projet choisi avec Ctrl+S appartient à l'élève.
/// Le secours appartient à la fenêtre qui détient son verrou sur le disque.
class SecoursSeance {
public:
    /// Réserve un nom distinct, même si deux élèves ouvrent la même vidéo en même temps.
    void nouvelle(const QString &video);
    /// Réserve une copie avant de la lire. Si une autre fenêtre écrit déjà dedans,
    /// on peut en lire les mesures, puis les continuer dans un nouveau fichier.
    static std::optional<SecoursSeance> reserve_pour_reprise(const QString &fichier);
    QString chemin() const { return m_chemin; }
    bool deja_ecrit() const { return !m_dernier_texte.isEmpty(); }
    /// Renvoie une chaîne vide après succès. Une erreur laisse la copie précédente intacte.
    QString ecrit(const QString &texte);
    void retire_copie_enregistree();

    /// Chemins complets, du plus récent au plus ancien, y compris le secours des anciennes versions.
    static QStringList disponibles();
    static bool est_secours(const QString &fichier);
    static QStringList filtres();

private:
    QString m_chemin;
    QString m_dernier_texte;
    std::unique_ptr<QLockFile> m_verrou;
};
