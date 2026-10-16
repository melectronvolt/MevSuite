// Description d'une figure produite par le mini-matplotlib (envoyée en JSON par pyplot.py).

#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

namespace figure {

struct SeriesDesc {
    /// « line », « scatter » ou « bar ».
    QString kind;
    /// Indices des tableaux de données transmis avec la figure.
    int x = 0, y = 0;
    std::optional<int> xerr, yerr;
    /// Nom de la variable du script, s'il a été retrouvé.
    std::optional<QString> xname, yname, label, color, marker, linestyle;
    std::optional<double> linewidth, markersize, width;
    bool right = false;
};

struct LineDesc {
    double value = 0;
    std::optional<QString> color, linestyle, label;
    bool right = false;
};

struct TextDesc {
    double x = 0, y = 0;
    QString text;
    std::optional<QString> color;
    bool right = false;
};

using Limits = std::optional<std::pair<std::optional<double>, std::optional<double>>>;

struct AxesDesc {
    /// Position dans la grille, à partir de 1 (comme `subplot`).
    int index = 1;
    std::optional<QString> title, xlabel, ylabel, y2label;
    Limits xlim, ylim, y2lim;
    bool grid = false, legend = false;
    std::optional<QString> xscale, yscale, y2scale;
    std::vector<SeriesDesc> series;
    std::vector<LineDesc> hlines, vlines;
    std::vector<TextDesc> texts;
};

struct FigureDesc {
    QString name;
    int rows = 1, cols = 1;
    std::optional<QString> suptitle;
    std::vector<AxesDesc> axes;

    static FigureDesc fromJson(const QJsonObject &o);
};

} // namespace figure
