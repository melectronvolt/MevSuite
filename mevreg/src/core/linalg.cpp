#include "core/linalg.h"

#include <cmath>

namespace linalg {

Matrix Matrix::gram() const
{
    Matrix g = zeros(cols, cols);
    for (size_t r = 0; r < rows; ++r) {
        const double *row = &data[r * cols];
        for (size_t i = 0; i < cols; ++i) {
            const double ri = row[i];
            if (ri == 0.0)
                continue;
            for (size_t j = i; j < cols; ++j)
                g.data[i * cols + j] += ri * row[j];
        }
    }
    for (size_t i = 0; i < cols; ++i) {
        for (size_t j = 0; j < i; ++j)
            g.data[i * cols + j] = g.data[j * cols + i];
    }
    return g;
}

std::vector<double> Matrix::tMulVec(const std::vector<double> &b) const
{
    std::vector<double> out(cols, 0.0);
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c)
            out[c] += data[r * cols + c] * b[r];
    }
    return out;
}

std::optional<std::vector<double>> solve(const Matrix &a, const std::vector<double> &b)
{
    const size_t n = a.rows;
    Matrix m = a;
    std::vector<double> x = b;
    double scale = 0.0;
    for (double v : m.data)
        scale = std::max(scale, std::abs(v));
    if (scale == 0.0 || !std::isfinite(scale))
        return std::nullopt;
    for (size_t col = 0; col < n; ++col) {
        size_t piv = col;
        for (size_t i = col + 1; i < n; ++i) {
            if (std::abs(m(i, col)) > std::abs(m(piv, col)))
                piv = i;
        }
        if (std::abs(m(piv, col)) <= 1e-14 * scale)
            return std::nullopt;
        if (piv != col) {
            for (size_t k = 0; k < n; ++k)
                std::swap(m(piv, k), m(col, k));
            std::swap(x[piv], x[col]);
        }
        for (size_t r = col + 1; r < n; ++r) {
            const double f = m(r, col) / m(col, col);
            if (f == 0.0)
                continue;
            for (size_t k = col; k < n; ++k)
                m(r, k) -= f * m(col, k);
            x[r] -= f * x[col];
        }
    }
    for (size_t col = n; col-- > 0;) {
        double s = x[col];
        for (size_t k = col + 1; k < n; ++k)
            s -= m(col, k) * x[k];
        x[col] = s / m(col, col);
    }
    for (double v : x) {
        if (!std::isfinite(v))
            return std::nullopt;
    }
    return x;
}

std::optional<Matrix> inverseSpd(const Matrix &a)
{
    const size_t n = a.rows;
    std::vector<double> s(n);
    for (size_t i = 0; i < n; ++i) {
        const double d = a(i, i);
        if (!(d > 0.0) || !std::isfinite(d))
            return std::nullopt;
        s[i] = 1.0 / std::sqrt(d);
    }
    Matrix scaled = a;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j)
            scaled(i, j) *= s[i] * s[j];
    }
    // Cholesky : scaled = L Lᵀ
    Matrix l = Matrix::zeros(n, n);
    for (size_t j = 0; j < n; ++j) {
        double diag = scaled(j, j);
        for (size_t k = 0; k < j; ++k)
            diag -= l(j, k) * l(j, k);
        if (diag <= 1e-13)
            return std::nullopt;
        const double ljj = std::sqrt(diag);
        l(j, j) = ljj;
        for (size_t i = j + 1; i < n; ++i) {
            double v = scaled(i, j);
            for (size_t k = 0; k < j; ++k)
                v -= l(i, k) * l(j, k);
            l(i, j) = v / ljj;
        }
    }
    Matrix inv = Matrix::zeros(n, n);
    for (size_t c = 0; c < n; ++c) {
        std::vector<double> e(n, 0.0);
        e[c] = 1.0;
        for (size_t i = 0; i < n; ++i) {
            double v = e[i];
            for (size_t k = 0; k < i; ++k)
                v -= l(i, k) * e[k];
            e[i] = v / l(i, i);
        }
        for (size_t i = n; i-- > 0;) {
            double v = e[i];
            for (size_t k = i + 1; k < n; ++k)
                v -= l(k, i) * e[k];
            e[i] = v / l(i, i);
        }
        for (size_t r = 0; r < n; ++r)
            inv(r, c) = e[r] * s[r] * s[c];
    }
    return inv;
}

std::optional<std::vector<double>> leastSquares(const Matrix &a, const std::vector<double> &b)
{
    const size_t n = a.rows, p = a.cols;
    if (n < p || p == 0)
        return std::nullopt;
    Matrix q = a;
    std::vector<double> norms(p);
    for (size_t j = 0; j < p; ++j) {
        double s = 0.0;
        for (size_t i = 0; i < n; ++i)
            s += q(i, j) * q(i, j);
        norms[j] = std::sqrt(s);
        if (norms[j] == 0.0 || !std::isfinite(norms[j]))
            return std::nullopt;
    }
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < p; ++j)
            q(i, j) /= norms[j];
    }
    std::vector<double> rhs = b;
    for (size_t k = 0; k < p; ++k) {
        double s = 0.0;
        for (size_t i = k; i < n; ++i)
            s += q(i, k) * q(i, k);
        s = std::sqrt(s);
        const double alpha = q(k, k) > 0.0 ? -s : s;
        if (std::abs(alpha) < 1e-12)
            return std::nullopt;
        std::vector<double> v(n - k);
        for (size_t i = k; i < n; ++i)
            v[i - k] = q(i, k);
        v[0] -= alpha;
        double vnorm2 = 0.0;
        for (double x : v)
            vnorm2 += x * x;
        if (vnorm2 == 0.0)
            continue;
        for (size_t j = k; j < p; ++j) {
            double dot = 0.0;
            for (size_t i = k; i < n; ++i)
                dot += v[i - k] * q(i, j);
            const double f = 2.0 * dot / vnorm2;
            for (size_t i = k; i < n; ++i)
                q(i, j) -= f * v[i - k];
        }
        double dot = 0.0;
        for (size_t i = k; i < n; ++i)
            dot += v[i - k] * rhs[i];
        const double f = 2.0 * dot / vnorm2;
        for (size_t i = k; i < n; ++i)
            rhs[i] -= f * v[i - k];
    }
    std::vector<double> x(p, 0.0);
    for (size_t i = p; i-- > 0;) {
        double s = rhs[i];
        for (size_t j = i + 1; j < p; ++j)
            s -= q(i, j) * x[j];
        if (std::abs(q(i, i)) < 1e-12)
            return std::nullopt;
        x[i] = s / q(i, i);
    }
    for (size_t j = 0; j < p; ++j) {
        x[j] /= norms[j];
        if (!std::isfinite(x[j]))
            return std::nullopt;
    }
    return x;
}

} // namespace linalg
