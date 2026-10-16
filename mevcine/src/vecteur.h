// Vecteurs 2D à coordonnées flottantes.
//
// Les coordonnées ne sont pas arrondies à chaque opération : on garde la pleine précision et on ne
// formate qu'à l'affichage ou à l'export.

#pragma once

#include <QString>

#include <cmath>
#include <format>

struct Vecteur {
    double x = 0.0;
    double y = 0.0;

    constexpr Vecteur() = default;
    constexpr Vecteur(double x, double y)
        : x(x)
        , y(y)
    {
    }

    double norme() const { return std::hypot(x, y); }

    /// ajuste les signes des coordonnées selon le sens des axes (±1)
    constexpr Vecteur redresse(int sens_x, int sens_y) const { return {sens_x * x, -sens_y * y}; }

    constexpr Vecteur operator+(Vecteur v) const { return {x + v.x, y + v.y}; }
    constexpr Vecteur operator-(Vecteur v) const { return {x - v.x, y - v.y}; }
    constexpr Vecteur operator-() const { return {-x, -y}; }
    /// produit du vecteur par un nombre
    constexpr Vecteur operator*(double k) const { return {x * k, y * k}; }
    constexpr bool operator==(const Vecteur &) const = default;

    /// pour les messages de débogage et des tests : « (1.00000, 2.00000) »
    QString texte() const { return QString::fromStdString(std::format("({:.5f}, {:.5f})", x, y)); }
};
