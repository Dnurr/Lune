#include "keybutton.h"

#include <QContextMenuEvent>
#include <QPainter>
#include <QPainterPath>

#include "frontend/theme/theme.h"

KeyButton::KeyButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setObjectName("KeyButton");
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(sizeHint());
    setState(State::None);
}

QColor KeyButton::keyColor()
{
    return QColor(0xE3, 0xB3, 0x41);
}

QColor KeyButton::animatedColor()
{
    return QColor(0x63, 0xB5, 0x7E);
}

void KeyButton::setState(State state)
{
    m_state = state;
    switch (state) {
    case State::None: setToolTip(tr("Insert a key at this frame (right click: more)")); break;
    case State::Animated: setToolTip(tr("Animated: insert a key at this frame (right click: more)")); break;
    case State::Key: setToolTip(tr("Delete the key at this frame (right click: more)")); break;
    }
    update();
}

void KeyButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = QRectF(rect()).center();
    const qreal r = 5.5;
    QPainterPath diamond;
    diamond.moveTo(c.x(), c.y() - r);
    diamond.lineTo(c.x() + r, c.y());
    diamond.lineTo(c.x(), c.y() + r);
    diamond.lineTo(c.x() - r, c.y());
    diamond.closeSubpath();

    QColor line;
    QColor fill = Qt::transparent;
    switch (m_state) {
    case State::None:
        line = Theme::currentPalette().onSurfaceVariant;
        line.setAlphaF(m_hover ? 0.9f : 0.45f);
        break;
    case State::Animated:
        line = animatedColor();
        fill = animatedColor();
        fill.setAlphaF(m_hover ? 0.45f : 0.2f);
        break;
    case State::Key:
        line = keyColor();
        fill = keyColor();
        if (m_hover)
            fill = fill.lighter(115);
        break;
    }
    p.setPen(QPen(line, 1.4));
    p.setBrush(fill);
    p.drawPath(diamond);
}

void KeyButton::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

void KeyButton::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void KeyButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}
