#include "nodegraph.h"

#include <QJsonArray>
#include <QVector2D>
#include <QVector3D>
#include <algorithm>

const GraphNode *NodeGraph::node(int id) const
{
    for (const GraphNode &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

GraphNode *NodeGraph::node(int id)
{
    for (GraphNode &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

int NodeGraph::addNode(const QString &type, const QPointF &position)
{
    GraphNode n;
    n.id = nextId++;
    n.type = type;
    n.position = position;
    nodes.push_back(n);
    return n.id;
}

void NodeGraph::removeNode(int id)
{
    std::erase_if(nodes, [id](const GraphNode &n) { return n.id == id; });
    std::erase_if(links, [id](const GraphLink &l) { return l.fromNode == id || l.toNode == id; });
}

bool NodeGraph::reaches(int from, int to) const
{
    if (from == to)
        return true;
    std::vector<int> stack { from };
    std::vector<int> seen;
    while (!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        if (std::find(seen.begin(), seen.end(), n) != seen.end())
            continue;
        seen.push_back(n);
        for (const GraphLink &l : links) {
            if (l.fromNode != n)
                continue;
            if (l.toNode == to)
                return true;
            stack.push_back(l.toNode);
        }
    }
    return false;
}

bool NodeGraph::connect(int fromNode, const QString &fromPort, int toNode, const QString &toPort)
{
    if (!node(fromNode) || !node(toNode) || fromNode == toNode)
        return false;
    if (reaches(toNode, fromNode))
        return false; // the new link would close a loop
    disconnect(toNode, toPort);
    links.push_back({ fromNode, fromPort, toNode, toPort });
    return true;
}

void NodeGraph::disconnect(int toNode, const QString &toPort)
{
    std::erase_if(links, [&](const GraphLink &l) { return l.toNode == toNode && l.toPort == toPort; });
}

const GraphLink *NodeGraph::linkTo(int toNode, const QString &toPort) const
{
    for (const GraphLink &l : links)
        if (l.toNode == toNode && l.toPort == toPort)
            return &l;
    return nullptr;
}

// ---------------------------------------------------------------------------
// JSON
//   { "nodes": [ { "id": 1, "type": "mix", "position": [x, y], "values": { "factor": 0.5,
//                  "a": [0.8, 0.8, 0.8], "offset": [0, 1],
//                  "image": { "image": "C:/textures/wood.png", "linear": false } } } ],
//     "links": [ { "from": [1, "result"], "to": [2, "baseColor"] } ] }
// ---------------------------------------------------------------------------

namespace
{
QJsonValue valueToJson(const QVariant &v)
{
    if (v.metaType().id() == QMetaType::QVector3D) {
        const QVector3D c = v.value<QVector3D>();
        return QJsonArray { c.x(), c.y(), c.z() };
    }
    if (v.metaType().id() == QMetaType::QVector2D) {
        const QVector2D c = v.value<QVector2D>();
        return QJsonArray { c.x(), c.y() };
    }
    if (v.metaType().id() == QMetaType::QVariantMap) // a texture: { image, linear }
        return QJsonObject::fromVariantMap(v.toMap());
    return v.toDouble();
}

QVariant valueFromJson(const QJsonValue &j)
{
    if (j.isArray()) {
        const QJsonArray a = j.toArray();
        if (a.size() == 3)
            return QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
        if (a.size() == 2)
            return QVector2D(float(a[0].toDouble()), float(a[1].toDouble()));
        return {};
    }
    if (j.isObject())
        return j.toObject().toVariantMap();
    if (j.isDouble())
        return float(j.toDouble());
    return {};
}
} // namespace

QJsonObject NodeGraph::toJson() const
{
    QJsonArray jsonNodes;
    for (const GraphNode &n : nodes) {
        QJsonObject values;
        for (auto it = n.values.cbegin(); it != n.values.cend(); ++it)
            values[it.key()] = valueToJson(it.value());
        QJsonObject o;
        o["id"] = n.id;
        o["type"] = n.type;
        o["position"] = QJsonArray { qRound(n.position.x()), qRound(n.position.y()) };
        if (!values.isEmpty())
            o["values"] = values;
        jsonNodes.append(o);
    }
    QJsonArray jsonLinks;
    for (const GraphLink &l : links) {
        QJsonObject o;
        o["from"] = QJsonArray { l.fromNode, l.fromPort };
        o["to"] = QJsonArray { l.toNode, l.toPort };
        jsonLinks.append(o);
    }
    QJsonObject json;
    json["nodes"] = jsonNodes;
    json["links"] = jsonLinks;
    return json;
}

NodeGraph NodeGraph::fromJson(const QJsonObject &json)
{
    NodeGraph g;
    for (const QJsonValue &v : json["nodes"].toArray()) {
        const QJsonObject o = v.toObject();
        GraphNode n;
        n.id = o["id"].toInt();
        n.type = o["type"].toString();
        const QJsonArray p = o["position"].toArray();
        if (p.size() == 2)
            n.position = QPointF(p[0].toDouble(), p[1].toDouble());
        const QJsonObject values = o["values"].toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QVariant value = valueFromJson(it.value());
            if (value.isValid())
                n.values[it.key()] = value;
        }
        if (n.id <= 0 || n.type.isEmpty() || g.node(n.id))
            continue; // broken or duplicate: skipped
        g.nodes.push_back(n);
        g.nextId = std::max(g.nextId, n.id + 1);
    }
    for (const QJsonValue &v : json["links"].toArray()) {
        const QJsonArray from = v.toObject()["from"].toArray(), to = v.toObject()["to"].toArray();
        if (from.size() == 2 && to.size() == 2)
            g.connect(from[0].toInt(), from[1].toString(), to[0].toInt(), to[1].toString());
    }
    return g;
}
