#include "panel.h"

#include <QBoxLayout>
#include <QEasingCurve>
#include <QEvent>
#include <QHash>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QWheelEvent>

#include <algorithm>

namespace
{
int g_nextOrder = 0;
constexpr int kResizeGrip = 6;     // px from the inner edge that start a resize
constexpr int kMinPanelSize = 140; // smallest width / height when resizing
const char *const kCollapsedProperty = "panelsCollapsed";
} // namespace

Panel::Panel(const QString &title, QWidget *parent, Side side)
    : QFrame(parent)
    , m_side(side)
    , m_order(g_nextOrder++)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(8);

    m_title = new QLabel(title, this);
    m_title->setObjectName("PanelTitle");
    m_title->setCursor(Qt::OpenHandCursor);
    m_title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // Labels don't consume mouse events, so presses on the title reach the Panel.
    root->addWidget(m_title);

    m_content = new QBoxLayout(QBoxLayout::TopToBottom);
    m_content->setSpacing(8);
    // m_body fills the area below the title; m_content is aligned inside it
    // (top or left, see applyOrientation) so children stay packed at that edge.
    m_body = new QBoxLayout(QBoxLayout::TopToBottom);
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->addLayout(m_content);
    root->addLayout(m_body, 1);

    m_snap = new QPropertyAnimation(this, "geometry", this);
    m_snap->setDuration(180);
    m_snap->setEasingCurve(QEasingCurve::OutCubic);

    applyOrientation();

    if (isNested()) {
        updateNestedSizing();
    } else if (parent) {
        // Top-level: follow the parent's size changes.
        parent->installEventFilter(this);
        // Ask the Viewport (if that's the parent) to draw the blurred scene behind us.
        setProperty("glass", true);
        setProperty("glassRadius", 12); // same as border-radius in the theme
        setMouseTracking(true);         // resize cursor on the inner edge
        updateDockSideProperty();
        if (isCollapsed(parent))
            QFrame::setVisible(false);
        relayoutAll(false); // existing panels make room for this one
        raise();
    }
}

Panel *Panel::hostPanel() const
{
    return qobject_cast<Panel *>(parentWidget());
}

void Panel::setSide(Side side)
{
    if (m_side == side)
        return;
    m_side = side;
    updateDockSideProperty();
    applyOrientation();
    relayoutAll(false);
    emit sideChanged(m_side);
}

void Panel::setMargin(int margin)
{
    m_margin = margin;
    relayoutAll(false);
}

void Panel::setPanelWidth(int width)
{
    m_width = width;
    if (isNested())
        updateNestedSizing();
    else
        relayoutAll(false);
}

void Panel::setPanelHeight(int height)
{
    m_height = height;
    relayoutAll(false);
}

bool Panel::event(QEvent *event)
{
    if (event->type() == QEvent::ParentChange && isNested())
        updateNestedSizing();
    return QFrame::event(event);
}

bool Panel::eventFilter(QObject *watched, QEvent *event)
{
    // Every top-level panel listens to the parent, so each one only updates itself here.
    if (watched == parentWidget() && event->type() == QEvent::Resize && !m_dragging && !m_resizing)
        applyGeometry(false);
    return QFrame::eventFilter(watched, event);
}

void Panel::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    if (!event->spontaneous())
        relayoutAll(false);
}

void Panel::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
    if (!event->spontaneous())
        relayoutAll(false); // siblings take back the freed space
}

bool Panel::isOnTitle(const QPoint &pos) const
{
    // The whole header strip (title row, full panel width) acts as the handle.
    return pos.y() <= m_title->geometry().bottom() + 4;
}

bool Panel::isOnResizeEdge(const QPoint &pos) const
{
    if (isNested() || !parentWidget())
        return false;
    switch (m_side) {
    case Side::Left:
        return pos.x() >= width() - kResizeGrip;
    case Side::Right:
        return pos.x() < kResizeGrip;
    case Side::Bottom:
        return pos.y() < kResizeGrip;
    }
    return false;
}

void Panel::updateDockSideProperty()
{
    if (isNested())
        return;
    static const char *const sides[] = { "left", "right", "bottom" };
    setProperty("dockSide", QString::fromLatin1(sides[int(m_side)]));
}

void Panel::setContentFills(bool fills)
{
    m_fills = fills;
    applyOrientation();
}

// ---------------------------------------------------------------------------
// Visibility, collapsing
// ---------------------------------------------------------------------------

void Panel::setPanelVisible(bool visible)
{
    m_wantsVisible = visible;
    applyVisibility();
}

void Panel::setPanelEnabled(bool enabled)
{
    m_enabled = enabled;
    applyVisibility();
}

void Panel::applyVisibility()
{
    const bool collapsed = !isNested() && isCollapsed(parentWidget());
    setVisible(m_wantsVisible && m_enabled && !collapsed);
}

bool Panel::isCollapsed(const QWidget *host)
{
    return host && host->property(kCollapsedProperty).toBool();
}

void Panel::setCollapsed(QWidget *host, bool collapsed)
{
    if (!host || isCollapsed(host) == collapsed)
        return;
    host->setProperty(kCollapsedProperty, collapsed);
    const QList<Panel *> panels = host->findChildren<Panel *>(Qt::FindDirectChildrenOnly);
    if (collapsed) {
        for (Panel *p : panels) {
            if (p->isVisible())
                p->slide(false);
            else
                p->QFrame::setVisible(false); // window not shown yet: no animation
        }
        return;
    }
    // Show them all first, so each one gets its final place, then slide them in.
    QList<Panel *> shown;
    for (Panel *p : panels) {
        if (p->m_wantsVisible && p->m_enabled && p->isHidden()) {
            p->QFrame::setVisible(true);
            shown.append(p);
        }
    }
    for (Panel *p : shown)
        p->slide(true);
}

// Slides the panel past its side of the parent (out), or from there to its place (in).
// Hides it at the end of a slide out.
void Panel::slide(bool in)
{
    const QRect target = in ? targetGeometry() : geometry();
    QRect away = target;
    switch (m_side) {
    case Side::Left:
        away.moveRight(-1);
        break;
    case Side::Right:
        away.moveLeft(parentWidget()->width());
        break;
    case Side::Bottom:
        away.moveTop(parentWidget()->height());
        break;
    }
    m_snap->stop();
    auto *anim = new QPropertyAnimation(this, "geometry", this);
    anim->setDuration(160);
    anim->setEasingCurve(in ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    anim->setStartValue(in ? away : target);
    anim->setEndValue(in ? target : away);
    connect(anim, &QPropertyAnimation::finished, this, [this] {
        if (isCollapsed(parentWidget()))
            QFrame::setVisible(false);
        else
            applyGeometry(false); // expanded again meanwhile: back to its place
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ---------------------------------------------------------------------------
// Layout state
// ---------------------------------------------------------------------------

QJsonObject Panel::layoutState() const
{
    static const char *const sides[] = { "left", "right", "bottom" };
    QJsonObject state;
    state["side"] = QString::fromLatin1(sides[int(m_side)]);
    state["width"] = m_width;
    state["height"] = m_height;
    state["enabled"] = m_enabled;
    return state;
}

void Panel::restoreLayoutState(const QJsonObject &state)
{
    const QString side = state["side"].toString();
    if (side == QLatin1String("left"))
        setSide(Side::Left);
    else if (side == QLatin1String("right"))
        setSide(Side::Right);
    else if (side == QLatin1String("bottom"))
        setSide(Side::Bottom);
    m_width = qMax(kMinPanelSize, state["width"].toInt(m_width));
    m_height = qMax(kMinPanelSize, state["height"].toInt(m_height));
    if (state.contains("enabled"))
        setPanelEnabled(state["enabled"].toBool());
    relayoutAll(false);
}

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------

void Panel::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (event->button() == Qt::LeftButton && isOnResizeEdge(pos)) {
        m_snap->stop();
        m_resizing = true;
        m_resizeStart = mapToParent(pos);
        m_resizeStartSize = m_side == Side::Bottom ? m_height : m_width;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && isOnTitle(pos)) {
        m_snap->stop();
        m_dragging = true;
        m_dragOffset = event->position().toPoint();
        m_title->setCursor(Qt::ClosedHandCursor);
        raise();
        event->accept();
        return;
    }
    // Not on a control: the press stops here instead of reaching the viewport behind.
    event->accept();
}

void Panel::wheelEvent(QWheelEvent *event)
{
    event->accept(); // scrolling over a panel never zooms the viewport
}

void Panel::leaveEvent(QEvent *event)
{
    if (!m_resizing)
        unsetCursor();
    QFrame::leaveEvent(event);
}

void Panel::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_resizing) {
        const QPoint d = mapToParent(pos) - m_resizeStart;
        const QWidget *p = parentWidget();
        if (m_side == Side::Bottom) {
            const int maxHeight = qMax(kMinPanelSize, p->height() * 3 / 4);
            m_height = qBound(kMinPanelSize, m_resizeStartSize - d.y(), maxHeight);
        } else {
            const int maxWidth = qMax(kMinPanelSize, p->width() / 2);
            const int grow = m_side == Side::Left ? d.x() : -d.x();
            m_width = qBound(kMinPanelSize, m_resizeStartSize + grow, maxWidth);
        }
        relayoutAll(false);
        event->accept();
        return;
    }
    if (!m_dragging) {
        // Hover: resize cursor on the inner edge.
        if (isOnResizeEdge(pos))
            setCursor(m_side == Side::Bottom ? Qt::SizeVerCursor : Qt::SizeHorCursor);
        else
            unsetCursor();
        event->accept();
        return;
    }

    // Wanted top-left in parent coordinates (cursor minus where we grabbed the panel).
    const QPoint topLeft = mapToParent(event->position().toPoint()) - m_dragOffset;

    if (Panel *host = hostPanel()) {
        // Nested: move along the host's axis only, inside the host's body area.
        const QRect area = host->m_body->geometry();
        if (host->isVertical()) {
            const int maxY = qMax(area.top(), area.bottom() + 1 - height());
            move(x(), qBound(area.top(), topLeft.y(), maxY));
        } else {
            const int maxX = qMax(area.left(), area.right() + 1 - width());
            move(qBound(area.left(), topLeft.x(), maxX), y());
        }
    } else {
        const QWidget *p = parentWidget();
        const int maxX = qMax(m_margin, p->width() - width() - m_margin);
        const int maxY = qMax(m_margin, p->height() - height() - m_margin);
        move(qBound(m_margin, topLeft.x(), maxX), qBound(m_margin, topLeft.y(), maxY));
    }
    event->accept();
}

void Panel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing && event->button() == Qt::LeftButton) {
        m_resizing = false;
        if (!isOnResizeEdge(event->position().toPoint()))
            unsetCursor();
        emit resized();
        event->accept();
        return;
    }
    if (!m_dragging || event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }

    m_dragging = false;
    m_title->setCursor(Qt::OpenHandCursor);

    if (isNested()) {
        dropNested();
        event->accept();
        return;
    }

    // Top-level: snap to the edge closest to the cursor.
    const QWidget *p = parentWidget();
    const QPoint c = mapToParent(event->position().toPoint());
    const int dLeft = c.x();
    const int dRight = p->width() - c.x();
    const int dBottom = p->height() - c.y();

    Side newSide = Side::Bottom;
    if (dLeft <= dRight && dLeft < dBottom)
        newSide = Side::Left;
    else if (dRight < dLeft && dRight < dBottom)
        newSide = Side::Right;

    const bool changed = newSide != m_side;
    m_side = newSide;
    updateDockSideProperty();
    if (changed)
        applyOrientation();
    relayoutAll(true);

    if (changed)
        emit sideChanged(m_side);
    event->accept();
}

void Panel::dropNested()
{
    Panel *host = hostPanel();
    QBoxLayout *lay = host->m_content;
    const bool vertical = host->isVertical();
    const auto center = [vertical](const QRect &r) { return vertical ? r.center().y() : r.center().x(); };

    const int myIndex = lay->indexOf(this);
    if (myIndex < 0) {
        // Not in the host's content layout: nothing to reorder, just let the layout put it back.
        host->layout()->invalidate();
        return;
    }

    // New index = how many other items sit before the dragged panel's center.
    const int myCenter = center(geometry());
    int newIndex = 0;
    QHash<QWidget *, QRect> before;
    for (int i = 0; i < lay->count(); ++i) {
        QLayoutItem *item = lay->itemAt(i);
        if (QWidget *w = item->widget())
            before.insert(w, w->geometry());
        if (i != myIndex && center(item->geometry()) < myCenter)
            ++newIndex;
    }

    lay->removeWidget(this);
    lay->insertWidget(newIndex, this);
    host->layout()->activate(); // apply the new order right away

    // Slide every nested panel from where it was to its new slot.
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        auto *p = qobject_cast<Panel *>(it.key());
        if (!p || p->geometry() == it.value())
            continue;
        p->m_snap->stop();
        p->m_snap->setStartValue(it.value());
        p->m_snap->setEndValue(p->geometry());
        p->setGeometry(it.value());
        p->m_snap->start();
    }
}

// ---------------------------------------------------------------------------
// Orientation / nesting
// ---------------------------------------------------------------------------

void Panel::applyOrientation()
{
    const auto dir = isVertical() ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    m_body->setDirection(dir);
    m_content->setDirection(dir);
    // Only the main axis is aligned: the content keeps its natural length along it
    // (packed at the top/left) and fills the whole cross axis.
    m_body->setAlignment(m_content, m_fills ? Qt::Alignment() : isVertical() ? Qt::AlignTop : Qt::AlignLeft);

    for (Panel *child : findChildren<Panel *>(Qt::FindDirectChildrenOnly))
        child->updateNestedSizing();
}

void Panel::updateNestedSizing()
{
    const Panel *host = hostPanel();
    if (!host)
        return;

    if (host->isVertical()) {
        // Stacked from the top: full width, height from the content.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    } else {
        // Packed from the left: m_width preferred (see sizeHint), shrinks if the host is too narrow, full height.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    updateGeometry(); // sizeHint depends on the host orientation
}

QSize Panel::sizeHint() const
{
    const QSize base = QFrame::sizeHint();
    const Panel *host = hostPanel();
    if (host && !host->isVertical())
        return QSize(m_width, base.height());
    return base;
}

// ---------------------------------------------------------------------------
// Top-level layout
// ---------------------------------------------------------------------------

QList<Panel *> Panel::siblings(Side side) const
{
    QList<Panel *> result;
    if (!parentWidget())
        return result;
    for (Panel *p : parentWidget()->findChildren<Panel *>(Qt::FindDirectChildrenOnly)) {
        if (p->m_side == side && !p->isHidden())
            result.append(p);
    }
    std::sort(result.begin(), result.end(), [](const Panel *a, const Panel *b) { return a->m_order < b->m_order; });
    return result;
}

QRect Panel::targetGeometry() const
{
    const QWidget *p = parentWidget();
    const int W = p->width();
    const int H = p->height();
    const int m = m_margin;

    // Width reserved by a left/right column (widest panel on that side), 0 if empty.
    auto columnWidth = [&](Side s) {
        int w = 0;
        for (const Panel *sp : siblings(s))
            w = qMax(w, qMin(sp->m_width, qMax(0, W - 2 * m)));
        return w;
    };

    const QList<Panel *> same = siblings(m_side);
    const int n = qMax(1, int(same.size()));
    const int idx = qMax(0, int(same.indexOf(const_cast<Panel *>(this))));

    if (m_side == Side::Bottom) {
        const int leftW = columnWidth(Side::Left);
        const int rightW = columnWidth(Side::Right);
        const int x0 = m + (leftW > 0 ? leftW + m : 0);
        const int x1 = W - m - (rightW > 0 ? rightW + m : 0);
        const int avail = qMax(0, x1 - x0);
        const int w = qMax(0, (avail - (n - 1) * m) / n);
        const int h = qMin(m_height, qMax(0, H - 2 * m));
        return QRect(x0 + idx * (w + m), H - m - h, w, h);
    }

    // Left / right: full height, stacked vertically when several share the side.
    const int avail = qMax(0, H - 2 * m);
    const int h = qMax(0, (avail - (n - 1) * m) / n);
    const int w = qMin(m_width, qMax(0, W - 2 * m));
    const int x = (m_side == Side::Left) ? m : W - w - m;
    return QRect(x, m + idx * (h + m), w, h);
}

void Panel::applyGeometry(bool animate)
{
    if (!parentWidget() || isNested())
        return; // nested panels are positioned by the host's layout
    const QRect target = targetGeometry();
    m_snap->stop();
    if (animate && target != geometry()) {
        m_snap->setStartValue(geometry());
        m_snap->setEndValue(target);
        m_snap->start();
    } else {
        setGeometry(target);
    }
}

void Panel::relayoutAll(bool animate)
{
    if (!parentWidget() || isNested())
        return;
    for (Panel *p : parentWidget()->findChildren<Panel *>(Qt::FindDirectChildrenOnly)) {
        if (!p->m_dragging)
            p->applyGeometry(animate);
    }
}
