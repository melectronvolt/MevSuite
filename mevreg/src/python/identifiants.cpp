#include "python/identifiants.h"

#include <QRegularExpression>
#include <QStringView>

namespace python {
namespace {

// Cette lecture sert aussi lorsque l'élève est encore en train de taper un
// script incomplet. Elle ne démarre pas l'interpréteur.
class LectureNoms {
public:
    explicit LectureNoms(QStringView source) : m_source(source) {}

    QStringList lit()
    {
        lit_code(false);
        return m_noms;
    }

private:
    void lit_format()
    {
        // Dans « {U:.2f} », f décrit l'affichage. Une largeur variable,
        // comme « {U:{largeur}.2f} », contient une nouvelle expression.
        while (m_position < m_source.size()) {
            const QChar caractere = m_source[m_position++];
            if (caractere == u'}')
                return;
            if (caractere == u'{')
                lit_code(true);
        }
    }

    void saute_nombre()
    {
        // Le e de 1e-3 et le j d'un nombre complexe appartiennent à la
        // constante, même si une grandeur du tableur porte ce nom.
        static const QRegularExpression nombre(QStringLiteral(
            R"(0[xX][\da-fA-F_]+|0[bB][01_]+|0[oO][0-7_]+|[0-9][0-9_]*(?:\.[0-9_]*)?(?:[eE][+-]?[0-9][0-9_]*)?[jJ]?)"));
        const auto lecture = nombre.matchView(m_source, m_position - 1, QRegularExpression::NormalMatch,
                                              QRegularExpression::AnchorAtOffsetMatchOption);
        if (lecture.hasMatch())
            m_position = lecture.capturedEnd();
    }

    void lit_code(bool dansChamp)
    {
        qsizetype imbrication = 0;
        bool apresPoint = false;
        while (m_position < m_source.size()) {
            const QChar c = m_source[m_position++];
            if (dansChamp && imbrication == 0) {
                if (c == u'}')
                    return;
                if (c == u':') {
                    lit_format();
                    return;
                }
                if (c == u'!' && m_position < m_source.size() && m_source[m_position] != u'=') {
                    ++m_position; // conversion !r, !s ou !a
                    continue;
                }
            }
            if (c == u'{' || c == u'(' || c == u'[')
                ++imbrication;
            else if (imbrication > 0 && (c == u'}' || c == u')' || c == u']'))
                --imbrication;
            if (c == u'#') {
                while (m_position < m_source.size() && m_source[m_position] != u'\n')
                    ++m_position;
            } else if (c == u'"' || c == u'\'') {
                lit_chaine(c, false);
            } else if (c.isDigit()) {
                saute_nombre();
            } else if (c.isLetter() || c == u'_') {
                const auto debut = m_position - 1;
                while (m_position < m_source.size() && (m_source[m_position].isLetterOrNumber() || m_source[m_position] == u'_'))
                    ++m_position;
                const QString nom = m_source.mid(debut, m_position - debut).toString();
                const auto suivant = m_source.mid(m_position, 1);
                if (suivant == u"'" || suivant == u"\"") {
                    const QString prefixe = nom.toLower();
                    const bool formate = prefixe == u"f" || prefixe == u"fr" || prefixe == u"rf"
                                      || prefixe == u"t" || prefixe == u"tr" || prefixe == u"rt";
                    if (formate || prefixe == u"r" || prefixe == u"b" || prefixe == u"br" || prefixe == u"rb" || prefixe == u"u") {
                        lit_chaine(m_source[m_position++], formate);
                        apresPoint = false;
                        continue;
                    }
                }
                if (!apresPoint && !m_noms.contains(nom))
                    m_noms.append(nom);
            }
            if (!c.isSpace() || c == u'\n')
                apresPoint = c == u'.';
        }
    }

    void lit_chaine(QChar guillemet, bool formatee)
    {
        const bool triple = m_source.mid(m_position, 2) == QString(2, guillemet);
        if (triple)
            m_position += 2;
        while (m_position < m_source.size()) {
            const QChar c = m_source[m_position++];
            if (c == guillemet) {
                if (!triple)
                    return;
                if (m_source.mid(m_position, 2) == QString(2, guillemet)) {
                    m_position += 2;
                    return;
                }
            } else if (c == u'\\' && m_position < m_source.size() && m_source[m_position] != u'{') {
                ++m_position;
            } else if (formatee && c == u'{') {
                if (m_position < m_source.size() && m_source[m_position] == u'{')
                    ++m_position;
                else
                    lit_code(true);
            } else if (!triple && c == u'\n') {
                return;
            }
        }
    }

    QStringView m_source;
    qsizetype m_position = 0;
    QStringList m_noms;
};

} // namespace

QStringList identifiants(const QString &code)
{
    return LectureNoms(code).lit();
}

} // namespace python
