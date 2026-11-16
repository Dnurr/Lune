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
#include "iblmaps.h"
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
};

// Scene renderer, independent from any widget: it only needs a QRhi and a render target
// (an editor viewport today, an offline render target tomorrow).
//
// Frame:
//   0. ibl     -> environment maps, only when the environment changed (IblMaps)
//      shadow  -> depth of every mesh seen from each shadowed light (ShadowMaps)
//   1. scene   -> environment background (optional), world grid, meshes (depth tested),
//                 selection outline                                         [HDR, linear]
//   2. display -> scene converted for the screen (future tonemapping), then
//                 the 2D overlay (gizmo) on top, in display colors            [8 bit, sRGB]
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

    const FrameStats &lastFrameStats() const { return m_stats; }
    const char *backendName() const;

    // Node materials: why a material can't be drawn (graph or shader compile error, empty
    // if it's fine or wasn't drawn yet). Entities using it fall back to their own values.
    QString materialError(MaterialId id) const { return m_materialErrors.value(id); }
    // True once after a frame that changed some material's error, or in which an image
    // failed to load (e.g. to refresh the UI).
    bool takeMaterialErrorsChanged();

    // Why a material image couldn't be loaded (empty if fine, loading, or never used).
    QString textureError(const QString &image) const { return m_textures.error(image); }
    // Something the last frame drew is still loading (textures): render again soon.
    bool needsAnotherFrame() const { return m_textures.loading(); }

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
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;      // opaque and clip
        std::unique_ptr<QRhiGraphicsPipeline> blendPipeline; // blend: blended over the scene, no depth writes
        bool shadowCompiled = false;
        QShader shadowFragment;
        QString shadowError;
        std::unique_ptr<QRhiGraphicsPipeline> clipShadowPipeline, translucentShadowPipeline;
    };
    // GLSL (for the backend in use) -> shader, or an invalid one and `error`.
    QShader compileFragment(const QString &source, const QString &name, QString *error) const;
    // Mesh pipeline with another fragment shader (node materials); same state as m_meshPipe,
    // or blended (premultiplied alpha, depth tested but not written).
    std::unique_ptr<QRhiGraphicsPipeline> createMeshPipeline(const QShader &fragment, bool blend);
    // The shader for a material's generated code: compiled on first use, then cached.
    MaterialShader &materialShader(const QString &code);
    // Its scene pipeline; nullptr (and `error` set) if it doesn't compile.
    QRhiGraphicsPipeline *scenePipeline(MaterialShader &shader, bool blend, QString *error);
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
    int m_tonemapper = 0;
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

    // Offscreen targets:
    //   scene   full size, HDR (RGBA16F) + depth, linear values
    //   display full size, RGBA8, display (sRGB) values: what ends up on screen
    //   blur    two reduced-size RGBA8 ping-pong textures
    std::unique_ptr<QRhiTexture> m_sceneTex, m_displayTex, m_blurA, m_blurB;
    std::unique_ptr<QRhiRenderBuffer> m_sceneDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneRt, m_displayRt, m_blurARt, m_blurBRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_sceneRp; // color + depth
    std::unique_ptr<QRhiRenderPassDescriptor> m_ldrRp;   // RGBA8 color only: display and blur targets

    std::unique_ptr<QRhiShaderResourceBindings> m_gridSrb, m_displaySrb, m_downSrb, m_compositeSrb, m_overlaySrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_backgroundSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_meshLayoutSrb;    // layout of the per-entity SRBs
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineLayoutSrb; // layout of the per-entity outline SRBs
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> m_blurSrbs;

    std::unique_ptr<QRhiGraphicsPipeline> m_gridPipe, m_meshPipe, m_outlinePipe, m_overlayPipe, m_backgroundPipe;
    std::unique_ptr<QRhiGraphicsPipeline> m_displayPipe, m_downPipe, m_blurPipe, m_compositePipe;

    std::unordered_map<const Mesh *, GpuMesh> m_meshes;
    std::unordered_map<EntityId, GpuEntity> m_entities;
};
