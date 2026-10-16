#include "core/numeric.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace numeric {

namespace {

std::vector<size_t> validIndices(const Vec &y, const Vec &x)
{
    std::vector<size_t> idx;
    const size_t n = std::min(y.size(), x.size());
    for (size_t i = 0; i < n; ++i) {
        if (std::isfinite(y[i]) && std::isfinite(x[i]))
            idx.push_back(i);
    }
    return idx;
}

} // namespace

Vec gradient(const Vec &y, const Vec &x)
{
    const size_t n = std::max(y.size(), x.size());
    Vec out(n, NAN);
    const auto idx = validIndices(y, x);
    if (idx.size() < 2)
        return out;
    Vec ys, xs;
    for (size_t i : idx) {
        ys.push_back(y[i]);
        xs.push_back(x[i]);
    }
    const Vec g = gradientNumpy(ys, xs);
    for (size_t k = 0; k < idx.size(); ++k)
        out[idx[k]] = std::isfinite(g[k]) ? g[k] : NAN;
    return out;
}

Vec gradientNumpy(const Vec &f, const Vec &x)
{
    const size_t n = f.size();
    if (n < 2)
        return Vec(n, NAN);
    Vec g(n, 0.0);
    // écarts entre abscisses ; constants : formule du pas constant
    Vec dx;
    if (x.size() == 1) {
        dx.assign(n - 1, x[0]);
    } else {
        for (size_t i = 0; i + 1 < x.size(); ++i)
            dx.push_back(x[i + 1] - x[i]);
    }
    const bool uniform = std::all_of(dx.begin(), dx.end(), [&](double d) { return d == dx[0]; });
    if (uniform) {
        const double h = dx[0];
        for (size_t i = 1; i + 1 < n; ++i)
            g[i] = (f[i + 1] - f[i - 1]) / (2.0 * h);
        g[0] = (f[1] - f[0]) / h;
        g[n - 1] = (f[n - 1] - f[n - 2]) / h;
        return g;
    }
    for (size_t i = 1; i + 1 < n; ++i) {
        const double dx1 = dx[i - 1], dx2 = dx[i];
        const double a = -dx2 / (dx1 * (dx1 + dx2));
        const double b = (dx2 - dx1) / (dx1 * dx2);
        const double c = dx1 / (dx2 * (dx1 + dx2));
        g[i] = a * f[i - 1] + b * f[i] + c * f[i + 1];
    }
    g[0] = (f[1] - f[0]) / dx[0];
    g[n - 1] = (f[n - 1] - f[n - 2]) / dx[n - 2];
    return g;
}

Vec cumulativeTrapezoid(const Vec &y, const Vec &x)
{
    const size_t n = std::max(y.size(), x.size());
    Vec out(n, NAN);
    const auto idx = validIndices(y, x);
    if (idx.empty())
        return out;
    double acc = 0.0;
    out[idx[0]] = 0.0;
    for (size_t k = 1; k < idx.size(); ++k) {
        const size_t a = idx[k - 1], b = idx[k];
        acc += 0.5 * (y[a] + y[b]) * (x[b] - x[a]);
        out[b] = acc;
    }
    return out;
}

double trapezoid(const Vec &y, const Vec &x)
{
    const auto idx = validIndices(y, x);
    double s = 0.0;
    for (size_t k = 1; k < idx.size(); ++k) {
        const size_t a = idx[k - 1], b = idx[k];
        s += 0.5 * (y[a] + y[b]) * (x[b] - x[a]);
    }
    return s;
}

double areaBetween(const Vec &y, const Vec &x, double a, double b)
{
    const double lo = std::min(a, b), hi = std::max(a, b), sign = a <= b ? 1.0 : -1.0;
    const auto [xs, ys] = sortedPairs(x, y);
    if (xs.size() < 2)
        return NAN;
    double area = 0.0;
    for (size_t k = 0; k + 1 < xs.size(); ++k) {
        const double x0 = xs[k], x1 = xs[k + 1], y0 = ys[k], y1 = ys[k + 1];
        const double s = std::max(x0, lo), e = std::min(x1, hi);
        if (e <= s || x1 <= x0)
            continue;
        const double ya = y0 + (y1 - y0) * (s - x0) / (x1 - x0);
        const double yb = y0 + (y1 - y0) * (e - x0) / (x1 - x0);
        area += 0.5 * (ya + yb) * (e - s);
    }
    return sign * area;
}

std::pair<Vec, Vec> sortedPairs(const Vec &x, const Vec &y)
{
    auto idx = validIndices(y, x);
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return x[a] < x[b]; });
    Vec xs, ys;
    xs.reserve(idx.size());
    ys.reserve(idx.size());
    for (size_t i : idx) {
        xs.push_back(x[i]);
        ys.push_back(y[i]);
    }
    return {xs, ys};
}

double interp(double t, const Vec &xs, const Vec &ys)
{
    const size_t n = xs.size();
    if (n == 0)
        return NAN;
    if (t <= xs[0])
        return ys[0];
    if (t >= xs[n - 1])
        return ys[n - 1];
    size_t k = static_cast<size_t>(std::upper_bound(xs.begin(), xs.end(), t) - xs.begin());
    k = std::min(k == 0 ? 0 : k - 1, n - 2);
    const double x0 = xs[k], x1 = xs[k + 1];
    if (x1 == x0)
        return ys[k];
    return ys[k] + (ys[k + 1] - ys[k]) * (t - x0) / (x1 - x0);
}

std::optional<std::pair<double, double>> stepInfo(const Vec &x)
{
    Vec xs;
    for (double v : x) {
        if (std::isfinite(v))
            xs.push_back(v);
    }
    if (xs.size() < 2)
        return std::nullopt;
    const double mean = (xs.back() - xs.front()) / static_cast<double>(xs.size() - 1);
    if (mean == 0.0)
        return std::nullopt;
    double dev = 0.0;
    for (size_t i = 0; i + 1 < xs.size(); ++i)
        dev = std::max(dev, std::abs((xs[i + 1] - xs[i]) - mean));
    return std::make_pair(mean, dev / std::abs(mean));
}

std::pair<Vec, Vec> resampleUniform(const Vec &x, const Vec &y, size_t n)
{
    auto [xs, ys] = sortedPairs(x, y);
    if (xs.size() < 2 || n < 2)
        return {xs, ys};
    const double a = xs.front(), b = xs.back();
    Vec grid(n), vals(n);
    for (size_t i = 0; i < n; ++i) {
        grid[i] = a + (b - a) * static_cast<double>(i) / static_cast<double>(n - 1);
        vals[i] = interp(grid[i], xs, ys);
    }
    return {grid, vals};
}

std::optional<std::pair<double, double>> finiteRange(const Vec &v)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double x : v) {
        if (std::isfinite(x)) {
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
    }
    if (lo <= hi)
        return std::make_pair(lo, hi);
    return std::nullopt;
}

} // namespace numeric
