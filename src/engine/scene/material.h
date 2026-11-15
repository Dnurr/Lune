#pragma once

#include <QString>

#include "engine/material/nodegraph.h"
#include "entity.h"

// A node-based material in the scene's library: a name and the node graph that computes
// the surface (base color, roughness, metallic...). Entities point at it by id.
struct Material
{
    MaterialId id = NoMaterial;
    QString name;
    NodeGraph graph;

    bool operator==(const Material &) const = default;

    // A new material's graph: just a Material Output, with the default surface values.
    static NodeGraph defaultGraph();
};
