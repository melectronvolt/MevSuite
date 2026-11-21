#include "app/guide.h"
#include "app/dialogs.h"
#include "app/icons.h"
#include "app/graphwidget.h"
#include "app/mainwindow.h"
#include "app/tableview.h"
#include "app/widgets.h"
#include "app/statspanel.h"
#include "app/theme.h"
#include "plot/resolve.h"

#include <QCheckBox>

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <tuple>

using namespace Qt::StringLiterals;

using dialogs::CourbeTracee;

namespace {
using Choix = std::vector<std::pair<quint64, QString>>;

quint64 selection(QComboBox *liste) { return liste->currentData().toULongLong(); }

template <class T>
void actualise(QComboBox *liste, const std::vector<std::pair<T, QString>> &choix, int defaut = 0, bool garder = true)
{
    bool identique = size_t(liste->count()) == choix.size();
    for (int i = 0; identique && i < liste->count(); ++i)
        identique = liste->itemData(i) == QVariant::fromValue(choix[i].first) && liste->itemText(i) == choix[i].second;
    QSignalBlocker bloque(liste);
    if (identique) {
        if (!garder) liste->setCurrentIndex(defaut);
        return;
    }
    const QVariant avant = liste->currentData();
    liste->clear();
    for (const auto &[id, nom] : choix)
        liste->addItem(nom, QVariant::fromValue(id));
    const int garde = liste->findData(avant);
    liste->setCurrentIndex(garder && garde >= 0 ? garde : std::min(defaut, liste->count() - 1));
}

} // namespace

Guide::Guide(MainWindow &win) : QWidget(&win), m_win(win)
{
    setObjectName(u"guide"_s);
    auto *layout = new QVBoxLayout(this);
    auto *split = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(split);
    m_etapes = new QTabWidget(split);
    m_etapes->setObjectName(u"etapesTP"_s);
    auto page = [this](const QString &titre, const char *icone, bool defilable = true) {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        if (defilable) {
            auto *zone = new QScrollArea;
            zone->setFrameShape(QFrame::NoFrame);
            zone->setWidgetResizable(true);
            zone->setWidget(w);
            m_etapes->addTab(zone, icons::obtient(icone), titre);
        } else {
            m_etapes->addTab(w, icons::obtient(icone), titre);
        }
        return l;
    };
    auto bouton = [this](QLayout *l, const char *icone, const QString &titre, auto fn) {
        auto *b = new QPushButton(icons::obtient(icone), titre);
        l->addWidget(b);
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    };
    auto liste = [](QFormLayout *f, const QString &titre, const QString &nom) {
        auto *c = new QComboBox;
        c->setObjectName(nom);
        c->setAccessibleName(titre);
        f->setRowWrapPolicy(QFormLayout::WrapLongRows);
        f->addRow(titre, c);
        return c;
    };

    m_mesures = page(u"Mesurer"_s, "table", false);
    auto *choixTable = new QFormLayout;
    m_mesures->addLayout(choixTable);
    m_tables = liste(choixTable, u"Tableur"_s, u"tableGuide"_s);
    auto *outils = new FlowLayout;
    m_mesures->addLayout(outils);
    bouton(outils, "table", u"Nouveau tableur…"_s, [this] {
        const auto libres = m_win.depot().ws.orphelins();
        if (!libres.empty()) m_win.nouveau_tableur(libres);
        else dialogs::tableur_guide(m_win);
    });
    bouton(outils, "numbers", u"Mesure"_s, [this] { dialogs::nouvelle_colonne(m_win, m_tableId); });
    bouton(outils, "function", u"Formule"_s, [this] { dialogs::nouvelle_grandeur(m_win, true, m_tableId); });
    bouton(outils, "plusminus", u"Incertitude…"_s, [this] {
        if (!m_tableModel || !m_table)
            return;
        const auto id = m_tableModel->grandeur_en(std::max(0, m_table->currentIndex().column()));
        if (id) dialogs::nouvelle_incertitude(m_win, id);
        else m_win.affiche_statut(u"Sélectionnez d’abord la colonne d’une mesure."_s);
    });
    m_statsColonne = bouton(outils, "stats", u"Statistiques…"_s, [this] {
        if (m_table) m_win.ouvre_statistiques(m_tableModel->grandeur_en(std::max(0, m_table->currentIndex().column())));
    });
    bouton(outils, "csv", u"Importer un CSV…"_s, [this] { m_win.importe_csv(m_tableId ? std::optional(m_tableId) : std::nullopt); });

    auto *tracer = page(u"Tracer"_s, "chart");
    auto *axes = new QFormLayout;
    tracer->addLayout(axes);
    m_x = liste(axes, u"En abscisse (x)"_s, u"abscisseGuide"_s);
    m_y = liste(axes, u"En ordonnée (y)"_s, u"ordonneeGuide"_s);
    auto *commandes = new FlowLayout;
    tracer->addLayout(commandes);
    bouton(commandes, "swap", u"Permuter les axes"_s, [this] {
        const int x = m_x->currentIndex();
        m_x->setCurrentIndex(m_y->currentIndex());
        m_y->setCurrentIndex(x);
    });
    m_tracer = bouton(commandes, "chart", u"Tracer les mesures"_s, [this] { trace(); });
    m_etatTrace = etiquette_attenuee({}, this);
    tracer->addWidget(m_etatTrace);
    tracer->addStretch();

    auto *exploiter = page(u"Exploiter"_s, "model");
    auto *form = new QFormLayout;
    exploiter->addLayout(form);
    m_courbes = liste(form, u"Courbe"_s, u"courbeGuide"_s);
    m_courbes->setPlaceholderText(u"Tracez une courbe dans l’onglet Tracer"_s);
    m_modele = liste(form, u"Modèle"_s, u"modeleGuide"_s);
    using M = fit::ModelKind;
    for (auto type : {M::Linear, M::Affine, M::Parabola, M::Constant, M::Power, M::ExpX,
                      M::Charge, M::Discharge, M::Ln, M::Sine}) {
        M modele;
        modele.type = type;
        QString nom = modele.etiquette();
        if (type == M::Linear) nom = u"Linéaire (proportionnel)"_s;
        if (type == M::Parabola) nom = u"Parabole (degré 2)"_s;
        m_modele->addItem(nom, type);
    }
    m_aideModele = new QLabel;
    m_aideModele->setWordWrap(true);
    m_aideModele->setTextInteractionFlags(Qt::TextSelectableByMouse);
    exploiter->addWidget(m_aideModele);
    m_ajuster = bouton(exploiter, "model", u"Ajuster aux mesures"_s, [this] { ajuste(); });
    m_resultat = new QLabel;
    m_resultat->setWordWrap(true);
    m_resultat->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_resultat->setTextFormat(Qt::PlainText);
    exploiter->addWidget(m_resultat);
    m_voirResidus = new QCheckBox(u"Afficher les résidus (mesure − modèle)"_s);
    m_voirResidus->setObjectName(u"residusGuide"_s);
    exploiter->addWidget(m_voirResidus);
    connect(m_voirResidus, &QCheckBox::toggled, this, [this] { affiche_resultat(); });

    auto *analyses = new FlowLayout;
    exploiter->addLayout(analyses);
    m_derivee = bouton(analyses, "derivative", u"Dérivée…"_s, [this] { dialogs::derivee(m_win, false, axes_choisis()); });
    m_primitive = bouton(analyses, "integral", u"Intégrale…"_s, [this] { dialogs::integrale(m_win, axes_choisis()); });
    m_tangentes = bouton(analyses, "tangent", u"Tangentes parallèles…"_s, [this] {
        const auto c = m_courbes->currentData().value<CourbeTracee>();
        dialogs::tangentes(m_win, m_grapheId, c);
    });
    m_maximum = bouton(analyses, "slope", u"Maximum de pente…"_s, [this] {
        auto c = m_courbes->currentData().value<CourbeTracee>();
        if (!c.x) std::tie(c.x, c.y) = axes_choisis();
        dialogs::methode_derivee(m_win, m_grapheId, c);
    });
    m_stats = bouton(analyses, "stats", u"Statistiques…"_s, [this] {
        const auto [x, y] = axes_choisis();
        if (y || x) m_win.ouvre_statistiques(y ? y : x);
    });
    exploiter->addStretch();

    auto *conclure = page(u"Conclure"_s, "pencil");
    conclure->addWidget(new QLabel(u"Que permettent de conclure vos mesures ?"_s));
    m_conclusion = new QPlainTextEdit;
    m_conclusion->setObjectName(u"conclusionTP"_s);
    m_conclusion->setPlaceholderText(u"Citez un résultat avec son unité et expliquez ce qu’il montre."_s);
    conclure->addWidget(m_conclusion);
    auto *sources = new QFormLayout;
    conclure->addLayout(sources);
    auto source = [sources](const QString &titre, const QString &nom, QCheckBox *&option) {
        option = new QCheckBox(titre);
        option->setChecked(true);
        auto *liste = new QComboBox;
        liste->setObjectName(nom);
        liste->setAccessibleName(titre);
        sources->addRow(option, liste);
        return liste;
    };
    m_tableRapport = source(u"Inclure le tableur"_s, u"tableRapport"_s, m_inclureTable);
    m_grapheRapport = source(u"Inclure le graphe"_s, u"grapheRapport"_s, m_inclureGraphe);
    auto *sorties = new FlowLayout;
    conclure->addLayout(sorties);
    bouton(sorties, "save", u"Enregistrer la séance…"_s, [this] { m_win.enregistre(false); });
    bouton(sorties, "pdf", u"Compte rendu court…"_s, [this] {
        m_win.rapport_pdf(false, m_inclureTable->isChecked() ? m_tableId : 0, m_inclureGraphe->isChecked() ? m_grapheId : 0);
    });
    m_copierGraphe = bouton(sorties, "copy", u"Copier le graphe"_s, [this] { m_win.copie_image_graphe(m_grapheId); });
    m_exporterTable = bouton(sorties, "export", u"Exporter le tableur…"_s, [this] {
        if (const auto *t = m_win.depot().ws.tableur(m_tableId)) m_win.exporte_csv(t->columns);
    });
    connect(m_conclusion, &QPlainTextEdit::textChanged, this, [this] {
        const QString texte = m_conclusion->toPlainText();
        m_win.depot().edite([&](Seance &ws) { ws.meta.notes = texte; });
    });

    m_apercu = new QWidget(split);
    m_trace = new QVBoxLayout(m_apercu);
    auto *vues = new QFormLayout;
    m_trace->addLayout(vues);
    m_graphes = liste(vues, u"Graphe affiché"_s, u"grapheGuide"_s);
    m_residus = new CourbeResultat(m_apercu);
    m_residus->setMinimumHeight(theme::px(140));
    m_trace->addWidget(m_residus, 1);
    m_residus->hide();
    split->setChildrenCollapsible(false);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    connect(&m_win.depot(), &Store::affichage_change, this, [this] { affiche_resultat(); });
    connect(m_courbes, &QComboBox::currentIndexChanged, this, [this] {
        retrouve_modele();
        affiche_resultat();
    });
    connect(m_etapes, &QTabWidget::currentChanged, this, [this] { affiche_etape(); });
    for (const auto &[source, destination] : {std::pair(m_tables, m_tableRapport), std::pair(m_graphes, m_grapheRapport)}) {
        destination->setModel(source->model());
        connect(destination, &QComboBox::activated, source, &QComboBox::setCurrentIndex);
    }
    for (auto *c : {m_graphes, m_tables})
        connect(c, &QComboBox::currentIndexChanged, this, [this] { rafraichit(); });
    for (auto *c : {m_x, m_y, m_modele})
        connect(c, &QComboBox::currentIndexChanged, this, [this] { affiche_resultat(); });
}

void Guide::rafraichit()
{
    const auto &ws = m_win.depot().ws;
    Choix tables, graphes, grandeurs;
    for (const auto &t : ws.tables) tables.emplace_back(t.id, t.name);
    for (const auto &g : ws.graphs) graphes.emplace_back(g.id, g.name);
    for (const auto &q : ws.doc.quantities)
        if (!ws.doc.proprietaire_incertitude(q.id))
            grandeurs.emplace_back(q.id, etiquette_avec_unite(q.name, q.unit));
    actualise(m_tables, tables);
    actualise(m_graphes, graphes);
    actualise(m_x, grandeurs);
    actualise(m_y, grandeurs, 1);
    if (m_tableId != selection(m_tables)) {
        if (m_table) { m_table->hide(); m_table->deleteLater(); }
        m_table = nullptr;
        m_tableModel = nullptr;
        m_tableId = selection(m_tables);
        if (const auto *table = ws.tableur(m_tableId)) {
            std::vector<int> axes;
            for (QId id : table->columns) {
                const int index = m_x->findData(QVariant::fromValue<qulonglong>(id));
                if (index >= 0) axes.push_back(index);
                if (axes.size() == 2) break;
            }
            if (axes.size() == 2) {
                QSignalBlocker bx(m_x), by(m_y);
                m_x->setCurrentIndex(axes[0]);
                m_y->setCurrentIndex(axes[1]);
            }
            m_tableModel = new TableModel(m_win.depot(), m_tableId);
            m_table = new TableGrid(m_win, m_tableModel);
            m_tableModel->setParent(m_table);
            m_mesures->addWidget(m_table, 1);
            connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, m_table, [this] {
                m_statsColonne->setEnabled(m_table && m_tableModel->grandeur_en(std::max(0, m_table->currentIndex().column())));
            });
        }
    }
    if (m_tableModel) {
        m_tableModel->recharge();
        m_table->repartit_colonnes();
    }
    if (m_grapheId != selection(m_graphes)) {
        if (m_graphe) { m_graphe->hide(); m_graphe->deleteLater(); }
        m_graphe = nullptr;
        m_grapheId = selection(m_graphes);
        if (m_grapheId) {
            m_graphe = new GraphCanvas(m_win, m_grapheId);
            m_graphe->setContextMenuPolicy(Qt::NoContextMenu);
            m_trace->insertWidget(1, m_graphe, 2);
        }
    }
    actualise_courbes();
    m_tableRapport->setCurrentIndex(m_tables->currentIndex());
    m_grapheRapport->setCurrentIndex(m_graphes->currentIndex());
    m_tableRapport->setEnabled(m_tableId);
    m_grapheRapport->setEnabled(m_grapheId);
    m_inclureTable->setEnabled(m_tableId);
    m_inclureGraphe->setEnabled(m_grapheId);
    m_statsColonne->setEnabled(m_table && m_tableModel->grandeur_en(std::max(0, m_table->currentIndex().column())));
    m_exporterTable->setEnabled(m_tableId);
    m_copierGraphe->setEnabled(m_grapheId);
    if (m_conclusion->toPlainText() != ws.meta.notes) {
        QSignalBlocker bloque(m_conclusion);
        m_conclusion->setPlainText(ws.meta.notes);
    }
    affiche_etape();
}

void Guide::ouvre_tableur(ViewId id)
{
    rafraichit();
    m_tables->setCurrentIndex(m_tables->findData(QVariant::fromValue<qulonglong>(id)));
    m_etapes->setCurrentIndex(0);
}

void Guide::ouvre_graphe(ViewId id)
{
    rafraichit();
    m_graphes->setCurrentIndex(m_graphes->findData(QVariant::fromValue<qulonglong>(id)));
    m_etapes->setCurrentIndex(1);
}

void Guide::reinitialise()
{
    for (auto *c : {m_tables, m_graphes, m_x, m_y, m_courbes}) {
        QSignalBlocker bloque(c);
        c->clear();
    }
    if (m_graphe) { m_graphe->hide(); m_graphe->deleteLater(); }
    if (m_table) { m_table->hide(); m_table->deleteLater(); }
    m_graphe = nullptr;
    m_table = nullptr;
    m_tableModel = nullptr;
    m_tableId = m_grapheId = 0;
    m_sourceCourbes = 0;
    m_modele->setCurrentIndex(0);
    m_voirResidus->setChecked(false);
    m_etapes->setCurrentIndex(0);
}

void Guide::affiche_etape()
{
    const int etape = m_etapes->currentIndex();
    m_apercu->setVisible(m_grapheId && (etape == 1 || etape == 2));
    affiche_resultat();
}

void Guide::actualise_courbes()
{
    const auto &ws = m_win.depot().ws;
    const auto *g = ws.graphe(m_grapheId);
    std::vector<std::pair<CourbeTracee, QString>> courbes;
    if (g) courbes = dialogs::courbes_visibles(*g, ws.doc);
    const QVariant avant = m_courbes->currentData();
    const bool memeGraphe = m_sourceCourbes == m_grapheId;
    actualise(m_courbes, courbes, 0, memeGraphe);
    m_courbes->setEnabled(!courbes.empty());
    m_sourceCourbes = m_grapheId;
    if (!memeGraphe || avant != m_courbes->currentData()) retrouve_modele();
}

std::pair<QId, QId> Guide::axes_choisis() const
{
    const auto c = m_courbes->currentData().value<CourbeTracee>();
    return c.x ? std::pair(c.x, c.y) : std::pair<QId, QId>(selection(m_x), selection(m_y));
}

void Guide::retrouve_modele()
{
    const auto c = m_courbes->currentData().value<CourbeTracee>();
    const auto *g = m_win.depot().ws.graphe(m_grapheId);
    int index = 0;
    if (g && c.x) {
        for (const auto &f : g->plots.at(c.panneau).fits) {
            if (f.x != c.x || f.y != c.y || f.side != c.cote) continue;
            const int i = m_modele->findData(f.kind.type);
            if (i >= 0) index = i;
        }
    }
    QSignalBlocker bloque(m_modele);
    m_modele->setCurrentIndex(index);
}

void Guide::trace()
{
    const auto &ws = m_win.depot().ws;
    const QId x = selection(m_x), y = selection(m_y);
    if (!x || !y || x == y) return;
    for (const auto &g : ws.graphs)
        if (!g.plots.empty() && !g.plots.front().curves.empty()) {
            const auto &c = g.plots.front().curves.front();
            if (c.x == x && c.y == y && c.visible) {
                ouvre_graphe(g.id);
                return;
            }
        }
    m_win.nouveau_graphe(std::pair(x, y));
}

const FitSpec *Guide::modele_actuel() const
{
    const auto *g = m_win.depot().ws.graphe(m_grapheId);
    const auto c = m_courbes->currentData().value<CourbeTracee>();
    if (!c.x || !g || c.panneau >= g->traces_visibles()) return nullptr;
    for (const auto &f : g->plots[c.panneau].fits)
        if (f.x == c.x && f.y == c.y && f.side == c.cote && f.kind.type == m_modele->currentData().toInt()) return &f;
    return nullptr;
}

void Guide::ajuste()
{
    if (modele_actuel()) { affiche_resultat(); return; }
    const auto *g = m_win.depot().ws.graphe(m_grapheId);
    const auto c = m_courbes->currentData().value<CourbeTracee>();
    if (!c.x || !g || c.panneau >= g->traces_visibles()) return;
    fit::ModelKind modele;
    modele.type = static_cast<fit::ModelKind::Type>(m_modele->currentData().toInt());
    m_win.depot().edite([&](Seance &ws) {
        auto &p = ws.graphe_modifiable(m_grapheId)->plots[c.panneau];
        auto f = FitSpec::fabrique(modele, c.x, c.y, p.couleur_suivante());
        f.side = c.cote;
        f.draw = FitDraw::Points;
        p.fits.push_back(std::move(f));
    });
}

void Guide::affiche_resultat()
{
    const Store &st = m_win.depot();
    const auto &doc = st.ws.doc;
    const int etape = m_etapes->currentIndex();
    if (etape != 2) m_residus->hide();
    if (etape == 1) {
        const QId x = selection(m_x), y = selection(m_y);
        const auto &xs = doc.valeurs(x), &ys = doc.valeurs(y);
        size_t n = 0;
        if (x != y)
            for (size_t i = 0; i < std::min(xs.size(), ys.size()); ++i)
                if (std::isfinite(xs[i]) && std::isfinite(ys[i])) ++n;
        m_tracer->setEnabled(n > 0);
        const QString couples = n == 1 ? u"1 couple de mesures"_s : u"%1 couples de mesures"_s.arg(n);
        m_etatTrace->setText(x && x == y ? u"Choisissez deux grandeurs différentes."_s :
                             n ? u"%1 pour tracer %2 en fonction de %3."_s.arg(couples, doc.nom(y), doc.nom(x)) :
                                 u"Saisissez x et y sur une même ligne du tableur."_s);
    }
    if (etape != 2) return;
    const auto [x, y] = axes_choisis();
    const bool paire = x && y && x != y;
    const bool courbe = m_courbes->currentIndex() >= 0;
    m_ajuster->setEnabled(courbe);
    m_derivee->setEnabled(paire);
    m_primitive->setEnabled(paire);
    m_tangentes->setEnabled(courbe);
    m_maximum->setEnabled(paire);
    m_stats->setEnabled(x || y);
    fit::ModelKind modele;
    modele.type = static_cast<fit::ModelKind::Type>(m_modele->currentData().toInt());
    QString indication = modele.gabarit();
    switch (modele.type) {
    case fit::ModelKind::Linear: indication += u" · La droite passe par l’origine."_s; break;
    case fit::ModelKind::Affine: indication += u" · a est la pente et b l’ordonnée à l’origine."_s; break;
    case fit::ModelKind::Parabola: indication += u" · La parabole s’ouvre vers le haut si a est positif."_s; break;
    case fit::ModelKind::Charge:
    case fit::ModelKind::Discharge:
        indication.replace(u"V0"_s, u"y₀"_s);
        indication += u" · À partir de x = Δ, l’écart au palier est divisé par e chaque fois que x augmente de τ."_s;
        break;
    default: break;
    }
    m_aideModele->setText(indication);
    const auto *f = modele_actuel();
    QString resultat;
    if (f && f->result) {
        const auto &r = *f->result;
        const auto *qx = doc.obtient(f->x), *qy = doc.obtient(f->y);
        if (qx && qy) {
            resultat = u"%1 en fonction de %2 · %3 points utilisés"_s.arg(qy->name, qx->name).arg(r.n);
            for (size_t i = 0; i < r.params.size(); ++i) {
                const QString unite = f->kind.unite_parametre(static_cast<int>(i), qx->unit, qy->unit);
                const QString &nom = f->paramNames.at(i);
                const bool palier = f->kind.type == fit::ModelKind::Charge || f->kind.type == fit::ModelKind::Discharge;
                resultat += u"\n%1 = %2"_s.arg(palier && nom == u"V0" ? u"y₀"_s : nom, r.texte_parametre(i, unite, st.style()));
            }
            if (r.residusNumeriques)
                resultat += u"\nLes points coïncident avec le modèle à la précision du calcul. Les incertitudes des mesures restent à déterminer expérimentalement."_s;
            else if (std::isfinite(r.residualStd))
                resultat += u"\nÉcart-type des résidus : %1 %2. Ajustement sans pondération par les incertitudes."_s
                                .arg(format_significatif(r.residualStd, 3, st.style()), unite_lisible(qy->unit));
            else
                resultat += u"\nAjoutez des points pour estimer la dispersion autour du modèle."_s;
        }
        if (!r.warning.isEmpty()) resultat += u'\n' + r.warning;
    } else if (f) resultat = f->resultError;
    else resultat = courbe ? u"Choisissez un modèle, puis ajustez-le aux points de la courbe."_s : u"Tracez d’abord les mesures à exploiter."_s;
    m_voirResidus->setEnabled(f && f->result && !f->result->residusNumeriques);
    const bool residus = f && f->result && !f->result->residusNumeriques && m_voirResidus->isChecked();
    m_residus->setVisible(residus);
    if (residus) {
        RenderOptions options;
        options.style = st.style();
        options.theme = theme::est_sombre() && st.prefs.darkPlots ? PlotTheme::theme_sombre() : PlotTheme::clair();
        m_residus->definit_trace(trace_residus(doc, *f), options);
        resultat += u"\nLes écarts sont-ils répartis autour de zéro ou dessinent-ils une tendance ?"_s;
    }
    m_resultat->setText(resultat);
}
