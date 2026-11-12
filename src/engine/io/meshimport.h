#pragma once

#include <QString>

#include "engine/scene/mesh.h"

// Loads a mesh file, choosing the loader from the extension (.obj, .gltf, .glb).
// Returns nullptr and fills `error` on failure.
MeshPtr importMesh(const QString &path, QString *error = nullptr);

// File dialog filter for every supported format.
QString meshFileFilter();
