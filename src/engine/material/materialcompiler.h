#pragma once

#include <QString>
#include <vector>

#include "nodegraph.h"
#include "nodelibrary.h"

// Turns a material's node graph into GLSL: the node functions of the library, then
//
//   Surface materialSurface()
//   {
//       vec3 n3_result;
//       node_mix(materialParams[0].xyz, n2_color, materialParams[1].x, n3_result);
//       Surface s;
//       s.baseColor = n3_result;  ...
//       return s;
//   }
//
// which replaces the default materialSurface() between the markers in mesh.frag.
// Only the nodes that reach the Material Output are generated, in dependency order.
//
// Values typed into unlinked inputs aren't written into the code: each one gets a slot
// of materialParams[] (a vec4 per input, in the entity's uniforms), so editing a value
// only updates a buffer. The code changes, and the shader is compiled again, only when
// nodes or links change.
//
// Linked ports of different types are converted: float -> vec3 repeats the value,
// vec3 -> float takes the luminance (Rec. 709 weights).
struct MaterialParam
{
    int node = 0;
    QString port;
    PortType type = PortType::Float;
    bool color = false; // sRGB in the graph: converted to linear when uploaded
};

struct CompiledMaterial
{
    QString code;                       // GLSL replacing the default materialSurface(); empty on error
    std::vector<MaterialParam> params;  // materialParams[i] <- params[i]
    QString error;                      // why there's no code (e.g. no Material Output)

    bool ok() const { return error.isEmpty(); }
};

namespace MaterialCompiler
{
constexpr int MaxParams = 32; // must match MAX_MATERIAL_PARAMS in mesh.frag
inline const QString BeginMarker = QStringLiteral("//@material-begin");
inline const QString EndMarker = QStringLiteral("//@material-end");

CompiledMaterial compile(const NodeGraph &graph, const NodeLibrary &library);

// `fragmentSource` (mesh.frag) with the part between the markers replaced by `code`.
// Empty if the markers are missing.
QString insertInto(const QString &fragmentSource, const QString &code);

// Value of an input: its own, or the type's default.
QVariant inputValue(const GraphNode &node, const PortDesc &port);
} // namespace MaterialCompiler
