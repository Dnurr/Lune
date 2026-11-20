#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <array>
#include <map>
#include <memory>
#include <rhi/qrhi.h>
#include <unordered_map>
#include <vector>

#include "engine/scene/entity.h"
#include "shadowmaps.h"

class Scene;

// Gas simulated on grids: the Fluid Volume components of the scene (smoke, fire, dust), fed by
// the Fluid Source components, drawn by ray marching.
//
// Each volume has 3D textures for its fields, at `resolution` voxels along the box's longest
// side: velocity and density (x = smoke, y = temperature) as RGBA16F pairs, pressure as an R32F
// pair, divergence as one R32F. Every pass reads its inputs as sampled textures (linear filtering
// for the semi-Lagrangian advection) and writes its output into the other texture of a pair as
// a storage image, so nothing is read and written at once; then the pair swaps.
//
// A step of dt = one timeline frame runs, in order (shaders/fluid_*.comp, the author's):
//
//   source      velocity, density  -> velocity', density'   the Fluid Sources over dt
//   advect      velocity, density  -> velocity', density'   the fluid carries itself
//   forces      velocity, density  -> velocity'             buoyancy, weight, vorticity confinement
//   divergence  velocity           -> divergence
//   pressure    pressure, divergence -> pressure'            x FluidVolume::pressureIterations
//   project     velocity, pressure -> velocity'             the divergence taken out
//
// All the passes of a step read one FluidBuf (the step's dt, time, solver settings, sources in
// grid space: the volume and the sources as they are at that frame, animated), through a dynamic
// offset into a buffer of the frame's steps.
//
// Like the particles, the simulation follows the timeline: frame start + k shows the fields
// after k steps from cleared fields; going back in time or editing the volume or a source
// clears and simulates again; catching up is spread over frames within FrameBudget (pending()).
//
// Drawing: the box (fluid_volume.vert: 36 vertices, front faces culled) with fluid_volume.frag
// marching through the density, over the scene with premultiplied alpha, before the particles;
// it reads the scene's view depth (the Renderer's distance pass) to stop at surfaces.
//
// Shadows: each volume is also drawn into the shadow maps' translucency map, in every tile of
// every shadowed light (fluid_shadow.vert: the box's faces toward the light; fluid_shadow.frag,
// the author's: the light that gets through the volume, per channel, and the linear depth where
// the light enters it). mesh.frag then dims, and tints, what lies behind the smoke.
class FluidSystem
{
public:
    static constexpr int MaxSources = 8;          // per step; must match MAX_FLUID_SOURCES in fluid_*.comp
    static constexpr int GroupSize = 8;           // local size of fluid_*.comp, on each axis
    static constexpr int MaxStepsPerFrame = 120;
    // GPU work one frame may take, in voxel passes (voxels x passes of a step): catching up
    // after a jump or an edit is spread over frames under it.
    static constexpr double FrameBudget = 4.0e8;

    void initialize(QRhi *rhi);
    // Pipelines for drawing into the scene's kept pass (Renderer::m_sceneKeepRp).
    void createPipelines(QRhiRenderPassDescriptor *sceneRp, int sampleCount);
    void release();
    // False without compute shaders or 3D textures: no fluids then.
    bool supported() const { return m_supported; }
    // The scene's view depth per pixel (see ParticleSystem::setSceneDistance()); null: a stand-in.
    void setSceneDistance(QRhiTexture *texture);
    // The shadow maps the volumes test the main light's shadows in (fluid_volume.frag's
    // shadowBuf, dirShadowMap). Set before drawing.
    void setShadowMaps(const ShadowMaps *shadows);

    // Brings every enabled volume of `scene` to scene time `seconds`: a compute pass. Outside any
    // render pass, before the scene pass.
    void simulate(QRhiCommandBuffer *cb, const Scene *scene, float seconds);
    // The volumes' draw uniforms. `lightDirection`: where the main light shines (unit, world);
    // `lightColor`: its radiance, linear; `hasLight` false: none. `shadowLight`: its index in
    // mesh.frag's lights[] (for its shadows), -1 for none.
    void prepareDraw(QRhiResourceUpdateBatch *rub, const QMatrix4x4 &clipViewProj, const QVector3D &eye,
                     const QVector3D &lightDirection, const QVector3D &lightColor, bool hasLight, int shadowLight,
                     const QSize &viewportPixels, float seconds);
    // Draws every volume, inside the scene's kept pass.
    void draw(QRhiCommandBuffer *cb, const QRhiViewport &viewport);

    // Shadows into the translucency map (see ShadowMaps::translucentViews()): the pipeline for
    // its render pass, the uniforms for the tiles of this frame (after simulate()), and the
    // draws, inside the translucency pass.
    void createShadowPipeline(QRhiRenderPassDescriptor *translucencyRp);
    void prepareShadows(QRhiResourceUpdateBatch *rub, const std::vector<ShadowMaps::TranslucentView> &views);
    void drawShadows(QRhiCommandBuffer *cb);
    int shadowDrawCount() const;

    bool pending() const { return m_pending; }
    int volumeCount() const { return m_drawn; }
    quint64 voxelCount() const { return m_voxels; }
    int stepsLastFrame() const { return m_stepsLastFrame; }

private:
    enum Pass
    {
        Source,
        Advect,
        Forces,
        Divergence,
        Pressure,
        Project,
        PassCount
    };
    struct Volume
    {
        Entity reference;                 // the entity at the first frame: a change means simulating again
        std::vector<Entity> sourceReference; // the same for the sources
        int nx = 0, ny = 0, nz = 0;
        float voxelSize = 1.0f;
        std::array<std::unique_ptr<QRhiTexture>, 2> velocity, density, pressure;
        std::unique_ptr<QRhiTexture> divergence;
        int v = 0, d = 0, p = 0;          // which texture of each pair holds the current field
        std::unique_ptr<QRhiBuffer> stepUniforms; // MaxStepsPerFrame FluidBufs (dynamic offsets)
        std::unique_ptr<QRhiBuffer> clearUniforms;
        // Binding sets by pass and ping-pong state (each made for its pass's pipeline).
        std::map<int, std::unique_ptr<QRhiShaderResourceBindings>> srbs;
        std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> clearSrbs;
        std::unique_ptr<QRhiBuffer> drawUniforms;
        std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> drawSrbs; // by d
        std::unique_ptr<QRhiBuffer> shadowUniforms; // ShadowMaps::ViewCount ShadowBufs (dynamic offsets)
        std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> shadowSrbs; // by d
        int stepsDone = -1;               // steps in the fields (-1: clear first)
        bool seen = false;
        QMatrix4x4 model;                 // box space -> world, this frame
        FluidVolume settings;             // this frame's, for drawing
    };
    QRhiShaderResourceBindings *passBindings(Volume &v, Pass pass);
    void retire(Volume &v);
    template<typename T> void retire(std::unique_ptr<T> &resource)
    {
        if (resource)
            m_retired.emplace_back(resource.release());
    }

    QRhi *m_rhi = nullptr;
    bool m_supported = false;
    std::array<std::unique_ptr<QRhiComputePipeline>, PassCount> m_pipes;
    std::unique_ptr<QRhiComputePipeline> m_clearPipe;
    std::unique_ptr<QRhiShaderResourceBindings> m_passLayout, m_clearLayout, m_drawLayout, m_shadowLayout;
    std::unique_ptr<QRhiBuffer> m_layoutUniforms;
    std::unique_ptr<QRhiTexture> m_layoutVolume;    // 1x1x1, for the layouts and unused inputs
    std::unique_ptr<QRhiTexture> m_distanceStandIn; // 1x1
    QRhiTexture *m_sceneDistance = nullptr;         // not owned
    const ShadowMaps *m_shadowMaps = nullptr;       // not owned
    std::unique_ptr<QRhiSampler> m_linear, m_nearest;
    std::unique_ptr<QRhiGraphicsPipeline> m_drawPipe;
    // OpenGL: Qt's backend has no dual-source blend factors, so each volume is drawn twice
    // with single-output variants of fluid_volume.frag: m_drawPipe multiplies what's behind by
    // the transmittance, m_drawAddPipe adds the light (see createPipelines()).
    std::unique_ptr<QRhiGraphicsPipeline> m_drawAddPipe;
    std::unique_ptr<QRhiGraphicsPipeline> m_shadowPipe;
    std::vector<QRectF> m_shadowViewports; // the tiles of the last prepareShadows()
    quint32 m_shadowStride = 0;
    std::unordered_map<EntityId, Volume> m_volumes;
    std::vector<std::unique_ptr<QRhiResource>> m_retired, m_retiredBefore;
    QRhiResourceUpdateBatch *m_pendingUpload = nullptr;
    bool m_pending = false;
    int m_drawn = 0;
    quint64 m_voxels = 0;
    int m_stepsLastFrame = 0;
};
