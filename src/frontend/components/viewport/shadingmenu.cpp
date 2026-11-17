#include "shadingmenu.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QChildEvent>
#include <QToolButton>

#include "frontend/components/panel/panel.h"
#include "viewport.h"

// Inside this class (a QFrame) the bare name "Panel" is the QFrame::Panel frame style,
// so our Panel class is written ::Panel.

namespace
{
constexpr int kMargin = 12; // from the top-left corner of the free area

struct Mode
{
    Shading shading;
    const char *text;
    const char *tip;
};
const Mode kModes[] = {
    { Shading::Wireframe, "Wire", "Wireframe: the edges of every mesh, see-through" },
    { Shading::Clay, "Clay", "Clay: every mesh in one plain gray material, with the lights and the environment" },
    { Shading::Unlit, "Unlit", "Unlit: the materials' base color, no lighting" },
    { Shading::Preview, "Preview", "Preview: the materials lit by the environment only (no scene lights)" },
    { Shading::Rendered, "Rendered", "Rendered: materials, lights, shadows and environment" },
};
} // namespace

ShadingMenu::ShadingMenu(Viewport *viewport)
    : QFrame(viewport)
    , m_viewport(viewport)
{
    setObjectName("ShadingMenu");
    setProperty("glass", true); // the viewport blurs the scene behind it
    setProperty("glassRadius", 10);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(4, 4, 4, 4);
    row->setSpacing(2);

    auto button = [this](const QString &text, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName("CameraMenuButton"); // same look as the camera bar
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (const Mode &mode : kModes) {
        QToolButton *b = button(QString::fromLatin1(mode.text), QString::fromLatin1(mode.tip));
        group->addButton(b);
        row->addWidget(b);
        m_modes.append(b);
        const Shading shading = mode.shading;
        connect(b, &QToolButton::clicked, this, [this, shading] {
            ViewSettings v = m_viewport->viewSettings();
            v.shading = shading;
            m_viewport->setViewSettings(v);
        });
    }

    auto *separator = new QFrame(this);
    separator->setObjectName("ShadingMenuSeparator");
    separator->setFixedSize(1, 16);
    row->addSpacing(4);
    row->addWidget(separator);
    row->addSpacing(4);

    m_sky = button("Sky", "Show the environment as background (when it has one). Off: the plain sky");
    row->addWidget(m_sky);
    connect(m_sky, &QToolButton::clicked, this, [this](bool on) {
        ViewSettings v = m_viewport->viewSettings();
        v.skybox = on;
        m_viewport->setViewSettings(v);
    });

    m_post = button("Post", "Rendered: apply the scene camera's Post Process effects (bloom, depth of field...)");
    row->addWidget(m_post);
    connect(m_post, &QToolButton::clicked, this, [this](bool on) {
        ViewSettings v = m_viewport->viewSettings();
        v.post = on;
        m_viewport->setViewSettings(v);
    });

    connect(m_viewport, &Viewport::viewSettingsChanged, this, &ShadingMenu::sync);

    // Stay in the corner when the viewport resizes or the left-side panels move.
    viewport->installEventFilter(this);
    for (QObject *child : viewport->children())
        if (child != this)
            child->installEventFilter(this);
    sync();
    raise();
}

void ShadingMenu::sync()
{
    const ViewSettings v = m_viewport->viewSettings();
    for (int i = 0; i < m_modes.size(); ++i)
        m_modes[i]->setChecked(kModes[i].shading == v.shading);
    m_sky->setChecked(v.skybox);
    m_post->setChecked(v.post);
    reposition();
}

void ShadingMenu::reposition()
{
    const QWidget *p = parentWidget();
    // Left edge of the free area: right of the panels docked on the left.
    int left = kMargin;
    for (QObject *child : p->children()) {
        auto *panel = qobject_cast<::Panel *>(child);
        if (panel && panel->isVisible() && !panel->isNested() && panel->side() == ::Panel::Side::Left)
            left = qMax(left, panel->geometry().right() + 1 + kMargin);
    }
    adjustSize();
    move(left, kMargin);
}

bool ShadingMenu::eventFilter(QObject *watched, QEvent *event)
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
        if (watched == parentWidget() || qobject_cast<::Panel *>(watched))
            reposition();
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}
