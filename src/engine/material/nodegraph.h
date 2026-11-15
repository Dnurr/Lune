#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <vector>

// A graph of nodes connected output -> input, e.g. a material: nodes compute values
// (colors, numbers) that end in a Material Output node. Plain data: what each node type
// does, and its ports, come from a NodeLibrary; the graph only stores instances, their
// input values and the links.
//
// Every input takes either a link from another node's output or its own value
// (`values`, by port name: float or QVector3D; colors are sRGB, 0..1). An input has at
// most one link; an output can feed any number of inputs. Links never form cycles.
struct GraphNode
{
    int id = 0;
    QString type;        // NodeLibrary type id, e.g. "mix", "output"
    QPointF position;    // in the node editor, top-left corner
    QVariantMap values;  // unlinked input values; missing = the type's default

    bool operator==(const GraphNode &) const = default;
};

struct GraphLink
{
    int fromNode = 0;
    QString fromPort; // an output
    int toNode = 0;
    QString toPort;   // an input

    bool operator==(const GraphLink &) const = default;
};

struct NodeGraph
{
    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;
    int nextId = 1;

    bool operator==(const NodeGraph &) const = default;

    const GraphNode *node(int id) const;
    GraphNode *node(int id);

    // New node with a fresh id; returns the id.
    int addNode(const QString &type, const QPointF &position);
    // Removes the node and every link to or from it.
    void removeNode(int id);

    // Connects an output to an input, replacing the input's previous link. Refused (false)
    // for unknown nodes, a node linked to itself, or a link that would close a cycle.
    bool connect(int fromNode, const QString &fromPort, int toNode, const QString &toPort);
    void disconnect(int toNode, const QString &toPort);
    // The link into an input, or nullptr.
    const GraphLink *linkTo(int toNode, const QString &toPort) const;
    // True if `to` can be reached from `from` following links forward.
    bool reaches(int from, int to) const;

    QJsonObject toJson() const;
    static NodeGraph fromJson(const QJsonObject &json);
};
