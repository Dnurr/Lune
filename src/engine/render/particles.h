#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <memory>
#include <rhi/qrhi.h>
#include <unordered_map>
#include <vector>

#include "engine/scene/entity.h"

class Scene;

// GPU particles: the Particle Emitter components of the scene, simulated by a compute shader
// (shaders/particles.comp) and drawn as instanced quads (shaders/particle.vert/.frag).
//
// Each emitter has a ring buffer of `maxParticles` particles (64 bytes each, see Particle in
// particles.comp), which is both the compute shader's storage buffer and the instance vertex
// buffer of the draw. QRhi orders the compute pass before the draws that read the buffer.
//
// The simulation follows the timeline: it advances in fixed steps of one frame (1 / fps) from
// the timeline's start, so the particles at a frame depend only on the scene, never on how the
// editor got there (playing, scrubbing, a render). Each step's emission and emitter state
// (transform, rate, shape, velocity, look: the animated values at that step) go to the GPU in
// a steps buffer, and one dispatch per frame runs every step that is due, each thread taking
// its particle through all of them (particles don't interact). Going back in time, or editing
// the emitter, starts again from the first step. Very long jumps are split over several frames
// (pending()).
//
// Emission: the number of particles born by the end of step k is the sum of rate x dt over the
// steps (kept as a double on the CPU), particle n (counted from the start) lives in slot
// n % maxParticles, and is born at a time spread evenly inside its step.
//
// Forces: every enabled Force component of the scene acts on every emitter. Each step lists
// its forces (as they are at that step: animated, moving with their entity) in a forces
// buffer; Step.forces says which records are its. Editing a force simulates again, like
// editing the emitter.
class ParticleSystem
{
public:
    static constexpr int ParticleSize = 64; // vec4 position (w = age), velocity (w = lifetime), color, extra
    static constexpr int MaxStepsPerFrame = 2400; // a re-simulation longer than this continues next frame

    void initialize(QRhi *rhi);
    // Pipelines for drawing into the scene pass.
    void createPipelines(QRhiRenderPassDescriptor *sceneRp, int sampleCount);
    void release();
    // False where the backend has no compute shaders or instancing: no particles then.
    bool supported() const { return m_supported; }

    // Brings every enabled emitter of `scene` to scene time `seconds` (Timeline::seconds()):
    // uploads and a compute pass. Call outside any render pass, before the scene pass.
    void simulate(QRhiCommandBuffer *cb, const Scene *scene, float seconds);
    // Uniforms of the draws: `clipViewProj` includes QRhi::clipSpaceCorrMatrix().
    void prepareDraw(QRhiResourceUpdateBatch *rub, const QMatrix4x4 &clipViewProj, const QMatrix4x4 &view,
                     const QVector3D &eye, const QSize &viewportPixels, float seconds);
    // Draws every emitter, inside the scene pass (depth tested, not written).
    void draw(QRhiCommandBuffer *cb, const QRhiViewport &viewport);

    // Some emitter still has steps to simulate (a long jump): render another frame.
    bool pending() const { return m_pending; }
    // For the stats: emitters drawn, particles alive (estimated from the emission: born
    // within a lifetime, at most the buffer), steps simulated in the last frame.
    int emitterCount() const { return m_drawn; }
    quint64 particleCount() const { return m_alive; }
    int stepsLastFrame() const { return m_stepsLastFrame; }

private:
    struct Emitter
    {
        Entity reference;            // the entity at the first frame: a change means simulating again
        std::vector<Entity> forceReference; // the same for the scene's forces
        ParticleEmitter::Blend blend = ParticleEmitter::Additive;
        int capacity = 0;
        std::unique_ptr<QRhiBuffer> particles;     // capacity * ParticleSize
        std::unique_ptr<QRhiBuffer> steps;         // MaxStepsPerFrame steps, made once
        std::unique_ptr<QRhiBuffer> forces;        // forceCapacity force records, grows when needed
        int forceCapacity = 0;
        std::unique_ptr<QRhiBuffer> simUniforms;
        std::unique_ptr<QRhiShaderResourceBindings> simSrb;
        int stepsDone = -1;          // steps already in the buffer (-1: must clear it first)
        double born = 0.0;           // particles born by the end of the last step
        bool seen = false;           // in this frame's scene
    };
    void rebuildBindings(Emitter &e);
    // Keeps a replaced GPU resource alive until two frames later, so nothing recorded for
    // the frame (or still in flight) can see it destroyed.
    void retire(std::unique_ptr<QRhiBuffer> &buffer);
    void retire(std::unique_ptr<QRhiShaderResourceBindings> &srb);
    void retire(Emitter &e);

    QRhi *m_rhi = nullptr;
    bool m_supported = false;
    std::unique_ptr<QRhiComputePipeline> m_simPipe;
    std::unique_ptr<QRhiShaderResourceBindings> m_simLayout;
    std::unique_ptr<QRhiBuffer> m_layoutUniforms, m_layoutStorage; // for the layout SRB only
    std::unique_ptr<QRhiBuffer> m_drawUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_drawSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_additivePipe, m_alphaPipe;
    std::unordered_map<EntityId, Emitter> m_emitters;
    std::vector<std::unique_ptr<QRhiResource>> m_retired, m_retiredBefore; // this frame's, the last one's
    bool m_pending = false;
    int m_drawn = 0;
    quint64 m_alive = 0;
    int m_stepsLastFrame = 0;
};
