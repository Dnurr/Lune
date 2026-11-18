#include "postprocess.h"

#include <QFile>
#include <QImage>
#include <QtMath>
#include <cmath>

namespace
{
// PostBuf: inputSize, outputSize, camera, params0, params1
constexpr int kPostUbufSize = 5 * 16;
// Sensor height the depth of field's focal length assumes (full frame, 36 x 24 mm).
constexpr float kSensorHeightMm = 24.0f;

QShader loadPostShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("PostProcessor: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}
} // namespace

void PostProcessor::initialize(QRhi *rhi)
{
    release();
    m_rhi = rhi;
    m_linear.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                     QRhiSampler::ClampToEdge));
    m_linear->create();
    m_nearest.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_nearest->create();
    for (auto &b : m_ubufs) {
        b.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kPostUbufSize));
        b->create();
    }
    // Stand-in for unused textures: never sampled meaningfully, white like the depth far plane.
    m_dummy.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_dummy->create();
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    QImage white(1, 1, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    rub->uploadTexture(m_dummy.get(), white);
    m_pendingUpload = rub;
}

void PostProcessor::release()
{
    m_srbs.clear();
    for (auto &p : m_pipelines)
        p.reset();
    m_layout.reset();
    m_hdrRp.reset();
    for (auto &t : m_hdr)
        t = {};
    for (auto &t : m_bloomDown)
        t = {};
    for (auto &t : m_bloomUp)
        t = {};
    m_ldr = {};
    for (auto &b : m_ubufs)
        b.reset();
    if (m_pendingUpload) {
        m_pendingUpload->release();
        m_pendingUpload = nullptr;
    }
    m_dummy.reset();
    m_linear.reset();
    m_nearest.reset();
    m_depth = nullptr;
    m_ldrRp = nullptr;
    m_rhi = nullptr;
    m_pixelSize = {};
}

PostProcessor::Target PostProcessor::newTarget(const QSize &size, QRhiTexture::Format format, bool hdr)
{
    Target t;
    t.texture.reset(m_rhi->newTexture(format, size.expandedTo(QSize(1, 1)), 1, QRhiTexture::RenderTarget));
    t.texture->create();
    t.target.reset(m_rhi->newTextureRenderTarget({ QRhiColorAttachment(t.texture.get()) }));
    if (hdr) {
        if (!m_hdrRp) // made once: every HDR target has the same format, and the pipelines use it
            m_hdrRp.reset(t.target->newCompatibleRenderPassDescriptor());
        t.target->setRenderPassDescriptor(m_hdrRp.get());
    } else {
        t.target->setRenderPassDescriptor(m_ldrRp);
    }
    t.target->create();
    return t;
}

void PostProcessor::resize(const QSize &pixelSize, QRhiTexture::Format hdrFormat, QRhiTexture *depth,
                           QRhiRenderPassDescriptor *ldrRp)
{
    m_srbs.clear(); // they point at the old textures
    m_pixelSize = pixelSize;
    m_depth = depth;
    m_ldrRp = ldrRp;
    for (auto &t : m_hdr)
        t = newTarget(pixelSize, hdrFormat, true);
    QSize level = pixelSize;
    for (int i = 0; i < BloomLevels; ++i) {
        level = (level / 2).expandedTo(QSize(1, 1));
        m_bloomDown[size_t(i)] = newTarget(level, hdrFormat, true);
        if (i < BloomLevels - 1)
            m_bloomUp[size_t(i)] = newTarget(level, hdrFormat, true);
    }
    m_ldr = newTarget(pixelSize, QRhiTexture::RGBA8, false);

    if (!m_layout) {
        using B = QRhiShaderResourceBinding;
        m_layout.reset(m_rhi->newShaderResourceBindings());
        m_layout->setBindings({ B::uniformBuffer(0, B::FragmentStage, m_ubufs[0].get()),
                                B::sampledTexture(1, B::FragmentStage, m_dummy.get(), m_linear.get()),
                                B::sampledTexture(2, B::FragmentStage, m_dummy.get(), m_linear.get()),
                                B::sampledTexture(3, B::FragmentStage, m_dummy.get(), m_nearest.get()) });
        m_layout->create();
    }
}

void PostProcessor::createPipelines(const QShader &fullscreen)
{
    auto pipeline = [&](const char *frag, QRhiRenderPassDescriptor *rp) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, fullscreen },
                             { QRhiShaderStage::Fragment, loadPostShader(QString::fromLatin1(frag)) } });
        p->setShaderResourceBindings(m_layout.get());
        p->setRenderPassDescriptor(rp);
        p->create();
        return p;
    };
    for (auto &p : m_pipelines)
        p.reset();
    m_pipelines[DepthOfField] = pipeline("post_dof.frag", m_hdrRp.get());
    m_pipelines[BloomDown] = pipeline("post_bloom_down.frag", m_hdrRp.get());
    m_pipelines[BloomUp] = pipeline("post_bloom_up.frag", m_hdrRp.get());
    m_pipelines[BloomComposite] = pipeline("post_bloom_composite.frag", m_hdrRp.get());
    m_pipelines[ChromaticAberration] = pipeline("post_chromatic.frag", m_ldrRp);
    m_pipelines[Vignette] = pipeline("post_vignette.frag", m_ldrRp);
    m_pipelines[FilmGrain] = pipeline("post_grain.frag", m_ldrRp);
    m_pipelines[Fxaa] = pipeline("post_fxaa.frag", m_ldrRp);
}

QRhiShaderResourceBindings *PostProcessor::bindings(Pass pass, QRhiTexture *a, QRhiTexture *b)
{
    const auto key = std::make_tuple(int(pass), a, b);
    auto it = m_srbs.find(key);
    if (it != m_srbs.end())
        return it->second.get();
    using B = QRhiShaderResourceBinding;
    std::unique_ptr<QRhiShaderResourceBindings> srb(m_rhi->newShaderResourceBindings());
    srb->setBindings({ B::uniformBuffer(0, B::FragmentStage, m_ubufs[size_t(pass)].get()),
                       B::sampledTexture(1, B::FragmentStage, a ? a : m_dummy.get(), m_linear.get()),
                       B::sampledTexture(2, B::FragmentStage, b ? b : m_dummy.get(), m_linear.get()),
                       B::sampledTexture(3, B::FragmentStage, m_depth ? m_depth : m_dummy.get(), m_nearest.get()) });
    srb->create();
    return m_srbs.emplace(key, std::move(srb)).first->second.get();
}

void PostProcessor::writeUniforms(QRhiResourceUpdateBatch *rub, Pass pass, const QSize &input, const QSize &output,
                                  const float params0[4], const float params1[4])
{
    float data[kPostUbufSize / 4] = {};
    data[0] = float(input.width());
    data[1] = float(input.height());
    data[2] = 1.0f / float(qMax(1, input.width()));
    data[3] = 1.0f / float(qMax(1, input.height()));
    data[4] = float(output.width());
    data[5] = float(output.height());
    data[6] = 1.0f / float(qMax(1, output.width()));
    data[7] = 1.0f / float(qMax(1, output.height()));
    for (int i = 0; i < 4; ++i) {
        data[8 + i] = m_camera[i];
        data[12 + i] = params0 ? params0[i] : 0.0f;
        data[16 + i] = params1 ? params1[i] : 0.0f;
    }
    rub->updateDynamicBuffer(m_ubufs[size_t(pass)].get(), 0, kPostUbufSize, data);
}

void PostProcessor::prepare(QRhiResourceUpdateBatch *rub, const Frame &frame)
{
    if (m_pendingUpload) { // the stand-in texture's pixel, from initialize()
        rub->merge(m_pendingUpload);
        m_pendingUpload->release();
        m_pendingUpload = nullptr;
    }
    const PostProcess &s = frame.settings;
    m_dof = s.enabled && s.depthOfField;
    m_bloom = s.enabled && s.bloom;
    m_ldrPasses.clear();
    if (s.enabled) {
        if (s.chromaticAberration)
            m_ldrPasses.push_back(ChromaticAberration);
        if (s.vignette)
            m_ldrPasses.push_back(Vignette);
        if (s.filmGrain)
            m_ldrPasses.push_back(FilmGrain);
        if (s.fxaa)
            m_ldrPasses.push_back(Fxaa);
    }

    // camera: near, far, depth texture (1) or the stand-in (-1), seconds. The depth values are
    // 0..1 on every backend: the projection is OpenGL's, remapped by clipSpaceCorrMatrix().
    m_camera[0] = frame.nearPlane;
    m_camera[1] = frame.farPlane;
    m_camera[2] = m_depth ? 1.0f : -1.0f;
    m_camera[3] = frame.seconds;

    const QSize full = m_pixelSize;
    // Pixel sizes in the settings are for a 1080 pixel tall image: the look doesn't change
    // with the resolution (viewport or render).
    const float pixelScale = float(full.height()) / 1080.0f;
    if (m_dof) {
        const float focalMm = 0.5f * kSensorHeightMm / std::tan(qDegreesToRadians(frame.fieldOfView) * 0.5f);
        const float p0[4] = { s.focusDistance, s.fStop, s.maxBlur * pixelScale, focalMm };
        writeUniforms(rub, DepthOfField, full, full, p0);
    }
    if (m_bloom) {
        QSize input = full;
        for (int i = 0; i < BloomLevels; ++i) {
            const QSize output = m_bloomDown[size_t(i)].texture->pixelSize();
            const float p0[4] = { float(i), s.bloomThreshold, s.bloomIntensity, s.bloomRadius };
            writeUniforms(rub, Pass(BloomDown + i), input, output, p0);
            input = output;
        }
        for (int i = BloomLevels - 2; i >= 0; --i) {
            const QSize output = m_bloomUp[size_t(i)].texture->pixelSize();
            const QSize smaller = m_bloomDown[size_t(i + 1)].texture->pixelSize(); // texture a: one level down
            const float p0[4] = { float(i), s.bloomThreshold, s.bloomIntensity, s.bloomRadius };
            writeUniforms(rub, Pass(BloomUp + i), smaller, output, p0);
        }
        const float p0[4] = { 0.0f, s.bloomThreshold, s.bloomIntensity, s.bloomRadius };
        writeUniforms(rub, BloomComposite, full, full, p0);
    }
    const float aspect = float(full.width()) / float(qMax(1, full.height()));
    for (Pass p : m_ldrPasses) {
        float p0[4] = {};
        switch (p) {
        case ChromaticAberration: p0[0] = s.chromaticIntensity; p0[1] = pixelScale; break;
        case Vignette: p0[0] = s.vignetteIntensity; p0[1] = s.vignetteSmoothness; p0[2] = aspect; break;
        case FilmGrain: p0[0] = s.grainIntensity; p0[1] = s.grainSize * pixelScale; p0[2] = s.grainResponse; break;
        default: break;
        }
        writeUniforms(rub, p, full, full, p0);
    }
}

int PostProcessor::passCount() const
{
    return (m_dof ? 1 : 0) + (m_bloom ? 2 * BloomLevels : 0) + ldrPassCount();
}

void PostProcessor::fullscreen(QRhiCommandBuffer *cb, QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb,
                               const QSize &size)
{
    cb->setGraphicsPipeline(pipe);
    cb->setViewport({ 0, 0, float(size.width()), float(size.height()) });
    cb->setShaderResources(srb);
    cb->draw(3);
}

QRhiTexture *PostProcessor::renderHdr(QRhiCommandBuffer *cb, QRhiTexture *scene)
{
    const QColor black(Qt::black);
    QRhiTexture *current = scene;
    int next = 0; // ping-pong target
    auto pass = [&](const Target &target, QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb) {
        cb->beginPass(target.target.get(), black, { 1.0f, 0 });
        fullscreen(cb, pipe, srb, target.texture->pixelSize());
        cb->endPass();
    };

    if (m_dof) {
        const Target &out = m_hdr[size_t(next)];
        pass(out, m_pipelines[DepthOfField].get(), bindings(DepthOfField, current, nullptr));
        current = out.texture.get();
        next ^= 1;
    }
    if (m_bloom) {
        QRhiTexture *input = current;
        for (int i = 0; i < BloomLevels; ++i) {
            pass(m_bloomDown[size_t(i)], m_pipelines[BloomDown].get(), bindings(Pass(BloomDown + i), input, nullptr));
            input = m_bloomDown[size_t(i)].texture.get();
        }
        QRhiTexture *smaller = m_bloomDown[size_t(BloomLevels - 1)].texture.get();
        for (int i = BloomLevels - 2; i >= 0; --i) {
            pass(m_bloomUp[size_t(i)], m_pipelines[BloomUp].get(),
                 bindings(Pass(BloomUp + i), smaller, m_bloomDown[size_t(i)].texture.get()));
            smaller = m_bloomUp[size_t(i)].texture.get();
        }
        const Target &out = m_hdr[size_t(next)];
        pass(out, m_pipelines[BloomComposite].get(), bindings(BloomComposite, current, smaller));
        current = out.texture.get();
        next ^= 1;
    }
    return current;
}

void PostProcessor::drawLdr(QRhiCommandBuffer *cb, int index, QRhiTexture *input, const QSize &size)
{
    const Pass p = m_ldrPasses[size_t(index)];
    fullscreen(cb, m_pipelines[p].get(), bindings(p, input, nullptr), size);
}
