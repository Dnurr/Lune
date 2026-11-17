#include "lighticons.h"

#include <QColor>
#include <QtMath>
#include <cmath>

#include "engine/scene/scene.h"
#include "overlaydraw.h"

namespace
{
const QColor kRing("#C9CCD6");
const QColor kRingSelected("#8FB0F2"); // palette primary, like the selection outline
constexpr float kDisc = 4.5f;
constexpr float kRingRadius = 7.0f;
constexpr float kLine = 1.5f;
constexpr float kDirectionPixels = 60.0f; // on-screen length of a directional light's line

constexpr float kCameraPixels = 40.0f; // on-screen depth of a camera's frustum, roughly

QColor dimmed(const QColor &c)
{
    QColor d = c;
    d.setAlphaF(0.45f);
    return d;
}

// A camera's frustum as screen segments: eye to the four corners, the frame, and the up
// triangle. Segments with an end behind the viewer are left out.
std::vector<std::pair<QPointF, QPointF>> cameraSegments(const Entity &e, const OrbitCamera &viewer, const QSizeF &viewSize,
                                                        float aspect)
{
    const QQuaternion q = e.transform.rotationQuaternion();
    const QVector3D eye = e.transform.position;
    const QVector3D f = q.rotatedVector(QVector3D(0, 0, -1));
    const QVector3D u = q.rotatedVector(QVector3D(0, 1, 0));
    const QVector3D r = q.rotatedVector(QVector3D(1, 0, 0));
    // A constant size on screen, like the light icons.
    const float d = viewer.worldPerPixel(qMax(0.01f, (eye - viewer.eye()).length()), float(viewSize.height())) * kCameraPixels;
    const float halfH = d * std::tan(qDegreesToRadians(e.camera->fieldOfView) * 0.5f);
    const float halfW = halfH * aspect;
    const QVector3D center = eye + f * d;
    const QVector3D corners[4] = { center - r * halfW + u * halfH, center + r * halfW + u * halfH,
                                   center + r * halfW - u * halfH, center - r * halfW - u * halfH };
    const QVector3D up[3] = { center - r * (halfW * 0.5f) + u * (halfH * 1.15f), center + r * (halfW * 0.5f) + u * (halfH * 1.15f),
                              center + u * (halfH * 1.7f) };

    std::vector<std::pair<QPointF, QPointF>> out;
    auto line = [&](const QVector3D &a, const QVector3D &b) {
        bool fa = false, fb = false;
        const QPointF pa = viewer.project(a, viewSize, &fa), pb = viewer.project(b, viewSize, &fb);
        if (fa && fb)
            out.emplace_back(pa, pb);
    };
    for (int i = 0; i < 4; ++i) {
        line(eye, corners[i]);
        line(corners[i], corners[(i + 1) % 4]);
    }
    for (int i = 0; i < 3; ++i)
        line(up[i], up[(i + 1) % 3]);
    return out;
}
} // namespace

namespace LightIcons
{
void append(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, EntityId selected, float cameraAspect,
            EntityId hiddenCamera, std::vector<OverlayVertex> &out)
{
    using namespace OverlayDraw;
    for (const Entity &e : scene.entities()) {
        if (e.camera && e.id != hiddenCamera) {
            QColor col = e.id == selected ? kRingSelected : kRing;
            if (!e.camera->enabled)
                col = dimmed(col);
            for (const auto &[a, b] : cameraSegments(e, camera, viewSize, cameraAspect))
                segment(out, a, b, kLine, col);
        }
    }
    for (const Entity &e : scene.entities()) {
        if (!e.light)
            continue;
        bool inFront = false;
        const QPointF c = camera.project(e.transform.position, viewSize, &inFront);
        if (!inFront)
            continue;

        const bool enabled = e.light->enabled;
        QColor ring = e.id == selected ? kRingSelected : kRing;
        QColor fill = e.light->color;
        if (!enabled) {
            ring = dimmed(ring);
            fill = dimmed(fill);
        }

        if (e.light->type == Light::Directional) {
            // Line towards where the light goes, with a fixed length on screen.
            const QVector3D dir = Light::direction(e.transform);
            bool tipInFront = false;
            const QPointF far = camera.project(e.transform.position + dir, viewSize, &tipInFront);
            QPointF d = far - c;
            const double len = std::hypot(d.x(), d.y());
            if (tipInFront && len > 1e-3) {
                d /= len;
                const QPointF start = c + d * (kRingRadius + 3.0);
                const QPointF end = c + d * kDirectionPixels;
                segment(out, start, end, kLine, ring);
                const QPointF n(-d.y(), d.x());
                triangle(out, end + d * 7.0, end + n * 4.0, end - n * 4.0, ring);
            }
        } else {
            for (int i = 0; i < 8; ++i) {
                const double a = M_PI * 2.0 * i / 8.0;
                const QPointF r(std::cos(a), std::sin(a));
                segment(out, c + r * (kRingRadius + 3.0), c + r * (kRingRadius + 6.5), kLine, ring);
            }
        }
        OverlayDraw::ring(out, c, kRingRadius, kLine, ring);
        disc(out, c, kDisc, fill);
    }
}

EntityId hitTest(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, const QPointF &point,
                 float cameraAspect, EntityId hiddenCamera)
{
    EntityId best = InvalidEntity;
    double bestDistance = HitRadius;
    for (const Entity &e : scene.entities()) {
        if (!e.camera || e.id == hiddenCamera)
            continue;
        for (const auto &[a, b] : cameraSegments(e, camera, viewSize, cameraAspect)) {
            const double d = OverlayDraw::distanceToSegment(point, a, b) + 4.0; // lights win when both are close
            if (d <= bestDistance) {
                best = e.id;
                bestDistance = d;
            }
        }
    }
    for (const Entity &e : scene.entities()) {
        if (!e.light)
            continue;
        bool inFront = false;
        const QPointF c = camera.project(e.transform.position, viewSize, &inFront);
        const double d = std::hypot(point.x() - c.x(), point.y() - c.y());
        if (inFront && d <= bestDistance) {
            best = e.id;
            bestDistance = d;
        }
    }
    return best;
}
} // namespace LightIcons
