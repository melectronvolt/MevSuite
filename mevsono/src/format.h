// Nombres affichés : séparateur décimal de la langue (virgule en français), chiffres significatifs,
// unités adaptées (ms ou s, Hz ou kHz).

#pragma once

#include <QChar>
#include <QString>

/// Nombre en écriture décimale arrondi à `chiffres` chiffres significatifs.
QString texte_nombre(double valeur, int chiffres, QChar separateur);
/// Nombre de décimales nécessaires pour écrire `valeur` avec `chiffres` chiffres significatifs.
int decimales_significatives(double valeur, int chiffres);
/// Nombre avec `decimals` décimales, le signe « − » et le séparateur décimal `dec`.
QString texte_decimal(double v, int decimals, QChar dec);
/// Nombre pour un fichier CSV : signe « - » ordinaire, séparateur décimal `dec`.
QString nombre_csv(double v, int decimals, QChar dec);
/// Écriture la plus courte qui redonne exactement `v`, sans exposant (« 0.1 », « 2 », « -55.5 »).
QString plus_court(double v);
/// Durée : en millisecondes en dessous d'une seconde, sinon en secondes.
QString texte_temps(double t, QChar dec);
/// Fréquence en hertz, avec une décimale (deux en dessous de 100 Hz, aucune au-delà de 10 kHz).
QString texte_frequence(double f, QChar dec);
/// Instant sur le signal, en secondes, avec assez de décimales pour distinguer deux échantillons.
QString texte_position(double t, double rate, QChar dec);
/// Durée d'un son : « 850 ms », « 3,20 s », « 2 min 05 s ».
QString texte_duree(double t, QChar dec);
/// Fréquence pour une graduation d'axe : 50 Hz, 2 kHz, 1,5 kHz…
QString texte_frequence_court(double f, QChar dec);
/// Amplitude en décibels par rapport à la pleine échelle.
double en_decibels(double a);
