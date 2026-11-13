#pragma once

#include <QPointF>
#include <QSizeF>
#include <vector>

#include "engine/render/camera.h"
#include "engine/render/renderer.h"
#include "engine/scene/entity.h"

class Scene;

// Lights have no mesh: the viewport shows them as small screen-space icons drawn in the
// overlay, and clicking an icon selects the light (like the gizmo, what you see is what
// you can click).
//
//   point light:        disc in the light's color, ring, short rays all around
//   directional light:  disc and ring, plus a line showing where it shines
namespace LightIcons
{
// Click distance from the icon center, in pixels.
constexpr float HitRadius = 12.0f;

void append(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, EntityId selected,
            std::vector<OverlayVertex> &out);

// Light whose icon is under `point` (the closest one), or InvalidEntity.
EntityId hitTest(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, const QPointF &point);
} // namespace LightIcons
