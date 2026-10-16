#include "app/docking.h"

#include "app/theme.h"

#include <QDockWidget>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QTabBar>

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>

using namespace Qt::StringLiterals;

namespace docking {

namespace {

int extent(const QRect &r, Qt::Orientation o)
{
    return o == Qt::Horizontal ? r.width() : r.height();
}

int startOf(const QRect &r, Qt::Orientation o)
{
    return o == Qt::Horizontal ? r.left() : r.top();
}

/// Fin exclue.
int endOf(const QRect &r, Qt::Orientation o)
{
    return startOf(r, o) + extent(r, o);
}

/// Bande de `r` de `start` à `start + length` dans le sens `o`.
QRect span(const QRect &r, Qt::Orientation o, int start, int length)
{
    return o == Qt::Horizontal ? QRect(start, r.top(), length, r.height()) : QRect(r.left(), start, r.width(), length);
}

Qt::Orientation orientationOf(Side s)
{
    return s == Side::Left || s == Side::Right ? Qt::Horizontal : Qt::Vertical;
}

/// La fenêtre posée vient avant la cible (à gauche ou au-dessus).
bool first(Side s)
{
    return s == Side::Left || s == Side::Top;
}

bool contains(const std::vector<QDockWidget *> &v, const QDockWidget *d)
{
    return std::find(v.begin(), v.end(), d) != v.end();
}

Node leaf(QDockWidget *d, const QRect &r)
{
    Node n;
    n.tabs = {d};
    n.current = d;
    n.rect = r;
    return n;
}

/// Découpe les piles en colonnes, sinon en lignes : la disposition de Qt est faite de partages
/// successifs, il existe toujours une découpe sur toute la hauteur ou toute la largeur.
Node build(std::vector<Node> items)
{
    if (items.size() == 1)
        return std::move(items.front());
    QRect all;
    for (const Node &n : items)
        all |= n.rect;
    for (Qt::Orientation o : {Qt::Horizontal, Qt::Vertical}) {
        std::stable_sort(items.begin(), items.end(), [o](const Node &a, const Node &b) { return startOf(a.rect, o) < startOf(b.rect, o); });
        // une part commence là où aucune pile de la précédente ne déborde
        std::vector<size_t> cuts;
        int reach = INT_MIN;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0 && startOf(items[i].rect, o) >= reach)
                cuts.push_back(i);
            reach = std::max(reach, endOf(items[i].rect, o));
        }
        if (cuts.empty())
            continue;
        cuts.push_back(items.size());
        Node n;
        n.orientation = o;
        n.rect = all;
        size_t from = 0;
        for (size_t c : cuts) {
            std::vector<Node> part(std::make_move_iterator(items.begin() + static_cast<std::ptrdiff_t>(from)),
                                   std::make_move_iterator(items.begin() + static_cast<std::ptrdiff_t>(c)));
            n.children.push_back(build(std::move(part)));
            from = c;
        }
        return n;
    }
    // aucune découpe (ne devrait pas arriver) : en colonnes, de gauche à droite
    std::stable_sort(items.begin(), items.end(), [](const Node &a, const Node &b) { return a.rect.left() < b.rect.left(); });
    Node n;
    n.rect = all;
    n.children = std::move(items);
    return n;
}

/// Range les enfants de `n` dans `r`, au prorata de leur place actuelle ; l'enfant `fixed` prend
/// `fixedExtent`. Les sous-arbres sont rangés de même.
void fit(Node &n, const QRect &r, int sep, int fixed = -1, int fixedExtent = 0)
{
    n.rect = r;
    if (n.leaf())
        return;
    const Qt::Orientation o = n.orientation;
    const int k = static_cast<int>(n.children.size());
    const int avail = std::max(k, extent(r, o) - sep * (k - 1));
    const int fixedPx = fixed >= 0 ? std::clamp(fixedExtent, 1, std::max(1, avail - 60 * (k - 1))) : 0;
    double total = 0;
    for (int i = 0; i < k; ++i)
        if (i != fixed)
            total += std::max(1, extent(n.children[static_cast<size_t>(i)].rect, o));
    std::vector<int> px(static_cast<size_t>(k));
    const double rest = avail - fixedPx;
    double acc = 0;
    int given = 0;
    for (int i = 0; i < k; ++i) {
        if (i == fixed) {
            px[static_cast<size_t>(i)] = fixedPx;
            continue;
        }
        acc += std::max(1, extent(n.children[static_cast<size_t>(i)].rect, o));
        const int upto = static_cast<int>(std::lround(rest * acc / total));
        px[static_cast<size_t>(i)] = std::max(1, upto - given);
        given = upto;
    }
    int pos = startOf(r, o);
    for (int i = 0; i < k; ++i) {
        const int e = px[static_cast<size_t>(i)];
        fit(n.children[static_cast<size_t>(i)], span(r, o, pos, e), sep);
        pos += e + sep;
    }
}

/// Chemin (rangs des enfants) vers la feuille qui contient `d`.
bool pathTo(const Node &n, const QDockWidget *d, std::vector<size_t> &path)
{
    if (n.leaf())
        return contains(n.tabs, d);
    for (size_t i = 0; i < n.children.size(); ++i) {
        path.push_back(i);
        if (pathTo(n.children[i], d, path))
            return true;
        path.pop_back();
    }
    return false;
}

Node &nodeAt(Node &root, const std::vector<size_t> &path, size_t depth)
{
    Node *n = &root;
    for (size_t i = 0; i < depth; ++i)
        n = &n->children[path[i]];
    return *n;
}

/// Retire `d` de l'arbre ; sa place va aux fenêtres voisines.
bool remove(Node &root, QDockWidget *d, int sep)
{
    std::vector<size_t> path;
    if (!pathTo(root, d, path))
        return false;
    Node &lf = nodeAt(root, path, path.size());
    if (lf.tabs.size() > 1) {
        const auto it = std::find(lf.tabs.begin(), lf.tabs.end(), d);
        const size_t at = static_cast<size_t>(it - lf.tabs.begin());
        lf.tabs.erase(it);
        if (lf.current == d || !contains(lf.tabs, lf.current))
            lf.current = lf.tabs[std::min(at, lf.tabs.size() - 1)];
        return true;
    }
    // fenêtre seule de l'espace de travail : il ne resterait rien
    if (path.empty())
        return false;
    const size_t index = path.back();
    path.pop_back();
    Node &parent = nodeAt(root, path, path.size());
    parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(index));
    fit(parent, parent.rect, sep);
    if (parent.children.size() > 1)
        return true;
    // une seule part restante : elle prend la place du partage
    Node only = std::move(parent.children.front());
    fit(only, parent.rect, sep);
    if (path.empty()) {
        root = std::move(only);
        return true;
    }
    const size_t pindex = path.back();
    path.pop_back();
    Node &grand = nodeAt(root, path, path.size());
    const auto pos = grand.children.begin() + static_cast<std::ptrdiff_t>(pindex);
    if (!only.leaf() && only.orientation == grand.orientation) {
        // partage de même sens que le parent : ses parts s'y rangent directement
        const auto at = grand.children.erase(pos);
        grand.children.insert(at, std::make_move_iterator(only.children.begin()), std::make_move_iterator(only.children.end()));
    } else {
        *pos = std::move(only);
    }
    return true;
}

/// Pose `d` (déjà retiré de l'arbre) sur la cible.
bool insert(Node &root, QDockWidget *d, const Target &t, QSize dragged, int sep)
{
    if (!t.group) {
        const Qt::Orientation o = orientationOf(t.side);
        const QRect ws = root.rect;
        const int e = edgeExtent(ws, dragged, t.side);
        if (root.leaf() || root.orientation != o) {
            Node n;
            n.orientation = o;
            n.rect = ws;
            n.children.push_back(std::move(root));
            root = std::move(n);
        }
        const size_t at = first(t.side) ? 0 : root.children.size();
        root.children.insert(root.children.begin() + static_cast<std::ptrdiff_t>(at), leaf(d, span(ws, o, startOf(ws, o), e)));
        fit(root, ws, sep, static_cast<int>(at), e);
        return true;
    }
    std::vector<size_t> path;
    if (!pathTo(root, t.group, path))
        return false;
    Node &target = nodeAt(root, path, path.size());
    if (t.side == Side::Center) {
        target.tabs.push_back(d);
        target.current = d;
        return true;
    }
    // la cible et la fenêtre posée se partagent la place de la cible
    const Qt::Orientation o = orientationOf(t.side);
    const QRect r = target.rect;
    const int half = std::max(1, (extent(r, o) - sep) / 2);
    const int other = std::max(1, extent(r, o) - sep - half);
    if (!path.empty()) {
        Node &parent = nodeAt(root, path, path.size() - 1);
        if (parent.orientation == o) {
            const size_t index = path.back();
            target.rect = span(r, o, startOf(r, o), other);
            parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(first(t.side) ? index : index + 1),
                                   leaf(d, span(r, o, startOf(r, o), half)));
            fit(parent, parent.rect, sep);
            return true;
        }
    }
    Node n;
    n.orientation = o;
    Node old = std::move(target);
    old.rect = span(r, o, startOf(r, o), other);
    Node added = leaf(d, span(r, o, startOf(r, o), half));
    if (first(t.side)) {
        n.children.push_back(std::move(added));
        n.children.push_back(std::move(old));
    } else {
        n.children.push_back(std::move(old));
        n.children.push_back(std::move(added));
    }
    fit(n, r, sep);
    target = std::move(n);
    return true;
}

QDockWidget *firstDock(const Node &n)
{
    return n.leaf() ? n.tabs.front() : firstDock(n.children.front());
}

/// Fenêtre par laquelle régler la taille de `c` dans le sens `o` : de préférence une feuille
/// directe, que Qt ne range dans aucun partage plus profond du même sens.
QDockWidget *sizer(const Node &c)
{
    if (c.leaf())
        return c.tabs.front();
    for (const Node &g : c.children)
        if (g.leaf())
            return g.tabs.front();
    return firstDock(c);
}

void splitAll(QMainWindow *win, const Node &n)
{
    if (n.leaf())
        return;
    for (size_t i = 1; i < n.children.size(); ++i)
        win->splitDockWidget(firstDock(n.children[i - 1]), firstDock(n.children[i]), n.orientation);
    for (const Node &c : n.children)
        splitAll(win, c);
}

void tabifyAll(QMainWindow *win, const Node &n)
{
    if (n.leaf()) {
        for (size_t k = 1; k < n.tabs.size(); ++k)
            win->tabifyDockWidget(n.tabs.front(), n.tabs[k]);
        return;
    }
    for (const Node &c : n.children)
        tabifyAll(win, c);
}

/// Tailles, du haut de l'arbre vers le bas : Qt reporte la taille donnée à une fenêtre sur les
/// partages qui la contiennent, les partages plus profonds corrigent ensuite les leurs.
void resizeAll(QMainWindow *win, const Node &n)
{
    if (n.leaf())
        return;
    QList<QDockWidget *> docks;
    QList<int> sizes;
    for (const Node &c : n.children) {
        docks << sizer(c);
        sizes << std::max(1, extent(c.rect, n.orientation));
    }
    win->resizeDocks(docks, sizes, n.orientation);
    for (const Node &c : n.children)
        resizeAll(win, c);
}

void raiseAll(const Node &n)
{
    if (n.leaf()) {
        if (n.tabs.size() > 1)
            (n.current && contains(n.tabs, n.current) ? n.current : n.tabs.front())->raise();
        return;
    }
    for (const Node &c : n.children)
        raiseAll(c);
}

void collect(const Node &n, std::vector<QDockWidget *> &out)
{
    if (n.leaf()) {
        out.insert(out.end(), n.tabs.begin(), n.tabs.end());
        return;
    }
    for (const Node &c : n.children)
        collect(c, out);
}

/// Fenêtre qui désigne la pile : celle affichée, sauf si c'est la fenêtre glissée.
QDockWidget *representative(const Group &g, const QDockWidget *dragged)
{
    if (g.current != dragged)
        return g.current;
    for (QDockWidget *d : g.tabs)
        if (d != dragged)
            return d;
    return nullptr;
}

QString quoted(const QString &s)
{
    return u"« "_s + s + u" »"_s;
}

} // namespace

std::vector<Group> groups(const QMainWindow *win, const std::vector<QDockWidget *> &docks)
{
    std::vector<QDockWidget *> docked;
    for (QDockWidget *d : docks)
        if (d && !d->isFloating() && win->dockWidgetArea(d) != Qt::NoDockWidgetArea)
            docked.push_back(d);
    // les barres d'onglets de la fenêtre principale rangent la fenêtre de chaque onglet dans ses données
    std::map<const QDockWidget *, std::pair<QTabBar *, int>> tabOf;
    for (QTabBar *bar : win->findChildren<QTabBar *>(Qt::FindDirectChildrenOnly)) {
        if (!bar->isVisible())
            continue;
        for (int i = 0; i < bar->count(); ++i) {
            const auto *d = reinterpret_cast<const QDockWidget *>(bar->tabData(i).value<quintptr>());
            if (contains(docked, d))
                tabOf[d] = {bar, i};
        }
    }
    std::vector<Group> out;
    std::set<const QDockWidget *> seen;
    for (QDockWidget *d : docked) {
        if (seen.count(d))
            continue;
        Group g;
        g.tabs = {d};
        for (QDockWidget *o : win->tabifiedDockWidgets(d))
            if (contains(docked, o) && !contains(g.tabs, o))
                g.tabs.push_back(o);
        auto rank = [&](const QDockWidget *x) {
            const auto it = tabOf.find(x);
            return it == tabOf.end() ? INT_MAX : it->second.second;
        };
        std::stable_sort(g.tabs.begin(), g.tabs.end(), [&](const QDockWidget *a, const QDockWidget *b) { return rank(a) < rank(b); });
        seen.insert(g.tabs.begin(), g.tabs.end());
        if (g.tabs.size() > 1)
            for (QDockWidget *t : g.tabs)
                if (const auto it = tabOf.find(t); it != tabOf.end()) {
                    g.tabBar = it->second.first;
                    break;
                }
        // fenêtre affichée : celle de l'onglet courant (Qt range les autres hors de la fenêtre)
        if (g.tabBar) {
            const auto *cur = reinterpret_cast<const QDockWidget *>(g.tabBar->tabData(g.tabBar->currentIndex()).value<quintptr>());
            for (QDockWidget *t : g.tabs)
                if (t == cur)
                    g.current = t;
        }
        for (QDockWidget *t : g.tabs)
            if (!g.current && t->isVisible() && win->rect().intersects(t->geometry()))
                g.current = t;
        if (!g.current)
            g.current = g.tabs.front();
        g.rect = g.current->geometry();
        if (g.tabBar)
            g.rect |= g.tabBar->geometry();
        out.push_back(std::move(g));
    }
    return out;
}

Node capture(const QMainWindow *win, const std::vector<QDockWidget *> &docks)
{
    std::vector<Node> leaves;
    for (const Group &g : groups(win, docks)) {
        Node n;
        n.tabs = g.tabs;
        n.current = g.current;
        n.rect = g.rect;
        leaves.push_back(std::move(n));
    }
    if (leaves.empty())
        return {};
    return build(std::move(leaves));
}

std::vector<QDockWidget *> docksIn(const Node &n)
{
    std::vector<QDockWidget *> out;
    collect(n, out);
    return out;
}

void apply(QMainWindow *win, const Node &root)
{
    const std::vector<QDockWidget *> all = docksIn(root);
    if (all.empty())
        return;
    const bool updates = win->updatesEnabled();
    win->setUpdatesEnabled(false);
    for (QDockWidget *d : all)
        win->removeDockWidget(d);
    win->addDockWidget(Qt::LeftDockWidgetArea, firstDock(root));
    splitAll(win, root);
    tabifyAll(win, root);
    for (QDockWidget *d : all)
        d->show();
    resizeAll(win, root);
    raiseAll(root);
    win->setUpdatesEnabled(updates);
}

int edgeExtent(const QRect &workspace, QSize dragged, Side side)
{
    const Qt::Orientation o = orientationOf(side);
    const int total = extent(workspace, o);
    const int current = o == Qt::Horizontal ? dragged.width() : dragged.height();
    return std::clamp(current, total / 5, std::max(total / 5, total * 2 / 5));
}

QRect preview(const Target &t, const std::vector<Group> &groups, const QRect &workspace, QSize dragged)
{
    QRect r = workspace;
    int e = 0;
    if (t.group) {
        const auto g = std::find_if(groups.begin(), groups.end(), [&](const Group &x) { return contains(x.tabs, t.group); });
        if (g == groups.end())
            return {};
        r = g->rect;
        e = extent(r, orientationOf(t.side)) / 2;
    } else if (t.side != Side::Center) {
        e = edgeExtent(workspace, dragged, t.side);
    }
    switch (t.side) {
    case Side::Center: return r;
    case Side::Left: return QRect(r.left(), r.top(), e, r.height());
    case Side::Right: return QRect(r.left() + r.width() - e, r.top(), e, r.height());
    case Side::Top: return QRect(r.left(), r.top(), r.width(), e);
    case Side::Bottom: return QRect(r.left(), r.top() + r.height() - e, r.width(), e);
    }
    return r;
}

bool move(Node &root, QDockWidget *dock, const Target &t, QSize dragged, int separator)
{
    if (t.group == dock)
        return false;
    // sur une copie : l'arbre reste tel quel si la cible n'existe plus
    Node copy = root;
    if (!remove(copy, dock, separator) || !insert(copy, dock, t, dragged, separator))
        return false;
    root = std::move(copy);
    return true;
}

void insertColumn(Node &root, QDockWidget *dock, size_t index, int width, int separator)
{
    const QRect ws = root.rect;
    if (root.leaf() || root.orientation != Qt::Horizontal) {
        Node n;
        n.orientation = Qt::Horizontal;
        n.rect = ws;
        n.children.push_back(std::move(root));
        root = std::move(n);
    }
    index = std::min(index, root.children.size());
    root.children.insert(root.children.begin() + static_cast<std::ptrdiff_t>(index), leaf(dock, span(ws, Qt::Horizontal, ws.left(), width)));
    fit(root, ws, separator, static_cast<int>(index), width);
}

// --- calque des repères ------------------------------------------------------------------------

Overlay::Overlay(QMainWindow *win, QDockWidget *dragged, const std::vector<QDockWidget *> &docks)
    : QWidget(win), m_dragged(dragged), m_groups(groups(win, docks))
{
    for (const Group &g : m_groups)
        m_workspace |= g.rect;
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setGeometry(win->rect());
    hide();
}

void Overlay::start(QPoint globalPos)
{
    show();
    raise();
    setFocus();
    grabMouse(Qt::ClosedHandCursor);
    grabKeyboard();
    hover(mapFromGlobal(globalPos));
}

bool Overlay::canDropOn(const Group &g) const
{
    return representative(g, m_dragged) != nullptr;
}

const Group *Overlay::groupAt(QPoint pos) const
{
    for (const Group &g : m_groups)
        if (g.rect.contains(pos))
            return &g;
    return nullptr;
}

std::vector<std::pair<Target, QRect>> Overlay::markers(QPoint pos) const
{
    std::vector<std::pair<Target, QRect>> out;
    // sur la pile survolée : en onglet au centre, à côté tout autour
    if (const Group *g = groupAt(pos); g && canDropOn(*g)) {
        const QRect r = g->rect;
        // repères à la taille de l'interface (agrandissement compris)
        const int s = std::clamp(std::min(r.width(), r.height()) / 5, theme::px(26), theme::px(44));
        const int gap = std::max(3, s / 8);
        const QPoint c = r.center();
        auto box = [&](int dx, int dy) { return QRect(c.x() - s / 2 + dx * (s + gap), c.y() - s / 2 + dy * (s + gap), s, s); };
        QDockWidget *rep = representative(*g, m_dragged);
        // une fenêtre ne se pose pas en onglet sur sa propre pile
        if (!contains(g->tabs, m_dragged))
            out.push_back({{rep, Side::Center}, box(0, 0)});
        out.push_back({{rep, Side::Left}, box(-1, 0)});
        out.push_back({{rep, Side::Right}, box(1, 0)});
        out.push_back({{rep, Side::Top}, box(0, -1)});
        out.push_back({{rep, Side::Bottom}, box(0, 1)});
    }
    // sur les bords de l'espace de travail : sur toute la hauteur ou toute la largeur
    if (std::any_of(m_groups.begin(), m_groups.end(), [&](const Group &g) { return canDropOn(g); })) {
        const QRect &w = m_workspace;
        const int s = theme::px(38), m = theme::px(8);
        const QPoint c = w.center();
        out.push_back({{nullptr, Side::Left}, QRect(w.left() + m, c.y() - s / 2, s, s)});
        out.push_back({{nullptr, Side::Right}, QRect(w.left() + w.width() - m - s, c.y() - s / 2, s, s)});
        out.push_back({{nullptr, Side::Top}, QRect(c.x() - s / 2, w.top() + m, s, s)});
        out.push_back({{nullptr, Side::Bottom}, QRect(c.x() - s / 2, w.top() + w.height() - m - s, s, s)});
    }
    return out;
}

std::optional<Target> Overlay::targetAt(QPoint pos) const
{
    for (const auto &[t, r] : markers(pos))
        if (r.contains(pos))
            return t;
    // sur la barre d'onglets d'une autre pile : en onglet
    for (const Group &g : m_groups)
        if (g.tabBar && !contains(g.tabs, m_dragged) && g.tabBar->geometry().contains(pos))
            return Target{representative(g, m_dragged), Side::Center};
    return std::nullopt;
}

void Overlay::hover(QPoint pos)
{
    m_pos = pos;
    m_target = targetAt(pos);
    update();
}

void Overlay::finish()
{
    if (m_done)
        return;
    if (!m_dragged) {
        cancel();
        return;
    }
    const std::optional<Target> t = m_target;
    close();
    if (t && onDrop)
        onDrop(*t);
}

void Overlay::cancel()
{
    if (!m_done)
        close();
}

void Overlay::close()
{
    m_done = true;
    releaseMouse();
    releaseKeyboard();
    hide();
    deleteLater();
}

void Overlay::mouseMoveEvent(QMouseEvent *event)
{
    // bouton relâché sans que le calque l'ait vu : rien n'est déplacé
    if (!(event->buttons() & Qt::LeftButton)) {
        cancel();
        return;
    }
    hover(event->position().toPoint());
}

void Overlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        hover(event->position().toPoint());
        finish();
    }
}

void Overlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        cancel();
}

void Overlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        cancel();
}

QString Overlay::describe(const Target &t) const
{
    if (!t.group) {
        switch (t.side) {
        case Side::Left: return u"Tout à gauche, sur toute la hauteur"_s;
        case Side::Right: return u"Tout à droite, sur toute la hauteur"_s;
        case Side::Top: return u"Tout en haut, sur toute la largeur"_s;
        case Side::Bottom: return u"Tout en bas, sur toute la largeur"_s;
        case Side::Center: break;
        }
        return {};
    }
    const QString name = quoted(t.group->windowTitle());
    switch (t.side) {
    case Side::Center: return u"En onglet avec "_s + name;
    case Side::Left: return u"À gauche de "_s + name;
    case Side::Right: return u"À droite de "_s + name;
    case Side::Top: return u"Au-dessus de "_s + name;
    case Side::Bottom: return u"Au-dessous de "_s + name;
    }
    return {};
}

namespace {

/// Repère : une petite fenêtre dont la partie qu'occupera la fenêtre posée est marquée ; sur les
/// bords de l'espace de travail, cette partie est une bande pleine.
void paintMarker(QPainter &p, const QPalette &pal, const QRect &b, Side side, bool edge, bool hot)
{
    const QColor accent = pal.color(QPalette::Highlight);
    const QColor fg = pal.color(QPalette::WindowText);
    QColor bg = pal.color(QPalette::Window);
    bg.setAlpha(242);
    p.setPen(QPen(hot ? accent : pal.color(QPalette::Mid), hot ? 2.0 : 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

    const int inset = static_cast<int>(std::lround(b.width() * 0.18));
    const QRectF w = QRectF(b.adjusted(inset, inset, -inset, -inset)).adjusted(0.5, 0.5, -0.5, -0.5);
    const double bar = std::max(3.0, std::round(b.height() * 0.1));
    const QRectF body(w.left(), w.top() + bar, w.width(), w.height() - bar);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::Base));
    p.drawRect(w);
    QRectF part = body;
    const double frac = edge ? 0.3 : 0.5;
    switch (side) {
    case Side::Left: part.setWidth(body.width() * frac); break;
    case Side::Right: part.setLeft(body.right() - body.width() * frac); break;
    case Side::Top: part.setHeight(body.height() * frac); break;
    case Side::Bottom: part.setTop(body.bottom() - body.height() * frac); break;
    case Side::Center: break;
    }
    if (hot) {
        QColor a = accent;
        a.setAlpha(150);
        p.fillRect(part, a);
    } else if (edge) {
        QColor f = fg;
        f.setAlpha(50);
        p.fillRect(part, f);
    }
    if (side != Side::Center) {
        QPen pen(fg, 1.0);
        if (!edge)
            pen.setDashPattern({2.5, 2.0});
        p.setPen(pen);
        switch (side) {
        case Side::Left: p.drawLine(QPointF(part.right(), body.top()), QPointF(part.right(), body.bottom())); break;
        case Side::Right: p.drawLine(QPointF(part.left(), body.top()), QPointF(part.left(), body.bottom())); break;
        case Side::Top: p.drawLine(QPointF(body.left(), part.bottom()), QPointF(body.right(), part.bottom())); break;
        case Side::Bottom: p.drawLine(QPointF(body.left(), part.top()), QPointF(body.right(), part.top())); break;
        case Side::Center: break;
        }
    }
    p.setPen(QPen(fg, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(w);
    p.fillRect(QRectF(w.left(), w.top(), w.width(), bar), fg);
}

/// Étiquette arrondie centrée en `center`, tenue dans `bounds`.
void paintPill(QPainter &p, const QFont &font, QPoint center, const QRect &bounds, const QString &text, const QColor &bg, const QColor &fg)
{
    const QFontMetrics fm(font);
    const QString t = fm.elidedText(text, Qt::ElideMiddle, std::max(40, bounds.width() - 28));
    const QSize sz(fm.horizontalAdvance(t) + theme::px(20), fm.height() + theme::px(10));
    QRect r(QPoint(center.x() - sz.width() / 2, center.y() - sz.height() / 2), sz);
    p.setFont(font);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(r), 5, 5);
    p.setPen(fg);
    p.drawText(r, Qt::AlignCenter, t);
}

} // namespace

void Overlay::paintEvent(QPaintEvent *)
{
    if (!m_dragged)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    const QColor accent = pal.color(QPalette::Highlight);

    // place que prendra la fenêtre
    if (m_target) {
        const QRect r = preview(*m_target, m_groups, m_workspace, m_dragged->size()).adjusted(2, 2, -2, -2);
        QColor fill = accent;
        fill.setAlpha(55);
        QColor line = accent;
        line.setAlpha(210);
        p.setPen(QPen(line, 2.0));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 4, 4);
        QFont f = font();
        f.setBold(true);
        // l'étiquette à l'écart des repères : vers le bord pour une moitié haute ou basse, sous les
        // repères sinon
        const int ph = QFontMetrics(f).height() + 10;
        const bool edge = !m_target->group;
        QPoint at = r.center();
        switch (m_target->side) {
        case Side::Top: at.setY(edge ? std::max(r.center().y(), r.top() + 50 + ph / 2) : r.top() + 10 + ph / 2); break;
        case Side::Bottom: at.setY(edge ? std::min(r.center().y(), r.bottom() - 50 - ph / 2) : r.bottom() - 10 - ph / 2); break;
        default: at.setY(std::min(r.center().y() + 80, r.bottom() - 8 - ph / 2)); break;
        }
        paintPill(p, f, at, r, describe(*m_target), accent, pal.color(QPalette::HighlightedText));
    }
    // pile survolée
    if (const Group *g = groupAt(m_pos); g && canDropOn(*g) && !(m_target && m_target->group && contains(g->tabs, m_target->group))) {
        QColor line = accent;
        line.setAlpha(110);
        p.setPen(QPen(line, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(g->rect).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    // repères : ceux des bords dessous, ceux de la pile survolée dessus
    const auto ms = markers(m_pos);
    for (auto it = ms.rbegin(); it != ms.rend(); ++it)
        paintMarker(p, pal, it->second, it->first.side, !it->first.group, m_target == it->first && it->second.contains(m_pos));

    // fenêtre glissée, près du pointeur
    const QFontMetrics fm(font());
    const QString title = m_dragged->windowTitle();
    const QString hint = m_target ? QString() : u"Lâchez sur un repère · Échap : annuler"_s;
    const int iconSize = theme::px(16);
    const int w = std::max(fm.horizontalAdvance(title) + iconSize + 6, fm.horizontalAdvance(hint)) + 20;
    const int h = fm.height() * (hint.isEmpty() ? 1 : 2) + 12 + (hint.isEmpty() ? 0 : 2);
    QRect ghost(m_pos + QPoint(theme::px(18), theme::px(16)), QSize(w, h));
    if (ghost.right() > width() - 4)
        ghost.moveRight(m_pos.x() - 12);
    if (ghost.bottom() > height() - 4)
        ghost.moveBottom(m_pos.y() - 12);
    QColor gb = pal.color(QPalette::ToolTipBase);
    gb.setAlpha(240);
    p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    p.setBrush(gb);
    p.drawRoundedRect(QRectF(ghost).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
    const QRect line1(ghost.left() + 10, ghost.top() + 6, ghost.width() - 20, fm.height());
    m_dragged->windowIcon().paint(&p, QRect(line1.left(), line1.top() + (fm.height() - iconSize) / 2, iconSize, iconSize));
    p.setFont(font());
    p.setPen(pal.color(QPalette::ToolTipText));
    p.drawText(line1.adjusted(iconSize + 6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, title);
    if (!hint.isEmpty()) {
        QColor weak = pal.color(QPalette::ToolTipText);
        weak.setAlpha(150);
        p.setPen(weak);
        p.drawText(QRect(line1.left(), line1.bottom() + 3, line1.width(), fm.height()), Qt::AlignVCenter | Qt::AlignLeft, hint);
    }
}

} // namespace docking
