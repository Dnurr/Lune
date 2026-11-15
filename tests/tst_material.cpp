// Material node graphs: the graph model, node types read from GLSL, code generation,
// scene files and undo.
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>
#include <QVector3D>
#include <rhi/qshaderbaker.h>

#include "engine/io/sceneio.h"
#include "engine/material/materialcompiler.h"
#include "engine/material/nodegraph.h"
#include "engine/material/nodelibrary.h"
#include "engine/scene/primitives.h"
#include "engine/scene/scene.h"
#include "frontend/core/commands.h"

namespace
{
const QString kLibrary = QStringLiteral(R"(
// @node Mix (Color)
// @param a 0.8 0.8 0.8 color
// @param b 0.2 color
// @param factor 0.5 range(0, 1)
// @param result color
void node_mix(vec3 a, vec3 b, float factor, out vec3 result) { result = mix(a, b, factor); }

// @node Value (Input)
// @param value 0.25 fixed
void node_value(float value, out float result) { result = value; }

void node_bad(mat3 m, out float result) { result = m[0][0]; }

// @node Broken
// @param nothere 1
void node_broken(float a, out float result) { result = a; }
)");

QString readSource(const QString &name)
{
    QFile f(QStringLiteral(LUNE_SOURCE_DIR "/src/engine/render/shaders/") + name);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
} // namespace

class TestMaterial : public QObject
{
    Q_OBJECT

private slots:
    void libraryReadsNodesFromGlsl()
    {
        const NodeLibrary lib(kLibrary);
        const NodeType *mix = lib.type("mix");
        QVERIFY(mix);
        QCOMPARE(mix->title, QStringLiteral("Mix"));
        QCOMPARE(mix->category, QStringLiteral("Color"));
        QCOMPARE(int(mix->inputs.size()), 3);
        QCOMPARE(int(mix->outputs.size()), 1);
        QVERIFY(mix->inputs[0].color);
        QCOMPARE(mix->inputs[1].defaultValue.value<QVector3D>(), QVector3D(0.2f, 0.2f, 0.2f)); // one number: all three
        QVERIFY(mix->inputs[2].hasRange);
        QCOMPARE(mix->inputs[2].defaultValue.toFloat(), 0.5f);
        QVERIFY(mix->outputs[0].color);
        QVERIFY(!lib.type("value")->inputs[0].linkable);
        QVERIFY(lib.type("output")); // built in
        // Unsupported type and unknown @param: skipped, reported.
        QVERIFY(!lib.type("bad"));
        QVERIFY(!lib.type("broken"));
        QCOMPARE(lib.errors().size(), 2);
    }

    void shippedLibraryHasNoErrors()
    {
        const NodeLibrary lib(readSource("material_nodes.glsl"));
        QVERIFY2(lib.errors().isEmpty(), qPrintable(lib.errors().join('\n')));
        QVERIFY(lib.types().size() > 1);
    }

    void graphLinksRefuseLoops()
    {
        NodeGraph g;
        const int a = g.addNode("mix", {});
        const int b = g.addNode("mix", {});
        QVERIFY(g.connect(a, "result", b, "a"));
        QVERIFY(!g.connect(b, "result", a, "a")); // would close a loop
        QVERIFY(!g.connect(a, "result", a, "b")); // itself
        QVERIFY(g.connect(a, "result", b, "a"));  // replaces, doesn't duplicate
        QCOMPARE(int(g.links.size()), 1);
        g.removeNode(a);
        QVERIFY(g.links.empty());
    }

    void graphJsonRoundTrip()
    {
        NodeGraph g = Material::defaultGraph();
        const int mix = g.addNode("mix", QPointF(-200, 10));
        g.node(mix)->values["factor"] = 0.75f;
        g.node(mix)->values["a"] = QVector3D(1, 0, 0);
        g.connect(mix, "result", 1, "baseColor");
        const NodeGraph back = NodeGraph::fromJson(g.toJson());
        QCOMPARE(back, g);
        QCOMPARE(back.nextId, g.nextId);
    }

    void compileGeneratesCallsAndParams()
    {
        const NodeLibrary lib(kLibrary);
        NodeGraph g = Material::defaultGraph(); // output = node 1
        const int value = g.addNode("value", {});
        const int mix = g.addNode("mix", {});
        g.addNode("mix", {}); // not connected: no code
        g.connect(value, "result", mix, "factor");
        g.connect(mix, "result", 1, "baseColor");
        g.connect(value, "result", 1, "roughness");

        const CompiledMaterial c = MaterialCompiler::compile(g, lib);
        QVERIFY2(c.ok(), qPrintable(c.error));
        QVERIFY(c.code.contains("Surface materialSurface()"));
        QVERIFY(c.code.contains(QStringLiteral("node_value(materialParams[0].x, n%1_result);").arg(value)));
        // mix: a, b from params, factor from the value node.
        QVERIFY(c.code.contains(QStringLiteral("node_mix(materialParams[1].xyz, materialParams[2].xyz, n%1_result, n%2_result);")
                                    .arg(value)
                                    .arg(mix)));
        QVERIFY(c.code.contains(QStringLiteral("s.baseColor = n%1_result;").arg(mix)));
        QVERIFY(c.code.contains(QStringLiteral("s.roughness = n%1_result;").arg(value)));
        QVERIFY(c.code.contains("s.metallic = materialParams[3].x;"));
        QCOMPARE(int(c.params.size()), 4);
        QVERIFY(c.params[1].color);
        QCOMPARE(c.code.count("node_mix("), 2); // the library's definition + one call
    }

    void compileConvertsTypes()
    {
        const NodeLibrary lib(kLibrary);
        NodeGraph g = Material::defaultGraph();
        const int value = g.addNode("value", {});
        const int mix = g.addNode("mix", {});
        g.connect(value, "result", 1, "baseColor"); // float -> vec3
        g.connect(mix, "result", 1, "metallic");    // vec3 -> float
        const CompiledMaterial c = MaterialCompiler::compile(g, lib);
        QVERIFY(c.code.contains(QStringLiteral("s.baseColor = vec3(n%1_result);").arg(value)));
        QVERIFY(c.code.contains(QStringLiteral("s.metallic = dot(n%1_result, vec3(0.2126, 0.7152, 0.0722));").arg(mix)));
    }

    void compileWithoutOutputFails()
    {
        NodeGraph g;
        g.addNode("mix", {});
        const CompiledMaterial c = MaterialCompiler::compile(g, NodeLibrary(kLibrary));
        QVERIFY(!c.ok());
        QVERIFY(c.code.isEmpty());
    }

    void insertReplacesTheMarkedBlock()
    {
        const QString frag = "a\n//@material-begin\nold\n//@material-end\nb\n";
        const QString out = MaterialCompiler::insertInto(frag, "new\n");
        QVERIFY(out.contains("new"));
        QVERIFY(!out.contains("old"));
        QVERIFY(out.startsWith("a\n//@material-begin"));
        QVERIFY(out.endsWith("//@material-end\nb\n"));
        QVERIFY(MaterialCompiler::insertInto("no markers", "x").isEmpty());
    }

    void meshFragHasTheMarkers()
    {
        const QString frag = readSource("mesh.frag");
        QVERIFY(!frag.isEmpty());
        QVERIFY(!MaterialCompiler::insertInto(frag, "// code\n").isEmpty());
        QVERIFY(frag.contains(QStringLiteral("MAX_MATERIAL_PARAMS %1").arg(MaterialCompiler::MaxParams)));
    }

    void generatedShaderCompiles()
    {
        // The real mesh.frag and node library, with a graph using every shipped node type.
        const NodeLibrary lib(readSource("material_nodes.glsl"));
        NodeGraph g = Material::defaultGraph();
        int previous = 0;
        for (const NodeType &t : lib.types()) {
            if (t.isOutput)
                continue;
            const int n = g.addNode(t.id, {});
            if (previous)
                g.connect(previous, lib.type(g.node(previous)->type)->outputs[0].name, n, t.inputs.empty() ? QString() : t.inputs.back().name);
            previous = n;
        }
        g.connect(previous, lib.type(g.node(previous)->type)->outputs[0].name, 1, "baseColor");
        const CompiledMaterial c = MaterialCompiler::compile(g, lib);
        QVERIFY2(c.ok(), qPrintable(c.error));
        const QString source = MaterialCompiler::insertInto(readSource("mesh.frag"), c.code);
        QVERIFY(!source.isEmpty());

        QShaderBaker baker;
        baker.setGeneratedShaders({ { QShader::SpirvShader, QShaderVersion(100) }, { QShader::HlslShader, QShaderVersion(50) } });
        baker.setGeneratedShaderVariants({ QShader::StandardShader });
        baker.setSourceString(source.toUtf8(), QShader::FragmentStage);
        const QShader shader = baker.bake();
        QVERIFY2(shader.isValid(), qPrintable(baker.errorMessage()));
    }

    void sceneFileRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("materials.lune");
        Scene scene;
        Material m;
        m.name = "Rust";
        m.graph = Material::defaultGraph();
        m.graph.node(1)->values["roughness"] = 0.8f;
        const MaterialId id = scene.addMaterial(m);
        Entity e;
        e.name = "Cube";
        e.mesh = Primitives::cube();
        e.material = id;
        scene.createEntity(e);
        QVERIFY(SceneIO::save(scene, path, {}));

        Scene loaded;
        QStringList warnings;
        QVERIFY(SceneIO::load(loaded, path, nullptr, &warnings));
        QVERIFY(warnings.isEmpty());
        QCOMPARE(int(loaded.materials().size()), 1);
        QCOMPARE(loaded.materials()[0].name, QStringLiteral("Rust"));
        QCOMPARE(loaded.materials()[0].graph, scene.materials()[0].graph);
        QCOMPARE(loaded.entities()[0].material, id);
    }

    void unknownMaterialIsAWarning()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("bad.lune");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"format":"lune-scene","version":1,"entities":[{"id":1,"name":"A","material":7}]})");
        f.close();
        Scene scene;
        QStringList warnings;
        QVERIFY(SceneIO::load(scene, path, nullptr, &warnings));
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(scene.entities()[0].material, NoMaterial);
    }

    void commandsUndoRedo()
    {
        Scene scene;
        QUndoStack stack;
        const EntityId cube = scene.createEntity("Cube");
        Material proto;
        proto.name = "M";
        proto.graph = Material::defaultGraph();
        auto *add = new AddMaterialCommand(&scene, proto, cube);
        stack.push(add);
        const MaterialId id = add->materialId();
        QVERIFY(scene.material(id));
        QCOMPARE(scene.entity(cube)->material, id);

        // Value scrubbing merges into one step.
        NodeGraph g = scene.material(id)->graph;
        for (float v : { 0.1f, 0.2f, 0.3f }) {
            g.node(1)->values["roughness"] = v;
            stack.push(new SetMaterialGraphCommand(&scene, id, g, "Edit", "value:1:roughness"));
        }
        QCOMPARE(stack.count(), 2);
        QCOMPARE(scene.material(id)->graph.node(1)->values["roughness"].toFloat(), 0.3f);

        stack.undo();
        QVERIFY(scene.material(id)->graph.node(1)->values.isEmpty());
        stack.undo();
        QVERIFY(!scene.material(id));
        QCOMPARE(scene.entity(cube)->material, NoMaterial);
        stack.redo();
        stack.redo();
        QCOMPARE(scene.entity(cube)->material, id); // same id again
        QCOMPARE(scene.material(id)->graph.node(1)->values["roughness"].toFloat(), 0.3f);
    }
};

QTEST_GUILESS_MAIN(TestMaterial)
#include "tst_material.moc"
