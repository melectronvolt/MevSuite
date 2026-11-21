// Export des graphes et des dessins, compte rendu de séance et impression.
#include "app/mainwindow.h"

#include "app/graphwidget.h"
#include "app/statspanel.h"
#include "ecriture_image.h"
#include "plot/report.h"

#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QStandardPaths>
#include <QSvgGenerator>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Exportation d'un dessin : 900 pixels sur le plus grand côté, plus la règle.
QSizeF taille_dessin(const drawing::Drawing &d)
{
    const double w = std::abs(d.desc.xmax - d.desc.xmin), h = std::abs(d.desc.ymax - d.desc.ymin);
    if (w <= 0 || h <= 0)
        return {900, 600};
    if (w >= h)
        return {900, std::max(150.0, 900.0 * h / w + 56.0)};
    return {std::max(200.0, 900.0 * w / h + 56.0), 900};
}

QImage image_dessin(const drawing::Drawing &d, double scale)
{
    const QSizeF size = taille_dessin(d);
    QImage img((size * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    drawing::paint(p, d, size);
    return img;
}

} // namespace

QString MainWindow::dialogue_enregistrement(const QString &defaultName, exporter::Format fmt)
{
    QString filter, ext;
    switch (fmt) {
    case exporter::Format::Png:
        filter = u"Image PNG (*.png)"_s;
        ext = u"png"_s;
        break;
    case exporter::Format::Svg:
        filter = u"Image vectorielle SVG (*.svg)"_s;
        ext = u"svg"_s;
        break;
    case exporter::Format::Pdf:
        filter = u"Document PDF (*.pdf)"_s;
        ext = u"pdf"_s;
        break;
    }
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, u"Exporter"_s, QDir(dir).filePath(csv::nettoie_nom(defaultName, u"graphe"_s) + u'.' + ext), filter);
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += u'.' + ext;
    return path;
}

QSizeF MainWindow::taille_export_graphe(ViewId id) const
{
    const GraphView *g = m_store.ws.graphe(id);
    if (GraphPanel *gp = panneau_graphe(id); gp && gp->canevas()->width() > 200 && gp->canevas()->height() > 150)
        return QSizeF(gp->canevas()->size());
    return exporter::taille_par_defaut(g ? g->rows : 1, g ? g->cols : 1);
}

void MainWindow::exporte_graphe(ViewId id, exporter::Format fmt)
{
    const GraphView *g = m_store.ws.graphe(id);
    if (!g)
        return;
    const QString path = dialogue_enregistrement(g->name, fmt);
    if (path.isEmpty())
        return;
    QString error;
    if (exporter::ecrit_graphe(path, fmt, *g, m_store.ws, taille_export_graphe(id), m_store.style(), &error))
        m_store.message(u"Graphe exporté : %1"_s.arg(QFileInfo(path).fileName()));
    else
        m_store.message(u"Export impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::copie_image_graphe(ViewId id)
{
    const GraphView *g = m_store.ws.graphe(id);
    if (!g)
        return;
    const QImage img = exporter::image_graphe(*g, m_store.ws.doc, taille_export_graphe(id), 2.0, m_store.style());
    QGuiApplication::clipboard()->setImage(img);
    m_store.message(u"Image du graphe copiée"_s);
}

bool MainWindow::imprime_pages(const report::PageWriter &pages, const QString &title)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(title);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    printer.setFullPage(true);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(u"Imprimer"_s);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    if (!pages.paint(printer)) {
        m_store.message(u"Impression impossible"_s, icons::Status::Error);
        return false;
    }
    m_store.message(u"Document envoyé à l'imprimante"_s);
    return true;
}

namespace {

report::PageWriter ecrivain_pour(const Seance &ws)
{
    const auto [title, line] = exporter::lignes_en_tete(ws.meta);
    return report::PageWriter(title, line);
}

void tableur_vers(report::PageWriter &w, const Seance &ws, ViewId id, const Prefs &prefs)
{
    const TableView *t = ws.tableur(id);
    if (!t)
        return;
    QStringList headers;
    std::vector<QStringList> cols;
    for (QId c : t->columns) {
        const Grandeur *q = ws.doc.obtient(c);
        if (!q)
            continue;
        headers << etiquette_avec_unite(q->name, q->unit);
        QStringList v;
        for (double x : q->values)
            v << format_significatif(x, prefs.sigDigits, prefs.style());
        cols.push_back(v);
    }
    w.tableur(headers, cols);
}

void script_vers(report::PageWriter &w, const Store &store, ViewId id)
{
    const Script *s = store.ws.script(id);
    if (!s)
        return;
    w.titre_section(u"Script Python « %1 »"_s.arg(s->name));
    w.code(s->code);
    auto it = store.runs.find(id);
    if (it == store.runs.end())
        return;
    const ScriptRun &r = it->second;
    if (!r.stdoutText.trimmed().isEmpty()) {
        w.paragraphe(u"Sortie du script :"_s, 10.5);
        QString out = r.stdoutText;
        while (out.endsWith(u'\n'))
            out.chop(1);
        w.code(out);
    }
    if (r.error)
        w.paragraphe(u"Erreur : "_s + r.error->message, 10.5);
}

} // namespace

void MainWindow::tableur_pdf(ViewId id, bool print)
{
    if (!valide_saisies())
        return;
    const TableView *t = m_store.ws.tableur(id);
    if (!t)
        return;
    report::PageWriter w = ecrivain_pour(m_store.ws);
    w.titre_section(t->name);
    tableur_vers(w, m_store.ws, id, m_store.prefs);
    if (print) {
        imprime_pages(w, t->name);
        return;
    }
    const QString path = dialogue_enregistrement(t->name, exporter::Format::Pdf);
    if (path.isEmpty())
        return;
    QString error;
    if (w.ecrit_pdf(path, t->name, &error))
        m_store.message(u"PDF enregistré : %1"_s.arg(QFileInfo(path).fileName()));
    else
        m_store.message(u"PDF impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::script_pdf(ViewId id, bool print)
{
    transmet_saisie_scripts();
    const Script *s = m_store.ws.script(id);
    if (!s)
        return;
    report::PageWriter w = ecrivain_pour(m_store.ws);
    script_vers(w, m_store, id);
    if (print) {
        imprime_pages(w, s->name);
        return;
    }
    const QString path = dialogue_enregistrement(s->name, exporter::Format::Pdf);
    if (path.isEmpty())
        return;
    QString error;
    if (w.ecrit_pdf(path, s->name, &error))
        m_store.message(u"PDF enregistré : %1"_s.arg(QFileInfo(path).fileName()));
    else
        m_store.message(u"PDF impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::rapport_pdf(bool print, std::optional<ViewId> table, std::optional<ViewId> graphe)
{
    if (!valide_saisies())
        return;
    const Seance &ws = m_store.ws;
    const NumberStyle style = m_store.style();
    report::PageWriter w = ecrivain_pour(ws);
    if (!ws.meta.notes.trimmed().isEmpty())
        w.paragraphe(ws.meta.notes.trimmed(), 11.0);
    for (const auto &t : ws.tables) {
        if (table && t.id != *table) continue;
        const bool any = std::any_of(t.columns.begin(), t.columns.end(), [&](QId c) {
            const Grandeur *q = ws.doc.obtient(c);
            return q && q->effectif() > 0;
        });
        if (!any)
            continue;
        w.titre_section(t.name);
        tableur_vers(w, ws, t.id, m_store.prefs);
    }
    QStringList params;
    if (!table || *table != 0)
        for (const auto &p : ws.doc.parameters)
            params << (u"%1 = %2 %3"_s.arg(p.name, format_significatif(p.value, 6, style), unite_lisible(p.unit))).trimmed();
    QStringList formulas;
    const auto *selection = table ? ws.tableur(*table) : nullptr;
    for (const auto &q : ws.doc.quantities)
        if (q.formula && (!table || (selection && std::count(selection->columns.begin(), selection->columns.end(), q.id))))
            formulas << u"%1 = %2"_s.arg(q.name, *q.formula);
    if (!params.isEmpty() || !formulas.isEmpty()) {
        w.titre_section(u"Calculs"_s);
        if (!params.isEmpty())
            w.paragraphe(u"Paramètres : "_s + params.join(u" ; "_s), 11.0);
        for (const QString &f : formulas)
            w.paragraphe(f, 11.0);
    }
    for (const auto &g : ws.graphs) {
        if (graphe && g.id != *graphe) continue;
        bool hasData = false;
        for (int k = 0; k < g.traces_visibles(); ++k)
            hasData = hasData || !g.plots[k].curves.empty();
        if (!hasData)
            continue;
        const double h = std::min(340.0 * g.rows, 860.0);
        // le titre reste avec son graphe
        w.assure(h + 60.0);
        w.titre_section(g.name);
        w.graphe(g, exporter::resout_tout(g, ws.doc, style), h, style);
        for (int k = 0; k < g.traces_visibles(); ++k) {
            for (const auto &f : g.plots[k].fits) {
                if (!f.visible || !f.result)
                    continue;
                const fit::FitResult &r = *f.result;
                const QString xn = ws.doc.nom(f.x), yn = ws.doc.nom(f.y);
                const auto model = fit::Model::fabrique(f.kind, xn, ws.doc.valeurs_parametres());
                if (!model)
                    continue;
                QString txt = u"Modèle %1 : %2"_s.arg(f.kind.etiquette().toLower(), model->affichage(r.params, xn, yn, style));
                for (int i = 0; i < model->parametres().size() && static_cast<size_t>(i) < r.params.size(); ++i) {
                    const auto *x = ws.doc.obtient(f.x), *y = ws.doc.obtient(f.y);
                    const QString unite = x && y ? f.kind.unite_parametre(i, x->unit, y->unit) : QString();
                    txt += u" ; %1 = %2"_s.arg(model->parametres()[i], r.texte_parametre(i, unite, style));
                }
                txt += u" ; R² = "_s + format_significatif(r.r2, 5, style);
                w.paragraphe(txt, 10.5);
            }
        }
    }
    if (!table && !graphe) {
        for (const auto &[name, d] : m_store.drawings) {
            const double ratio = std::abs((d.desc.ymax - d.desc.ymin) / (d.desc.xmax - d.desc.xmin));
            const double h = std::clamp(w.largeur() * ratio + 56.0, 150.0, 800.0);
            w.assure(h + 60.0);
            w.titre_section(name);
            w.dessin_rapport(d, h);
        }
        for (const auto &s : ws.scripts)
            script_vers(w, m_store, s.id);
    }
    const QString title = ws.meta.title.trimmed().isEmpty() ? u"compte-rendu"_s : ws.meta.title.trimmed();
    if (print) {
        imprime_pages(w, title);
        return;
    }
    const QString path = dialogue_enregistrement(title, exporter::Format::Pdf);
    if (path.isEmpty())
        return;
    QString error;
    if (w.ecrit_pdf(path, title, &error))
        m_store.message(u"Compte rendu enregistré : %1 (%2 page%3)"_s.arg(QFileInfo(path).fileName()).arg(w.nombre_pages()).arg(w.nombre_pages() > 1 ? u"s" : u""));
    else
        m_store.message(u"PDF impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::imprime_graphe(ViewId id)
{
    const GraphView *g = m_store.ws.graphe(id);
    if (!g)
        return;
    report::PageWriter w = ecrivain_pour(m_store.ws);
    const QSizeF size = taille_export_graphe(id);
    // le graphe à la largeur de la page, dans les proportions de l'écran
    const double h = std::clamp(w.largeur() * size.height() / std::max(size.width(), 1.0), 200.0, report::PAGE_H - 220.0);
    w.graphe(*g, exporter::resout_tout(*g, m_store.ws.doc, m_store.style()), h, m_store.style());
    imprime_pages(w, g->name);
}

void MainWindow::exporte_dessin(const QString &name, exporter::Format fmt)
{
    auto it = m_store.drawings.find(name);
    if (it == m_store.drawings.end())
        return;
    const drawing::Drawing &d = it->second;
    const QString path = dialogue_enregistrement(name, fmt);
    if (path.isEmpty())
        return;
    const QSizeF size = taille_dessin(d);
    QString error;
    const bool ok = exporter::ecrit_fichier(path, [&](QIODevice &file) {
        bool ok = false;
        switch (fmt) {
        case exporter::Format::Png:
            ok = ecriture_image::ecrit(file, image_dessin(d, 2.0), "PNG");
            break;
        case exporter::Format::Svg: {
            QSvgGenerator gen;
            gen.setOutputDevice(&file);
            gen.setResolution(96);
            gen.setSize(size.toSize());
            gen.setViewBox(QRectF(QPointF(0, 0), size));
            gen.setTitle(name);
            QPainter p;
            if ((ok = p.begin(&gen))) {
                p.fillRect(QRectF(QPointF(0, 0), size), Qt::white);
                drawing::paint(p, d, size);
                ok = p.end();
            }
            break;
        }
        case exporter::Format::Pdf: {
            QPdfWriter pdf(&file);
            pdf.setResolution(96);
            pdf.setTitle(name);
            pdf.setCreator(u"MeVReg"_s);
            pdf.setPageSize(QPageSize(size * 72.0 / 96.0, QPageSize::Point, QString(), QPageSize::ExactMatch));
            pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p;
            if ((ok = p.begin(&pdf))) {
                p.fillRect(QRectF(QPointF(0, 0), size), Qt::white);
                drawing::paint(p, d, size);
                ok = p.end();
            }
            break;
        }
        }
        return ok;
    }, &error);
    if (ok)
        m_store.message(u"Dessin exporté : %1"_s.arg(QFileInfo(path).fileName()));
    else
        m_store.message(u"Export impossible : %1"_s.arg(error), icons::Status::Error);
}

void MainWindow::copie_dessin(const QString &name)
{
    auto it = m_store.drawings.find(name);
    if (it == m_store.drawings.end())
        return;
    QGuiApplication::clipboard()->setImage(image_dessin(it->second, 1.5));
    m_store.message(u"Image du dessin copiée"_s);
}

void MainWindow::exporte_histogramme(exporter::Format fmt, const StatsPanel &panneau)
{
    if (!panneau.grandeur_choisie())
        return;
    const auto h = trace_histogramme(m_store.ws.doc, *panneau.grandeur_choisie(), panneau.classes(), m_store.style());
    if (!h)
        return;
    const QString path = dialogue_enregistrement(u"histogramme"_s, fmt);
    if (path.isEmpty())
        return;
    QString error;
    if (exporter::ecrit_traces(path, fmt, h->view, h->data, m_store.ws.meta, QSizeF(720, 460), m_store.style(), &error))
        m_store.message(u"Histogramme exporté : %1"_s.arg(QFileInfo(path).fileName()));
    else
        m_store.message(u"Export impossible : %1"_s.arg(error), icons::Status::Error);
}
