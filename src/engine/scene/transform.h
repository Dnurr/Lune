#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>
#include <QtMath>

#include <cmath>

// Position, rotation and scale of an entity.
// Rotation is stored as Euler angles in degrees (X, then Y, then Z): easy to show and edit.
// A quaternion can be derived from it when the renderer needs one.
struct Transform
{
    QVector3D position { 0.0f, 0.0f, 0.0f };
    QVector3D rotation { 0.0f, 0.0f, 0.0f };
    QVector3D scale { 1.0f, 1.0f, 1.0f };

    bool operator==(const Transform &other) const = default;

    // Object -> world: translate * rotate (Z * Y * X) * scale.
    QMatrix4x4 matrix() const
    {
        QMatrix4x4 m;
        m.translate(position);
        m.rotate(rotation.z(), 0, 0, 1);
        m.rotate(rotation.y(), 0, 1, 0);
        m.rotate(rotation.x(), 1, 0, 0);
        m.scale(scale);
        return m;
    }

    // The rotation as a quaternion (same order: Z * Y * X).
    QQuaternion rotationQuaternion() const
    {
        return QQuaternion::fromAxisAndAngle(0, 0, 1, rotation.z()) * QQuaternion::fromAxisAndAngle(0, 1, 0, rotation.y())
            * QQuaternion::fromAxisAndAngle(1, 0, 0, rotation.x());
    }

    // World direction of a local axis (0 = X, 1 = Y, 2 = Z), unit length; scale ignored.
    QVector3D axis(int index) const
    {
        return rotationQuaternion().rotatedVector(QVector3D(index == 0, index == 1, index == 2)).normalized();
    }

    // Euler angles (degrees, X then Y then Z, as in `rotation`) for a rotation. Among the
    // equivalent sets, returns the one closest to `closeTo`, so values change continuously
    // while dragging (e.g. 170 -> 190, not 170 -> -170).
    static QVector3D eulerFromQuaternion(const QQuaternion &q, const QVector3D &closeTo = {})
    {
        const QMatrix3x3 r = q.normalized().toRotationMatrix(); // R = Rz(c) * Ry(b) * Rx(a)
        // cos(b) from the first column: accurate near ±90°, where 1 - sin² loses all precision.
        const float sinB = -r(2, 0);
        const float cosB = std::hypot(r(0, 0), r(1, 0));
        float a, b = qRadiansToDegrees(std::atan2(sinB, cosB)), c;
        if (cosB > 1e-3f) {
            a = qRadiansToDegrees(std::atan2(r(2, 1), r(2, 2)));
            c = qRadiansToDegrees(std::atan2(r(1, 0), r(0, 0)));
        } else { // gimbal lock (b = ±90°): only a - c (or a + c) is defined; keep a
            a = closeTo.x();
            const float d = sinB > 0.0f ? qRadiansToDegrees(std::atan2(r(0, 1), r(0, 2)))
                                        : qRadiansToDegrees(std::atan2(-r(0, 1), -r(0, 2)));
            c = sinB > 0.0f ? a - d : d - a;
        }
        auto unwrap = [](float v, float ref) { return v + 360.0f * std::round((ref - v) / 360.0f); };
        auto closest = [&](float x, float y, float z) {
            return QVector3D(unwrap(x, closeTo.x()), unwrap(y, closeTo.y()), unwrap(z, closeTo.z()));
        };
        const QVector3D first = closest(a, b, c);
        const QVector3D second = closest(a + 180.0f, 180.0f - b, c + 180.0f); // the other equivalent set
        return (first - closeTo).lengthSquared() <= (second - closeTo).lengthSquared() ? first : second;
    }
};

// Which part of a Transform an edit touched (used to group undo steps).
enum class TransformField
{
    Position,
    Rotation,
    Scale,
};
