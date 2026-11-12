#pragma once

#include <QMatrix4x4>
#include <QVector3D>

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
};

// Which part of a Transform an edit touched (used to group undo steps).
enum class TransformField
{
    Position,
    Rotation,
    Scale,
};
