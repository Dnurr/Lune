#pragma once

#include <QString>

#include <vector>

#include "engine/scene/material.h"
#include "engine/scene/mesh.h"

// Loads a mesh file, choosing the loader from the extension (.obj, .gltf, .glb).
// Returns nullptr and fills `error` on failure.
MeshPtr importMesh(const QString &path, QString *error = nullptr);

// A mesh file with its materials, ready to become an entity.
struct ImportedModel
{
    MeshPtr mesh; // nullptr on failure
    // One per submesh (material slot): the file's material as a node graph (graphFromGltf),
    // or a Material with an empty name where the submesh has none. Ids are NoMaterial:
    // the scene assigns them when the materials are added.
    std::vector<Material> materials;
};

// importMesh(), plus the materials of glTF files (OBJ materials aren't read yet).
ImportedModel importModel(const QString &path, QString *error = nullptr);

// File dialog filter for every supported format.
QString meshFileFilter();
