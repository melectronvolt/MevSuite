#pragma once

#include <QProxyStyle>
#include <QStyleOptionMenuItem>

namespace mevsuite {

class StyleMenu : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &taille,
                           const QWidget *widget) const override
    {
        QSize t = QProxyStyle::sizeFromContents(type, option, taille, widget);
        if (type == CT_MenuBarItem)
            return {t.width() + t.height() / 4, qRound(t.height() * 1.1)};
        if (type == CT_MenuItem && a_une_icone_et_une_coche(option))
            t.rwidth() += option->fontMetrics.horizontalAdvance(coche);
        return t;
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *p,
                     const QWidget *widget) const override
    {
        if (element == CE_MenuItem && a_une_icone_et_une_coche(option)) {
            auto ligne = *qstyleoption_cast<const QStyleOptionMenuItem *>(option);
            // Le style dessine le pictogramme entier. La coche suit le libellé, avant le raccourci.
            if (ligne.checked) {
                const auto tab = ligne.text.indexOf(u'\t');
                ligne.text.insert(tab < 0 ? ligne.text.size() : tab, coche);
            }
            ligne.checked = false;
            ligne.checkType = QStyleOptionMenuItem::NotCheckable;
            QProxyStyle::drawControl(element, &ligne, p, widget);
            return;
        }
        QProxyStyle::drawControl(element, option, p, widget);
    }

private:
    static inline const QString coche = QStringLiteral("  ✓");

    static bool a_une_icone_et_une_coche(const QStyleOption *option)
    {
        const auto *ligne = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        return ligne && !ligne->icon.isNull() && ligne->checkType != QStyleOptionMenuItem::NotCheckable;
    }
};

} // namespace mevsuite
