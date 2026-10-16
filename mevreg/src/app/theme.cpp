#include "app/theme.h"

#include "app/icons.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFontDatabase>
#include <QLayout>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>

#include <algorithm>
#include <cmath>
#include <optional>

#ifdef MEVREG_PORTAL
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#endif

using namespace Qt::StringLiterals;

namespace theme {

namespace {

/// Choix du bouton (`Unknown` : thème du bureau).
Qt::ColorScheme g_choice = Qt::ColorScheme::Unknown;
/// Thème du bureau lu auprès du portail (vide : Qt le cherche lui-même).
std::optional<Qt::ColorScheme> g_desktop;
/// Vrai si une palette a été imposée, faute d'être fournie par le style.
bool g_forced = false;
/// Agrandissement de l'interface.
double g_zoom = 1.0;

// Tailles données à 100 %, gardées dans les propriétés des widgets pour `rescale`.
constexpr char MIN_W[] = "mevreg_minimumWidth";
constexpr char MAX_W[] = "mevreg_maximumWidth";
constexpr char MIN_SIZE[] = "mevreg_minimumSize";
constexpr char FIXED_SIZE[] = "mevreg_fixedSize";
constexpr char FIXED_H[] = "mevreg_fixedHeight";
constexpr char ICON_SIZE[] = "mevreg_iconSize";
constexpr char FONT_FACTOR[] = "mevreg_fontFactor";
constexpr char FONT_WEIGHT[] = "mevreg_fontWeight";

bool darkPalette(const QPalette &p)
{
    return p.color(QPalette::WindowText).lightness() > p.color(QPalette::Window).lightness();
}

/// Thème sombre des logiciels de la suite, plus sombre que ceux des styles de Qt (fond presque noir,
/// boutons et bordures encore visibles).
QPalette darkPalette()
{
    QPalette p(QColor(50, 51, 55));
    p.setColor(QPalette::Window, QColor(32, 33, 36));
    p.setColor(QPalette::Base, QColor(22, 23, 25));
    p.setColor(QPalette::AlternateBase, QColor(30, 31, 34));
    p.setColor(QPalette::Light, QColor(78, 80, 86));
    p.setColor(QPalette::Midlight, QColor(62, 64, 69));
    p.setColor(QPalette::Mid, QColor(70, 72, 78));
    p.setColor(QPalette::Dark, QColor(18, 18, 20));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Text, QColor(235, 235, 235));
    p.setColor(QPalette::WindowText, QColor(232, 232, 232));
    p.setColor(QPalette::ButtonText, QColor(232, 232, 232));
    p.setColor(QPalette::PlaceholderText, QColor(140, 142, 148));
    p.setColor(QPalette::ToolTipBase, QColor(50, 51, 55));
    p.setColor(QPalette::ToolTipText, QColor(235, 235, 235));
    p.setColor(QPalette::Highlight, QColor(53, 132, 228));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(120, 174, 237));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(110, 110, 115));
    return p;
}

/// Palette claire, quand celle du style est sombre (bureau sombre, thème clair choisi).
QPalette lightPalette()
{
    QPalette p(QColor(239, 239, 239));
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    p.setColor(QPalette::Text, QColor(20, 20, 20));
    p.setColor(QPalette::WindowText, QColor(20, 20, 20));
    p.setColor(QPalette::ButtonText, QColor(20, 20, 20));
    p.setColor(QPalette::PlaceholderText, QColor(120, 120, 120));
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    p.setColor(QPalette::ToolTipText, Qt::black);
    p.setColor(QPalette::Highlight, QColor(53, 132, 228));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(26, 95, 180));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(150, 150, 150));
    return p;
}

/// Couleur opaque : une couleur translucide de la palette (PlaceholderText de certains styles) est
/// mélangée au fond de la fenêtre, pour servir aussi dans les textes HTML.
QColor opaque(QColor c)
{
    if (c.alpha() == 255)
        return c;
    const QColor bg = QApplication::palette().color(QPalette::Window);
    const double a = c.alphaF();
    return QColor::fromRgbF(float(c.redF() * a + bg.redF() * (1 - a)), float(c.greenF() * a + bg.greenF() * (1 - a)),
                            float(c.blueF() * a + bg.blueF() * (1 - a)));
}

/// Style Fusion, et séparations des fenêtres ancrées de la suite : un filet couleur Mid (Highlight
/// au survol) au milieu d'une bande de la couleur de la fenêtre, assez large pour être saisie à la
/// souris. Pas de feuille de style pour l'application : elle empêcherait les fenêtres ancrées de
/// transmettre leur palette (fond des panneaux) à leur contenu.
class SuiteStyle : public QProxyStyle {
public:
    SuiteStyle() : QProxyStyle(QStyleFactory::create(u"Fusion"_s)) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override
    {
        const bool arrow = element == PE_IndicatorArrowUp || element == PE_IndicatorArrowDown || element == PE_IndicatorArrowLeft
                           || element == PE_IndicatorArrowRight;
        if (arrow && zoom() != 1.0) {
            // Flèches (menus des boutons, sections dépliables) : Fusion les dessine de 8 px au plus,
            // minuscules une fois l'interface agrandie ; même triangle, à l'agrandissement en cours.
            const QRectF r = option->rect;
            const double s = std::min(8.0 * zoom(), std::min(r.width(), r.height()));
            if (s <= 1.0)
                return;
            const QPointF c = r.center();
            QPolygonF t;
            switch (element) {
            case PE_IndicatorArrowUp: t << c + QPointF(-s / 2, s / 4) << c + QPointF(s / 2, s / 4) << c + QPointF(0, -s / 4); break;
            case PE_IndicatorArrowDown: t << c + QPointF(-s / 2, -s / 4) << c + QPointF(s / 2, -s / 4) << c + QPointF(0, s / 4); break;
            case PE_IndicatorArrowLeft: t << c + QPointF(s / 4, -s / 2) << c + QPointF(s / 4, s / 2) << c + QPointF(-s / 4, 0); break;
            default: t << c + QPointF(-s / 4, -s / 2) << c + QPointF(-s / 4, s / 2) << c + QPointF(s / 4, 0); break;
            }
            QColor color = option->palette.windowText().color();
            color.setAlpha(160);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPolygon(t);
            painter->restore();
            return;
        }
        if (element != PE_IndicatorDockWidgetResizeHandle) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        const QRect r = option->rect;
        painter->fillRect(r, option->palette.color(QPalette::Window));
        // State_Horizontal : séparation horizontale, entre deux fenêtres l'une au-dessus de l'autre
        const QRect line = (option->state & State_Horizontal) ? QRect(r.left(), r.top() + r.height() / 2, r.width(), 1)
                                                              : QRect(r.left() + r.width() / 2, r.top(), 1, r.height());
        painter->fillRect(line, option->palette.color((option->state & State_MouseOver) ? QPalette::Highlight : QPalette::Mid));
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        // Une mesure demandée par le style lui-même pour en calculer une autre (la taille des
        // icônes d'onglets d'après celle des petites icônes…) n'est pas agrandie : seul le résultat
        // l'est, une fois.
        static thread_local int depth = 0;
        const bool nested = depth > 0;
        if (metric == PM_DockWidgetSeparatorExtent)
            return nested ? 5 : px(5);
        ++depth;
        const int value = QProxyStyle::pixelMetric(metric, option, widget);
        --depth;
        // agrandissement de l'interface : icônes, cases, barres de défilement, marges et
        // espacements suivent la police ; les traits (cadres, bordures) restent fins
        return !nested && zoomed(metric) ? px(value) : value;
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override
    {
        QSize r = QProxyStyle::sizeFromContents(type, option, size, widget);
        // barre de menus 10 % plus haute, titres centrés (sans feuille de style)
        if (type == CT_MenuBarItem)
            r = QSize(r.width() + r.height() / 4, qRound(r.height() * 1.1));
        return r;
    }


    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *data) const override
    {
        // boutons des boîtes de dialogue sans icône : celles du thème du bureau juraient avec le reste
        if (hint == SH_DialogButtonBox_ButtonsHaveIcons)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }

    QIcon standardIcon(StandardPixmap icon, const QStyleOption *option, const QWidget *widget) const override
    {
        // icônes des messages : les pastilles de la barre d'état, dessinées comme le reste
        switch (icon) {
        case SP_MessageBoxInformation: return icons::status(icons::Status::Info);
        case SP_MessageBoxWarning: return icons::status(icons::Status::Warning);
        case SP_MessageBoxCritical: return icons::status(icons::Status::Error);
        case SP_MessageBoxQuestion: return icons::question();
        default: return QProxyStyle::standardIcon(icon, option, widget);
        }
    }
private:
    static bool zoomed(PixelMetric metric)
    {
        switch (metric) {
        case PM_ButtonMargin:
        case PM_ButtonIconSize:
        case PM_SmallIconSize:
        case PM_LargeIconSize:
        case PM_ToolBarIconSize:
        case PM_TabBarIconSize:
        case PM_ListViewIconSize:
        case PM_IconViewIconSize:
        case PM_MessageBoxIconSize:
        case PM_IndicatorWidth:
        case PM_IndicatorHeight:
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight:
        case PM_CheckBoxLabelSpacing:
        case PM_RadioButtonLabelSpacing:
        case PM_MenuButtonIndicator:
        case PM_ScrollBarExtent:
        case PM_ScrollBarSliderMin:
        case PM_SliderThickness:
        case PM_SliderLength:
        case PM_SliderControlThickness:
        case PM_SliderTickmarkOffset:
        case PM_SplitterWidth:
        case PM_DockWidgetTitleMargin:
        case PM_DockWidgetTitleBarButtonMargin:
        case PM_TitleBarHeight:
        case PM_TitleBarButtonIconSize:
        case PM_TitleBarButtonSize:
        case PM_TabBarTabHSpace:
        case PM_TabBarTabVSpace:
        case PM_TabBarScrollButtonWidth:
        case PM_TabCloseIndicatorWidth:
        case PM_TabCloseIndicatorHeight:
        case PM_HeaderMargin:
        case PM_HeaderMarkSize:
        case PM_HeaderGripMargin:
        case PM_HeaderDefaultSectionSizeHorizontal:
        case PM_HeaderDefaultSectionSizeVertical:
        case PM_MenuHMargin:
        case PM_MenuVMargin:
        case PM_MenuBarItemSpacing:
        case PM_MenuBarHMargin:
        case PM_MenuBarVMargin:
        case PM_MenuScrollerHeight:
        case PM_ToolBarItemSpacing:
        case PM_ToolBarItemMargin:
        case PM_ToolBarSeparatorExtent:
        case PM_ToolBarExtensionExtent:
        case PM_ToolBarHandleExtent:
        case PM_LayoutLeftMargin:
        case PM_LayoutTopMargin:
        case PM_LayoutRightMargin:
        case PM_LayoutBottomMargin:
        case PM_LayoutHorizontalSpacing:
        case PM_LayoutVerticalSpacing:
        case PM_TreeViewIndentation:
            return true;
        default:
            return false;
        }
    }
};

/// Thème demandé à Qt : le choix du bouton, sinon celui du bureau (`Unknown` : Qt le cherche lui-même).
Qt::ColorScheme requested()
{
    return g_choice != Qt::ColorScheme::Unknown ? g_choice : g_desktop.value_or(Qt::ColorScheme::Unknown);
}

/// Thème sombre : toujours la palette de la suite. Thème clair : celle du style, sauf si elle est
/// sombre (certains thèmes de Qt ignorent la demande, comme « xdgdesktopportal »).
void fitPalette()
{
    if (g_forced) {
        // Palette sans rôle imposé : celle du style revient.
        QApplication::setPalette(QPalette());
        g_forced = false;
    }
    Qt::ColorScheme scheme = requested();
    if (scheme == Qt::ColorScheme::Unknown)
        scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown) {
        const bool dark = scheme == Qt::ColorScheme::Dark;
        if (dark || darkPalette(QApplication::palette())) {
            QApplication::setPalette(dark ? darkPalette() : lightPalette());
            g_forced = true;
        }
    }
}

void update()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(requested());
#endif
    fitPalette();
}

#ifdef MEVREG_PORTAL

const QString PORTAL_SERVICE = u"org.freedesktop.portal.Desktop"_s;
const QString PORTAL_PATH = u"/org/freedesktop/portal/desktop"_s;
const QString PORTAL_SETTINGS = u"org.freedesktop.portal.Settings"_s;
const QString APPEARANCE = u"org.freedesktop.appearance"_s;
const QString COLOR_SCHEME = u"color-scheme"_s;

/// Réglage « color-scheme » du portail : 1 = sombre, 2 = clair, 0 = sans préférence.
Qt::ColorScheme fromPortal(QVariant value)
{
    while (value.canConvert<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    switch (value.toUInt()) {
    case 1: return Qt::ColorScheme::Dark;
    case 2: return Qt::ColorScheme::Light;
    default: return Qt::ColorScheme::Unknown;
    }
}

/// Réglage du bureau, lu auprès du portail (GNOME, KDE…) ; vide si le portail ne répond pas.
std::optional<Qt::ColorScheme> readPortal()
{
    // `ReadOne` (portails récents), sinon `Read`, qui renvoie la valeur dans un variant de plus.
    for (const QString &method : {u"ReadOne"_s, u"Read"_s}) {
        QDBusMessage call = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, PORTAL_SETTINGS, method);
        call << APPEARANCE << COLOR_SCHEME;
        const QDBusReply<QVariant> reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 1000);
        if (reply.isValid())
            return fromPortal(reply.value());
    }
    return std::nullopt;
}

/// Suit les changements du réglage du bureau pendant que le programme tourne.
class PortalWatcher : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

public Q_SLOTS:
    void settingChanged(const QString &ns, const QString &key, const QDBusVariant &value)
    {
        if (ns == APPEARANCE && key == COLOR_SCHEME) {
            g_desktop = fromPortal(value.variant());
            update();
        }
    }
};

#endif

} // namespace

void init()
{
    // même apparence sous Linux et sous Windows ; elle suit les palettes claire et sombre
    QApplication::setStyle(new SuiteStyle);
    // Signal émis avant que Qt change la palette : elle est vérifiée ensuite.
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] { fitPalette(); },
                     Qt::QueuedConnection);
#ifdef MEVREG_PORTAL
    // Sous Linux, Qt ne lit pas toujours le réglage du bureau (GNOME en sombre vu comme clair) :
    // on le demande au portail. Ailleurs, Qt suit le système tout seul.
    g_desktop = readPortal();
    if (g_desktop) {
        QDBusConnection::sessionBus().connect(PORTAL_SERVICE, PORTAL_PATH, PORTAL_SETTINGS, u"SettingChanged"_s, new PortalWatcher(qApp),
                                              SLOT(settingChanged(QString, QString, QDBusVariant)));
    }
#endif
    update();
}

void choose(Qt::ColorScheme c)
{
    g_choice = c;
    update();
}

Qt::ColorScheme choice()
{
    return g_choice;
}

bool isDark()
{
    return darkPalette(QApplication::palette());
}

QColor accent()
{
    return isDark() ? QColor(108, 168, 240) : QColor(31, 111, 180);
}

QColor errorColor()
{
    return isDark() ? QColor(255, 90, 90) : QColor(200, 0, 0);
}

QColor warnColor()
{
    return isDark() ? QColor(255, 143, 0) : QColor(200, 90, 0);
}

QColor successColor()
{
    return isDark() ? QColor(110, 215, 130) : QColor(20, 125, 55);
}

QColor weakColor()
{
    return opaque(QApplication::palette().color(QPalette::PlaceholderText));
}

QColor panelColor()
{
    return QApplication::palette().color(QPalette::Window).darker(isDark() ? 125 : 104);
}

QColor kindColor(bool calculated, bool uncertainty)
{
    const bool dark = isDark();
    if (uncertainty)
        return dark ? QColor(216, 168, 152) : QColor(140, 86, 75);
    if (calculated)
        return dark ? QColor(140, 208, 140) : QColor(44, 128, 44);
    return dark ? QColor(140, 184, 240) : QColor(31, 95, 164);
}

TableColors tableColors()
{
    const QColor weak = weakColor();
    if (isDark()) {
        return {QColor(38, 40, 44), QColor(27, 36, 30), QColor(52, 54, 59), QColor(60, 120, 200, 96), QColor(108, 168, 240),
                errorColor(), QColor(235, 235, 235), weak};
    }
    return {QColor(233, 237, 242), QColor(238, 246, 238), QColor(220, 224, 230), QColor(60, 120, 200, 64), QColor(31, 111, 180),
            errorColor(), QColor(26, 26, 26), weak};
}

// --- agrandissement --------------------------------------------------------------------------

double zoom()
{
    return g_zoom;
}

void setZoom(double z)
{
    g_zoom = z > 0.0 ? z : 1.0;
}

int px(double length)
{
    return static_cast<int>(std::lround(length * g_zoom));
}

QSize px(QSize size)
{
    return {px(size.width()), px(size.height())};
}

QFont fixedFont()
{
    QFont f;
    f.setFamilies(QFontDatabase::systemFont(QFontDatabase::FixedFont).families());
    f.setStyleHint(QFont::Monospace);
    return f;
}

namespace {

/// Largeur minimale : celle donnée (à l'agrandissement en cours), et pas moins que ce que demande
/// la disposition du widget.
void fitMinimumWidth(QWidget *w)
{
    const QVariant v = w->property(MIN_W);
    if (!v.isValid())
        return;
    int width = px(v.toInt());
    if (const QLayout *l = w->layout())
        width = std::max(width, l->totalMinimumSize().width());
    if (w->minimumWidth() != width)
        w->setMinimumWidth(width);
}

/// Revoit la largeur minimale d'un widget quand sa disposition change (page reconstruite, police
/// agrandie) ; la disposition est déjà recalculée quand le filtre reçoit l'événement.
class MinimumKeeper : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::LayoutRequest && watched->isWidgetType())
            fitMinimumWidth(static_cast<QWidget *>(watched));
        return false;
    }
};

MinimumKeeper *keeper()
{
    static auto *k = new MinimumKeeper(qApp);
    return k;
}

/// Applique les tailles relatives gardées dans les propriétés du widget.
void apply(QWidget *w)
{
    fitMinimumWidth(w);
    if (const QVariant v = w->property(MAX_W); v.isValid())
        w->setMaximumWidth(px(v.toInt()));
    if (const QVariant v = w->property(MIN_SIZE); v.isValid())
        w->setMinimumSize(px(v.toSize()));
    if (const QVariant v = w->property(FIXED_SIZE); v.isValid())
        w->setFixedSize(px(v.toSize()));
    if (const QVariant v = w->property(FIXED_H); v.isValid())
        w->setFixedHeight(px(v.toInt()));
    if (const QVariant v = w->property(ICON_SIZE); v.isValid())
        if (auto *b = qobject_cast<QAbstractButton *>(w))
            b->setIconSize(px(v.toSize()));
    if (const QVariant v = w->property(FONT_FACTOR); v.isValid()) {
        // seules la taille et la graisse sont imposées : la famille suit celle de l'interface
        const QFont base = QApplication::font();
        QFont f;
        if (base.pointSizeF() > 0.0)
            f.setPointSizeF(base.pointSizeF() * v.toDouble());
        else
            f.setPixelSize(std::max(1, static_cast<int>(std::lround(base.pixelSize() * v.toDouble()))));
        f.setWeight(static_cast<QFont::Weight>(w->property(FONT_WEIGHT).toInt()));
        w->setFont(f);
    }
}

void remember(QWidget *w, const char *name, const QVariant &value)
{
    w->setProperty(name, value);
    apply(w);
}

} // namespace

void setMinimumWidth(QWidget *w, int width)
{
    w->removeEventFilter(keeper());
    w->installEventFilter(keeper());
    remember(w, MIN_W, width);
}

void setMaximumWidth(QWidget *w, int width)
{
    remember(w, MAX_W, width);
}

void setMinimumSize(QWidget *w, QSize size)
{
    remember(w, MIN_SIZE, size);
}

void setFixedSize(QWidget *w, QSize size)
{
    remember(w, FIXED_SIZE, size);
}

void setFixedHeight(QWidget *w, int height)
{
    remember(w, FIXED_H, height);
}

void setIconSize(QAbstractButton *b, QSize size)
{
    remember(b, ICON_SIZE, size);
}

int baseMinimumWidth(const QWidget *w)
{
    return w->property(MIN_W).toInt();
}

void setRelativeFont(QWidget *w, double factor, QFont::Weight weight)
{
    w->setProperty(FONT_WEIGHT, static_cast<int>(weight));
    remember(w, FONT_FACTOR, factor);
}

void rescale()
{
    for (QWidget *w : QApplication::allWidgets())
        apply(w);
}

} // namespace theme

#ifdef MEVREG_PORTAL
#include "theme.moc"
#endif
