#pragma once

#include <QList>
#include <QRectF>
#include <QSize>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/scene/entity.h"

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
//   1. scene  -> world grid, meshes (depth tested), selection outline, 2D overlay
//   2. blur   -> scene downsampled to 1/4 and blurred (Gaussian, 2 iterations)
//   3. final  -> scene, with the blurred copy inside the "glass" rectangles
//
// Usage:
//   initialize(rhi, target)              on the first frame and whenever the target/QRhi changes
//   render(cb, target, camera, scene...) every frame
//   release()                            when the QRhi goes away
class Renderer
{
public:
    static constexpr int MaxGlassRects = 16; // must match MAX_RECTS in composite.frag

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

    const FrameStats &lastFrameStats() const { return m_stats; }
    const char *backendName() const;

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
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };

    void createSizeDependentResources();
    void createPipelines(QRhiRenderTarget *target);
    GpuMesh &gpuMesh(const MeshPtr &mesh, QRhiResourceUpdateBatch *rub);
    GpuEntity &gpuEntity(EntityId id);

    QRhi *m_rhi = nullptr;
    QSize m_pixelSize;

    QList<QRectF> m_glassRects;
    float m_glassRadius = 0.0f;
    FrameStats m_stats;
    std::vector<OverlayVertex> m_overlay;
    float m_overlayScale = 1.0f;

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_gridUbuf;
    std::unique_ptr<QRhiBuffer> m_downUbuf;
    std::array<std::unique_ptr<QRhiBuffer>, 4> m_blurUbufs; // H, V, H, V
    std::unique_ptr<QRhiBuffer> m_compositeUbuf;
    std::unique_ptr<QRhiBuffer> m_overlayUbuf;
    std::unique_ptr<QRhiBuffer> m_overlayVbuf;
    std::unique_ptr<QRhiBuffer> m_layoutUbuf; // only gives m_meshLayoutSrb a buffer to point to

    // Offscreen targets: full-size scene (+ depth), two 1/4-size ping-pong textures for the blur.
    std::unique_ptr<QRhiTexture> m_sceneTex, m_blurA, m_blurB;
    std::unique_ptr<QRhiRenderBuffer> m_sceneDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneRt, m_blurARt, m_blurBRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_sceneRp; // color + depth
    std::unique_ptr<QRhiRenderPassDescriptor> m_blurRp;  // color only, shared by both blur targets

    std::unique_ptr<QRhiShaderResourceBindings> m_gridSrb, m_downSrb, m_compositeSrb, m_overlaySrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_meshLayoutSrb; // layout of the per-entity SRBs
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> m_blurSrbs;

    std::unique_ptr<QRhiGraphicsPipeline> m_gridPipe, m_meshPipe, m_outlinePipe, m_overlayPipe;
    std::unique_ptr<QRhiGraphicsPipeline> m_downPipe, m_blurPipe, m_compositePipe;

    std::unordered_map<const Mesh *, GpuMesh> m_meshes;
    std::unordered_map<EntityId, GpuEntity> m_entities;
};
