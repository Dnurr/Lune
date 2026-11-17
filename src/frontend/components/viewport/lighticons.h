#pragma once

#include <QPointF>
#include <QSizeF>
#include <vector>

#include "engine/render/camera.h"
#include "engine/render/renderer.h"
#include "engine/scene/entity.h"

class Scene;

// Lights and cameras have no mesh: the viewport shows them as icons drawn in the overlay,
// and clicking an icon selects the entity (like the gizmo, what you see is what you can
// click).
//
//   point light:        disc in the light's color, ring, short rays all around
//   directional light:  disc and ring, plus a line showing where it shines
//   camera:             its frustum in the world (a pyramid from the eye), with a
//                       triangle on top showing which way is up
namespace LightIcons
{
// Click distance from the icon center (or a camera's lines), in pixels.
constexpr float HitRadius = 12.0f;

// `cameraAspect`: width / height of the camera frustums (the render's).
// `hiddenCamera`: not drawn (the camera the viewport looks through).
void append(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, EntityId selected, float cameraAspect,
            EntityId hiddenCamera, std::vector<OverlayVertex> &out);

// Light or camera whose icon is under `point` (the closest one), or InvalidEntity.
EntityId hitTest(const Scene &scene, const OrbitCamera &camera, const QSizeF &viewSize, const QPointF &point,
                 float cameraAspect, EntityId hiddenCamera);
} // namespace LightIcons
