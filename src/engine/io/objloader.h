#pragma once

#include <QString>

#include "engine/scene/mesh.h"

// Loads a Wavefront .obj file as a single mesh (all objects/groups merged).
// Supports v, vn and f (any polygon, triangulated as a fan; v, v/vt, v//vn, v/vt/vn;
// negative indices). Faces without normals get flat normals. Materials and UVs are ignored.
// Returns nullptr and fills `error` on failure.
MeshPtr loadObj(const QString &path, QString *error = nullptr);
