#include "core/drawing.h"

#include "core/expr.h"
#include "core/format.h"
#include "plot/ticks.h"
#include "plot/renderer.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QtConcurrent/QtConcurrentMap>

#include <cmath>

using namespace Qt::StringLiterals;

namespace drawing {

namespace {

QColor parseColor(const QString &c)
{
    QColor q(c);
    return q.isValid() ? q : QColor(Qt::black);
}

std::array<double, 3> lerp3(const std::vector<std::array<double, 3>> &stops, double t)
{
    t = std::clamp(t, 0.0, 1.0) * static_cast<double>(stops.size() - 1);
    const size_t k = std::min(static_cast<size_t>(std::floor(t)), stops.size() - 2);
    const double u = t - static_cast<double>(k);
    const auto &a = stops[k];
    const auto &b = stops[k + 1];
    return {a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u};
}

/// Valeurs ramenées à [0, 1] (ou [−1, 1] pour une palette divergente).
std::vector<double> normalized(const std::vector<double> &values, bool normalize, bool isSigned)
{
    if (!normalize)
        return values;
    double max = 0.0;
    for (double v : values) {
        if (std::isfinite(v))
            max = std::max(max, isSigned ? std::abs(v) : v);
    }
    std::vector<double> out(values.size(), 0.0);
    if (max <= 0.0)
        return out;
    for (size_t i = 0; i < values.size(); ++i)
        out[i] = std::isfinite(values[i]) ? values[i] / max : 0.0;
    return out;
}

QImage colorize(const std::vector<double> &values, int w, int h, const QString &palette, std::optional<double> wavelength, bool normalize)
{
    const auto t = normalized(values, normalize, palette == u"bleu-rouge");
    QImage img(w, h, QImage::Format_RGB32);
    for (int j = 0; j < h; ++j) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(j));
        for (int i = 0; i < w; ++i)
            line[i] = paletteColor(palette, t[static_cast<size_t>(j) * w + i], wavelength).rgb();
    }
    return img;
}

std::pair<int, int> imageSize(const Desc &d, int longest)
{
    const double w = std::abs(d.xmax - d.xmin), h = std::abs(d.ymax - d.ymin);
    if (w >= h)
        return {longest, std::max(1, static_cast<int>(std::lround(longest * h / w)))};
    return {std::max(1, static_cast<int>(std::lround(longest * w / h))), longest};
}

QImage itemImage(const Desc &d, const Item &it, const std::vector<std::vector<double>> &data)
{
    static const std::vector<double> empty;
    const auto &vals = it.data >= 0 && static_cast<size_t>(it.data) < data.size() ? data[it.data] : empty;
    switch (it.type) {
    case Item::Bands:
        if (vals.empty())
            return {};
        return colorize(vals, static_cast<int>(vals.size()), 1, it.palette, it.wavelength, it.normalize);
    case Item::Radial: {
        if (vals.size() < 2 || it.rmax <= 0.0)
            return {};
        const auto [w, h] = imageSize(d, 600);
        std::vector<double> img(static_cast<size_t>(w) * h);
        for (int j = 0; j < h; ++j) {
            const double y = d.ymax - (j + 0.5) / h * (d.ymax - d.ymin);
            for (int i = 0; i < w; ++i) {
                const double x = d.xmin + (i + 0.5) / w * (d.xmax - d.xmin);
                const double r = std::hypot(x, y) / it.rmax * static_cast<double>(vals.size() - 1);
                const size_t k = static_cast<size_t>(std::floor(r));
                img[static_cast<size_t>(j) * w + i] = k + 1 < vals.size() ? vals[k] + (vals[k + 1] - vals[k]) * (r - k) : vals.back();
            }
        }
        return colorize(img, w, h, it.palette, it.wavelength, it.normalize);
    }
    case Item::Map:
        if (it.nx <= 0 || it.ny <= 0 || vals.size() != static_cast<size_t>(it.nx) * it.ny)
            return {};
        return colorize(vals, it.nx, it.ny, it.palette, it.wavelength, it.normalize);
    default:
        return {};
    }
}

double num(const QJsonObject &o, const char16_t *k, double def = 0.0)
{
    const QJsonValue v = o.value(QString::fromUtf16(k));
    return v.isDouble() ? v.toDouble() : def;
}

} // namespace

const QStringList &palettes()
{
    static const QStringList p = {u"gris"_s, u"laser"_s, u"chaud"_s, u"viridis"_s, u"bleu-rouge"_s};
    return p;
}

std::array<double, 3> wavelengthRgb(double lambda)
{
    const double nm = lambda * 1e9;
    double r = 0, g = 0, b = 0;
    if (nm >= 380 && nm < 440) {
        r = -(nm - 440) / 60;
        b = 1;
    } else if (nm >= 440 && nm < 490) {
        g = (nm - 440) / 50;
        b = 1;
    } else if (nm >= 490 && nm < 510) {
        g = 1;
        b = -(nm - 510) / 20;
    } else if (nm >= 510 && nm < 580) {
        r = (nm - 510) / 70;
        g = 1;
    } else if (nm >= 580 && nm < 645) {
        r = 1;
        g = -(nm - 645) / 65;
    } else if (nm >= 645 && nm <= 780) {
        r = 1;
    }
    // atténuation près des limites du visible
    double f = 0.0;
    if (nm >= 380 && nm < 420)
        f = 0.3 + 0.7 * (nm - 380) / 40;
    else if (nm >= 420 && nm <= 700)
        f = 1.0;
    else if (nm > 700 && nm <= 780)
        f = 0.3 + 0.7 * (780 - nm) / 80;
    return {r * f, g * f, b * f};
}

QColor paletteColor(const QString &name, double t, std::optional<double> wavelength)
{
    std::array<double, 3> c;
    if (name == u"laser") {
        const auto base = wavelengthRgb(wavelength.value_or(633e-9));
        // l'œil perçoit mieux les faibles intensités : correction gamma
        const double k = std::pow(std::clamp(t, 0.0, 1.0), 0.6);
        c = {base[0] * k, base[1] * k, base[2] * k};
    } else if (name == u"chaud") {
        c = lerp3({{0, 0, 0}, {0.8, 0, 0}, {1, 0.6, 0}, {1, 1, 0.4}, {1, 1, 1}}, t);
    } else if (name == u"viridis") {
        c = lerp3({{0.267, 0.005, 0.329}, {0.229, 0.322, 0.546}, {0.128, 0.567, 0.551}, {0.369, 0.789, 0.383}, {0.993, 0.906, 0.144}}, t);
    } else if (name == u"bleu-rouge") {
        c = lerp3({{0.02, 0.19, 0.38}, {0.57, 0.77, 0.87}, {0.97, 0.97, 0.97}, {0.96, 0.65, 0.51}, {0.4, 0.0, 0.12}}, (t + 1.0) / 2.0);
    } else {
        const double v = std::clamp(t, 0.0, 1.0);
        c = {v, v, v};
    }
    auto ch = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return QColor(ch(c[0]), ch(c[1]), ch(c[2]));
}

std::optional<std::vector<double>> evalMap(const QString &src, double xmin, double xmax, double ymin, double ymax, int nx, int ny,
                                           const std::vector<std::pair<QString, double>> &params, QString *error)
{
    auto fail = [&](const QString &m) -> std::optional<std::vector<double>> {
        if (error)
            *error = m;
        return std::nullopt;
    };
    const auto parsed = expr::parse(src);
    if (!parsed.ok())
        return fail(parsed.error.message);
    for (const QString &v : parsed.expr->variables()) {
        const bool known = v == u"x" || v == u"y" || v == u"r" || expr::constant(v)
                           || std::any_of(params.begin(), params.end(), [&](const auto &p) { return p.first == v; });
        if (!known)
            return fail(u"nom inconnu dans l'expression : « %1 » (donnez sa valeur : %1=…)"_s.arg(v));
    }
    if (nx <= 0 || ny <= 0 || static_cast<long long>(nx) * ny > 4000000)
        return fail(u"taille de carte invalide"_s);
    std::vector<double> xs(nx);
    for (int i = 0; i < nx; ++i)
        xs[i] = xmin + (i + 0.5) / nx * (xmax - xmin);
    // blocs de lignes évalués en parallèle
    const int rowsPerChunk = std::max(1, 65536 / nx);
    std::vector<std::pair<int, int>> chunks;
    for (int j0 = 0; j0 < ny; j0 += rowsPerChunk)
        chunks.emplace_back(j0, std::min(j0 + rowsPerChunk, ny));
    const expr::Expr e = *parsed.expr;
    std::atomic<bool> failed = false;
    QString firstError;
    QMutex mutex;
    auto work = [&](const std::pair<int, int> &chunk) {
        std::vector<double> x, y;
        for (int j = chunk.first; j < chunk.second; ++j) {
            const double yv = ymax - (j + 0.5) / ny * (ymax - ymin);
            x.insert(x.end(), xs.begin(), xs.end());
            y.insert(y.end(), static_cast<size_t>(nx), yv);
        }
        const size_t n = x.size();
        auto env = [&](const QString &name) -> std::optional<expr::Value> {
            if (name == u"x")
                return expr::Value::vector(x);
            if (name == u"y")
                return expr::Value::vector(y);
            if (name == u"r") {
                std::vector<double> r(n);
                for (size_t i = 0; i < n; ++i)
                    r[i] = std::hypot(x[i], y[i]);
                return expr::Value::vector(std::move(r));
            }
            for (const auto &[k, v] : params) {
                if (k == name)
                    return expr::Value::number(v);
            }
            return std::nullopt;
        };
        expr::Error err;
        auto v = e.eval(env, &err);
        if (!v) {
            QMutexLocker lock(&mutex);
            failed = true;
            firstError = err.message;
            return std::vector<double>();
        }
        if (v->scalar)
            return std::vector<double>(n, v->s);
        auto out = v->v;
        out.resize(n, NAN);
        return out;
    };
    const QList<std::vector<double>> parts = QtConcurrent::blockingMapped<QList<std::vector<double>>>(chunks, work);
    if (failed)
        return fail(firstError);
    std::vector<double> out;
    out.reserve(static_cast<size_t>(nx) * ny);
    for (const auto &p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

std::optional<Desc> Desc::fromJson(const QJsonObject &o, QString *error)
{
    Desc d;
    d.name = o.value(u"name"_s).toString();
    d.xmin = num(o, u"xmin", -1);
    d.xmax = num(o, u"xmax", 1);
    d.ymin = num(o, u"ymin", -1);
    d.ymax = num(o, u"ymax", 1);
    d.unit = o.value(u"unit"_s).toString();
    d.background = o.value(u"background"_s).toString(u"#ffffff"_s);
    static const QStringList types = {u"Line"_s, u"Arrow"_s, u"Circle"_s, u"Rect"_s, u"Text"_s, u"Point"_s, u"Bands"_s, u"Radial"_s, u"Map"_s};
    for (const QJsonValue v : o.value(u"items"_s).toArray()) {
        const QJsonObject j = v.toObject();
        const qsizetype t = types.indexOf(j.value(u"type"_s).toString());
        if (t < 0) {
            if (error)
                *error = u"élément de dessin inconnu : %1"_s.arg(j.value(u"type"_s).toString());
            return std::nullopt;
        }
        Item it;
        it.type = static_cast<Item::Type>(t);
        it.x1 = num(j, u"x1");
        it.y1 = num(j, u"y1");
        it.x2 = num(j, u"x2");
        it.y2 = num(j, u"y2");
        it.x = num(j, u"x");
        it.y = num(j, u"y");
        it.r = num(j, u"r");
        it.w = num(j, u"w");
        it.h = num(j, u"h");
        it.color = j.value(u"color"_s).toString(u"#000000"_s);
        it.width = num(j, u"width", 1.5);
        it.dash = j.value(u"dash"_s).toString();
        if (j.value(u"fill"_s).isString())
            it.fill = j.value(u"fill"_s).toString();
        it.text = j.value(u"text"_s).toString();
        it.size = num(j, u"size", 12);
        it.data = j.value(u"data"_s).toInt();
        it.nx = j.value(u"nx"_s).toInt();
        it.ny = j.value(u"ny"_s).toInt();
        it.rmax = num(j, u"rmax");
        it.palette = j.value(u"palette"_s).toString(u"gris"_s);
        if (j.value(u"wavelength"_s).isDouble())
            it.wavelength = j.value(u"wavelength"_s).toDouble();
        it.normalize = j.value(u"normalize"_s).toBool(true);
        d.items.push_back(it);
    }
    return d;
}

Drawing Drawing::make(Desc desc, std::vector<std::vector<double>> data)
{
    Drawing d{std::move(desc), std::move(data), {}};
    for (const auto &it : d.desc.items)
        d.images.push_back(itemImage(d.desc, it, d.data));
    return d;
}

View paint(QPainter &p, const Drawing &d, QSizeF size)
{
    const Desc &desc = d.desc;
    const double margin = 28.0;
    const double ww = std::max(std::abs(desc.xmax - desc.xmin), 1e-300);
    const double wh = std::max(std::abs(desc.ymax - desc.ymin), 1e-300);
    const double scale = std::max(std::min((size.width() - 2 * margin) / ww, (size.height() - 2 * margin) / wh), 1e-30);
    View view;
    view.scale = scale;
    view.w = ww * scale;
    view.h = wh * scale;
    view.x0 = (size.width() - view.w) / 2.0;
    view.y0 = (size.height() - view.h) / 2.0;
    view.xmin = desc.xmin;
    view.ymax = desc.ymax;
    const QRectF frame = view.frame();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(frame, parseColor(desc.background));
    p.setClipRect(frame);
    auto pen = [](const QString &c, double w) {
        QPen pen(parseColor(c), std::max(w, 0.3));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        return pen;
    };
    for (size_t k = 0; k < desc.items.size(); ++k) {
        const Item &it = desc.items[k];
        switch (it.type) {
        case Item::Bands:
        case Item::Radial:
        case Item::Map:
            if (!d.images[k].isNull())
                p.drawImage(frame, d.images[k]);
            break;
        case Item::Line: {
            QPen pn = pen(it.color, it.width);
            if (it.dash == u"--")
                pn.setDashPattern({6.0 / pn.widthF(), 4.0 / pn.widthF()});
            else if (it.dash == u":")
                pn.setDashPattern({1.5 / pn.widthF(), 3.0 / pn.widthF()});
            else if (it.dash == u"-.")
                pn.setDashPattern({6.0 / pn.widthF(), 3.0 / pn.widthF(), 1.5 / pn.widthF(), 3.0 / pn.widthF()});
            if (pn.style() != Qt::SolidLine || !pn.dashPattern().isEmpty())
                pn.setCapStyle(Qt::FlatCap);
            p.setPen(pn);
            p.drawLine(view.toPx(it.x1, it.y1), view.toPx(it.x2, it.y2));
            break;
        }
        case Item::Arrow: {
            const QPointF a = view.toPx(it.x1, it.y1), b = view.toPx(it.x2, it.y2);
            const double dx = b.x() - a.x(), dy = b.y() - a.y();
            const double len = std::max(std::hypot(dx, dy), 1e-6);
            const double ux = dx / len, uy = dy / len;
            const double head = std::min(8.0 + 2.0 * it.width, len * 0.5);
            const double wing = head * 0.45;
            const QPointF base(b.x() - ux * head, b.y() - uy * head);
            p.setPen(pen(it.color, it.width));
            p.drawLine(a, base);
            QPainterPath tri;
            tri.moveTo(b);
            tri.lineTo(base.x() - uy * wing, base.y() + ux * wing);
            tri.lineTo(base.x() + uy * wing, base.y() - ux * wing);
            tri.closeSubpath();
            p.setBrush(parseColor(it.color));
            p.drawPath(tri);
            p.setBrush(Qt::NoBrush);
            break;
        }
        case Item::Circle: {
            const double r = std::max(it.r * scale, 0.5);
            p.setPen(pen(it.color, it.width));
            p.setBrush(it.fill ? QBrush(parseColor(*it.fill)) : QBrush(Qt::NoBrush));
            p.drawEllipse(view.toPx(it.x, it.y), r, r);
            p.setBrush(Qt::NoBrush);
            break;
        }
        case Item::Rect: {
            p.setPen(pen(it.color, it.width));
            p.setBrush(it.fill ? QBrush(parseColor(*it.fill)) : QBrush(Qt::NoBrush));
            p.drawRect(QRectF(view.toPx(it.x, it.y), view.toPx(it.x + it.w, it.y + it.h)).normalized());
            p.setBrush(Qt::NoBrush);
            break;
        }
        case Item::Point: {
            const double r = std::max(it.size / 2.0, 1.0);
            p.setPen(Qt::NoPen);
            p.setBrush(parseColor(it.color));
            p.drawEllipse(view.toPx(it.x, it.y), r, r);
            p.setBrush(Qt::NoBrush);
            break;
        }
        case Item::Text: {
            p.setPen(parseColor(it.color));
            p.setFont(plotFont(it.size, false, p.device()));
            p.setClipping(false);
            p.drawText(view.toPx(it.x, it.y), it.text);
            p.setClipRect(frame);
            break;
        }
        }
    }
    p.setClipping(false);
    // règle graduée sous la fenêtre (échelle)
    const double step = ticks::niceStep(desc.xmax - desc.xmin, 4.0);
    const double barPx = std::min(step * scale, view.w);
    const double by = std::min(view.y0 + view.h + 14.0, size.height() - 4.0);
    const QColor gray(90, 90, 90);
    QPen rp(gray, 1.2);
    rp.setCapStyle(Qt::FlatCap);
    p.setPen(rp);
    p.drawPolyline(QPolygonF{QPointF(view.x0, by - 4), QPointF(view.x0, by), QPointF(view.x0 + barPx, by), QPointF(view.x0 + barPx, by - 4)});
    const QString label = (formatSig(step, 3, NumberStyle{}) + u' ' + prettyUnit(desc.unit)).trimmed();
    const QFont f = plotFont(11.0, false, p.device());
    p.setFont(f);
    p.drawText(QPointF(view.x0 + barPx + 6.0, by + QFontMetricsF(f, p.device()).capHeight() / 2.0 - 2.0), label);
    p.restore();
    return view;
}

} // namespace drawing
