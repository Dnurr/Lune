#include "panel.h"

#include <QBoxLayout>
#include <QEasingCurve>
#include <QEvent>
#include <QHash>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>

#include <algorithm>

namespace
{
int g_nextOrder = 0;
}

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
    if (watched == parentWidget() && event->type() == QEvent::Resize && !m_dragging)
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

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------

void Panel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isOnTitle(event->position().toPoint())) {
        m_snap->stop();
        m_dragging = true;
        m_dragOffset = event->position().toPoint();
        m_title->setCursor(Qt::ClosedHandCursor);
        raise();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void Panel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QFrame::mouseMoveEvent(event);
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
    if (!m_dragging || event->button() != Qt::LeftButton) {
        QFrame::mouseReleaseEvent(event);
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
    m_body->setAlignment(m_content, isVertical() ? Qt::AlignTop : Qt::AlignLeft);

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
