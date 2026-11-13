#pragma once

#include <QColor>
#include <QString>
#include <QtGlobal>
#include <optional>

#include "mesh.h"
#include "transform.h"

// Stable identifier of an entity: never reused within a Scene, so it stays valid
// across undo/redo (a deleted and restored entity keeps its id).
using EntityId = quint64;
constexpr EntityId InvalidEntity = 0;

// A light source. Its position and direction come from the entity's transform:
// a point light shines from the position in every direction; a directional light (the sun)
// shines along the entity's local -Y axis, so with no rotation it points straight down.
struct Light
{
    enum Type
    {
        Directional = 0,
        Point = 1,
    };

    Type type = Directional;
    QColor color { 0xFF, 0xFF, 0xFF }; // sRGB, like every color in the editor
    float intensity = 3.0f;            // directional: radiance; point: radiance at 1 unit
    bool enabled = true;

    bool operator==(const Light &) const = default;

    // World direction a directional light shines along: the transform's local -Y axis.
    static QVector3D direction(const Transform &transform)
    {
        Transform rotationOnly;
        rotationOnly.rotation = transform.rotation;
        return rotationOnly.matrix().mapVector(QVector3D(0.0f, -1.0f, 0.0f)).normalized();
    }
};

struct Entity
{
    EntityId id = InvalidEntity;
    QString name;
    Transform transform;
    MeshPtr mesh; // nullptr = empty entity (just a transform)

    // Viewport display of the mesh
    QColor displayColor { 0xB9, 0xBB, 0xC4 };
    bool visible = true;

    // Surface parameters for physically based shading (mesh.frag), both 0..1.
    // The base color is displayColor.
    float roughness = 0.5f; // 0 = mirror-like, 1 = fully diffuse highlight
    float metallic = 0.0f;  // 0 = dielectric (plastic, stone...), 1 = metal

    std::optional<Light> light; // set = this entity is a light
};
