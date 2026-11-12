#include "renderer.h"

#include "camera.h"
#include "engine/scene/scene.h"

#include <QColor>
#include <QFile>
#include <rhi/qrhi.h>

#include <cmath>
#include <cstring>

namespace
{
constexpr int kBlurDownscale = 4; // blur resolution = target / 4
constexpr int kGridUbufSize = 64 + 8 * 16;
constexpr int kCompositeUbufSize = 2 * 16 + Renderer::MaxGlassRects * 16;
constexpr int kEntityUbufSize = 3 * 64 + 4 * 16; // see mesh.vert
constexpr int kOverlayVertexSize = int(sizeof(OverlayVertex));

// Editor look of meshes.
const QColor kOutlineColor("#8FB0F2"); // palette primary (navy accent)
constexpr float kOutlineWidth = 2.0f;  // logical pixels

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Renderer: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

float *putVec4(float *dst, const QColor &c, float w)
{
    dst[0] = float(c.redF());
    dst[1] = float(c.greenF());
    dst[2] = float(c.blueF());
    dst[3] = w;
    return dst + 4;
}

// Colors picked in the UI are display (sRGB) values; mesh lighting happens in linear space.
float *putLinearColor(float *dst, const QColor &c)
{
    dst[0] = std::pow(float(c.redF()), 2.2f);
    dst[1] = std::pow(float(c.greenF()), 2.2f);
    dst[2] = std::pow(float(c.blueF()), 2.2f);
    dst[3] = 1.0f;
    return dst + 4;
}

float *putMat4(float *dst, const QMatrix4x4 &m)
{
    std::memcpy(dst, m.constData(), 64); // column-major, as std140 expects
    return dst + 16;
}

QRhiVertexInputLayout meshInputLayout()
{
    QRhiVertexInputLayout layout;
    layout.setBindings({ { quint32(sizeof(MeshVertex)) } });
    layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
                           { 0, 1, QRhiVertexInputAttribute::Float3, quint32(sizeof(QVector3D)) } });
    return layout;
}
} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() = default;

void Renderer::setGlassRects(const QList<QRectF> &rects, float cornerRadius)
{
    m_glassRects = rects.mid(0, MaxGlassRects);
    m_glassRadius = cornerRadius;
}

const char *Renderer::backendName() const
{
    return m_rhi ? m_rhi->backendName() : "none";
}

void Renderer::setOverlay(std::vector<OverlayVertex> vertices, float pixelScale)
{
    m_overlay = std::move(vertices);
    m_overlayScale = pixelScale;
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
        m_overlayUbuf = ubuf(16);
        m_layoutUbuf = ubuf(kEntityUbufSize);

        using B = QRhiShaderResourceBinding;
        const auto stages = B::VertexStage | B::FragmentStage;
        m_meshLayoutSrb.reset(m_rhi->newShaderResourceBindings());
        m_meshLayoutSrb->setBindings({ B::uniformBuffer(0, stages, m_layoutUbuf.get()) });
        m_meshLayoutSrb->create();
        m_overlaySrb.reset(m_rhi->newShaderResourceBindings());
        m_overlaySrb->setBindings({ B::uniformBuffer(0, B::VertexStage, m_overlayUbuf.get()) });
        m_overlaySrb->create();
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

    m_sceneDepth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, m_pixelSize));
    m_sceneDepth->create();

    // Render pass descriptors are created once and reused after a resize (same formats),
    // so they always match the pipelines built with them.
    QRhiTextureRenderTargetDescription sceneDesc { QRhiColorAttachment(m_sceneTex.get()) };
    sceneDesc.setDepthStencilBuffer(m_sceneDepth.get());
    m_sceneRt.reset(m_rhi->newTextureRenderTarget(sceneDesc));
    if (!m_sceneRp)
        m_sceneRp.reset(m_sceneRt->newCompatibleRenderPassDescriptor());
    m_sceneRt->setRenderPassDescriptor(m_sceneRp.get());
    m_sceneRt->create();

    auto blurTarget = [this](QRhiTexture *t) {
        std::unique_ptr<QRhiTextureRenderTarget> rt(m_rhi->newTextureRenderTarget({ QRhiColorAttachment(t) }));
        if (!m_blurRp)
            m_blurRp.reset(rt->newCompatibleRenderPassDescriptor());
        rt->setRenderPassDescriptor(m_blurRp.get());
        rt->create();
        return rt;
    };
    m_blurARt = blurTarget(m_blurA.get());
    m_blurBRt = blurTarget(m_blurB.get());

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
    const QShader fullscreen = loadShader(flip ? QStringLiteral("fullscreen_flipped.vert") : QStringLiteral("fullscreen.vert"));

    struct Options
    {
        QRhiVertexInputLayout input;
        QRhiGraphicsPipeline::CullMode cull = QRhiGraphicsPipeline::None;
        bool depth = false;
        bool blend = false;
    };
    auto pipeline = [&](const QShader &vert, const QString &frag, QRhiShaderResourceBindings *srb,
                        QRhiRenderPassDescriptor *rp, int sampleCount, const Options &o = {}) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, vert }, { QRhiShaderStage::Fragment, loadShader(frag) } });
        p->setVertexInputLayout(o.input);
        p->setShaderResourceBindings(srb);
        p->setRenderPassDescriptor(rp);
        p->setSampleCount(sampleCount);
        p->setCullMode(o.cull);
        p->setDepthTest(o.depth);
        p->setDepthWrite(o.depth);
        if (o.blend) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = QRhiGraphicsPipeline::One; // premultiplied alpha
            b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::One;
            b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            p->setTargetBlends({ b });
        }
        p->create();
        return p;
    };

    Options mesh;
    mesh.input = meshInputLayout();
    mesh.depth = true;
    mesh.cull = QRhiGraphicsPipeline::None; // two-sided: imported meshes may have mixed winding
    Options outline = mesh;
    outline.cull = QRhiGraphicsPipeline::Front; // inverted hull: only the back faces
    Options overlay;
    overlay.input.setBindings({ { quint32(kOverlayVertexSize) } });
    overlay.input.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
                                  { 0, 1, QRhiVertexInputAttribute::Float4, quint32(2 * sizeof(float)) } });
    overlay.blend = true;

    // Rebuilt on every initialize(): they reference SRBs and the target's render pass,
    // which can be recreated on resize. Cheap enough at this scale.
    m_gridPipe = pipeline(fullscreen, QStringLiteral("grid.frag"), m_gridSrb.get(), m_sceneRp.get(), 1);
    m_meshPipe = pipeline(loadShader(QStringLiteral("mesh.vert")), QStringLiteral("mesh.frag"), m_meshLayoutSrb.get(),
                          m_sceneRp.get(), 1, mesh);
    m_outlinePipe = pipeline(loadShader(QStringLiteral("outline.vert")), QStringLiteral("outline.frag"),
                             m_meshLayoutSrb.get(), m_sceneRp.get(), 1, outline);
    m_overlayPipe = pipeline(loadShader(QStringLiteral("overlay.vert")), QStringLiteral("overlay.frag"),
                             m_overlaySrb.get(), m_sceneRp.get(), 1, overlay);
    m_downPipe = pipeline(fullscreen, QStringLiteral("downsample.frag"), m_downSrb.get(), m_blurRp.get(), 1);
    m_blurPipe = pipeline(fullscreen, QStringLiteral("blur.frag"), m_blurSrbs[0].get(), m_blurRp.get(), 1);
    m_compositePipe = pipeline(fullscreen, QStringLiteral("composite.frag"), m_compositeSrb.get(),
                               target->renderPassDescriptor(), target->sampleCount());
}

void Renderer::release()
{
    m_meshes.clear();
    m_entities.clear();
    m_gridPipe.reset();
    m_meshPipe.reset();
    m_outlinePipe.reset();
    m_overlayPipe.reset();
    m_downPipe.reset();
    m_blurPipe.reset();
    m_compositePipe.reset();
    m_gridSrb.reset();
    m_downSrb.reset();
    m_compositeSrb.reset();
    m_overlaySrb.reset();
    m_meshLayoutSrb.reset();
    for (auto &s : m_blurSrbs)
        s.reset();
    m_sceneRt.reset();
    m_blurARt.reset();
    m_blurBRt.reset();
    m_sceneRp.reset();
    m_blurRp.reset();
    m_sceneDepth.reset();
    m_sceneTex.reset();
    m_blurA.reset();
    m_blurB.reset();
    m_gridUbuf.reset();
    m_downUbuf.reset();
    for (auto &b : m_blurUbufs)
        b.reset();
    m_compositeUbuf.reset();
    m_overlayUbuf.reset();
    m_overlayVbuf.reset();
    m_layoutUbuf.reset();
    m_sampler.reset();
    m_rhi = nullptr;
    m_pixelSize = {};
}

Renderer::GpuMesh &Renderer::gpuMesh(const MeshPtr &mesh, QRhiResourceUpdateBatch *rub)
{
    auto it = m_meshes.find(mesh.get());
    if (it != m_meshes.end())
        return it->second;

    GpuMesh g;
    g.mesh = mesh;
    g.indexCount = quint32(mesh->indices.size());
    const quint32 vbytes = quint32(mesh->vertices.size() * sizeof(MeshVertex));
    const quint32 ibytes = quint32(mesh->indices.size() * sizeof(quint32));
    g.vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vbytes));
    g.vertices->create();
    g.indices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, ibytes));
    g.indices->create();
    rub->uploadStaticBuffer(g.vertices.get(), mesh->vertices.data());
    rub->uploadStaticBuffer(g.indices.get(), mesh->indices.data());
    return m_meshes.emplace(mesh.get(), std::move(g)).first->second;
}

Renderer::GpuEntity &Renderer::gpuEntity(EntityId id)
{
    auto it = m_entities.find(id);
    if (it != m_entities.end())
        return it->second;

    using B = QRhiShaderResourceBinding;
    GpuEntity g;
    g.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kEntityUbufSize));
    g.uniforms->create();
    g.srb.reset(m_rhi->newShaderResourceBindings());
    g.srb->setBindings({ B::uniformBuffer(0, B::VertexStage | B::FragmentStage, g.uniforms.get()) });
    g.srb->create();
    return m_entities.emplace(id, std::move(g)).first->second;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Renderer::render(QRhiCommandBuffer *cb, QRhiRenderTarget *target, const OrbitCamera &camera, const Scene *scene,
                      EntityId selected)
{
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    const QSize blurSize = m_blurA->pixelSize();
    const QVector3D eye = camera.eye();
    const float aspect = float(m_pixelSize.width()) / qMax(1, m_pixelSize.height());
    const QMatrix4x4 viewProj = camera.projection(aspect) * camera.view();

    // --- Grid uniforms ---
    {
        float data[kGridUbufSize / 4] = {};
        float *v = putMat4(data, viewProj.inverted());
        v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z(); v[3] = 1.0f;
        v += 4;
        // NDC y sign (Vulkan has y down in clip space), fade distance, minor/major cell size
        v[0] = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
        v[1] = qMax(60.0f, camera.distance * 8.0f);
        v[2] = 1.0f;
        v[3] = 10.0f;
        v += 4;
        v = putVec4(v, QColor("#1C1C20"), 1.0f); // sky top
        v = putVec4(v, QColor("#121214"), 1.0f); // sky bottom / horizon
        v = putVec4(v, QColor("#2E2E34"), 1.0f); // minor lines
        v = putVec4(v, QColor("#46464E"), 1.0f); // major lines
        v = putVec4(v, QColor("#C8434F"), 0.9f); // X axis
        putVec4(v, QColor("#3F6FD8"), 0.9f);     // Z axis (navy-blue accent)
        rub->updateDynamicBuffer(m_gridUbuf.get(), 0, kGridUbufSize, data);
    }

    // --- Meshes: upload new ones, per-entity uniforms ---
    struct Draw
    {
        GpuMesh *mesh;
        QRhiShaderResourceBindings *srb;
        bool selected;
    };
    std::vector<Draw> draws;
    {
        const QMatrix4x4 clipViewProj = m_rhi->clipSpaceCorrMatrix() * viewProj;
        std::unordered_map<EntityId, bool> seen;
        if (scene) {
            for (const Entity &e : scene->entities()) {
                if (!e.mesh || e.mesh->indices.empty() || !e.visible)
                    continue;
                GpuMesh &gm = gpuMesh(e.mesh, rub);
                GpuEntity &ge = gpuEntity(e.id);
                seen[e.id] = true;

                const QMatrix4x4 model = e.transform.matrix();
                float data[kEntityUbufSize / 4] = {};
                float *v = putMat4(data, clipViewProj * model);
                v = putMat4(v, model);
                v = putMat4(v, QMatrix4x4(model.normalMatrix()));
                v = putLinearColor(v, e.displayColor);
                v = putVec4(v, kOutlineColor, kOutlineWidth * m_overlayScale);
                v[0] = float(m_pixelSize.width());
                v[1] = float(m_pixelSize.height());
                v += 4;
                v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z();
                rub->updateDynamicBuffer(ge.uniforms.get(), 0, kEntityUbufSize, data);

                draws.push_back({ &gm, ge.srb.get(), e.id == selected });
            }
        }
        // Forget entities that are gone, and meshes no entity uses any more
        // (an undo-able deleted entity still holds its MeshPtr, so its mesh stays).
        std::erase_if(m_entities, [&](const auto &kv) { return !seen.count(kv.first); });
        std::erase_if(m_meshes, [](const auto &kv) { return kv.second.mesh.use_count() == 1; });
    }

    // --- Overlay ---
    const quint32 overlayCount = quint32(m_overlay.size());
    if (overlayCount > 0) {
        const quint32 bytes = overlayCount * kOverlayVertexSize;
        if (!m_overlayVbuf || m_overlayVbuf->size() < bytes) {
            m_overlayVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, qMax(bytes * 2, 4096u)));
            m_overlayVbuf->create();
        }
        rub->updateDynamicBuffer(m_overlayVbuf.get(), 0, bytes, m_overlay.data());
        const float p[4] = { m_pixelSize.width() / m_overlayScale, m_pixelSize.height() / m_overlayScale,
                             m_rhi->isYUpInNDC() ? 1.0f : 0.0f, 0.0f };
        rub->updateDynamicBuffer(m_overlayUbuf.get(), 0, 16, p);
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

    const QRhiViewport fullViewport(0, 0, float(m_pixelSize.width()), float(m_pixelSize.height()));
    auto fullscreen = [cb](QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb, const QSize &s) {
        cb->setGraphicsPipeline(pipe);
        cb->setViewport({ 0, 0, float(s.width()), float(s.height()) });
        cb->setShaderResources(srb);
        cb->draw(3);
    };
    auto drawMesh = [&](QRhiGraphicsPipeline *pipe, const Draw &d) {
        cb->setGraphicsPipeline(pipe);
        cb->setViewport(fullViewport);
        cb->setShaderResources(d.srb);
        const QRhiCommandBuffer::VertexInput vin(d.mesh->vertices.get(), 0);
        cb->setVertexInput(0, 1, &vin, d.mesh->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(d.mesh->indexCount);
    };
    const QColor black(Qt::black);

    // Stats: grid + downsample + 4 blur + composite = 7 fullscreen draws in 7 passes.
    m_stats = {};
    m_stats.meshes = int(draws.size());
    m_stats.renderPasses = 7;
    m_stats.drawCalls = 7 + (overlayCount > 0 ? 1 : 0);
    for (const Draw &d : draws) {
        const int times = d.selected ? 2 : 1; // the outline draws the mesh again
        m_stats.drawCalls += times;
        m_stats.triangles += quint64(d.mesh->indexCount / 3) * quint64(times);
    }

    // 1. Scene: grid as background (no depth), meshes, selection outline, overlay on top
    cb->beginPass(m_sceneRt.get(), black, { 1.0f, 0 }, rub);
    fullscreen(m_gridPipe.get(), m_gridSrb.get(), m_pixelSize);
    for (const Draw &d : draws)
        drawMesh(m_meshPipe.get(), d);
    for (const Draw &d : draws) {
        if (d.selected)
            drawMesh(m_outlinePipe.get(), d);
    }
    if (overlayCount > 0) {
        cb->setGraphicsPipeline(m_overlayPipe.get());
        cb->setViewport(fullViewport);
        cb->setShaderResources(m_overlaySrb.get());
        const QRhiCommandBuffer::VertexInput vin(m_overlayVbuf.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(overlayCount);
    }
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
