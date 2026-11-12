#pragma once

#include "../panel.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QVector3D>

class QDoubleSpinBox;

enum class CoordColour
{
    Red,
    Green,
    Blue
};

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

class Coordinate : public QFrame
{
    Q_OBJECT

public:
    Coordinate(const QString &label, CoordColour colour, QWidget *parent = nullptr);

    double value() const;
    void setValue(double value);

    // Value change per pixel dragged on the label (before Shift/Ctrl).
    void setDragStep(double step) { m_dragStep = step; }
    double dragStep() const { return m_dragStep; }

signals:
    void valueChanged(double value);

private:
    QDoubleSpinBox *m_spin = nullptr;
    double m_dragStep = 0.01;
};

class Vector3 : public QWidget
{
    Q_OBJECT

public:
    Vector3(const QString &label, QWidget *parent = nullptr);

    QVector3D value() const;
    void setValue(const QVector3D &value);

    // Value change per pixel dragged, for the three coordinates and the "Vector3" label.
    void setDragStep(double step);

    // Width the label needs for its text, and a fixed width to force on it.
    // Give every Vector3 in a group the same label width (the widest one) so their
    // coordinates line up in columns with the same size (see alignLabels()).
    int labelWidthHint() const;
    void setLabelWidth(int width);

signals:
    void valueChanged(const QVector3D &value);

private:
    Coordinate *m_x = nullptr;
    Coordinate *m_y = nullptr;
    Coordinate *m_z = nullptr;
    DragLabel *m_label = nullptr;
    double m_dragStep = 0.01;
    bool m_batch = false; // true while changing all three at once: emit valueChanged only once
};

// Sets every label to the width of the widest one.
void alignLabels(const QList<Vector3 *> &vectors);

class TransformPanel : public Panel
{
    Q_OBJECT

public:
    explicit TransformPanel(QWidget *parent = nullptr);
};
