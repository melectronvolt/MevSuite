// Fenêtre de dessin : figure produite par le module Python `fenetre`, coordonnées sous le
// pointeur en unités physiques.

#pragma once

#include "core/drawing.h"

#include <QWidget>

#include <optional>

class MainWindow;

class DrawingCanvas : public QWidget {
public:
    DrawingCanvas(MainWindow &win, const QString &name, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    MainWindow &m_win;
    QString m_name;
    std::optional<drawing::View> m_view;
    std::optional<QPointF> m_hover;
};

class DrawingView : public QWidget {
    Q_OBJECT

public:
    DrawingView(MainWindow &win, const QString &name, QWidget *parent = nullptr);

private:
    MainWindow &m_win;
    QString m_name;
    DrawingCanvas *m_canvas;
};
