#pragma once

#include <QString>
#include <QtGlobal>
#include <vector>

#include "materialcompiler.h"
#include "nodegraph.h"
#include "nodelibrary.h"

// Particle graphs: what acts on an emitter's particles, as a node graph of its own
// (ParticleEmitter::graph), compiled into the simulation shader.
//
// The graph ends in a Particle Output, whose input is the acceleration (units / s^2) the
// particle gets at each step. Force nodes (particle_nodes.glsl, the author's) take the
// acceleration so far and add their own, so they chain: Scene Forces -> Vortex -> Output.
// Inputs can read the particle being simulated with expr(): `p` (Particle), `s` (Step) and
// `time` are in scope.
//
// Built-in nodes:
//   Scene Forces   every Force component of the scene (the default graph has just this, so an
//                  emitter without a graph behaves as before graphs)
//   Object         an entity of the scene, followed through the simulation: its position and
//                  axes at each step, to put a force on it (e.g. a vortex around a moving object)
//
// compile() gives
//
//   vec3 graphAcceleration(Particle p, Step s, float time)
//   {
//       mat4 n4_m = forces[s.objects.x + 0u].transform;   // Object (slot 0)
//       vec3 n4_position = n4_m[3].xyz; ...
//       vec3 n2_result;
//       node_vortex(vec3(0.0), (p.position.xyz), n4_position, graphParams[0].x, ..., n2_result);
//       return n2_result;
//   }
//
// with the library's node functions before it; it replaces the default graphAcceleration()
// between the markers in particles.comp. Unlinked values read graphParams[] (SimBuf), so
// editing one doesn't compile anything again. Object slots: the CPU puts each referenced
// entity's transform, per step, after that step's forces (Step.objects).
namespace ParticleGraph
{
constexpr int MaxParams = 32;  // must match MAX_GRAPH_PARAMS in particles.comp
constexpr int MaxObjects = 8;
inline const QString BeginMarker = QStringLiteral("//@graph-begin");
inline const QString EndMarker = QStringLiteral("//@graph-end");

struct Compiled
{
    QString code;                       // replaces the default graphAcceleration(); empty on error
    std::vector<MaterialParam> params;  // graphParams[i] <- params[i]
    std::vector<quint64> objects;       // object slot i <- this entity (0: none, identity)
    QString error;

    bool ok() const { return error.isEmpty(); }
};

Compiled compile(const NodeGraph &graph, const NodeLibrary &library);

// A new emitter graph: Scene Forces into the Particle Output.
NodeGraph defaultGraph();

// `source` (particles.comp) with the part between the markers replaced by `code`; empty if
// the markers are missing.
QString insertInto(const QString &source, const QString &code);
} // namespace ParticleGraph
