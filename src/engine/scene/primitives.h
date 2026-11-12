#pragma once

#include "mesh.h"

// Built-in meshes, all 1 unit in size and centered on the origin.
// Each one is created once and shared (every cube in the scene uses the same Mesh).
namespace Primitives
{
MeshPtr cube();
MeshPtr sphere();
MeshPtr plane(); // on the XZ plane, facing +Y
} // namespace Primitives
