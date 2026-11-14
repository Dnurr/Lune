// Mesh loaders: OBJ and glTF, built from small files written by the test itself.
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include "engine/io/meshimport.h"
#include "engine/scene/primitives.h"

namespace
{
QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(content);
    return path;
}

bool near(const QVector3D &a, const QVector3D &b)
{
    return (a - b).length() < 1e-5f;
}

// Minimal glTF: one triangle (positions only, no indices) in a base64 buffer,
// placed by a node with a translation.
QByteArray triangleGltf(const QVector3D &translation)
{
    const float positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    const QByteArray buffer(reinterpret_cast<const char *>(positions), sizeof(positions));
    const QString json = QStringLiteral(R"({
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": 36, "uri": "data:application/octet-stream;base64,%1"}],
        "bufferViews": [{"buffer": 0, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0, 0, 0], "max": [1, 1, 0]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "nodes": [{"mesh": 0, "translation": [%2, %3, %4]}],
        "scenes": [{"nodes": [0]}],
        "scene": 0
    })")
                             .arg(QString::fromLatin1(buffer.toBase64()))
                             .arg(translation.x())
                             .arg(translation.y())
                             .arg(translation.z());
    return json.toUtf8();
}
} // namespace

class TestMeshImport : public QObject
{
    Q_OBJECT

private slots:
    void objTriangleWithNormals()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 2\nf 1//1 2//1 3//1\n");
        QString error;
        const MeshPtr m = importMesh(path, &error);
        QVERIFY2(m, qPrintable(error));
        QCOMPARE(m->name, QStringLiteral("tri"));
        QCOMPARE(m->source, QFileInfo(path).absoluteFilePath());
        QCOMPARE(m->indices.size(), size_t(3));
        QVERIFY(near(m->vertices[0].normal, QVector3D(0, 0, 1))); // normalized
        QVERIFY(near(m->boundsMax, QVector3D(1, 1, 0)));
    }

    void objQuadIsTriangulatedAndSharesVertices()
    {
        QTemporaryDir dir;
        // Negative indices count back from the last vertex/normal read.
        const QString path = writeFile(dir, "quad.obj",
                                       "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\n"
                                       "f -4//-1 -3//-1 -2//-1 -1//-1\n");
        const MeshPtr m = importMesh(path);
        QVERIFY(m);
        QCOMPARE(m->indices.size(), size_t(6)); // 2 triangles
        QCOMPARE(m->vertices.size(), size_t(4)); // corners with the same normal are shared
    }

    void objWithoutNormalsGetsFlatNormals()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "flat.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        const MeshPtr m = importMesh(path);
        QVERIFY(m);
        for (const MeshVertex &v : m->vertices)
            QVERIFY(near(v.normal, QVector3D(0, 0, 1))); // counter-clockwise in XY faces +Z
    }

    void objBadIndexFails()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "bad.obj", "v 0 0 0\nf 1 2 3\n");
        QString error;
        QVERIFY(!importMesh(path, &error));
        QVERIFY(error.contains(QStringLiteral("Line 2")));
    }

    void objWithoutFacesFails()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "empty.obj", "# nothing\nv 0 0 0\n");
        QVERIFY(!importMesh(path));
    }

    void gltfTriangleWithNodeTransform()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "tri.gltf", triangleGltf(QVector3D(0, 2, 0)));
        QString error;
        const MeshPtr m = importMesh(path, &error);
        QVERIFY2(m, qPrintable(error));
        QCOMPARE(m->name, QStringLiteral("tri"));
        QCOMPARE(m->vertices.size(), size_t(3));
        QCOMPARE(m->indices.size(), size_t(3));
        QVERIFY(near(m->vertices[1].position, QVector3D(1, 2, 0))); // node translation baked in
        for (const MeshVertex &v : m->vertices)
            QVERIFY(near(v.normal, QVector3D(0, 0, 1))); // computed: the file has no normals
    }

    void gltfInvalidFails()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "broken.gltf", "{ not json");
        QString error;
        QVERIFY(!importMesh(path, &error));
        QVERIFY(!error.isEmpty());
    }

    void unicodePath()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("modello_città.gltf"), triangleGltf(QVector3D()));
        QString error;
        QVERIFY2(importMesh(path, &error), qPrintable(error));
    }

    void closedMeshes()
    {
        // Primitives: the cube's faces have their own vertices (split normals), the sphere
        // has a seam and degenerate triangles at the poles; both are still closed.
        QVERIFY(Primitives::cube()->closed);
        QVERIFY(Primitives::sphere()->closed);
        QVERIFY(!Primitives::plane()->closed);

        QTemporaryDir dir;
        const QByteArray cube = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
                                "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n";
        const MeshPtr closed = importMesh(writeFile(dir, "cube.obj", cube));
        QVERIFY(closed);
        QVERIFY(closed->closed);

        // A missing face leaves a hole.
        const MeshPtr open = importMesh(writeFile(dir, "box.obj", cube.left(cube.lastIndexOf("f 4"))));
        QVERIFY(open);
        QVERIFY(!open->closed);

        // One face wound the wrong way: not consistently oriented, treated as open.
        QByteArray flipped = cube;
        flipped.replace("f 5 6 7 8", "f 8 7 6 5");
        const MeshPtr bad = importMesh(writeFile(dir, "flipped.obj", flipped));
        QVERIFY(bad);
        QVERIFY(!bad->closed);
    }

    void unsupportedExtension()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "mesh.fbx", "whatever");
        QString error;
        QVERIFY(!importMesh(path, &error));
        QVERIFY(error.contains(QStringLiteral(".fbx")));
    }
};

QTEST_GUILESS_MAIN(TestMeshImport)
#include "tst_meshimport.moc"
