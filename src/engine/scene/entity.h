#pragma once

#include <QColor>
#include <QString>
#include <QtGlobal>
#include <optional>

#include "engine/material/nodegraph.h"
#include "keyframes.h"
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
//   display, after it:        FXAA, chromatic aberration, vignette, film grain
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
    float grainResponse = 0.25f; // width of the bell over luminance: small = grain only in the midtones

    bool fxaa = false;

    bool operator==(const PostProcess &) const = default;
};

// A source of GPU particles (engine/render/particles.h). New particles start on the emitter's
// shape and fly along its local +Y axis; the simulation runs on the timeline, so the same frame
// always shows the same particles (scrubbing back simulates again from the start).
struct ParticleEmitter
{
    enum Shape
    {
        Point = 0,
        Sphere = 1, // shapeSize.x = radius
        Box = 2,    // shapeSize = edge lengths
        Disc = 3,   // shapeSize.x = radius, in the local XZ plane
    };
    enum Blend
    {
        Additive = 0, // glows: light adds up, no sorting needed
        Alpha = 1,    // smoke, dust: premultiplied alpha over the scene
    };

    bool enabled = true;
    float rate = 200.0f;              // particles per second
    float lifetime = 2.0f;            // seconds
    float lifetimeVariation = 0.25f;  // 0..1: up to this fraction is taken off each lifetime, at random
    int maxParticles = 10000;         // the buffer's size: the oldest are replaced when it's full
    int seed = 1;                     // another seed, another random pattern

    Shape shape = Point;
    QVector3D shapeSize { 0.5f, 0.5f, 0.5f };

    float speed = 2.0f;               // units per second, along local +Y
    float speedVariation = 0.2f;      // 0..1, random fraction taken off
    float spread = 15.0f;             // degrees: half angle of the cone directions are taken in

    QColor startColor { 0xFF, 0xC8, 0x78 }; // sRGB, at birth
    QColor endColor { 0xFF, 0x50, 0x20 };   // sRGB, at death
    float startOpacity = 1.0f;
    float endOpacity = 0.0f;
    float intensity = 2.0f;           // HDR multiplier of the colors (bloom picks bright particles up)
    float startSize = 0.08f;          // world units
    float endSize = 0.02f;
    Blend blend = Additive;
    float softness = 0.2f;            // world units: particles fade out this close to the surfaces behind them
    // What acts on the particles (engine/material/particlegraph.h). No nodes: every Force
    // component of the scene, like the default graph.
    NodeGraph graph;

    bool operator==(const ParticleEmitter &) const = default;
};

// A force on the scene's particles (engine/render/particles.h). Where it is and which way
// it points come from the entity's transform: its axis is local +Y. Any entity can carry one
// (an empty, a mesh, an emitter), so a force follows what it's on.
//
//   Gravity     acceleration `strength` along -axis (down, unrotated)
//   Wind        particles are dragged toward a velocity of `strength` along +axis, by `drag`
//   Turbulence  swirling noise of `strength` (curl noise: `frequency`, changing at `speed`)
//   Attractor   acceleration `strength` toward the position (negative: pushes away)
//   Vortex      spin of `strength` around the axis line through the position, `pull`
//               toward the axis, `lift` along it, plus local swirls of `noise` (the noise
//               settings): hurricanes, smoke columns, whirlpools
//
// `radius` limits the reach (from the position; for the vortex, from its axis), 0 = everywhere;
// `falloff` is the outer part of the radius where it fades out.
struct ParticleForce
{
    enum Type
    {
        Gravity = 0,
        Wind = 1,
        Turbulence = 2,
        Attractor = 3,
        Vortex = 4,
    };

    bool enabled = true;
    Type type = Gravity;
    float strength = 9.81f;
    float radius = 0.0f;   // world units, 0 = no limit
    float falloff = 0.5f;  // 0..1 of the radius
    float drag = 1.0f;     // wind: per second
    float frequency = 1.0f; // noise: features per world unit
    float speed = 0.5f;    // noise: change per second
    float noise = 0.0f;    // vortex: strength of its local swirls
    float pull = 0.0f;     // vortex: toward the axis (negative: outward), units / s^2
    float lift = 0.0f;     // vortex: along the axis, units / s^2

    bool operator==(const ParticleForce &) const = default;
};

// A box of gas simulated on a grid (engine/render/fluids.h): smoke, fire, dust. The box is
// centered on the entity and `size` world units along its local axes (it turns with the
// entity; the entity's scale is ignored). The grid has `resolution` voxels along the box's
// longest side, cubic voxels.
struct FluidVolume
{
    // How the fluid carries itself (fluid_advect*.comp): one semi-Lagrangian trace back, or
    // MacCormack: that, then a trace forward to measure the error and take half of it back
    // (sharper smoke and curls at the same resolution, one pass more).
    enum Advection
    {
        SemiLagrangian,
        MacCormack
    };

    bool enabled = true;
    QVector3D size { 4.0f, 4.0f, 4.0f };
    int resolution = 64;

    // Solver (the author's passes read these; see fluid_*.comp)
    float dissipation = 0.05f;  // fraction of the smoke lost per second
    float cooling = 0.5f;       // fraction of the heat lost per second
    float buoyancy = 2.0f;      // upward acceleration (units / s^2) per unit of temperature
    float weight = 0.2f;        // downward acceleration per unit of smoke
    float vorticity = 0.3f;     // vorticity confinement: how strongly small swirls are kept
    int pressureIterations = 20; // Jacobi iterations of the pressure solve, per step
    Advection advection = SemiLagrangian;
    float ambientLapse = 0.0f;  // stratified air: how much warmer it gets per world unit of
                                // height above the box's lowest point (0: the same everywhere);
                                // hot gas rises until it's no warmer than the air around it

    // Look (fluid_volume.frag)
    QColor color { 0xD0, 0xD0, 0xD8 };          // the smoke's scattering color (sRGB)
    QColor extinction { 0xFF, 0xFF, 0xFF };     // extinction per channel, relative to `density`
                                                 // (white: grey smoke; less red: the light that
                                                 // gets through, and the shadows, turn red)
    float density = 4.0f;                        // extinction per unit of smoke per world unit
    QColor emissionColor { 0xFF, 0x78, 0x28 };  // fire: the color of hot gas (sRGB)
    float emission = 0.0f;                       // fire: brightness per unit of temperature
    int samples = 96;                            // ray marching steps across the box

    // Detail (fluid_volume.frag, fluid_shadow.frag): a noise finer than the grid, carried by the
    // fluid through rest coordinates (FluidSystem: two half-resolution fields, reset in turn)
    float detail = 0.0f;       // how strongly it carves the smoke (0: none, no rest fields)
    float detailScale = 0.1f;  // the size of its swirls (world units)
    int detailPeriod = 48;     // steps between resets of each rest field: longer stretches the
                               // noise more, shorter makes its fading in and out show

    bool operator==(const FluidVolume &) const = default;
    // The same simulation: equal but for how it's drawn (look, detail strength and size). A change
    // to those doesn't simulate again; turning the detail on or off does (the rest fields).
    bool sameSimulation(const FluidVolume &o) const
    {
        FluidVolume a = *this, b = o;
        for (FluidVolume *f : { &a, &b }) {
            f->color = f->extinction = f->emissionColor = QColor();
            f->density = f->emission = f->detailScale = 0.0f;
            f->samples = 0;
            f->detail = f->detail > 0.0f ? 1.0f : 0.0f;
        }
        return a == b;
    }
};

// Where gas comes from: a sphere around the entity's position adding smoke, heat and velocity
// to every Fluid Volume it overlaps.
struct FluidSource
{
    bool enabled = true;
    float radius = 0.4f;                 // world units
    float density = 1.0f;                // smoke added per second, at the center
    float temperature = 1.0f;            // heat added per second, at the center
    QVector3D velocity { 0.0f, 0.0f, 0.0f }; // world units / s, given to the gas inside

    bool operator==(const FluidSource &) const = default;
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
    std::optional<ParticleEmitter> emitter; // set = this entity emits particles
    std::optional<ParticleForce> force;     // set = this entity pushes particles around
    std::optional<FluidVolume> fluid;       // set = this entity is a box of simulated gas
    std::optional<FluidSource> fluidSource; // set = this entity adds gas to the volumes it's in

    // Keyframes of its properties (engine/scene/animation.h), one track per animated property.
    std::vector<AnimationTrack> animation;
};
