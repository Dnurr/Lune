#include "camera.h"

#include <QtMath>

QVector3D OrbitCamera::eye() const
{
    const float y = qDegreesToRadians(yaw);
    const float p = qDegreesToRadians(pitch);
    const QVector3D offset(qCos(p) * qSin(y), qSin(p), qCos(p) * qCos(y));
    return target + offset * distance;
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
