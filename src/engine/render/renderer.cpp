#include "renderer.h"

#include "camera.h"

#include <QColor>
#include <QFile>
#include <rhi/qrhi.h>

#include <cstring>

namespace
{
constexpr int kBlurDownscale = 4; // blur resolution = target / 4
constexpr int kGridUbufSize = 64 + 8 * 16;
constexpr int kCompositeUbufSize = 2 * 16 + Renderer::MaxGlassRects * 16;

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Renderer: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

void putVec4(float *dst, const QColor &c, float w)
{
    dst[0] = float(c.redF());
    dst[1] = float(c.greenF());
    dst[2] = float(c.blueF());
    dst[3] = w;
}
} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() = default;

void Renderer::setGlassRects(const QList<QRectF> &rects, float cornerRadius)
{
    m_glassRects = rects.mid(0, MaxGlassRects);
    m_glassRadius = cornerRadius;
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

void Renderer::initialize(QRhi *rhi, QRhiRenderTarget *target)
{
    if (m_rhi != rhi) {
        release();
        m_rhi = rhi;

        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_sampler->create();

        auto ubuf = [this](int size) {
            std::unique_ptr<QRhiBuffer> b(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, size));
            b->create();
            return b;
        };
        m_gridUbuf = ubuf(kGridUbufSize);
        m_downUbuf = ubuf(16);
        for (auto &b : m_blurUbufs)
            b = ubuf(16);
        m_compositeUbuf = ubuf(kCompositeUbufSize);
    }

    const QSize size = target->pixelSize();
    if (size != m_pixelSize || !m_sceneTex) {
        m_pixelSize = size;
        createSizeDependentResources();
    }

    createPipelines(target);
}

void Renderer::createSizeDependentResources()
{
    const QSize blurSize = (m_pixelSize / kBlurDownscale).expandedTo(QSize(1, 1));

    auto texture = [this](const QSize &s) {
        std::unique_ptr<QRhiTexture> t(m_rhi->newTexture(QRhiTexture::RGBA8, s, 1, QRhiTexture::RenderTarget));
        t->create();
        return t;
    };
    m_sceneTex = texture(m_pixelSize);
    m_blurA = texture(blurSize);
    m_blurB = texture(blurSize);

    auto renderTarget = [this](QRhiTexture *t) {
        std::unique_ptr<QRhiTextureRenderTarget> rt(m_rhi->newTextureRenderTarget({ QRhiColorAttachment(t) }));
        // All offscreen targets have the same format: one render pass descriptor (and the
        // pipelines built with it) works for all of them, also after a resize.
        if (!m_offscreenRp)
            m_offscreenRp.reset(rt->newCompatibleRenderPassDescriptor());
        rt->setRenderPassDescriptor(m_offscreenRp.get());
        rt->create();
        return rt;
    };
    m_sceneRt = renderTarget(m_sceneTex.get());
    m_blurARt = renderTarget(m_blurA.get());
    m_blurBRt = renderTarget(m_blurB.get());

    using B = QRhiShaderResourceBinding;
    const auto stages = B::VertexStage | B::FragmentStage;
    auto srb = [this](std::initializer_list<QRhiShaderResourceBinding> bindings) {
        std::unique_ptr<QRhiShaderResourceBindings> s(m_rhi->newShaderResourceBindings());
        s->setBindings(bindings);
        s->create();
        return s;
    };

    m_gridSrb = srb({ B::uniformBuffer(0, stages, m_gridUbuf.get()) });
    m_downSrb = srb({ B::uniformBuffer(0, stages, m_downUbuf.get()),
                      B::sampledTexture(1, B::FragmentStage, m_sceneTex.get(), m_sampler.get()) });
    // Ping-pong: H reads A writes B, V reads B writes A (twice).
    for (int i = 0; i < 4; ++i) {
        QRhiTexture *src = (i % 2 == 0) ? m_blurA.get() : m_blurB.get();
        m_blurSrbs[i] = srb({ B::uniformBuffer(0, stages, m_blurUbufs[i].get()),
                              B::sampledTexture(1, B::FragmentStage, src, m_sampler.get()) });
    }
    m_compositeSrb = srb({ B::uniformBuffer(0, stages, m_compositeUbuf.get()),
                           B::sampledTexture(1, B::FragmentStage, m_sceneTex.get(), m_sampler.get()),
                           B::sampledTexture(2, B::FragmentStage, m_blurA.get(), m_sampler.get()) });
}

void Renderer::createPipelines(QRhiRenderTarget *target)
{
    // Backends where clip-space "up" and texture "up" disagree (D3D, Metal) need the
    // flipped fullscreen triangle so that texture (0,0) is the same corner everywhere.
    const bool flip = m_rhi->isYUpInNDC() != m_rhi->isYUpInFramebuffer();
    const QShader vert = loadShader(flip ? QStringLiteral("fullscreen_flipped.vert") : QStringLiteral("fullscreen.vert"));

    auto pipeline = [&](const QString &frag, QRhiShaderResourceBindings *srb, QRhiRenderPassDescriptor *rp,
                        int sampleCount) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, vert }, { QRhiShaderStage::Fragment, loadShader(frag) } });
        p->setVertexInputLayout({}); // fullscreen triangle from gl_VertexIndex
        p->setShaderResourceBindings(srb);
        p->setRenderPassDescriptor(rp);
        p->setSampleCount(sampleCount);
        p->create();
        return p;
    };

    // Rebuilt on every initialize(): they reference the SRBs and the target's render pass,
    // both of which can be recreated on resize. Cheap enough at this scale.
    m_gridPipe = pipeline(QStringLiteral("grid.frag"), m_gridSrb.get(), m_offscreenRp.get(), 1);
    m_downPipe = pipeline(QStringLiteral("downsample.frag"), m_downSrb.get(), m_offscreenRp.get(), 1);
    m_blurPipe = pipeline(QStringLiteral("blur.frag"), m_blurSrbs[0].get(), m_offscreenRp.get(), 1);
    m_compositePipe = pipeline(QStringLiteral("composite.frag"), m_compositeSrb.get(),
                               target->renderPassDescriptor(), target->sampleCount());
}

void Renderer::release()
{
    m_gridPipe.reset();
    m_downPipe.reset();
    m_blurPipe.reset();
    m_compositePipe.reset();
    m_gridSrb.reset();
    m_downSrb.reset();
    m_compositeSrb.reset();
    for (auto &s : m_blurSrbs)
        s.reset();
    m_sceneRt.reset();
    m_blurARt.reset();
    m_blurBRt.reset();
    m_offscreenRp.reset();
    m_sceneTex.reset();
    m_blurA.reset();
    m_blurB.reset();
    m_gridUbuf.reset();
    m_downUbuf.reset();
    for (auto &b : m_blurUbufs)
        b.reset();
    m_compositeUbuf.reset();
    m_sampler.reset();
    m_rhi = nullptr;
    m_pixelSize = {};
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Renderer::render(QRhiCommandBuffer *cb, QRhiRenderTarget *target, const OrbitCamera &camera)
{
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    const QSize blurSize = m_blurA->pixelSize();

    // --- Grid uniforms ---
    {
        const QVector3D eye = camera.eye();
        const float aspect = float(m_pixelSize.width()) / qMax(1, m_pixelSize.height());
        const QMatrix4x4 invViewProj = (camera.projection(aspect) * camera.view()).inverted();

        float data[kGridUbufSize / 4] = {};
        std::memcpy(data, invViewProj.constData(), 64);
        float *v = data + 16;
        v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z(); v[3] = 1.0f;
        v += 4;
        // NDC y sign (Vulkan has y down in clip space), fade distance, minor/major cell size
        v[0] = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
        v[1] = qMax(60.0f, camera.distance * 8.0f);
        v[2] = 1.0f;
        v[3] = 10.0f;
        v += 4;
        putVec4(v, QColor("#1C1C20"), 1.0f); v += 4; // sky top
        putVec4(v, QColor("#121214"), 1.0f); v += 4; // sky bottom / horizon
        putVec4(v, QColor("#2E2E34"), 1.0f); v += 4; // minor lines
        putVec4(v, QColor("#46464E"), 1.0f); v += 4; // major lines
        putVec4(v, QColor("#C8434F"), 0.9f); v += 4; // X axis
        putVec4(v, QColor("#3F6FD8"), 0.9f);         // Z axis (navy-blue accent)
        rub->updateDynamicBuffer(m_gridUbuf.get(), 0, kGridUbufSize, data);
    }

    // --- Blur uniforms ---
    {
        const float down[4] = { 1.0f / m_pixelSize.width(), 1.0f / m_pixelSize.height(), 0, 0 };
        rub->updateDynamicBuffer(m_downUbuf.get(), 0, 16, down);
        const float tx = 1.0f / blurSize.width();
        const float ty = 1.0f / blurSize.height();
        for (int i = 0; i < 4; ++i) {
            const float p[4] = { (i % 2 == 0) ? tx : 0.0f, (i % 2 == 0) ? 0.0f : ty, 0, 0 };
            rub->updateDynamicBuffer(m_blurUbufs[i].get(), 0, 16, p);
        }
    }

    // --- Composite uniforms: glass rectangles ---
    {
        float data[kCompositeUbufSize / 4] = {};
        for (int i = 0; i < m_glassRects.size(); ++i) {
            const QRectF &r = m_glassRects[i];
            float *rect = data + 8 + i * 4;
            rect[0] = float(r.x());
            rect[1] = float(r.y());
            rect[2] = float(r.width());
            rect[3] = float(r.height());
        }
        data[0] = float(m_glassRects.size());
        data[1] = m_glassRadius;
        data[2] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
        data[4] = float(m_pixelSize.width());
        data[5] = float(m_pixelSize.height());
        rub->updateDynamicBuffer(m_compositeUbuf.get(), 0, kCompositeUbufSize, data);
    }

    auto fullscreen = [cb](QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb, const QSize &s) {
        cb->setGraphicsPipeline(pipe);
        cb->setViewport({ 0, 0, float(s.width()), float(s.height()) });
        cb->setShaderResources(srb);
        cb->draw(3);
    };
    const QColor black(Qt::black);

    // 1. Scene
    cb->beginPass(m_sceneRt.get(), black, { 1.0f, 0 }, rub);
    fullscreen(m_gridPipe.get(), m_gridSrb.get(), m_pixelSize);
    cb->endPass();

    // 2. Blur: downsample into A, then H (A->B), V (B->A), twice
    cb->beginPass(m_blurARt.get(), black, { 1.0f, 0 });
    fullscreen(m_downPipe.get(), m_downSrb.get(), blurSize);
    cb->endPass();
    for (int i = 0; i < 4; ++i) {
        cb->beginPass((i % 2 == 0) ? m_blurBRt.get() : m_blurARt.get(), black, { 1.0f, 0 });
        fullscreen(m_blurPipe.get(), m_blurSrbs[i].get(), blurSize);
        cb->endPass();
    }

    // 3. Final image
    cb->beginPass(target, black, { 1.0f, 0 });
    fullscreen(m_compositePipe.get(), m_compositeSrb.get(), m_pixelSize);
    cb->endPass();
}
