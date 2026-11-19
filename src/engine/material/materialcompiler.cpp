#include "materialcompiler.h"

#include <algorithm>
#include <functional>

namespace MaterialCompiler
{
namespace
{
QString glslType(PortType t)
{
    switch (t) {
    case PortType::Float: return QStringLiteral("float");
    case PortType::Vec2: return QStringLiteral("vec2");
    case PortType::Vec3: return QStringLiteral("vec3");
    case PortType::Texture: return QStringLiteral("sampler2D");
    case PortType::Entity: return QStringLiteral("float");
    }
    return QString();
}

QString convert(const QString &expr, PortType from, PortType to)
{
    if (from == to)
        return expr;
    if (from == PortType::Float)
        return QStringLiteral("%1(%2)").arg(glslType(to), expr);
    if (from == PortType::Vec3 && to == PortType::Float)
        return QStringLiteral("dot(%1, vec3(0.2126, 0.7152, 0.0722))").arg(expr);
    if (from == PortType::Vec3 && to == PortType::Vec2)
        return QStringLiteral("(%1).xy").arg(expr);
    if (from == PortType::Vec2 && to == PortType::Float)
        return QStringLiteral("(%1).x").arg(expr);
    if (from == PortType::Vec2 && to == PortType::Vec3)
        return QStringLiteral("vec3(%1, 0.0)").arg(expr);
    return expr; // textures are never linked
}

QString swizzle(PortType t)
{
    switch (t) {
    case PortType::Vec2: return QStringLiteral("xy");
    case PortType::Vec3: return QStringLiteral("xyz");
    default: return QStringLiteral("x");
    }
}

} // namespace

QString variable(int node, const QString &port)
{
    return QStringLiteral("n%1_%2").arg(node).arg(port);
}

QVariant inputValue(const GraphNode &node, const PortDesc &port)
{
    const QVariant v = node.values.value(port.name);
    return v.isValid() ? v : port.defaultValue;
}

GraphCode generate(const NodeGraph &graph, const NodeLibrary &library, const QString &paramArray,
                   const QString &texturePrefix, const QString &noOutput, const BuiltinNode &builtin)
{
    GraphCode result;
    const GraphNode *output = nullptr;
    for (const GraphNode &n : graph.nodes) {
        if (n.type == QLatin1String(NodeLibrary::OutputType)) {
            output = &n;
            break;
        }
    }
    if (!output) {
        result.error = noOutput;
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
        if (in.type == PortType::Texture) {
            const int slot = int(result.textures.size());
            result.textures.push_back({ n.id, in.name });
            return texturePrefix + QString::number(slot);
        }
        if (!in.expression.isEmpty())
            return QStringLiteral("(%1)").arg(in.expression);
        const int slot = int(result.params.size());
        result.params.push_back({ n.id, in.name, in.type, in.color });
        return QStringLiteral("%1[%2].%3").arg(paramArray).arg(slot).arg(swizzle(in.type));
    };

    QString body;
    for (const GraphNode *n : order) {
        const NodeType *type = library.type(n->type);
        if (type->isOutput)
            continue;
        if (type->builtin) {
            body += QStringLiteral("    // %1 (node %2)\n").arg(type->title).arg(n->id);
            if (builtin)
                body += builtin(*n, *type, [&](const PortDesc &in) { return inputExpr(*n, in); });
            continue;
        }
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
    for (const PortDesc &in : outType->inputs)
        result.outputInputs[in.name] = inputExpr(*output, in);
    result.body = body;
    return result;
}

CompiledMaterial compile(const NodeGraph &graph, const NodeLibrary &library)
{
    CompiledMaterial result;
    const GraphCode g = generate(graph, library, QStringLiteral("materialParams"), QStringLiteral("materialTexture"),
                                 QStringLiteral("The material has no Material Output node."));
    if (!g.error.isEmpty()) {
        result.error = g.error;
        return result;
    }
    result.params = g.params;
    result.textures = g.textures;
    QString body = g.body;
    body += QStringLiteral("    Surface s;\n");
    for (const PortDesc &in : library.outputType()->inputs)
        body += QStringLiteral("    s.%1 = %2;\n").arg(in.name, g.outputInputs.at(in.name));
    body += QStringLiteral("    return s;\n");

    if (int(result.params.size()) > MaxParams) {
        result.error = QStringLiteral("Too many unlinked inputs (%1, at most %2).").arg(result.params.size()).arg(MaxParams);
        result.params.clear();
        result.textures.clear();
        return result;
    }
    if (int(result.textures.size()) > MaxTextures) {
        result.error = QStringLiteral("Too many textures (%1, at most %2).").arg(result.textures.size()).arg(MaxTextures);
        result.params.clear();
        result.textures.clear();
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

QString shadowSource(const QString &fragmentSource, const QString &code, const QString &shadowMain)
{
    const int begin = fragmentSource.indexOf(BeginMarker);
    const int end = fragmentSource.indexOf(EndMarker);
    if (begin < 0 || end < begin)
        return QString();
    return fragmentSource.left(begin) + code + QLatin1Char('\n') + shadowMain;
}
} // namespace MaterialCompiler
