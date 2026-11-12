// Scene files: round trip, mesh references, robustness against bad or newer files.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "engine/io/meshimport.h"
#include "engine/io/sceneio.h"
#include "engine/scene/primitives.h"
#include "engine/scene/scene.h"

namespace
{
void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

const QByteArray kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

bool fuzzyEqual(const QVector3D &a, const QVector3D &b)
{
    return (a - b).length() < 1e-5f;
}
} // namespace

class TestSceneIO : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("scene.lune");

        Scene scene;
        Entity cube;
        cube.name = QStringLiteral("Cube");
        cube.mesh = Primitives::cube();
        cube.transform.position = QVector3D(1.5f, 0.5f, -2.0f);
        cube.transform.rotation = QVector3D(10, 20, 30);
        cube.transform.scale = QVector3D(1, 2, 3);
        cube.displayColor = QColor("#336699");
        cube.visible = false;
        scene.createEntity(cube);
        Entity sphere;
        sphere.name = QStringLiteral("Sphere");
        sphere.mesh = Primitives::sphere();
        scene.createEntity(sphere);
        scene.createEntity(QStringLiteral("Empty"));

        QJsonObject editor;
        editor["answer"] = 42;
        QString error;
        QVERIFY2(SceneIO::save(scene, path, editor, &error), qPrintable(error));

        Scene loaded;
        QJsonObject loadedEditor;
        QStringList warnings;
        QVERIFY2(SceneIO::load(loaded, path, &loadedEditor, &warnings, &error), qPrintable(error));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QCOMPARE(loadedEditor["answer"].toInt(), 42);

        QCOMPARE(loaded.entities().size(), size_t(3));
        const Entity &c = loaded.entities()[0];
        QCOMPARE(c.id, EntityId(1));
        QCOMPARE(c.name, QStringLiteral("Cube"));
        QCOMPARE(c.mesh, Primitives::cube()); // built-in meshes are shared, not copied
        QVERIFY(fuzzyEqual(c.transform.position, cube.transform.position));
        QVERIFY(fuzzyEqual(c.transform.rotation, cube.transform.rotation));
        QVERIFY(fuzzyEqual(c.transform.scale, cube.transform.scale));
        QCOMPARE(c.displayColor, QColor("#336699"));
        QVERIFY(!c.visible);
        QCOMPARE(loaded.entities()[1].mesh, Primitives::sphere());
        QVERIFY(!loaded.entities()[2].mesh);
    }

    void meshFilesUseRelativePathsAndLoadOnce()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("project/meshes/tri.obj"), kTriangleObj);
        const MeshPtr mesh = importMesh(dir.filePath("project/meshes/tri.obj"));
        QVERIFY(mesh);

        Scene scene;
        for (int i = 0; i < 2; ++i) {
            Entity e;
            e.name = QStringLiteral("Tri");
            e.mesh = mesh;
            scene.createEntity(e);
        }
        const QString path = dir.filePath("project/scene.lune");
        QVERIFY(SceneIO::save(scene, path, {}));
        const QJsonArray entities = readJson(path)["entities"].toArray();
        QCOMPARE(entities[0].toObject()["mesh"].toString(), QStringLiteral("meshes/tri.obj"));

        // Move the whole project folder: the relative path still works.
        QVERIFY(QDir(dir.path()).rename("project", "moved"));
        Scene loaded;
        QStringList warnings;
        QVERIFY(SceneIO::load(loaded, dir.filePath("moved/scene.lune"), nullptr, &warnings));
        QVERIFY(warnings.isEmpty());
        QVERIFY(loaded.entities()[0].mesh);
        QCOMPARE(loaded.entities()[0].mesh, loaded.entities()[1].mesh); // one load, shared
        QCOMPARE(loaded.entities()[0].mesh->indices.size(), size_t(3));
    }

    void missingMeshIsAWarning()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("scene.lune");
        writeFile(path, R"({"format":"lune-scene","version":1,"entities":[
            {"id":1,"name":"A","mesh":"gone.obj","components":{}},
            {"id":2,"name":"B","mesh":"gone.obj","components":{}}]})");
        Scene scene;
        QStringList warnings;
        QVERIFY(SceneIO::load(scene, path, nullptr, &warnings));
        QCOMPARE(scene.entities().size(), size_t(2));
        QVERIFY(!scene.entities()[0].mesh);
        QCOMPARE(warnings.size(), 1); // reported once per file, not per entity
    }

    void badFilesLeaveTheSceneUntouched_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("not json") << QByteArray("this is not json");
        QTest::newRow("wrong format") << QByteArray(R"({"format":"something-else","version":1})");
        QTest::newRow("newer version") << QByteArray(R"({"format":"lune-scene","version":999,"entities":[]})");
    }

    void badFilesLeaveTheSceneUntouched()
    {
        QFETCH(QByteArray, content);
        QTemporaryDir dir;
        const QString path = dir.filePath("bad.lune");
        writeFile(path, content);

        Scene scene;
        scene.createEntity(QStringLiteral("Keep me"));
        QString error;
        QVERIFY(!SceneIO::load(scene, path, nullptr, nullptr, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(scene.entities().size(), size_t(1));
        QCOMPARE(scene.entities()[0].name, QStringLiteral("Keep me"));
    }

    void unknownFieldsAreIgnoredAndBadValuesReported()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("scene.lune");
        writeFile(path, R"({"format":"lune-scene","version":1,"futureField":true,"entities":[
            {"id":1,"name":"A","mesh":"primitive:cube","components":{
                "Transform":{"position":[1,2,3],"scale":"big","wobble":7},
                "FutureComponent":{"x":1}}}]})");
        Scene scene;
        QStringList warnings;
        QVERIFY(SceneIO::load(scene, path, nullptr, &warnings));
        const Entity &e = scene.entities()[0];
        QCOMPARE(e.transform.position, QVector3D(1, 2, 3));
        QCOMPARE(e.transform.scale, QVector3D(1, 1, 1)); // invalid: default kept
        QCOMPARE(warnings.size(), 1);                    // the bad "scale"
    }

    void duplicateIdsGetFreshOnes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("scene.lune");
        writeFile(path, R"({"format":"lune-scene","version":1,"entities":[
            {"id":5,"name":"A"},{"id":5,"name":"B"},{"name":"C"}]})");
        Scene scene;
        QVERIFY(SceneIO::load(scene, path));
        QCOMPARE(scene.entities()[0].id, EntityId(5));
        QCOMPARE(scene.entities()[1].id, EntityId(6));
        QCOMPARE(scene.entities()[2].id, EntityId(7));
        QCOMPARE(scene.createEntity(QStringLiteral("D")), EntityId(8));
    }

    void numbersAreWrittenTidy()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("scene.lune");
        Scene scene;
        Transform t;
        t.position = QVector3D(0.1f, 0, 0); // 0.1f is 0.100000001490116... as a double
        scene.createEntity(QStringLiteral("A"), t);
        QVERIFY(SceneIO::save(scene, path, {}));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("0.1"));
        QVERIFY(!text.contains("0.100000001"));
    }
};

QTEST_GUILESS_MAIN(TestSceneIO)
#include "tst_sceneio.moc"
