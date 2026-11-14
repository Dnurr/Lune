#include "primitives.h"

#include <QtMath>

namespace
{
// Adds a quad with corners c +/- u +/- v; u x v must point along the normal n
// so that the two triangles are counter-clockwise seen from outside.
void addQuad(Mesh &m, const QVector3D &c, const QVector3D &u, const QVector3D &v, const QVector3D &n)
{
    const quint32 base = quint32(m.vertices.size());
    m.vertices.push_back({ c - u - v, n });
    m.vertices.push_back({ c + u - v, n });
    m.vertices.push_back({ c + u + v, n });
    m.vertices.push_back({ c - u + v, n });
    m.indices.insert(m.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}

MeshPtr makeCube()
{
    auto m = std::make_shared<Mesh>();
    m->name = QStringLiteral("Cube");
    m->source = QStringLiteral("primitive:cube");
    const QVector3D X(0.5f, 0, 0), Y(0, 0.5f, 0), Z(0, 0, 0.5f);
    addQuad(*m, X, Y, Z, X.normalized());
    addQuad(*m, -X, Z, Y, -X.normalized());
    addQuad(*m, Y, Z, X, Y.normalized());
    addQuad(*m, -Y, X, Z, -Y.normalized());
    addQuad(*m, Z, X, Y, Z.normalized());
    addQuad(*m, -Z, Y, X, -Z.normalized());
    m->computeBounds();
    m->computeClosed();
    return m;
}

MeshPtr makeSphere()
{
    auto m = std::make_shared<Mesh>();
    m->name = QStringLiteral("Sphere");
    m->source = QStringLiteral("primitive:sphere");
    const int segments = 48;
    const int rings = 24;
    for (int i = 0; i <= rings; ++i) {
        const float phi = float(M_PI) * i / rings; // 0 = top
        for (int j = 0; j <= segments; ++j) {
            const float theta = 2.0f * float(M_PI) * j / segments;
            const QVector3D n(qSin(phi) * qCos(theta), qCos(phi), qSin(phi) * qSin(theta));
            m->vertices.push_back({ n * 0.5f, n });
        }
    }
    const quint32 row = segments + 1;
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const quint32 a = quint32(i) * row + quint32(j);
            const quint32 b = a + row; // one ring below
            m->indices.insert(m->indices.end(), { a, a + 1, b, a + 1, b + 1, b });
        }
    }
    m->computeBounds();
    m->computeClosed();
    return m;
}

MeshPtr makePlane()
{
    auto m = std::make_shared<Mesh>();
    m->name = QStringLiteral("Plane");
    m->source = QStringLiteral("primitive:plane");
    addQuad(*m, QVector3D(), QVector3D(0, 0, 0.5f), QVector3D(0.5f, 0, 0), QVector3D(0, 1, 0));
    m->computeBounds();
    m->computeClosed();
    return m;
}
} // namespace

namespace Primitives
{
MeshPtr cube()
{
    static const MeshPtr m = makeCube();
    return m;
}

MeshPtr sphere()
{
    static const MeshPtr m = makeSphere();
    return m;
}

MeshPtr plane()
{
    static const MeshPtr m = makePlane();
    return m;
}

MeshPtr fromSource(const QString &source)
{
    for (const MeshPtr &m : { cube(), sphere(), plane() }) {
        if (m->source == source)
            return m;
    }
    return nullptr;
}
} // namespace Primitives
