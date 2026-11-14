#pragma once

#include <QString>
#include <QVector3D>
#include <QtGlobal>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

struct MeshVertex
{
    QVector3D position;
    QVector3D normal;
};
static_assert(sizeof(MeshVertex) == 6 * sizeof(float), "MeshVertex is uploaded to the GPU as is");

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
    QVector3D boundsMin, boundsMax;
    // Watertight: every edge is shared by exactly two triangles, wound in opposite directions
    // (vertices at the same position count as one, so split normals and UV seams don't matter).
    // Closed meshes write their back faces into the shadow maps. See computeClosed().
    bool closed = false;

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

    // Sets `closed`. Call after building the mesh (positions and indices final).
    void computeClosed()
    {
        // Weld positions closer than ~1e-5 of the mesh size.
        const float extent = qMax(1e-6f, (boundsMax - boundsMin).length());
        const float scale = 1e5f / extent;
        std::map<std::tuple<long long, long long, long long>, quint32> ids;
        std::vector<quint32> weld(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) {
            const QVector3D &p = vertices[i].position;
            const auto key = std::make_tuple(std::llround(p.x() * scale), std::llround(p.y() * scale),
                                             std::llround(p.z() * scale));
            weld[i] = ids.emplace(key, quint32(ids.size())).first->second;
        }
        // Directed edges: in a closed, consistently wound mesh each one appears once, and so
        // does its reverse.
        std::unordered_map<quint64, int> edges;
        auto edgeKey = [](quint32 a, quint32 b) { return (quint64(a) << 32) | b; };
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const quint32 v[3] = { weld[indices[t]], weld[indices[t + 1]], weld[indices[t + 2]] };
            if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0])
                continue; // degenerate (e.g. at a sphere's poles)
            for (int k = 0; k < 3; ++k)
                ++edges[edgeKey(v[k], v[(k + 1) % 3])];
        }
        closed = !edges.empty();
        for (const auto &[key, count] : edges) {
            const auto reverse = edges.find(edgeKey(quint32(key & 0xffffffffu), quint32(key >> 32)));
            if (count != 1 || reverse == edges.end() || reverse->second != 1) {
                closed = false;
                break;
            }
        }
    }
};

using MeshPtr = std::shared_ptr<const Mesh>;
