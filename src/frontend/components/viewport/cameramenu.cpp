#include "cameramenu.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QChildEvent>
#include <QLabel>
#include <QToolButton>

#include "frontend/components/panel/panel.h"
#include "viewport.h"

// Inside this class (a QFrame) the bare name "Panel" is the QFrame::Panel frame style,
// so our Panel class is written ::Panel.

namespace
{
constexpr int kMargin = 12; // from the top of the viewport
}

CameraMenu::CameraMenu(Viewport *viewport)
    : QFrame(viewport)
    , m_viewport(viewport)
{
    setObjectName("CameraMenu");
    setProperty("glass", true); // the viewport blurs the scene behind it
    setProperty("glassRadius", 10);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(4, 4, 4, 4);
    row->setSpacing(2);

    auto button = [this](const QString &text, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName("CameraMenuButton");
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    m_orbit = button("Orbit", "Orbit camera: right drag turns around the target, wheel zooms");
    m_free = button("Free", "Free camera: hold the right button to look around and fly with "
                            "W A S D, Q / E down / up, Shift faster; wheel while holding = speed");
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_orbit);
    group->addButton(m_free);
    m_speed = new QLabel(this);
    m_speed->setObjectName("CameraMenuSpeed");
    m_speed->setToolTip("Flying speed (world units per second): wheel while holding the right button");

    row->addWidget(m_orbit);
    row->addWidget(m_free);
    row->addWidget(m_speed);

    connect(m_orbit, &QToolButton::clicked, this, [this] { m_viewport->setCameraMode(CameraMode::Orbit); });
    connect(m_free, &QToolButton::clicked, this, [this] { m_viewport->setCameraMode(CameraMode::Free); });
    connect(m_viewport, &Viewport::cameraModeChanged, this, &CameraMenu::sync);
    connect(m_viewport, &Viewport::flySpeedChanged, this, &CameraMenu::sync);

    // Stay centered when the viewport resizes or the side panels move.
    viewport->installEventFilter(this);
    for (QObject *child : viewport->children())
        if (child != this)
            child->installEventFilter(this);
    sync();
    raise();
}

void CameraMenu::sync()
{
    const bool free = m_viewport->cameraMode() == CameraMode::Free;
    m_orbit->setChecked(!free);
    m_free->setChecked(free);
    const float speed = m_viewport->flySpeed();
    m_speed->setText(QString::number(speed, 'f', speed < 10.0f ? 1 : 0) + QStringLiteral(" u/s"));
    m_speed->setVisible(free);
    reposition();
}

void CameraMenu::reposition()
{
    const QWidget *p = parentWidget();
    // Free area between the panels docked on the left and on the right.
    int left = 0, right = p->width();
    for (QObject *child : p->children()) {
        auto *panel = qobject_cast<::Panel *>(child);
        if (!panel || !panel->isVisible() || panel->isNested())
            continue;
        if (panel->side() == ::Panel::Side::Left)
            left = qMax(left, panel->geometry().right() + 1);
        else if (panel->side() == ::Panel::Side::Right)
            right = qMin(right, panel->geometry().left());
    }
    adjustSize();
    int x = left + (right - left - width()) / 2;
    // Never over the shading bar at the top left (wider than the gap when panels are open).
    for (QObject *child : p->children()) {
        auto *w = qobject_cast<QWidget *>(child);
        if (w && w != this && w->isVisible() && w->inherits("ShadingMenu"))
            x = qMax(x, w->geometry().right() + 1 + kMargin);
    }
    move(x, kMargin);
}

bool CameraMenu::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::ChildAdded:
        // Not a Panel yet while it's being constructed: watch every child, check later.
        if (watched == parentWidget())
            static_cast<QChildEvent *>(event)->child()->installEventFilter(this);
        break;
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
        if (watched == parentWidget() || qobject_cast<::Panel *>(watched) || watched->inherits("ShadingMenu"))
            reposition();
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}
