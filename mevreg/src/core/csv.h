// Import et export de tableaux de valeurs : fichiers CSV et copier-coller depuis un tableur
// (Excel, LibreOffice, Regressi, Latis Pro…).

#pragma once

#include "core/document.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace csv {

/// Colonne lue dans un fichier ou dans le presse-papiers.
struct Column {
    QString name;
    QString unit;
    std::vector<double> values;
};

struct Parsed {
    std::vector<Column> columns;
    /// Nombre de cellules non numériques ignorées.
    int ignored = 0;
};

/// Découpe le texte en cellules (séparateur détecté : tabulation, point-virgule, virgule, espaces).
std::vector<QStringList> splitCells(const QString &text);

// --- lecture réglée d'un fichier (aperçu avant import) ----------------------------------------

/// Séparateur décimal : « 3,14 » ou « 3.14 ». L'autre signe rend la case illisible, plutôt que
/// d'être deviné (« 1,234 » : un nombre décimal, ou un millier ?).
enum class Decimal { Comma, Point };

/// Séparateur des valeurs « une suite d'espaces ou de tabulations ».
inline constexpr QChar SPACES = u' ';

/// Réglages de lecture, proposés par `detect` et modifiables par l'utilisateur. Lignes numérotées
/// à partir de 0 ; -1 : aucune.
struct ReadOptions {
    QChar separator = u';';
    Decimal decimal = Decimal::Comma;
    int nameLine = -1;
    int unitLine = -1;
    /// Première ligne de valeurs ; les lignes suivantes sont toutes des valeurs (sauf celles des
    /// noms et des unités).
    int firstLine = 0;
    /// Lignes sans aucune valeur (vides, ou faites de séparateurs) ignorées ; sinon, elles donnent
    /// une rangée de cases vides.
    bool skipEmpty = true;
    bool operator==(const ReadOptions &) const = default;
};

/// Lignes du texte : marque d'ordre des octets et retours chariot ôtés, lignes vides de la fin
/// retirées.
QStringList textLines(const QString &text);
/// Cases d'une ligne, sans les espaces autour ; un séparateur entre guillemets en fait partie.
QStringList splitLine(const QString &line, QChar separator);
/// Nombre écrit avec le séparateur décimal donné.
std::optional<double> number(const QString &cell, Decimal decimal);
/// Réglages les plus probables ; `fallback` : séparateur décimal quand rien ne l'indique (entiers).
ReadOptions detect(const QStringList &lines, Decimal fallback = Decimal::Comma);

/// Case lue qui n'est pas un nombre (laissée vide).
struct BadCell {
    int line = -1;
    int column = -1;
    QString text;
};

/// Résultat d'une lecture, avec de quoi prévenir l'utilisateur.
struct Reading {
    /// Toutes les colonnes, même sans aucune valeur ; une case vide ou illisible vaut NaN (les
    /// cases vides de la fin sont retirées).
    std::vector<Column> columns;
    /// Ligne du fichier de chaque rangée de valeurs.
    std::vector<int> rows;
    /// Cases non numériques, la première d'entre elles, et combien seraient lues avec l'autre
    /// séparateur décimal.
    int badCells = 0;
    BadCell firstBad;
    int otherDecimal = 0;
    /// Nombre de cases le plus fréquent d'une rangée ; rangées qui en ont un autre, la première.
    int usualWidth = 0;
    int ragged = 0;
    int firstRagged = -1;
    /// Lignes sans valeur ignorées.
    int skipped = 0;
};
Reading read(const QStringList &lines, const ReadOptions &options);
/// Sépare « t (s) », « t [s] » ou « t / s » en nom et unité.
std::pair<QString, QString> splitHeader(const QString &cell);
/// Transforme un en-tête quelconque en nom de grandeur valable.
QString sanitizeName(const QString &raw, const QString &fallback);
/// Lit un tableau : lignes d'en-tête facultatives (noms, puis unités), puis valeurs.
std::optional<Parsed> parse(const QString &text, QString *error);
/// Ajoute les colonnes au document (noms rendus uniques) ; renvoie les grandeurs créées.
std::vector<QId> import(Document &doc, const Parsed &parsed);

struct ExportOptions {
    QChar separator;
    bool decimalComma;
    /// En-têtes : « nom (unité) » sur une ligne, le nom puis l'unité sur deux lignes, ou le nom seul.
    enum class Header { NameUnit, TwoLines, NameOnly } header = Header::NameUnit;
};

/// Codage des caractères du fichier écrit : UTF-8 avec sa marque (Excel reconnaît les accents),
/// UTF-8 seul, ou Windows-1252 (anciens logiciels).
enum class Encoding { Utf8Bom, Utf8, Windows1252 };
QByteArray encodeText(const QString &text, Encoding encoding);
/// Tableurs français : point-virgule et virgule décimale.
inline constexpr ExportOptions FRENCH{u';', true};
/// Format international : virgule et point décimal.
inline constexpr ExportOptions INTERNATIONAL{u',', false};

/// Écrit les grandeurs en colonnes, avec leurs en-têtes (« nom (unité) » par défaut).
QString exportText(const Document &doc, const std::vector<QId> &ids, ExportOptions opts);
/// Texte d'un fichier : UTF-8, sinon Windows-1252 (fichiers des vieux logiciels).
QString decodeText(const QByteArray &bytes);

} // namespace csv
