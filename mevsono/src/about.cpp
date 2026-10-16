#include "about.h"

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

struct Component {
    QString name;
    QString licence;
    QString url;
};

/// Composants principaux : nom, licence, site. Leur rôle est traduit dans `Tr::roles`.
std::array<Component, MAIN_COMPONENT_COUNT> mainComponents()
{
    return {{
        {u"GCC (libstdc++)"_s, u"GPL-3.0+ WITH GCC-exception-3.1"_s, u"https://gcc.gnu.org"_s},
        {u"Qt %1"_s.arg(QString::fromLatin1(qVersion())), u"LGPL-3.0-only"_s, u"https://www.qt.io"_s},
        {u"Qt Multimedia"_s, u"LGPL-3.0-only"_s, u"https://doc.qt.io/qt-6/qtmultimedia-index.html"_s},
        {u"FFmpeg"_s, u"LGPL-2.1-or-later / GPL-2.0-or-later"_s, u"https://ffmpeg.org"_s},
        {u"MinGW-w64"_s, u"ZPL-2.1, MIT, GPL-3.0+ WITH GCC-exception-3.1"_s, u"https://www.mingw-w64.org"_s},
    }};
}

QFont pixelFont(QFont font, double px, bool bold = false)
{
    font.setPixelSize(static_cast<int>(std::lround(px)));
    font.setBold(bold);
    return font;
}

QFrame *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

QPixmap about::icon()
{
    return QPixmap(u":/icons/mevsono-256.png"_s);
}

// ---------------------------------------------------------------- écran d'accueil

namespace {
constexpr double HOLD = 2.0;
constexpr double FADE = 0.5;
} // namespace

Splash::Splash(const Tr &tr, QWidget *parent)
    : QWidget(parent)
    , m_tr(tr)
    , m_icon(about::icon())
{
    setGeometry(parent->rect());
    parent->installEventFilter(this);
    qApp->installEventFilter(this);
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (elapsed() - m_start >= HOLD + FADE)
            deleteLater();
        else
            update();
    });
    m_timer.start(16);
    raise();
    show();
}

double Splash::elapsed() const
{
    return double(m_clock.elapsed()) / 1000.0;
}

void Splash::skip()
{
    if (elapsed() - m_start < HOLD)
        m_start = elapsed() - HOLD;
}

bool Splash::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    if (event->type() == QEvent::KeyPress)
        skip();
    return false;
}

void Splash::mousePressEvent(QMouseEvent *event)
{
    // Les clics ne traversent pas l'écran d'accueil.
    skip();
    event->accept();
}

void Splash::paintEvent(QPaintEvent *)
{
    const double alpha = 1.0 - std::clamp((elapsed() - m_start - HOLD) / FADE, 0.0, 1.0);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(alpha);
    painter.fillRect(rect(), QColor(20, 16, 34));

    const QRectF r(rect());
    const QPointF c = r.center();
    const double side = std::min(r.height() * 0.3, 180.0);
    painter.drawPixmap(QRectF(c.x() - side / 2, c.y() - 150.0 - side / 2, side, side), m_icon, m_icon.rect());

    const auto text = [&](QPointF pos, const QString &s, double size, const QColor &color, bool bold = false) {
        painter.setFont(pixelFont(font(), size, bold));
        painter.setPen(color);
        painter.drawText(QRectF(pos.x() - r.width() / 2, pos.y() - size, r.width(), 2 * size), Qt::AlignCenter, s);
    };
    text(c + QPointF(0, -20), about::NAME, 64, Qt::white);
    text(c + QPointF(0, 34), m_tr.tagline, 18, QColor(190, 190, 190));
    text(c + QPointF(0, 100), about::AUTHOR, 28, Qt::white);
    text(c + QPointF(0, 138), about::EMAIL, 16, QColor(160, 160, 160));
    text(QPointF(c.x(), r.bottom() - 24), m_tr.version(about::VERSION), 13, QColor(120, 120, 120));
}

// ---------------------------------------------------------------- « À propos »

AboutDialog::AboutDialog(const Tr &tr, const SettingsFiles &settings, QWidget *parent)
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

    auto *logo = new QLabel(content);
    logo->setPixmap(about::icon().scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignHCenter);
    layout->addWidget(logo);
    auto *name = label(about::NAME);
    name->setFont(pixelFont(font(), 34, true));
    centered(name);
    centered(label(tr.version(about::VERSION)));
    centered(label(tr.tagline));
    layout->addSpacing(6);
    auto *author = label(tr.author + u' ' + about::AUTHOR);
    QFont authorFont = font();
    authorFont.setPointSizeF(authorFont.pointSizeF() * 1.3);
    author->setFont(authorFont);
    centered(author);
    auto *email = label(u"%1 <a href=\"mailto:%2\">%2</a>"_s.arg(tr.contact.toHtmlEscaped(), about::EMAIL));
    email->setTextFormat(Qt::RichText);
    email->setOpenExternalLinks(true);
    centered(email);
    layout->addSpacing(10);
    layout->addWidget(separator(content));

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
    const auto components = mainComponents();
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
        layout->addWidget(separator(content));
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

    const int width = std::clamp(screen.width() - 80, 300, 640);
    // Fenêtre très petite (interface fortement agrandie) : la hauteur reste positive.
    const int height = std::max(screen.height() - 140, 120);
    const int wanted = layout->totalHeightForWidth(width - 40) + buttons->sizeHint().height() + 40;
    resize(width, std::min(height, wanted));
}
