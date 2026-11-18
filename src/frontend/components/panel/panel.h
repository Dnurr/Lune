#pragma once

#include <QFrame>
#include <QJsonObject>
#include <QPoint>

class QBoxLayout;
class QLabel;
class QPropertyAnimation;
class QScrollArea;

// Floating panel drawn on top of its parent widget (e.g. the viewport).
//
// Top-level panel (parent is NOT a Panel):
//   docked to one side of the parent (left, right or bottom), keeping `margin`
//   pixels from the edges, and following the parent when it is resized.
//   Drag it by its title: on release it snaps to the side nearest to the cursor.
//   Sibling panels share the space without overlapping:
//     - left/right panels take the full height (stacked if more than one per side)
//     - bottom panels sit between the left and right columns (side by side)
//     - footer panels take the full width under everything (stacked if more than one):
//       the others end above them. They stay where they are (no dragging) and are
//       resized from their top edge, like bottom panels.
//
// Nested panel (parent IS a Panel, added to the parent's contentLayout()):
//   laid out by the parent. Its orientation follows the parent's side:
//     - parent on left/right: children stacked from the top, dragged vertically only
//     - parent at the bottom: children packed from the left, dragged horizontally only
//   On release the dragged child takes its new place in the order.
//
// Top-level panels can be resized from their inner edge (the one facing the viewport).
// Mouse presses and wheel events on a panel stop there: they never reach the viewport
// behind it, even outside any control. The content of a left/right panel scrolls when it's
// taller than the panel (nested panels live inside that scroll area; hostPanel() still
// finds their host).
//
// Collapsing (setCollapsed on the parent): every top-level panel slides out and hides;
// expanding brings back the ones that want to be visible (setPanelVisible).
class Panel : public QFrame
{
    Q_OBJECT

public:
    enum class Side
    {
        Left,
        Right,
        Bottom,
        Footer,
    };

    explicit Panel(const QString &title, QWidget *parent = nullptr, Side side = Side::Right);

    void setSide(Side side);
    Side side() const { return m_side; }
    void setMargin(int margin);
    void setPanelWidth(int width);   // top-level left/right width; nested width when the parent is at the bottom
    void setPanelHeight(int height); // top-level bottom height

    // Layout where the panel's content widgets go (below the title).
    // Vertical when the panel is on the left/right, horizontal when it is at the bottom.
    QBoxLayout *contentLayout() const { return m_content; }

    bool isNested() const { return hostPanel() != nullptr; }

    // The content fills the whole area below the title, instead of keeping its natural
    // length along the panel's main axis (packed at the top / left).
    void setContentFills(bool fills);

    // Shows / hides the title row (e.g. a footer bar that needs every pixel of height).
    // Without a title the panel can't be dragged.
    void setTitleVisible(bool visible);

    // Visibility the panel wants (e.g. the inspector only while something is selected).
    // Applied right away, or when the panels are expanded again if they're collapsed.
    void setPanelVisible(bool visible);
    bool wantsVisible() const { return m_wantsVisible; }
    // Part of the current workspace (the editor turns panels on and off per workspace).
    // Shown only when enabled, wanting to be visible, and not collapsed.
    void setPanelEnabled(bool enabled);
    bool isPanelEnabled() const { return m_enabled; }

    // Collapses / expands every top-level panel of `host` (animated).
    static void setCollapsed(QWidget *host, bool collapsed);
    static bool isCollapsed(const QWidget *host);

    // Side and size, to save and restore the editor layout.
    QJsonObject layoutState() const;
    void restoreLayoutState(const QJsonObject &state);

    QSize sizeHint() const override;

signals:
    void sideChanged(Panel::Side side);
    void resized(); // by the user, from the inner edge

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    Panel *hostPanel() const; // parent Panel when nested (also through its scroll area), otherwise nullptr
    QWidget *contentWidget() const; // the widget contentLayout() lays out: the scroll area's content, or this
    bool isVertical() const { return m_side == Side::Left || m_side == Side::Right; }
    bool isHorizontalBar() const { return m_side == Side::Bottom || m_side == Side::Footer; }
    bool isOnTitle(const QPoint &pos) const;
    bool isOnResizeEdge(const QPoint &pos) const; // inner edge of a top-level panel
    void slide(bool in);                          // collapse animation

    // Top-level layout
    QList<Panel *> siblings(Side side) const; // visible panels on `side`, in creation order
    QRect targetGeometry() const;
    void applyGeometry(bool animate);
    void relayoutAll(bool animate); // this panel and all its siblings

    // Orientation / nesting
    void applyOrientation();    // content direction + sizing of nested children
    void updateNestedSizing();  // size policy of this panel inside its host
    void dropNested();          // reorder inside the host after a drag

    QLabel *m_title = nullptr;
    QBoxLayout *m_body = nullptr;    // area below the title; m_content is aligned top/left inside it
    QBoxLayout *m_content = nullptr;
    QScrollArea *m_scroll = nullptr; // top-level panels: scrolls the content (side panels only)
    QPropertyAnimation *m_snap = nullptr;
    Side m_side = Side::Right;
    int m_margin = 12;
    int m_width = 280;
    int m_height = 220;
    int m_order = 0; // creation order, keeps stacking stable

    bool m_dragging = false;
    QPoint m_dragOffset; // cursor position inside the panel when the drag started
    bool m_resizing = false;
    QPoint m_resizeStart; // cursor in parent coordinates when the resize started
    int m_resizeStartSize = 0;
    bool m_fills = false;
    bool m_wantsVisible = true;
    bool m_enabled = true;
    void applyVisibility();
    void updateDockSideProperty(); // "dockSide" = left/right/bottom/footer, read by the viewport
};
