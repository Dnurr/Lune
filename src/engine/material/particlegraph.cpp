#include "particlegraph.h"

#include <cmath>

namespace ParticleGraph
{
Compiled compile(const NodeGraph &graph, const NodeLibrary &library)
{
    Compiled result;
    std::vector<quint64> objects;
    // Built-in nodes: their statements.
    const BuiltinNode builtin = [&](const GraphNode &node, const NodeType &type,
                                    const std::function<QString(const PortDesc &)> &input) -> QString {
        auto var = [&](const char *port) { return MaterialCompiler::variable(node.id, QString::fromLatin1(port)); };
        if (type.id == QLatin1String("scene_forces")) {
            const QString out = var("result");
            return QStringLiteral("    vec3 %1 = %2;\n"
                                  "    for (uint i = s.forces.x; i < s.forces.x + s.forces.y; ++i)\n"
                                  "        %1 += forceAcceleration(forces[i], p, time);\n")
                .arg(out, input(*type.input(QStringLiteral("acceleration"))));
        }
        if (type.id == QLatin1String("object")) {
            const int slot = int(objects.size());
            const double id = node.values.value(QStringLiteral("object")).toDouble();
            objects.push_back(id > 0.0 ? quint64(std::llround(id)) : 0);
            const QString m = MaterialCompiler::variable(node.id, QStringLiteral("m"));
            return QStringLiteral("    mat4 %1 = forces[s.objects.x + %2u].transform;\n"
                                  "    vec3 %3 = %1[3].xyz;\n"
                                  "    vec3 %4 = normalize(mat3(%1) * vec3(0.0, 1.0, 0.0));\n"
                                  "    vec3 %5 = normalize(mat3(%1) * vec3(0.0, 0.0, -1.0));\n"
                                  "    vec3 %6 = normalize(mat3(%1) * vec3(1.0, 0.0, 0.0));\n")
                .arg(m)
                .arg(slot)
                .arg(var("position"), var("axis"), var("forward"), var("right"));
        }
        return QString();
    };
    const GraphCode g = MaterialCompiler::generate(graph, library, QStringLiteral("graphParams"), QStringLiteral("graphTexture"),
                                                   QStringLiteral("The graph has no Particle Output node."), builtin);
    if (!g.error.isEmpty()) {
        result.error = g.error;
        return result;
    }
    if (!g.textures.empty()) {
        result.error = QStringLiteral("Particle graphs can't use images.");
        return result;
    }
    if (int(g.params.size()) > MaxParams) {
        result.error = QStringLiteral("Too many unlinked inputs (%1, at most %2).").arg(g.params.size()).arg(MaxParams);
        return result;
    }
    if (int(objects.size()) > MaxObjects) {
        result.error = QStringLiteral("Too many Object nodes (%1, at most %2).").arg(objects.size()).arg(MaxObjects);
        return result;
    }
    result.params = g.params;
    result.objects = objects;
    result.code = QStringLiteral("// Generated from the emitter's node graph.\n") + library.source()
        + QStringLiteral("\nvec3 graphAcceleration(Particle p, Step s, float time)\n{\n") + g.body
        + QStringLiteral("    return %1;\n}\n").arg(g.outputInputs.at(QStringLiteral("acceleration")));
    return result;
}

NodeGraph defaultGraph()
{
    NodeGraph g;
    const int forces = g.addNode(QStringLiteral("scene_forces"), QPointF(-260, 0));
    const int out = g.addNode(QString::fromLatin1(NodeLibrary::OutputType), QPointF(40, 0));
    g.connect(forces, QStringLiteral("result"), out, QStringLiteral("acceleration"));
    return g;
}

QString insertInto(const QString &source, const QString &code)
{
    const int begin = source.indexOf(BeginMarker);
    const int end = source.indexOf(EndMarker);
    if (begin < 0 || end < begin)
        return QString();
    return source.left(begin + BeginMarker.size()) + QLatin1Char('\n') + code + source.mid(end);
}
} // namespace ParticleGraph
