#pragma once

#include <QString>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

struct MeshVertex
{
    QVector3D position;
    QVector3D normal;
    // Texture coordinates, glTF convention: (0, 0) = top-left corner of the image.
    QVector2D uv;
    // Tangent space for normal maps: xyz = tangent (along +u), w = +-1, the handedness:
    // bitangent = cross(normal, tangent.xyz) * w (glTF convention). See computeTangents().
    QVector4D tangent;
};
static_assert(sizeof(MeshVertex) == 12 * sizeof(float), "MeshVertex is uploaded to the GPU as is");

// A range of a mesh's triangles drawn with one material: an entity has one material slot
// per submesh (Entity::materials).
struct Submesh
{
    quint32 firstIndex = 0;
    quint32 indexCount = 0;
    QString name;           // the material name in the source file, if any
    int sourceMaterial = -1; // index of that material in the source file (glTF), -1 = none
};

// Triangle mesh in object space: indexed triangles, counter-clockwise = front face.
// Immutable once built and shared between entities (MeshPtr): the renderer uploads
// each Mesh once, however many entities use it.
struct Mesh
{
    QString name;
    // Where the mesh comes from, so a saved scene can find it again:
    // "primitive:<name>" for built-in meshes, otherwise the absolute path of the imported file.
    QString source;
    std::vector<MeshVertex> vertices;
    std::vector<quint32> indices; // 3 per triangle
    std::vector<Submesh> submeshes; // at least one after finish(), covering every index
    QVector3D boundsMin, boundsMax;
    // Watertight: every edge is shared by exactly two triangles, wound in opposite directions
    // (vertices at the same position count as one, so split normals and UV seams don't matter).
    // Closed meshes write their back faces into the shadow maps. See computeClosed().
    bool closed = false;

    // Call once the vertices and indices are final: bounds, a submesh covering everything
    // if there's none, tangents (unless the file provided them), closed.
    void finish(bool hasTangents = false)
    {
        computeBounds();
        if (submeshes.empty())
            submeshes.push_back({ 0, quint32(indices.size()), QString(), -1 });
        if (!hasTangents)
            computeTangents();
        computeClosed();
    }

    void computeBounds()
    {
        if (vertices.empty()) {
            boundsMin = boundsMax = QVector3D();
            return;
        }
        boundsMin = boundsMax = vertices.front().position;
        for (const MeshVertex &v : vertices) {
            const QVector3D &p = v.position;
            boundsMin = QVector3D(qMin(boundsMin.x(), p.x()), qMin(boundsMin.y(), p.y()), qMin(boundsMin.z(), p.z()));
            boundsMax = QVector3D(qMax(boundsMax.x(), p.x()), qMax(boundsMax.y(), p.y()), qMax(boundsMax.z(), p.z()));
        }
    }

    // Per-vertex tangents from the UVs: the triangles' u directions, summed around each
    // vertex, made perpendicular to the normal; the handedness from the v direction.
    // Where the UVs don't define one (no UVs, degenerate mapping), any direction
    // perpendicular to the normal.
    void computeTangents()
    {
        std::vector<QVector3D> tan(vertices.size()), bitan(vertices.size());
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const quint32 i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
            const MeshVertex &a = vertices[i0], &b = vertices[i1], &c = vertices[i2];
            const QVector3D e1 = b.position - a.position, e2 = c.position - a.position;
            const QVector2D d1 = b.uv - a.uv, d2 = c.uv - a.uv;
            const float det = d1.x() * d2.y() - d2.x() * d1.y();
            if (std::abs(det) < 1e-12f)
                continue;
            const float r = 1.0f / det;
            // Weighted by the triangle's UV area (r carries its sign): bigger triangles count more.
            const QVector3D sdir = (e1 * d2.y() - e2 * d1.y()) * r;
            const QVector3D tdir = (e2 * d1.x() - e1 * d2.x()) * r;
            for (quint32 i : { i0, i1, i2 }) {
                tan[i] += sdir;
                bitan[i] += tdir;
            }
        }
        for (size_t i = 0; i < vertices.size(); ++i) {
            const QVector3D n = vertices[i].normal.normalized();
            QVector3D t = tan[i] - n * QVector3D::dotProduct(n, tan[i]); // Gram-Schmidt
            // (Divided by hand: QVector3D::normalize() leaves very short vectors as they are.)
            if (t.lengthSquared() < 1e-16f * std::max(1e-30f, tan[i].lengthSquared()) || t.lengthSquared() < 1e-30f) {
                // Any perpendicular: from the axis least aligned with the normal.
                const QVector3D axis = std::abs(n.x()) < 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
                t = QVector3D::crossProduct(axis, n);
            }
            t /= t.length();
            const float w = QVector3D::dotProduct(QVector3D::crossProduct(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
            vertices[i].tangent = QVector4D(t, w);
        }
    }

    // Sets `closed`. Call after building the mesh (positions and indices final).
    void computeClosed()
    {
        // Weld positions closer than ~1e-5 of the mesh size: grid cells of 21 bits per axis.
        const QVector3D size = boundsMax - boundsMin;
        const float extent = qMax(1e-6f, qMax(size.x(), qMax(size.y(), size.z())));
        const float scale = 1e5f / extent; // cell coordinates 0..1e5 (< 2^21)
        auto cell = [&](float v, float min) { return quint64(std::llround((v - min) * scale)) & 0x1fffffu; };
        std::unordered_map<quint64, quint32> ids;
        ids.reserve(vertices.size());
        std::vector<quint32> weld(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) {
            const QVector3D &p = vertices[i].position;
            const quint64 key = cell(p.x(), boundsMin.x()) | cell(p.y(), boundsMin.y()) << 21
                              | cell(p.z(), boundsMin.z()) << 42;
            weld[i] = ids.emplace(key, quint32(ids.size())).first->second;
        }
        // Directed edges: in a closed, consistently wound mesh each one appears once, and so
        // does its reverse. Sorted, so both are cheap to check (millions of triangles).
        auto edgeKey = [](quint32 a, quint32 b) { return (quint64(a) << 32) | b; };
        std::vector<quint64> edges;
        edges.reserve(indices.size());
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const quint32 v[3] = { weld[indices[t]], weld[indices[t + 1]], weld[indices[t + 2]] };
            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0])
                continue; // degenerate (e.g. at a sphere's poles)
            for (int k = 0; k < 3; ++k)
                edges.push_back(edgeKey(v[k], v[(k + 1) % 3]));
        }
        std::sort(edges.begin(), edges.end());
        closed = !edges.empty();
        for (size_t i = 0; closed && i < edges.size(); ++i) {
            const quint64 e = edges[i];
            if (i + 1 < edges.size() && edges[i + 1] == e)
                closed = false; // the same edge twice
            else
                closed = std::binary_search(edges.begin(), edges.end(), edgeKey(quint32(e & 0xffffffffu), quint32(e >> 32)));
        }
    }
};

using MeshPtr = std::shared_ptr<const Mesh>;
