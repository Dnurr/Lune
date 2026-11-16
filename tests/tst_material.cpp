// Material node graphs: the graph model, node types read from GLSL, code generation,
// scene files and undo.
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QUndoStack>
#include <QVector2D>
#include <QVector3D>
#include <rhi/qshaderbaker.h>

#include "engine/io/sceneio.h"
#include "engine/material/gltfmaterialgraph.h"
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

// Compiles generated material code inside the real mesh.frag, for Vulkan and Direct3D.
QString bakeError(const CompiledMaterial &c)
{
    const QString source = MaterialCompiler::insertInto(readSource("mesh.frag"), c.code);
    if (source.isEmpty())
        return QStringLiteral("no markers");
    QShaderBaker baker;
    baker.setGeneratedShaders({ { QShader::SpirvShader, QShaderVersion(100) }, { QShader::HlslShader, QShaderVersion(50) } });
    baker.setGeneratedShaderVariants({ QShader::StandardShader });
    baker.setSourceString(source.toUtf8(), QShader::FragmentStage);
    return baker.bake().isValid() ? QString() : baker.errorMessage();
}

int countNodes(const NodeGraph &g, const QString &type)
{
    return int(std::count_if(g.nodes.begin(), g.nodes.end(), [&](const GraphNode &n) { return n.type == type; }));
}

// The node linked into an input (nullptr if none).
const GraphNode *source(const NodeGraph &g, int node, const QString &port, QString *fromPort = nullptr)
{
    const GraphLink *l = g.linkTo(node, port);
    if (l && fromPort)
        *fromPort = l->fromPort;
    return l ? g.node(l->fromNode) : nullptr;
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
        // normal: unlinked, its expression; occlusion and emission: values.
        QVERIFY(c.code.contains("s.normal = (normalize(v_normal));"));
        QVERIFY(c.code.contains("s.occlusion = materialParams[4].x;"));
        QVERIFY(c.code.contains("s.emission = materialParams[5].xyz;"));
        QVERIFY(c.code.contains("s.alpha = materialParams[6].x;"));
        QCOMPARE(int(c.params.size()), 7);
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

        // The shadow pass variant: mesh.frag's declarations + the same code + shadow_material.glsl.
        const QString shadow = MaterialCompiler::shadowSource(readSource("mesh.frag"), c.code,
                                                              readSource("shadow_material.glsl"));
        QVERIFY(!shadow.isEmpty());
        QVERIFY(!shadow.contains("//@material-begin"));
        QCOMPARE(shadow.count("void main()"), 1); // mesh.frag's own main() isn't in it
        baker.setSourceString(shadow.toUtf8(), QShader::FragmentStage);
        QVERIFY2(baker.bake().isValid(), qPrintable(baker.errorMessage()));
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
        e.materials = { id };
        scene.createEntity(e);
        QVERIFY(SceneIO::save(scene, path, {}));

        Scene loaded;
        QStringList warnings;
        QVERIFY(SceneIO::load(loaded, path, nullptr, &warnings));
        QVERIFY(warnings.isEmpty());
        QCOMPARE(int(loaded.materials().size()), 1);
        QCOMPARE(loaded.materials()[0].name, QStringLiteral("Rust"));
        QCOMPARE(loaded.materials()[0].graph, scene.materials()[0].graph);
        QCOMPARE(loaded.materials()[0].alphaMode, AlphaMode::Opaque);
        QCOMPARE(loaded.entities()[0].material(), id);

        // Alpha mode and cutoff
        scene.setMaterialAlpha(id, AlphaMode::Clip, 0.3f);
        QVERIFY(SceneIO::save(scene, path, {}));
        Scene clipped;
        QVERIFY(SceneIO::load(clipped, path, nullptr, nullptr));
        QCOMPARE(clipped.materials()[0].alphaMode, AlphaMode::Clip);
        QCOMPARE(clipped.materials()[0].alphaCutoff, 0.3f);
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
        QCOMPARE(scene.entities()[0].material(), NoMaterial);
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
        QCOMPARE(scene.entity(cube)->material(), id);

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
        QCOMPARE(scene.entity(cube)->material(), NoMaterial);
        stack.redo();
        stack.redo();
        QCOMPARE(scene.entity(cube)->material(), id); // same id again
        QCOMPARE(scene.material(id)->graph.node(1)->values["roughness"].toFloat(), 0.3f);

        // Alpha: a mode change is a step, cutoff scrubbing merges into one.
        const int before = stack.count();
        stack.push(new SetMaterialAlphaCommand(&scene, id, AlphaMode::Clip, 0.5f));
        for (float c : { 0.4f, 0.3f, 0.2f })
            stack.push(new SetMaterialAlphaCommand(&scene, id, AlphaMode::Clip, c));
        QCOMPARE(stack.count(), before + 2);
        QCOMPARE(scene.material(id)->alphaCutoff, 0.2f);
        stack.undo();
        QCOMPARE(scene.material(id)->alphaCutoff, 0.5f);
        QCOMPARE(scene.material(id)->alphaMode, AlphaMode::Clip);
        stack.undo();
        QCOMPARE(scene.material(id)->alphaMode, AlphaMode::Opaque);
    }

    void materialSlots()
    {
        Scene scene;
        Entity e;
        e.name = "Model";
        e.mesh = Primitives::cube();
        const EntityId id = scene.createEntity(e);
        const MaterialId a = scene.addMaterial(Material { NoMaterial, "A", Material::defaultGraph() });

        QUndoStack stack;
        stack.push(new AssignMaterialCommand(&scene, id, 2, a));
        QCOMPARE(scene.entity(id)->material(2), a);
        QCOMPARE(scene.entity(id)->material(0), NoMaterial);
        QCOMPARE(int(scene.entity(id)->materials.size()), 3);
        stack.undo();
        QVERIFY(scene.entity(id)->materials.empty()); // trailing empty slots are dropped
    }

    void libraryReadsTexturePortsAndExpressions()
    {
        const NodeLibrary lib(QStringLiteral(R"(
// @node Tex (Texture)
// @param image normal
// @param uv expr(v_uv * vec2(2.0, 1.0))
// @param offset 0.5 1
void node_tex(sampler2D image, vec2 uv, vec2 offset, out vec3 color) { color = texture(image, uv + offset).rgb; }

// @node Bad
void node_outsampler(out sampler2D s) { }

// A signature in a comment isn't a node:
//   void node_fake(float a, out float b)
)"));
        const NodeType *t = lib.type("tex");
        QVERIFY(t);
        const PortDesc *image = t->input("image");
        QCOMPARE(image->type, PortType::Texture);
        QVERIFY(!image->linkable);
        QCOMPARE(image->fallback, TextureFallback::Normal);
        QVERIFY(image->linear); // normal maps are data
        const PortDesc *uv = t->input("uv");
        QCOMPARE(uv->type, PortType::Vec2);
        QCOMPARE(uv->expression, QStringLiteral("v_uv * vec2(2.0, 1.0)"));
        QVERIFY(!uv->hasValue());
        QCOMPARE(t->input("offset")->defaultValue.value<QVector2D>(), QVector2D(0.5f, 1.0f));
        QVERIFY(!lib.type("outsampler"));
        QVERIFY(!lib.type("fake"));
        QCOMPARE(lib.errors().size(), 1);

        const TextureValue v { "C:/a.png", true };
        QCOMPARE(TextureValue::fromVariant(v.toVariant(), false), v);
        QVERIFY(TextureValue::fromVariant(QVariant(), true).linear); // no value: the port's default
    }

    void compileTexturesExpressionsAndVec2()
    {
        const NodeLibrary lib(readSource("material_nodes.glsl"));
        QVERIFY2(lib.errors().isEmpty(), qPrintable(lib.errors().join('\n')));
        NodeGraph g = Material::defaultGraph();
        const int tex = g.addNode("image_texture", {});
        g.node(tex)->values["image"] = TextureValue { "C:/wood.png", false }.toVariant();
        QVERIFY(g.connect(tex, "color", 1, "baseColor"));
        QVERIFY(g.connect(tex, "alpha", 1, "roughness"));

        CompiledMaterial c = MaterialCompiler::compile(g, lib);
        QVERIFY2(c.ok(), qPrintable(c.error));
        // The image is a sampler slot, the unlinked uv its expression: no parameters for them.
        QVERIFY(c.code.contains(QStringLiteral("node_image_texture(materialTexture0, (v_uv), n%1_color, n%1_alpha);").arg(tex)));
        QCOMPARE(int(c.textures.size()), 1);
        QCOMPARE(c.textures[0].node, tex);
        QCOMPARE(c.textures[0].port, QStringLiteral("image"));
        for (const MaterialParam &p : c.params)
            QVERIFY(p.node != tex);
        QVERIFY2(bakeError(c).isEmpty(), qPrintable(bakeError(c)));

        // Linked UVs; vec2 into a float and into a vec3.
        const int uv = g.addNode("texcoord", {});
        QVERIFY(g.connect(uv, "uv", tex, "uv"));
        QVERIFY(g.connect(uv, "uv", 1, "metallic"));
        QVERIFY(g.connect(uv, "uv", 1, "emission"));
        c = MaterialCompiler::compile(g, lib);
        QVERIFY2(c.ok(), qPrintable(c.error));
        QVERIFY(c.code.contains(QStringLiteral("node_image_texture(materialTexture0, n%1_uv,").arg(uv)));
        QVERIFY(c.code.contains(QStringLiteral("s.metallic = (n%1_uv).x;").arg(uv)));
        QVERIFY(c.code.contains(QStringLiteral("s.emission = vec3(n%1_uv, 0.0);").arg(uv)));
        QVERIFY2(bakeError(c).isEmpty(), qPrintable(bakeError(c)));
    }

    void tooManyTexturesIsAnError()
    {
        const NodeLibrary lib(readSource("material_nodes.glsl"));
        NodeGraph g = Material::defaultGraph();
        int previous = g.addNode("image_texture", {});
        QString previousPort = "color";
        for (int i = 1; i <= MaterialCompiler::MaxTextures; ++i) { // one texture too many
            const int tex = g.addNode("image_texture", {});
            const int mul = g.addNode("multiply_color", {});
            g.connect(previous, previousPort, mul, "a");
            g.connect(tex, "color", mul, "b");
            previous = mul;
            previousPort = "result";
        }
        g.connect(previous, previousPort, 1, "baseColor");
        const CompiledMaterial c = MaterialCompiler::compile(g, lib);
        QVERIFY(!c.ok());
        QVERIFY(c.error.contains("Too many textures"));
    }

    void imagePathsAreRelativeInSceneFiles()
    {
        QTemporaryDir dir;
        const QString image = QDir::cleanPath(dir.filePath("textures/wood.png"));
        const QString packed = QDir::cleanPath(dir.filePath("models/chair.glb")) + "#image2";
        Scene scene;
        Material m { NoMaterial, "Wood", Material::defaultGraph() };
        const int a = m.graph.addNode("image_texture", {});
        m.graph.node(a)->values["image"] = TextureValue { image, false }.toVariant();
        const int b = m.graph.addNode("image_texture", {});
        m.graph.node(b)->values["image"] = TextureValue { packed, true }.toVariant();
        scene.addMaterial(m);

        const QString path = dir.filePath("scene.lune");
        QString error;
        QVERIFY2(SceneIO::save(scene, path, {}, &error), qPrintable(error));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("\"textures/wood.png\""));
        QVERIFY(text.contains("\"models/chair.glb#image2\""));
        QVERIFY(!text.contains(QDir::cleanPath(dir.path()).toUtf8()));

        Scene loaded;
        QVERIFY2(SceneIO::load(loaded, path, nullptr, nullptr, &error), qPrintable(error));
        const NodeGraph &g = loaded.materials()[0].graph;
        QCOMPARE(TextureValue::fromVariant(g.node(a)->values["image"], false), (TextureValue { image, false }));
        QCOMPARE(TextureValue::fromVariant(g.node(b)->values["image"], false), (TextureValue { packed, true }));
    }

    void gltfMaterialBecomesAGraph()
    {
        // First without a Normal Map node (the shipped one renamed away).
        const NodeLibrary lib(readSource("material_nodes.glsl").replace("node_normal_map(", "node_no_normal_map("));
        GltfMaterial m;
        m.name = "Chair";
        m.baseColorTexture = "C:/m/base.png";
        m.baseColorFactor = QVector4D(0.5f, 0.5f, 0.5f, 1.0f);
        m.metallicRoughnessTexture = "C:/m/orm.png";
        m.occlusionTexture = "C:/m/orm.png"; // packed: one texture node for both
        m.roughnessFactor = 0.5f;
        m.normalTexture = "C:/m/normal.png";
        const NodeGraph g = graphFromGltf(m, lib);

        QCOMPARE(countNodes(g, "image_texture"), 3);
        QCOMPARE(countNodes(g, "separate_rgb"), 1);
        int out = 0;
        for (const GraphNode &n : g.nodes)
            if (n.type == "output")
                out = n.id;
        QString port;
        // Base color: texture x factor (stored sRGB: 0.5 linear -> ~0.735).
        const GraphNode *tint = source(g, out, "baseColor");
        QVERIFY(tint && tint->type == "multiply_color");
        QVERIFY(qAbs(tint->values["b"].value<QVector3D>().x() - 0.7354f) < 1e-3f);
        const GraphNode *base = source(g, tint->id, "a");
        QVERIFY(!TextureValue::fromVariant(base->values["image"], true).linear);
        // Roughness: G x 0.5; metallic: B (factor 1, no multiply); occlusion: R.
        const GraphNode *rough = source(g, out, "roughness");
        QVERIFY(rough && rough->type == "multiply");
        QCOMPARE(rough->values["b"].toFloat(), 0.5f);
        QVERIFY(source(g, rough->id, "a", &port)->type == "separate_rgb" && port == "g");
        QVERIFY(source(g, out, "metallic", &port)->type == "separate_rgb" && port == "b");
        QVERIFY(source(g, out, "occlusion", &port)->type == "separate_rgb" && port == "r");
        // No Normal Map node in the library: the normal texture stays unlinked.
        QVERIFY(!g.linkTo(out, "normal"));
        QVERIFY(!g.linkTo(out, "alpha")); // OPAQUE: alpha unused
        QVERIFY(!g.node(out)->values.contains("alpha"));

        CompiledMaterial c = MaterialCompiler::compile(g, lib);
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(int(c.textures.size()), 2); // the unlinked normal texture isn't generated
        QVERIFY2(bakeError(c).isEmpty(), qPrintable(bakeError(c)));

        // With the Author's Normal Map node (shipped): linked, with glTF's scale.
        m.normalScale = 0.7f;
        const NodeLibrary withNormalMap(readSource("material_nodes.glsl"));
        QVERIFY(withNormalMap.type("normal_map"));
        const NodeGraph g2 = graphFromGltf(m, withNormalMap);
        const GraphNode *nm = source(g2, out, "normal");
        QVERIFY(nm && nm->type == "normal_map");
        QCOMPARE(nm->values["strength"].toFloat(), 0.7f);
        c = MaterialCompiler::compile(g2, withNormalMap);
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(int(c.textures.size()), 3);
    }

    void gltfAlpha()
    {
        const NodeLibrary lib(readSource("material_nodes.glsl"));
        GltfMaterial m;
        m.baseColorTexture = "C:/m/leaf.png";
        m.baseColorFactor = QVector4D(1, 1, 1, 0.5f);
        m.alphaMode = GltfMaterial::Mask;
        NodeGraph g = graphFromGltf(m, lib);
        int out = 0;
        for (const GraphNode &n : g.nodes)
            if (n.type == "output")
                out = n.id;
        // Texture alpha x 0.5, from the same Image Texture as the color.
        QString port;
        const GraphNode *mul = source(g, out, "alpha");
        QVERIFY(mul && mul->type == "multiply");
        QCOMPARE(mul->values["b"].toFloat(), 0.5f);
        const GraphNode *tex = source(g, mul->id, "a", &port);
        QVERIFY(tex && tex->type == "image_texture" && port == "alpha");
        QCOMPARE(countNodes(g, "image_texture"), 1);
        QVERIFY2(bakeError(MaterialCompiler::compile(g, lib)).isEmpty(), "alpha graph doesn't compile");

        // Blend without a texture: the factor as the value.
        m.baseColorTexture.clear();
        m.alphaMode = GltfMaterial::Blend;
        g = graphFromGltf(m, lib);
        QVERIFY(!g.linkTo(out, "alpha"));
        QCOMPARE(g.node(out)->values["alpha"].toFloat(), 0.5f);
    }
};

QTEST_GUILESS_MAIN(TestMaterial)
#include "tst_material.moc"
