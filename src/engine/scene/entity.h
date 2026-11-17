#pragma once

#include <QColor>
#include <QString>
#include <QtGlobal>
#include <optional>

#include "mesh.h"
#include "transform.h"

// Stable identifier of an entity: never reused within a Scene, so it stays valid
// across undo/redo (a deleted and restored entity keeps its id).
using EntityId = quint64;
constexpr EntityId InvalidEntity = 0;

// A material of the scene's library (Scene::materials()), shared by any number of
// entities. 0 = none: the entity uses its own color, roughness and metallic.
using MaterialId = quint64;
constexpr MaterialId NoMaterial = 0;

// A light source. Its position and direction come from the entity's transform:
// a point light shines from the position in every direction; a directional light (the sun)
// shines along the entity's local -Y axis, so with no rotation it points straight down.
struct Light
{
    enum Type
    {
        Directional = 0,
        Point = 1,
    };

    Type type = Directional;
    QColor color { 0xFF, 0xFF, 0xFF }; // sRGB, like every color in the editor
    float intensity = 3.0f;            // directional: radiance; point: radiance at 1 unit
    bool shadows = true;               // casts shadows
    float size = 0.1f;                 // physical size, for soft shadows: point = radius in units,
                                       // directional = angular diameter in degrees (the sun: ~0.5)
    float softness = 1.0f;             // shadow edge softness: filter radius in shadow map texels
    bool enabled = true;

    bool operator==(const Light &) const = default;

    // World direction a directional light shines along: the transform's local -Y axis.
    static QVector3D direction(const Transform &transform)
    {
        Transform rotationOnly;
        rotationOnly.rotation = transform.rotation;
        return rotationOnly.matrix().mapVector(QVector3D(0.0f, -1.0f, 0.0f)).normalized();
    }
};

// Image-based lighting: light coming from every direction, from an HDR image of the
// surroundings (an equirectangular .hdr) or, without one, from a sky/ground gradient.
// The entity's rotation turns the environment; its position doesn't matter.
// Only the first enabled environment in the scene is used.
struct Environment
{
    QString image;                           // absolute path of an .hdr file; empty = gradient
    float intensity = 1.0f;                  // multiplies the radiance
    QColor skyColor { 0x95, 0xA2, 0xB3 };    // gradient, from above (sRGB)
    QColor groundColor { 0x59, 0x55, 0x50 }; // gradient, from below (sRGB)
    bool background = true;                  // shown behind the scene instead of the editor sky
    float blur = 0.0f;                       // background blur, 0..1
    bool enabled = true;

    bool operator==(const Environment &) const = default;
};

// A camera the scene is rendered from (Render Image, Render Passes, Camera View). It looks
// along the entity's local -Z axis with local +Y up; position and rotation come from the
// transform, scale is ignored. Only the first enabled camera in the scene is used.
struct Camera
{
    float fieldOfView = 50.0f; // vertical, degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool enabled = true;

    bool operator==(const Camera &) const = default;
};

// Post-processing of the image a camera renders, in a fixed order:
//   HDR, before tonemapping:  depth of field, bloom
//   display, after it:        chromatic aberration, vignette, film grain, FXAA
// Each effect has a switch; an effect that is off costs nothing. The values reach the
// effect's shader as params0/params1 (see PostProcessor in engine/render/postprocess.h).
// Used from the scene camera's entity (the first enabled Camera), when it has one.
struct PostProcess
{
    bool enabled = true; // all effects

    bool depthOfField = false;
    float focusDistance = 5.0f; // world units from the camera
    float fStop = 2.8f;         // smaller = shallower focus
    float maxBlur = 8.0f;       // circle of confusion limit, pixels at 1080p

    bool bloom = false;
    float bloomIntensity = 0.05f;
    float bloomThreshold = 1.0f; // linear radiance where the glow starts
    float bloomRadius = 0.6f;    // 0..1, how far the glow spreads

    bool chromaticAberration = false;
    float chromaticIntensity = 0.5f;

    bool vignette = false;
    float vignetteIntensity = 0.3f;
    float vignetteSmoothness = 0.5f;

    bool filmGrain = false;
    float grainIntensity = 0.1f;
    float grainSize = 1.0f; // pixels

    bool fxaa = false;

    bool operator==(const PostProcess &) const = default;
};

struct Entity
{
    EntityId id = InvalidEntity;
    QString name;
    Transform transform;
    MeshPtr mesh; // nullptr = empty entity (just a transform)

    // Viewport display of the mesh
    QColor displayColor { 0xB9, 0xBB, 0xC4 };
    bool visible = true;

    // Surface parameters for physically based shading (mesh.frag), both 0..1.
    // The base color is displayColor.
    float roughness = 0.5f; // 0 = mirror-like, 1 = fully diffuse highlight
    float metallic = 0.0f;  // 0 = dielectric (plastic, stone...), 1 = metal
    // Node materials from the scene's library, one slot per submesh of the mesh (slot i
    // draws mesh->submeshes[i]); missing slots = NoMaterial. A slot with a material
    // replaces the three values above for its triangles.
    std::vector<MaterialId> materials;

    MaterialId material(int slot = 0) const
    {
        return slot >= 0 && slot < int(materials.size()) ? materials[size_t(slot)] : NoMaterial;
    }
    void setMaterial(int slot, MaterialId id)
    {
        if (slot < 0)
            return;
        if (slot >= int(materials.size()))
            materials.resize(size_t(slot) + 1, NoMaterial);
        materials[size_t(slot)] = id;
        while (!materials.empty() && materials.back() == NoMaterial)
            materials.pop_back(); // trailing empty slots: the same as missing
    }

    std::optional<Light> light;             // set = this entity is a light
    std::optional<Environment> environment; // set = this entity is the scene's environment
    std::optional<Camera> camera;           // set = this entity is a camera
    std::optional<PostProcess> postProcess; // effects of the image rendered by this entity's camera
};
