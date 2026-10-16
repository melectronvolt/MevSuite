// Traduction de l'interface.
//
// Les textes sont écrits en français dans le code, dans `trad("…")`. Avec des arguments, `{}` marque
// leur place ; une traduction peut les réordonner avec `{0}`, `{1}`… La fonction ne s'appelle pas
// `tr` : dans une classe Qt, ce nom désignerait `QObject::tr`.
//
// Les traductions sont dans `data/lang/<code>.po`, au format gettext : on les modifie avec un éditeur
// de texte ou avec Poedit. Elles sont incluses dans l'exécutable (ressources Qt). Un texte non traduit
// reste en français. Le format et les textes sont ceux de MeVCine (version Rust) : un fichier .po se
// reprend de l'un à l'autre. Le modèle `data/lang/mevcine.pot` se refait avec
// `MEVCINE_ECRIT_POT=1 ./mevcine_tests modelePo`.
//
// Au démarrage, la langue est celle choisie avec le bouton du drapeau (retenue dans la
// configuration), sinon celle du système, sinon l'anglais.

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <array>
#include <concepts>
#include <optional>

enum class Langue { Francais, Anglais };

inline constexpr std::array<Langue, 2> TOUTES_LES_LANGUES = {Langue::Francais, Langue::Anglais};

namespace i18n {

/// le code ISO 639-1 de la langue, comme dans la configuration
QString code(Langue l);
/// le nom de la langue, dans cette langue
QString nom(Langue l);
/// La langue d'une étiquette de langue : « fr », « fr_FR.UTF-8 », « en-GB »…
std::optional<Langue> depuis_code(const QString &etiquette);
/// l'anglais écrit les nombres avec un point décimal ; le français avec une virgule
bool virgule_decimale(Langue l);

/// la langue du système, si MeVCine la connaît, sinon l'anglais
Langue langue_du_systeme();
/// la langue de l'interface
Langue langue();
/// change la langue de l'interface
void choisit(Langue l);

/// le contenu du fichier .po d'une langue (vide pour le français, langue des sources)
QString po(Langue l);
/// traduit un texte source (en français, codé en UTF-8) dans la langue de l'interface
QString traduit(const char *source);
/// Remplace les marques `{}` (dans l'ordre) et `{0}`, `{1}`… (par numéro) d'un modèle par les
/// arguments
QString remplit(const QString &modele, const QStringList &args);
/// Les couples (texte source, traduction) d'un fichier .po. Les entrées floues (« fuzzy ») et les
/// traductions vides sont ignorées.
QHash<QString, QString> analyse_po(const QString &texte);

/// un argument d'un texte traduit
inline QString arg(const QString &s) { return s; }
inline QString arg(const char *s) { return QString::fromUtf8(s); }
template <std::integral T> QString arg(T v) { return QString::number(v); }

} // namespace i18n

/// Traduit un texte de l'interface
inline QString trad(const char *source)
{
    return i18n::traduit(source);
}

/// Traduit un texte de l'interface, et place les arguments dans la traduction
template <typename... A> QString trad(const char *source, const A &...args)
{
    return i18n::remplit(i18n::traduit(source), {i18n::arg(args)...});
}
