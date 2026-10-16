#include "theme.h"

#include "icons.h"

#include "style.h"

#include <QApplication>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleHints>

#include <optional>

#ifdef MEVSONO_PORTAL
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

/// Thème demandé à Qt : le choix du bouton, sinon celui du bureau (`Unknown` : Qt le cherche lui-même).
Qt::ColorScheme requested()
{
    return g_choice != Qt::ColorScheme::Unknown ? g_choice : g_desktop.value_or(Qt::ColorScheme::Unknown);
}

/// Thème sombre : toujours la palette de MeVSono. Thème clair : celle du style, sauf si elle est sombre
/// (certains thèmes de Qt ignorent la demande, comme « xdgdesktopportal »).
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
    if (scheme == Qt::ColorScheme::Unknown)
        return;
    const bool dark = scheme == Qt::ColorScheme::Dark;
    if (dark || isDark(QApplication::palette())) {
        QApplication::setPalette(dark ? darkPalette() : lightPalette());
        g_forced = true;
    }
}

void update()
{
    QGuiApplication::styleHints()->setColorScheme(requested());
    fitPalette();
}

#ifdef MEVSONO_PORTAL

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

public slots:
    void settingChanged(const QString &ns, const QString &key, const QDBusVariant &value)
    {
        if (ns == APPEARANCE && key == COLOR_SCHEME) {
            g_desktop = fromPortal(value.variant());
            update();
        }
    }
};

#endif

/// Style de la suite : celui du poste, avec une barre de menus 10 % plus haute (titres centrés), qui
/// paraissait écrasée.
class SuiteStyle : public QProxyStyle {
public:
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override
    {
        const QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_MenuBarItem)
            return {result.width() + result.height() / 4, qRound(result.height() * 1.1)};
        return result;
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
};

/// Installe le style de la suite, lit le thème du bureau et s'abonne à ses changements (une seule
/// fois).
void watchDesktop()
{
    static bool watching = false;
    if (watching)
        return;
    watching = true;
    // avant la palette : changer de style rétablit celle du style
    QApplication::setStyle(new SuiteStyle);
    // Signal émis avant que Qt change la palette : elle est vérifiée ensuite.
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] { fitPalette(); },
                     Qt::QueuedConnection);
#ifdef MEVSONO_PORTAL
    // Sous Linux, Qt ne lit pas toujours le réglage du bureau (GNOME en sombre vu comme clair) :
    // on le demande au portail. Ailleurs, Qt suit le système tout seul.
    g_desktop = readPortal();
    if (g_desktop) {
        QDBusConnection::sessionBus().connect(PORTAL_SERVICE, PORTAL_PATH, PORTAL_SETTINGS, u"SettingChanged"_s,
                                              new PortalWatcher(qApp),
                                              SLOT(settingChanged(QString, QString, QDBusVariant)));
    }
#endif
}

} // namespace

void choose(Qt::ColorScheme choice)
{
    watchDesktop();
    g_choice = choice;
    update();
}

} // namespace theme

#ifdef MEVSONO_PORTAL
#include "theme.moc"
#endif
