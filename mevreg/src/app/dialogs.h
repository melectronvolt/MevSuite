// Boîtes de dialogue : création de grandeurs et de paramètres, incertitudes, suites de valeurs,
// suppression, calculs (dérivée, intégrale, spectre, méthodes de dosage), préférences,
// informations du TP, export vers Python.

#pragma once

#include "core/plotspec.h"

#include <optional>

class MainWindow;

#include <QString>

namespace dialogs {

/// Formule saisie : une virgule entre deux chiffres est une virgule décimale (« 0,05 », « sin(0,5) »),
/// sauf entre les arguments d'une fonction qui en a plusieurs (« min(2,3) », « pow(x,2) »).
QString formulaText(const QString &s);

/// Nouvelle grandeur (mesure ou calculée), ajoutée au tableur donné ou au premier tableur.
void newQuantity(MainWindow &win, bool calculated, std::optional<ViewId> table = std::nullopt);
void newParameter(MainWindow &win);
/// Incertitude-type u(x) rattachée à une grandeur.
void newUncertainty(MainWindow &win, QId of);
/// Remplit une grandeur expérimentale avec une suite de valeurs.
void fill(MainWindow &win, QId q);
/// Suppression confirmée, avec la liste des usages de la grandeur.
void confirmDelete(MainWindow &win, QId q);
/// Nom, unité, formule d'une grandeur (double-clic sur l'en-tête d'une colonne).
void editQuantity(MainWindow &win, QId q);
/// Nouvelle grandeur expérimentale, en colonne d'un tableur : elle n'est créée qu'à la validation.
void newColumn(MainWindow &win, ViewId table);
void derivative(MainWindow &win, bool second);
void integral(MainWindow &win);
void spectrum(MainWindow &win);
void tangents(MainWindow &win, std::optional<ViewId> graph = std::nullopt, int plot = 0);
void derivativeMethod(MainWindow &win);
void meta(MainWindow &win);
void preferences(MainWindow &win);
void pythonExport(MainWindow &win);

} // namespace dialogs
