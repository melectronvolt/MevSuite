#include "python/figure.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace figure {

namespace {

std::optional<QString> optStr(const QJsonObject &o, const QString &k)
{
    const QJsonValue v = o.value(k);
    if (v.isString())
        return v.toString();
    return std::nullopt;
}

std::optional<double> optNum(const QJsonObject &o, const QString &k)
{
    const QJsonValue v = o.value(k);
    if (v.isDouble())
        return v.toDouble();
    return std::nullopt;
}

std::optional<int> optInt(const QJsonObject &o, const QString &k)
{
    const QJsonValue v = o.value(k);
    if (v.isDouble())
        return v.toInt();
    return std::nullopt;
}

Limits limits(const QJsonObject &o, const QString &k)
{
    const QJsonArray a = o.value(k).toArray();
    if (a.size() != 2)
        return std::nullopt;
    auto n = [](const QJsonValue &v) -> std::optional<double> {
        if (v.isDouble())
            return v.toDouble();
        return std::nullopt;
    };
    return std::make_pair(n(a[0]), n(a[1]));
}

LineDesc line(const QJsonObject &o)
{
    LineDesc l;
    l.value = o.value(u"value"_s).toDouble();
    l.color = optStr(o, u"color"_s);
    l.linestyle = optStr(o, u"linestyle"_s);
    l.label = optStr(o, u"label"_s);
    l.right = o.value(u"right"_s).toBool();
    return l;
}

} // namespace

FigureDesc FigureDesc::fromJson(const QJsonObject &o)
{
    FigureDesc f;
    f.name = o.value(u"name"_s).toString();
    f.rows = o.value(u"rows"_s).toInt(1);
    f.cols = o.value(u"cols"_s).toInt(1);
    f.suptitle = optStr(o, u"suptitle"_s);
    for (const QJsonValue av : o.value(u"axes"_s).toArray()) {
        const QJsonObject a = av.toObject();
        AxesDesc ax;
        ax.index = a.value(u"index"_s).toInt(1);
        ax.title = optStr(a, u"title"_s);
        ax.xlabel = optStr(a, u"xlabel"_s);
        ax.ylabel = optStr(a, u"ylabel"_s);
        ax.y2label = optStr(a, u"y2label"_s);
        ax.xlim = limits(a, u"xlim"_s);
        ax.ylim = limits(a, u"ylim"_s);
        ax.y2lim = limits(a, u"y2lim"_s);
        ax.grid = a.value(u"grid"_s).toBool();
        ax.legend = a.value(u"legend"_s).toBool();
        ax.xscale = optStr(a, u"xscale"_s);
        ax.yscale = optStr(a, u"yscale"_s);
        ax.y2scale = optStr(a, u"y2scale"_s);
        for (const QJsonValue sv : a.value(u"series"_s).toArray()) {
            const QJsonObject s = sv.toObject();
            SeriesDesc d;
            d.kind = s.value(u"kind"_s).toString();
            d.x = s.value(u"x"_s).toInt();
            d.y = s.value(u"y"_s).toInt();
            d.xerr = optInt(s, u"xerr"_s);
            d.yerr = optInt(s, u"yerr"_s);
            d.xname = optStr(s, u"xname"_s);
            d.yname = optStr(s, u"yname"_s);
            d.label = optStr(s, u"label"_s);
            d.color = optStr(s, u"color"_s);
            d.marker = optStr(s, u"marker"_s);
            d.linestyle = optStr(s, u"linestyle"_s);
            d.linewidth = optNum(s, u"linewidth"_s);
            d.markersize = optNum(s, u"markersize"_s);
            d.width = optNum(s, u"width"_s);
            d.right = s.value(u"right"_s).toBool();
            ax.series.push_back(d);
        }
        for (const QJsonValue v : a.value(u"hlines"_s).toArray())
            ax.hlines.push_back(line(v.toObject()));
        for (const QJsonValue v : a.value(u"vlines"_s).toArray())
            ax.vlines.push_back(line(v.toObject()));
        for (const QJsonValue v : a.value(u"texts"_s).toArray()) {
            const QJsonObject t = v.toObject();
            ax.texts.push_back({t.value(u"x"_s).toDouble(), t.value(u"y"_s).toDouble(), t.value(u"text"_s).toString(), optStr(t, u"color"_s),
                                t.value(u"right"_s).toBool()});
        }
        f.axes.push_back(ax);
    }
    return f;
}

} // namespace figure
