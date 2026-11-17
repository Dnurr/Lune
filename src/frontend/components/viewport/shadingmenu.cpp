#include "shadingmenu.h"

#include <QBoxLayout>
#include <QActionGroup>
#include <QMenu>
#include <QChildEvent>
#include <QToolButton>

#include "frontend/components/panel/panel.h"
#include "frontend/theme/icons.h"
#include "viewport.h"

// Inside this class (a QFrame) the bare name "Panel" is the QFrame::Panel frame style,
// so our Panel class is written ::Panel.

namespace
{
constexpr int kMargin = 12; // from the top-left corner of the free area

struct Mode
{
    Shading shading;
    const char *name;
    const char *icon; // assets/icons/<icon>.svg; "shading" until it exists
    const char *tip;
};
const Mode kModes[] = {
    { Shading::Wireframe, "Wireframe", "wireframe", "The edges of every mesh, see-through" },
    { Shading::Clay, "Clay", "clay", "Every mesh in one plain gray material, with the lights and the environment" },
    { Shading::Unlit, "Unlit", "unlit", "The materials' base color, no lighting" },
    { Shading::Preview, "Preview", "preview", "The materials lit by the environment only (no scene lights)" },
    { Shading::Rendered, "Rendered", "rendered", "Materials, lights, shadows and environment" },
};
constexpr QSize kIconSize(18, 18);

QIcon modeIcon(const Mode &mode)
{
    return Icons::first({ mode.icon, "shading" });
}
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

    // Shading mode: one button showing the current mode's icon, the modes in its menu.
    m_mode = new QToolButton(this);
    m_mode->setObjectName("CameraMenuButton"); // same look as the camera bar
    m_mode->setIconSize(kIconSize);
    m_mode->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_mode->setText(QString::fromUtf8("▾")); // small down triangle: it opens a menu
    m_mode->setPopupMode(QToolButton::InstantPopup);
    m_mode->setCursor(Qt::PointingHandCursor);
    m_mode->setFocusPolicy(Qt::NoFocus);
    auto *menu = new QMenu(m_mode);
    menu->setObjectName("ShadingModeMenu");
    auto *group = new QActionGroup(menu);
    for (const Mode &mode : kModes) {
        QAction *a = menu->addAction(modeIcon(mode), QString::fromLatin1(mode.name));
        a->setToolTip(QString::fromLatin1(mode.tip));
        a->setCheckable(true);
        group->addAction(a);
        m_modeActions.append(a);
        const Shading shading = mode.shading;
        connect(a, &QAction::triggered, this, [this, shading] {
            ViewSettings v = m_viewport->viewSettings();
            v.shading = shading;
            m_viewport->setViewSettings(v);
        });
    }
    menu->setToolTipsVisible(true);
    m_mode->setMenu(menu);
    row->addWidget(m_mode);

    auto *separator = new QFrame(this);
    separator->setObjectName("ShadingMenuSeparator");
    separator->setFixedSize(1, 16);
    row->addSpacing(2);
    row->addWidget(separator);
    row->addSpacing(2);

    auto toggle = [this, row](const char *icon, const QString &fallback, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName("CameraMenuButton");
        const QIcon i = Icons::get(QString::fromLatin1(icon));
        if (i.isNull()) {
            b->setText(fallback);
        } else {
            b->setIcon(i);
            b->setIconSize(kIconSize);
        }
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        row->addWidget(b);
        return b;
    };
    m_sky = toggle("sky", "Sky", "Sky: show the environment as background (when it has one). Off: the plain sky");
    connect(m_sky, &QToolButton::clicked, this, [this](bool on) {
        ViewSettings v = m_viewport->viewSettings();
        v.skybox = on;
        m_viewport->setViewSettings(v);
    });
    m_post = toggle("post-process", "Post",
                    "Post-processing: in Rendered, the scene camera's Post Process effects (bloom, depth of field...)");
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
    for (int i = 0; i < m_modeActions.size(); ++i) {
        const bool current = kModes[i].shading == v.shading;
        m_modeActions[i]->setChecked(current);
        if (current) {
            m_mode->setIcon(modeIcon(kModes[i]));
            m_mode->setToolTip(QStringLiteral("Shading: %1. %2").arg(QString::fromLatin1(kModes[i].name),
                                                                     QString::fromLatin1(kModes[i].tip)));
        }
    }
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
