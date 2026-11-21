// Boîtes de dialogue : création de grandeurs et de paramètres, incertitudes, suites de valeurs,
// suppression, calculs (dérivée, intégrale, spectre, tangentes), préférences,
// informations du document, export vers Python.

#pragma once

#include "core/plotspec.h"

#include <QMetaType>
#include <optional>
#include <utility>

class MainWindow;

#include <QString>

namespace dialogs {

/// Formule saisie : une virgule entre deux chiffres est une virgule décimale (« 0,05 », « sin(0,5) »),
/// sauf entre les arguments d'une fonction qui en a plusieurs (« min(2,3) », « pow(x,2) »).
QString texte_formule(const QString &s);

/// Nouvelle grandeur (mesure ou calculée), ajoutée au tableur donné ou au premier tableur.
void nouvelle_grandeur(MainWindow &win, bool calculated, std::optional<ViewId> table = std::nullopt);
void nouveau_parametre(MainWindow &win);
/// Incertitude-type u(x) rattachée à une grandeur.
void nouvelle_incertitude(MainWindow &win, QId of);
/// Remplit une grandeur expérimentale avec une suite de valeurs.
void remplit(MainWindow &win, QId q);
/// Suppression confirmée, avec la liste des usages de la grandeur.
void confirme_suppression(MainWindow &win, QId q);
/// Nom, unité, formule d'une grandeur (double-clic sur l'en-tête d'une colonne).
void modifie_grandeur(MainWindow &win, QId q, bool convertir = false);
/// Nouvelle grandeur expérimentale, en colonne d'un tableur : elle n'est créée qu'à la validation.
void tableur_guide(MainWindow &win);
void nouvelle_colonne(MainWindow &win, ViewId table);
using Axes = std::pair<QId, QId>;
struct CourbeTracee {
    int panneau = 0;
    QId x = 0, y = 0;
    YSide cote = YSide::Left;
    bool operator==(const CourbeTracee &) const = default;
};
std::vector<std::pair<CourbeTracee, QString>> courbes_visibles(const GraphView &graphe, const Document &doc);
void derivee(MainWindow &win, bool second, std::optional<Axes> axes = std::nullopt);
void integrale(MainWindow &win, std::optional<Axes> axes = std::nullopt);
void spectre(MainWindow &win);
void tangentes(MainWindow &win, std::optional<ViewId> graphe = std::nullopt, CourbeTracee cible = {});
void methode_derivee(MainWindow &win, std::optional<ViewId> graphe = std::nullopt, CourbeTracee cible = {});
void meta(MainWindow &win);
void preferences(MainWindow &win);
void export_python(MainWindow &win);

} // namespace dialogs

Q_DECLARE_METATYPE(dialogs::CourbeTracee)
