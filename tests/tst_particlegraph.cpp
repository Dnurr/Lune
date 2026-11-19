// Particle graphs: the node library read from particle_nodes.glsl, code generation into
// particles.comp (baked for Vulkan and Direct3D), scene files and undo.
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>
#include <QVector3D>
#include <rhi/qshaderbaker.h>

#include "engine/io/sceneio.h"
#include "engine/material/particlegraph.h"
#include "engine/scene/scene.h"
#include "frontend/core/commands.h"

namespace
{
QString readSource(const QString &name)
{
    QFile f(QStringLiteral(LUNE_SOURCE_DIR "/src/engine/render/shaders/") + name);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// Bakes generated graph code inside the real particles.comp; the error, or empty.
QString bakeError(const ParticleGraph::Compiled &c)
{
    const QString source = ParticleGraph::insertInto(readSource("particles.comp"), c.code);
    if (source.isEmpty())
        return QStringLiteral("no markers");
    QShaderBaker baker;
    baker.setGeneratedShaders({ { QShader::SpirvShader, QShaderVersion(100) }, { QShader::HlslShader, QShaderVersion(50) } });
    baker.setGeneratedShaderVariants({ QShader::StandardShader });
    baker.setSourceString(source.toUtf8(), QShader::ComputeStage);
    return baker.bake().isValid() ? QString() : baker.errorMessage();
}
} // namespace

class TestParticleGraph : public QObject
{
    Q_OBJECT

private slots:
    void shippedLibraryHasNoErrors()
    {
        const NodeLibrary lib(readSource("particle_nodes.glsl"), NodeLibrary::Kind::Particle);
        QVERIFY2(lib.errors().isEmpty(), qPrintable(lib.errors().join('\n')));
        QVERIFY(lib.type("vortex"));
        QVERIFY(lib.type("scene_forces") && lib.type("scene_forces")->builtin);
        QVERIFY(lib.type("object") && lib.type("object")->builtin);
        QCOMPARE(lib.outputType()->title, QStringLiteral("Particle Output"));
        // Material libraries don't get the particle built-ins.
        QVERIFY(!NodeLibrary(QString()).type("object"));
    }

    void particlesCompHasTheMarkers()
    {
        const QString source = readSource("particles.comp");
        QVERIFY(source.contains(ParticleGraph::BeginMarker));
        QVERIFY(source.contains(ParticleGraph::EndMarker));
    }

    void defaultGraphCompiles()
    {
        const NodeLibrary lib(readSource("particle_nodes.glsl"), NodeLibrary::Kind::Particle);
        const ParticleGraph::Compiled c = ParticleGraph::compile(ParticleGraph::defaultGraph(), lib);
        QVERIFY2(c.ok(), qPrintable(c.error));
        QVERIFY(c.code.contains("forceAcceleration(forces[i], p, time)"));
        QVERIFY(c.objects.empty());
        QVERIFY2(bakeError(c).isEmpty(), qPrintable(bakeError(c)));
    }

    void everyNodeChainedCompiles()
    {
        // Scene Forces -> every force node -> Output, with an Object placing the vortex.
        const NodeLibrary lib(readSource("particle_nodes.glsl"), NodeLibrary::Kind::Particle);
        NodeGraph g = ParticleGraph::defaultGraph(); // node 1: Scene Forces, node 2: output
        int previous = 1;
        QString previousPort = "result";
        int vortex = 0;
        for (const char *type : { "gravity", "wind", "attractor", "turbulence", "vortex" }) {
            const int n = g.addNode(type, {});
            QVERIFY(g.connect(previous, previousPort, n, "acceleration"));
            if (QLatin1String(type) == QLatin1String("vortex"))
                vortex = n;
            previous = n;
            previousPort = "result";
        }
        QVERIFY(g.connect(previous, "result", 2, "acceleration"));
        const int object = g.addNode("object", {});
        g.node(object)->values["object"] = 42.0;
        QVERIFY(g.connect(object, "position", vortex, "center"));
        QVERIFY(g.connect(object, "axis", vortex, "axis"));

        const ParticleGraph::Compiled c = ParticleGraph::compile(g, lib);
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(c.objects, std::vector<quint64> { 42 });
        QVERIFY(c.code.contains("forces[s.objects.x + 0u].transform"));
        QVERIFY(c.params.size() > 5); // the forces' settings: values, not code
        QVERIFY(c.code.contains("graphParams["));
        QVERIFY2(bakeError(c).isEmpty(), qPrintable(bakeError(c)));
    }

    void withoutOutputFails()
    {
        const NodeLibrary lib(readSource("particle_nodes.glsl"), NodeLibrary::Kind::Particle);
        NodeGraph g;
        g.addNode("gravity", {});
        const ParticleGraph::Compiled c = ParticleGraph::compile(g, lib);
        QVERIFY(!c.ok());
        QVERIFY(c.code.isEmpty());
    }

    void sceneFileRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("graph.lune");
        Scene scene;
        Entity e;
        e.name = QStringLiteral("Sparks");
        e.emitter = ParticleEmitter {};
        e.emitter->graph = ParticleGraph::defaultGraph();
        const int gravity = e.emitter->graph.addNode("gravity", QPointF(10, 20));
        e.emitter->graph.node(gravity)->values["strength"] = 3.5f;
        scene.createEntity(e);
        Entity plain; // without a graph: nothing saved for it
        plain.name = QStringLiteral("Plain");
        plain.emitter = ParticleEmitter {};
        scene.createEntity(plain);

        QString error;
        QVERIFY2(SceneIO::save(scene, path, {}, &error), qPrintable(error));
        Scene loaded;
        QVERIFY2(SceneIO::load(loaded, path, nullptr, nullptr, &error), qPrintable(error));
        QCOMPARE(loaded.entities()[0].emitter->graph, e.emitter->graph);
        QVERIFY(loaded.entities()[1].emitter->graph.nodes.empty());
    }

    void commandUndoRedo()
    {
        Scene scene;
        Entity e;
        e.emitter = ParticleEmitter {};
        const EntityId id = scene.createEntity(e);
        QUndoStack undo;
        undo.push(new SetEmitterGraphCommand(&scene, id, ParticleGraph::defaultGraph(), "Add particle graph"));
        QCOMPARE(scene.entity(id)->emitter->graph, ParticleGraph::defaultGraph());

        // Value edits with the same key merge into one step.
        NodeGraph a = ParticleGraph::defaultGraph();
        const int n = a.addNode("gravity", {});
        undo.push(new SetEmitterGraphCommand(&scene, id, a, "Add node"));
        NodeGraph b = a;
        b.node(n)->values["strength"] = 1.0f;
        undo.push(new SetEmitterGraphCommand(&scene, id, b, "Edit", "value"));
        NodeGraph c = b;
        c.node(n)->values["strength"] = 2.0f;
        undo.push(new SetEmitterGraphCommand(&scene, id, c, "Edit", "value"));
        QCOMPARE(undo.count(), 3);
        undo.undo();
        QCOMPARE(scene.entity(id)->emitter->graph, a);
        undo.undo();
        undo.undo();
        QVERIFY(scene.entity(id)->emitter->graph.nodes.empty());
        undo.redo();
        QCOMPARE(scene.entity(id)->emitter->graph, ParticleGraph::defaultGraph());
    }
};

QTEST_GUILESS_MAIN(TestParticleGraph)
#include "tst_particlegraph.moc"
