#pragma once

#include <QVector3D>

#include "entity.h"

class Scene;

// Closest entity hit by a ray (origin + t * direction, t > 0), testing the triangles
// of each entity's mesh. Entities without a mesh can't be picked.
// Returns InvalidEntity if nothing is hit; `t` receives the ray parameter of the hit.
EntityId pickEntity(const Scene &scene, const QVector3D &origin, const QVector3D &direction, float *t = nullptr);
