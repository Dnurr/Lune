#pragma once

#include <QString>
#include <QVector3D>
#include <QtGlobal>
#include <memory>
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
    std::vector<MeshVertex> vertices;
    std::vector<quint32> indices; // 3 per triangle
    QVector3D boundsMin, boundsMax;

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
};

using MeshPtr = std::shared_ptr<const Mesh>;
