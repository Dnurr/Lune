#pragma once

#include "engine/io/gltfloader.h"
#include "nodegraph.h"
#include "nodelibrary.h"

// A node graph computing a glTF material (metallic-roughness), with the library's nodes:
//
//   baseColor   Image Texture (color) x baseColorFactor (Multiply Color), or the factor
//   roughness   G of the metallic-roughness texture (Separate RGB) x roughnessFactor
//   metallic    B of the same texture x metallicFactor
//   occlusion   R of the occlusion texture (often the same image: one node for both)
//   normal      normal texture (data) -> Normal Map node, if the library has one with the
//               expected signature (see material_nodes.glsl); otherwise left unlinked
//   emission    emissive texture x emissiveFactor
//
// Factors equal to 1 (or white) add no Multiply node, a factor 0 is just the value 0;
// without a texture the factor is the Material Output's value. Colors are stored sRGB in graphs: the linear glTF factors are
// converted. Not supported: alpha (blending, masks), texture transforms, texCoord sets
// other than the first, occlusion strength.
NodeGraph graphFromGltf(const GltfMaterial &material, const NodeLibrary &library);
