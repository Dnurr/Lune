#include "particles.h"

#include <QFile>
#include <QImage>
#include <QVector2D>
#include <QtMath>
#include <rhi/qshaderbaker.h>
#include <cmath>
#include <cstring>

#include "engine/material/materialcompiler.h"
#include "engine/scene/animation.h"
#include "engine/scene/scene.h"

namespace
{
// Step in particles.comp (std430): mat4 emitter, vec4 time, uvec4 spawn, vec4 shape,
// vec4 velocity, vec4 life, vec4 startColor, vec4 endColor, uvec4 forces, uvec4 objects.
constexpr int kStepSize = 64 + 9 * 16;
// Force in particles.comp (std430): mat4 transform, vec4 params0, params1, params2.
constexpr int kForceSize = 64 + 3 * 16;
// SimBuf: uvec4 counts, vec4 params, vec4 graphParams[MAX_GRAPH_PARAMS].
constexpr int kSimUbufSize = 32 + ParticleGraph::MaxParams * 16;
// DrawBuf: mat4 viewProj, vec4 cameraRight, cameraUp, cameraPos, params, soft.
constexpr int kDrawUbufSize = 64 + 5 * 16;
constexpr int kWorkgroupSize = 256; // local_size_x in particles.comp

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("ParticleSystem: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

float linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Linear RGB times `scale`, and `alpha`.
void putColor(float *v, const QColor &c, float scale, float alpha)
{
    v[0] = linear(float(c.redF())) * scale;
    v[1] = linear(float(c.greenF())) * scale;
    v[2] = linear(float(c.blueF())) * scale;
    v[3] = alpha;
}

// The entity as the simulation sees it at `frame`: its animated values there.
Entity at(const Entity &e, double frame)
{
    Entity copy = e;
    if (!copy.animation.empty())
        Animation::applyTo(copy, frame);
    return copy;
}

bool sameSimulation(const Entity &a, const Entity &b)
{
    return a.emitter == b.emitter && a.transform == b.transform && a.animation == b.animation;
}

bool sameForces(const std::vector<Entity> &a, const std::vector<Entity> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || a[i].force != b[i].force || a[i].transform != b[i].transform
            || a[i].animation != b[i].animation)
            return false;
    }
    return true;
}

// One Force record (see kForceSize).
void putForce(float *v, const Entity &e)
{
    const ParticleForce &f = *e.force;
    std::memcpy(v, e.transform.matrix().constData(), 64);
    v[16] = float(f.type); // params0: type, strength, radius, falloff
    v[17] = f.strength;
    v[18] = f.radius;
    v[19] = f.falloff;
    v[20] = f.drag;        // params1: drag, frequency, speed, noise
    v[21] = f.frequency;
    v[22] = f.speed;
    v[23] = f.noise;
    v[24] = f.pull;        // params2: pull, lift
    v[25] = f.lift;
}
} // namespace

void ParticleSystem::initialize(QRhi *rhi)
{
    release();
    m_rhi = rhi;
    m_supported = m_rhi->isFeatureSupported(QRhi::Compute) && m_rhi->isFeatureSupported(QRhi::Instancing);
    m_drawUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kDrawUbufSize));
    m_drawUniforms->create();
    m_nearest.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_nearest->create();
    // Stand-in for the scene's distance where it doesn't exist: never read (DrawBuf.soft.x = 0).
    m_distanceStandIn.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_distanceStandIn->create();
    m_pendingUpload = m_rhi->nextResourceUpdateBatch();
    QImage white(1, 1, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    m_pendingUpload->uploadTexture(m_distanceStandIn.get(), white);
    using B = QRhiShaderResourceBinding;
    m_drawSrb.reset(m_rhi->newShaderResourceBindings());
    m_drawSrb->setBindings({ B::uniformBuffer(0, B::VertexStage | B::FragmentStage, m_drawUniforms.get()),
                             B::sampledTexture(1, B::FragmentStage, m_distanceStandIn.get(), m_nearest.get()) });
    m_drawSrb->create();
    if (!m_supported)
        return;

    m_layoutUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kSimUbufSize));
    m_layoutUniforms->create();
    m_layoutStorage.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, kStepSize + kForceSize));
    m_layoutStorage->create();
    m_simLayout.reset(m_rhi->newShaderResourceBindings());
    m_simLayout->setBindings({ B::uniformBuffer(0, B::ComputeStage, m_layoutUniforms.get()),
                               B::bufferLoadStore(1, B::ComputeStage, m_layoutStorage.get()),
                               B::bufferLoad(2, B::ComputeStage, m_layoutStorage.get()),
                               B::bufferLoad(3, B::ComputeStage, m_layoutStorage.get()) });
    m_simLayout->create();
    m_simPipe.reset(m_rhi->newComputePipeline());
    m_simPipe->setShaderStage({ QRhiShaderStage::Compute, loadShader(QStringLiteral("particles.comp")) });
    m_simPipe->setShaderResourceBindings(m_simLayout.get());
    if (!m_simPipe->create()) {
        qWarning("ParticleSystem: the simulation pipeline can't be created");
        m_supported = false;
    }
}

void ParticleSystem::createPipelines(QRhiRenderPassDescriptor *sceneRp, int sampleCount)
{
    m_additivePipe.reset();
    m_alphaPipe.reset();
    if (!m_supported)
        return;
    const QShader vert = loadShader(QStringLiteral("particle.vert"));
    const QShader frag = loadShader(QStringLiteral("particle.frag"));
    QRhiVertexInputLayout input; // one particle per instance, read straight from the simulation's buffer
    input.setBindings({ { quint32(ParticleSize), QRhiVertexInputBinding::PerInstance } });
    input.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float4, 0 },
                          { 0, 1, QRhiVertexInputAttribute::Float4, 16 },
                          { 0, 2, QRhiVertexInputAttribute::Float4, 32 },
                          { 0, 3, QRhiVertexInputAttribute::Float4, 48 } });
    auto make = [&](bool additive) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, vert }, { QRhiShaderStage::Fragment, frag } });
        p->setVertexInputLayout(input);
        p->setShaderResourceBindings(m_drawSrb.get());
        p->setRenderPassDescriptor(sceneRp);
        p->setSampleCount(sampleCount);
        p->setCullMode(QRhiGraphicsPipeline::None);
        p->setDepthTest(true);   // hidden behind meshes
        p->setDepthWrite(false); // but they don't hide each other
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::One; // premultiplied color out of particle.frag
        b.dstColor = additive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::One;
        b.dstAlpha = additive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
        p->setTargetBlends({ b });
        p->create();
        return p;
    };
    m_additivePipe = make(true);
    m_alphaPipe = make(false);
}

void ParticleSystem::release()
{
    m_emitters.clear();
    m_graphShaders.clear();
    m_errors.clear();
    m_retired.clear();
    m_retiredBefore.clear();
    m_additivePipe.reset();
    m_alphaPipe.reset();
    m_simPipe.reset();
    m_simLayout.reset();
    m_layoutUniforms.reset();
    m_layoutStorage.reset();
    m_drawSrb.reset();
    m_drawUniforms.reset();
    if (m_pendingUpload) {
        m_pendingUpload->release();
        m_pendingUpload = nullptr;
    }
    m_distanceStandIn.reset();
    m_nearest.reset();
    m_sceneDistance = nullptr;
    m_rhi = nullptr;
    m_supported = false;
    m_pending = false;
    m_drawn = 0;
    m_alive = 0;
    m_stepsLastFrame = 0;
}

void ParticleSystem::setError(EntityId emitter, const QString &error)
{
    if (m_errors.value(emitter) == error)
        return;
    if (error.isEmpty())
        m_errors.remove(emitter);
    else
        m_errors.insert(emitter, error);
    m_errorsChanged = true;
}

QRhiComputePipeline *ParticleSystem::graphPipeline(const QString &code, QString *error)
{
    auto it = m_graphShaders.find(code);
    if (it == m_graphShaders.end()) {
        GraphShader g;
        if (m_simSource.isEmpty()) {
            QFile f(QStringLiteral(":/shadersrc/particles.comp"));
            if (f.open(QIODevice::ReadOnly))
                m_simSource = QString::fromUtf8(f.readAll());
        }
        const QString source = ParticleGraph::insertInto(m_simSource, code);
        if (source.isEmpty()) {
            g.error = QStringLiteral("particles.comp has no graph markers.");
        } else {
            // Compiled for the backend in use only: much faster than every target.
            QShaderBaker baker;
            QList<QShaderBaker::GeneratedShader> targets;
            switch (m_rhi->backend()) {
            case QRhi::Vulkan: targets.append({ QShader::SpirvShader, QShaderVersion(100) }); break;
            case QRhi::D3D11:
            case QRhi::D3D12: targets.append({ QShader::HlslShader, QShaderVersion(50) }); break;
            case QRhi::Metal: targets.append({ QShader::MslShader, QShaderVersion(12) }); break;
            case QRhi::OpenGLES2:
                targets.append({ QShader::GlslShader, QShaderVersion(430) });
                targets.append({ QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs) });
                break;
            default: break;
            }
            baker.setGeneratedShaders(targets);
            baker.setGeneratedShaderVariants({ QShader::StandardShader });
            baker.setSourceString(source.toUtf8(), QShader::ComputeStage, "particle graph");
            const QShader shader = baker.bake();
            if (!shader.isValid()) {
                g.error = QStringLiteral("Shader error: ") + baker.errorMessage().trimmed();
            } else {
                g.pipeline.reset(m_rhi->newComputePipeline());
                g.pipeline->setShaderStage({ QRhiShaderStage::Compute, shader });
                g.pipeline->setShaderResourceBindings(m_simLayout.get());
                if (!g.pipeline->create()) {
                    g.pipeline.reset();
                    g.error = QStringLiteral("The graph's simulation pipeline can't be created.");
                }
            }
        }
        it = m_graphShaders.emplace(code, std::move(g)).first;
    }
    *error = it->second.error;
    return it->second.pipeline.get();
}

void ParticleSystem::retire(std::unique_ptr<QRhiBuffer> &buffer)
{
    if (buffer)
        m_retired.emplace_back(buffer.release());
}

void ParticleSystem::retire(std::unique_ptr<QRhiShaderResourceBindings> &srb)
{
    if (srb)
        m_retired.emplace_back(srb.release());
}

void ParticleSystem::retire(Emitter &e)
{
    retire(e.simSrb);
    retire(e.particles);
    retire(e.steps);
    retire(e.forces);
    retire(e.simUniforms);
    retire(e.drawSrb);
    retire(e.drawUniforms);
}

void ParticleSystem::setSceneDistance(QRhiTexture *texture)
{
    if (texture == m_sceneDistance)
        return;
    m_sceneDistance = texture;
    for (auto &[id, e] : m_emitters)
        retire(e.drawSrb); // made again, with the new texture, when drawn
}

void ParticleSystem::rebuildDrawBindings(Emitter &e)
{
    using B = QRhiShaderResourceBinding;
    retire(e.drawSrb);
    e.drawSrb.reset(m_rhi->newShaderResourceBindings());
    e.drawSrb->setBindings({ B::uniformBuffer(0, B::VertexStage | B::FragmentStage, e.drawUniforms.get()),
                             B::sampledTexture(1, B::FragmentStage,
                                               m_sceneDistance ? m_sceneDistance : m_distanceStandIn.get(), m_nearest.get()) });
    e.drawSrb->create();
}

void ParticleSystem::rebuildBindings(Emitter &e)
{
    retire(e.simSrb);
    e.srbPipeline = e.pipeline ? e.pipeline : m_simPipe.get();
    using B = QRhiShaderResourceBinding;
    e.simSrb.reset(m_rhi->newShaderResourceBindings());
    e.simSrb->setBindings({ B::uniformBuffer(0, B::ComputeStage, e.simUniforms.get()),
                            B::bufferLoadStore(1, B::ComputeStage, e.particles.get()),
                            B::bufferLoad(2, B::ComputeStage, e.steps.get()),
                            B::bufferLoad(3, B::ComputeStage, e.forces.get()) });
    e.simSrb->create();
}

void ParticleSystem::simulate(QRhiCommandBuffer *cb, const Scene *scene, float seconds)
{
    m_pending = false;
    m_stepsLastFrame = 0;
    m_drawn = 0;
    m_alive = 0;
    // What was replaced two frames ago can go now.
    m_retiredBefore = std::move(m_retired);
    m_retired.clear();
    if (!m_supported || !scene) {
        for (auto &[id, e] : m_emitters)
            retire(e);
        m_emitters.clear();
        return;
    }
    const Timeline &timeline = scene->timeline();
    const double fps = qMax(1.0, timeline.fps);
    const float dt = float(1.0 / fps);
    // Steps due: one per frame since the start (frame start + k is reached after k steps).
    const int target = qMax(0, int(std::floor(double(seconds) * fps + 1e-4)));

    for (auto &[id, e] : m_emitters)
        e.seen = false;

    // The scene's forces, and how they are at the first frame (for spotting edits).
    std::vector<const Entity *> forceEntities;
    std::vector<Entity> forceReference;
    // Cost of one particle step: the particle itself, plus each force (noise forces cost more).
    double stepCost = 1.0;
    for (const Entity &entity : scene->entities()) {
        if (entity.force) {
            forceEntities.push_back(&entity);
            forceReference.push_back(at(entity, timeline.start));
            if (entity.force->enabled) {
                const bool noise = entity.force->type == ParticleForce::Turbulence
                    || (entity.force->type == ParticleForce::Vortex && entity.force->noise > 0.0f);
                stepCost += noise ? NoiseForceCost : 1.0;
            }
        }
    }
    double budget = FrameBudget; // shared by the emitters, in weighted particle steps

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    if (m_pendingUpload) { // the distance stand-in's pixel, from initialize()
        rub->merge(m_pendingUpload);
        m_pendingUpload->release();
        m_pendingUpload = nullptr;
    }
    struct Dispatch
    {
        Emitter *emitter;
        int groups;
    };
    std::vector<Dispatch> dispatches;

    for (const Entity &entity : scene->entities()) {
        if (!entity.emitter || !entity.emitter->enabled)
            continue;
        Emitter &e = m_emitters[entity.id];
        e.seen = true;
        const Entity reference = at(entity, timeline.start);

        // The graph: compiled again when it changed; its shader from the cache.
        const NodeGraph &graph = entity.emitter->graph;
        if (graph != e.compiledGraph || (e.compiledGraph.nodes.empty() && !graph.nodes.empty())) {
            e.compiledGraph = graph;
            e.graph = graph.nodes.empty() ? ParticleGraph::Compiled {}
                                          : ParticleGraph::compile(graph, NodeLibrary::particleInstance());
        }
        QString graphError = e.graph.error;
        e.pipeline = nullptr;
        if (!graph.nodes.empty() && e.graph.ok())
            e.pipeline = graphPipeline(e.graph.code, &graphError);
        setError(entity.id, graphError);
        // The graph's objects (and their state at the first frame, for spotting edits).
        std::vector<const Entity *> objects;
        std::vector<Entity> linkedReference = forceReference;
        if (e.pipeline) {
            for (quint64 id : e.graph.objects) {
                const Entity *o = id ? scene->entity(EntityId(id)) : nullptr;
                objects.push_back(o);
                if (o)
                    linkedReference.push_back(at(*o, timeline.start));
            }
        }
        const int capacity = qMax(1, entity.emitter->maxParticles);
        if (!e.particles || e.capacity != capacity) {
            e.capacity = capacity;
            retire(e.particles);
            retire(e.simSrb);
            e.particles.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::VertexBuffer | QRhiBuffer::StorageBuffer,
                                               quint32(capacity) * ParticleSize));
            e.particles->create();
            e.stepsDone = -1;
        }
        if (!e.simUniforms) {
            e.simUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kSimUbufSize));
            e.simUniforms->create();
        }
        if (!e.steps) { // the most one frame can run: never resized
            e.steps.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer,
                                           quint32(MaxStepsPerFrame) * kStepSize));
            e.steps->create();
        }
        if (!e.forces) {
            e.forceCapacity = 64;
            e.forces.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer,
                                            quint32(e.forceCapacity) * kForceSize));
            e.forces->create();
        }
        if (!e.simSrb || e.srbPipeline != (e.pipeline ? e.pipeline : m_simPipe.get()))
            rebuildBindings(e);
        if (!sameSimulation(reference, e.reference) || !sameForces(linkedReference, e.forceReference)
            || target < e.stepsDone) {
            e.reference = reference;
            e.forceReference = linkedReference;
            e.stepsDone = -1;
        }
        e.blend = entity.emitter->blend;
        e.softness = entity.emitter->softness;
        if (!e.drawUniforms) {
            e.drawUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kDrawUbufSize));
            e.drawUniforms->create();
        }
        if (!e.drawSrb)
            rebuildDrawBindings(e);
        if (e.stepsDone < 0) { // from the start: every particle dead (lifetime 0)
            const QByteArray zeros(qsizetype(capacity) * ParticleSize, '\0');
            rub->uploadStaticBuffer(e.particles.get(), zeros.constData());
            e.stepsDone = 0;
            e.born = 0.0;
        }

        // At least one step, so every emitter keeps moving; at most what the budget left allows.
        const int affordable = int(qBound(1.0, budget / (double(capacity) * stepCost), double(MaxStepsPerFrame)));
        const int count = qMin(target - e.stepsDone, affordable);
        budget = qMax(0.0, budget - double(qMax(count, 0)) * double(capacity) * stepCost);
        if (count > 0) {
            if (target - e.stepsDone > count)
                m_pending = true;

            QByteArray steps(qsizetype(count) * kStepSize, '\0');
            QByteArray forces;
            int forceCount = 0; // records in `forces`
            for (int i = 0; i < count; ++i) {
                const int k = e.stepsDone + i;
                const Entity s = at(entity, timeline.start + k); // the emitter during this step
                const quint32 firstForce = quint32(forceCount);
                for (const Entity *fe : forceEntities) { // the forces during this step
                    const Entity f = at(*fe, timeline.start + k);
                    if (!f.force->enabled)
                        continue;
                    forces.resize(qsizetype(forceCount + 1) * kForceSize, '\0');
                    putForce(reinterpret_cast<float *>(forces.data() + qsizetype(forceCount) * kForceSize), f);
                    ++forceCount;
                }
                const ParticleEmitter &p = *s.emitter;
                const double born0 = e.born;
                e.born += double(p.rate) * dt;

                char *base = steps.data() + qsizetype(i) * kStepSize;
                float *v = reinterpret_cast<float *>(base);
                std::memcpy(v, s.transform.matrix().constData(), 64);
                v[16] = float(k * double(dt));       // time: step start, end (seconds)
                v[17] = float((k + 1) * double(dt));
                const quint32 spawn[4] = { quint32(std::floor(born0)), quint32(std::floor(e.born)), 0, 0 };
                std::memcpy(v + 20, spawn, sizeof(spawn));
                v[24] = float(p.shape);              // shape: type, size
                v[25] = p.shapeSize.x();
                v[26] = p.shapeSize.y();
                v[27] = p.shapeSize.z();
                v[28] = p.speed;                     // velocity: speed, variation, spread (radians)
                v[29] = p.speedVariation;
                v[30] = qDegreesToRadians(p.spread);
                v[32] = p.lifetime;                  // life: lifetime, variation, start size, end size
                v[33] = p.lifetimeVariation;
                v[34] = p.startSize;
                v[35] = p.endSize;
                putColor(v + 36, p.startColor, p.intensity, p.startOpacity);
                putColor(v + 40, p.endColor, p.intensity, p.endOpacity);
                const quint32 forceRange[4] = { firstForce, quint32(forceCount) - firstForce, 0, 0 };
                std::memcpy(v + 44, forceRange, sizeof(forceRange));
                // The graph's objects during this step, after the forces (identity for a missing one).
                const quint32 firstObject = quint32(forceCount);
                for (const Entity *o : objects) {
                    forces.resize(qsizetype(forceCount + 1) * kForceSize, '\0');
                    float *r = reinterpret_cast<float *>(forces.data() + qsizetype(forceCount) * kForceSize);
                    const QMatrix4x4 m = o ? at(*o, timeline.start + k).transform.matrix() : QMatrix4x4();
                    std::memcpy(r, m.constData(), 64);
                    ++forceCount;
                }
                const quint32 objectRange[4] = { firstObject, quint32(forceCount) - firstObject, 0, 0 };
                std::memcpy(v + 48, objectRange, sizeof(objectRange));
            }
            rub->uploadStaticBuffer(e.steps.get(), 0, quint32(steps.size()), steps.constData());
            if (forceCount > e.forceCapacity) {
                retire(e.forces);
                e.forceCapacity = qMax(forceCount, e.forceCapacity * 2);
                e.forces.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer,
                                                quint32(e.forceCapacity) * kForceSize));
                e.forces->create();
                rebuildBindings(e);
            }
            if (forceCount > 0)
                rub->uploadStaticBuffer(e.forces.get(), 0, quint32(forces.size()), forces.constData());

            const quint32 counts[4] = { quint32(capacity), quint32(count), quint32(entity.emitter->seed), 0 };
            float sim[kSimUbufSize / 4] = {};
            std::memcpy(sim, counts, sizeof(counts));
            sim[4] = dt;
            if (e.pipeline) { // the graph's values: colors linear, like materials
                const NodeLibrary &library = NodeLibrary::particleInstance();
                for (size_t i = 0; i < e.graph.params.size(); ++i) {
                    const MaterialParam &param = e.graph.params[i];
                    const GraphNode *node = graph.node(param.node);
                    const NodeType *type = node ? library.type(node->type) : nullptr;
                    const PortDesc *port = type ? type->input(param.port) : nullptr;
                    if (!port)
                        continue;
                    const QVariant value = MaterialCompiler::inputValue(*node, *port);
                    float *slot = sim + 8 + i * 4;
                    if (port->type == PortType::Float) {
                        slot[0] = value.toFloat();
                    } else if (port->type == PortType::Vec2) {
                        const QVector2D v2 = value.value<QVector2D>();
                        slot[0] = v2.x();
                        slot[1] = v2.y();
                    } else {
                        const QVector3D v3 = value.value<QVector3D>();
                        slot[0] = port->color ? linear(v3.x()) : v3.x();
                        slot[1] = port->color ? linear(v3.y()) : v3.y();
                        slot[2] = port->color ? linear(v3.z()) : v3.z();
                    }
                }
            }
            rub->updateDynamicBuffer(e.simUniforms.get(), 0, kSimUbufSize, sim);
            dispatches.push_back({ &e, (capacity + kWorkgroupSize - 1) / kWorkgroupSize });
            e.stepsDone += count;
            m_stepsLastFrame += count;
        }
        ++m_drawn;
        const double inLifetime = double(entity.emitter->rate) * entity.emitter->lifetime;
        m_alive += quint64(qMin(double(capacity), qMin(std::floor(e.born), std::ceil(inLifetime))));
    }
    for (auto it = m_emitters.begin(); it != m_emitters.end();) {
        if (it->second.seen) {
            ++it;
        } else {
            setError(it->first, QString());
            retire(it->second);
            it = m_emitters.erase(it);
        }
    }

    if (dispatches.empty()) { // nothing to step (paused): just the uploads, if any
        cb->resourceUpdate(rub);
        return;
    }
    cb->beginComputePass(rub);
    for (const Dispatch &d : dispatches) {
        cb->setComputePipeline(d.emitter->pipeline ? d.emitter->pipeline : m_simPipe.get());
        cb->setShaderResources(d.emitter->simSrb.get());
        cb->dispatch(d.groups, 1, 1);
    }
    cb->endComputePass();
}

void ParticleSystem::prepareDraw(QRhiResourceUpdateBatch *rub, const QMatrix4x4 &clipViewProj, const QMatrix4x4 &view,
                                 const QVector3D &eye, const QSize &viewportPixels, float seconds)
{
    if (!m_supported || m_emitters.empty())
        return;
    float data[kDrawUbufSize / 4] = {};
    std::memcpy(data, clipViewProj.constData(), 64);
    // The view's rows are the camera's axes in world space.
    const QVector4D right = view.row(0), up = view.row(1);
    data[16] = right.x(); data[17] = right.y(); data[18] = right.z();
    data[20] = up.x(); data[21] = up.y(); data[22] = up.z();
    data[24] = eye.x(); data[25] = eye.y(); data[26] = eye.z(); data[27] = 1.0f;
    data[28] = float(viewportPixels.width());
    data[29] = float(viewportPixels.height());
    data[30] = seconds;
    data[32] = m_sceneDistance ? 1.0f : 0.0f; // soft: the scene's distance is there, the emitter's softness
    for (auto &[id, e] : m_emitters) {
        if (!e.drawUniforms)
            continue;
        data[33] = e.softness;
        rub->updateDynamicBuffer(e.drawUniforms.get(), 0, kDrawUbufSize, data);
    }
}

void ParticleSystem::draw(QRhiCommandBuffer *cb, const QRhiViewport &viewport)
{
    if (!m_supported || !m_additivePipe)
        return;
    for (auto &[id, e] : m_emitters) {
        if (!e.particles || e.stepsDone <= 0 || !e.drawSrb)
            continue;
        cb->setGraphicsPipeline(e.blend == ParticleEmitter::Additive ? m_additivePipe.get() : m_alphaPipe.get());
        cb->setViewport(viewport);
        cb->setShaderResources(e.drawSrb.get());
        const QRhiCommandBuffer::VertexInput vin(e.particles.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(6, quint32(e.capacity)); // two triangles per particle; dead ones collapse
    }
}
