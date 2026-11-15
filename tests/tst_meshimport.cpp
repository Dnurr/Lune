// Mesh loaders: OBJ and glTF, built from small files written by the test itself.
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include "engine/io/gltfloader.h"
#include "engine/io/imageloader.h"
#include "engine/io/meshimport.h"
#include "engine/scene/primitives.h"

#include <QBuffer>
#include <QImage>

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
// Every vertex: UVs set, tangent of unit length, perpendicular to the normal, w = +-1;
// the submeshes cover the indices exactly once.
void checkSurfaceData(const Mesh &m)
{
    for (const MeshVertex &v : m.vertices) {
        const QVector3D t = v.tangent.toVector3D();
        QVERIFY(qAbs(t.length() - 1.0f) < 1e-4f);
        QVERIFY(qAbs(QVector3D::dotProduct(t, v.normal.normalized())) < 1e-3f);
        QVERIFY(qAbs(qAbs(v.tangent.w()) - 1.0f) < 1e-6f);
    }
    quint32 next = 0;
    for (const Submesh &s : m.submeshes) {
        QCOMPARE(s.firstIndex, next);
        next += s.indexCount;
    }
    QCOMPARE(next, quint32(m.indices.size()));
}

// glTF with two triangles in two primitives (two materials), UVs on both, tangents on
// the first only, and an image stored in the buffer.
QByteArray twoMaterialGltf(const QByteArray &png)
{
    // Per primitive: 3 positions, 3 normals, 3 uvs (+ 3 tangents for the first).
    const float p0[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    const float p1[9] = { 2, 0, 0, 3, 0, 0, 2, 1, 0 };
    const float n[9] = { 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    const float uv[6] = { 0, 1, 1, 1, 0, 0 };
    const float tan[12] = { 1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1 };
    QByteArray buffer;
    auto put = [&buffer](const void *data, int size) {
        const int offset = int(buffer.size());
        buffer.append(static_cast<const char *>(data), size);
        return offset;
    };
    const int o[7] = { put(p0, 36), put(n, 36), put(uv, 24), put(tan, 48), put(p1, 36), put(n, 36), put(uv, 24) };
    const int imageOffset = put(png.constData(), int(png.size()));
    while (buffer.size() % 4)
        buffer.append('\0');
    const int sizes[7] = { 36, 36, 24, 48, 36, 36, 24 };
    QStringList views;
    for (int i = 0; i < 7; ++i)
        views << QStringLiteral(R"({"buffer": 0, "byteOffset": %1, "byteLength": %2})").arg(o[i]).arg(sizes[i]);
    views << QStringLiteral(R"({"buffer": 0, "byteOffset": %1, "byteLength": %2})").arg(imageOffset).arg(png.size());
    const QString json = QStringLiteral(R"({
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": %1, "uri": "data:application/octet-stream;base64,%2"}],
        "bufferViews": [%3],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
            {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3", "min": [2, 0, 0], "max": [3, 1, 0]},
            {"bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 6, "componentType": 5126, "count": 3, "type": "VEC2"}],
        "images": [{"bufferView": 7, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "materials": [
            {"name": "Painted", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0}},
            {"name": "Steel", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.5, 0.5, 1], "roughnessFactor": 0.3}}],
        "meshes": [{"primitives": [
            {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3}, "material": 1},
            {"attributes": {"POSITION": 4, "NORMAL": 5, "TEXCOORD_0": 6}, "material": 0}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0
    })")
                             .arg(buffer.size())
                             .arg(QString::fromLatin1(buffer.toBase64()))
                             .arg(views.join(", "));
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

    void primitivesHaveUvsAndTangents()
    {
        for (const MeshPtr &m : { Primitives::cube(), Primitives::sphere(), Primitives::plane() }) {
            QCOMPARE(int(m->submeshes.size()), 1);
            checkSurfaceData(*m);
            for (const MeshVertex &v : m->vertices)
                QVERIFY(v.uv.x() >= 0 && v.uv.x() <= 1 && v.uv.y() >= 0 && v.uv.y() <= 1);
        }
        // Cube sides are upright: on the +Z face, uv (0, 0) (the image's top-left) is the
        // top-left corner seen from the front.
        bool found = false;
        for (const MeshVertex &v : Primitives::cube()->vertices) {
            if (near(v.normal, QVector3D(0, 0, 1)) && v.uv == QVector2D(0, 0)) {
                QVERIFY(near(v.position, QVector3D(-0.5f, 0.5f, 0.5f)));
                // u grows to the right: the tangent points along +X.
                QVERIFY(near(v.tangent.toVector3D(), QVector3D(1, 0, 0)));
                found = true;
            }
        }
        QVERIFY(found);
    }

    void objUvsAndMaterials()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "two.obj",
                                       "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
                                       "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\n"
                                       "usemtl red\nf 1/1 2/2 3/3\n"
                                       "usemtl blue\nf 2/2 4/4 3/3\n"
                                       "usemtl red\nf 1/1 3/3 2/2\n");
        QString error;
        const MeshPtr m = importMesh(path, &error);
        QVERIFY2(m, qPrintable(error));
        QCOMPARE(int(m->submeshes.size()), 2);
        QCOMPARE(m->submeshes[0].name, QStringLiteral("red"));
        QCOMPARE(m->submeshes[0].indexCount, 6u);
        QCOMPARE(m->submeshes[1].name, QStringLiteral("blue"));
        QCOMPARE(m->submeshes[1].indexCount, 3u);
        checkSurfaceData(*m);
        // OBJ's v goes up; Lune's down: vertex (0, 1, 0) with vt (0, 1) gets uv (0, 0).
        for (const MeshVertex &v : m->vertices)
            if (near(v.position, QVector3D(0, 1, 0)))
                QCOMPARE(v.uv, QVector2D(0, 0));
    }

    void gltfUvsTangentsMaterialsAndImages()
    {
        QImage red(2, 2, QImage::Format_RGBA8888);
        red.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        red.save(&buffer, "PNG");

        QTemporaryDir dir;
        const QString path = writeFile(dir, "two.gltf", twoMaterialGltf(png));
        QString error;
        const GltfModel model = loadGltfModel(path, &error);
        QVERIFY2(model.mesh, qPrintable(error));
        const Mesh &m = *model.mesh;
        // One submesh per material, in material order.
        QCOMPARE(int(m.submeshes.size()), 2);
        QCOMPARE(m.submeshes[0].name, QStringLiteral("Painted"));
        QCOMPARE(m.submeshes[0].sourceMaterial, 0);
        QCOMPARE(m.submeshes[1].name, QStringLiteral("Steel"));
        checkSurfaceData(m);
        // Tangents: missing on one primitive, so computed for all (from the UVs: +X here).
        for (const MeshVertex &v : m.vertices)
            QVERIFY(near(v.tangent.toVector3D(), QVector3D(1, 0, 0)));
        QCOMPARE(m.vertices[1].uv, QVector2D(1, 1));

        QCOMPARE(int(model.materials.size()), 2);
        QCOMPARE(model.materials[0].metallicFactor, 0.0f);
        QVERIFY(model.materials[0].baseColorTexture.endsWith("two.gltf#image0"));
        QCOMPARE(model.materials[1].roughnessFactor, 0.3f);
        QVERIFY(model.materials[1].baseColorTexture.isEmpty());

        // The image inside the glTF loads.
        const QImage loaded = loadImage(model.materials[0].baseColorTexture, &error);
        QVERIFY2(!loaded.isNull(), qPrintable(error));
        QCOMPARE(loaded.size(), QSize(2, 2));
        QCOMPARE(loaded.format(), QImage::Format_RGBA8888);
        QCOMPARE(QColor(loaded.pixel(0, 0)), QColor(Qt::red));

        // importModel: a material per slot.
        const ImportedModel imported = importModel(path, &error);
        QCOMPARE(int(imported.materials.size()), 2);
        QCOMPARE(imported.materials[1].name, QStringLiteral("Steel"));

        QVERIFY(loadImage(dir.filePath("missing.png"), &error).isNull());
        QVERIFY(!error.isEmpty());
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
