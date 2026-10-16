#include "dialogues.h"

#include "dessin.h"
#include "format.h"
#include "i18n.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

DialogueEchelle::DialogueEchelle(const QString &saisie, Unite unite, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(trad("Longueur de l'étalon"));
    auto *l = new QVBoxLayout(this);
    auto *titre = new QLabel(trad("Longueur de l'étalon"), this);
    dessin::demi_gras(titre);
    l->addWidget(titre);
    l->addWidget(new QLabel(trad("Quelle est la longueur réelle de l'étalon ? Choisissez aussi l'unité."), this));
    auto *ligne = new QHBoxLayout();
    m_champ = new QLineEdit(saisie, this);
    m_champ->setMinimumWidth(160);
    // à l'ouverture, la valeur proposée est sélectionnée : la saisie la remplace
    m_champ->selectAll();
    m_unite = new QComboBox(this);
    for (Unite u : TOUTES_LES_UNITES)
        m_unite->addItem(symbole(u), static_cast<int>(u));
    m_unite->setCurrentIndex(m_unite->findData(static_cast<int>(unite)));
    ligne->addWidget(m_champ);
    ligne->addWidget(m_unite);
    ligne->addStretch(1);
    l->addLayout(ligne);
    auto *exemples = new QLabel(trad("Exemples : 0,85 · 8,5E-1 · 2,5×10^-3 · 85 cm"), this);
    dessin::discret(exemples);
    l->addWidget(exemples);
    m_erreur = new QLabel(this);
    m_erreur->setWordWrap(true);
    dessin::colore(m_erreur, dessin::Ton::Erreur);
    m_erreur->hide();
    l->addWidget(m_erreur);
    auto *boutons = new QDialogButtonBox(this);
    QPushButton *ok = boutons->addButton(trad("OK"), QDialogButtonBox::AcceptRole);
    boutons->addButton(trad("Annuler"), QDialogButtonBox::RejectRole);
    ok->setDefault(true);
    connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_champ, &QLineEdit::textEdited, m_erreur, &QLabel::hide);
    connect(m_unite, &QComboBox::currentIndexChanged, m_erreur, &QLabel::hide);
    l->addWidget(boutons);
    m_champ->setFocus();
}

QString DialogueEchelle::erreur() const
{
    return m_erreur->isHidden() ? QString() : m_erreur->text();
}

void DialogueEchelle::accept()
{
    const auto l = parse_longueur(m_champ->text(), static_cast<Unite>(m_unite->currentData().toInt()));
    if (!l) {
        m_erreur->setText(l.error());
        m_erreur->show();
        return;
    }
    m_longueur = *l;
    QDialog::accept();
}
