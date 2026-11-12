#include "draglabel.h"

#include <QMouseEvent>

// ---------------------------------------------------------------------------
// DragLabel
// ---------------------------------------------------------------------------

DragLabel::DragLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent)
{
    setCursor(Qt::SizeHorCursor);
}

void DragLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastX = qRound(event->globalPosition().x());
        event->accept();
        return;
    }
    QLabel::mousePressEvent(event);
}

void DragLabel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QLabel::mouseMoveEvent(event);
        return;
    }
    // Global coordinates: stable even when the cursor leaves the label
    // (the label keeps receiving moves while the button is held).
    const int x = qRound(event->globalPosition().x());
    const int dx = x - m_lastX;
    m_lastX = x;
    if (dx != 0)
        emit dragged(dx, event->modifiers());
    event->accept();
}

void DragLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QLabel::mouseReleaseEvent(event);
}

double dragFactor(Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::ShiftModifier)
        return 0.1;
    if (modifiers & Qt::ControlModifier)
        return 10.0;
    return 1.0;
}
