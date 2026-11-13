#include "gizmo.h"

#include "overlaydraw.h"

#include <QColor>
#include <QLineF>
#include <QtMath>

#include <limits>

using namespace OverlayDraw;

namespace
{
constexpr float kHandlePixels = 110.0f; // on-screen length of an axis handle
constexpr float kHitDistance = 9.0f;    // pixels
constexpr float kLineWidth = 2.5f;
constexpr float kLineWidthActive = 4.0f;
constexpr int kRingSegments = 72;

QColor axisColor(int handle)
{
    switch (handle) {
    case Gizmo::AxisX:
        return QColor("#E0525F");
    case Gizmo::AxisY:
        return QColor("#79C26A");
    case Gizmo::AxisZ:
        return QColor("#5B8DEF");
    default:
        return QColor("#E4E4E8");
    }
}

QColor highlighted(const QColor &c)
{
    return QColor::fromRgbF(c.redF() + (1.0f - c.redF()) * 0.55f, c.greenF() + (1.0f - c.greenF()) * 0.55f,
                            c.blueF() + (1.0f - c.blueF()) * 0.55f);
}

// Wrap an angle difference to (-180, 180].
float wrapDegrees(float a)
{
    while (a > 180.0f)
        a -= 360.0f;
    while (a <= -180.0f)
        a += 360.0f;
    return a;
}
} // namespace

QVector3D Gizmo::axisVector(int axis) const
{
    return QVector3D(axis == AxisX ? 1.0f : 0.0f, axis == AxisY ? 1.0f : 0.0f, axis == AxisZ ? 1.0f : 0.0f);
}

void Gizmo::layout(const OrbitCamera &camera, const QSizeF &viewSize, const Transform &transform)
{
    m_camera = camera;
    m_viewSize = viewSize;
    m_center = transform.position;
    m_shapes.clear();
    m_axisEnds.assign(3, QPointF());

    bool inFront = false;
    m_screenCenter = camera.project(m_center, viewSize, &inFront);
    m_visible = inFront;
    if (!m_visible)
        return;

    const float dist = (camera.eye() - m_center).length();
    m_length = camera.worldPerPixel(dist, float(viewSize.height())) * kHandlePixels;

    for (int axis = AxisX; axis <= AxisZ; ++axis) {
        const QVector3D a = axisVector(axis);
        if (m_mode == Mode::Rotate) {
            // Ring around the axis.
            const QVector3D u = axisVector((axis + 1) % 3);
            const QVector3D v = axisVector((axis + 2) % 3);
            Shape ring { axis, {}, true };
            for (int i = 0; i < kRingSegments; ++i) {
                const float t = 2.0f * float(M_PI) * i / kRingSegments;
                const QVector3D p = m_center + (u * qCos(t) + v * qSin(t)) * m_length * 0.85f;
                ring.points.push_back(camera.project(p, viewSize));
            }
            m_shapes.push_back(std::move(ring));
        } else {
            const QPointF end = camera.project(m_center + a * m_length, viewSize);
            m_axisEnds[axis] = end;
            m_shapes.push_back({ axis, { m_screenCenter, end }, false });
        }
    }
}

int Gizmo::hitTest(const QPointF &point) const
{
    if (!m_visible)
        return None;

    if (m_mode == Mode::Scale) {
        const QPointF d = point - m_screenCenter;
        if (qAbs(d.x()) <= 8 && qAbs(d.y()) <= 8)
            return Uniform;
    }

    int best = None;
    double bestDist = kHitDistance;
    for (const Shape &s : m_shapes) {
        const size_t n = s.points.size();
        const size_t segments = s.closed ? n : n - 1;
        for (size_t i = 0; i < segments; ++i) {
            const double d = distanceToSegment(point, s.points[i], s.points[(i + 1) % n]);
            if (d < bestDist) {
                bestDist = d;
                best = s.handle;
            }
        }
        // Arrow heads / end squares extend a bit past the line.
        if (!s.closed && m_mode != Mode::Rotate) {
            const QPointF e = s.points.back();
            const double d = std::hypot(point.x() - e.x(), point.y() - e.y()) - 6.0;
            if (d < bestDist) {
                bestDist = d;
                best = s.handle;
            }
        }
    }
    return best;
}

void Gizmo::appendGeometry(std::vector<OverlayVertex> &out) const
{
    if (!m_visible)
        return;

    const int focus = m_active != None ? m_active : m_hovered;
    auto colorFor = [&](int handle) {
        const QColor c = axisColor(handle);
        return handle == focus ? highlighted(c) : c;
    };

    for (const Shape &s : m_shapes) {
        // While dragging, only the active handle is drawn (less clutter).
        if (m_active != None && s.handle != m_active)
            continue;
        const QColor col = colorFor(s.handle);
        const float width = s.handle == focus ? kLineWidthActive : kLineWidth;
        const size_t n = s.points.size();
        const size_t segments = s.closed ? n : n - 1;
        for (size_t i = 0; i < segments; ++i)
            segment(out, s.points[i], s.points[(i + 1) % n], width, col);

        if (s.closed)
            continue;
        const QPointF a = s.points.front();
        const QPointF b = s.points.back();
        const QPointF d = b - a;
        const double len = std::hypot(d.x(), d.y());
        if (len < 1e-3)
            continue;
        const QPointF dir = d / len;
        const QPointF nrm(-dir.y(), dir.x());
        if (m_mode == Mode::Translate)
            triangle(out, b + dir * 14.0, b + nrm * 6.0, b - nrm * 6.0, col); // arrow head
        else
            square(out, b, 5.0f, col);
    }

    if (m_mode == Mode::Scale && (m_active == None || m_active == Uniform))
        square(out, m_screenCenter, 6.0f, colorFor(Uniform));
}

void Gizmo::beginDrag(int handle, const QPointF &mouse, const Transform &start)
{
    m_active = handle;
    m_start = start;
    m_startMouse = mouse;
    if (handle >= AxisX && handle <= AxisZ)
        m_startAxisScreen = m_axisEnds[handle] - m_screenCenter;

    if (m_mode == Mode::Translate) {
        m_startParam = 0.0f;
        // Same computation as dragTo, so the object doesn't jump on the first move.
        QVector3D o, d;
        m_camera.screenRay(mouse, m_viewSize, &o, &d);
        const QVector3D a = axisVector(handle);
        const QVector3D w = m_center - o;
        const float A = 1.0f, B = QVector3D::dotProduct(a, d), C = QVector3D::dotProduct(d, d);
        const float D = QVector3D::dotProduct(a, w), E = QVector3D::dotProduct(d, w);
        const float denom = A * C - B * B;
        if (qAbs(denom) > 1e-8f)
            m_startParam = (B * E - C * D) / denom;
    } else if (m_mode == Mode::Rotate) {
        const QPointF r = mouse - m_screenCenter;
        m_lastAngle = float(qRadiansToDegrees(std::atan2(r.y(), r.x())));
        m_angle = 0.0f;
    }
}

Transform Gizmo::dragTo(const QPointF &mouse)
{
    Transform t = m_start;
    if (m_active == None)
        return t;

    switch (m_mode) {
    case Mode::Translate: {
        // Closest point on the axis line to the mouse ray.
        QVector3D o, d;
        m_camera.screenRay(mouse, m_viewSize, &o, &d);
        const QVector3D a = axisVector(m_active);
        const QVector3D w = m_start.position - o;
        const float B = QVector3D::dotProduct(a, d), C = QVector3D::dotProduct(d, d);
        const float D = QVector3D::dotProduct(a, w), E = QVector3D::dotProduct(d, w);
        const float denom = C - B * B; // A = |a|^2 = 1
        if (qAbs(denom) < 1e-8f)
            return t; // looking straight down the axis: can't move along it
        const float s = (B * E - C * D) / denom;
        t.position = m_start.position + a * (s - m_startParam);
        break;
    }
    case Mode::Rotate: {
        const QPointF r = mouse - m_screenCenter;
        const float angle = float(qRadiansToDegrees(std::atan2(r.y(), r.x())));
        m_angle += wrapDegrees(angle - m_lastAngle);
        m_lastAngle = angle;
        // Screen y points down, so a growing atan2 angle is clockwise on screen.
        // A positive rotation is counter-clockwise when the axis points at the viewer.
        const QVector3D a = axisVector(m_active);
        const float facing = QVector3D::dotProduct(a, m_camera.eye() - m_start.position) >= 0.0f ? 1.0f : -1.0f;
        QVector3D rot = m_start.rotation;
        rot[m_active] = m_start.rotation[m_active] - m_angle * facing;
        t.rotation = rot;
        break;
    }
    case Mode::Scale: {
        if (m_active == Uniform) {
            const float f = qMax(0.01f, 1.0f + float(mouse.x() - m_startMouse.x()) / 120.0f);
            t.scale = m_start.scale * f;
        } else {
            const double len2 = QPointF::dotProduct(m_startAxisScreen, m_startAxisScreen);
            if (len2 < 1.0)
                return t;
            const float f = qMax(0.01f, 1.0f + float(QPointF::dotProduct(mouse - m_startMouse, m_startAxisScreen) / len2));
            QVector3D s = m_start.scale;
            s[m_active] = m_start.scale[m_active] * f;
            t.scale = s;
        }
        break;
    }
    }
    return t;
}

void Gizmo::endDrag()
{
    m_active = None;
}

TransformField Gizmo::field() const
{
    switch (m_mode) {
    case Mode::Translate:
        return TransformField::Position;
    case Mode::Rotate:
        return TransformField::Rotation;
    case Mode::Scale:
        return TransformField::Scale;
    }
    return TransformField::Position;
}
