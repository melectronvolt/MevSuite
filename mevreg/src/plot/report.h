// Documents de plusieurs pages A4 : tableur, script, compte rendu complet du TP, avec
// l'en-tête du TP et les numéros de page. Écrits en PDF ou envoyés à l'imprimante.

#pragma once

#include "core/drawing.h"
#include "plot/renderer.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QPagedPaintDevice;
class QPainter;

namespace report {

/// Page A4 à 96 pixels par pouce.
inline constexpr double PAGE_W = 794.0;
inline constexpr double PAGE_H = 1123.0;

/// Mise en page sur plusieurs pages ; le dessin a lieu à la fin, une fois les pages comptées.
class PageWriter {
public:
    /// L'en-tête (titre, ligne d'informations) figure en haut de la première page.
    PageWriter(const QString &title, const QString &infoLine);

    /// Largeur disponible pour un graphe ou un dessin.
    double width() const;
    /// Passe à la page suivante si `h` pixels ne tiennent plus.
    void ensure(double h);
    /// Titre de section.
    void heading(const QString &s);
    /// Paragraphe, coupé en lignes à la largeur de la page.
    void paragraph(const QString &s, double size = 11.0);
    /// Tableau : en-têtes et colonnes de textes, en-têtes répétés sur chaque page.
    void table(const QStringList &headers, const std::vector<QStringList> &columns);
    /// Code (police à chasse fixe), lignes numérotées.
    void code(const QString &src);
    /// Fenêtre graphique de la largeur de la page.
    void graph(const GraphView &view, const std::vector<PlotData> &data, double height, NumberStyle style);
    /// Fenêtre de dessin.
    void drawing(const drawing::Drawing &d, double height);

    int pageCount() const;
    /// Dessine toutes les pages sur l'appareil (PDF ou imprimante) ; faux en cas d'échec.
    bool paint(QPagedPaintDevice &device) const;
    /// Enregistre en PDF.
    bool writePdf(const QString &path, const QString &title, QString *error) const;

private:
    using Op = std::function<void(QPainter &)>;
    void push(Op op);
    void newPage();
    void lineText(const QString &s, double size, bool bold, const QColor &color);

    std::vector<std::vector<Op>> m_pages;
    std::vector<Op> m_ops;
    double m_y;
};

} // namespace report
