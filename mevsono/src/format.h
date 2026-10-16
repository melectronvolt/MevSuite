// Nombres affichés : séparateur décimal de la langue (virgule en français), chiffres significatifs,
// unités adaptées (ms ou s, Hz ou kHz).

#pragma once

#include <QChar>
#include <QString>

/// Nombre avec `sig` chiffres significatifs.
QString fmtNum(double v, int sig, QChar dec);
/// Nombre de décimales nécessaires pour écrire `v` avec `sig` chiffres significatifs.
int sigDecimals(double v, int sig);
/// Nombre avec `decimals` décimales, le signe « − » et le séparateur décimal `dec`.
QString fmtDec(double v, int decimals, QChar dec);
/// Nombre pour un fichier CSV : signe « - » ordinaire, séparateur décimal `dec`.
QString csvNum(double v, int decimals, QChar dec);
/// Écriture la plus courte qui redonne exactement `v`, sans exposant (« 0.1 », « 2 », « -55.5 »).
QString shortest(double v);
/// Durée : en millisecondes en dessous d'une seconde, sinon en secondes.
QString fmtTime(double t, QChar dec);
/// Fréquence en hertz, avec une décimale (deux en dessous de 100 Hz, aucune au-delà de 10 kHz).
QString fmtFreq(double f, QChar dec);
/// Instant sur le signal, en secondes avec quatre décimales.
QString fmtPosition(double t, QChar dec);
/// Durée d'un son : « 850 ms », « 3,20 s », « 2 min 05 s ».
QString fmtDuration(double t, QChar dec);
/// Fréquence pour une graduation d'axe : 50 Hz, 2 kHz, 1,5 kHz…
QString fmtFreqShort(double f, QChar dec);
/// Amplitude en décibels par rapport à la pleine échelle.
double toDb(double a);
