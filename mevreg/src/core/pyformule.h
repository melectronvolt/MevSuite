// Traduction des formules du tableur pour le script exporté vers Capytale.
#pragma once

#include "core/expr.h"

class Document;

namespace pyexport {

class FormulesPython {
public:
    explicit FormulesPython(const Document *document = nullptr) : m_document(document) {}
    QString nom(const QString &nom) const;
    QString formule(const expr::Expr &expression);
    QString noeud(const expr::Node *e, bool aligne = false);
    /// Définitions Python nécessaires aux formules déjà traduites.
    QString fonctions_auxiliaires() const;

private:
    QString par_ligne(const QString &operation, const QString &a, const QString &b);
    const Document *m_document;
    bool m_lignes = false;
    bool m_reduction = false;
    bool m_derivee = false;
    bool m_integrale = false;
};

} // namespace pyexport
