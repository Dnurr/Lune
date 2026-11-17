#include "camera.h"

#include <QtMath>
#include <cmath>

QVector3D OrbitCamera::eye() const
{
    const float y = qDegreesToRadians(yaw);
    const float p = qDegreesToRadians(pitch);
    const QVector3D offset(qCos(p) * qSin(y), qSin(p), qCos(p) * qCos(y));
    return target + offset * distance;
}

QVector3D OrbitCamera::forward() const
{
    return (target - eye()).normalized();
}

QVector3D OrbitCamera::right() const
{
    const float y = qDegreesToRadians(yaw);
    return QVector3D(qCos(y), 0.0f, -qSin(y)); // forward x world up, flattened
}

QVector3D OrbitCamera::up() const
{
    const QVector3D f = forward();
    const QVector3D r = QVector3D::crossProduct(f, QVector3D(0, 1, 0)).normalized(); // pitch < 90: never zero
    const QVector3D u = QVector3D::crossProduct(r, f);
    if (roll == 0.0f)
        return u;
    const float a = qDegreesToRadians(roll);
    return (u * qCos(a) + r * qSin(a)).normalized();
}

QMatrix4x4 OrbitCamera::view() const
{
    QMatrix4x4 m;
    m.lookAt(eye(), target, up());
    return m;
}

OrbitCamera OrbitCamera::fromSceneCamera(const Transform &transform, const Camera &camera, float distance)
{
    const QQuaternion q = transform.rotationQuaternion();
    const QVector3D f = q.rotatedVector(QVector3D(0, 0, -1)).normalized();
    const QVector3D upAxis = q.rotatedVector(QVector3D(0, 1, 0)).normalized();

    OrbitCamera c;
    c.fieldOfView = camera.fieldOfView;
    c.nearPlane = camera.nearPlane;
    c.farPlane = camera.farPlane;
    c.distance = distance;
    // The eye sits at the target + (cos p sin y, sin p, cos p cos y) * distance: that offset is -f.
    c.pitch = qBound(-89.0f, qRadiansToDegrees(std::asin(qBound(-1.0f, -f.y(), 1.0f))), 89.0f);
    c.yaw = qRadiansToDegrees(std::atan2(-f.x(), -f.z()));
    if (std::hypot(f.x(), f.z()) < 1e-6f) // straight up or down: the yaw comes from the camera's up
        c.yaw = qRadiansToDegrees(std::atan2(f.y() > 0 ? upAxis.x() : -upAxis.x(), f.y() > 0 ? upAxis.z() : -upAxis.z()));
    c.target = QVector3D(0, 0, 0);
    c.target = transform.position - c.eye(); // eye() is target + offset: now the eye is the position

    // Roll: the camera's up measured from the unrolled up, around the view direction.
    c.roll = 0.0f;
    const QVector3D fw = c.forward();
    const QVector3D r = QVector3D::crossProduct(fw, QVector3D(0, 1, 0)).normalized();
    const QVector3D u = QVector3D::crossProduct(r, fw);
    c.roll = qRadiansToDegrees(std::atan2(QVector3D::dotProduct(upAxis, r), QVector3D::dotProduct(upAxis, u)));
    return c;
}

QVector3D OrbitCamera::sceneCameraRotation(const QVector3D &closeTo) const
{
    const QVector3D z = -forward();
    const QVector3D y = up();
    const QVector3D x = QVector3D::crossProduct(y, z).normalized();
    return Transform::eulerFromQuaternion(QQuaternion::fromAxes(x, y, z), closeTo);
}

QMatrix4x4 OrbitCamera::projection(float aspect) const
{
    QMatrix4x4 m;
    m.perspective(fieldOfView, aspect, nearPlane, farPlane);
    if (shiftX != 0.0f) {
        QMatrix4x4 shift; // clip x += shiftX * w
        shift(0, 3) = shiftX;
        m = shift * m;
    }
    return m;
}

QPointF OrbitCamera::project(const QVector3D &world, const QSizeF &viewSize, bool *inFront) const
{
    const float aspect = float(viewSize.width() / qMax(1.0, viewSize.height()));
    const QVector4D clip = projection(aspect) * view() * QVector4D(world, 1.0f);
    if (inFront)
        *inFront = clip.w() > 1e-4f;
    const float w = qAbs(clip.w()) > 1e-6f ? clip.w() : 1e-6f;
    const float x = clip.x() / w;
    const float y = clip.y() / w;
    return QPointF((x + 1.0f) * 0.5f * viewSize.width(), (1.0f - y) * 0.5f * viewSize.height());
}

void OrbitCamera::screenRay(const QPointF &point, const QSizeF &viewSize, QVector3D *origin, QVector3D *direction) const
{
    const float aspect = float(viewSize.width() / qMax(1.0, viewSize.height()));
    const QMatrix4x4 inv = (projection(aspect) * view()).inverted();
    const float x = float(point.x() / viewSize.width() * 2.0 - 1.0);
    const float y = float(1.0 - point.y() / viewSize.height() * 2.0);
    const QVector3D nearP = inv.map(QVector3D(x, y, -1.0f));
    const QVector3D farP = inv.map(QVector3D(x, y, 1.0f));
    *origin = nearP;
    *direction = farP - nearP;
}

float OrbitCamera::worldPerPixel(float distanceFromEye, float viewHeight) const
{
    return 2.0f * distanceFromEye * qTan(qDegreesToRadians(fieldOfView) * 0.5f) / qMax(1.0f, viewHeight);
}

void OrbitCamera::orbit(float deltaYaw, float deltaPitch)
{
    yaw += deltaYaw;
    pitch = qBound(-89.0f, pitch + deltaPitch, 89.0f);
}

void OrbitCamera::pan(float dx, float dy)
{
    const QVector3D forward = (target - eye()).normalized();
    const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
    const QVector3D up = QVector3D::crossProduct(right, forward);
    const float k = distance * 0.0015f;
    target += (-right * dx + up * dy) * k;
}

void OrbitCamera::zoom(float steps)
{
    distance = qBound(0.5f, distance * qPow(0.88f, steps), 500.0f);
}

void OrbitCamera::look(float deltaYaw, float deltaPitch)
{
    const QVector3D e = eye();
    orbit(deltaYaw, deltaPitch);
    target += e - eye(); // the eye stays, the target moves around it
}

void OrbitCamera::moveTowards(const OrbitCamera &goal, float amount)
{
    const float t = qBound(0.0f, amount, 1.0f);
    target += (goal.target - target) * t;
    yaw += (goal.yaw - yaw) * t;
    pitch += (goal.pitch - pitch) * t;
    // Distance: in log space, so zooming feels the same near and far.
    distance *= qPow(goal.distance / distance, t);
    shiftX += (goal.shiftX - shiftX) * t;
    roll += (goal.roll - roll) * t;
    fieldOfView = goal.fieldOfView;
    nearPlane = goal.nearPlane;
    farPlane = goal.farPlane;
}

bool OrbitCamera::isCloseTo(const OrbitCamera &other) const
{
    const float eps = 1e-4f * qMax(1.0f, distance);
    return (target - other.target).length() < eps && qAbs(yaw - other.yaw) < 0.01f
        && qAbs(pitch - other.pitch) < 0.01f && qAbs(distance - other.distance) < eps
        && qAbs(shiftX - other.shiftX) < 1e-4f && qAbs(roll - other.roll) < 0.01f;
}

void OrbitCamera::fly(const QVector3D &move)
{
    target += right() * move.x() + QVector3D(0, 1, 0) * move.y() + forward() * move.z();
}
