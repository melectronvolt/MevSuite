// Fenêtre Calculs : paramètres réglables par curseur et grandeurs calculées par formule.

#pragma once

#include <QWidget>

#include <functional>
#include <vector>

class MainWindow;
class QLabel;
class QLineEdit;
class QVBoxLayout;

class CalculationsPanel : public QWidget {
    Q_OBJECT

public:
    explicit CalculationsPanel(MainWindow &win, QWidget *parent = nullptr);

private:
    void refresh();
    void rebuild();
    QString signature() const;
    void addFormula();

    MainWindow &m_win;
    QWidget *m_params = nullptr;
    QWidget *m_formulas = nullptr;
    QVBoxLayout *m_paramsLayout = nullptr;
    QVBoxLayout *m_formulasLayout = nullptr;
    QLineEdit *m_newFormula = nullptr;
    QLabel *m_newError = nullptr;
    QString m_signature;
    std::vector<std::function<void()>> m_sync;
};
