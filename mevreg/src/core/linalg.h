// Petite algèbre linéaire dense pour les moindres carrés.

#pragma once

#include <optional>
#include <vector>

namespace linalg {

/// Matrice dense stockée ligne par ligne.
struct Matrix {
    size_t rows = 0;
    size_t cols = 0;
    std::vector<double> data;

    static Matrix zeros(size_t r, size_t c) { return {r, c, std::vector<double>(r * c, 0.0)}; }

    double &operator()(size_t r, size_t c) { return data[r * cols + c]; }
    double operator()(size_t r, size_t c) const { return data[r * cols + c]; }

    /// Produit transposé AᵀA.
    Matrix gram() const;
    /// Produit Aᵀb.
    std::vector<double> tMulVec(const std::vector<double> &b) const;
};

/// Résout `a x = b` par élimination de Gauss avec pivot partiel ; `nullopt` si singulière.
std::optional<std::vector<double>> solve(const Matrix &a, const std::vector<double> &b);
/// Inverse d'une matrice symétrique définie positive (Cholesky après mise à l'échelle).
std::optional<Matrix> inverseSpd(const Matrix &a);
/// Moindres carrés min ‖A p − b‖ par QR de Householder, colonnes normalisées.
std::optional<std::vector<double>> leastSquares(const Matrix &a, const std::vector<double> &b);

} // namespace linalg
