#include "a_propos.h"

#include "i18n.h"
#include "prefs.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

struct Composant {
    QString nom;
    QString role;
    QString licence;
    QString adresse;
};

/// Les composants libres : nom, rôle dans MeVCine, licence, adresse
std::vector<Composant> composants()
{
    return {
        {u"pymecavideo"_s,
         trad("du code adapté (pointage, suivi automatique, trajectoires, grapheur)"),
         u"GPL-3.0-or-later"_s, u"https://gitlab.com/oppl/pymecavideo"_s},
        {u"Qt %1"_s.arg(QString::fromLatin1(qVersion())),
         trad("l'interface graphique, la lecture et l'écriture des images, les calculs en parallèle du suivi automatique"),
         u"LGPL-3.0-only"_s, u"https://www.qt.io"_s},
        {u"FFmpeg"_s,
         trad("la lecture des vidéos ; la version livrée (BtbN) contient aussi dav1d, libaom, libvpx, SVT-AV1, zimg, "
              "libplacebo…"),
         u"LGPL-3.0-or-later / GPL-3.0-or-later"_s, u"https://ffmpeg.org"_s},
        {u"GCC (libstdc++)"_s, trad("le compilateur C++ et sa bibliothèque standard"),
         u"GPL-3.0-or-later WITH GCC-exception-3.1"_s, u"https://gcc.gnu.org"_s},
        {u"mingw-w64"_s, trad("les bibliothèques d'exécution de la version Windows"),
         u"ZPL-2.1, MIT, GPL-3.0-or-later WITH GCC-exception-3.1"_s, u"https://www.mingw-w64.org"_s},
    };
}

QLabel *texte(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    l->setOpenExternalLinks(true);
    return l;
}

/// la police de l'interface, à une taille en points d'écran
QFont police_px(QFont f, double px, bool gras = false)
{
    f.setPixelSize(static_cast<int>(std::lround(px)));
    f.setBold(gras);
    return f;
}

QFrame *separation(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

} // namespace

QPixmap a_propos::icone()
{
    return QPixmap(u":/icones/mevcine-256.png"_s);
}

FenetreAPropos::FenetreAPropos(QWidget *parent)
    : QDialog(parent)
{
    // la même fenêtre que dans les autres logiciels de la suite
    setWindowTitle(trad("À propos de MeVCine"));
    auto *contenu = new QWidget;
    auto *lc = new QVBoxLayout(contenu);
    // toute la largeur, texte centré : une étiquette qui passe à la ligne garde la bonne hauteur
    const auto centre = [lc](QLabel *l) {
        l->setAlignment(Qt::AlignHCenter);
        lc->addWidget(l);
        return l;
    };
    const auto titre = [contenu, lc](const QString &t) {
        auto *l = new QLabel(t, contenu);
        l->setWordWrap(true);
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 1.4 : 14.0);
        f.setBold(true);
        l->setFont(f);
        lc->addWidget(l);
    };
    QFont petite = font();
    if (petite.pointSizeF() > 0)
        petite.setPointSizeF(petite.pointSizeF() * 0.85);

    auto *logo = new QLabel(contenu);
    logo->setPixmap(a_propos::icone().scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    centre(logo);
    centre(texte(a_propos::NOM, contenu))->setFont(police_px(font(), 34, true));
    centre(texte(trad("version {}", a_propos::VERSION), contenu));
    centre(texte(trad("Étude cinématique de vidéos"), contenu));
    lc->addSpacing(6);
    QLabel *auteur = centre(texte(trad("Auteur :") + u' ' + a_propos::AUTEUR, contenu));
    QFont f = auteur->font();
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * 1.3);
    auteur->setFont(f);
    QLabel *courriel =
        centre(texte(u"%1 <a href=\"mailto:%2\">%2</a>"_s.arg(trad("Contact :").toHtmlEscaped(), a_propos::COURRIEL), contenu));
    courriel->setTextFormat(Qt::RichText);
    lc->addSpacing(10);
    lc->addWidget(separation(contenu));

    // les logiciels libres : nom (lien vers le site), puis rôle et licence
    titre(trad("Logiciels libres utilisés"));
    const QString discret = palette().color(QPalette::PlaceholderText).name();
    QString lignes;
    for (const Composant &c : composants()) {
        lignes += u"<tr><td style=\"white-space:nowrap\"><a href=\"%1\">%2</a></td>"
                  "<td>%3<br><span style=\"color:%4\">%5</span></td></tr>"_s.arg(
                      c.adresse, c.nom.toHtmlEscaped(), c.role.toHtmlEscaped(), discret, c.licence.toHtmlEscaped());
    }
    auto *tableau = texte(u"<table cellspacing=\"5\">%1</table>"_s.arg(lignes), contenu);
    tableau->setTextFormat(Qt::RichText);
    lc->addWidget(tableau);
    auto *note = texte(trad("Le bouton « À propos de Qt » donne la licence de Qt et ses composants."), contenu);
    note->setFont(petite);
    lc->addWidget(note);

    titre(trad("Licence"));
    lc->addWidget(texte(trad("MeVCine est un logiciel libre : vous pouvez le redistribuer et le modifier selon les termes "
                             "de la licence GPL (GNU General Public License) publiée par la Free Software Foundation, "
                             "version 3 ou (à votre choix) toute version ultérieure. Il est distribué dans l'espoir qu'il "
                             "sera utile, mais SANS AUCUNE GARANTIE."),
                        contenu));
    auto *copyright = texte(u"Copyright (C) 2026 Rémi MEVAERE\n"_s + trad("Code adapté de pymecavideo :")
                                + u" Copyright (C) 2007-2016 Jean-Baptiste Butet <ashashiwa@gmail.com>,\n"
                                  "Copyright (C) 2007-2023 Georges Khaznadar <georgesk@debian.org>"_s,
                            contenu);
    copyright->setTextFormat(Qt::PlainText);
    copyright->setFont(petite);
    lc->addWidget(copyright);
    lc->addWidget(texte(u"<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">https://www.gnu.org/licenses/gpl-3.0.html</a>"_s,
                        contenu));

    // où sont les réglages, comme dans les autres logiciels de la suite
    lc->addWidget(separation(contenu));
    auto *reglages = texte(u"<span style=\"color:%1\">%2</span> <code>%3</code>"_s.arg(
                               discret, trad("Fichier de réglages :").toHtmlEscaped(),
                               QDir::toNativeSeparators(prefs::fichier_conf()).toHtmlEscaped()),
                           contenu);
    reglages->setTextFormat(Qt::RichText);
    reglages->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lc->addWidget(reglages);
    lc->addStretch(1);

    auto *defile = new QScrollArea(this);
    defile->setWidget(contenu);
    defile->setWidgetResizable(true);
    defile->setFrameShape(QFrame::NoFrame);
    defile->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *boutons = new QDialogButtonBox(this);
    QPushButton *qt = boutons->addButton(trad("À propos de Qt"), QDialogButtonBox::HelpRole);
    QPushButton *fermer = boutons->addButton(trad("Fermer"), QDialogButtonBox::AcceptRole);
    connect(qt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(this); });
    connect(fermer, &QPushButton::clicked, this, &QDialog::accept);

    auto *l = new QVBoxLayout(this);
    l->addWidget(defile, 1);
    l->addWidget(boutons);

    // les boutons restent visibles sous la partie qui défile
    const QScreen *ecran = parent && parent->screen() ? parent->screen() : QApplication::primaryScreen();
    const QRect dispo = ecran ? ecran->availableGeometry() : QRect(0, 0, 800, 600);
    const int largeur = std::clamp(dispo.width() - 80, 300, 640);
    // fenêtre très petite (interface fortement agrandie) : la hauteur reste positive
    const int hauteur = std::max(dispo.height() - 140, 120);
    const int voulue = lc->totalHeightForWidth(largeur - 40) + boutons->sizeHint().height() + 40;
    resize(largeur, std::min(hauteur, voulue));
}
