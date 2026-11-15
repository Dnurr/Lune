#pragma once

#include <QFrame>

class QLabel;
class QToolButton;
class Viewport;

// Small glass bar at the top center of the free viewport area (between the panels on the
// left and on the right): Orbit / Free camera, and the flying speed while Free.
// Follows the viewport when the mode or the speed change elsewhere (menu, loaded scene).
class CameraMenu : public QFrame
{
    Q_OBJECT

public:
    explicit CameraMenu(Viewport *viewport);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void sync();
    void reposition();

    Viewport *m_viewport;
    QToolButton *m_orbit = nullptr;
    QToolButton *m_free = nullptr;
    QLabel *m_speed = nullptr;
};
