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

#include "engine/scene/entity.h"
#include "iblmaps.h"
#include "shadowmaps.h"

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
    // True once after a frame that changed some material's error (e.g. to refresh the UI).
    bool takeMaterialErrorsChanged();

private:
    struct GpuMesh
    {
        MeshPtr mesh; // keeps the Mesh alive while it's on the GPU
        std::unique_ptr<QRhiBuffer> vertices, indices;
        quint32 indexCount = 0;
    };
    struct GpuEntity
    {
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> srb;       // scene pass (meshBindings)
        std::unique_ptr<QRhiShaderResourceBindings> shadowSrb; // shadow passes (ShadowMaps::casterBindings)
        // Selection outline: entity uniforms only. Not the scene SRB: on Direct3D 11 the
        // registers of a binding set are resolved for the first pipeline that uses it, and
        // mesh.frag and outline.frag put the entity block in different registers.
        std::unique_ptr<QRhiShaderResourceBindings> outlineSrb;
    };

    void createSizeDependentResources();
    void createPipelines(QRhiRenderTarget *target);
    GpuMesh &gpuMesh(const MeshPtr &mesh, QRhiResourceUpdateBatch *rub);
    GpuEntity &gpuEntity(EntityId id);
    // Bindings of every mesh SRB: entity uniforms, then what the whole scene shares.
    QList<QRhiShaderResourceBinding> meshBindings(QRhiBuffer *entityUniforms) const;
    // Mesh pipeline with another fragment shader (node materials); same state as m_meshPipe.
    std::unique_ptr<QRhiGraphicsPipeline> createMeshPipeline(const QShader &fragment);
    // The pipeline for a material's generated code: compiled on first use, then cached.
    // nullptr (and `error` set) if it doesn't compile.
    QRhiGraphicsPipeline *materialPipeline(const QString &code, QString *error);

    // A node material shader, by generated code. The pipeline is rebuilt after initialize()
    // (it references the scene render pass); the compiled shader is kept.
    struct MaterialShader
    {
        QShader fragment;
        QString error;
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    };
    std::unordered_map<QString, MaterialShader> m_materialShaders;
    QString m_meshFragSource; // mesh.frag as GLSL, with the material markers
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
