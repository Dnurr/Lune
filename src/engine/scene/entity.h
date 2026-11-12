#pragma once

#include <QColor>
#include <QString>
#include <QtGlobal>

#include "mesh.h"
#include "transform.h"

// Stable identifier of an entity: never reused within a Scene, so it stays valid
// across undo/redo (a deleted and restored entity keeps its id).
using EntityId = quint64;
constexpr EntityId InvalidEntity = 0;

struct Entity
{
    EntityId id = InvalidEntity;
    QString name;
    Transform transform;
    MeshPtr mesh; // nullptr = empty entity (just a transform)

    // Viewport display of the mesh
    QColor displayColor { 0xB9, 0xBB, 0xC4 };
    bool visible = true;
};
