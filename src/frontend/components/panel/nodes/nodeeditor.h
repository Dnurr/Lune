#pragma once

#include <QGraphicsView>
#include <QHash>

#include "engine/material/nodegraph.h"

class NodeItem;
class NodeLibrary;
class QGraphicsPathItem;

// Canvas showing a node graph: nodes as boxes with their ports, links as curves.
// It never changes the graph it shows: every edit is emitted as the whole graph after the
// change (edited()), and comes back through setGraph() once the owner applied it (undo
// stack -> scene -> panel).
//
//   left drag a node        move it (several if selected)
//   left drag from a port   link output -> input (or input -> output); dragging from a linked
//                           input picks its link up: drop it on another input, or in the void
//                           to remove it
//   left drag on empty      rubber band selection
//   middle drag / wheel     pan / zoom
//   right click, Shift+A    add a node (at the cursor)
//   Delete, X               delete the selected nodes (not the Material Output)
//   F                       frame all the nodes
class NodeEditorView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit NodeEditorView(QWidget *parent = nullptr);
    ~NodeEditorView() override;

    // Shows `graph` (nothing to do if it's the one already shown). Keeps the selection and
    // the view. `library` gives the node types; it must outlive the view.
    void setGraph(const NodeGraph &graph, const NodeLibrary *library);
    void clearGraph(); // shows nothing (no material)
    bool hasGraph() const { return m_hasGraph; }
    const NodeGraph &graph() const { return m_graph; }

    int selectedNode() const; // the only selected node, or 0
    void frameAll();

signals:
    void edited(const NodeGraph &after, const QString &text, const QString &mergeKey);
    void nodeSelected(int id); // 0 = none or several

protected:
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    friend class NodeItem;
    struct PortHit
    {
        int node = 0;
        QString port;
        bool output = false;
        bool valid() const { return node != 0; }
    };

    void rebuild();
    void frameOnce();
    void updateLinks(); // link curves follow the nodes
    PortHit portAt(const QPointF &scenePos) const;
    QPointF portPosition(int node, const QString &port, bool output) const;
    void showAddMenu(const QPoint &globalPos, const QPointF &scenePos);
    void deleteSelected();
    void emitEdit(const NodeGraph &after, const QString &text, const QString &mergeKey = QString());
    static QPainterPath linkPath(const QPointF &from, const QPointF &to);

    QGraphicsScene *m_scene = nullptr;
    const NodeLibrary *m_library = nullptr;
    NodeGraph m_graph;
    bool m_hasGraph = false;
    QHash<int, NodeItem *> m_items;
    QList<QGraphicsPathItem *> m_links;

    // Link being dragged
    bool m_linking = false;
    PortHit m_linkStart;        // where the dragged end is anchored
    NodeGraph m_linkGraph;      // the graph without the picked-up link (if any)
    bool m_pickedUp = false;    // started by picking up an existing link
    QGraphicsPathItem *m_dragLink = nullptr;

    bool m_panning = false;
    QPoint m_lastPan;
    bool m_framed = false; // the first graph is framed once the view has a size
};
