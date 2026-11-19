#pragma once

#include <QHash>
#include <QMatrix4x4>
#include <QVector3D>
#include <memory>
#include <rhi/qrhi.h>
#include <unordered_map>
#include <vector>

#include "engine/material/particlegraph.h"
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
// the emitter, starts again from the first step. Long jumps are split over several frames
// (pending()), within FrameBudget.
//
// Emission: the number of particles born by the end of step k is the sum of rate x dt over the
// steps (kept as a double on the CPU), particle n (counted from the start) lives in slot
// n % maxParticles, and is born at a time spread evenly inside its step.
//
// Forces: every enabled Force component of the scene acts on every emitter. Each step lists
// its forces (as they are at that step: animated, moving with their entity) in a forces
// buffer; Step.forces says which records are its. Editing a force simulates again, like
// editing the emitter.
//
// Particle graphs: an emitter with a graph (ParticleEmitter::graph) gets its own simulation
// shader, particles.comp with the generated graphAcceleration() (ParticleGraph), compiled
// at runtime when the graph's code changes and cached by code; its values go to
// SimBuf.graphParams, and the transforms of its Object nodes, per step, after the step's
// forces (Step.objects). A graph that doesn't compile leaves that emitter on the default
// shader (the scene's forces), with the reason in error().
class ParticleSystem
{
public:
    static constexpr int ParticleSize = 64; // vec4 position (w = age), velocity (w = lifetime), color, extra
    static constexpr int MaxStepsPerFrame = 2400; // a re-simulation longer than this continues next frame
    // GPU work one frame may take, in particle steps weighted by the forces' cost (a noise
    // force counts as NoiseForceCost plain ones). Catching up after a jump or an edit is spread
    // over frames to stay under it: one long dispatch would make Windows reset the GPU
    // (a "device lost" after about two seconds).
    static constexpr double FrameBudget = 4.0e6;
    static constexpr double NoiseForceCost = 10.0;

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
    // The scene's view depth per pixel (Renderer's distance pass), sampled by particle.frag for
    // soft particles; null where the depth can't be sampled (a stand-in is bound, DrawBuf.soft.x = 0).
    void setSceneDistance(QRhiTexture *texture);

    // Some emitter still has steps to simulate (a long jump): render another frame.
    bool pending() const { return m_pending; }
    // For the stats: emitters drawn, particles alive (estimated from the emission: born
    // within a lifetime, at most the buffer), steps simulated in the last frame.
    int emitterCount() const { return m_drawn; }
    quint64 particleCount() const { return m_alive; }
    int stepsLastFrame() const { return m_stepsLastFrame; }

    // Why an emitter's graph can't be used (graph or shader error); empty when fine.
    QString error(EntityId emitter) const { return m_errors.value(emitter); }
    // True once after a frame that changed some emitter's error.
    bool takeErrorsChanged()
    {
        const bool changed = m_errorsChanged;
        m_errorsChanged = false;
        return changed;
    }

private:
    struct Emitter
    {
        Entity reference;            // the entity at the first frame: a change means simulating again
        std::vector<Entity> forceReference; // the same for the scene's forces and the graph's objects
        NodeGraph compiledGraph;     // the graph `graph` was compiled from
        ParticleGraph::Compiled graph;
        QRhiComputePipeline *pipeline = nullptr; // its shader's (in m_graphShaders), or null: the default
        // The pipeline simSrb was made for. On Direct3D 11 a binding set's registers follow the
        // first pipeline that uses it, and shaders of different graphs may number them
        // differently (resources a graph doesn't use are left out): a new pipeline, a new set.
        const QRhiComputePipeline *srbPipeline = nullptr;
        ParticleEmitter::Blend blend = ParticleEmitter::Additive;
        int capacity = 0;
        std::unique_ptr<QRhiBuffer> particles;     // capacity * ParticleSize
        std::unique_ptr<QRhiBuffer> steps;         // MaxStepsPerFrame steps, made once
        std::unique_ptr<QRhiBuffer> forces;        // forceCapacity force records, grows when needed
        int forceCapacity = 0;
        std::unique_ptr<QRhiBuffer> simUniforms;
        std::unique_ptr<QRhiShaderResourceBindings> simSrb;
        std::unique_ptr<QRhiBuffer> drawUniforms;  // DrawBuf: the camera, and this emitter's softness
        std::unique_ptr<QRhiShaderResourceBindings> drawSrb;
        float softness = 0.0f;
        int stepsDone = -1;          // steps already in the buffer (-1: must clear it first)
        double born = 0.0;           // particles born by the end of the last step
        bool seen = false;           // in this frame's scene
    };
    void rebuildBindings(Emitter &e);
    void rebuildDrawBindings(Emitter &e);
    // Keeps a replaced GPU resource alive until two frames later, so nothing recorded for
    // the frame (or still in flight) can see it destroyed.
    void retire(std::unique_ptr<QRhiBuffer> &buffer);
    void retire(std::unique_ptr<QRhiShaderResourceBindings> &srb);
    void retire(Emitter &e);
    // The simulation pipeline for a graph's code, compiled on first use; null (and `error` set)
    // if it doesn't compile.
    QRhiComputePipeline *graphPipeline(const QString &code, QString *error);
    void setError(EntityId emitter, const QString &error);

    struct GraphShader
    {
        QString error;
        std::unique_ptr<QRhiComputePipeline> pipeline;
    };
    std::unordered_map<QString, GraphShader> m_graphShaders; // by generated code
    QString m_simSource; // particles.comp as GLSL, with the graph markers
    QHash<EntityId, QString> m_errors;
    bool m_errorsChanged = false;

    QRhi *m_rhi = nullptr;
    bool m_supported = false;
    std::unique_ptr<QRhiComputePipeline> m_simPipe;
    std::unique_ptr<QRhiShaderResourceBindings> m_simLayout;
    std::unique_ptr<QRhiBuffer> m_layoutUniforms, m_layoutStorage; // for the layout SRB only
    std::unique_ptr<QRhiBuffer> m_drawUniforms;            // for the layout SRB only
    std::unique_ptr<QRhiShaderResourceBindings> m_drawSrb; // layout of the emitters' draw SRBs
    QRhiTexture *m_sceneDistance = nullptr;                // not owned; null: m_distanceStandIn
    std::unique_ptr<QRhiTexture> m_distanceStandIn;
    std::unique_ptr<QRhiSampler> m_nearest;
    QRhiResourceUpdateBatch *m_pendingUpload = nullptr;    // the stand-in's pixel
    std::unique_ptr<QRhiGraphicsPipeline> m_additivePipe, m_alphaPipe;
    std::unordered_map<EntityId, Emitter> m_emitters;
    std::vector<std::unique_ptr<QRhiResource>> m_retired, m_retiredBefore; // this frame's, the last one's
    bool m_pending = false;
    int m_drawn = 0;
    quint64 m_alive = 0;
    int m_stepsLastFrame = 0;
};
