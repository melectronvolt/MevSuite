#include "theme.h"

#include "icones.h"

#include "dessin.h"

#include <QApplication>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleHints>

#include <optional>

#ifdef MEVCINE_PORTAIL
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#endif

using namespace Qt::StringLiterals;

namespace theme {

namespace {

/// choix du bouton (`Unknown` : thème du bureau)
Qt::ColorScheme g_choix = Qt::ColorScheme::Unknown;
/// thème du bureau lu auprès du portail (vide : Qt le cherche lui-même)
std::optional<Qt::ColorScheme> g_bureau;
/// vrai si une palette a été imposée, faute d'être fournie par le style
bool g_imposee = false;

/// Le style du poste, à une retouche près, commune aux logiciels de la suite : la barre des menus
/// 10 % plus haute, et ses titres un peu plus espacés (la barre standard paraissait écrasée) ; le
/// texte reste centré. Sans feuille de style, qui figerait les couleurs au premier thème.
class SuiteStyle : public QProxyStyle {
public:
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &taille,
                           const QWidget *widget) const override
    {
        QSize t = QProxyStyle::sizeFromContents(type, option, taille, widget);
        if (type == CT_MenuBarItem)
            t = QSize(t.width() + t.height() / 4, qRound(t.height() * 1.1));
        return t;
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
        case SP_MessageBoxInformation: return icones::statut(icones::Statut::Info);
        case SP_MessageBoxWarning: return icones::statut(icones::Statut::Attention);
        case SP_MessageBoxCritical: return icones::statut(icones::Statut::Erreur);
        case SP_MessageBoxQuestion: return icones::question();
        default: return QProxyStyle::standardIcon(icon, option, widget);
        }
    }
};

/// installe le style de la suite, une seule fois, avant toute palette
void installe_style()
{
    if (!dynamic_cast<SuiteStyle *>(QApplication::style()))
        QApplication::setStyle(new SuiteStyle);
}

/// Le thème sombre de MeVCine, plus sombre que ceux des styles de Qt : fond presque noir, boutons et
/// bordures encore visibles
QPalette palette_sombre()
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

QPalette palette_claire()
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

/// le thème demandé à Qt : celui du bouton, sinon celui du bureau (`Unknown` : Qt le cherche lui-même)
Qt::ColorScheme demande()
{
    return g_choix != Qt::ColorScheme::Unknown ? g_choix : g_bureau.value_or(Qt::ColorScheme::Unknown);
}

/// Thème sombre : toujours la palette de MeVCine. Thème clair : celle du style, sauf si elle est
/// sombre (certains thèmes de Qt ignorent la demande, comme « xdgdesktopportal »).
void ajuste_palette()
{
    if (g_imposee) {
        // une palette sans rôle imposé : celle du style revient
        QApplication::setPalette(QPalette());
        g_imposee = false;
    }
    Qt::ColorScheme schema = demande();
    if (schema == Qt::ColorScheme::Unknown)
        schema = QGuiApplication::styleHints()->colorScheme();
    if (schema == Qt::ColorScheme::Unknown)
        return;
    const bool sombre = schema == Qt::ColorScheme::Dark;
    if (sombre || dessin::sombre(QApplication::palette())) {
        QApplication::setPalette(sombre ? palette_sombre() : palette_claire());
        g_imposee = true;
    }
}

void applique()
{
    QGuiApplication::styleHints()->setColorScheme(demande());
    ajuste_palette();
}

#ifdef MEVCINE_PORTAIL

const QString SERVICE_PORTAIL = u"org.freedesktop.portal.Desktop"_s;
const QString CHEMIN_PORTAIL = u"/org/freedesktop/portal/desktop"_s;
const QString REGLAGES_PORTAIL = u"org.freedesktop.portal.Settings"_s;
const QString APPARENCE = u"org.freedesktop.appearance"_s;
const QString SCHEMA = u"color-scheme"_s;

/// le réglage « color-scheme » du portail : 1 = sombre, 2 = clair, 0 = sans préférence
Qt::ColorScheme depuis_portail(QVariant valeur)
{
    while (valeur.canConvert<QDBusVariant>())
        valeur = valeur.value<QDBusVariant>().variant();
    switch (valeur.toUInt()) {
    case 1: return Qt::ColorScheme::Dark;
    case 2: return Qt::ColorScheme::Light;
    default: return Qt::ColorScheme::Unknown;
    }
}

/// le réglage du bureau, lu auprès du portail (GNOME, KDE…) ; rien si le portail ne répond pas
std::optional<Qt::ColorScheme> lit_portail()
{
    // `ReadOne` (portails récents), sinon `Read`, qui renvoie la valeur dans un variant de plus
    for (const QString &methode : {u"ReadOne"_s, u"Read"_s}) {
        QDBusMessage appel = QDBusMessage::createMethodCall(SERVICE_PORTAIL, CHEMIN_PORTAIL, REGLAGES_PORTAIL, methode);
        appel << APPARENCE << SCHEMA;
        const QDBusReply<QVariant> reponse = QDBusConnection::sessionBus().call(appel, QDBus::Block, 1000);
        if (reponse.isValid())
            return depuis_portail(reponse.value());
    }
    return std::nullopt;
}

/// suit les changements du réglage du bureau pendant que le programme tourne
class VeillePortail : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void reglage_change(const QString &espace, const QString &cle, const QDBusVariant &valeur)
    {
        if (espace == APPARENCE && cle == SCHEMA) {
            g_bureau = depuis_portail(valeur.variant());
            applique();
        }
    }
};

#endif

/// lit le thème du bureau et s'abonne à ses changements (une seule fois)
void suit_le_bureau()
{
    static bool suivi = false;
    if (suivi)
        return;
    suivi = true;
    // signal émis avant que Qt change la palette : elle est vérifiée ensuite
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] { ajuste_palette(); },
                     Qt::QueuedConnection);
#ifdef MEVCINE_PORTAIL
    // Sous Linux, Qt ne lit pas toujours le réglage du bureau (GNOME en sombre vu comme clair) : on
    // le demande au portail. Ailleurs, Qt suit le système tout seul.
    g_bureau = lit_portail();
    if (g_bureau) {
        QDBusConnection::sessionBus().connect(SERVICE_PORTAIL, CHEMIN_PORTAIL, REGLAGES_PORTAIL, u"SettingChanged"_s,
                                              new VeillePortail(qApp),
                                              SLOT(reglage_change(QString, QString, QDBusVariant)));
    }
#endif
}

} // namespace

void choisit(Qt::ColorScheme choix)
{
    installe_style();
    suit_le_bureau();
    g_choix = choix;
    applique();
}

Qt::ColorScheme depuis_texte(const QString &texte)
{
    // « clair » et « sombre » : valeurs écrites par les versions précédentes
    const QString t = texte.trimmed().toLower();
    if (t == u"dark" || t == u"sombre")
        return Qt::ColorScheme::Dark;
    if (t == u"light" || t == u"clair")
        return Qt::ColorScheme::Light;
    return Qt::ColorScheme::Unknown;
}

QString en_texte(Qt::ColorScheme choix)
{
    switch (choix) {
    case Qt::ColorScheme::Dark: return u"dark"_s;
    case Qt::ColorScheme::Light: return u"light"_s;
    default: return u"auto"_s;
    }
}

} // namespace theme

#ifdef MEVCINE_PORTAIL
#include "theme.moc"
#endif
