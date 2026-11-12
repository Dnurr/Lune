#pragma once

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
};

// Which part of a Transform an edit touched (used to group undo steps).
enum class TransformField
{
    Position,
    Rotation,
    Scale,
};
