#pragma once

#include <QAbstractAnimation>
#include <QElapsedTimer>
#include <QPointer>
#include <QWindow>

// Advances Qt's animations (QPropertyAnimation & co.: panel slides and snaps) once per
// frame of a window, right before the window paints, instead of from Qt's own animation
// timer.
//
// Why: the default driver ticks on a ~16 ms timer of its own, and the window repaints on
// another ~16 ms timer. The two drift against each other, so some frames get two animation
// steps and others none: the motion stutters even when every frame is fast. Driven by the
// frames, each painted frame shows exactly one step, computed for the time it's painted.
class FrameAnimationDriver : public QAnimationDriver
{
    Q_OBJECT

public:
    // `window` is the top-level window whose frames drive the animations.
    explicit FrameAnimationDriver(QWindow *window, QObject *parent = nullptr);
    ~FrameAnimationDriver() override;

    qint64 elapsed() const override;

protected:
    void start() override;
    void stop() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    QElapsedTimer m_clock;  // restarted by start()
    qint64 m_frameTime = 0; // ms from start() to the frame being painted
};
