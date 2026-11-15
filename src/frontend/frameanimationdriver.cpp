#include "frameanimationdriver.h"

#include <QEvent>

FrameAnimationDriver::FrameAnimationDriver(QWindow *window, QObject *parent)
    : QAnimationDriver(parent)
    , m_window(window)
{
    m_clock.start();
    if (m_window)
        m_window->installEventFilter(this);
}

FrameAnimationDriver::~FrameAnimationDriver()
{
    uninstall(); // back to Qt's timer driver, so animations still running don't use a dead one
}

qint64 FrameAnimationDriver::elapsed() const
{
    // Time since start(): Qt adds it to the animation time at which the driver started.
    // Every step of a frame sees the same value, the time of that frame.
    return isRunning() ? m_frameTime : 0;
}

void FrameAnimationDriver::start()
{
    m_clock.restart();
    m_frameTime = 0;
    QAnimationDriver::start();
    if (m_window)
        m_window->requestUpdate();
}

void FrameAnimationDriver::stop()
{
    QAnimationDriver::stop();
}

bool FrameAnimationDriver::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::UpdateRequest && isRunning()) {
        // Before the window paints: the widgets the animations move are repainted in this
        // same frame. Then ask for the next frame while anything is still animating.
        m_frameTime = m_clock.elapsed();
        advance();
        if (isRunning())
            m_window->requestUpdate();
    }
    return QAnimationDriver::eventFilter(watched, event);
}
