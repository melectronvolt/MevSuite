#include "app/about.h"
#include "app/store.h"

#include "app/theme.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSvgRenderer>
#include <QVBoxLayout>

#include <Python.h>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

struct Component {
    QString name, role, licence, url;
};

std::vector<Component> components()
{
    const QString py = QString::fromLatin1(Py_GetVersion()).section(u' ', 0, 0);
    return {
        {u"GCC (libstdc++)"_s, u"compilateur C++ et bibliothèque standard"_s, u"GPL-3.0+ WITH GCC-exception-3.1"_s, u"https://gcc.gnu.org"_s},
        {u"Qt %1"_s.arg(QString::fromLatin1(qVersion())), u"interface graphique, dessin, exports SVG et PDF, impression"_s, u"LGPL-3.0-only"_s,
         u"https://www.qt.io"_s},
        {u"CPython %1"_s.arg(py), u"interpréteur Python intégré"_s, u"PSF-2.0"_s, u"https://www.python.org"_s},
        {u"DejaVu Sans, DejaVu Sans Mono"_s, u"polices des graphes et des documents"_s, u"Bitstream Vera / DejaVu"_s, u"https://dejavu-fonts.github.io"_s},
        {u"MinGW-w64"_s, u"bibliothèques d'exécution C (version Windows)"_s, u"ZPL-2.1, MIT, GPL-3.0+ WITH GCC-exception-3.1"_s, u"https://www.mingw-w64.org"_s},
    };
}

QFont pixelFont(QFont font, double px, bool bold = false)
{
    font.setPixelSize(static_cast<int>(std::lround(px)));
    font.setBold(bold);
    return font;
}

/// Taille relative à la police de l'interface.
QFont scaledFont(QFont font, double factor, bool bold = false)
{
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(font.pointSizeF() * factor);
    else
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(font.pixelSize() * factor))));
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

/// Licence des polices DejaVu, intégrées au programme.
void showFontLicence(QWidget *parent)
{
    QFile f(u":/fonts/LICENSE-DejaVu.txt"_s);
    if (!f.open(QIODevice::ReadOnly))
        return;
    auto *v = new QDialog(parent);
    v->setAttribute(Qt::WA_DeleteOnClose);
    v->setWindowTitle(u"Licence des polices DejaVu"_s);
    v->resize(640, 520);
    auto *vl = new QVBoxLayout(v);
    auto *t = new QPlainTextEdit(QString::fromUtf8(f.readAll()), v);
    t->setReadOnly(true);
    t->setFont(theme::fixedFont());
    vl->addWidget(t);
    auto *b = new QDialogButtonBox(QDialogButtonBox::Close, v);
    QObject::connect(b, &QDialogButtonBox::rejected, v, &QDialog::reject);
    vl->addWidget(b);
    v->open();
}

} // namespace

namespace about {

QIcon icon()
{
    return QIcon(u":/icons/mevreg.svg"_s);
}

void show(QWidget *parent)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(u"À propos de MeVReg"_s);
    const QRect screen = parent && parent->screen() ? parent->screen()->availableGeometry() : QRect(0, 0, 800, 600);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    const auto label = [&](const QString &text) {
        auto *l = new QLabel(text, content);
        l->setWordWrap(true);
        return l;
    };
    // toute la largeur, texte centré : une étiquette qui passe à la ligne garde la bonne hauteur
    const auto centered = [&](QLabel *l) {
        l->setAlignment(Qt::AlignHCenter);
        layout->addWidget(l);
    };

    auto *logo = new QLabel(content);
    logo->setPixmap(icon().pixmap(QSize(96, 96), dlg->devicePixelRatioF()));
    logo->setAlignment(Qt::AlignHCenter);
    layout->addWidget(logo);
    auto *name = label(NAME);
    name->setFont(pixelFont(dlg->font(), 34, true));
    centered(name);
    centered(label(u"Version %1"_s.arg(VERSION)));
    centered(label(TAGLINE));
    layout->addSpacing(6);
    auto *author = label(u"Auteur : "_s + AUTHOR);
    author->setFont(scaledFont(dlg->font(), 1.3));
    centered(author);
    auto *email = label(u"Contact : <a href=\"mailto:%1\">%1</a>"_s.arg(EMAIL));
    email->setTextFormat(Qt::RichText);
    email->setOpenExternalLinks(true);
    centered(email);
    layout->addSpacing(10);
    layout->addWidget(separator(content));

    auto *heading = label(u"Logiciels libres utilisés"_s);
    heading->setFont(scaledFont(dlg->font(), 1.4, true));
    layout->addWidget(heading);
    layout->addWidget(label(u"MeVReg est écrit en C++ et s'appuie sur les logiciels libres suivants. Merci à leurs auteurs !"_s));

    // composants : nom (lien vers le site), rôle et licence en dessous
    const QString weak = theme::weakColor().name();
    QString rows;
    for (const Component &c : components()) {
        rows += u"<tr><td style=\"white-space:nowrap\"><a href=\"%1\">%2</a></td>"
                u"<td>%3<br><span style=\"color:%4\">%5</span></td></tr>"_s.arg(c.url, c.name.toHtmlEscaped(), c.role.toHtmlEscaped(), weak,
                                                                             c.licence.toHtmlEscaped());
    }
    auto *table = label(u"<table cellspacing=\"5\">%1</table>"_s.arg(rows));
    table->setTextFormat(Qt::RichText);
    table->setOpenExternalLinks(true);
    layout->addWidget(table);

    auto *note = label(u"MeVReg est un logiciel libre, distribué sous licence MIT, sans aucune garantie ; son code source est publié avec "
                       "chaque version. Qt est distribué sous licence LGPL version 3 : ses bibliothèques sont liées dynamiquement et peuvent "
                       "être remplacées par une autre version compatible ; le bouton « À propos de Qt » donne sa version et ses conditions "
                       "de licence."_s);
    note->setFont(scaledFont(dlg->font(), 0.85));
    layout->addWidget(note);

    layout->addWidget(separator(content));
    const QString ini = settingsFile();
    auto *line = label(u"<span style=\"color:%1\">Fichier de réglages :</span> <code>%2</code>"_s.arg(weak, QDir::toNativeSeparators(ini).toHtmlEscaped()));
    line->setTextFormat(Qt::RichText);
    line->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(line);
    layout->addStretch();

    auto *scroll = new QScrollArea(dlg);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *buttons = new QDialogButtonBox(dlg);
    auto *aboutQt = buttons->addButton(u"À propos de Qt"_s, QDialogButtonBox::HelpRole);
    auto *fonts = buttons->addButton(u"Licence des polices…"_s, QDialogButtonBox::ActionRole);
    buttons->addButton(u"Fermer"_s, QDialogButtonBox::AcceptRole);
    QObject::connect(aboutQt, &QPushButton::clicked, dlg, [dlg] { QMessageBox::aboutQt(dlg); });
    QObject::connect(fonts, &QPushButton::clicked, dlg, [dlg] { showFontLicence(dlg); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);

    auto *outer = new QVBoxLayout(dlg);
    outer->addWidget(scroll);
    outer->addWidget(buttons);

    const int width = std::clamp(screen.width() - 80, 300, 640);
    // fenêtre très petite : la hauteur reste positive
    const int height = std::max(screen.height() - 140, 120);
    const int wanted = layout->totalHeightForWidth(width - 40) + buttons->sizeHint().height() + 40;
    dlg->resize(width, std::min(height, wanted));
    dlg->open();
}

} // namespace about

// --- Écran de démarrage ------------------------------------------------------------------------

namespace {
constexpr double HOLD = 2.0;
constexpr double FADE = 0.5;
} // namespace

Splash::Splash(QWidget *parent) : QWidget(parent)
{
    setObjectName(u"splash"_s);
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
    QWidget::show();
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
    // les clics ne traversent pas l'écran de démarrage
    skip();
    event->accept();
}

void Splash::paintEvent(QPaintEvent *)
{
    const double alpha = 1.0 - std::clamp((elapsed() - m_start - HOLD) / FADE, 0.0, 1.0);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(alpha);
    // fond propre à MeVReg : le vert sombre de son icône, plus foncé
    painter.fillRect(rect(), QColor(12, 28, 22));

    const QRectF r(rect());
    const QPointF c = r.center();
    const double side = std::min(r.height() * 0.3, 180.0);
    QSvgRenderer(u":/icons/mevreg.svg"_s).render(&painter, QRectF(c.x() - side / 2, c.y() - 150.0 - side / 2, side, side));

    const auto text = [&](QPointF pos, const QString &s, double size, const QColor &color, bool bold = false) {
        painter.setFont(pixelFont(font(), size, bold));
        painter.setPen(color);
        painter.drawText(QRectF(pos.x() - r.width() / 2, pos.y() - size, r.width(), 2 * size), Qt::AlignCenter, s);
    };
    text(c + QPointF(0, -20), about::NAME, 64, Qt::white);
    text(c + QPointF(0, 34), about::TAGLINE, 18, QColor(190, 190, 190));
    text(c + QPointF(0, 100), about::AUTHOR, 28, Qt::white);
    text(c + QPointF(0, 138), about::EMAIL, 16, QColor(160, 160, 160));
    text(QPointF(c.x(), r.bottom() - 24), u"Version %1"_s.arg(about::VERSION), 13, QColor(120, 120, 120));
}
