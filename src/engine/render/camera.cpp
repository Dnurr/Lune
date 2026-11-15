#include "camera.h"

#include <QtMath>

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

QMatrix4x4 OrbitCamera::view() const
{
    QMatrix4x4 m;
    m.lookAt(eye(), target, QVector3D(0, 1, 0));
    return m;
}

QMatrix4x4 OrbitCamera::projection(float aspect) const
{
    QMatrix4x4 m;
    m.perspective(fieldOfView, aspect, nearPlane, farPlane);
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

void OrbitCamera::fly(const QVector3D &move)
{
    target += right() * move.x() + QVector3D(0, 1, 0) * move.y() + forward() * move.z();
}
