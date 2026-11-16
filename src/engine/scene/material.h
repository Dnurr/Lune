#pragma once

#include <QString>

#include "engine/material/nodegraph.h"
#include "entity.h"

// How a material uses the alpha of its surface (Material Output's `alpha`):
//   Opaque  alpha is ignored
//   Clip    pixels with alpha below the cutoff are cut away (leaves, fences); the rest is
//           opaque. Shadows have the same holes.
//   Blend   the surface is see-through by its alpha (glass, smoke): drawn after the opaque
//           ones, far to near, blended over them. It casts a translucent shadow that lets
//           1 - alpha of the light through (alpha 0.5: half of it).
enum class AlphaMode { Opaque, Clip, Blend };

// A node-based material in the scene's library: a name and the node graph that computes
// the surface (base color, roughness, metallic...). Entities point at it by id.
struct Material
{
    MaterialId id = NoMaterial;
    QString name;
    NodeGraph graph;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f; // Clip: pixels with alpha < this are cut away

    bool operator==(const Material &) const = default;

    // A new material's graph: just a Material Output, with the default surface values.
    static NodeGraph defaultGraph();

    // "opaque", "clip", "blend" (scene files); unknown names are Opaque.
    static QString alphaModeName(AlphaMode mode);
    static AlphaMode alphaModeFromName(const QString &name);
};
