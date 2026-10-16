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
    void setDark(bool dark);

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
    void setErrorLine(std::optional<int> line);
    int gutterWidth() const;
    void paintGutter(QPaintEvent *e);
    /// Police du code (13,5 px à 100 %) et ce qui en dépend : elle suit l'agrandissement.
    void fitFont();

Q_SIGNALS:
    void runRequested();

protected:
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void updateSelections();

    QWidget *m_gutter;
    std::optional<int> m_errorLine;
};

class ScriptPanel : public QWidget {
    Q_OBJECT

public:
    ScriptPanel(MainWindow &win, ViewId id, QWidget *parent = nullptr);

    /// Enregistre tout de suite le code saisi (avant une exécution, un enregistrement).
    void flush();
    CodeEditor *editor() const { return m_editor; }

private:
    void refresh();
    void updateRunState();
    void updateConsole();
    void run();

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
