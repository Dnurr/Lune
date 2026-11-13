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

QColor dimmed(const QColor &c)
{
    QColor d = c;
    d.setAlphaF(0.45f);
    return d;
}
} // namespace

namespace LightIcons
{
void append(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, EntityId selected,
            std::vector<OverlayVertex> &out)
{
    using namespace OverlayDraw;
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

EntityId hitTest(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, const QPointF &point)
{
    EntityId best = InvalidEntity;
    double bestDistance = HitRadius;
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
