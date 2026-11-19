#include "particles.h"

#include <QFile>
#include <QtMath>
#include <cmath>
#include <cstring>

#include "engine/scene/animation.h"
#include "engine/scene/scene.h"

namespace
{
// Step in particles.comp (std430): mat4 emitter, vec4 time, uvec4 spawn, vec4 shape,
// vec4 velocity, vec4 life, vec4 startColor, vec4 endColor.
constexpr int kStepSize = 64 + 7 * 16;
// SimBuf: uvec4 counts, vec4 params.
constexpr int kSimUbufSize = 32;
// DrawBuf: mat4 viewProj, vec4 cameraRight, cameraUp, cameraPos, params.
constexpr int kDrawUbufSize = 64 + 4 * 16;
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
} // namespace

void ParticleSystem::initialize(QRhi *rhi)
{
    release();
    m_rhi = rhi;
    m_supported = m_rhi->isFeatureSupported(QRhi::Compute) && m_rhi->isFeatureSupported(QRhi::Instancing);
    m_drawUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kDrawUbufSize));
    m_drawUniforms->create();
    using B = QRhiShaderResourceBinding;
    m_drawSrb.reset(m_rhi->newShaderResourceBindings());
    m_drawSrb->setBindings({ B::uniformBuffer(0, B::VertexStage | B::FragmentStage, m_drawUniforms.get()) });
    m_drawSrb->create();
    if (!m_supported)
        return;

    m_layoutUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kSimUbufSize));
    m_layoutUniforms->create();
    m_layoutStorage.reset(m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, kStepSize));
    m_layoutStorage->create();
    m_simLayout.reset(m_rhi->newShaderResourceBindings());
    m_simLayout->setBindings({ B::uniformBuffer(0, B::ComputeStage, m_layoutUniforms.get()),
                               B::bufferLoadStore(1, B::ComputeStage, m_layoutStorage.get()),
                               B::bufferLoad(2, B::ComputeStage, m_layoutStorage.get()) });
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
    m_rhi = nullptr;
    m_supported = false;
    m_pending = false;
    m_drawn = 0;
    m_alive = 0;
    m_stepsLastFrame = 0;
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
    retire(e.simUniforms);
}

void ParticleSystem::rebuildBindings(Emitter &e)
{
    retire(e.simSrb);
    using B = QRhiShaderResourceBinding;
    e.simSrb.reset(m_rhi->newShaderResourceBindings());
    e.simSrb->setBindings({ B::uniformBuffer(0, B::ComputeStage, e.simUniforms.get()),
                            B::bufferLoadStore(1, B::ComputeStage, e.particles.get()),
                            B::bufferLoad(2, B::ComputeStage, e.steps.get()) });
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

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
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
        if (!e.simSrb)
            rebuildBindings(e);
        if (!sameSimulation(reference, e.reference) || target < e.stepsDone) {
            e.reference = reference;
            e.stepsDone = -1;
        }
        e.blend = entity.emitter->blend;
        if (e.stepsDone < 0) { // from the start: every particle dead (lifetime 0)
            const QByteArray zeros(qsizetype(capacity) * ParticleSize, '\0');
            rub->uploadStaticBuffer(e.particles.get(), zeros.constData());
            e.stepsDone = 0;
            e.born = 0.0;
        }

        const int count = qMin(target - e.stepsDone, MaxStepsPerFrame);
        if (count > 0) {
            if (target - e.stepsDone > count)
                m_pending = true;

            QByteArray steps(qsizetype(count) * kStepSize, '\0');
            for (int i = 0; i < count; ++i) {
                const int k = e.stepsDone + i;
                const Entity s = at(entity, timeline.start + k); // the emitter during this step
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
            }
            rub->uploadStaticBuffer(e.steps.get(), 0, quint32(steps.size()), steps.constData());

            const quint32 counts[4] = { quint32(capacity), quint32(count), quint32(entity.emitter->seed), 0 };
            float sim[kSimUbufSize / 4] = {};
            std::memcpy(sim, counts, sizeof(counts));
            sim[4] = dt;
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
            retire(it->second);
            it = m_emitters.erase(it);
        }
    }

    if (dispatches.empty()) { // nothing to step (paused): just the uploads, if any
        cb->resourceUpdate(rub);
        return;
    }
    cb->beginComputePass(rub);
    cb->setComputePipeline(m_simPipe.get());
    for (const Dispatch &d : dispatches) {
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
    rub->updateDynamicBuffer(m_drawUniforms.get(), 0, kDrawUbufSize, data);
}

void ParticleSystem::draw(QRhiCommandBuffer *cb, const QRhiViewport &viewport)
{
    if (!m_supported || !m_additivePipe)
        return;
    for (auto &[id, e] : m_emitters) {
        if (!e.particles || e.stepsDone <= 0)
            continue;
        cb->setGraphicsPipeline(e.blend == ParticleEmitter::Additive ? m_additivePipe.get() : m_alphaPipe.get());
        cb->setViewport(viewport);
        cb->setShaderResources(m_drawSrb.get());
        const QRhiCommandBuffer::VertexInput vin(e.particles.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(6, quint32(e.capacity)); // two triangles per particle; dead ones collapse
    }
}
