#pragma once

#include <QList>
#include <QMatrix4x4>
#include <QPair>
#include <QVector3D>
#include <array>
#include <list>
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
//               (MacCormack: advect into the predicted fields, then advect_correct: velocity,
//               density, predicted -> velocity', density')
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
// after k steps from cleared fields; editing the volume or a source clears and simulates again;
// catching up is spread over frames within FrameBudget (pending()).
//
// The cache, two kinds of copies within setCacheBudget() bytes per volume, dropped on an edit:
//   frames     every frame as it's drawn, sparse, like a VDB grid: the grid in bricks of 8^3
//              voxels, only the bricks with something in them, quantized to 16 bits
//              (fluid_brick_pack.comp), into a pool of pages (PageBytes each) shared by the
//              volume's frames, with a table per frame of where its bricks are. The bricks take
//              the pool's slots in order from one counter on the GPU, without gaps; the counter,
//              read back a frame or two later, tells which pages to bind and when to make the
//              next. Smoke and temperature take two slots a brick; without emission (no fire to
//              draw) only the smoke is kept, one slot (changing that drops the frames). A cached
//              frame is drawn as it is, without simulating (the simulation stays where it was):
//              fluid_brick_unpack.comp makes it dense again in a texture of its own. Frames get
//              FrameShare of the budget.
//   snapshots  every `interval` steps all the fields (velocity, density, pressure,
//              fluid_copy.comp), exact (bit for bit), in the rest of the budget (the interval
//              grows with the timeline to fit): to simulate a frame that isn't cached, going back
//              in time or jumping ahead, the nearest snapshot at or before it is restored and only
//              the steps after it are simulated
// Snapshots make a frame the same as simulating from the start; cached frames differ only by
// the quantization (relative error below 0.01% for the smoke densities that show).
//
// Detail (FluidVolume::detail > 0): rest coordinates carried by the fluid (fluid_rest.comp, a pass
// at the end of each step), two fields at half the resolution, each reset to the identity every
// detailPeriod steps, half a period apart. The volume and shadow shaders read a noise at those
// coordinates and fade between the two fields (VolumeBuf.detail.y: A's weight, 0 at its reset).
// Snapshots and cached frames keep them too (one slot more a brick).
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
    // The cache (see above): the default budget per volume, the share of it for cached frames,
    // the fewest steps between two snapshots, one page of the brick pool.
    static constexpr quint64 DefaultCacheBudget = 2048ull * 1024 * 1024;
    static constexpr double FrameShare = 0.75;
    static constexpr int MinSnapshotInterval = 4;
    static constexpr int PageSlots = 65536;                          // must match fluid_brick_*.comp
    static constexpr quint64 PageBytes = 256ull * 256 * 256 * 4;     // a page: 256^3 RGBA8 texels

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
    // GPU memory the cache may take per volume (frames and snapshots). Less than a volume's
    // cache uses already drops it.
    void setCacheBudget(quint64 bytes);

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
    quint64 cacheBytes() const; // every volume's snapshots, pages and unpacked frame
    // The timeline frames every volume can show at once without simulating (cached, or the
    // simulation's own), as ranges of frames (first, last). Empty without volumes.
    QList<QPair<int, int>> cachedFrames() const;

private:
    enum Pass
    {
        Source,
        Advect,
        Forces,
        Divergence,
        Pressure,
        Project,
        AdvectPredict, // fluid_advect.comp into the predicted fields (MacCormack)
        AdvectCorrect, // fluid_advect_correct.comp
        Rest,          // fluid_rest.comp (detail)
        PassCount
    };
    // A volume's fields after some step: velocity, density, pressure (the formats of the pairs).
    struct Snapshot
    {
        std::array<std::unique_ptr<QRhiTexture>, 3> fields;
        std::array<std::unique_ptr<QRhiTexture>, 2> rest; // A, B, with detail
    };
    // A frame in the brick pool: its table (header and an entry per brick).
    struct CachedFrame
    {
        std::unique_ptr<QRhiBuffer> table;
        int page = 0;            // the first of the two pages it was packed with (Volume::pages)
        bool confirmed = false;  // its header read back, every brick with a slot: drawable
        bool rest = false;       // with the rest coordinates
        int bricks = 0;          // once confirmed
    };
    // Read back after a frame was packed: its table's header, or the pool counter (step -1).
    struct Readback
    {
        EntityId volume = InvalidEntity;
        quint64 generation = 0; // Volume::generation then: a dropped cache ignores it
        int step = 0;
        QRhiReadbackResult result;
        bool done = false;
    };
    struct Volume
    {
        Entity reference;                 // the entity at the first frame: a change means simulating again
        std::vector<Entity> sourceReference; // the same for the sources
        int nx = 0, ny = 0, nz = 0;
        float voxelSize = 1.0f;
        std::array<std::unique_ptr<QRhiTexture>, 2> velocity, density, pressure;
        std::unique_ptr<QRhiTexture> divergence;
        // MacCormack: the semi-Lagrangian result, before the correction (made when first used).
        std::unique_ptr<QRhiTexture> predictedVelocity, predictedDensity;
        // Detail: the rest fields, half resolution, a ping-pong pair each (made when first used).
        std::array<std::unique_ptr<QRhiTexture>, 2> restA, restB;
        int r = 0;                        // which texture of each rest pair is current
        int v = 0, d = 0, p = 0;          // which texture of each pair holds the current field
        std::unique_ptr<QRhiBuffer> stepUniforms; // MaxStepsPerFrame FluidBufs (dynamic offsets)
        std::unique_ptr<QRhiBuffer> clearUniforms;
        // Binding sets by pass and ping-pong state (each made for its pass's pipeline).
        std::map<int, std::unique_ptr<QRhiShaderResourceBindings>> srbs;
        std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> clearSrbs;
        std::unique_ptr<QRhiBuffer> drawUniforms;
        // By d * 2 + r (the density and rest fields drawn); 4: a cached frame.
        std::array<std::unique_ptr<QRhiShaderResourceBindings>, 5> drawSrbs;
        std::unique_ptr<QRhiBuffer> shadowUniforms; // ShadowMaps::ViewCount ShadowBufs (dynamic offsets)
        std::array<std::unique_ptr<QRhiShaderResourceBindings>, 5> shadowSrbs; // as drawSrbs
        int stepsDone = -1;               // steps in the fields (-1: clear first)
        std::map<int, Snapshot> snapshots; // by step (the fields after that many steps)
        quint64 snapshotBytes = 0;        // one snapshot's
        std::map<int, CachedFrame> frames; // by step
        std::vector<std::unique_ptr<QRhiTexture>> pages; // the brick pool (RGBA8: 2 x 16 bits, 256^3 voxels each)
        std::unique_ptr<QRhiBuffer> counter; // the pool's slots taken (one uint, on the GPU)
        quint32 knownSlots = 0;           // the counter as last read back (it's at least this)
        bool smokeOnly = false;           // how the frames are kept (no emission: smoke only)
        quint64 generation = 0;           // bumped when the cache is dropped
        std::unique_ptr<QRhiTexture> unpacked; // the cached frame being drawn, dense (RGBA16F)
        std::array<std::unique_ptr<QRhiTexture>, 2> unpackedRest; // and its rest fields
        int unpackedStep = -1;            // which (-1: none)
        bool shown = false;               // drawn from `unpacked` this frame (else the simulation's)
        bool seen = false;
        QMatrix4x4 model;                 // box space -> world, this frame
        FluidVolume settings;             // this frame's, for drawing
    };
    QRhiShaderResourceBindings *passBindings(Volume &v, Pass pass);
    // A copy (fluid_copy.comp): from the three textures to the other three. Its binding set is
    // retired at once (it lives until the frames using it are done).
    QRhiShaderResourceBindings *copyBindings(Volume &v, QRhiTexture *const from[3], QRhiTexture *const to[3]);
    // The same for the two rest fields (fluid_copy_rest.comp).
    QRhiShaderResourceBindings *copyRestBindings(Volume &v, QRhiTexture *const from[2], QRhiTexture *const to[2]);
    // Half-resolution rest field textures (two), for a snapshot, the pairs or a cached frame.
    bool makeRest(const Volume &v, std::array<std::unique_ptr<QRhiTexture>, 2> &out);
    void dropSnapshots(Volume &v); // and the frames
    void dropFrames(Volume &v);
    void retire(Volume &v);
    template<typename T> void retire(std::unique_ptr<T> &resource)
    {
        if (resource)
            m_retired.emplace_back(resource.release());
    }

    QRhi *m_rhi = nullptr;
    bool m_supported = false;
    std::array<std::unique_ptr<QRhiComputePipeline>, PassCount> m_pipes;
    std::unique_ptr<QRhiComputePipeline> m_clearPipe, m_copyPipe, m_copyRestPipe, m_packPipe, m_unpackPipe;
    std::unique_ptr<QRhiShaderResourceBindings> m_passLayout, m_clearLayout, m_copyLayout, m_copyRestLayout, m_packLayout,
        m_unpackLayout, m_drawLayout, m_shadowLayout;

    std::unique_ptr<QRhiBuffer> m_layoutStorage; // for the pack/unpack layouts
    quint64 m_cacheBudget = DefaultCacheBudget;
    std::list<Readback> m_readbacks;
    int m_timelineStart = 0; // of the last simulate()
    std::unique_ptr<QRhiBuffer> m_layoutUniforms;
    std::unique_ptr<QRhiTexture> m_layoutVolume;    // 1x1x1, for the layouts and unused inputs
    std::unique_ptr<QRhiTexture> m_spareBrickPage;  // 1x1x1 RGBA8: a brick pass's second page when there's one
    std::unique_ptr<QRhiTexture> m_spareRestOut;    // 1x1x1 RGBA16F: an unpack's second rest output, unused
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
