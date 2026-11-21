#include "fichier_seance.h"

#include "ecriture_image.h"
#include "projet.h"
#include "savefile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace {

// Le codeur PNG écrit la photo par morceaux. Chaque morceau alimente à la fois le fichier
// temporaire et l'empreinte. Une grande photo n'a donc pas à être recompressée ni relue.
class SortieAvecEmpreinte : public QIODevice {
public:
    explicit SortieAvecEmpreinte(QSaveFile &fichier) : m_fichier(fichier) { open(WriteOnly); }
    bool isSequential() const override { return true; }
    QByteArray empreinte() const { return m_calcul.result(); }

protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *donnees, qint64 taille) override
    {
        const qint64 ecrits = m_fichier.write(donnees, taille);
        if (ecrits > 0)
            m_calcul.addData(QByteArrayView(donnees, ecrits));
        else if (ecrits < 0)
            setErrorString(m_fichier.errorString());
        return ecrits;
    }

private:
    QSaveFile &m_fichier;
    QCryptographicHash m_calcul{QCryptographicHash::Sha256};
};

} // namespace

QString chemin_reel_seance(const QString &chemin)
{
    const QFileInfo info(chemin);
    const QString existant = info.canonicalFilePath();
    if (!existant.isEmpty())
        return existant;
    const QString dossier = info.dir().canonicalPath();
    return dossier.isEmpty() ? info.absoluteFilePath() : QDir(dossier).filePath(info.fileName());
}

QByteArray empreinte_lecture(QFile &fichier)
{
    QCryptographicHash calcul(QCryptographicHash::Sha256);
    if (!calcul.addData(&fichier) || fichier.error() != QFileDevice::NoError)
        return {};
    return calcul.result();
}

VersionFichier empreinte_sur_disque(const QString &chemin)
{
    const QFileInfo info(chemin);
    if (!info.exists())
        return {QByteArray{}, {}};
    // Une séance est un fichier ordinaire. La lecture d'un tube pourrait attendre
    // indéfiniment que quelqu'un y écrive, celle d'un périphérique ne jamais finir.
    if (!info.isFile())
        return {std::nullopt, {}, EtatSauvegarde::FichierSpecial};
    QFile fichier(chemin);
    if (fichier.open(QIODevice::ReadOnly)) {
        const QByteArray empreinte = empreinte_lecture(fichier);
        if (!empreinte.isEmpty())
            return {empreinte, {}};
    }
    return {std::nullopt, fichier.errorString()};
}

ResultatSauvegarde enregistre_seance(const QString &chemin, const ContenuSeance &seance, const QByteArray &empreinteAttendue)
{
    if (!seance.image)
        return {EtatSauvegarde::Erreur, {}, {}};
    const QByteArray entete = donnees_seance(seance) + '\n';
    if (entete.size() >= TAILLE_MAX_ENTETE_SEANCE)
        return {EtatSauvegarde::EnteteTropGrande, {}, {}};

    // Deux fenêtres peuvent travailler sur la même séance. Une seule à la fois vérifie puis
    // remplace le fichier sur disque. Le verrou est libéré à la sortie de cette fonction.
    const QString cible = chemin_reel_seance(chemin);
    QLockFile verrou(cible + u".lock"_s);
    verrou.setStaleLockTime(0);
    if (!verrou.tryLock()) {
        const auto etat = verrou.error() == QLockFile::LockFailedError ? EtatSauvegarde::Occupe
                                                                       : EtatSauvegarde::VerrouImpossible;
        return {etat, {}, {}};
    }

    const QFileInfo original(cible);
    if (original.exists() && !original.isFile())
        return {EtatSauvegarde::FichierSpecial, {}, {}};
    // Un ancien fichier supprimé ou un nom déjà pris sont détectables avant la compression du PNG.
    const bool fichierAttendu = !empreinteAttendue.isEmpty();
    if (original.exists() != fichierAttendu)
        return {EtatSauvegarde::Conflit, {}, {}};
    QSaveFile fichier(cible);
    if (!fichier.open(QIODevice::WriteOnly))
        return {EtatSauvegarde::Erreur, {}, fichier.errorString()};
    SortieAvecEmpreinte sortie(fichier);
    if (!ecrit_tout(sortie, SIGNATURE_SEANCE) || !ecrit_tout(sortie, entete))
        return {EtatSauvegarde::Erreur, {}, fichier.errorString()};
    QString erreur;
    if (!ecriture_image::ecrit(sortie, seance.image->rgb, "PNG", &erreur))
        return {EtatSauvegarde::Erreur, {}, erreur};

    // QSaveFile a écrit dans un fichier temporaire. Vérifier l'original au dernier moment.
    // Toute sortie avant commit() laisse l'original en place.
    const auto version = empreinte_sur_disque(cible);
    if (!version.empreinte)
        return {version.causeEchec, {}, version.detail};
    if (*version.empreinte != empreinteAttendue)
        return {EtatSauvegarde::Conflit, {}, {}};
    if (!fichier.commit())
        return {EtatSauvegarde::Erreur, {}, fichier.errorString()};
    return {EtatSauvegarde::Enregistree, sortie.empreinte(), {}};
}
