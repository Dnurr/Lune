#pragma once

#include <QLabel>

// Label you can press and drag horizontally to change a value ("scrubbing").
// It only reports the movement; whoever owns it decides what to change.
class DragLabel : public QLabel
{
    Q_OBJECT

public:
    explicit DragLabel(const QString &text, QWidget *parent = nullptr);

signals:
    // dx: horizontal pixels moved since the last signal (positive = right).
    void dragged(int dx, Qt::KeyboardModifiers modifiers);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool m_dragging = false;
    int m_lastX = 0; // global x of the cursor at the previous move
};

// Multiplier for a drag: Shift = fine (x0.1), Ctrl = coarse (x10).
double dragFactor(Qt::KeyboardModifiers modifiers);
