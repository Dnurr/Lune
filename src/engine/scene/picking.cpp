#include "picking.h"

#include <limits>

#include "scene.h"

namespace
{
// Slab test: does the ray hit the box before `maxT`?
bool hitsBox(const QVector3D &o, const QVector3D &d, const QVector3D &bmin, const QVector3D &bmax, float maxT)
{
    float t0 = 0.0f, t1 = maxT;
    for (int i = 0; i < 3; ++i) {
        if (qAbs(d[i]) < 1e-12f) {
            if (o[i] < bmin[i] || o[i] > bmax[i])
                return false;
            continue;
        }
        float a = (bmin[i] - o[i]) / d[i];
        float b = (bmax[i] - o[i]) / d[i];
        if (a > b)
            std::swap(a, b);
        t0 = qMax(t0, a);
        t1 = qMin(t1, b);
        if (t0 > t1)
            return false;
    }
    return true;
}

// Moller-Trumbore, both faces. Returns t or a negative value.
float hitTriangle(const QVector3D &o, const QVector3D &d, const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    const QVector3D e1 = b - a, e2 = c - a;
    const QVector3D p = QVector3D::crossProduct(d, e2);
    const float det = QVector3D::dotProduct(e1, p);
    if (qAbs(det) < 1e-12f)
        return -1.0f;
    const float inv = 1.0f / det;
    const QVector3D s = o - a;
    const float u = QVector3D::dotProduct(s, p) * inv;
    if (u < 0.0f || u > 1.0f)
        return -1.0f;
    const QVector3D q = QVector3D::crossProduct(s, e1);
    const float v = QVector3D::dotProduct(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f)
        return -1.0f;
    return QVector3D::dotProduct(e2, q) * inv;
}
} // namespace

EntityId pickEntity(const Scene &scene, const QVector3D &origin, const QVector3D &direction, float *t)
{
    EntityId best = InvalidEntity;
    float bestT = std::numeric_limits<float>::max();

    for (const Entity &e : scene.entities()) {
        if (!e.mesh || !e.visible)
            continue;
        bool invertible = false;
        const QMatrix4x4 toObject = e.transform.matrix().inverted(&invertible);
        if (!invertible)
            continue;
        // Affine map: the ray keeps the same parameter t in object space.
        const QVector3D o = toObject.map(origin);
        const QVector3D d = toObject.mapVector(direction);
        if (!hitsBox(o, d, e.mesh->boundsMin, e.mesh->boundsMax, bestT))
            continue;

        const auto &v = e.mesh->vertices;
        const auto &idx = e.mesh->indices;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const float hit = hitTriangle(o, d, v[idx[i]].position, v[idx[i + 1]].position, v[idx[i + 2]].position);
            if (hit > 0.0f && hit < bestT) {
                bestT = hit;
                best = e.id;
            }
        }
    }
    if (t && best != InvalidEntity)
        *t = bestT;
    return best;
}
