#include "nodeeditor.h"

#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QVector3D>
#include <QtMath>
#include <map>

#include "engine/material/materialcompiler.h"
#include "engine/material/nodelibrary.h"

namespace
{
constexpr qreal kNodeWidth = 168;
constexpr qreal kHeader = 24;
constexpr qreal kRow = 22;
constexpr qreal kPortRadius = 5;
constexpr qreal kRadius = 8;

QColor portColor(const PortDesc &p)
{
    if (p.color)
        return QColor("#E6C35C"); // colors: yellow
    return p.type == PortType::Vec3 ? QColor("#9B8CF2") : QColor("#A8A8B0"); // vectors: violet, numbers: gray
}

QColor headerColor(const NodeType &t)
{
    if (t.isOutput)
        return QColor("#7A3B3B");
    if (t.category == QLatin1String("Input"))
        return QColor("#3B5A7A");
    if (t.category == QLatin1String("Color"))
        return QColor("#6A5A2E");
    if (t.category == QLatin1String("Math"))
        return QColor("#3E5E4C");
    return QColor("#4A4A58");
}

QString valueText(const QVariant &v, const PortDesc &p)
{
    if (p.type == PortType::Float)
        return QString::number(v.toDouble(), 'f', 2);
    if (p.color)
        return QString();
    const QVector3D c = v.value<QVector3D>();
    return QStringLiteral("%1 %2 %3").arg(c.x(), 0, 'f', 1).arg(c.y(), 0, 'f', 1).arg(c.z(), 0, 'f', 1);
}

QColor toColor(const QVariant &v)
{
    const QVector3D c = v.value<QVector3D>();
    return QColor::fromRgbF(qBound(0.0f, c.x(), 1.0f), qBound(0.0f, c.y(), 1.0f), qBound(0.0f, c.z(), 1.0f));
}
} // namespace

// ---------------------------------------------------------------------------
// One node: header with the title, then a row per output (port on the right) and per
// input (port on the left; its value when it has no link).
// ---------------------------------------------------------------------------
class NodeItem : public QGraphicsItem
{
public:
    NodeItem(NodeEditorView *view, const GraphNode &node, const NodeType *type)
        : m_view(view)
        , m_node(node)
        , m_type(type)
    {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setPos(node.position);
        setZValue(1);
    }

    int id() const { return m_node.id; }
    const NodeType *nodeType() const { return m_type; }

    int rowCount() const { return m_type ? int(m_type->outputs.size() + m_type->inputs.size()) : 0; }
    QRectF boundingRect() const override
    {
        return QRectF(-kPortRadius - 1, -1, kNodeWidth + 2 * kPortRadius + 2, kHeader + rowCount() * kRow + 8);
    }

    // Port center in item coordinates; null point if the port doesn't exist.
    QPointF portPosition(const QString &port, bool output) const
    {
        if (!m_type)
            return {};
        int row = 0;
        for (const PortDesc &p : m_type->outputs) {
            if (output && p.name == port)
                return QPointF(kNodeWidth, kHeader + row * kRow + kRow / 2);
            ++row;
        }
        for (const PortDesc &p : m_type->inputs) {
            if (!output && p.name == port && p.linkable)
                return QPointF(0, kHeader + row * kRow + kRow / 2);
            ++row;
        }
        return {};
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF body(0, 0, kNodeWidth, kHeader + rowCount() * kRow + 6);
        const QColor header = m_type ? headerColor(*m_type) : QColor("#802020");

        painter->setPen(QPen(isSelected() ? QColor("#8FB0F2") : QColor(255, 255, 255, 40), isSelected() ? 2 : 1));
        painter->setBrush(QColor(30, 30, 36, 235));
        painter->drawRoundedRect(body, kRadius, kRadius);

        QPainterPath top;
        top.addRoundedRect(QRectF(0, 0, kNodeWidth, kHeader), kRadius, kRadius);
        top.addRect(QRectF(0, kHeader / 2, kNodeWidth, kHeader / 2));
        painter->fillPath(top.simplified(), header);
        painter->setPen(QColor("#ECECF0"));
        QFont bold = painter->font();
        bold.setBold(true);
        painter->setFont(bold);
        painter->drawText(QRectF(10, 0, kNodeWidth - 20, kHeader), Qt::AlignVCenter | Qt::AlignLeft,
                          m_type ? m_type->title : QStringLiteral("? %1").arg(m_node.type));
        bold.setBold(false);
        painter->setFont(bold);
        if (!m_type)
            return;

        int row = 0;
        auto rowRect = [&](int r) { return QRectF(12, kHeader + r * kRow, kNodeWidth - 24, kRow); };
        for (const PortDesc &p : m_type->outputs) {
            painter->setPen(QColor("#C8C8D0"));
            painter->drawText(rowRect(row), Qt::AlignVCenter | Qt::AlignRight, p.name);
            drawPort(painter, QPointF(kNodeWidth, kHeader + row * kRow + kRow / 2), p);
            ++row;
        }
        for (const PortDesc &p : m_type->inputs) {
            const QRectF r = rowRect(row);
            const bool linked = p.linkable && m_view->m_graph.linkTo(m_node.id, p.name);
            painter->setPen(QColor("#C8C8D0"));
            painter->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, p.name);
            if (!linked) {
                const QVariant v = MaterialCompiler::inputValue(m_node, p);
                if (p.color) {
                    const QRectF swatch(r.right() - 34, r.top() + 5, 34, r.height() - 10);
                    painter->setPen(QColor(255, 255, 255, 50));
                    painter->setBrush(toColor(v));
                    painter->drawRoundedRect(swatch, 3, 3);
                } else {
                    painter->setPen(QColor("#9A9AA6"));
                    painter->drawText(r, Qt::AlignVCenter | Qt::AlignRight, valueText(v, p));
                }
            }
            if (p.linkable)
                drawPort(painter, QPointF(0, kHeader + row * kRow + kRow / 2), p);
            ++row;
        }
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == ItemPositionHasChanged)
            m_view->updateLinks();
        return QGraphicsItem::itemChange(change, value);
    }

private:
    static void drawPort(QPainter *painter, const QPointF &c, const PortDesc &p)
    {
        painter->setPen(QPen(QColor(20, 20, 24), 1.5));
        painter->setBrush(portColor(p));
        painter->drawEllipse(c, kPortRadius, kPortRadius);
    }

    NodeEditorView *m_view;
    GraphNode m_node;
    const NodeType *m_type;
};

// ---------------------------------------------------------------------------

NodeEditorView::NodeEditorView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setObjectName("NodeEditor");
    setScene(m_scene);
    m_scene->setSceneRect(-20000, -20000, 40000, 40000);
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::ClickFocus);
    // Transparent: the panel's glass shows through.
    setBackgroundBrush(Qt::NoBrush);
    viewport()->setAutoFillBackground(false);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] { emit nodeSelected(selectedNode()); });
}

NodeEditorView::~NodeEditorView() = default;

void NodeEditorView::setGraph(const NodeGraph &graph, const NodeLibrary *library)
{
    if (m_hasGraph && graph == m_graph && library == m_library)
        return;
    m_library = library;
    m_graph = graph;
    m_hasGraph = true;
    rebuild();
    frameOnce();
}

void NodeEditorView::frameOnce()
{
    // A new graph is framed once the canvas is on screen with its real size.
    if (!m_framed && m_hasGraph && isVisible() && width() > 100 && height() > 100) {
        frameAll();
        m_framed = true;
    }
}

void NodeEditorView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    frameOnce();
}

void NodeEditorView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    frameOnce();
}

void NodeEditorView::clearGraph()
{
    m_graph = NodeGraph();
    m_hasGraph = false;
    rebuild();
    m_framed = false;
}

void NodeEditorView::rebuild()
{
    QList<int> selected;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        if (it.value()->isSelected())
            selected.append(it.key());

    m_scene->blockSignals(true);
    m_scene->clear(); // items and links
    m_items.clear();
    m_links.clear();
    m_dragLink = nullptr;
    if (m_hasGraph && m_library) {
        for (const GraphNode &n : m_graph.nodes) {
            auto *item = new NodeItem(this, n, m_library->type(n.type));
            m_scene->addItem(item);
            m_items.insert(n.id, item);
            item->setSelected(selected.contains(n.id));
        }
    }
    m_scene->blockSignals(false);
    updateLinks();
    emit nodeSelected(selectedNode());
}

QPainterPath NodeEditorView::linkPath(const QPointF &from, const QPointF &to)
{
    const qreal dx = qMax(40.0, qAbs(to.x() - from.x()) * 0.5);
    QPainterPath path(from);
    path.cubicTo(from + QPointF(dx, 0), to - QPointF(dx, 0), to);
    return path;
}

void NodeEditorView::updateLinks()
{
    // One path item per link, recycled; extra ones removed.
    int i = 0;
    for (const GraphLink &l : m_graph.links) {
        const QPointF a = portPosition(l.fromNode, l.fromPort, true);
        const QPointF b = portPosition(l.toNode, l.toPort, false);
        if (a.isNull() || b.isNull())
            continue;
        if (i == m_links.size()) {
            auto *item = m_scene->addPath(QPainterPath(), QPen(QColor(200, 200, 210, 200), 2));
            item->setZValue(0);
            m_links.append(item);
        }
        m_links[i]->setPath(linkPath(a, b));
        ++i;
    }
    while (m_links.size() > i)
        delete m_links.takeLast();
}

QPointF NodeEditorView::portPosition(int node, const QString &port, bool output) const
{
    const NodeItem *item = m_items.value(node);
    if (!item)
        return {};
    const QPointF p = item->portPosition(port, output);
    return p.isNull() ? QPointF() : item->mapToScene(p);
}

NodeEditorView::PortHit NodeEditorView::portAt(const QPointF &scenePos) const
{
    const qreal grab = 9.0 / qMax(0.2, transform().m11()); // ~9 screen pixels
    for (const NodeItem *item : m_items) {
        const NodeType *t = item->nodeType();
        if (!t)
            continue;
        for (const PortDesc &p : t->outputs)
            if (QLineF(item->mapToScene(item->portPosition(p.name, true)), scenePos).length() <= grab)
                return { item->id(), p.name, true };
        for (const PortDesc &p : t->inputs)
            if (p.linkable && QLineF(item->mapToScene(item->portPosition(p.name, false)), scenePos).length() <= grab)
                return { item->id(), p.name, false };
    }
    return {};
}

int NodeEditorView::selectedNode() const
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.size() != 1)
        return 0;
    const auto *item = static_cast<const NodeItem *>(selected.first());
    return item->id();
}

void NodeEditorView::frameAll()
{
    QRectF bounds;
    for (const NodeItem *item : m_items)
        bounds |= item->sceneBoundingRect();
    if (bounds.isNull()) {
        resetTransform();
        centerOn(0, 0);
        return;
    }
    fitInView(bounds.adjusted(-60, -60, 60, 60), Qt::KeepAspectRatio);
    const qreal s = transform().m11();
    if (s > 1.0 || s < 0.4) { // don't blow a single node up, nor shrink it to a dot
        resetTransform();
        if (s < 0.4)
            scale(0.4, 0.4);
        centerOn(bounds.center());
    }
}

void NodeEditorView::emitEdit(const NodeGraph &after, const QString &text, const QString &mergeKey)
{
    // Deferred: the owner rebuilds the items through setGraph(), not inside our own event handler.
    QTimer::singleShot(0, this, [this, after, text, mergeKey] { emit edited(after, text, mergeKey); });
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool NodeEditorView::event(QEvent *event)
{
    // Keys that are window shortcuts elsewhere (Delete, Shift+A, X, F) belong to the editor here.
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool shiftA = key->key() == Qt::Key_A && key->modifiers() == Qt::ShiftModifier;
        const bool plain = key->modifiers() == Qt::NoModifier
            && (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace || key->key() == Qt::Key_X
                || key->key() == Qt::Key_F);
        if (m_hasGraph && (shiftA || plain)) {
            event->accept();
            return true;
        }
    }
    return QGraphicsView::event(event);
}

void NodeEditorView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPan = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_hasGraph) {
        const QPointF scenePos = mapToScene(event->position().toPoint());
        const PortHit hit = portAt(scenePos);
        if (hit.valid()) {
            m_linkGraph = m_graph;
            m_pickedUp = false;
            m_linkStart = hit;
            if (!hit.output) {
                if (const GraphLink *l = m_graph.linkTo(hit.node, hit.port)) {
                    // Pick the link up: the dragged end is the input side, anchored at its source.
                    m_linkStart = { l->fromNode, l->fromPort, true };
                    m_linkGraph.disconnect(hit.node, hit.port);
                    m_pickedUp = true;
                    for (QGraphicsPathItem *item : m_links) // hide it while dragging
                        if (item->path().pointAtPercent(1).toPoint() == portPosition(hit.node, hit.port, false).toPoint())
                            item->hide();
                }
            }
            m_linking = true;
            m_dragLink = m_scene->addPath(QPainterPath(), QPen(QColor("#8FB0F2"), 2, Qt::DashLine));
            m_dragLink->setZValue(2);
            mouseMoveEvent(event);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void NodeEditorView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint d = event->position().toPoint() - m_lastPan;
        m_lastPan = event->position().toPoint();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        event->accept();
        return;
    }
    if (m_linking && m_dragLink) {
        QPointF cursor = mapToScene(event->position().toPoint());
        const PortHit over = portAt(cursor);
        if (over.valid() && over.output != m_linkStart.output && over.node != m_linkStart.node)
            cursor = portPosition(over.node, over.port, over.output); // snap
        const QPointF anchor = portPosition(m_linkStart.node, m_linkStart.port, m_linkStart.output);
        m_dragLink->setPath(m_linkStart.output ? linkPath(anchor, cursor) : linkPath(cursor, anchor));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void NodeEditorView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    if (m_linking && event->button() == Qt::LeftButton) {
        m_linking = false;
        delete m_dragLink;
        m_dragLink = nullptr;
        const PortHit end = portAt(mapToScene(event->position().toPoint()));
        NodeGraph after = m_linkGraph;
        bool connected = false;
        if (end.valid() && end.output != m_linkStart.output && end.node != m_linkStart.node) {
            const PortHit &out = m_linkStart.output ? m_linkStart : end;
            const PortHit &in = m_linkStart.output ? end : m_linkStart;
            connected = after.connect(out.node, out.port, in.node, in.port);
        }
        if (connected)
            emitEdit(after, QStringLiteral("Connect nodes"));
        else if (m_pickedUp)
            emitEdit(after, QStringLiteral("Disconnect nodes"));
        else
            updateLinks();
        for (QGraphicsPathItem *item : m_links)
            item->show();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);

    // Nodes moved by the drag: one edit with the new positions.
    if (event->button() == Qt::LeftButton && m_hasGraph) {
        NodeGraph after = m_graph;
        bool moved = false;
        for (GraphNode &n : after.nodes) {
            const NodeItem *item = m_items.value(n.id);
            if (item && item->pos() != n.position) {
                n.position = QPointF(qRound(item->pos().x()), qRound(item->pos().y()));
                moved = true;
            }
        }
        if (moved)
            emitEdit(after, QStringLiteral("Move nodes"));
    }
}

void NodeEditorView::wheelEvent(QWheelEvent *event)
{
    const qreal factor = qPow(1.15, event->angleDelta().y() / 120.0);
    const qreal current = transform().m11();
    const qreal target = qBound(0.25, current * factor, 2.5);
    scale(target / current, target / current);
    event->accept();
}

void NodeEditorView::keyPressEvent(QKeyEvent *event)
{
    if (!m_hasGraph) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ShiftModifier) {
        const QPoint local = mapFromGlobal(QCursor::pos());
        showAddMenu(QCursor::pos(), mapToScene(rect().contains(local) ? local : rect().center()));
        return;
    }
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    case Qt::Key_X:
        deleteSelected();
        return;
    case Qt::Key_F:
        frameAll();
        return;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

void NodeEditorView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_hasGraph)
        return;
    showAddMenu(event->globalPos(), mapToScene(event->pos()));
}

void NodeEditorView::showAddMenu(const QPoint &globalPos, const QPointF &scenePos)
{
    if (!m_library)
        return;
    QMenu menu(this);
    std::map<QString, QMenu *> categories;
    for (const NodeType &t : m_library->types()) {
        if (t.isOutput)
            continue;
        QMenu *target = &menu;
        if (!t.category.isEmpty()) {
            QMenu *&sub = categories[t.category];
            if (!sub)
                sub = menu.addMenu(t.category);
            target = sub;
        }
        target->addAction(t.title)->setData(t.id);
    }
    bool hasOutput = false;
    for (const GraphNode &n : m_graph.nodes)
        hasOutput |= n.type == QLatin1String(NodeLibrary::OutputType);
    if (!hasOutput) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Material Output"))->setData(QString::fromLatin1(NodeLibrary::OutputType));
    }
    if (selectedNode() || !m_scene->selectedItems().isEmpty()) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Delete Selected"), this, &NodeEditorView::deleteSelected);
    }
    QAction *chosen = menu.exec(globalPos);
    if (!chosen || !chosen->data().isValid())
        return;
    NodeGraph after = m_graph;
    const int id = after.addNode(chosen->data().toString(), scenePos - QPointF(kNodeWidth / 2, kHeader / 2));
    after.node(id)->position = QPointF(qRound(after.node(id)->position.x()), qRound(after.node(id)->position.y()));
    emitEdit(after, QStringLiteral("Add node"));
}

void NodeEditorView::deleteSelected()
{
    NodeGraph after = m_graph;
    bool changed = false;
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        const auto *node = static_cast<NodeItem *>(item);
        if (node->nodeType() && node->nodeType()->isOutput)
            continue; // the material needs it
        after.removeNode(node->id());
        changed = true;
    }
    if (changed)
        emitEdit(after, QStringLiteral("Delete nodes"));
}

void NodeEditorView::drawBackground(QPainter *painter, const QRectF &rect)
{
    // Dot grid on the glass, every 24 units (sparser when zoomed out).
    const qreal step = transform().m11() < 0.6 ? 48 : 24;
    painter->setPen(QPen(QColor(255, 255, 255, 28), 2.0 / qMax(0.25, transform().m11())));
    const qreal left = std::floor(rect.left() / step) * step, top = std::floor(rect.top() / step) * step;
    QVector<QPointF> dots;
    for (qreal x = left; x < rect.right(); x += step)
        for (qreal y = top; y < rect.bottom(); y += step)
            dots.append(QPointF(x, y));
    painter->drawPoints(dots.data(), int(dots.size()));
}
