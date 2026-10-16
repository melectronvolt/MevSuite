#include "core/spline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spline {

namespace {

double signum(double v)
{
    return std::signbit(v) ? -1.0 : 1.0;
}

/// Pentes de la spline cubique naturelle (dérivée seconde nulle aux bords).
std::vector<double> naturalSlopes(const std::vector<double> &x, const std::vector<double> &y)
{
    const size_t n = x.size();
    if (n == 2) {
        const double s = (y[1] - y[0]) / (x[1] - x[0]);
        return {s, s};
    }
    std::vector<double> h(n - 1);
    for (size_t i = 0; i + 1 < n; ++i)
        h[i] = x[i + 1] - x[i];
    std::vector<double> a(n, 0.0), b(n, 1.0), c(n, 0.0), r(n, 0.0);
    for (size_t i = 1; i + 1 < n; ++i) {
        a[i] = h[i - 1];
        b[i] = 2.0 * (h[i - 1] + h[i]);
        c[i] = h[i];
        r[i] = 6.0 * ((y[i + 1] - y[i]) / h[i] - (y[i] - y[i - 1]) / h[i - 1]);
    }
    // algorithme de Thomas
    for (size_t i = 1; i < n; ++i) {
        const double w = a[i] / b[i - 1];
        b[i] -= w * c[i - 1];
        r[i] -= w * r[i - 1];
    }
    std::vector<double> m(n, 0.0);
    m[n - 1] = r[n - 1] / b[n - 1];
    for (size_t i = n - 1; i-- > 0;)
        m[i] = (r[i] - c[i] * m[i + 1]) / b[i];
    std::vector<double> d(n, 0.0);
    for (size_t i = 0; i + 1 < n; ++i)
        d[i] = (y[i + 1] - y[i]) / h[i] - h[i] * (2.0 * m[i] + m[i + 1]) / 6.0;
    d[n - 1] = (y[n - 1] - y[n - 2]) / h[n - 2] + h[n - 2] * (m[n - 2] + 2.0 * m[n - 1]) / 6.0;
    return d;
}

double pchipEdge(double h0, double h1, double m0, double m1)
{
    const double d = ((2.0 * h0 + h1) * m0 - h0 * m1) / (h0 + h1);
    if (signum(d) != signum(m0) || m0 == 0.0)
        return 0.0;
    if (signum(m0) != signum(m1) && std::abs(d) > 3.0 * std::abs(m0))
        return 3.0 * m0;
    return d;
}

/// Pentes PCHIP (Fritsch–Butland), identiques à celles de scipy.
std::vector<double> pchipSlopes(const std::vector<double> &x, const std::vector<double> &y)
{
    const size_t n = x.size();
    std::vector<double> h(n - 1), delta(n - 1);
    for (size_t k = 0; k + 1 < n; ++k) {
        h[k] = x[k + 1] - x[k];
        delta[k] = (y[k + 1] - y[k]) / h[k];
    }
    if (n == 2)
        return {delta[0], delta[0]};
    std::vector<double> d(n, 0.0);
    for (size_t k = 1; k + 1 < n; ++k) {
        const double d0 = delta[k - 1], d1 = delta[k];
        if (d0 == 0.0 || d1 == 0.0 || signum(d0) != signum(d1)) {
            d[k] = 0.0;
        } else {
            const double w1 = 2.0 * h[k] + h[k - 1];
            const double w2 = h[k] + 2.0 * h[k - 1];
            d[k] = (w1 + w2) / (w1 / d0 + w2 / d1);
        }
    }
    d[0] = pchipEdge(h[0], h[1], delta[0], delta[1]);
    d[n - 1] = pchipEdge(h[n - 2], h[n - 3], delta[n - 2], delta[n - 3]);
    return d;
}

} // namespace

std::optional<Hermite> Hermite::make(const std::vector<double> &x, const std::vector<double> &y, Kind kind)
{
    if (x.size() < 2 || x.size() != y.size())
        return std::nullopt;
    for (size_t i = 0; i + 1 < x.size(); ++i) {
        if (!(x[i + 1] > x[i]))
            return std::nullopt;
    }
    Hermite h;
    h.x = x;
    h.y = y;
    h.d = kind == Kind::Cubic ? naturalSlopes(x, y) : pchipSlopes(x, y);
    return h;
}

size_t Hermite::segment(double t) const
{
    const size_t n = x.size();
    const size_t k = static_cast<size_t>(std::upper_bound(x.begin(), x.end(), t) - x.begin());
    return std::min(k == 0 ? 0 : k - 1, n - 2);
}

double Hermite::evalIn(size_t k, double t) const
{
    const double h = x[k + 1] - x[k];
    const double s = (t - x[k]) / h;
    const double s2 = s * s, s3 = s2 * s;
    const double h00 = 2 * s3 - 3 * s2 + 1;
    const double h10 = s3 - 2 * s2 + s;
    const double h01 = -2 * s3 + 3 * s2;
    const double h11 = s3 - s2;
    return h00 * y[k] + h10 * h * d[k] + h01 * y[k + 1] + h11 * h * d[k + 1];
}

double Hermite::eval(double t) const
{
    return evalIn(segment(t), t);
}

double Hermite::deriv(double t) const
{
    const size_t k = segment(t);
    const double h = x[k + 1] - x[k];
    const double s = (t - x[k]) / h;
    const double s2 = s * s;
    const double dh00 = (6 * s2 - 6 * s) / h;
    const double dh10 = 3 * s2 - 4 * s + 1;
    const double dh01 = (-6 * s2 + 6 * s) / h;
    const double dh11 = 3 * s2 - 2 * s;
    return dh00 * y[k] + dh10 * d[k] + dh01 * y[k + 1] + dh11 * d[k + 1];
}

std::vector<std::pair<double, double>> Hermite::sample(int perSegment) const
{
    const int per = std::max(perSegment, 1);
    std::vector<std::pair<double, double>> out;
    out.reserve((x.size() - 1) * per + 1);
    for (size_t k = 0; k + 1 < x.size(); ++k) {
        for (int j = 0; j < per; ++j) {
            const double t = x[k] + (x[k + 1] - x[k]) * j / per;
            out.emplace_back(t, j == 0 ? y[k] : evalIn(k, t));
        }
    }
    out.emplace_back(x.back(), y.back());
    return out;
}

std::vector<std::pair<double, double>> smoothCurve(const std::vector<double> &x, const std::vector<double> &y, Kind kind, int perSegment)
{
    std::vector<std::pair<double, double>> pts;
    for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
        if (std::isfinite(x[i]) && std::isfinite(y[i]))
            pts.emplace_back(x[i], y[i]);
    }
    if (pts.size() < 3)
        return pts;
    bool increasing = true, decreasing = true;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        increasing = increasing && pts[i + 1].first > pts[i].first;
        decreasing = decreasing && pts[i + 1].first < pts[i].first;
    }
    if (increasing || decreasing) {
        auto p = pts;
        if (decreasing)
            std::reverse(p.begin(), p.end());
        std::vector<double> xs, ys;
        for (auto [a, b] : p) {
            xs.push_back(a);
            ys.push_back(b);
        }
        auto h = Hermite::make(xs, ys, kind);
        return h ? h->sample(perSegment) : pts;
    }
    // paramétrage par la longueur de corde, normalisée par l'étendue de chaque axe
    double x0 = std::numeric_limits<double>::infinity(), x1 = -x0, y0 = x0, y1 = -x0;
    for (auto [a, b] : pts) {
        x0 = std::min(x0, a);
        x1 = std::max(x1, a);
        y0 = std::min(y0, b);
        y1 = std::max(y1, b);
    }
    const double xr = x1 > x0 ? x1 - x0 : 1.0;
    const double yr = y1 > y0 ? y1 - y0 : 1.0;
    std::vector<double> u{0.0}, xs{pts[0].first}, ys{pts[0].second};
    for (size_t i = 1; i < pts.size(); ++i) {
        const double dx = (pts[i].first - pts[i - 1].first) / xr;
        const double dy = (pts[i].second - pts[i - 1].second) / yr;
        u.push_back(u.back() + std::max(std::sqrt(dx * dx + dy * dy), 1e-12));
        xs.push_back(pts[i].first);
        ys.push_back(pts[i].second);
    }
    auto hx = Hermite::make(u, xs, kind);
    auto hy = Hermite::make(u, ys, kind);
    if (!hx || !hy)
        return pts;
    const auto sx = hx->sample(perSegment);
    const auto sy = hy->sample(perSegment);
    std::vector<std::pair<double, double>> out(sx.size());
    for (size_t i = 0; i < sx.size(); ++i)
        out[i] = {sx[i].second, sy[i].second};
    return out;
}

} // namespace spline
