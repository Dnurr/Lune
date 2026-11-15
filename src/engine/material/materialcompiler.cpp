#include "materialcompiler.h"

#include <algorithm>
#include <functional>

namespace MaterialCompiler
{
namespace
{
QString glslType(PortType t)
{
    return t == PortType::Vec3 ? QStringLiteral("vec3") : QStringLiteral("float");
}

QString convert(const QString &expr, PortType from, PortType to)
{
    if (from == to)
        return expr;
    if (from == PortType::Float)
        return QStringLiteral("vec3(%1)").arg(expr);
    return QStringLiteral("dot(%1, vec3(0.2126, 0.7152, 0.0722))").arg(expr);
}

QString variable(int node, const QString &port)
{
    return QStringLiteral("n%1_%2").arg(node).arg(port);
}
} // namespace

QVariant inputValue(const GraphNode &node, const PortDesc &port)
{
    const QVariant v = node.values.value(port.name);
    return v.isValid() ? v : port.defaultValue;
}

CompiledMaterial compile(const NodeGraph &graph, const NodeLibrary &library)
{
    CompiledMaterial result;
    const GraphNode *output = nullptr;
    for (const GraphNode &n : graph.nodes) {
        if (n.type == QLatin1String(NodeLibrary::OutputType)) {
            output = &n;
            break;
        }
    }
    if (!output) {
        result.error = QStringLiteral("The material has no Material Output node.");
        return result;
    }

    // Dependency order: every node after the nodes feeding it (depth-first from the output).
    std::vector<const GraphNode *> order;
    std::vector<int> visiting;
    QString error;
    std::function<void(const GraphNode &)> visit = [&](const GraphNode &n) {
        if (!error.isEmpty() || std::find(order.begin(), order.end(), &n) != order.end())
            return;
        if (std::find(visiting.begin(), visiting.end(), n.id) != visiting.end()) {
            error = QStringLiteral("The graph has a loop."); // NodeGraph::connect prevents it; files might not
            return;
        }
        const NodeType *type = library.type(n.type);
        if (!type) {
            error = QStringLiteral("Unknown node type \"%1\".").arg(n.type);
            return;
        }
        visiting.push_back(n.id);
        for (const PortDesc &in : type->inputs) {
            if (const GraphLink *l = graph.linkTo(n.id, in.name)) {
                if (const GraphNode *from = graph.node(l->fromNode))
                    visit(*from);
            }
        }
        visiting.pop_back();
        order.push_back(&n);
    };
    visit(*output);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }

    // Expression feeding an input: the linked output (converted) or a parameter slot.
    auto inputExpr = [&](const GraphNode &n, const PortDesc &in) -> QString {
        if (in.linkable) {
            if (const GraphLink *l = graph.linkTo(n.id, in.name)) {
                const GraphNode *from = graph.node(l->fromNode);
                const NodeType *fromType = from ? library.type(from->type) : nullptr;
                const PortDesc *out = fromType ? fromType->output(l->fromPort) : nullptr;
                if (out)
                    return convert(variable(from->id, out->name), out->type, in.type);
                // A link to an output that no longer exists: use the value instead.
            }
        }
        const int slot = int(result.params.size());
        result.params.push_back({ n.id, in.name, in.type, in.color });
        return QStringLiteral("materialParams[%1].%2").arg(slot).arg(in.type == PortType::Vec3 ? "xyz" : "x");
    };

    QString body;
    for (const GraphNode *n : order) {
        const NodeType *type = library.type(n->type);
        if (type->isOutput)
            continue;
        QStringList args;
        for (const PortDesc &in : type->inputs)
            args.append(inputExpr(*n, in));
        body += QStringLiteral("    // %1 (node %2)\n").arg(type->title).arg(n->id);
        for (const PortDesc &out : type->outputs) {
            body += QStringLiteral("    %1 %2;\n").arg(glslType(out.type), variable(n->id, out.name));
            args.append(variable(n->id, out.name));
        }
        body += QStringLiteral("    node_%1(%2);\n").arg(type->id, args.join(QStringLiteral(", ")));
    }
    const NodeType *outType = library.type(output->type);
    body += QStringLiteral("    Surface s;\n");
    for (const PortDesc &in : outType->inputs)
        body += QStringLiteral("    s.%1 = %2;\n").arg(in.name, inputExpr(*output, in));
    body += QStringLiteral("    return s;\n");

    if (int(result.params.size()) > MaxParams) {
        result.error = QStringLiteral("Too many unlinked inputs (%1, at most %2).").arg(result.params.size()).arg(MaxParams);
        result.params.clear();
        return result;
    }

    result.code = QStringLiteral("// Generated from the material's node graph.\n") + library.source()
        + QStringLiteral("\nSurface materialSurface()\n{\n") + body + QStringLiteral("}\n");
    return result;
}

QString insertInto(const QString &fragmentSource, const QString &code)
{
    const int begin = fragmentSource.indexOf(BeginMarker);
    const int end = fragmentSource.indexOf(EndMarker);
    if (begin < 0 || end < begin)
        return QString();
    const int afterBegin = begin + BeginMarker.size();
    return fragmentSource.left(afterBegin) + QLatin1Char('\n') + code + fragmentSource.mid(end);
}
} // namespace MaterialCompiler
