// Fenêtre Script Python : éditeur (numéros de ligne, coloration, ligne de l'erreur), console,
// exécution et arrêt.

#pragma once

#include "core/plotspec.h"

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTimer>
#include <QWidget>

#include <optional>

class MainWindow;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTextBrowser;
class QToolButton;

/// Coloration syntaxique de Python (mots-clés, fonctions usuelles, chaînes, nombres, commentaires).
class PythonHighlighter : public QSyntaxHighlighter {
public:
    explicit PythonHighlighter(QTextDocument *doc);
    void definit_sombre(bool dark);

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat m_keyword, m_builtin, m_string, m_number, m_comment, m_def;
};

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    /// Ligne surlignée (erreur), numérotée à partir de 1.
    void definit_ligne_erreur(std::optional<int> line);
    int largeur_marge() const;
    void dessine_marge(QPaintEvent *e);
    /// Police du code (13,5 px à 100 %) et ce qui en dépend : elle suit l'agrandissement.
    void ajuste_police();

Q_SIGNALS:
    void execution_demandee();

protected:
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void maj_selections();

    QWidget *m_gutter;
    std::optional<int> m_errorLine;
};

class ScriptPanel : public QWidget {
    Q_OBJECT

public:
    ScriptPanel(MainWindow &win, ViewId id, QWidget *parent = nullptr);

    /// Copie le texte de l'éditeur dans la séance, même si le délai après la frappe court encore.
    void transmet_saisie();
    CodeEditor *editeur() const { return m_editor; }

private:
    void rafraichit();
    void maj_etat_execution();
    void maj_console();
    void lance();

    MainWindow &m_win;
    ViewId m_id;
    CodeEditor *m_editor;
    PythonHighlighter *m_highlighter;
    QTextBrowser *m_console;
    QLabel *m_runInfo;
    QToolButton *m_run;
    QToolButton *m_stop;
    QLabel *m_busy;
    QCheckBox *m_linked;
    QLineEdit *m_name;
    QTimer m_codeTimer;
    QTimer m_busyTimer;
    int m_busyFrame = 0;
};
