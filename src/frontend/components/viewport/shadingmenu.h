#pragma once

#include <QFrame>

class QToolButton;
class Viewport;

// Glass bar at the top left of the free viewport area: how the viewport shades the scene
// (Wireframe, Clay, Unlit, Preview, Rendered), whether it shows the environment as
// background (Sky) and the scene camera's post-processing in Rendered (Post). Follows the viewport when the settings change elsewhere (View menu,
// loaded scene).
class ShadingMenu : public QFrame
{
    Q_OBJECT

public:
    explicit ShadingMenu(Viewport *viewport);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void sync();
    void reposition();

    Viewport *m_viewport;
    QList<QToolButton *> m_modes; // in Shading order
    QToolButton *m_sky = nullptr;
    QToolButton *m_post = nullptr;
};
