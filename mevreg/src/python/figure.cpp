#include "python/figure.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace figure {

namespace {

std::optional<QString> chaine_optionnelle(const QJsonObject &o, const QString &k)
{
    const QJsonValue v = o.value(k);
    if (v.isString())
        return v.toString();
    return std::nullopt;
}

std::optional<double> nombre_optionnel(const QJsonObject &o, const QString &k)
{
    const QJsonValue v = o.value(k);
    if (v.isDouble())
        return v.toDouble();
    return std::nullopt;
}

std::optional<int> entier_optionnel(const QJsonObject &o, const QString &k)
{
    const QJsonValue v = o.value(k);
    if (v.isDouble())
        return v.toInt();
    return std::nullopt;
}

Limits limites(const QJsonObject &o, const QString &k)
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

LineDesc trait(const QJsonObject &o)
{
    LineDesc l;
    l.value = o.value(u"value"_s).toDouble();
    l.color = chaine_optionnelle(o, u"color"_s);
    l.linestyle = chaine_optionnelle(o, u"linestyle"_s);
    l.label = chaine_optionnelle(o, u"label"_s);
    l.right = o.value(u"right"_s).toBool();
    return l;
}

} // namespace

FigureDesc FigureDesc::depuis_json(const QJsonObject &o)
{
    FigureDesc f;
    f.name = o.value(u"name"_s).toString();
    f.rows = o.value(u"rows"_s).toInt(1);
    f.cols = o.value(u"cols"_s).toInt(1);
    f.suptitle = chaine_optionnelle(o, u"suptitle"_s);
    for (const QJsonValue av : o.value(u"axes"_s).toArray()) {
        const QJsonObject a = av.toObject();
        AxesDesc ax;
        ax.index = a.value(u"index"_s).toInt(1);
        ax.title = chaine_optionnelle(a, u"title"_s);
        ax.xlabel = chaine_optionnelle(a, u"xlabel"_s);
        ax.ylabel = chaine_optionnelle(a, u"ylabel"_s);
        ax.y2label = chaine_optionnelle(a, u"y2label"_s);
        ax.xlim = limites(a, u"xlim"_s);
        ax.ylim = limites(a, u"ylim"_s);
        ax.y2lim = limites(a, u"y2lim"_s);
        ax.xticks = graduations_depuis_json(a.value(u"xticks"_s));
        ax.yticks = graduations_depuis_json(a.value(u"yticks"_s));
        ax.y2ticks = graduations_depuis_json(a.value(u"y2ticks"_s));
        ax.grid = a.value(u"grid"_s).toBool();
        ax.legend = a.value(u"legend"_s).toBool();
        ax.xscale = chaine_optionnelle(a, u"xscale"_s);
        ax.yscale = chaine_optionnelle(a, u"yscale"_s);
        ax.y2scale = chaine_optionnelle(a, u"y2scale"_s);
        for (const QJsonValue sv : a.value(u"series"_s).toArray()) {
            const QJsonObject s = sv.toObject();
            SeriesDesc d;
            d.kind = s.value(u"kind"_s).toString();
            d.x = s.value(u"x"_s).toInt();
            d.y = s.value(u"y"_s).toInt();
            d.xerr = entier_optionnel(s, u"xerr"_s);
            d.yerr = entier_optionnel(s, u"yerr"_s);
            d.xhigh = entier_optionnel(s, u"xhigh"_s);
            d.yhigh = entier_optionnel(s, u"yhigh"_s);
            d.xname = chaine_optionnelle(s, u"xname"_s);
            d.yname = chaine_optionnelle(s, u"yname"_s);
            d.label = chaine_optionnelle(s, u"label"_s);
            d.color = chaine_optionnelle(s, u"color"_s);
            d.marker = chaine_optionnelle(s, u"marker"_s);
            d.linestyle = chaine_optionnelle(s, u"linestyle"_s);
            d.linewidth = nombre_optionnel(s, u"linewidth"_s);
            d.markersize = nombre_optionnel(s, u"markersize"_s);
            d.width = nombre_optionnel(s, u"width"_s);
            d.right = s.value(u"right"_s).toBool();
            ax.series.push_back(d);
        }
        for (const QJsonValue v : a.value(u"hlines"_s).toArray())
            ax.hlines.push_back(trait(v.toObject()));
        for (const QJsonValue v : a.value(u"vlines"_s).toArray())
            ax.vlines.push_back(trait(v.toObject()));
        for (const QJsonValue v : a.value(u"texts"_s).toArray()) {
            const QJsonObject t = v.toObject();
            ax.texts.push_back({t.value(u"x"_s).toDouble(), t.value(u"y"_s).toDouble(), t.value(u"text"_s).toString(), chaine_optionnelle(t, u"color"_s),
                                t.value(u"right"_s).toBool()});
        }
        f.axes.push_back(ax);
    }
    return f;
}

} // namespace figure
