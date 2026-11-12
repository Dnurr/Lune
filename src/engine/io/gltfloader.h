#pragma once

#include <QString>

#include "engine/scene/mesh.h"

// Loads a glTF 2.0 file (.gltf with its .bin/embedded buffers, or .glb) as a single mesh:
// every triangle primitive of every node, baked with the node's world transform
// (same "one file = one mesh" model as the OBJ loader). Missing normals are computed
// (smooth). Materials, UVs, skins and animations are ignored for now.
// Returns nullptr and fills `error` on failure.
MeshPtr loadGltf(const QString &path, QString *error = nullptr);
