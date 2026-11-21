#include "about.h"

#include "coordonnees_auteur.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// Largeur du logo divisée par sa hauteur.
constexpr double LOGO_ASPECT = 4.0;

struct Component {
    QString name;
    QString licence;
    QString url;
};

/// Composants principaux : nom, licence, site. Leur rôle est traduit dans `Tr::roles`.
std::array<Component, MAIN_COMPONENT_COUNT> composants_principaux()
{
    return {{
        {u"GCC (libstdc++)"_s, u"GPL-3.0+ WITH GCC-exception-3.1"_s, u"https://gcc.gnu.org"_s},
        {u"Qt %1"_s.arg(QString::fromLatin1(qVersion())), u"LGPL-3.0-only"_s, u"https://www.qt.io"_s},
        {u"Qt Image Formats"_s, u"LGPL-3.0-only"_s, u"https://doc.qt.io/qt-6/qtimageformats-index.html"_s},
        {u"MinGW-w64"_s, u"ZPL-2.1, MIT, GPL-3.0+ WITH GCC-exception-3.1"_s, u"https://www.mingw-w64.org"_s},
        {u"Noto Color Emoji"_s, u"OFL-1.1"_s, u"https://github.com/googlefonts/noto-emoji"_s},
    }};
}

QColor avec_transparence(QColor color, double factor)
{
    color.setAlphaF(static_cast<float>(color.alphaF() * std::clamp(factor, 0.0, 1.0)));
    return color;
}

QFont police_en_pixels(QFont font, double px, bool bold = false)
{
    font.setPixelSize(static_cast<int>(std::lround(px)));
    font.setBold(bold);
    return font;
}

/// Logo dessiné à la taille du widget, dans la couleur du texte.
class LogoWidget : public QWidget {
public:
    explicit LogoWidget(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(320, static_cast<int>(320 / LOGO_ASPECT));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        about::dessine_logo(painter, QRectF(rect()), palette().color(QPalette::WindowText));
    }
};

QFrame *separateur(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

void about::dessine_logo(QPainter &painter, const QRectF &rect, const QColor &ink)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double s = rect.height() / 100.0;
    const QRectF ruler(QPointF(rect.left(), rect.top() + 58.0 * s), rect.bottomRight());
    const double margin = 16.0 * s;
    constexpr int TICKS = 40;
    const auto tickX = [&](int k) { return ruler.left() + margin + (ruler.width() - 2.0 * margin) * k / TICKS; };
    const double xa = tickX(8), xb = tickX(32);

    // Grille de pixels : objet clair à bords flous entre les deux extrémités du segment.
    const double cell = 10.0 * s;
    constexpr int ROWS = 5;
    const int cols = static_cast<int>(ruler.width() / cell);
    const double left = rect.center().x() - cols * cell / 2.0;
    painter.setPen(Qt::NoPen);
    for (int j = 0; j < ROWS; ++j) {
        for (int i = 0; i < cols; ++i) {
            const double x = left + (i + 0.5) * cell;
            const double inside = std::clamp(std::min(x - xa, xb - x) / (1.5 * cell), -1.0, 1.0) * 0.5 + 0.5;
            const double row = 1.0 - std::pow((j - 2.0) / 2.6, 2);
            const double grain = ((i * 7 + j * 13) % 5) / 5.0; // texture régulière, sans aléatoire
            const double level = std::min(0.07 + 0.38 * inside * row + 0.05 * grain, 1.0);
            const QRectF px(left + i * cell, rect.top() + j * cell, cell, cell);
            painter.fillRect(px.adjusted(0.6 * s, 0.6 * s, -0.6 * s, -0.6 * s), avec_transparence(ink, level));
        }
    }

    // Règle graduée : grands traits toutes les 10 graduations, moyens toutes les 5.
    const double thin = 1.2 * s;
    painter.setPen(QPen(ink, thin));
    painter.setBrush(avec_transparence(ink, 0.08));
    painter.drawRoundedRect(ruler.adjusted(thin / 2, thin / 2, -thin / 2, -thin / 2), 5.0 * s, 5.0 * s);
    for (int k = 0; k <= TICKS; ++k) {
        const double len = (k % 10 == 0 ? 22.0 : k % 5 == 0 ? 15.0 : 9.0) * s;
        const double width = (k % 10 == 0 ? 2.0 : 1.2) * s;
        const double x = tickX(k);
        painter.setPen(QPen(ink, width, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(QPointF(x, ruler.top()), QPointF(x, ruler.top() + len));
    }

    // Segment de mesure aligné sur deux graduations, avec ses repères en pointillés.
    const double y = rect.top() + 2.5 * cell;
    QPen guide(avec_transparence(ink, 0.7), 1.0 * s, Qt::CustomDashLine, Qt::FlatCap);
    guide.setDashPattern({4.0, 3.0});
    painter.setPen(guide);
    for (double x : {xa, xb})
        painter.drawLine(QPointF(x, y), QPointF(x, ruler.top()));
    const QColor cyan(0, 229, 255);
    painter.setPen(QPen(QColor(0, 0, 0, 180), 6.0 * s, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(xa, y), QPointF(xb, y));
    painter.setPen(QPen(cyan, 3.0 * s, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(xa, y), QPointF(xb, y));
    painter.setPen(QPen(Qt::black, 1.5 * s));
    painter.setBrush(cyan);
    for (double x : {xa, xb})
        painter.drawEllipse(QPointF(x, y), 6.5 * s, 6.5 * s);
    painter.restore();
}

// ---------------------------------------------------------------- écran d'accueil

namespace {
constexpr double HOLD = 2.0;
constexpr double FADE = 0.5;
} // namespace

EcranDemarrage::EcranDemarrage(const Textes &tr, QWidget *parent)
    : QWidget(parent)
    , m_presentation{QIcon(u":/icons/mevpix-256.png"_s), about::NAME, tr.tagline, tr.version(about::VERSION), about::AUTHOR}
{
    setGeometry(parent->rect());
    parent->installEventFilter(this);
    qApp->installEventFilter(this);
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (duree_ecoulee() - m_start >= HOLD + FADE)
            deleteLater();
        else
            update();
    });
    m_timer.start(16);
    raise();
    show();
}

double EcranDemarrage::duree_ecoulee() const
{
    return m_clock.elapsed() / 1000.0;
}

void EcranDemarrage::saute()
{
    if (duree_ecoulee() - m_start < HOLD)
        m_start = duree_ecoulee() - HOLD;
}

bool EcranDemarrage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    if (event->type() == QEvent::KeyPress)
        saute();
    return false;
}

void EcranDemarrage::mousePressEvent(QMouseEvent *event)
{
    // Les clics ne traversent pas l'écran d'accueil.
    saute();
    event->accept();
}

void EcranDemarrage::paintEvent(QPaintEvent *)
{
    const double alpha = 1.0 - std::clamp((duree_ecoulee() - m_start - HOLD) / FADE, 0.0, 1.0);
    m_presentation.dessine(*this, alpha);
}

// ---------------------------------------------------------------- « À propos »

DialogueAPropos::DialogueAPropos(const Textes &tr, const FichiersReglages &settings, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr.menuAbout.chopped(1).trimmed());
    const QRect screen = parent && parent->screen() ? parent->screen()->availableGeometry() : QRect(0, 0, 800, 600);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    const auto label = [&](const QString &text) {
        auto *l = new QLabel(text, content);
        l->setWordWrap(true);
        return l;
    };
    // Toute la largeur, texte centré : une étiquette qui passe à la ligne garde la bonne hauteur.
    const auto centered = [&](QLabel *l) {
        l->setAlignment(Qt::AlignHCenter);
        layout->addWidget(l);
    };

    layout->addWidget(new LogoWidget(content), 0, Qt::AlignHCenter);
    auto *name = label(about::NAME);
    name->setFont(police_en_pixels(font(), 34, true));
    centered(name);
    centered(label(tr.version(about::VERSION)));
    centered(label(tr.tagline));
    layout->addSpacing(6);
    layout->addLayout(mevsuite::coordonnees_auteur(tr.author, tr.contact, tr.website, about::AUTHOR, about::EMAIL, content));
    layout->addSpacing(10);
    layout->addWidget(separateur(content));

    auto *heading = label(tr.ossHeading);
    QFont headingFont = font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 1.4);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    layout->addWidget(label(tr.ossIntro));

    // Tableau des composants principaux : nom (lien vers le site), rôle et licence en dessous.
    const QString weak = palette().color(QPalette::PlaceholderText).name();
    QString rows;
    const auto components = composants_principaux();
    for (size_t i = 0; i < components.size(); ++i) {
        const Component &c = components[i];
        rows += u"<tr><td style=\"white-space:nowrap\"><a href=\"%1\">%2</a></td>"
                u"<td>%3<br><span style=\"color:%4\">%5</span></td></tr>"_s.arg(
                    c.url, c.name.toHtmlEscaped(), tr.roles[i].toHtmlEscaped(), weak, c.licence.toHtmlEscaped());
    }
    auto *table = label(u"<table cellspacing=\"5\">%1</table>"_s.arg(rows));
    table->setTextFormat(Qt::RichText);
    table->setOpenExternalLinks(true);
    layout->addWidget(table);

    auto *note = label(tr.licenseNote);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    note->setFont(small);
    layout->addWidget(note);

    const std::pair<QString, QString> files[] = {{tr.settingsFile, settings.user}, {tr.settingsImposed, settings.admin}};
    if (!settings.user.isEmpty() || !settings.admin.isEmpty())
        layout->addWidget(separateur(content));
    for (const auto &[title, path] : files) {
        if (path.isEmpty())
            continue;
        auto *line = label(u"<span style=\"color:%1\">%2</span> <code>%3</code>"_s.arg(
            weak, title.toHtmlEscaped(), QDir::toNativeSeparators(path).toHtmlEscaped()));
        line->setTextFormat(Qt::RichText);
        line->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(line);
    }
    layout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *buttons = new QDialogButtonBox(this);
    auto *aboutQt = buttons->addButton(tr.menuAboutQt.chopped(1).trimmed(), QDialogButtonBox::HelpRole);
    buttons->addButton(tr.close, QDialogButtonBox::AcceptRole);
    connect(aboutQt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(this); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(scroll);
    outer->addWidget(buttons);

    const int width = std::clamp(screen.width() - 80, 300, 620);
    // Fenêtre très petite (interface fortement agrandie) : la hauteur reste positive.
    const int height = std::max(screen.height() - 140, 120);
    const int wanted = layout->totalHeightForWidth(width - 40) + buttons->sizeHint().height() + 40;
    resize(width, std::min(height, wanted));
}
