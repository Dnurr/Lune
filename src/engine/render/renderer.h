#pragma once

#include <QHash>
#include <QList>
#include <QRectF>
#include <QSize>
#include <rhi/qshader.h>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/material/materialcompiler.h"
#include "engine/scene/entity.h"
#include "fluids.h"
#include "iblmaps.h"
#include "particles.h"
#include "postprocess.h"
#include "shadowmaps.h"
#include "texturecache.h"

class OrbitCamera;
class Scene;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBinding;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

// One vertex of the 2D overlay (gizmos): position in widget pixels (origin top-left)
// and a straight (non-premultiplied) RGBA color.
struct OverlayVertex
{
    float x, y;
    float r, g, b, a;
};

// What the last frame did (for the stats overlay).
struct FrameStats
{
    int drawCalls = 0;      // every draw, fullscreen passes included
    quint64 triangles = 0;  // mesh triangles (selection outline included)
    int meshes = 0;         // visible entities with a mesh
    int textures = 0;       // material textures bound (with repeats), fallbacks excluded
    int renderPasses = 0;
    int emitters = 0;       // particle emitters drawn
    quint64 particles = 0;  // particles alive (estimated, see ParticleSystem)
    int particleSteps = 0;  // simulation steps run this frame
    int fluids = 0;         // fluid volumes drawn
    quint64 voxels = 0;     // their voxels
    int fluidSteps = 0;     // fluid steps run this frame
    quint64 fluidCache = 0; // bytes of the fluid cache's snapshots
};

// How meshes are shaded (the viewport's shading menu, and each pass of an image render).
enum class Shading
{
    Wireframe, // edges only (see-through), no surfaces
    Clay,      // every mesh with one plain gray material, scene lights and environment
    Unlit,     // materials' base color only (mesh.frag: DebugView::Unlit)
    Preview,   // materials lit by the environment only (no scene lights, no shadows)
    Rendered,  // everything
};

// What mesh.frag's main() writes instead of the shaded color: lightCount.y in the lights
// block. Implemented there (until it is, every view looks like None).
enum class DebugView
{
    None = 0,
    Unlit = 1,       // the base color, no lighting
    Normals = 2,     // the shading normal (world space), as 0.5 + 0.5 * N
    Depth = 3,       // linear distance from the camera
    DirectOnly = 4,  // the scene lights only (no environment)
    ShadowsOnly = 5, // the lights' shadow factors (white = lit)
};

struct ViewSettings
{
    Shading shading = Shading::Rendered;
    bool skybox = true;         // an environment with `background` on shows it; off: the plain sky
    bool wireOverlay = false;   // edges drawn over the shaded meshes
    bool post = true;           // Rendered: the scene camera's Post Process effects (PostProcessor)
    DebugView debug = DebugView::None; // Shading::Unlit always uses DebugView::Unlit

    bool operator==(const ViewSettings &) const = default;
};

// Scene renderer, independent from any widget: it only needs a QRhi and a render target
// (an editor viewport, or the offscreen target of an image render: imagerender.h).
//
// Frame:
//   0. ibl     -> environment maps, only when the environment changed (IblMaps)
//      particles -> emitters simulated up to the scene time (ParticleSystem, compute)
//      shadow  -> depth of every mesh seen from each shadowed light (ShadowMaps)
//   1. scene   -> environment background (optional), world grid, meshes (depth tested),
//                 selection outline                                         [HDR, linear]
//      distance-> with particles: the scene's depth as view depth (scene_distance.frag), for
//                 soft particles
//      particles-> the scene again, kept (not cleared): fluid volumes (ray marched), then the
//                 particles, depth tested
//      post    -> HDR effects of the scene camera's Post Process: depth of field, bloom
//                 (PostProcessor; Rendered shading with ViewSettings::post only)
//   2. display -> scene converted for the screen (exposure, tonemapping), then the display
//                 effects (FXAA, chromatic aberration, vignette, film grain), then the
//                 2D overlay (gizmo) on top, in display colors                [8 bit, sRGB]
//   3. blur    -> display image downsampled and blurred (Gaussian, 2 iterations)
//   4. final   -> display image, with the blurred copy inside the "glass" rectangles
//
// Usage:
//   initialize(rhi, target)              on the first frame and whenever the target/QRhi changes
//   render(cb, target, camera, scene...) every frame
//   release()                            when the QRhi goes away
class Renderer
{
public:
    static constexpr int MaxGlassRects = 16; // must match MAX_RECTS in composite.frag
    static constexpr int MaxLights = 8;      // must match MAX_LIGHTS in mesh.frag; extra lights are ignored
    static_assert(MaxLights == ShadowMaps::MaxLights);

    Renderer();
    ~Renderer();

    void initialize(QRhi *rhi, QRhiRenderTarget *target);
    void render(QRhiCommandBuffer *cb, QRhiRenderTarget *target, const OrbitCamera &camera, const Scene *scene,
                EntityId selected);
    void release();

    // Areas (in target pixels, origin top-left) that show the blurred scene,
    // with rounded corners. Used for the editor's floating panels.
    void setGlassRects(const QList<QRectF> &rects, float cornerRadius);

    // Triangles drawn on top of the scene. `pixelScale` converts their coordinates
    // to target pixels (the widget's device pixel ratio).
    void setOverlay(std::vector<OverlayVertex> vertices, float pixelScale);

    // Parameters of the display pass (display.frag): params.x = exposure in EV,
    // params.y = tonemapping operator index.
    void setDisplayParams(float exposureEv, int tonemapper);

    // Scene time in seconds (Timeline::seconds() of the frame shown): `viewport.w` in the
    // mesh shaders' uniforms, `camera.w` in the post-processing shaders.
    void setTime(float seconds) { m_seconds = seconds; }
    float time() const { return m_seconds; }

    // The world grid's lines (on by default). Off: the grid pass still draws its sky (when
    // there's no environment background), without lines.
    void setGridLinesVisible(bool visible) { m_gridLines = visible; }

    // Shading mode, background and debug outputs (default: everything, as rendered).
    void setViewSettings(const ViewSettings &settings) { m_view = settings; }
    const ViewSettings &viewSettings() const { return m_view; }
    // False where the backend can't draw lines from triangles (OpenGL ES): Wireframe then
    // draws like Clay and wireOverlay is ignored.
    bool wireframeSupported() const;

    const FrameStats &lastFrameStats() const { return m_stats; }
    const char *backendName() const;

    // Node materials: why a material can't be drawn (graph or shader compile error, empty
    // if it's fine or wasn't drawn yet). Entities using it fall back to their own values.
    QString materialError(MaterialId id) const { return m_materialErrors.value(id); }
    // Particle graphs: why an emitter's graph can't be used (empty if fine). Changes are
    // reported by takeMaterialErrorsChanged() too.
    QString particleGraphError(EntityId emitter) const { return m_particles.error(emitter); }
    // True once after a frame that changed some material's error, or in which an image
    // failed to load (e.g. to refresh the UI).
    bool takeMaterialErrorsChanged();

    // Why a material image couldn't be loaded (empty if fine, loading, or never used).
    QString textureError(const QString &image) const { return m_textures.error(image); }
    // Something the last frame drew is still loading (textures): render again soon.
    // Also while particles catch up with a long jump in time (ParticleSystem::pending()).
    bool needsAnotherFrame() const { return m_textures.loading() || m_particles.pending() || m_fluids.pending(); }

private:
    struct GpuMesh
    {
        MeshPtr mesh; // keeps the Mesh alive while it's on the GPU
        std::unique_ptr<QRhiBuffer> vertices, indices;
        quint32 indexCount = 0;
    };
    using TextureSet = std::array<QRhiTexture *, MaterialCompiler::MaxTextures>; // materialTexture0..7

    // One material slot of an entity (a submesh): the entity uniforms with that slot's
    // material values, and the scene binding set with its textures.
    struct GpuSlot
    {
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> srb; // scene pass (meshBindings)
        TextureSet textures {};                          // what srb binds at 11..18
        // Shadow passes of a clip or blend material (casterBindings below); made when first needed.
        std::unique_ptr<QRhiShaderResourceBindings> shadowSrb;
    };
    struct GpuEntity
    {
        std::vector<GpuSlot> materialSlots; // one per submesh; materialSlots[0]'s uniforms also serve the passes below
        std::unique_ptr<QRhiShaderResourceBindings> shadowSrb; // shadow passes (ShadowMaps::casterBindings)
        // Selection outline: entity uniforms only. Not the scene SRB: on Direct3D 11 the
        // registers of a binding set are resolved for the first pipeline that uses it, and
        // mesh.frag and outline.frag put the entity block in different registers.
        std::unique_ptr<QRhiShaderResourceBindings> outlineSrb;
    };

    void createSizeDependentResources();
    void createPipelines(QRhiRenderTarget *target);
    GpuMesh &gpuMesh(const MeshPtr &mesh, QRhiResourceUpdateBatch *rub);
    GpuEntity &gpuEntity(EntityId id, int slotCount);
    GpuSlot newSlot();
    // Points the slot's binding sets at these textures (rebuilt only if they changed).
    void bindTextures(GpuSlot &slot, const TextureSet &textures);
    TextureSet fallbackTextures() const; // white everywhere
    // Bindings of every mesh SRB: entity uniforms, what the whole scene shares, then the
    // material's textures. `forCasters`: see ShadowMaps::meshBindings().
    QList<QRhiShaderResourceBinding> meshBindings(QRhiBuffer *entityUniforms, const TextureSet &textures,
                                                  bool forCasters = false) const;
    // The same for a material's shadow passes: shadow maps replaced by stand-ins
    // (ShadowMaps::meshBindings(true)), plus the shadow view.
    QList<QRhiShaderResourceBinding> casterBindings(QRhiBuffer *entityUniforms, const TextureSet &textures) const;
    QRhiShaderResourceBindings *slotShadowSrb(GpuSlot &slot);

    // A node material shader, by generated code. Scene pipelines are rebuilt after
    // initialize() (they reference the scene render pass); compiled shaders are kept. The
    // shadow variant is compiled only for materials that need it (clip, blend).
    struct MaterialShader
    {
        QShader fragment;
        QString error;
        std::unique_ptr<QRhiGraphicsPipeline> pipeline; // opaque and clip
        // Blend: blended over the scene, no depth writes; by MeshCull (two-sided, or one side
        // for closed meshes, drawn back faces first).
        std::array<std::unique_ptr<QRhiGraphicsPipeline>, 3> blendPipelines;
        bool shadowCompiled = false;
        QShader shadowFragment;
        QString shadowError;
        std::unique_ptr<QRhiGraphicsPipeline> clipShadowPipeline, translucentShadowPipeline;
    };
    // GLSL (for the backend in use) -> shader, or an invalid one and `error`.
    QShader compileFragment(const QString &source, const QString &name, QString *error) const;
    enum MeshCull { CullNone, CullFront, CullBack };
    // Mesh pipeline with another fragment shader (node materials); same state as m_meshPipe,
    // or blended (premultiplied alpha, depth tested but not written, culling `cull`).
    std::unique_ptr<QRhiGraphicsPipeline> createMeshPipeline(const QShader &fragment, bool blend, MeshCull cull = CullNone);
    // The shader for a material's generated code: compiled on first use, then cached.
    MaterialShader &materialShader(const QString &code);
    // Its scene pipeline; nullptr (and `error` set) if it doesn't compile.
    QRhiGraphicsPipeline *scenePipeline(MaterialShader &shader, bool blend, QString *error, MeshCull cull = CullNone);
    // Its shadow pipeline (clip: depth atlases, translucent: translucency map); nullptr
    // (and `error` set) if it doesn't compile.
    QRhiGraphicsPipeline *shadowPipeline(MaterialShader &shader, const QString &code, bool translucent, QString *error);

    std::unordered_map<QString, MaterialShader> m_materialShaders;
    QString m_meshFragSource;      // mesh.frag as GLSL, with the material markers
    QString m_shadowMaterialSource; // shadow_material.glsl
    std::unique_ptr<QRhiShaderResourceBindings> m_shadowLayoutSrb; // layout of the slots' shadowSrb
    QHash<MaterialId, QString> m_materialErrors;
    bool m_materialErrorsChanged = false;


    QRhi *m_rhi = nullptr;
    QSize m_pixelSize;

    QList<QRectF> m_glassRects;
    float m_glassRadius = 0.0f;
    FrameStats m_stats;
    float m_exposure = 0.0f;
    float m_seconds = 0.0f;
    int m_tonemapper = 0;
    bool m_gridLines = true;
    ViewSettings m_view;
    std::vector<OverlayVertex> m_overlay;
    float m_overlayScale = 1.0f;

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_gridUbuf;
    std::unique_ptr<QRhiBuffer> m_downUbuf;
    std::array<std::unique_ptr<QRhiBuffer>, 4> m_blurUbufs; // H, V, H, V
    std::unique_ptr<QRhiBuffer> m_compositeUbuf;
    std::unique_ptr<QRhiBuffer> m_displayUbuf; // free parameters for the display pass (exposure, tonemapping...)
    std::unique_ptr<QRhiBuffer> m_overlayUbuf;
    std::unique_ptr<QRhiBuffer> m_overlayVbuf;
    std::unique_ptr<QRhiBuffer> m_layoutUbuf; // only gives m_meshLayoutSrb a buffer to point to
    std::unique_ptr<QRhiBuffer> m_lightsUbuf; // the frame's lights, binding 1 of every mesh SRB
    std::unique_ptr<QRhiBuffer> m_envUbuf;    // environment rotation and parameters, binding 2 of every mesh SRB
    std::unique_ptr<QRhiBuffer> m_backgroundUbuf;

    ShadowMaps m_shadows; // shadow atlases, bindings 6..10 of every mesh SRB
    TextureCache m_textures; // material images, bindings 11..18 of the mesh SRBs

    IblMaps m_ibl; // environment cubemaps and BRDF LUT, bindings 3..5 of every mesh SRB

    PostProcessor m_post; // post-processing targets and passes
    ParticleSystem m_particles; // emitters' buffers, simulation and draws
    FluidSystem m_fluids;       // fluid volumes' grids, solver passes and draws

    // Offscreen targets:
    //   scene   full size, HDR (RGBA16F) + depth, linear values
    //   display full size, RGBA8, display (sRGB) values: what ends up on screen
    //   blur    two reduced-size RGBA8 ping-pong textures
    std::unique_ptr<QRhiTexture> m_sceneTex, m_displayTex, m_blurA, m_blurB;
    std::unique_ptr<QRhiRenderBuffer> m_sceneDepth;   // where depth can't be sampled
    std::unique_ptr<QRhiTexture> m_sceneDepthTex;     // otherwise: post effects read it (depth of field)
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneRt, m_displayRt, m_blurARt, m_blurBRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_sceneRp; // color + depth
    // The scene's targets without clearing them: the particles' pass, after the distance pass.
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneKeepRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_sceneKeepRp;
    // The scene's view depth (one float per pixel), for soft particles; only where the depth
    // can be sampled (m_sceneDepthTex).
    std::unique_ptr<QRhiTexture> m_distanceTex;
    std::unique_ptr<QRhiTextureRenderTarget> m_distanceRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_distanceRp;
    std::unique_ptr<QRhiShaderResourceBindings> m_distanceSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_distancePipe;
    std::unique_ptr<QRhiBuffer> m_distanceUbuf;
    std::unique_ptr<QRhiRenderPassDescriptor> m_ldrRp;   // RGBA8 color only: display and blur targets

    std::unique_ptr<QRhiShaderResourceBindings> m_gridSrb, m_displaySrb, m_downSrb, m_compositeSrb, m_overlaySrb;
    // The display pass reading the HDR post targets (PostProcessor::hdrTexture(0/1)) instead of the scene.
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> m_displayPostSrbs;
    std::unique_ptr<QRhiShaderResourceBindings> m_backgroundSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_meshLayoutSrb;    // layout of the per-entity SRBs
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineLayoutSrb; // layout of the per-entity outline SRBs
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> m_blurSrbs;

    std::unique_ptr<QRhiGraphicsPipeline> m_gridPipe, m_meshPipe, m_outlinePipe, m_overlayPipe, m_backgroundPipe;
    // Edges (polygon mode Line, wireframe.vert/.frag): Shading::Wireframe, and over the
    // shaded meshes (depth tested, pulled toward the camera). Null where unsupported.
    std::unique_ptr<QRhiGraphicsPipeline> m_wirePipe, m_wireOverPipe;
    std::unique_ptr<QRhiGraphicsPipeline> m_displayPipe, m_downPipe, m_blurPipe, m_compositePipe;

    std::unordered_map<const Mesh *, GpuMesh> m_meshes;
    std::unordered_map<EntityId, GpuEntity> m_entities;
};
