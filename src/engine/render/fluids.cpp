#include "fluids.h"
#include "shadernames.h"

#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QtMath>
#include <rhi/qshaderbaker.h>
#include <cmath>
#include <cstring>

#include "engine/scene/animation.h"
#include "engine/scene/scene.h"

#ifdef Q_OS_WIN
#include <QtGui/private/qrhid3d11_p.h>
#endif

namespace
{
// Qt 6.8's Direct3D 11 backend makes the storage (UAV) view of a 3D texture without setting
// its depth range: WSize stays 0, a view of no slices, and every imageStore into it is lost.
// The view is made here instead, over every slice, and put where QRhi looks for it first
// (QD3D11Texture::perLevelViews, which QRhi releases with the texture).
void fixVolumeStorageView(QRhi *rhi, QRhiTexture *texture)
{
#ifdef Q_OS_WIN
    if (rhi->backend() != QRhi::D3D11)
        return;
    auto *t = static_cast<QD3D11Texture *>(texture);
    if (!t->tex3D || t->perLevelViews[0])
        return;
    ID3D11Device *device = nullptr;
    t->tex3D->GetDevice(&device);
    if (!device)
        return;
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = t->dxgiFormat;
    desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    desc.Texture3D.MipSlice = 0;
    desc.Texture3D.FirstWSlice = 0;
    desc.Texture3D.WSize = UINT(-1); // every slice
    ID3D11UnorderedAccessView *view = nullptr;
    if (SUCCEEDED(device->CreateUnorderedAccessView(t->tex3D, &desc, &view)))
        t->perLevelViews[0] = view;
    device->Release();
#else
    Q_UNUSED(rhi);
    Q_UNUSED(texture);
#endif
}

// FluidBuf in fluid_*.comp: uvec4 grid, vec4 step, solver, solver2, up, vec4 sources[MaxSources * 3].
constexpr int kFluidUbufSize = 5 * 16 + FluidSystem::MaxSources * 3 * 16;
// VolumeBuf in fluid_volume.vert/.frag: mat4 viewProj, model, worldToBox, then 10 vec4.
constexpr int kVolumeUbufSize = 3 * 64 + 10 * 16;
// ShadowBuf in fluid_shadow.vert/.frag: mat4 lightViewProj, model, worldToBox, then 4 vec4.
constexpr int kShadowUbufSize = 3 * 64 + 4 * 16;

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("FluidSystem: missing shader %s", qPrintable(name));
        return {};
    }
    return withReflectedMemberNames(QShader::fromSerialized(f.readAll()));
}

// OpenGL without dual-source blending (FluidSystem::createPipelines): fluid_volume.frag with
// its two outputs turned into plain variables and a main() that writes one of them, as the
// only output. `multiply`: vec4(transmittance.rgb, 1 - alpha), for dst * src (color and
// alpha); otherwise fragColor, added on top. Together: dst * T + L and a + dst_a * (1 - a),
// as the dual-source blend does. An invalid shader if the source doesn't declare the outputs
// as documented in it.
QShader volumeVariant(bool multiply, QString *error)
{
    QFile f(QStringLiteral(":/shadersrc/fluid_volume.frag"));
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("missing fluid_volume.frag source");
        return {};
    }
    QString src = QString::fromUtf8(f.readAll());
    const QRegularExpression color(QStringLiteral(R"(layout\s*\(\s*location\s*=\s*0\s*\)\s*out\s+vec4\s+fragColor\s*;)"));
    const QRegularExpression second(
        QStringLiteral(R"(layout\s*\(\s*location\s*=\s*0\s*,\s*index\s*=\s*1\s*\)\s*out\s+vec4\s+transmittance\s*;)"));
    const QRegularExpression entry(QStringLiteral(R"(\bvoid\s+main\s*\(\s*\))"));
    if (!src.contains(color) || !src.contains(second) || !src.contains(entry)) {
        *error = QStringLiteral("fluid_volume.frag outputs not as expected");
        return {};
    }
    src.replace(color, QStringLiteral("vec4 fragColor;\nlayout(location = 0) out vec4 luneOut;"));
    src.replace(second, QStringLiteral("vec4 transmittance;"));
    src.replace(entry, QStringLiteral("void luneVolumeMain()"));
    src += multiply ? QStringLiteral("\nvoid main()\n{\n    luneVolumeMain();\n"
                                     "    luneOut = vec4(transmittance.rgb, 1.0 - fragColor.a);\n}\n")
                    : QStringLiteral("\nvoid main()\n{\n    luneVolumeMain();\n    luneOut = fragColor;\n}\n");

    QShaderBaker baker;
    baker.setGeneratedShaders({ { QShader::GlslShader, QShaderVersion(430) },
                                { QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs) } });
    baker.setGeneratedShaderVariants({ QShader::StandardShader });
    baker.setSourceString(src.toUtf8(), QShader::FragmentStage, "fluid_volume.frag (OpenGL)");
    const QShader shader = baker.bake();
    if (!shader.isValid()) {
        *error = baker.errorMessage().trimmed();
        return shader;
    }
    return withReflectedMemberNames(shader);
}

float linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

void putColor(float *v, const QColor &c)
{
    v[0] = linear(float(c.redF()));
    v[1] = linear(float(c.greenF()));
    v[2] = linear(float(c.blueF()));
    v[3] = 1.0f;
}

Entity at(const Entity &e, double frame)
{
    Entity copy = e;
    if (!copy.animation.empty())
        Animation::applyTo(copy, frame);
    return copy;
}

// The box's frame: position and rotation of the entity, without its scale.
QMatrix4x4 boxFrame(const Transform &t)
{
    Transform unscaled = t;
    unscaled.scale = QVector3D(1.0f, 1.0f, 1.0f);
    return unscaled.matrix();
}

bool sameEntities(const std::vector<Entity> &a, const std::vector<Entity> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || a[i].fluidSource != b[i].fluidSource || a[i].transform != b[i].transform
            || a[i].animation != b[i].animation)
            return false;
    }
    return true;
}

int groups(int n)
{
    return (n + FluidSystem::GroupSize - 1) / FluidSystem::GroupSize;
}
} // namespace

void FluidSystem::initialize(QRhi *rhi)
{
    release();
    m_rhi = rhi;
    m_supported = m_rhi->isFeatureSupported(QRhi::Compute) && m_rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures);
    // Qt 6.8's Direct3D 12 backend loses the device on the first write to a 3D storage image (the
    // views are made inside QRhi, so fixVolumeStorageView's Direct3D 11 workaround can't reach
    // them): no fluids there rather than a lost device.
    if (m_rhi->backend() == QRhi::D3D12)
        m_supported = false;

    m_linear.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_linear->create();
    m_nearest.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_nearest->create();
    m_distanceStandIn.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_distanceStandIn->create();
    m_pendingUpload = m_rhi->nextResourceUpdateBatch();
    QImage white(1, 1, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    m_pendingUpload->uploadTexture(m_distanceStandIn.get(), white);
    if (!m_supported)
        return;

    m_layoutVolume.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, 1, 1, 1, 1,
                                           QRhiTexture::ThreeDimensional | QRhiTexture::UsedWithLoadStore));
    if (m_layoutVolume->create())
        fixVolumeStorageView(m_rhi, m_layoutVolume.get());
    else {
        qWarning("FluidSystem: 3D storage textures can't be made: no fluids");
        m_supported = false;
        return;
    }
    m_layoutUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kVolumeUbufSize));
    m_layoutUniforms->create();

    using B = QRhiShaderResourceBinding;
    const auto cs = B::ComputeStage;
    QRhiTexture *t = m_layoutVolume.get();
    m_passLayout.reset(m_rhi->newShaderResourceBindings());
    m_passLayout->setBindings({ B::uniformBufferWithDynamicOffset(0, cs, m_layoutUniforms.get(), kFluidUbufSize),
                                B::sampledTexture(1, cs, t, m_linear.get()), B::sampledTexture(2, cs, t, m_linear.get()),
                                B::sampledTexture(3, cs, t, m_linear.get()), B::sampledTexture(4, cs, t, m_linear.get()),
                                B::imageStore(5, cs, t, 0), B::imageStore(6, cs, t, 0) });
    m_passLayout->create();
    m_clearLayout.reset(m_rhi->newShaderResourceBindings());
    m_clearLayout->setBindings({ B::uniformBuffer(0, cs, m_layoutUniforms.get()), B::imageStore(1, cs, t, 0),
                                 B::imageStore(2, cs, t, 0), B::imageStore(3, cs, t, 0), B::imageStore(4, cs, t, 0) });
    m_clearLayout->create();
    m_drawLayout.reset(m_rhi->newShaderResourceBindings());
    m_drawLayout->setBindings({ B::uniformBuffer(0, B::VertexStage | B::FragmentStage, m_layoutUniforms.get()),
                                B::sampledTexture(1, B::FragmentStage, t, m_linear.get()),
                                B::sampledTexture(2, B::FragmentStage, m_distanceStandIn.get(), m_nearest.get()),
                                // the shadow block, the directional atlas, the glass map (setShadowMaps())
                                B::uniformBuffer(3, B::FragmentStage, m_layoutUniforms.get()),
                                B::sampledTexture(4, B::FragmentStage, m_distanceStandIn.get(), m_nearest.get()),
                                B::sampledTexture(5, B::FragmentStage, m_distanceStandIn.get(), m_nearest.get()) });
    m_drawLayout->create();
    m_shadowLayout.reset(m_rhi->newShaderResourceBindings());
    m_shadowLayout->setBindings({ B::uniformBufferWithDynamicOffset(0, B::VertexStage | B::FragmentStage, m_layoutUniforms.get(),
                                                                    kShadowUbufSize),
                                  B::sampledTexture(1, B::FragmentStage, t, m_linear.get()) });
    m_shadowLayout->create();
    m_shadowStride = quint32(m_rhi->ubufAligned(kShadowUbufSize));

    auto compute = [&](const char *name, QRhiShaderResourceBindings *layout) {
        std::unique_ptr<QRhiComputePipeline> p(m_rhi->newComputePipeline());
        p->setShaderStage({ QRhiShaderStage::Compute, loadShader(QString::fromLatin1(name)) });
        p->setShaderResourceBindings(layout);
        if (!p->create()) {
            qWarning("FluidSystem: %s can't be created", name);
            m_supported = false;
        }
        return p;
    };
    m_pipes[Source] = compute("fluid_source.comp", m_passLayout.get());
    m_pipes[Advect] = compute("fluid_advect.comp", m_passLayout.get());
    m_pipes[Forces] = compute("fluid_forces.comp", m_passLayout.get());
    m_pipes[Divergence] = compute("fluid_divergence.comp", m_passLayout.get());
    m_pipes[Pressure] = compute("fluid_pressure.comp", m_passLayout.get());
    m_pipes[Project] = compute("fluid_project.comp", m_passLayout.get());
    m_clearPipe = compute("fluid_clear.comp", m_clearLayout.get());
}

void FluidSystem::createPipelines(QRhiRenderPassDescriptor *sceneRp, int sampleCount)
{
    m_drawPipe.reset();
    m_drawAddPipe.reset();
    if (!m_supported)
        return;
    const QShader vs = loadShader(QStringLiteral("fluid_volume.vert"));
    auto pipeline = [&](const QShader &fs, const QRhiGraphicsPipeline::TargetBlend &b) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
        p->setShaderResourceBindings(m_drawLayout.get());
        p->setRenderPassDescriptor(sceneRp);
        p->setSampleCount(sampleCount);
        p->setCullMode(QRhiGraphicsPipeline::Front); // back faces: the box from inside too
        p->setDepthTest(false);                       // the march stops at the scene itself
        p->setDepthWrite(false);
        p->setTargetBlends({ b });
        p->create();
        return p;
    };
    using G = QRhiGraphicsPipeline;
    if (m_rhi->backend() != QRhi::OpenGLES2) {
        // Dual-source: the shader's second output (index 1) is the transmittance per channel,
        // so what's behind is dimmed, and tinted, channel by channel: dst * T + L. Alpha stays
        // premultiplied (a = how much is hidden, on average).
        G::TargetBlend b;
        b.enable = true;
        b.srcColor = G::One;
        b.dstColor = G::Src1Color;
        b.srcAlpha = G::One;
        b.dstAlpha = G::OneMinusSrcAlpha;
        m_drawPipe = pipeline(loadShader(QStringLiteral("fluid_volume.frag")), b);
        return;
    }
    // OpenGL: the same result in two draws (volumeVariant()): dst * (T, 1 - a), then + (L, a).
    QString error;
    const QShader multiplyFs = volumeVariant(true, &error);
    const QShader addFs = multiplyFs.isValid() ? volumeVariant(false, &error) : QShader();
    if (!addFs.isValid()) {
        qWarning("FluidSystem: %s", qPrintable(error));
        return; // no volumes drawn
    }
    G::TargetBlend multiply;
    multiply.enable = true;
    multiply.srcColor = G::Zero;
    multiply.dstColor = G::SrcColor;
    multiply.srcAlpha = G::Zero;
    multiply.dstAlpha = G::SrcAlpha;
    m_drawPipe = pipeline(multiplyFs, multiply);
    G::TargetBlend add;
    add.enable = true;
    add.srcColor = G::One;
    add.dstColor = G::One;
    add.srcAlpha = G::One;
    add.dstAlpha = G::One;
    m_drawAddPipe = pipeline(addFs, add);
}

void FluidSystem::release()
{
    m_volumes.clear();
    m_retired.clear();
    m_retiredBefore.clear();
    m_drawPipe.reset();
    m_drawAddPipe.reset();
    m_shadowPipe.reset();
    m_shadowViewports.clear();
    for (auto &p : m_pipes)
        p.reset();
    m_clearPipe.reset();
    m_passLayout.reset();
    m_clearLayout.reset();
    m_drawLayout.reset();
    m_shadowLayout.reset();
    m_layoutUniforms.reset();
    m_layoutVolume.reset();
    if (m_pendingUpload) {
        m_pendingUpload->release();
        m_pendingUpload = nullptr;
    }
    m_distanceStandIn.reset();
    m_linear.reset();
    m_nearest.reset();
    m_sceneDistance = nullptr;
    m_rhi = nullptr;
    m_supported = false;
    m_pending = false;
    m_drawn = 0;
    m_voxels = 0;
    m_stepsLastFrame = 0;
}

void FluidSystem::setSceneDistance(QRhiTexture *texture)
{
    if (texture == m_sceneDistance)
        return;
    m_sceneDistance = texture;
    for (auto &[id, v] : m_volumes)
        for (auto &s : v.drawSrbs)
            retire(s);
}

void FluidSystem::setShadowMaps(const ShadowMaps *shadows)
{
    if (shadows == m_shadowMaps)
        return;
    m_shadowMaps = shadows;
    for (auto &[id, v] : m_volumes)
        for (auto &s : v.drawSrbs)
            retire(s);
}

void FluidSystem::retire(Volume &v)
{
    for (auto &[key, s] : v.srbs)
        retire(s);
    v.srbs.clear();
    for (auto &s : v.clearSrbs)
        retire(s);
    for (auto &s : v.drawSrbs)
        retire(s);
    for (auto &s : v.shadowSrbs)
        retire(s);
    retire(v.shadowUniforms);
    for (auto *pair : { &v.velocity, &v.density, &v.pressure })
        for (auto &t : *pair)
            retire(t);
    retire(v.divergence);
    retire(v.stepUniforms);
    retire(v.clearUniforms);
    retire(v.drawUniforms);
}

QRhiShaderResourceBindings *FluidSystem::passBindings(Volume &v, Pass pass)
{
    const int key = int(pass) * 8 + v.v * 4 + v.d * 2 + v.p;
    auto it = v.srbs.find(key);
    if (it != v.srbs.end())
        return it->second.get();
    using B = QRhiShaderResourceBinding;
    const auto cs = B::ComputeStage;
    QRhiTexture *none = m_layoutVolume.get();
    QRhiTexture *velIn = v.velocity[size_t(v.v)].get(), *velOut = v.velocity[size_t(1 - v.v)].get();
    QRhiTexture *denIn = v.density[size_t(v.d)].get(), *denOut = v.density[size_t(1 - v.d)].get();
    QRhiTexture *preIn = v.pressure[size_t(v.p)].get(), *preOut = v.pressure[size_t(1 - v.p)].get();
    QRhiTexture *div = v.divergence.get();
    // Inputs 1..4, outputs 5, 6: a texture is never an input and an output of the same pass.
    QRhiTexture *in[4] = { velIn, denIn, preIn, div };
    QRhiTexture *out5 = velOut;
    switch (pass) {
    case Source:
    case Advect:
    case Forces:
    case Project: out5 = velOut; break;
    case Divergence:
        out5 = div;
        in[3] = none;
        break;
    case Pressure: out5 = preOut; break;
    default: break;
    }
    std::unique_ptr<QRhiShaderResourceBindings> srb(m_rhi->newShaderResourceBindings());
    srb->setBindings({ B::uniformBufferWithDynamicOffset(0, cs, v.stepUniforms.get(), kFluidUbufSize),
                       B::sampledTexture(1, cs, in[0], m_linear.get()), B::sampledTexture(2, cs, in[1], m_linear.get()),
                       B::sampledTexture(3, cs, in[2], m_linear.get()), B::sampledTexture(4, cs, in[3], m_linear.get()),
                       B::imageStore(5, cs, out5, 0), B::imageStore(6, cs, denOut, 0) });
    srb->create();
    return v.srbs.emplace(key, std::move(srb)).first->second.get();
}

void FluidSystem::simulate(QRhiCommandBuffer *cb, const Scene *scene, float seconds)
{
    m_pending = false;
    m_stepsLastFrame = 0;
    m_drawn = 0;
    m_voxels = 0;
    m_retiredBefore = std::move(m_retired);
    m_retired.clear();
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    if (m_pendingUpload) { // the distance stand-in's pixel, from initialize()
        rub->merge(m_pendingUpload);
        m_pendingUpload->release();
        m_pendingUpload = nullptr;
    }
    if (!m_supported || !scene) {
        for (auto &[id, v] : m_volumes)
            retire(v);
        m_volumes.clear();
        cb->resourceUpdate(rub);
        return;
    }
    const Timeline &timeline = scene->timeline();
    const double fps = qMax(1.0, timeline.fps);
    const float dt = float(1.0 / fps);
    const int target = qMax(0, int(std::floor(double(seconds) * fps + 1e-4)));
    const quint32 stride = quint32(m_rhi->ubufAligned(kFluidUbufSize));

    std::vector<const Entity *> sources;
    std::vector<Entity> sourceReference;
    for (const Entity &e : scene->entities()) {
        if (e.fluidSource) {
            sources.push_back(&e);
            sourceReference.push_back(at(e, timeline.start));
        }
    }

    struct Dispatch
    {
        QRhiComputePipeline *pipe;
        QRhiShaderResourceBindings *srb;
        quint32 offset; // dynamic offset (pass bindings) or ~0 (clear)
        int gx, gy, gz;
    };
    std::vector<Dispatch> dispatches;
    double budget = FrameBudget;
    for (auto &[id, v] : m_volumes)
        v.seen = false;

    for (const Entity &entity : scene->entities()) {
        if (!entity.fluid || !entity.fluid->enabled)
            continue;
        Volume &v = m_volumes[entity.id];
        v.seen = true;
        const FluidVolume &f = *entity.fluid;
        v.settings = f;
        v.model = boxFrame(entity.transform);
        v.model.scale(f.size);

        // The grid: cubic voxels, `resolution` along the longest side.
        const float longest = qMax(f.size.x(), qMax(f.size.y(), f.size.z()));
        const float voxel = longest / float(qMax(1, f.resolution));
        const int nx = qMax(1, int(std::lround(f.size.x() / voxel)));
        const int ny = qMax(1, int(std::lround(f.size.y() / voxel)));
        const int nz = qMax(1, int(std::lround(f.size.z() / voxel)));
        if (!v.divergence || nx != v.nx || ny != v.ny || nz != v.nz) {
            retire(v);
            v.nx = nx;
            v.ny = ny;
            v.nz = nz;
            const auto flags = QRhiTexture::ThreeDimensional | QRhiTexture::UsedWithLoadStore;
            for (int i = 0; i < 2; ++i) {
                v.velocity[size_t(i)].reset(m_rhi->newTexture(QRhiTexture::RGBA16F, nx, ny, nz, 1, flags));
                v.density[size_t(i)].reset(m_rhi->newTexture(QRhiTexture::RGBA16F, nx, ny, nz, 1, flags));
                v.pressure[size_t(i)].reset(m_rhi->newTexture(QRhiTexture::R32F, nx, ny, nz, 1, flags));
                for (auto *t : { v.velocity[size_t(i)].get(), v.density[size_t(i)].get(), v.pressure[size_t(i)].get() }) {
                    t->create();
                    fixVolumeStorageView(m_rhi, t);
                }
            }
            v.divergence.reset(m_rhi->newTexture(QRhiTexture::R32F, nx, ny, nz, 1, flags));
            v.divergence->create();
            fixVolumeStorageView(m_rhi, v.divergence.get());
            v.stepUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, quint32(MaxStepsPerFrame) * stride));
            v.stepUniforms->create();
            v.clearUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
            v.clearUniforms->create();
            v.drawUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kVolumeUbufSize));
            v.drawUniforms->create();
            using B = QRhiShaderResourceBinding;
            const auto cs = B::ComputeStage;
            for (int i = 0; i < 2; ++i) {
                v.clearSrbs[size_t(i)].reset(m_rhi->newShaderResourceBindings());
                v.clearSrbs[size_t(i)]->setBindings({ B::uniformBuffer(0, cs, v.clearUniforms.get()),
                                                      B::imageStore(1, cs, v.velocity[size_t(i)].get(), 0),
                                                      B::imageStore(2, cs, v.density[size_t(i)].get(), 0),
                                                      B::imageStore(3, cs, v.pressure[size_t(i)].get(), 0),
                                                      B::imageStore(4, cs, v.divergence.get(), 0) });
                v.clearSrbs[size_t(i)]->create();
            }
            v.stepsDone = -1;
        }
        v.voxelSize = voxel;
        m_voxels += quint64(nx) * quint64(ny) * quint64(nz);
        ++m_drawn;

        const Entity reference = at(entity, timeline.start);
        if (reference.fluid != v.reference.fluid || reference.transform != v.reference.transform
            || reference.animation != v.reference.animation || !sameEntities(sourceReference, v.sourceReference)
            || target < v.stepsDone) {
            v.reference = reference;
            v.sourceReference = sourceReference;
            v.stepsDone = -1;
        }
        const int gx = groups(nx), gy = groups(ny), gz = groups(nz);
        if (v.stepsDone < 0) { // from cleared fields
            const quint32 grid[4] = { quint32(nx), quint32(ny), quint32(nz), 0 };
            rub->updateDynamicBuffer(v.clearUniforms.get(), 0, 16, grid);
            for (auto &s : v.clearSrbs)
                dispatches.push_back({ m_clearPipe.get(), s.get(), ~0u, gx, gy, gz });
            v.v = v.d = v.p = 0;
            v.stepsDone = 0;
        }

        // Steps due this frame, within what's left of the budget (at least one).
        const double stepCost = double(nx) * ny * nz * (5 + f.pressureIterations);
        const int affordable = int(qBound(1.0, budget / stepCost, double(MaxStepsPerFrame)));
        const int count = qMin(target - v.stepsDone, affordable);
        if (count <= 0)
            continue;
        budget = qMax(0.0, budget - stepCost * count);
        if (target - v.stepsDone > count)
            m_pending = true;

        QByteArray data(qsizetype(count) * stride, '\0');
        for (int i = 0; i < count; ++i) {
            const int k = v.stepsDone + i;
            const Entity box = at(entity, timeline.start + k); // the volume during this step
            const FluidVolume &bf = *box.fluid;
            // World -> grid: into the box's frame, then voxels from its corner.
            const QMatrix4x4 frame = boxFrame(box.transform);
            const QMatrix4x4 toLocal = frame.inverted();
            const QVector3D half = bf.size * 0.5f;
            auto toGrid = [&](const QVector3D &world) { return (toLocal.map(world) + half) / voxel; };
            const QVector3D up = toLocal.mapVector(QVector3D(0.0f, 1.0f, 0.0f)).normalized();

            float *u = reinterpret_cast<float *>(data.data() + qsizetype(i) * stride);
            int sourceCount = 0;
            for (const Entity *se : sources) {
                if (sourceCount == MaxSources)
                    break;
                const Entity s = at(*se, timeline.start + k);
                if (!s.fluidSource->enabled)
                    continue;
                const QVector3D c = toGrid(s.transform.position);
                const QVector3D vel = toLocal.mapVector(s.fluidSource->velocity) / voxel;
                float *src = u + 20 + sourceCount * 12;
                src[0] = c.x(); src[1] = c.y(); src[2] = c.z(); src[3] = s.fluidSource->radius / voxel;
                src[4] = s.fluidSource->density; src[5] = s.fluidSource->temperature;
                src[8] = vel.x(); src[9] = vel.y(); src[10] = vel.z();
                ++sourceCount;
            }
            const quint32 grid[4] = { quint32(nx), quint32(ny), quint32(nz), quint32(sourceCount) };
            std::memcpy(u, grid, sizeof(grid));
            u[4] = dt;                              // step: dt, time at its end, voxel size
            u[5] = float((k + 1) * double(dt));
            u[6] = voxel;
            u[8] = bf.dissipation;                  // solver
            u[9] = bf.cooling;
            u[10] = bf.buoyancy;
            u[11] = bf.weight;
            u[12] = bf.vorticity;                   // solver2
            // Stratified air: its temperature at a grid point p is solver2.y * dot(p, up) +
            // solver2.z, 0 at the box's lowest corner, rising by ambientLapse per world unit.
            float lowest = 0.0f;
            for (int c = 0; c < 8; ++c) {
                const QVector3D corner(c & 1 ? float(nx) : 0.0f, c & 2 ? float(ny) : 0.0f, c & 4 ? float(nz) : 0.0f);
                lowest = qMin(lowest, QVector3D::dotProduct(corner, up));
            }
            u[13] = bf.ambientLapse * voxel;        // per voxel of height
            u[14] = -u[13] * lowest;
            u[16] = up.x(); u[17] = up.y(); u[18] = up.z(); // up, in grid axes

            // The step's passes, swapping each pair after its output is written.
            const quint32 offset = quint32(i) * stride;
            auto pass = [&](Pass p) { dispatches.push_back({ m_pipes[p].get(), passBindings(v, p), offset, gx, gy, gz }); };
            pass(Source);
            v.v ^= 1;
            v.d ^= 1;
            pass(Advect);
            v.v ^= 1;
            v.d ^= 1;
            pass(Forces);
            v.v ^= 1;
            pass(Divergence);
            for (int it = 0; it < bf.pressureIterations; ++it) {
                pass(Pressure);
                v.p ^= 1;
            }
            pass(Project);
            v.v ^= 1;
        }
        rub->updateDynamicBuffer(v.stepUniforms.get(), 0, quint32(data.size()), data.constData());
        v.stepsDone += count;
        m_stepsLastFrame += count;
    }
    for (auto it = m_volumes.begin(); it != m_volumes.end();) {
        if (it->second.seen) {
            ++it;
        } else {
            retire(it->second);
            it = m_volumes.erase(it);
        }
    }

    if (dispatches.empty()) {
        cb->resourceUpdate(rub);
        return;
    }
    // One compute pass per dispatch: each pass reads what the one before wrote. On Direct3D 11 a
    // texture still bound for writing can't be bound for reading (the runtime unbinds the read,
    // which then gives zeros), and QRhi unbinds everything only when a pass begins.
    for (size_t i = 0; i < dispatches.size(); ++i) {
        const Dispatch &d = dispatches[i];
        cb->beginComputePass(i == 0 ? rub : nullptr);
        cb->setComputePipeline(d.pipe);
        if (d.offset == ~0u) {
            cb->setShaderResources(d.srb);
        } else {
            const QRhiCommandBuffer::DynamicOffset offset(0, d.offset);
            cb->setShaderResources(d.srb, 1, &offset);
        }
        cb->dispatch(d.gx, d.gy, d.gz);
        cb->endComputePass();
    }
}

void FluidSystem::prepareDraw(QRhiResourceUpdateBatch *rub, const QMatrix4x4 &clipViewProj, const QVector3D &eye,
                              const QVector3D &lightDirection, const QVector3D &lightColor, bool hasLight,
                              int shadowLight, const QSize &viewportPixels, float seconds)
{
    if (!m_supported)
        return;
    for (auto &[id, v] : m_volumes) {
        if (!v.drawUniforms)
            continue;
        float data[kVolumeUbufSize / 4] = {};
        std::memcpy(data, clipViewProj.constData(), 64);
        std::memcpy(data + 16, v.model.constData(), 64);
        std::memcpy(data + 32, v.model.inverted().constData(), 64);
        float *x = data + 48;
        x[0] = eye.x(); x[1] = eye.y(); x[2] = eye.z(); x[3] = 1.0f;
        x[4] = lightDirection.x(); x[5] = lightDirection.y(); x[6] = lightDirection.z(); x[7] = hasLight ? 1.0f : 0.0f;
        x[8] = lightColor.x(); x[9] = lightColor.y(); x[10] = lightColor.z();
        x[12] = v.settings.density;                 // look
        x[13] = float(v.settings.samples);
        x[14] = v.settings.emission;
        x[15] = m_sceneDistance ? 1.0f : 0.0f;
        putColor(x + 16, v.settings.color);
        putColor(x + 20, v.settings.emissionColor);
        x[24] = float(v.nx); x[25] = float(v.ny); x[26] = float(v.nz); x[27] = v.voxelSize;
        x[28] = float(viewportPixels.width());
        x[29] = float(viewportPixels.height());
        x[30] = seconds;
        putColor(x + 32, v.settings.extinction);
        x[36] = m_shadowMaps && hasLight ? float(shadowLight) : -1.0f; // shadow
        rub->updateDynamicBuffer(v.drawUniforms.get(), 0, kVolumeUbufSize, data);
    }
}

void FluidSystem::draw(QRhiCommandBuffer *cb, const QRhiViewport &viewport)
{
    if (!m_supported || !m_drawPipe || !m_shadowMaps)
        return;
    for (auto &[id, v] : m_volumes) {
        if (!v.drawUniforms || v.stepsDone < 0)
            continue;
        auto &srb = v.drawSrbs[size_t(v.d)];
        if (!srb) {
            using B = QRhiShaderResourceBinding;
            srb.reset(m_rhi->newShaderResourceBindings());
            QList<QRhiShaderResourceBinding> bindings = {
                B::uniformBuffer(0, B::VertexStage | B::FragmentStage, v.drawUniforms.get()),
                B::sampledTexture(1, B::FragmentStage, v.density[size_t(v.d)].get(), m_linear.get()),
                B::sampledTexture(2, B::FragmentStage, m_sceneDistance ? m_sceneDistance : m_distanceStandIn.get(), m_nearest.get())
            };
            bindings.append(m_shadowMaps->directionalBindings(3, 4, 5));
            srb->setBindings(bindings.cbegin(), bindings.cend());
            srb->create();
        }
        for (QRhiGraphicsPipeline *pipe : { m_drawPipe.get(), m_drawAddPipe.get() }) {
            if (!pipe)
                continue; // one pipeline (dual-source) except on OpenGL
            cb->setGraphicsPipeline(pipe);
            cb->setViewport(viewport);
            cb->setShaderResources(srb.get());
            cb->draw(36);
        }
    }
}

void FluidSystem::createShadowPipeline(QRhiRenderPassDescriptor *translucencyRp)
{
    m_shadowPipe.reset();
    if (!m_supported || !translucencyRp)
        return;
    using G = QRhiGraphicsPipeline;
    m_shadowPipe.reset(m_rhi->newGraphicsPipeline());
    m_shadowPipe->setShaderStages({ { QRhiShaderStage::Vertex, loadShader(QStringLiteral("fluid_shadow.vert")) },
                                    { QRhiShaderStage::Fragment, loadShader(QStringLiteral("fluid_shadow.frag")) } });
    m_shadowPipe->setShaderResourceBindings(m_shadowLayout.get());
    m_shadowPipe->setRenderPassDescriptor(translucencyRp);
    // The faces toward the light: one fragment per texel, where the light enters the box (a
    // point light inside the box casts no volume shadow).
    m_shadowPipe->setCullMode(G::Back);
    // Like a blend material's shadow (ShadowMaps::createMaterialPipeline): rgb multiplied by
    // what gets through, a = the nearest linear depth.
    G::TargetBlend b;
    b.enable = true;
    b.srcColor = G::DstColor;
    b.dstColor = G::Zero;
    b.srcAlpha = G::One;
    b.dstAlpha = G::One;
    b.opAlpha = G::Min;
    m_shadowPipe->setTargetBlends({ b });
    if (!m_shadowPipe->create()) {
        qWarning("FluidSystem: the shadow pipeline can't be created");
        m_shadowPipe.reset();
    }
}

void FluidSystem::prepareShadows(QRhiResourceUpdateBatch *rub, const std::vector<ShadowMaps::TranslucentView> &views)
{
    m_shadowViewports.clear();
    if (!m_supported || !m_shadowPipe)
        return;
    const int count = qMin(int(views.size()), ShadowMaps::ViewCount);
    for (int i = 0; i < count; ++i)
        m_shadowViewports.push_back(views[size_t(i)].viewport);
    if (count == 0)
        return;
    for (auto &[id, v] : m_volumes) {
        if (!v.drawUniforms)
            continue;
        if (!v.shadowUniforms) {
            v.shadowUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                                    quint32(ShadowMaps::ViewCount) * m_shadowStride));
            v.shadowUniforms->create();
        }
        QByteArray data(qsizetype(count) * m_shadowStride, char(0));
        const QMatrix4x4 worldToBox = v.model.inverted();
        for (int i = 0; i < count; ++i) {
            const ShadowMaps::TranslucentView &view = views[size_t(i)];
            float *x = reinterpret_cast<float *>(data.data() + qsizetype(i) * m_shadowStride);
            std::memcpy(x, view.clip.constData(), 64);
            std::memcpy(x + 16, v.model.constData(), 64);
            std::memcpy(x + 32, worldToBox.constData(), 64);
            x[48] = view.light.x(); // light
            x[49] = view.light.y();
            x[50] = view.light.z();
            x[51] = view.point ? 1.0f : 0.0f;
            x[52] = view.point ? 1.0f : 0.0f; // view
            x[53] = view.nearPlane;
            x[54] = view.farPlane;
            x[56] = v.settings.density; // look
            x[57] = float(v.settings.samples);
            putColor(x + 60, v.settings.extinction);
        }
        rub->updateDynamicBuffer(v.shadowUniforms.get(), 0, quint32(data.size()), data.constData());
    }
}

void FluidSystem::drawShadows(QRhiCommandBuffer *cb)
{
    if (!m_supported || !m_shadowPipe || m_shadowViewports.empty())
        return;
    bool bound = false;
    for (auto &[id, v] : m_volumes) {
        if (!v.shadowUniforms || v.stepsDone < 0)
            continue;
        auto &srb = v.shadowSrbs[size_t(v.d)];
        if (!srb) {
            using B = QRhiShaderResourceBinding;
            srb.reset(m_rhi->newShaderResourceBindings());
            srb->setBindings({ B::uniformBufferWithDynamicOffset(0, B::VertexStage | B::FragmentStage, v.shadowUniforms.get(),
                                                                 kShadowUbufSize),
                               B::sampledTexture(1, B::FragmentStage, v.density[size_t(v.d)].get(), m_linear.get()) });
            srb->create();
        }
        if (!bound) {
            cb->setGraphicsPipeline(m_shadowPipe.get());
            bound = true;
        }
        for (size_t i = 0; i < m_shadowViewports.size(); ++i) {
            const QRectF &r = m_shadowViewports[i];
            cb->setViewport({ float(r.x()), float(r.y()), float(r.width()), float(r.height()) });
            const QRhiCommandBuffer::DynamicOffset offset(0, quint32(i) * m_shadowStride);
            cb->setShaderResources(srb.get(), 1, &offset);
            cb->draw(36);
        }
    }
}

int FluidSystem::shadowDrawCount() const
{
    if (!m_supported || !m_shadowPipe)
        return 0;
    int volumes = 0;
    for (const auto &[id, v] : m_volumes)
        volumes += v.shadowUniforms && v.stepsDone >= 0 ? 1 : 0;
    return volumes * int(m_shadowViewports.size());
}
